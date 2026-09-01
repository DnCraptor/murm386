#include "sdcard.h"

#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
//#define SDCARD_PIO 1
#ifndef SDCARD_PIO
#include "hardware/spi.h"
#else
#include "pio_spi.h"
#endif
#include "hardware/gpio.h"
//#include "hardware/gpio_ex.h"

#include "ff.h"
#include "diskio.h"
#include "profile_subsys.h"
#include "diskcache.h"
#include "mem.h"


/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* MMC/SD command */
#define CMD0	(0)			/* GO_IDLE_STATE */
#define CMD1	(1)			/* SEND_OP_COND (MMC) */
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(8)			/* SEND_IF_COND */
#define CMD9	(9)			/* SEND_CSD */
#define CMD10	(10)		/* SEND_CID */
#define CMD12	(12)		/* STOP_TRANSMISSION */
#define ACMD13	(0x80+13)	/* SD_STATUS (SDC) */
#define CMD16	(16)		/* SET_BLOCKLEN */
#define CMD17	(17)		/* READ_SINGLE_BLOCK */
#define CMD18	(18)		/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)		/* SET_BLOCK_COUNT (MMC) */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)		/* WRITE_BLOCK */
#define CMD25	(25)		/* WRITE_MULTIPLE_BLOCK */
#define CMD32	(32)		/* ERASE_ER_BLK_START */
#define CMD33	(33)		/* ERASE_ER_BLK_END */
#define CMD38	(38)		/* ERASE */
#define CMD55	(55)		/* APP_CMD */
#define CMD58	(58)		/* READ_OCR */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC         0x01            /* MMC ver 3 */
#define CT_SD1         0x02            /* SD ver 1 */
#define CT_SD2         0x04            /* SD ver 2 */
#define CT_SDC         (CT_SD1|CT_SD2) /* SD */
#define CT_BLOCK       0x08            /* Block addressing */

#define CLK_SLOW	(100 * KHZ)
#define CLK_FAST	(30 * MHZ)

static volatile
DSTATUS Stat = STA_NOINIT;	/* Physical drive status */

static
BYTE CardType;			/* Card type flags */

/*
 * Small direct-mapped FatFs disk-I/O cache backed by direct-addressable arenas.
 *
 * Arena 0 is the persistent RAM_4_EXT region in non-VGA256 builds. Arena 1
 * is the old core0 stack region after SP has moved into GFX_BUFFER. Arena 2
 * borrows the payload of the largest free conventional-DOS MCB while guest
 * RAM has a stable native mapping. Keeping
 * the tag/epoch next to each 512-byte payload means growing the cache does not
 * consume scarce ordinary SRAM for a parallel tag table.
 *
 * Writes are true write-through: the physical SD write completes first, then
 * the resident cache line is updated. No disk data exists only in this SRAM.
 */
#define FF_STACK_CACHE_SECTOR_SIZE 512u
#define FF_STACK_CACHE_MAX_ARENAS  3u
#define FF_STACK_CACHE_INVALID_EPOCH 0u

typedef struct __attribute__((aligned(4))) {
    LBA_t tag;
    uint32_t epoch;
    BYTE data[FF_STACK_CACHE_SECTOR_SIZE];
} ff_stack_cache_line_t;

typedef struct {
    ff_stack_cache_line_t *lines;
    uint16_t line_count;
    uint16_t first_slot;
} ff_stack_cache_arena_t;

typedef bool (*ff_stack_cache_read_hook_t)(LBA_t sector, UINT count, BYTE *dst);
typedef void (*ff_stack_cache_store_hook_t)(LBA_t sector, UINT count, const BYTE *src);

static ff_stack_cache_arena_t ff_stack_cache_arena[FF_STACK_CACHE_MAX_ARENAS];
static uint16_t ff_stack_cache_total_lines;
static uint32_t ff_stack_cache_epoch = 1u;

/*
 * Reclaimable L2 cache in memory-mapped QSPI PSRAM. Lines are anchored from
 * the fixed ceiling downward, so shrinking the free tail never moves the
 * surviving lines. A direct mapping keeps all tags out of scarce SRAM; each
 * QSPI line carries its own LBA tag and epoch next to the 512-byte payload.
 *
 * owner_floor[] is deliberately small and generic. XMS may move its floor
 * both ways as EMBs are allocated/freed. The native allocator currently only
 * raises its floor (see task.c), which is conservative but cannot resurrect a
 * line over in-band heap metadata that may still be live.
 */
typedef struct __attribute__((aligned(4))) {
    uint32_t tag;
    uint32_t epoch;
    BYTE data[FF_STACK_CACHE_SECTOR_SIZE];
} ff_qspi_cache_line_t;

#define FF_QSPI_CACHE_INVALID_EPOCH 0u

static uintptr_t ff_qspi_cache_minimum;
static uintptr_t ff_qspi_cache_ceiling;
static uintptr_t ff_qspi_cache_owner_floor[SDCARD_FF_QSPI_OWNER_COUNT];
static uint32_t ff_qspi_cache_max_lines;
static uint32_t ff_qspi_cache_active_lines;
static uint32_t ff_qspi_cache_epoch = 1u;
static bool ff_qspi_cache_enabled;

