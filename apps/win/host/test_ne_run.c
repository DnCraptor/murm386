/* NE programs run end to end on the emulator's own i386 core (src/i386.c,
 * built for the host with x86run/): staging, the RM->PM trampoline, API
 * calls through the RM gateway, exceptions and the yield vector (another
 * task's turn) through the private IDT, back to the DOS context.
 *
 * The program runs in ring 3: what it may not do (CLI, an OUT of its own,
 * leave protected mode, HLT) faults, a fault with its stack gone still has
 * one (ring 0's), and WINN gets the CPU back.
 *
 *     ./test_ne_run NEDIALOG.EXE NELOOP-fixture.exe NEFAULT.EXE NEHANG.EXE \
 *                   NESTACK.EXE NECLI.EXE NEOUT.EXE NECR0.EXE NEHLT.EXE \
 *                   NEFPU.EXE NEUNDEF.EXE OWHELLO.EXE OWBUSY.EXE OWSEGS.EXE
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "x86run/dos-api.h"
#include "x86run/dos_guest_ptr.h"

#include "../core/ne_exec.h"
#include "../core/ne_guest_api.h"
#include "../core/ne_boot.h"

enum { WIN_CS = 1, WIN_SS = 2 };     /* CPU_ext_accessors segment numbers */

/* the native side */
static char last_text[512], last_caption[128];
static int boxes;
uint32_t GetVersion(void) { return 0x07000a03; }
uint16_t GetCurrentTask(void) { return 0x1234; }
unsigned GetNumTasks(void) { return 2; }
uint32_t GetTickCount(void) { return 1000; }
int GetSystemMetrics(int i) { return i == 0 ? 640 : i; }
uint32_t GetSysColor(int i) { return (uint32_t)i; }
int MessageBox(uint16_t owner, const char *text, const char *caption, unsigned type)
{
    (void)owner; (void)type;
    /* the DOS context is back while the native API runs */
    assert(get_PC()->cpu->ext_accessors->get_seg16(get_PC()->cpu, WIN_CS) == 0xf000);
    snprintf(last_text, sizeof(last_text), "%s", text);
    snprintf(last_caption, sizeof(last_caption), "%s", caption);
    boxes++;
    return 1;
}

/* the other tasks' turn: NEHANG is told to quit at its third */
static int yields, quit_at;
static int run_yield(void)
{
    /* the DOS context is back here too */
    assert(get_PC()->cpu->ext_accessors->get_seg16(get_PC()->cpu, WIN_CS) == 0xf000);
    return ++yields == quit_at;
}

static int ends_with(const char *s, const char *tail)
{
    size_t n = strlen(s), m = strlen(tail);
    return n >= m && !strcmp(s + n - m, tail);
}

int main(int argc, char **argv)
{
    /* the "DOS" context the guest runs from and must come back to */
    CPU *cpu = get_PC()->cpu;

    for (int i = 1; i < argc; i++) {
        const char *f = argv[i];
        win_ne_result r;
        boxes = yields = 0;
        quit_at = ends_with(f, "NEHANG.EXE") ? 3 : 0;
        last_text[0] = 0;
        int rc = win_ne_execute(f, "hello world", 1, run_yield, &r);
        printf("%s: rc %d, value %08x%s%s, yields %d, boxes %d, fault %d at %04x:%04x (seg %u)%s%s\n",
               f, rc, (unsigned)r.value, r.exited ? " (exit)" : "", r.stopped ? " (stopped)" : "",
               yields, boxes, r.fault.vector,
               r.fault.cs, r.fault.ip, r.fault.segment, boxes ? ", last: " : "",
               boxes ? last_text : "");
        fflush(stdout);
        /* back in the DOS context */
        assert(cpu->ext_accessors->get_seg16(cpu, WIN_CS) == 0xf000);
        assert(cpu->ext_accessors->get_seg16(cpu, WIN_SS) == 0x1000);
        assert(cpu->gprx[regsp].r16 == 0xfffe && cpu->ip == 0x1234);
        if (ends_with(f, "NEDIALOG.EXE"))
            assert(rc == 0 && boxes == 1);
        else if (ends_with(f, "NEFAULT.EXE"))
            assert(rc == WIN_NE_EXEC_FAULT && r.fault.vector == 6 && r.fault.segment == 1);
        else if (ends_with(f, "NESTACK.EXE"))     /* #UD with SP 0: the frame in ring 0 */
            assert(rc == WIN_NE_EXEC_FAULT && r.fault.vector == 6 && r.fault.segment == 1 &&
                   (r.fault.cs & 3) == 3 && r.fault.ip == 7);
        else if (ends_with(f, "NECLI.EXE") || ends_with(f, "NEOUT.EXE") ||
                 ends_with(f, "NECR0.EXE") || ends_with(f, "NEHLT.EXE"))  /* #GP */
            assert(rc == WIN_NE_EXEC_FAULT && r.fault.vector == 13 && r.fault.segment == 1 &&
                   (r.fault.cs & 3) == 3 &&
                   r.fault.ip == (ends_with(f, "NEOUT.EXE") ? 11 : 5));
        else if (ends_with(f, "NEHANG.EXE"))     /* other tasks run; it ends when told */
            assert(rc == 0 && r.stopped && yields == 3);
        else if (ends_with(f, "NEFPU.EXE"))       /* the coprocessor, WIN87EM */
            assert(rc == 0 && r.exited && r.value == 117);
        else if (ends_with(f, "NEUNDEF.EXE"))     /* loads; its undefined call ends it */
            assert(rc == WIN_NE_EXEC_UNSUPPORTED && !strcmp(r.undefined, "DDEML.2"));
        else if (ends_with(f, "NELOOP-fixture.exe"))
            assert(rc == 0 && boxes >= 1);
        else if (ends_with(f, "OWHELLO.EXE"))     /* Open Watcom's start-up code */
            assert(rc == 0 && r.exited && r.value == 3 && boxes == 1 &&
                   !strcmp(last_text, "hello world") && !strcmp(last_caption, "Open Watcom"));
        else if (ends_with(f, "OWSEGS.EXE"))      /* far calls between code segments */
            assert(rc == 0 && r.exited && boxes == 1 && !strcmp(last_text, "C8CA765F"));
        else if (ends_with(f, "OWBUSY.EXE"))      /* the same result, turns in between */
            assert(rc == 0 && r.exited && yields >= 5 && boxes == 1 &&
                   !strcmp(last_text, "94FB0EA1"));
    }
    puts("ne run on the i386 core: PASS");
    return 0;
}
