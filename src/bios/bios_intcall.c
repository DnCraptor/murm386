#include <pico.h>
//#include <hardware/gpio.h>
#include "286/cpu.h"
#include "bios/bios.h"
#include "fdos/fdos.h"
#include <stdio.h>

const char* last_int_call = "NONE";

static bool intcall_waiter(CPU* cpu, bios_callback_params_t* params) {
    //gpio_put(PICO_DEFAULT_LED_PIN, 1);
    last_int_call = params->owner;
    if (!params->done) {
        ifl = 0; /// no more IRQ till return to normal flow
        params->done = true;
        cpu->native_done = true;
    } else {
        ifl = 1; // allow IRQ
    }
    return false; // in a loop on the same CS:IP, no IRET required there
}

extern struct PC* pc;
void pc_step(struct PC* pc, size_t max_ops);
/* proto.h (fdos) not included here - forward-declare the terminate probe. */
extern bool terminate_requested(void);

void bios_intcall(CPU* cpu, uint8_t intnum, const char* owner) {
    uint16_t entry_ax = CPU_AX;
#if WAIT_LOOPS_DETECTION
    uint32_t wait_loops = 0;
    last_int_call = owner;
#endif
    u16 cs = CPU_CS;
    u16 ip = CPU_IP;
    /*
     * Вложенное исполнение гостевого обработчика не должно ни наследовать
     * single-step состояние прерванного потока, ни утащить своё наружу:
     * - tf прерванного потока (например, Norton трассирует через TF, а
     *   ядро посреди этого вызывает INT 28h/INT 23h) не должен пошагово
     *   трассировать вызываемый здесь обработчик;
     * - pending-трап, взведённый ПОСЛЕДНЕЙ инструкцией вложенного потока
     *   (IRET обработчика, восстановивший TF=1 из стекового образа),
     *   не должен выстрелить после первой инструкции внешнего потока.
     * cpu_intcall() сам сбрасывает tf для входа в обработчик (как INT),
     * но образ флагов в стеке хранит исходное значение - его IRET и
     * вернёт; здесь мы страхуем только межпоточную утечку состояния.
     */
    bool old_pending_trap = cpu_pending_trap();
    bool old_irq_shadow = cpu_irq_shadow(cpu);
    cpu_pending_trap_set(false);
    cpu_irq_shadow_set(cpu, false);
    bios_callback_params_t params = {
        .callback = intcall_waiter,
        .expected_cs = 0xFFEF, // just default, may be changed
        .expected_ip = 0x000F, // by set_bios_callback reenter=true
        .done = false,
        .owner = owner
    };
    set_bios_callback(cpu, &params, true);
#if BIOS_DEBUG
    if (intnum != 0x1C) {
        char buf[80];
        u16 new_cs = getmem16(0, (uint16_t) intnum * 4 + 2);
        u16 new_ip = getmem16(0, (uint16_t) intnum * 4);
        int snprintf(char *s, size_t n, const char *fmt, ...);
        snprintf(buf, 79, "INT %02Xh ARM? %04X:%04X->%04X:%04X AX:%04X  ", intnum, cs, ip, params.expected_cs, params.expected_ip, CPU_AX);
        print_line(buf, 0);
    }
#endif
    // to handle IRET by intcall_waiter:
    SET_CS ( params.expected_cs ); // -> FFEFF
    SET_IP ( params.expected_ip );
    bool old_ifl = ifl;
    /* native_done делят request_terminate(), этот waiter и
       cpu_far_call(): состояние ВНЕШНЕГО потока (например, уже
       запрошенная терминация) стекуется, а не затирается. Внутри
       вложенного цикла оно обязано быть false - его выставит только
       наш intcall_waiter. */
    bool old_native_done = cpu->native_done;
    // set CS:IP/flags, prep stack, and on IRET will recover
    cpu_intcall(cpu, intnum);
    cpu->native_done = false;
    while(!params.done) {
#if WAIT_LOOPS_DETECTION
        if (++wait_loops == 256u) {
            char buf[80];
            int snprintf(char *s, size_t n, const char *fmt, ...);
            snprintf(buf, sizeof(buf),
                     "WAIT INT %02X AX=%04X %s",
                     intnum, entry_ax, owner ? owner : "?");
            print_line(buf, 0);
        }
#endif
        /* Break the nested guest burst when a terminate is pending.
           request_terminate() (e.g. LMSW PE=1) latches terminate_flag
           and native_done, but this loop only ends on params.done -
           which our intcall_waiter sets when the guest returns to the
           trap address.  An aborted guest thread never gets there, so
           native_done stays true, i286_step() bails at the top running
           zero instructions, and the loop spins forever.  Leave
           terminate_flag set: the signal cascades up to the owning
           exec_run_process(), which performs the process teardown. */
        if (terminate_requested())
            break;
        pc_step(pc, 4096); /// TODO: a lot of?
    }
    cpu->native_done = old_native_done;
    drop_bios_callback(cpu, &params);
    ifl = old_ifl;
    cpu_pending_trap_set(old_pending_trap);
    cpu_irq_shadow_set(cpu, old_irq_shadow);
    params.done = false;
    // restore initial CS:IP
    SET_CS (cs);
    SET_IP (ip);
    //gpio_put(PICO_DEFAULT_LED_PIN, 0);
}