static inline uintptr_t ff_qspi_cache_effective_floor(void)
{
    uintptr_t floor = ff_qspi_cache_minimum;
    for (uint32_t i = 0; i < SDCARD_FF_QSPI_OWNER_COUNT; ++i)
        if (ff_qspi_cache_owner_floor[i] > floor)
            floor = ff_qspi_cache_owner_floor[i];
    if (floor > ff_qspi_cache_ceiling)
        floor = ff_qspi_cache_ceiling;
    return floor;
}

static inline ff_qspi_cache_line_t *ff_qspi_cache_line(uint32_t slot)
{
    return (ff_qspi_cache_line_t *)(ff_qspi_cache_ceiling -
        (uintptr_t)(slot + 1u) * sizeof(ff_qspi_cache_line_t));
}

static void __not_in_flash_func(ff_qspi_cache_resize)(void)
{
    uint32_t old_active = ff_qspi_cache_active_lines;
    uintptr_t floor;
    uint32_t active;

    if (!ff_qspi_cache_enabled || ff_qspi_cache_max_lines == 0) {
        ff_qspi_cache_active_lines = 0;
        return;
    }

    floor = ff_qspi_cache_effective_floor();
    active = (uint32_t)((ff_qspi_cache_ceiling - floor) /
                        sizeof(ff_qspi_cache_line_t));
    if (active > ff_qspi_cache_max_lines)
        active = ff_qspi_cache_max_lines;

    if (active > old_active) {
        for (uint32_t slot = old_active; slot < active; ++slot)
            ff_qspi_cache_line(slot)->epoch = FF_QSPI_CACHE_INVALID_EPOCH;
    }
    ff_qspi_cache_active_lines = active;
}

static inline uint32_t ff_qspi_cache_slot(LBA_t sector)
{
    if (!ff_qspi_cache_max_lines)
        return 0;
    return (uint32_t)(sector % ff_qspi_cache_max_lines);
}

static ff_qspi_cache_line_t *__not_in_flash_func(ff_qspi_cache_find)(LBA_t sector)
{
    uint32_t slot;
    ff_qspi_cache_line_t *line;

    if (!ff_qspi_cache_enabled || !ff_qspi_cache_active_lines)
        return NULL;
    slot = ff_qspi_cache_slot(sector);
    if (slot >= ff_qspi_cache_active_lines)
        return NULL;
    line = ff_qspi_cache_line(slot);
    if (line->epoch != ff_qspi_cache_epoch || line->tag != sector)
        return NULL;
    return line;
}

static void __not_in_flash_func(ff_qspi_cache_store)(LBA_t sector,
                                                      UINT count,
                                                      const BYTE *src)
{
    if (!ff_qspi_cache_enabled || !ff_qspi_cache_active_lines)
        return;
    for (UINT i = 0; i < count; ++i) {
        LBA_t tag = sector + i;
        uint32_t slot = ff_qspi_cache_slot(tag);
        ff_qspi_cache_line_t *line;
        if (slot >= ff_qspi_cache_active_lines)
            continue;
        line = ff_qspi_cache_line(slot);
        line->epoch = FF_QSPI_CACHE_INVALID_EPOCH;
        dos_api_memcpy(line->data,
                       src + (size_t)i * FF_STACK_CACHE_SECTOR_SIZE,
                       FF_STACK_CACHE_SECTOR_SIZE);
        line->tag = tag;
        line->epoch = ff_qspi_cache_epoch;
    }
}

static void __not_in_flash_func(ff_qspi_cache_invalidate_range)(LBA_t sector,
                                                                 UINT count)
{
    if (!ff_qspi_cache_enabled || !count)
        return;
    for (UINT i = 0; i < count; ++i) {
        LBA_t tag = sector + i;
        uint32_t slot = ff_qspi_cache_slot(tag);
        ff_qspi_cache_line_t *line;
        if (slot >= ff_qspi_cache_active_lines)
            continue;
        line = ff_qspi_cache_line(slot);
        if (line->epoch == ff_qspi_cache_epoch && line->tag == tag)
            line->epoch = FF_QSPI_CACHE_INVALID_EPOCH;
    }
}

static void __not_in_flash_func(ff_qspi_cache_invalidate_all)(void)
{
    if (!ff_qspi_cache_enabled)
        return;
    ++ff_qspi_cache_epoch;
    if (ff_qspi_cache_epoch == FF_QSPI_CACHE_INVALID_EPOCH) {
        ff_qspi_cache_epoch = 1u;
        for (uint32_t slot = 0; slot < ff_qspi_cache_active_lines; ++slot)
            ff_qspi_cache_line(slot)->epoch = FF_QSPI_CACHE_INVALID_EPOCH;
    }
}

static bool __not_in_flash_func(ff_stack_cache_read_disabled)(LBA_t sector,
                                                               UINT count,
                                                               BYTE *dst)
{
    (void)sector;
    (void)count;
    (void)dst;
    return false;
}

static void __not_in_flash_func(ff_stack_cache_store_disabled)(LBA_t sector,
                                                                UINT count,
                                                                const BYTE *src)
{
    (void)sector;
    (void)count;
    (void)src;
}

