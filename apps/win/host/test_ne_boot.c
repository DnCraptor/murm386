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
    assert(len == WIN_NE_BOOT_CAPACITY);
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
    assert(len == WIN_NE_BOOT_CAPACITY);
    assert(boot[WIN_NE_BOOT_GATEWAY_OFFSET - WIN_NE_BOOT_OFFSET] == 0x2e);
    assert(boot[WIN_NE_BOOT_SCRATCH_OFFSET - 1] == 0xcc);
    assert(boot[WIN_NE_BOOT_SVC_OFFSET] == 0);
    /* Fixed service gateway: save PM stack, enter RM, OUT, re-enter PM. */
    const unsigned gw = WIN_NE_BOOT_GATEWAY_OFFSET - WIN_NE_BOOT_OFFSET;
    assert(boot[gw] == 0x2e && boot[gw+1] == 0xa3);
    assert(rd16(boot+gw+2) == WIN_NE_BOOT_OFFSET+WIN_NE_BOOT_SVC_OFFSET);
    assert(boot[gw+9] == 0x60); /* PUSHA */
    assert(!memcmp(boot+0x99, "\x0f\x20\xc0\x24\xfe\x0f\x22\xc0", 8));
    assert(boot[0xa1] == 0xea && rd16(boot+0xa2) == 0xa6);
    assert(rd16(boot+0xa4) == args.rm_segment);
    assert(boot[0xb4] == 0xba && rd16(boot+0xb5) == WINX86_BRIDGE_PORT);
    assert(boot[0xba] == 0xef); /* callback invoked in RM */
    assert(boot[0xcf] == 0xea && rd16(boot+0xd0) ==
           WIN_NE_BOOT_OFFSET + 0xd4);
    assert(rd16(boot+0xd2) == args.pm_segment);
    assert(boot[0xe2] == 0x61 && boot[0xec] == 0xcb); /* POPA, RETF */
    args.ne_sp = 8;
    assert(!win_ne_boot_make(boot,sizeof(boot), &args));
    args.ne_sp = 0x100;
    args.gdt_linear = 0x1000000;
    assert(!win_ne_boot_make(boot,sizeof(boot), &args));
    puts("ne boot: PASS");
    return 0;
}
