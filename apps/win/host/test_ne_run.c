/* NE programs run end to end on the emulator's own i386 core (src/i386.c,
 * built for the host with x86run/): staging, the RM->PM trampoline, API
 * calls through the RM gateway, exceptions and the yield vector (another
 * task's turn) through the private IDT, back to the DOS context.
 *
 *     ./test_ne_run NEDIALOG.EXE NELOOP-fixture.exe NEFAULT.EXE NEHANG.EXE \
 *                   OWHELLO.EXE OWBUSY.EXE
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "x86run/dos-api.h"
#include "x86run/dos_guest_ptr.h"

#define __arm__ 1
#include "../core/ne_exec.c"

CPU *cpu_new(int gen, CPU_CB **cb);

static PC pc;
PC *get_PC(void) { return &pc; }

/* DOS memory: a bump allocator above the "DOS" area */
static uint32_t next_free = 0x20000;
static int run_alloc(void *o, uint32_t bytes, uint32_t *linear)
{
    (void)o;
    if (!bytes || bytes > 65536 || next_free + bytes > 0xa0000)
        return 0;
    *linear = next_free;
    next_free = (next_free + bytes + 15) & ~15u;
    memset(X86RUN_RAM + *linear, 0xa5, bytes);      /* not zeroed, as DOS */
    return 1;
}
static void run_free(void *o, uint32_t linear) { (void)o; (void)linear; }
static int run_write(void *o, uint32_t linear, const void *data, uint32_t bytes)
{
    (void)o;
    if (linear >= 0x100000 || bytes > 0x100000 - linear)
        return 0;
    memcpy(X86RUN_RAM + linear, data, bytes);
    return 1;
}
const win_ne_guest_io *win_ne_guest_dos_io(void)
{
    static const win_ne_guest_io io = { run_alloc, run_free, run_write };
    return &io;
}

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
    if (mmap(X86RUN_RAM, 0x110000, PROT_READ | PROT_WRITE,
             MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != X86RUN_RAM) {
        perror("mmap");
        return 1;
    }
    CPU_CB *cb;
    pc.cpu = cpu_new(3, &cb);
    /* the "DOS" context the guest runs from and must come back to */
    CPU *cpu = pc.cpu;
    cpu->ext_accessors->set_seg16(cpu, WIN_CS, 0xf000);
    cpu->ext_accessors->set_seg16(cpu, WIN_SS, 0x1000);
    cpu->gprx[regsp].r16 = 0xfffe;
    cpu->ip = cpu->next_ip = 0x1234;

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
        else if (ends_with(f, "NEHANG.EXE"))     /* other tasks run; it ends when told */
            assert(rc == 0 && r.stopped && yields == 3);
        else if (ends_with(f, "NELOOP-fixture.exe"))
            assert(rc == 0 && boxes >= 1);
        else if (ends_with(f, "OWHELLO.EXE"))     /* Open Watcom's start-up code */
            assert(rc == 0 && r.exited && r.value == 3 && boxes == 1 &&
                   !strcmp(last_text, "hello world") && !strcmp(last_caption, "Open Watcom"));
        else if (ends_with(f, "OWBUSY.EXE"))      /* the same result, turns in between */
            assert(rc == 0 && r.exited && yields >= 5 && boxes == 1 &&
                   !strcmp(last_text, "94FB0EA1"));
    }
    puts("ne run on the i386 core: PASS");
    return 0;
}