static ff_stack_cache_read_hook_t ff_stack_cache_read_hook = ff_stack_cache_read_disabled;
static ff_stack_cache_store_hook_t ff_stack_cache_store_hook = ff_stack_cache_store_disabled;

static ff_stack_cache_line_t *__not_in_flash_func(ff_stack_cache_slot)(LBA_t sector)
{
    uint32_t slot;

    if (!ff_stack_cache_total_lines)
        return NULL;
    slot = (uint32_t)(sector % ff_stack_cache_total_lines);
    for (uint32_t i = 0; i < FF_STACK_CACHE_MAX_ARENAS; ++i) {
        ff_stack_cache_arena_t *a = &ff_stack_cache_arena[i];
        if (slot >= a->first_slot &&
            slot < (uint32_t)a->first_slot + a->line_count)
            return &a->lines[slot - a->first_slot];
    }
    return NULL;
}

static ff_stack_cache_line_t *__not_in_flash_func(ff_stack_cache_find)(LBA_t sector)
{
    ff_stack_cache_line_t *line = ff_stack_cache_slot(sector);
    if (!line || line->epoch != ff_stack_cache_epoch || line->tag != sector)
        return NULL;
    return line;
}

static void __not_in_flash_func(ff_stack_cache_store_line)(LBA_t sector,
                                                            const BYTE *src)
{
    ff_stack_cache_line_t *line = ff_stack_cache_slot(sector);
    if (!line)
        return;
    line->epoch = FF_STACK_CACHE_INVALID_EPOCH;
    dos_api_memcpy(line->data, src, FF_STACK_CACHE_SECTOR_SIZE);
    line->tag = sector;
    line->epoch = ff_stack_cache_epoch;
}

static void __not_in_flash_func(ff_stack_cache_invalidate_all)(void)
{
    if (ff_stack_cache_total_lines) {
        ++ff_stack_cache_epoch;
        if (ff_stack_cache_epoch == FF_STACK_CACHE_INVALID_EPOCH) {
            ff_stack_cache_epoch = 1u;
            for (uint32_t i = 0; i < FF_STACK_CACHE_MAX_ARENAS; ++i) {
                ff_stack_cache_arena_t *a = &ff_stack_cache_arena[i];
                for (uint32_t n = 0; n < a->line_count; ++n)
                    a->lines[n].epoch = FF_STACK_CACHE_INVALID_EPOCH;
            }
        }
    }
    ff_qspi_cache_invalidate_all();
}

static void __not_in_flash_func(ff_stack_cache_invalidate_range)(LBA_t sector, UINT count)
{
    ff_qspi_cache_invalidate_range(sector, count);
    if (!ff_stack_cache_total_lines || !count)
        return;
    for (UINT i = 0; i < count; ++i) {
        LBA_t tag = sector + i;
        ff_stack_cache_line_t *line = ff_stack_cache_slot(tag);
        if (line && line->epoch == ff_stack_cache_epoch && line->tag == tag)
            line->epoch = FF_STACK_CACHE_INVALID_EPOCH;
    }
}

static bool __not_in_flash_func(ff_stack_cache_read_active)(LBA_t sector,
                                                             UINT count,
                                                             BYTE *dst)
{
    if (!count)
        return false;

    /* Keep multi-sector reads atomic: prove the complete request is present
     * across SRAM L1 and QSPI L2 before modifying the destination buffer. */
    for (UINT i = 0; i < count; ++i) {
        if (ff_stack_cache_find(sector + i))
            continue;
        if (ff_qspi_cache_find(sector + i) == NULL)
            return false;
    }

    for (UINT i = 0; i < count; ++i) {
        LBA_t tag = sector + i;
        BYTE *out = dst + (size_t)i * FF_STACK_CACHE_SECTOR_SIZE;
        ff_stack_cache_line_t *l1 = ff_stack_cache_find(tag);
        if (l1) {
            dos_api_memcpy(out, l1->data, FF_STACK_CACHE_SECTOR_SIZE);
        } else {
            ff_qspi_cache_line_t *l2 = ff_qspi_cache_find(tag);
            dos_api_memcpy(out, l2->data, FF_STACK_CACHE_SECTOR_SIZE);
            /* Promote an L2 hit into whichever SRAM arena maps this LBA. */
            ff_stack_cache_store_line(tag, l2->data);
        }
    }
    return true;
}

static void __not_in_flash_func(ff_stack_cache_store_active)(LBA_t sector,
                                                              UINT count,
                                                              const BYTE *src)
{
    ff_qspi_cache_store(sector, count, src);
    for (UINT i = 0; i < count; ++i)
        ff_stack_cache_store_line(sector + i,
            src + (size_t)i * FF_STACK_CACHE_SECTOR_SIZE);
}

