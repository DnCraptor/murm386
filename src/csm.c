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
 * The 8237 transfer itself stays on core0 and fills a small FIFO under DREQ
 * flow control, like murm386's working SB16 path.  The 44.1-kHz mixer on
 * core1 consumes that FIFO at the AYDMA sample clock.  The final DMA byte is
 * deliberately not prefetched until all earlier samples have reached the DAC,
 * keeping 8237 terminal count aligned with the audible block boundary.
 */

#define CSM_CLOCK_NUM 237102u
#define CSM_MIX_RATE  44100u
#define CSM_FIFO_LEN  256u
#define CSM_FIFO_MASK (CSM_FIFO_LEN - 1u)

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

    /* Published by core0 as one coherent snapshot.  The 64-bit audio_phase
     * and audio_seen_epoch below are owned exclusively by core1. */
    volatile uint32_t audio_cfg;
    volatile uint32_t audio_epoch;
    uint64_t audio_phase;
    uint32_t audio_seen_epoch;
    uint8_t audio_blocked;

    uint8_t fifo[CSM_FIFO_LEN];
    volatile uint32_t fifo_p;
    volatile uint32_t fifo_q;
    volatile uint8_t producer_done;
    volatile uint8_t consumer_done;
    volatile uint8_t irq_pending;

    volatile uint8_t irq_asserted;
    uint16_t io_base;
} CsmState;

static CsmState csm;

#define CSM_AUDIO_CFG_RUNNING       0x00000001u
#define CSM_AUDIO_CFG_INTERVAL_SHIFT 1u
#define CSM_AUDIO_CFG_MULT_SHIFT    17u

static inline void csm_publish_audio_cfg(void)
{
    uint32_t cfg = 0;
    if (csm.dma_running && csm.dma_enabled && csm.dma_interval > 1) {
        cfg = CSM_AUDIO_CFG_RUNNING |
              ((uint32_t)csm.dma_interval << CSM_AUDIO_CFG_INTERVAL_SHIFT) |
              ((uint32_t)csm.dma_mult << CSM_AUDIO_CFG_MULT_SHIFT);
    }
    __atomic_store_n(&csm.audio_cfg, cfg, __ATOMIC_RELEASE);
    __atomic_add_fetch(&csm.audio_epoch, 1u, __ATOMIC_RELEASE);
}

static inline uint32_t csm_fifo_p(void)
{
    return __atomic_load_n(&csm.fifo_p, __ATOMIC_ACQUIRE);
}

static inline uint32_t csm_fifo_q(void)
{
    return __atomic_load_n(&csm.fifo_q, __ATOMIC_ACQUIRE);
}

static inline void csm_fifo_reset(void)
{
    uint32_t q = csm_fifo_q();
    __atomic_store_n(&csm.fifo_p, q, __ATOMIC_RELEASE);
    __atomic_store_n(&csm.producer_done, 0, __ATOMIC_RELEASE);
}

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
    __atomic_store_n(&csm.consumer_done, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&csm.irq_pending, 0, __ATOMIC_RELEASE);
    csm_fifo_reset();
    csm_publish_audio_cfg();
    if (csm.dma)
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
}

