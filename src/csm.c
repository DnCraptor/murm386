#include "csm.h"
#include "csm_psg.h"
#include <string.h>

/*
 * Covox Sound Master AYDMA support.
 *
 * The DMA clock is derived from AY8930 tone channel C.  Behaviour and timing
 * here follow the working 86Box Sound Master implementation:
 *   - AY R4/R5: channel-C period / DMA interval
 *   - AY R7 bit2: channel-C tone gate; 0 permits the DMA clock
 *   - AY R15 bit5: 0 enables DMA
 *   - AY R15 bit6: 0 enables terminal-count IRQ
 *   - AY R15 bit7: 0 routes channel C to DMA instead of PSG output
 *   - AY R10 changes the DMA clock multiplier
 *
 * 86Box advances its DMA timer by 16*N*0.1318 us and transfers on every
 * second timer pulse.  Therefore the sample clock is approximately
 * 237102/(N*mult) Hz.  The fixed-point accumulator below reproduces that
 * timing against murm386's 44.1-kHz mixer without a second high-rate IRQ.
 *
 * The 8237 transfer itself stays on core0.  The 44.1-kHz mixer on core1
 * only generates DREQ pulses; each pulse is serviced as exactly one DMA byte
 * by pc_step()/core0.  This keeps 8237 terminal-count timing aligned with
 * the AYDMA sample clock.
 */

#define CSM_CLOCK_NUM 237102u
#define CSM_MIX_RATE  44100u

typedef struct CsmState {
    I8257State *dma;
    PicState2 *pic;

    uint8_t index;
    uint8_t regs[16]; /* bank-A DMA/control cache; PSG model is authoritative */

    uint8_t dma_enabled;
    uint8_t dma_running;
    uint8_t dma_mult;
    uint16_t dma_interval;

    volatile uint8_t pcm_sample;
    uint64_t phase;

    volatile uint8_t irq_asserted;
    uint16_t io_base;
} CsmState;

static CsmState csm;

static inline int csm_irq_enabled(void)
{
    return (csm_psg_get_bank_a_register(15) & 0x40) == 0;
}

static void csm_set_irq(int level)
{
    if (!csm.pic)
        return;
    i8259_set_irq(csm.pic, CSM_IRQ, level);
    __atomic_store_n(&csm.irq_asserted, level ? 1 : 0, __ATOMIC_RELEASE);
}

static void csm_stop_dma(void)
{
    csm.dma_running = 0;
    if (csm.dma)
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
}

static void csm_mode_bits_changed(void)
{
    /* Broderbund software observed on real hardware does not explicitly
     * switch Port B back to PSG after DMA.  86Box uses period <= 1 as the
     * hardware-compatible heuristic observed from that software. */
    if (csm.dma_interval <= 1)
        csm_psg_force_channel_c_output();

    const uint8_t r7 = csm_psg_get_bank_a_register(7);
    const uint8_t r15 = csm_psg_get_bank_a_register(15);

    csm.dma_enabled = (((r15 & 0x20) == 0) &&
                       ((r15 & 0x80) == 0) &&
                       (csm.dma_interval > 1));

    if ((r7 & 0x04) || !csm.dma_enabled) {
        csm_stop_dma();
        csm.phase = 0;
        return;
    }

    csm.dma_running = 1;
    const uint8_t r10 = csm_psg_get_bank_a_register(10);
    csm.dma_mult = ((r10 & 0x10) || r10 == 0) ? 1 : 2;
    csm.phase = 0;
}

static int csm_dma_transfer(void *opaque, int nchan, int dma_pos, int dma_len)
{
    (void)opaque;

    if (nchan != CSM_DMA_CHAN || !csm.dma_running || !csm.dma_enabled || dma_len <= 0) {
        if (csm.dma)
            i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        return dma_pos;
    }

    if (dma_pos >= dma_len) {
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        return dma_len;
    }

    /* One AYDMA clock pulse corresponds to exactly one 8237 transfer.
     * Do not prefetch: Prince of Persia observes DMA terminal-count/status
     * for synchronization, so fetching a block ahead makes TC happen much
     * earlier than the sample actually reaches the DAC. */
    uint8_t sample = 0x80;
    int copied = i8257_dma_read_memory((IsaDma *)csm.dma, nchan,
                                       &sample, dma_pos, 1);
    i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);

    if (copied <= 0)
        return dma_pos;

    __atomic_store_n(&csm.pcm_sample, sample, __ATOMIC_RELEASE);
    ++dma_pos;

    if (dma_pos >= dma_len) {
        if (csm_irq_enabled())
            csm_set_irq(1);

        /* DMA_OVER stops the Sound Master's own AYDMA clock.  The 8237 may
         * auto-initialize its address/count independently, but that must not
         * make the CSM continue clocking a new block without another PSG
         * programming event.  This matches the working 86Box CSM model. */
        csm.dma_running = 0;
        csm.phase = 0;
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);

        return dma_len;
    }

    return dma_pos;
}