void sdcard_enable_ff_cache_arena(unsigned arena, void *storage, size_t bytes)
{
    uintptr_t lo;
    size_t line_count;
    ff_stack_cache_line_t *new_lines = NULL;
    uint16_t new_line_count = 0;
    uint32_t first = 0;

    if (arena >= FF_STACK_CACHE_MAX_ARENAS)
        return;

    lo = ((uintptr_t)storage + 3u) & ~(uintptr_t)3u;
    if (storage && bytes > lo - (uintptr_t)storage) {
        bytes -= lo - (uintptr_t)storage;
        line_count = bytes / sizeof(ff_stack_cache_line_t);
        if (line_count > UINT16_MAX)
            line_count = UINT16_MAX;
        new_lines = (ff_stack_cache_line_t *)lo;
        new_line_count = (uint16_t)line_count;
    }

    /* Paragraph-sized DOS changes often leave the usable line geometry
     * unchanged. Do not throw away every arena unless the mapping really
     * changes. */
    if (ff_stack_cache_arena[arena].lines == new_lines &&
        ff_stack_cache_arena[arena].line_count == new_line_count)
        return;

    ff_stack_cache_arena[arena].lines = new_lines;
    ff_stack_cache_arena[arena].line_count = new_line_count;

    for (uint32_t i = 0; i < FF_STACK_CACHE_MAX_ARENAS; ++i) {
        ff_stack_cache_arena[i].first_slot = (uint16_t)first;
        first += ff_stack_cache_arena[i].line_count;
    }
    ff_stack_cache_total_lines = (uint16_t)first;

    /* Geometry changes the global sector->slot mapping, so old lines are
     * invalid even in an arena whose address did not change. */
    ff_stack_cache_epoch = 1u;
    for (uint32_t i = 0; i < FF_STACK_CACHE_MAX_ARENAS; ++i) {
        ff_stack_cache_arena_t *a = &ff_stack_cache_arena[i];
        for (uint32_t n = 0; n < a->line_count; ++n)
            a->lines[n].epoch = FF_STACK_CACHE_INVALID_EPOCH;
    }

    if (ff_stack_cache_total_lines || ff_qspi_cache_enabled) {
        ff_stack_cache_store_hook = ff_stack_cache_store_active;
        ff_stack_cache_read_hook = ff_stack_cache_read_active;
    } else {
        ff_stack_cache_store_hook = ff_stack_cache_store_disabled;
        ff_stack_cache_read_hook = ff_stack_cache_read_disabled;
    }
}

void sdcard_enable_ff_stack_cache(void *storage, size_t bytes)
{
    sdcard_enable_ff_cache_arena(1u, storage, bytes);
}

void sdcard_enable_ff_dos_cache(void *storage, size_t bytes)
{
    sdcard_enable_ff_cache_arena(2u, storage, bytes);
}

void sdcard_enable_ff_qspi_cache(void *minimum, void *ceiling)
{
    uintptr_t lo = ((uintptr_t)minimum + 3u) & ~(uintptr_t)3u;
    uintptr_t hi = (uintptr_t)ceiling & ~(uintptr_t)3u;

    ff_qspi_cache_enabled = false;
    ff_qspi_cache_active_lines = 0;
    ff_qspi_cache_max_lines = 0;
    if (!minimum || !ceiling || hi <= lo + sizeof(ff_qspi_cache_line_t))
        return;

    ff_qspi_cache_minimum = lo;
    ff_qspi_cache_ceiling = hi;
    for (uint32_t i = 0; i < SDCARD_FF_QSPI_OWNER_COUNT; ++i) {
        if (ff_qspi_cache_owner_floor[i] < lo)
            ff_qspi_cache_owner_floor[i] = lo;
        else if (ff_qspi_cache_owner_floor[i] > hi)
            ff_qspi_cache_owner_floor[i] = hi;
    }
    ff_qspi_cache_max_lines =
        (uint32_t)((hi - lo) / sizeof(ff_qspi_cache_line_t));
    ff_qspi_cache_epoch = 1u;
    ff_qspi_cache_enabled = ff_qspi_cache_max_lines != 0;
    ff_qspi_cache_resize();
    if (ff_qspi_cache_enabled) {
        ff_stack_cache_store_hook = ff_stack_cache_store_active;
        ff_stack_cache_read_hook = ff_stack_cache_read_active;
    }
}

void sdcard_ff_qspi_cache_set_floor(sdcard_ff_qspi_owner_t owner, void *floor)
{
    uintptr_t p;
    if ((unsigned)owner >= SDCARD_FF_QSPI_OWNER_COUNT)
        return;
    p = (uintptr_t)floor;
    ff_qspi_cache_owner_floor[owner] = p;
    ff_qspi_cache_resize();
}

#ifdef SDCARD_PIO
pio_spi_inst_t pio_spi = {
		.pio = pio1,
		.sm = -1
};
#endif

static inline uint32_t _millis(void)
{
	return to_ms_since_boot(get_absolute_time());
}

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/

static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 0);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 1);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static void FCLK_SLOW(void)
{
#ifndef SDCARD_PIO
    spi_set_baudrate(SDCARD_SPI_BUS, CLK_SLOW);
#endif
}

static void FCLK_FAST(void)
{
#ifndef SDCARD_PIO
    spi_set_baudrate(SDCARD_SPI_BUS, CLK_FAST);
#endif
}

static void CS_HIGH(void)
{
    cs_deselect(SDCARD_PIN_SPI0_CS);
}

