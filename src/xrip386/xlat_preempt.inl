/* Included by cpu.c after the core0 NVIC bridge's state. The compiler runs
 * in Thread mode on PSP; its caller and all native handlers use MSP.
 * The same lowest-priority software IRQ switches the two contexts. No
 * guest execution, PIC acknowledge or cache publication in Handler mode. */
static inline bool xrip_bridge_begin(void);

/* Only NO_PAGING builds reserve this section. main.c protects it by using
 * __ram_4_ext_end__ as the core0 stack floor, rather than consuming heap. */
#define XLAT_PSP_WORDS 768u
static uint32_t xlat_psp_stack[XLAT_PSP_WORDS]
    __attribute__((section(".bss.ram_4_ext.xlat_compiler_stack"), aligned(8)));

/* Assembly-visible state: 0 = normal bridge, 1 = enter PSP, 2 = on PSP.
 * The saved pointer addresses r4-r11, EXC_RETURN, optional s16-s31,
 * followed by the hardware exception frame. Zero EXC_RETURN means new. */
static volatile uint32_t xlat_switch_mode __asm__("xlat_switch_mode") __attribute__((used));
static uint32_t *volatile xlat_saved_psp __asm__("xlat_saved_psp") __attribute__((used));
static const volatile uint8_t *volatile xlat_switch_intr
    __asm__("xlat_switch_intr") __attribute__((used));
static volatile bool xlat_worker_done;
static uint32_t xlat_active_us, xlat_active_start;

#if defined(__ARM_FP) && __ARM_FP
/* An FP instruction in Handler mode also completes any lazy FP stacking.
 * Save the high registers when the interrupted context owns an FP frame,
 * as in Cortex-M RTOS context switches. The hardware saves s0-s15/FPSCR. */
#define XLAT_SAVE_MSP_FP "tst lr, #16\n" "it eq\n" "vpusheq {s16-s31}\n"
#define XLAT_LOAD_MSP_FP "tst lr, #16\n" "it eq\n" "vpopeq {s16-s31}\n"
#define XLAT_SAVE_PSP_FP "tst lr, #16\n" "it eq\n" "vstmdbeq r2!, {s16-s31}\n"
#define XLAT_LOAD_PSP_FP "tst lr, #16\n" "it eq\n" "vldmiaeq r2!, {s16-s31}\n"
#else
#define XLAT_SAVE_MSP_FP ""
#define XLAT_LOAD_MSP_FP ""
#define XLAT_SAVE_PSP_FP ""
#define XLAT_LOAD_PSP_FP ""
#endif

/* Entered directly from the naked bridge handler, with the hardware frame
 * already stacked. All r0-r3/r12 are scratch. A very short PRIMASK section
 * protects the stack switch; native IRQs remain enabled during compilation.
 * The bridge is the lowest-priority IRQ; both source contexts are Thread
 * mode, in the same security state. */
static __attribute__((naked, used)) void __not_in_flash_func(xlat_switch_context)(void)
{
    __asm volatile(
        "cpsid i\n"
        "ldr r0, =xlat_switch_mode\n"
        "ldr r1, [r0]\n"
        "cmp r1, #1\n"
        "beq 1f\n"
        /* PSP -> MSP. Preserve the interrupted compiler at its exact PC. */
        "mrs r2, psp\n"
        XLAT_SAVE_PSP_FP
        "stmdb r2!, {r4-r11, lr}\n"
        "ldr r3, =xlat_saved_psp\n"
        "str r2, [r3]\n"
        "ldmia sp!, {r4-r11, r12, lr}\n"
        XLAT_LOAD_MSP_FP
        "b 3f\n"
        /* MSP -> PSP. Do not consume an actual guest IRQ as a start signal:
         * the PIC/CPU request latch remains authoritative. */
        "1:\n"
        "ldr r2, =xlat_switch_intr\n"
        "ldr r2, [r2]\n"
        "ldrb r2, [r2]\n"
        "cmp r2, #0\n"
        "bne 3f\n"
        XLAT_SAVE_MSP_FP
        /* Ten words keep MSP aligned for every native C IRQ on PSP. */
        "stmdb sp!, {r4-r11, r12, lr}\n"
        "mov r3, lr\n"
        "ldr r2, =xlat_saved_psp\n"
        "ldr r2, [r2]\n"
        "ldmia r2!, {r4-r11, lr}\n"
        /* New context: preserve security bits from the real EXC_RETURN,
         * select Thread PSP and a basic frame. Resumes use saved LR. */
        "cmp lr, #0\n"
        "it eq\n"
        "orreq lr, r3, #20\n"
        XLAT_LOAD_PSP_FP
        "msr psp, r2\n"
        "movs r1, #2\n"
        "str r1, [r0]\n"
        "b 4f\n"
        /* Back on MSP, or a start cancelled by a pending guest IRQ.
         * Disable ONLY the software bridge, retaining future pending bits. */
        "3:\n"
        "movs r1, #0\n"
        "str r1, [r0]\n"
        "ldr r2, =xrip_bridge_icer\n"
        "ldr r2, [r2]\n"
        "ldr r3, =xrip_bridge_bit\n"
        "ldr r3, [r3]\n"
        "str r3, [r2]\n"
        "4:\n"
        "dsb sy\n"
        "isb sy\n"
        "cpsie i\n"
        "bx lr\n");
}