void csm_init(I8257State *dma, PicState2 *pic)
{
    memset(&csm, 0, sizeof(csm));
    csm.dma = dma;
    csm.pic = pic;
    csm.pcm_sample = 0x80;
    csm.dma_interval = 1;
    csm.dma_mult = 1;
    csm.io_base = CSM_IO_BASE_DEFAULT;

    /* DMA1 is shared with SB16.  Do not claim it merely because the CSM
     * state object exists; the settings layer binds the selected device. */
    csm_mode_bits_changed();
}

void csm_set_io_base(uint16_t base)
{
    if (base != 0x0220 && base != 0x0240)
        base = CSM_IO_BASE_DEFAULT;
    csm.io_base = base;
}

uint16_t csm_get_io_base(void)
{
    return csm.io_base;
}

void csm_bind_dma(void)
{
    if (!csm.dma)
        return;
    i8257_dma_register_channel((IsaDma *)csm.dma, CSM_DMA_CHAN,
                               csm_dma_transfer, &csm);
}

void csm_deactivate(void)
{
    csm_stop_dma();
    csm.phase = 0;
    if (csm.irq_asserted)
        csm_set_irq(0);
}

static void csm_write_ay(uint8_t reg, uint8_t data)
{
    (void)data;

    /* csm_psg_write_data() has already updated the authoritative AY8930
     * model.  Mirror only bank-A control bytes needed by AYDMA. */
    for (uint8_t r = 0; r < 16; ++r)
        csm.regs[r] = csm_psg_get_bank_a_register(r);

    csm.dma_interval = ((uint16_t)csm.regs[5] << 8) | csm.regs[4];
    if (!csm.dma_interval)
        csm.dma_interval = 1;

    if (reg == 4 || reg == 5 || reg == 7 || reg == 10 || reg == 13 || reg == 14 || reg == 15)
        csm_mode_bits_changed();
}

void csm_write(uint16_t port, uint8_t value)
{
    switch ((uint16_t)(port - csm.io_base) & 0x1f) {
    case 0:
        csm.index = value;
        break;
    case 1:
        if (csm.index < 16)
            csm_write_ay(csm.index, value);
        break;
    case 2:
    case 15:
        __atomic_store_n(&csm.pcm_sample, value, __ATOMIC_RELEASE);
        break;
    case 3:
        if (csm.dma_enabled)
            csm_set_irq(0);
        break;
    default:
        break;
    }
}

uint8_t csm_read(uint16_t port)
{
    switch ((uint16_t)(port - csm.io_base) & 0x1f) {
    case 1:
        return csm_psg_read_data();
    case 4:
    case 5:
    case 14:
        return 0xff;
    default:
        return 0x00;
    }
}

int csm_channel_c_output_enabled(void)
{
    return (csm_psg_get_bank_a_register(15) & 0x80) != 0;
}

void csm_service(void)
{
    /* DMA transfers are now paced one byte per AYDMA clock pulse, so there
     * is no deferred FIFO terminal-count work to service here. */
}

int16_t csm_getsample(void)
{
    if (csm.dma_running && csm.dma_interval > 1) {
        uint64_t threshold = (uint64_t)CSM_MIX_RATE * csm.dma_interval *
                             csm.dma_mult;
        csm.phase += CSM_CLOCK_NUM;

        while (csm.phase >= threshold) {
            csm.phase -= threshold;

            /* Each AYDMA sample clock generates one DMA request.  The
             * existing i8257 implementation only latches DREQ here; core0
             * performs the memory transfer from pc_step(). */
            if (csm.dma_enabled && csm.dma_running)
                i8257_dma_hold_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        }
    }

    uint8_t sample = __atomic_load_n(&csm.pcm_sample, __ATOMIC_ACQUIRE);
    return (int16_t)(((int16_t)sample - 128) * 256);
}