handler_t handlers[256];

void cpu_err_msg(CPU* cpu, const char* msg) {
    char buf[80];
    uint32_t ip = ((uint32_t)CPU_CS << 4) + CPU_IP;
    uint32_t sp = ((uint32_t)CPU_SS << 4) + CPU_SP;
    uint16_t fw = cpu_getflags(cpu);
    print_line(msg, 1);

    snprintf(buf, sizeof(buf), "AX=%04X BX=%04X CX=%04X DX=%04X ",
             CPU_AX, CPU_BX, CPU_CX, CPU_DX); print_line(buf, 2);
    snprintf(buf, sizeof(buf), "SI=%04X DI=%04X BP=%04X SP=%04X ",
             CPU_SI, CPU_DI, CPU_BP, CPU_SP); print_line(buf, 3);
    snprintf(buf, sizeof(buf), "CS:IP=%04X:%04X  SS:SP=%04X:%04X ",
             CPU_CS, CPU_IP, CPU_SS, CPU_SP); print_line(buf, 4);
    snprintf(buf, sizeof(buf), "DS=%04X ES=%04X FS=%04X GS=%04X ",
             CPU_DS, CPU_ES, xrip_get_seg16(cpu, SEG_FS),
             xrip_get_seg16(cpu, SEG_GS)); print_line(buf, 5);

    snprintf(buf, sizeof(buf), "FLAGS=%04X %c%c%c%c%c%c%c%c%c ",
             fw,
             (fw & 0x0800) ? 'O' : '-',
             (fw & 0x0400) ? 'D' : '-',
             (fw & 0x0200) ? 'I' : '-',
             (fw & 0x0100) ? 'T' : '-',
             (fw & 0x0080) ? 'S' : '-',
             (fw & 0x0040) ? 'Z' : '-',
             (fw & 0x0010) ? 'A' : '-',
             (fw & 0x0004) ? 'P' : '-',
             (fw & 0x0001) ? 'C' : '-');
    print_line(buf, 6);

    snprintf(buf, sizeof(buf), "OP=%02X %02X %02X %02X %02X %02X ",
             read86(ip + 0), read86(ip + 1), read86(ip + 2),
             read86(ip + 3), read86(ip + 4), read86(ip + 5));
    print_line(buf, 7);

    snprintf(buf, sizeof(buf), "STACK=%04X %04X %04X %04X ",
             readw86(sp + 0), readw86(sp + 2),
             readw86(sp + 4), readw86(sp + 6));
    print_line(buf, 8);

    snprintf(buf, sizeof(buf), "RET? IP=%04X CS=%04X FL=%04X ",
             readw86(sp + 0), readw86(sp + 2), readw86(sp + 4));
    print_line(buf, 9);
}
static bool no_handler(CPU* cpu) {
#ifdef NO_HANDLER_DETECTOR
    cpu_err_msg(cpu, "ERROR: no handler defined");
while(1); // remove it
#endif
    return true;
}

void cpu_install_bios_handlers(CPU* cpu) {
    for(int i = 0; i < 256; ++i) {
        if (i == 0x21 || i == 0x29 || i == 0x2f) continue;
        handlers[i] = no_handler;
    }
    handlers[0x00] = bios_00h; // DIVIDE BY ZERO
    handlers[0x05] = bios_05h; // PRINT SCREEN / BOUND EXCEPTION
    handlers[0x08] = bios_08h; // IRQ0: Timer
    handlers[0x09] = bios_09h; // IRQ1: Keyboard
    handlers[0x10] = bios_10h; // VIDEO
    handlers[0x11] = bios_11h; // EQUIPMENT LIST
    handlers[0x12] = bios_12h; // Conventional RAM count
    handlers[0x13] = bios_13h; // DISK
    handlers[0x14] = bios_14h; // SERIAL
    handlers[0x15] = bios_15h; // TSR
    handlers[0x16] = bios_16h; // KEYBOARD
    handlers[0x17] = bios_17h; // PRINTERS
    handlers[0x18] = bios_18h; // BASIC
    handlers[0x19] = bios_19h; // BOOTSTRAP
    handlers[0x1A] = bios_1Ah; // CMOS TIME
    handlers[0x33] = bios_33h; // MS MOUSE
    handlers[0x74] = bios_74h; // IRQ12: PS/2 mouse
    handlers[0xFF] = bios_FFh; // W/A BIOS callback
}