#undef XLAT_SAVE_MSP_FP
#undef XLAT_LOAD_MSP_FP
#undef XLAT_SAVE_PSP_FP
#undef XLAT_LOAD_PSP_FP

static __attribute__((noreturn, noinline)) void xlat_worker(void *arg, void (*fn)(void *))
{
    fn(arg);
    /* Result was stored in the unpublished PSRAM job, not on caller MSP.
     * If an IRQ lands here, returning to MSP is also normal completion. */
    __asm volatile("dmb sy" ::: "memory");
    xlat_worker_done = true;
    irq_set_pending((uint)xrip_bridge_irq);
    for (;;)
        __asm volatile("nop" ::: "memory");
}

bool xlat_preempt_available(void)
{
    uint32_t control;
    __asm volatile("mrs %0, control" : "=r"(control));
    /* A privileged Thread MSP caller is required. Native code entered from
     * Handler mode or under host IRQ masks must not switch stacks. */
    return !xlat_switch_mode && !(control & 3u) && xrip_bridge_begin();
}

void xlat_preempt_discard(void)
{
    /* Only called from MSP, after any previous worker has returned there. */
    xlat_saved_psp = NULL;
    xlat_worker_done = false;
}

uint32_t xlat_preempt_time(const volatile uint32_t *clock)
{
    /* The elapsed/start pair changes on each return to MSP. Sample both
     * with the timer in one short critical section, not per instruction. */
    uint32_t mask;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask) :: "memory");
    const uint32_t now = *clock;
    const uint32_t elapsed = xlat_switch_mode == 2 ? xlat_active_us + (now - xlat_active_start) : now;
    __asm volatile("msr primask, %0" :: "r"(mask) : "memory");
    return elapsed;
}

bool xlat_preempt_run(void (*fn)(void *), void *arg,
                      const volatile uint8_t *intr, const volatile uint32_t *clock)
{
    if (!xlat_preempt_available() || !intr || *intr)
        return false;
    if (!xlat_saved_psp) {
        /* Eight hardware words; nine software words (including LR). PSP
         * at exception return is 8-byte aligned, per AAPCS. */
        uint32_t *frame = xlat_psp_stack + XLAT_PSP_WORDS - 8u;
        for (unsigned i = 0; i < 8u; ++i)
            frame[i] = 0;
        frame[0] = (uint32_t)(uintptr_t)arg;
        frame[1] = (uint32_t)(uintptr_t)fn;
        frame[6] = (uint32_t)(uintptr_t)xlat_worker & ~1u;
        frame[7] = 0x01000000u; /* Thumb, no stack-alignment padding */
        for (unsigned i = 1; i <= 9u; ++i)
            frame[-(int)i] = 0;
        xlat_saved_psp = frame - 9;
        xlat_worker_done = false;
        xlat_active_us = 0;
    }
    uint32_t old_psp, old_limit;
    __asm volatile("mrs %0, psp\n\tmrs %1, psplim" : "=r"(old_psp), "=r"(old_limit));
    /* Leave 128 bytes below the hardware limit for the software-saved
     * r4-r11/LR and (if needed) s16-s31 frame. */
    const uint32_t limit = (uint32_t)(uintptr_t)xlat_psp_stack + 128u;
    __asm volatile("msr psplim, %0" :: "r"(limit) : "memory");
    xlat_switch_intr = intr;
    xlat_active_start = *clock;
    xlat_switch_mode = 1;
    __asm volatile("dmb sy" ::: "memory");
    irq_set_pending((uint)xrip_bridge_irq);
    *xrip_bridge_iser = xrip_bridge_bit; /* SDK enable would clear pending */
    __asm volatile("dsb sy\n\tisb sy" ::: "memory");
    while (xlat_switch_mode)
        __asm volatile("nop" ::: "memory");
    xlat_active_us += *clock - xlat_active_start;
    __asm volatile("msr psplim, %0\n\tmsr psp, %1\n\tisb sy"
                   :: "r"(old_limit), "r"(old_psp) : "memory");
    const bool done = xlat_worker_done;
    if (done)
        xlat_preempt_discard();
    return done;
}
