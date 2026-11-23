/* TEST.EXE: probes for the startup hang, one function at a time.

   Run 1 showed: label "01 malloc policy" drawn, then core0 locked.  The
   first dos_yield() of the run is the only candidate that can block, and
   dos_yield() runs guest IRQs (arm_elf_service_guest_irq loops until the
   guest IRET returns).  Probes below mask PIC interrupts around one yield.

   Probe 1: yield, unmasked (control)
   Probe 2: yield, all PIC IRQs masked
   Probe 3: yield, IRQ0 (timer) masked only
   Probe 4: yield, IRQ1 (keyboard) masked only

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

static void test_row0(const char *s, unsigned char attr)
{
    int x;
    if (!test_video)
        return;
    for (x = 0; x < SCRW; x++) {
        test_video[x * 4] = (unsigned char)(s[x] ? s[x] : ' ');
        test_video[x * 4 + 1] = attr;
    }
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

static const struct {
    const char *name;
    void (*fn)(void);
} probes[] = {
    { "yield unmasked",       probe1_yield },
    { "yield all masked",     probe2_yield_all_masked },
    { "yield IRQ0 masked",    probe3_yield_irq0_masked },
    { "yield IRQ1 masked",    probe4_yield_irq1_masked },
};

#define NPROBES ((int)(sizeof(probes) / sizeof(probes[0])))

int main(int argc, char **argv)
{
    int n, i;
    char text[SCRW + 1];

    test_video_init();
    if (argc < 2) {
        label("usage: TEST.EXE 1..4", 0x0C);
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
