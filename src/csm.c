#include "csm.h"
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
    uint8_t regs[16];
    uint8_t regs_bankb[16];
    uint8_t extended_mode;
    uint8_t extended_bank;

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
    return (csm.regs[15] & 0x40) == 0;
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
        csm.regs[15] |= 0x80;

    csm.dma_enabled = (((csm.regs[15] & 0x20) == 0) &&
                       ((csm.regs[15] & 0x80) == 0) &&
                       (csm.dma_interval > 1));

    if ((csm.regs[7] & 0x04) || !csm.dma_enabled) {
        csm_stop_dma();
        csm.phase = 0;
        return;
    }

    csm.dma_running = 1;
    csm.dma_mult = ((csm.regs[10] & 0x10) || csm.regs[10] == 0) ? 1 : 2;
    csm.phase = 0;
}

static void csm_clear_ay_regs(void)
{
    uint8_t mode = csm.regs[13] & 0xf0;
    uint8_t mixer = csm.regs[7];

    memset(csm.regs, 0, sizeof(csm.regs));
    memset(csm.regs_bankb, 0, sizeof(csm.regs_bankb));
    csm.regs[7] = mixer;
    csm.regs[13] = mode;
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

        /* Auto-init is reloaded by i8257_channel_run() after this callback
         * returns terminal count.  Non-auto-init stops until reprogrammed. */
        if (!(csm.dma->regs[CSM_DMA_CHAN].mode & 0x10))
            csm.dma_running = 0;

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
    csm.regs[15] = 0xe0;
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
    switch (reg) {
    case 0:
    case 2:
    case 4:
    case 6:
    case 7:
    case 11:
    case 12:
        if (!csm.extended_bank)
            csm.regs[reg] = data;
        else
            csm.regs_bankb[reg] = data;
        break;

    case 1:
    case 3:
    case 5:
        if (!csm.extended_mode)
            csm.regs[reg] = data & 0x0f;
        else if (!csm.extended_bank)
            csm.regs[reg] = data;
        else
            csm.regs_bankb[reg] = data;
        break;

    case 8:
    case 9:
    case 10:
        if (!csm.extended_mode)
            csm.regs[reg] = data & 0x1f;
        else if (!csm.extended_bank)
            csm.regs[reg] = data & 0x3f;
        else
            csm.regs_bankb[reg] = data;
        break;

    case 13:
        if ((data & 0xe0) == 0xa0) {
            if (!csm.extended_mode)
                csm_clear_ay_regs();
            csm.extended_mode = 1;
            csm.extended_bank = (data & 0x10) ? 1 : 0;
            csm.regs[13] = data;
        } else {
            if (csm.extended_mode)
                csm_clear_ay_regs();
            csm.extended_mode = 0;
            csm.extended_bank = 0;
            csm.regs[13] = data & 0x0f;
        }
        break;

    case 14:
    case 15:
        if (!csm.extended_bank)
            csm.regs[reg] = data;
        else
            csm.regs_bankb[reg] = data;
        break;

    default:
        return;
    }

    if (!csm.extended_bank) {
        if (reg == 4) {
            csm.dma_interval = ((uint16_t)csm.regs[5] << 8) | data;
            if (!csm.dma_interval)
                csm.dma_interval = 1;
        } else if (reg == 5) {
            /* Match 86Box's known-good AYDMA interpretation exactly: the
             * raw byte written forms the high DMA-period byte. */
            csm.dma_interval = ((uint16_t)data << 8) | csm.regs[4];
            if (!csm.dma_interval)
                csm.dma_interval = 1;
        }
    }

    if (!csm.extended_bank && (reg == 7 || reg == 14 || reg == 15))
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
        if (csm.index <= 13) {
            if (!csm.extended_bank || csm.index == 13)
                return csm.regs[csm.index];
            return csm.regs_bankb[csm.index];
        }
        if (csm.index == 14) {
            if (csm.extended_bank)
                return csm.regs_bankb[14];
            if (csm.regs[14] == 0xff)
                return 0xff;
            return (csm.regs[7] & 0x40) ? csm.regs[14] : 0;
        }
        if (csm.index == 15) {
            if (csm.extended_bank)
                return csm.regs_bankb[15];
            if (csm.regs[15] == 0xff)
                return 0xff;
            return (csm.regs[7] & 0x80) ? csm.regs[15] : 0xf0;
        }
        return csm.index;
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
    return (csm.regs[15] & 0x80) != 0;
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
