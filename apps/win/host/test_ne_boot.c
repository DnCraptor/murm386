/* Trampoline byte-level and relocation checks, no hardware required. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../core/ne_boot.h"
#include "../core/x86bridge.h"

static unsigned rd16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned rd32(const uint8_t *p) { return rd16(p) | (rd16(p+2)<<16); }
int main(void)
{
    uint8_t boot[WIN_NE_BOOT_CAPACITY];
    win_ne_boot_args args = { 0x1234, 8, 0x10, 0x42, 0x18, 0x100, 31, 0x6a30 };
    size_t len = win_ne_boot_make(boot, sizeof(boot), &args);
    assert(len > 70 && len < WIN_NE_BOOT_CAPACITY);
    /* Real-mode entry at RM_SEG:0000. */
    assert(boot[0] == 0xfa && boot[1] == 0x8c && boot[2] == 0xc8);
    assert(!memcmp(boot + 5, "\x0f\x01\x06", 3)); /* SGDT */
    assert(!memcmp(boot + 10, "\x0f\x01\x16", 3));/* LGDT */
    assert(!memcmp(boot + 15, "\x0f\x20\xc0", 3));/* MOV EAX,CR0 */
    assert(!memcmp(boot + 20, "\x0f\x22\xc0", 3));/* MOV CR0,EAX */
    assert(boot[23] == 0xea && rd16(boot+26) == 0x0008);
    unsigned pm = rd16(boot+24) - WIN_NE_BOOT_OFFSET;
    assert(pm == 28);
    assert(boot[pm] == 0xb8 && rd16(boot+pm+1) == 0x18); /* stack sel */
    assert(boot[pm+3] == 0x8e && boot[pm+4] == 0xd0);    /* MOV SS,AX */
    assert(boot[pm+5] == 0xbc && rd16(boot+pm+6) == 0x100);
    assert(boot[pm+12] == 0x9a && rd16(boot+pm+13) == 0x42 &&
           rd16(boot+pm+15) == 0x10);
    /* PM -> real mode jumps back to original boot segment. */
    unsigned pm_exit = pm + 17 + 4 + 3 + 2 + 3;
    assert(boot[pm_exit] == 0xea && rd16(boot+pm_exit+3) == 0x1234);
    unsigned rm = rd16(boot+pm_exit+1);
    assert(rm == pm_exit + 5);
    assert(boot[rm] == 0x8c && boot[rm+1] == 0xc8);
    unsigned saved = rd16(boot+8), fresh = rd16(boot+13);
    assert(rd16(boot+rm+11) == saved);
    assert(rd16(boot+fresh) == args.gdt_limit);
    assert(rd32(boot+fresh+2) == args.gdt_linear);
    assert(boot[rm+13] == 0xba && rd16(boot+rm+14) == WINX86_BRIDGE_PORT);
    assert(boot[rm+16] == 0xb8 && rd16(boot+rm+17) == WINX86_BRIDGE_EXIT);
    assert(boot[rm+19] == 0xef);
    assert(boot[len] == 0xcc);
    args.ne_sp = 8;
    assert(!win_ne_boot_make(boot,sizeof(boot), &args));
    args.ne_sp = 0x100;
    args.gdt_linear = 0x1000000;
    assert(!win_ne_boot_make(boot,sizeof(boot), &args));
    puts("ne boot: PASS");
    return 0;
}