static void CS_LOW(void)
{
    cs_select(SDCARD_PIN_SPI0_CS);
}

/* Initialize MMC interface */
static
void init_spi(void)
{
	/* GPIO pin configuration */
	/* pull up of MISO is MUST (10Kohm external pull up is recommended) */
	/* Set drive strength and slew rate if needed to meet wire condition */
	gpio_init(SDCARD_PIN_SPI0_SCK);
	//gpio_pull_up(SDCARD_PIN_SPI0_SCK);
	//gpio_set_drive_strength(SDCARD_PIN_SPI0_SCK, PADS_BANK0_GPIO0_DRIVE_VALUE_4MA); // 2mA, 4mA (default), 8mA, 12mA
	//gpio_set_slew_rate(SDCARD_PIN_SPI0_SCK, 0); // 0: SLOW (default), 1: FAST

	gpio_init(SDCARD_PIN_SPI0_MISO);
	gpio_pull_up(SDCARD_PIN_SPI0_MISO);
	//gpio_set_schmitt(SDCARD_PIN_SPI0_MISO, 1); // 0: Off, 1: On (default)

	gpio_init(SDCARD_PIN_SPI0_MOSI);
	gpio_pull_up(SDCARD_PIN_SPI0_MOSI);
	//gpio_set_drive_strength(SDCARD_PIN_SPI0_MOSI, PADS_BANK0_GPIO0_DRIVE_VALUE_4MA); // 2mA, 4mA (default), 8mA, 12mA
	//gpio_set_slew_rate(SDCARD_PIN_SPI0_MOSI, 0); // 0: SLOW (default), 1: FAST

	gpio_init(SDCARD_PIN_SPI0_CS);
	//gpio_pull_up(SDCARD_PIN_SPI0_CS);
	//gpio_set_drive_strength(SDCARD_PIN_SPI0_CS, PADS_BANK0_GPIO0_DRIVE_VALUE_4MA); // 2mA, 4mA (default), 8mA, 12mA
	//gpio_set_slew_rate(SDCARD_PIN_SPI0_CS, 0); // 0: SLOW (default), 1: FAST
	gpio_set_dir(SDCARD_PIN_SPI0_CS, GPIO_OUT);

	/* chip _select invalid*/
	CS_HIGH();

#ifndef SDCARD_PIO
	gpio_set_function(SDCARD_PIN_SPI0_SCK, GPIO_FUNC_SPI);
	gpio_set_function(SDCARD_PIN_SPI0_MISO, GPIO_FUNC_SPI);
	gpio_set_function(SDCARD_PIN_SPI0_MOSI, GPIO_FUNC_SPI);

	spi_init(SDCARD_SPI_BUS, CLK_SLOW);

	/* SPI0 parameter config */
	spi_set_format(SDCARD_SPI_BUS,
		8, /* data_bits */
		SPI_CPOL_0, /* cpol */
		SPI_CPHA_0, /* cpha */
		SPI_MSB_FIRST /* order */
	);
#else
    gpio_set_dir(SDCARD_PIN_SPI0_SCK, GPIO_OUT);
    gpio_set_dir(SDCARD_PIN_SPI0_MISO, GPIO_OUT);
    gpio_set_dir(SDCARD_PIN_SPI0_MOSI, GPIO_OUT);

	float clkdiv = 4.0f;
	int cpol = 0;
	int cpha = 0;
	uint cpha0_prog_offs = pio_add_program(pio_spi.pio, &spi_cpha0_program);
	pio_spi.sm = pio_claim_unused_sm(pio_spi.pio, true);
	pio_spi_init(pio_spi.pio, pio_spi.sm,
				cpha0_prog_offs,
				8,       // 8 bits per SPI frame
				clkdiv,
				cpha,
				cpol,
				SDCARD_PIN_SPI0_SCK,
				SDCARD_PIN_SPI0_MOSI,
				SDCARD_PIN_SPI0_MISO
	);
#endif
}

/* Exchange a byte */
static
BYTE xchg_spi (
	BYTE dat	/* Data to send */
)
{
	uint8_t *buff = (uint8_t *) &dat;
#ifndef SDCARD_PIO
	spi_write_read_blocking(SDCARD_SPI_BUS, buff, buff, 1);
#else
	pio_spi_write8_read8_blocking(&pio_spi, buff, buff, 1);
#endif
	return (BYTE) *buff;
}


/* Receive multiple byte */
static
void rcvr_spi_multi (
	BYTE *buff,		/* Pointer to data buffer */
	UINT btr		/* Number of bytes to receive (even number) */
)
{
	uint8_t *b = (uint8_t *) buff;
#ifndef SDCARD_PIO
	spi_read_blocking(SDCARD_SPI_BUS, 0xff, b, btr);
#else
	pio_spi_repeat8_read8_blocking(&pio_spi, 0xff, b, btr);
#endif
}