void cpu_install_dos_handlers(CPU* cpu) {
    handlers[0x20] = fdos_20h; // old-style (CP/M) terminate
	pstore16(0x20*4, NATIVE_BIOS_STUB_OFF_FOR(0x20));
	pstore16(0x20*4 + 2, NATIVE_BIOS_STUB_SEG);

    handlers[0x21] = fdos_21h; // main DOS handler
	pstore16(0x21*4, NATIVE_BIOS_STUB_OFF_FOR(0x21));
	pstore16(0x21*4 + 2, NATIVE_BIOS_STUB_SEG);

    handlers[0x25] = fdos_25h; // absolute disk read
	pstore16(0x25*4, NATIVE_BIOS_STUB_OFF_FOR(0x25));
	pstore16(0x25*4 + 2, NATIVE_BIOS_STUB_SEG);

    handlers[0x26] = fdos_26h; // absolute disk write
	pstore16(0x26*4, NATIVE_BIOS_STUB_OFF_FOR(0x26));
	pstore16(0x26*4 + 2, NATIVE_BIOS_STUB_SEG);

    handlers[0x27] = fdos_27h; // old-style TSR
	pstore16(0x27*4, NATIVE_BIOS_STUB_OFF_FOR(0x27));
	pstore16(0x27*4 + 2, NATIVE_BIOS_STUB_SEG);

    handlers[0x28] = fdos_28h; // DOS idle
	pstore16(0x28*4, NATIVE_BIOS_STUB_OFF_FOR(0x28));
	pstore16(0x28*4 + 2, NATIVE_BIOS_STUB_SEG);

    handlers[0x29] = fdos_29h; // fast console output.
	pstore16(0x29*4, NATIVE_BIOS_STUB_OFF_FOR(0x29));
	pstore16(0x29*4 + 2, NATIVE_BIOS_STUB_SEG);

    handlers[0x2F] = fdos_2fh; // XMS
	pstore16(0x2f*4, NATIVE_BIOS_STUB_OFF_FOR(0x2f));
	pstore16(0x2f*4 + 2, NATIVE_BIOS_STUB_SEG);

    /*
     * CP/M CALL-5 gateway. This is NOT a software INT 30h: PSP:0005h does
     * CALL FAR 0000:00C0, and 0000:00C0 (written in PSPInit) is a JMP FAR to
     * the handler-30h native escape stub. Landing there dispatches here. fdos_30h()
     * consumes the far-call frame itself and returns false, so the common
     * IRET path is not applied.
     */
    handlers[0x30] = fdos_30h;
}

#if PDB_DEBUG
void dpb_watch_check_chain(const char *tag);
static void dpb_watch_native_checkpoint(CPU* cpu, const char *where, uint8_t intnum)
{
    static char tags[16][48];
    static unsigned tag_idx;

    char *tag = tags[tag_idx++ & 15];
    snprintf(tag, 48, "NATIVE-%s INT=%02x AX=%04x", where, intnum, CPU_AX);
    dpb_watch_check_chain(tag);
}
#else
#define dpb_watch_native_checkpoint(...)
#endif

/// TODO: inline
bool rp2350_bios_handler(CPU* cpu, uint8_t intnum) {
#if BIOS_DEBUG
    print_line2("BIOS", 0, 8);
#endif
    dpb_watch_native_checkpoint(cpu, "entry", intnum);
    /* The 386 core keeps arithmetic flags lazily (cc.mask) and folds them
     * into cpu->flags only inside get_flags().  The handlers set ZF/CF
     * through the cpu->flags.bits macros and then read them back with
     * cpu_getflags(), which would refold the stale lazy state over them.
     * Fold the pending state first.  An INT instruction already did that
     * when it pushed FLAGS, but code that emulates INT with PUSHF + far
     * CALL/RETF to the vector (Turbo Pascal Dos.Intr, debuggers, TSRs that
     * chain) reaches the stub with flags still lazy: Intr($16) with AH=01h
     * then got ZF from the last "shl bx,1" inside Intr - always "a key is
     * waiting" - and the following AH=00h blocked until a key was pressed. */
    (void)cpu_getflags(cpu);
    bool res = handlers[intnum](cpu);
    dpb_watch_native_checkpoint(cpu, "exit", intnum);
    return res;
}
