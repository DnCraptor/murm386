/* TEST.EXE: probes for the startup hang, one function at a time.

   Run 1 showed: label "01 malloc policy" drawn, then core0 locked.  The
   first dos_yield() of the run is the only candidate that can block, and
   dos_yield() runs guest IRQs (arm_elf_service_guest_irq loops until the
   guest IRET returns).  Probes below mask PIC interrupts around one yield.

   Probe 1: yield, unmasked (control)
   Probe 2: yield, all PIC IRQs masked
   Probe 3: yield, IRQ0 (timer) masked only
   Probe 4: yield, IRQ1 (keyboard) masked only
   Probe 5: BIOS int10 gettextinfo, no yield
   Probe 6: physical read of the BDA, no yield
   Probe 7: int33h mouse reset, then yield (as apps/win)
   Probe 8: int10h mode 12h, then yield (as apps/win disp_vga)
   Probe 9: dump SS (via int386x) and IVT[10h]/[21h], no int10 call
   Probe 10: same dump, then gettextinfo (the int10 call from probe 5)

   Each run draws its label, then yields.  The label is drawn only by a
   yield's refresh, so after a lock the screen shows "NN ... start" with no
   "OK".  "NN ... OK" means that yield returned.

   TEST.EXE N   run probe N


   It includes tiny.c, so the static functions of the application (load_model,
   alloc_memory, rain, logo_in, sweep, ...) are called directly. Nothing here
   changes the application itself.

   Screen protocol (row 0):
     "NN name"      step NN is running (written before the call)
     "NN name OK"   step NN returned
   After a hard lock, the last line on row 0 without "OK" is the function
   that hangs.  Drawing steps (rain, logo_in, sweep) overwrite row 0 while they
   run, so run them alone: TEST.EXE 14, TEST.EXE 15, TEST.EXE 16.

   TEST.EXE          run all steps in order, stop at the first hang
   TEST.EXE N        run only step N (1-based)
   Needs TINY.BIN in the current directory for steps 10-13.                 */

#include "dos_video.h"
#include "dos_phys.h"
#include "dos_yield.h"
#define main tiny_app_main
#include "../tiny.c"
#undef main

#include <stdlib.h>


/* ---- breadcrumbs written straight into the VGA backing store ------------ */

static uint8_t *test_video;

static void test_video_init(void)
{
    uint32_t bytes = 0;
    uint8_t *buf = dos_video_get_buffer(&bytes);
    test_video = (buf && bytes >= SCRW * 25 * 4) ? buf : 0;
}

static void test_row(int row, const char *s, unsigned char attr)
{
    int x;
    if (!test_video)
        return;
    for (x = 0; x < SCRW; x++) {
        uint32_t off = ((uint32_t)row * SCRW + (uint32_t)x) * 4u;
        test_video[off] = (unsigned char)(s[x] ? s[x] : ' ');
        test_video[off + 1] = attr;
    }
}

static void test_row0(const char *s, unsigned char attr)
{
    test_row(0, s, attr);
}

static char test_label[SCRW + 1];

/* No yield here: the probe's own yield is the thing under test, and its
   refresh is what makes this label visible. */
static void label(const char *s, unsigned char attr)
{
    int n = 0;
    while (s[n] && n < SCRW) { test_label[n] = s[n]; n++; }
    test_label[n] = 0;
    test_row0(test_label, attr);
}

/* ---- probes -------------------------------------------------------------- */

static void probe1_yield(void)
{
    (void)dos_yield();
}

static void probe2_yield_all_masked(void)
{
    uint8_t m1 = inp(0x21), m2 = inp(0xA1);
    outp(0x21, 0xFF);
    outp(0xA1, 0xFF);
    (void)dos_yield();
    outp(0x21, m1);
    outp(0xA1, m2);
}

static void probe3_yield_irq0_masked(void)
{
    uint8_t m1 = inp(0x21);
    outp(0x21, (uint8_t)(m1 | 0x01));
    (void)dos_yield();
    outp(0x21, m1);
}

static void probe4_yield_irq1_masked(void)
{
    uint8_t m1 = inp(0x21);
    outp(0x21, (uint8_t)(m1 | 0x02));
    (void)dos_yield();
    outp(0x21, m1);
}