/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
int wait_ready (	/* 1:Ready, 0:Timeout */
	UINT wt			/* Timeout [ms] */
)
{
	BYTE d;

	uint32_t t = _millis();
	do {
		d = xchg_spi(0xFF);
		/* This loop takes a time. Insert rot_rdq() here for multitask envilonment. */
	} while (d != 0xFF && _millis() < t + wt);	/* Wait for card goes ready or timeout */

	return (d == 0xFF) ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/

static
void deselect (void)
{
	CS_HIGH();		/* Set CS# high */
	xchg_spi(0xFF);	/* Dummy clock (force DO hi-z for multiple slave SPI) */
}



/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/

static
int _select (void)	/* 1:OK, 0:Timeout */
{
	CS_LOW();		/* Set CS# low */
	xchg_spi(0xFF);	/* Dummy clock (force DO enabled) */
	if (wait_ready(500)) return 1;	/* Wait for card ready */

	deselect();
	return 0;	/* Timeout */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/

static
int rcvr_datablock (	/* 1:OK, 0:Error */
	BYTE *buff,			/* Data buffer */
	UINT btr			/* Data block length (byte) */
)
{
	BYTE token;

	const uint32_t timeout = 200;
	uint32_t t = _millis();
	do {							/* Wait for DataStart token in timeout of 200ms */
		token = xchg_spi(0xFF);
		/* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
	} while (token == 0xFF && _millis() < t + timeout);
	if(token != 0xFE) return 0;		/* Function fails if invalid DataStart token or timeout */

	rcvr_spi_multi(buff, btr);		/* Store trailing data to the buffer */
	{
		BYTE crc_hi = xchg_spi(0xFF);	/* CRC16 high byte */
		BYTE crc_lo = xchg_spi(0xFF);	/* CRC16 low  byte */
#ifdef SDCARD_VERIFY_CRC
		/* The data packet always carries a valid CRC16-CCITT in SPI mode,
		   regardless of CMD59. Verify it so a corrupted transfer is not
		   silently accepted as a zero/garbage block. */
		uint16_t rx = ((uint16_t)crc_hi << 8) | crc_lo;
		uint16_t crc = 0;
		UINT i; int k;
		for (i = 0; i < btr; i++) {
			crc ^= (uint16_t)buff[i] << 8;
			for (k = 0; k < 8; k++)
				crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
				                     : (uint16_t)(crc << 1);
		}
		if (crc != rx) return 0;	/* CRC mismatch -> error; caller retries */
#else
		(void)crc_hi; (void)crc_lo;
#endif
	}

	return 1;						/* Function succeeded */
}


/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/

static
BYTE send_cmd (		/* Return value: R1 resp (bit7==1:Failed to send) */
	BYTE cmd,		/* Command index */
	DWORD arg		/* Argument */
)
{
	BYTE n, res;


	if (cmd & 0x80) {	/* Send a CMD55 prior to ACMD<n> */
		cmd &= 0x7F;
		res = send_cmd(CMD55, 0);
		if (res > 1) return res;
	}

	/* Select the card and wait for ready except to stop multiple block read */
	if (cmd != CMD12) {
		deselect();
		if (!_select()) return 0xFF;
	}

	/* Send command packet */
	xchg_spi(0x40 | cmd);				/* Start + command index */
	xchg_spi((BYTE)(arg >> 24));		/* Argument[31..24] */
	xchg_spi((BYTE)(arg >> 16));		/* Argument[23..16] */
	xchg_spi((BYTE)(arg >> 8));			/* Argument[15..8] */
	xchg_spi((BYTE)arg);				/* Argument[7..0] */
	n = 0x01;							/* Dummy CRC + Stop */
	if (cmd == CMD0) n = 0x95;			/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) n = 0x87;			/* Valid CRC for CMD8(0x1AA) */
	xchg_spi(n);

	/* Receive command resp */
	if (cmd == CMD12) xchg_spi(0xFF);	/* Diacard following one byte when CMD12 */
	n = 10;								/* Wait for response (10 bytes max) */
	do {
		res = xchg_spi(0xFF);
	} while ((res & 0x80) && --n);

	return res;							/* Return received response */
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE drv		/* Physical drive number (0) */
)
{
	BYTE n, cmd, ty, ocr[4];
	const uint32_t timeout = 1000; /* Initialization timeout = 1 sec */
	uint32_t t;


	if (drv) return STA_NOINIT;			/* Supports only drive 0 */
	ff_stack_cache_invalidate_all();		/* Media/re-init invalidates cached sectors */
	init_spi();							/* Initialize SPI */
    sleep_ms(10);

	if (Stat & STA_NODISK) return Stat;	/* Is card existing in the soket? */

	FCLK_SLOW();
	CS_LOW();
	for (n = 10; n; n--) xchg_spi(0xFF);	/* Send 80 dummy clocks */

	ty = 0;
	if (send_cmd(CMD0, 0) == 1) {			/* Put the card SPI/Idle state */
		t = _millis();
		if (send_cmd(CMD8, 0x1AA) == 1) {	/* SDv2? */
			for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);	/* Get 32 bit return value of R7 resp */
			if (ocr[2] == 0x01 && ocr[3] == 0xAA) {				/* Is the card supports vcc of 2.7-3.6V? */
				while (_millis() < t + timeout && send_cmd(ACMD41, 1UL << 30)) ;	/* Wait for end of initialization with ACMD41(HCS) */
				if (_millis() < t + timeout && send_cmd(CMD58, 0) == 0) {		/* Check CCS bit in the OCR */
					for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
					ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;	/* Card id SDv2 */
				}
			}
		} else {	/* Not SDv2 card */
			if (send_cmd(ACMD41, 0) <= 1) 	{	/* SDv1 or MMC? */
				ty = CT_SD1; cmd = ACMD41;	/* SDv1 (ACMD41(0)) */
			} else {
				ty = CT_MMC; cmd = CMD1;	/* MMCv3 (CMD1(0)) */
			}
			while (_millis() < t + timeout && send_cmd(cmd, 0)) ;		/* Wait for end of initialization */
			if (_millis() >= t + timeout || send_cmd(CMD16, 512) != 0)	/* Set block length: 512 */
				ty = 0;
		}
	}
	CardType = ty;	/* Card type */
	deselect();

	if (ty) {			/* OK */
		FCLK_FAST();			/* Set fast clock */
		Stat &= ~STA_NOINIT;	/* Clear STA_NOINIT flag */
	} else {			/* Failed */
		Stat = STA_NOINIT;
	}

	return Stat;
}



/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE drv		/* Physical drive number (0) */
)
{
	if (drv) return STA_NOINIT;		/* Supports only drive 0 */

	return Stat;	/* Return disk status */
}