static void csm_mode_bits_changed(void)
{
    const uint8_t r7 = csm_psg_get_bank_a_register(7);
    const uint8_t r15 = csm_psg_get_bank_a_register(15);

    /* AYDMA is armed purely by the Port B routing bits, exactly as the
     * hardware does: R15 bit5 (DMA enable) and bit7 (channel C -> DMA output).
     * The channel-C period R4/R5 only sets the DMA clock RATE -- a 0/1 period
     * means "fastest clock", never "DMA off".  The old `interval > 1` gate
     * (and the interval<=1 Port-B force) broke Prince of Persia's Sound Master
     * autodetect: its 4-byte probe arms DMA+IRQ via R15 while R4/R5 are still
     * 0, so the probe was silently disabled here, no terminal-count IRQ fired,
     * and Prince recorded "no IRQ" ([3326]=0) and skipped all IRQ setup -- so
     * later DAC blocks played once but the IRQ-driven block chain never ran. */
    csm.dma_enabled = (((r15 & 0x20) == 0) &&
                       ((r15 & 0x80) == 0));

    /* Keep the pacing period >= 2 so the mixer's sample-clock threshold can
     * never be zero (which would spin core1); the DMA still runs, just at the
     * fastest representable rate. */
    if (csm.dma_interval < 2)
        csm.dma_interval = 2;

    if ((r7 & 0x04) || !csm.dma_enabled) {
        csm_stop_dma();
        return;
    }

    /* Re-arm on any start that is not interrupting a block still being
     * fetched.  A block that already hit producer terminal count is finished
     * on the 8237 side even while core1 is still draining its FIFO tail, so
     * gating on dma_running alone loses the restart: Prince programs the next
     * block from its terminal-count ISR, before the tail-drain has cleared
     * dma_running, and the start would then skip the FIFO reset / DREQ hold
     * and strand producer_done. */
    const int in_flight = csm.dma_running &&
                          !__atomic_load_n(&csm.producer_done, __ATOMIC_ACQUIRE);
    csm.dma_running = 1;
    const uint8_t r10 = csm_psg_get_bank_a_register(10);
    csm.dma_mult = ((r10 & 0x10) || r10 == 0) ? 1 : 2;
    csm_publish_audio_cfg();

    if (!in_flight) {
        __atomic_store_n(&csm.consumer_done, 0, __ATOMIC_RELEASE);
        __atomic_store_n(&csm.irq_pending, 0, __ATOMIC_RELEASE);
        csm_fifo_reset();
        if (csm.dma)
            i8257_dma_hold_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
    }
}

static int csm_dma_transfer(void *opaque, int nchan, int dma_pos, int dma_len)
{
    (void)opaque;

    if (nchan != CSM_DMA_CHAN || !csm.dma_running || !csm.dma_enabled ||
        __atomic_load_n(&csm.producer_done, __ATOMIC_ACQUIRE) || dma_len <= 0) {
        if (csm.dma)
            i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        return dma_pos;
    }

    if (dma_pos >= dma_len) {
        __atomic_store_n(&csm.producer_done, 1, __ATOMIC_RELEASE);
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        /* 8237 terminal count == Sound Master DMA_OVER: latch the IRQ now, on
         * core0, exactly as hardware/86Box do.  dma_running is left set so
         * core1 can still play out the final FIFO byte; csm_service() retires
         * the consumer once it drains. */
        if (csm_irq_enabled())
            csm_set_irq(1);
        return dma_len;
    }

    uint32_t p = csm_fifo_p();
    uint32_t q = csm_fifo_q();
    uint32_t used = q - p;
    if (used >= CSM_FIFO_LEN - 1u) {
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        return dma_pos;
    }

    int remain = dma_len - dma_pos;
    uint32_t free_count = (CSM_FIFO_LEN - 1u) - used;

    /* Keep the final DMA byte out of the prefetch queue until every earlier
     * sample has actually reached the DAC.  This makes the generic 8237
     * terminal-count transition coincide with the final AYDMA sample to
     * within one sample clock instead of occurring a FIFO ahead. */
    int count;
    if (remain == 1) {
        if (used != 0) {
            i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
            return dma_pos;
        }
        count = 1;
    } else {
        count = remain - 1;
        if ((uint32_t)count > free_count)
            count = (int)free_count;
    }

    if (count <= 0) {
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        return dma_pos;
    }

    uint8_t tmp[CSM_FIFO_LEN];
    int copied = i8257_dma_read_memory((IsaDma *)csm.dma, nchan,
                                       tmp, dma_pos, count);
    if (copied <= 0) {
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        return dma_pos;
    }

    for (int i = 0; i < copied; ++i)
        csm.fifo[(q + (uint32_t)i) & CSM_FIFO_MASK] = tmp[i];
    __atomic_store_n(&csm.fifo_q, q + (uint32_t)copied, __ATOMIC_RELEASE);

    dma_pos += copied;
    if (dma_pos >= dma_len) {
        __atomic_store_n(&csm.producer_done, 1, __ATOMIC_RELEASE);
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
        /* 8237 terminal count == Sound Master DMA_OVER: latch the IRQ now, on
         * core0, exactly as hardware/86Box do.  dma_running is left set so
         * core1 can still play out the final FIFO byte; csm_service() retires
         * the consumer once it drains. */
        if (csm_irq_enabled())
            csm_set_irq(1);
        return dma_len;
    }

    /* Like SB16, DREQ is only FIFO flow control.  Leave it asserted while
     * there is producer space; otherwise core1 re-asserts it after consuming
     * samples. */
    p = csm_fifo_p();
    q = csm_fifo_q();
    if ((q - p) >= CSM_FIFO_LEN - 1u)
        i8257_dma_release_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);

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

    /* Only bank A R7/R14/R15 control the Sound Master DMA/IRQ routing.
     * In AY8930 expanded bank B, R7 is a duty-cycle register and R14/R15
     * are not CSM control ports.  Treating those writes as bank-A control
     * changes can spuriously start/stop AYDMA while the PSG is playing. */
    if (!csm_psg_is_bank_b() && (reg == 7 || reg == 14 || reg == 15))
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
        /* Base+3 is the Sound Master IRQ acknowledge / latch-clear port.
         * Prince writes it to ACK inside its ISR and to clear a stale latch
         * before enabling the IRQ, so it must ALWAYS deassert.  Gating on
         * dma_enabled can strand the edge-triggered PIC line asserted and
         * silently drop the next completion IRQ. */
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
    /* The completion IRQ is latched at terminal count in csm_dma_transfer().
     * Here we only retire the consumer after it has drained the FIFO tail, and
     * only for the block that actually finished (producer_done still set): a
     * late consumer_done from a block already superseded by a restart is
     * dropped so it cannot tear the new block down, and no second IRQ is
     * raised. */
    if (__atomic_exchange_n(&csm.consumer_done, 0, __ATOMIC_ACQ_REL)) {
        if (__atomic_load_n(&csm.producer_done, __ATOMIC_ACQUIRE)) {
            csm.dma_running = 0;
            csm_publish_audio_cfg();
        }
    }

    if (__atomic_exchange_n(&csm.irq_pending, 0, __ATOMIC_ACQ_REL)) {
        if (csm_irq_enabled())
            csm_set_irq(1);
    }
}