/* No yield anywhere in these two: they show whether the system table and
   the BIOS path work at all, independent of dos_yield(). */
static void probe5_bios_no_yield(void)
{
    struct text_info ti;
    gettextinfo(&ti);
}

static void probe6_phys_read_no_yield(void)
{
    (void)dos_phys_read8(0x417u);
}

/* Same first steps as apps/win: BIOS mouse reset (win's input_dos.c runs
   int 33h before its first dos_yield), then a yield. */
static void probe7_mouse_reset_then_yield(void)
{
    union REGS r = {0};
    r.w.ax = 0x0000;
    int386(0x33, &r, &r);
    (void)dos_yield();
}

/* Same mode as apps/win's built-in VGA driver (disp_vga.c, mode 12h),
   set by BIOS, then a yield. */
static void probe8_mode12_then_yield(void)
{
    union REGS r = {0};
    r.w.ax = 0x0012;
    int386(0x10, &r, &r);
    (void)dos_yield();
}

/* Guest-state dump, no int10 call.  SS comes back from int386x() (the
   segment state produced by the DOS call); SP is not exposed by the native
   API (union REGS has no esp), so it is not printed.  IVT entries are read
   from physical 0000:0040 (INT 10h) and 0000:0084 (INT 21h). */
static void dump_guest_state(void)
{
    union REGS r = {0};
    struct SREGS sr;
    char text[SCRW + 1];
    uint16_t ivt10_off = dos_phys_read16(0x40u), ivt10_seg = dos_phys_read16(0x42u);
    uint16_t ivt21_off = dos_phys_read16(0x84u), ivt21_seg = dos_phys_read16(0x86u);

    segread(&sr);
    r.h.ah = 0x30;                     /* DOS version: safe, no guest IRQ work */
    int386x(0x21, &r, &r, &sr);

    sprintf(text, "SS=%04X  (after INT21/30h)", (unsigned)sr.ss);
    test_row(1, text, 0x0F);
    sprintf(text, "IVT10=%04X:%04X  IVT21=%04X:%04X",
            (unsigned)ivt10_seg, (unsigned)ivt10_off,
            (unsigned)ivt21_seg, (unsigned)ivt21_off);
    test_row(2, text, 0x0F);
    (void)dos_yield();
}

static void probe9_dump_only(void)
{
    dump_guest_state();
}

static void probe10_dump_then_int10(void)
{
    struct text_info ti;
    dump_guest_state();
    gettextinfo(&ti);                  /* the call that hangs (probe 5) */
}

static const struct {
    const char *name;
    void (*fn)(void);
} probes[] = {
    { "yield unmasked",       probe1_yield },
    { "yield all masked",     probe2_yield_all_masked },
    { "yield IRQ0 masked",    probe3_yield_irq0_masked },
    { "yield IRQ1 masked",    probe4_yield_irq1_masked },
    { "BIOS int10, no yield", probe5_bios_no_yield },
    { "BDA read, no yield",   probe6_phys_read_no_yield },
    { "mouse reset + yield",  probe7_mouse_reset_then_yield },
    { "mode 12h + yield",     probe8_mode12_then_yield },
    { "guest state dump",     probe9_dump_only },
    { "dump, then int10",     probe10_dump_then_int10 },
};

#define NPROBES ((int)(sizeof(probes) / sizeof(probes[0])))

int main(int argc, char **argv)
{
    int n, i;
    char text[SCRW + 1];

    test_video_init();
    if (argc < 2) {
        label("usage: TEST.EXE 1..10", 0x0C);
        for (i = 0; i < NPROBES; i++) (void)dos_yield();
        return 1;
    }
    n = atoi(argv[1]);
    if (n < 1 || n > NPROBES)
        return 1;

    sprintf(text, "%02d %s start", n, probes[n - 1].name);
    label(text, 0x0E);
    probes[n - 1].fn();
    sprintf(text, "%02d %s OK", n, probes[n - 1].name);
    label(text, 0x0A);
    (void)dos_yield();
    return 0;
}