/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                        */
/*-----------------------------------------------------------------------*/

static DRESULT disk_read_impl (
	BYTE drv,		/* Physical drive number (0) */
	BYTE *buff,		/* Pointer to the data buffer to store read data */
	LBA_t sector,	/* Start sector number (LBA) */
	UINT count		/* Number of sectors to read (1..128) */
)
{
	if (drv || !count) return RES_PARERR;		/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* LBA ot BA conversion (byte addressing cards) */

	if (count == 1) {	/* Single sector read */
		int tries = 5;
		while (tries--) {	/* retry on CRC/token failure */
			if ((send_cmd(CMD17, sector) == 0)	/* READ_SINGLE_BLOCK */
				&& rcvr_datablock(buff, 512)) {
				count = 0;
				break;
			}
		}
	}
	else {				/* Multiple sector read */
		if (send_cmd(CMD18, sector) == 0) {	/* READ_MULTIPLE_BLOCK */
			do {
				if (!rcvr_datablock(buff, 512)) break;
				buff += 512;
			} while (--count);
			send_cmd(CMD12, 0);				/* STOP_TRANSMISSION */
		}
	}
	deselect();

	return count ? RES_ERROR : RES_OK;	/* Return result */
}



#if !FF_FS_READONLY && !FF_FS_NORTC
/* get the current time */
DWORD get_fattime (void)
{
	return 0;
}
#endif

#if FF_FS_READONLY == 0
/* Transmit multiple byte */
static
void xmit_spi_multi (
	const BYTE *buff,		/* Pointer to data buffer */
	UINT btx		/* Number of bytes to transmit (even number) */
)
{
	const uint8_t *b = (const uint8_t *) buff;
#ifndef SDCARD_PIO
	spi_write_blocking(SDCARD_SPI_BUS, b, btx);
#else
	pio_spi_write8_blocking(&pio_spi, b, btx);
#endif
}

/*-----------------------------------------------------------------------*/
/* Transmit a data packet to the MMC                                     */
/*-----------------------------------------------------------------------*/

static
int xmit_datablock (	/* 1:OK, 0:Error */
	const BYTE *buff, /* 512 byte data block to be transmitted */
	BYTE token /* Data/Stop token */
)
{
	BYTE resp;
	if (!wait_ready(500)) return 0;
	xchg_spi(token); /* Xmit data token */
	if (token != 0xFD) { /* Is data token */
		xmit_spi_multi(buff, 512); /* Xmit the data block to the MMC */
		xchg_spi(0xFF); /* CRC (Dummy) */
		xchg_spi(0xFF);
		resp = xchg_spi(0xFF); /* Reveive data response */
		if ((resp & 0x1F) != 0x05) /* If not accepted, return with error */
			return 0;
	}
	return 1;
}

/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/