int16_t csm_getsample(void)
{
    uint32_t epoch = __atomic_load_n(&csm.audio_epoch, __ATOMIC_ACQUIRE);
    if (epoch != csm.audio_seen_epoch) {
        csm.audio_seen_epoch = epoch;
        csm.audio_phase = 0;
        csm.audio_blocked = 0;
    }

    uint32_t cfg = __atomic_load_n(&csm.audio_cfg, __ATOMIC_ACQUIRE);
    if ((cfg & CSM_AUDIO_CFG_RUNNING) && !csm.audio_blocked) {
        uint16_t interval = (uint16_t)(cfg >> CSM_AUDIO_CFG_INTERVAL_SHIFT);
        uint8_t mult = (uint8_t)((cfg >> CSM_AUDIO_CFG_MULT_SHIFT) & 0x03u);
        uint64_t threshold = (uint64_t)CSM_MIX_RATE * interval * mult;
        csm.audio_phase += CSM_CLOCK_NUM;

        while (csm.audio_phase >= threshold && !csm.audio_blocked) {
            csm.audio_phase -= threshold;

            uint32_t p = csm_fifo_p();
            uint32_t q = csm_fifo_q();
            if (p != q) {
                uint8_t sample = csm.fifo[p & CSM_FIFO_MASK];
                __atomic_store_n(&csm.pcm_sample, sample, __ATOMIC_RELEASE);
                __atomic_store_n(&csm.fifo_p, p + 1u, __ATOMIC_RELEASE);

                p++;
                if (__atomic_load_n(&csm.producer_done, __ATOMIC_ACQUIRE) && p == q) {
                    /* Do not mutate core0-owned dma_running/PSG state here. */
                    csm.audio_blocked = 1;
                    csm.audio_phase = 0;
                    __atomic_store_n(&csm.consumer_done, 1, __ATOMIC_RELEASE);
                    break;
                }

                if (!__atomic_load_n(&csm.producer_done, __ATOMIC_ACQUIRE))
                    i8257_dma_hold_DREQ((IsaDma *)csm.dma, CSM_DMA_CHAN);
            }
        }
    }

    uint8_t sample = __atomic_load_n(&csm.pcm_sample, __ATOMIC_ACQUIRE);
    return (int16_t)(((int16_t)sample - 128) * 256);
}