static DRESULT disk_write_impl (
	BYTE drv,			/* Physical drive number (0) */
	const BYTE *buff,	/* Ponter to the data to write */
	LBA_t sector,		/* Start sector number (LBA) */
	UINT count			/* Number of sectors to write (1..128) */
)
{
	if (drv || !count) return RES_PARERR;		/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check drive status */
	if (Stat & STA_PROTECT) return RES_WRPRT;	/* Check write protect */

	if (!(CardType & CT_BLOCK)) sector *= 512;	/* LBA ==> BA conversion (byte addressing cards) */

	if (!_select()) return RES_NOTRDY;

	if (count == 1) {	/* Single sector write */
		if ((send_cmd(CMD24, sector) == 0)	/* WRITE_BLOCK */
			&& xmit_datablock(buff, 0xFE)) {
			count = 0;
		}
	}
	else {				/* Multiple sector write */
		if (CardType & CT_SDC) send_cmd(ACMD23, count);	/* Predefine number of sectors */
		if (send_cmd(CMD25, sector) == 0) {	/* WRITE_MULTIPLE_BLOCK */
			do {
				if (!xmit_datablock(buff, 0xFC)) break;
				buff += 512;
			} while (--count);
			if (!xmit_datablock(0, 0xFD)) count = 1;	/* STOP_TRAN token */
		}
	}
	deselect();

	return count ? RES_ERROR : RES_OK;	/* Return result */
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE drv,		/* Physical drive number (0) */
	BYTE cmd,		/* Control command code */
	void *buff		/* Pointer to the conrtol data */
)
{
	DRESULT res;
	BYTE n, csd[16];
	DWORD *dp, st, ed, csize;


	if (drv) return RES_PARERR;					/* Check parameter */
	if (Stat & STA_NOINIT) return RES_NOTRDY;	/* Check if drive is ready */

	res = RES_ERROR;

	switch (cmd) {
	case CTRL_SYNC :		/* Wait for end of internal write process of the drive */
		if (_select()) res = RES_OK;
		break;

	case GET_SECTOR_COUNT :	/* Get drive capacity in unit of sector (DWORD) */
		if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
			if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
				csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
				*(DWORD*)buff = csize << 10;
			} else {					/* SDC ver 1.XX or MMC ver 3 */
				n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
				csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
				*(DWORD*)buff = csize << (n - 9);
			}
			res = RES_OK;
		}
		break;

	case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (DWORD) */
		if (CardType & CT_SD2) {	/* SDC ver 2.00 */
			if (send_cmd(ACMD13, 0) == 0) {	/* Read SD status */
				xchg_spi(0xFF);
				if (rcvr_datablock(csd, 16)) {				/* Read partial block */
					for (n = 64 - 16; n; n--) xchg_spi(0xFF);	/* Purge trailing data */
					*(DWORD*)buff = 16UL << (csd[10] >> 4);
					res = RES_OK;
				}
			}
		} else {					/* SDC ver 1.XX or MMC */
			if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {	/* Read CSD */
				if (CardType & CT_SD1) {	/* SDC ver 1.XX */
					*(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
				} else {					/* MMC */
					*(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
				}
				res = RES_OK;
			}
		}
		break;

	case CTRL_TRIM :	/* Erase a block of sectors (used when _USE_ERASE == 1) */
		if (!(CardType & CT_SDC)) break;				/* Check if the card is SDC */
		if (disk_ioctl(drv, MMC_GET_CSD, csd)) break;	/* Get CSD */
		if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break;	/* Check if sector erase can be applied to the card */
		dp = buff; st = dp[0]; ed = dp[1];				/* Load sector block */
		if (!(CardType & CT_BLOCK)) {
			st *= 512; ed *= 512;
		}
		if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000)) {	/* Erase sector block */
			res = RES_OK;	/* FatFs does not check result of this command */
			ff_stack_cache_invalidate_range((LBA_t)dp[0], (UINT)(dp[1] - dp[0] + 1u));
		}
		break;

	default:
		res = RES_PARERR;
	}

	deselect();

	return res;
}
/*
 * Timed wrappers around the two FatFS entry points.
 *
 * Every byte the emulator reads from a disk image passes through here,
 * so this is the one place that can answer "how much of core 0 goes to
 * blocking SD I/O?" without instrumenting each f_read site in ide.c,
 * fdd.c and disk.c. It measures the card transfer, not FatFS's own
 * bookkeeping on top of it, so treat the figure as a lower bound.
 */
DRESULT disk_read (BYTE drv, BYTE *buff, LBA_t sector, UINT count)
{
	if (!drv && count && !(Stat & STA_NOINIT) &&
	    ff_stack_cache_read_hook(sector, count, buff))
		return RES_OK;

	/* Served from the slave's PSRAM when the whole request is resident;
	 * roughly 30x faster than the same blocks over SPI. */
	if (dc_read((uint32_t)sector, (uint32_t)count, (uint8_t *)buff)) {
		ff_stack_cache_store_hook(sector, count, buff);
		return RES_OK;
	}

	PROF_T(t_disk);
	DRESULT r = disk_read_impl(drv, buff, sector, count);
#if SUBSYS_PROFILE
	PROF_ADD(t_disk, disk);
	g_prof.disk_ops++;
#endif
	if (r == RES_OK) {
		ff_stack_cache_store_hook(sector, count, buff);
		dc_fill((uint32_t)sector, (uint32_t)count, (const uint8_t *)buff);
	}
	return r;
}

DRESULT disk_write (BYTE drv, const BYTE *buff, LBA_t sector, UINT count)
{
	PROF_T(t_disk);
	DRESULT r = disk_write_impl(drv, buff, sector, count);
#if SUBSYS_PROFILE
	PROF_ADD(t_disk, disk);
	g_prof.disk_ops++;
#endif
	if (r == RES_OK)
		ff_stack_cache_store_hook(sector, count, buff);
	else
		/* A failed multi-sector write may have reached only part of the card. */
		ff_stack_cache_invalidate_range(sector, count);

	/* The optional slave-PSRAM cache keeps its existing invalidate-on-write
	 * contract.  The local SRAM cache above is write-through and stays hot. */
	dc_invalidate((uint32_t)sector, (uint32_t)count);
	return r;
}
