/*
 * system.c - SYSTEM.DLL, WINN's: Windows 3.x's system driver (SYSTEM.DRV)
 * as a program sees it: InquireSystem (the timer's resolution, a drive's
 * kind), the system timers (CreateSystemTimer: a procedure called as
 * often as its rate in milliseconds, as its task takes its messages;
 * Windows calls them from the timer's interrupt), turned off and on
 * together (Disable/EnableSystemTimers), GetSystemMSecCount, the 80x87's
 * state (the coprocessor is the i386 core's, its state the program's own:
 * Save80x87State gives an FNINIT one, Restore80x87State leaves it as it
 * is), A20_Proc (the line is always on). A native DLL (ddi/windll.h).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "windows.h"
#include "windll.h"

#define NE (__win_dll_ne)

#define TIMERS 8
#define FPU_STATE 94                    /* FSAVE's, in real mode */

static struct {
    HTASK task;                         /* 0: free */
    UINT id;                            /* SetTimer's */
    uint32_t proc;                      /* a Win16 program's FAR procedure */
    void (CALLBACK *native)(void);
    UINT rate;
} timers[TIMERS];
static int disabled;                    /* DisableSystemTimers */

static void CALLBACK tick(HWND h, UINT msg, UINT id, DWORD time)
{
    (void)h; (void)msg; (void)time;
    HTASK t = GetCurrentTask();
    for (int i = 0; i < TIMERS; i++) {
        if (timers[i].task != t || timers[i].id != id)
            continue;
        if (disabled)
            return;
        if (timers[i].native)
            timers[i].native();
        else if (NE && NE->size >= sizeof(win_dll_ne) && NE->call)
            NE->call((uint16_t)t, timers[i].proc, NULL, 0);
        return;
    }
}

/* the timer's number (1..), 0 if there is none free */
static WORD create_timer(UINT rate, uint32_t proc, void (CALLBACK *native)(void))
{
    for (int i = 0; i < TIMERS; i++) {
        if (timers[i].task)
            continue;
        UINT id = SetTimer(0, 0, rate ? rate : 1, tick);
        if (!id)
            return 0;
        timers[i].task = GetCurrentTask();
        timers[i].id = id;
        timers[i].proc = proc;
        timers[i].native = native;
        timers[i].rate = rate;
        return (WORD)(i + 1);
    }
    return 0;
}

static WORD WINAPI sys_create_system_timer(WORD rate, void (CALLBACK *proc)(void))
{
    return proc ? create_timer(rate, 0, proc) : 0;
}

/* 0 when it is gone, the timer itself if there was none such */
static WORD WINAPI sys_kill_system_timer(WORD timer)
{
    if (!timer || timer > TIMERS || !timers[timer - 1].task)
        return timer;
    KillTimer(0, timers[timer - 1].id);
    timers[timer - 1].task = 0;
    return 0;
}

static void WINAPI sys_enable_system_timers(void) { disabled = 0; }
static void WINAPI sys_disable_system_timers(void) { disabled = 1; }
static DWORD WINAPI sys_msec_count(void) { return GetTickCount(); }

/* what (0: the timer's resolution in microseconds, a tick's; 1: the kind of
 * a drive, 0 A:, as GetDriveType; 2: one-drive logic, none) */
static DWORD WINAPI sys_inquire_system(WORD what, WORD drive)
{
    switch (what) {
    case 0: return 54925;
    case 1: return (DWORD)GetDriveType(drive);
    }
    return 0;
}

static WORD WINAPI sys_fpu_save_size(void) { return FPU_STATE; }

/* an FNINIT coprocessor's: control 037Fh (all masked), status 0, all empty */
static void fpu_init_state(uint8_t s[FPU_STATE])
{
    memset(s, 0, FPU_STATE);
    s[0] = 0x7f;
    s[1] = 0x03;
    s[4] = 0xff;
    s[5] = 0xff;
}

static int WINAPI sys_a20_proc(WORD on) { (void)on; return 0; }

/* ================================================================ Win16 programs */

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    switch (ordinal) {
    case 1:                             /* InquireSystem(what, drive) */
        *v = sys_inquire_system(NE->arg16(c, 6), NE->arg16(c, 4));
        return WINDLL_DWORD;
    case 2:                             /* CreateSystemTimer(rate, FARPROC) */
        *v = NE->arg32(c, 4) ? create_timer(NE->arg16(c, 8), NE->arg32(c, 4), NULL) : 0;
        return WINDLL_WORD;
    case 3:
        *v = sys_kill_system_timer(NE->arg16(c, 4));
        return WINDLL_WORD;
    case 4:
        sys_enable_system_timers();
        *v = 0;
        return WINDLL_WORD;
    case 5:
        sys_disable_system_timers();
        *v = 0;
        return WINDLL_WORD;
    case 6:
        *v = sys_msec_count();
        return WINDLL_DWORD;
    case 7:
        *v = FPU_STATE;
        return WINDLL_WORD;
    case 8: {                           /* Save80x87State(LPVOID) */
        uint8_t s[FPU_STATE];
        fpu_init_state(s);
        *v = NE->write(c, NE->arg32(c, 4), s, FPU_STATE) ? 0 : 1;
        return WINDLL_WORD;
    }
    case 9:                             /* Restore80x87State(LPVOID) */
        *v = 0;
        return WINDLL_WORD;
    case 20:
        *v = (uint16_t)sys_a20_proc(NE->arg16(c, 4));
        return WINDLL_WORD;
    }
    return WINDLL_NONE;
}

static void task_end(uint16_t task)
{
    for (int i = 0; i < TIMERS; i++)
        if (timers[i].task == (HTASK)(uintptr_t)task) {
            KillTimer(0, timers[i].id);
            timers[i].task = 0;
        }
}

static const win_dll_export exports[] = {
    { 1, 4, "InquireSystem", (const void *)sys_inquire_system },
    { 2, 6, "CreateSystemTimer", (const void *)sys_create_system_timer },
    { 3, 2, "KillSystemTimer", (const void *)sys_kill_system_timer },
    { 4, 0, "EnableSystemTimers", (const void *)sys_enable_system_timers },
    { 5, 0, "DisableSystemTimers", (const void *)sys_disable_system_timers },
    { 6, 0, "GetSystemMSecCount", (const void *)sys_msec_count },
    { 7, 0, "Get80x87SaveSize", (const void *)sys_fpu_save_size },
    { 8, 4, "Save80x87State", NULL },
    { 9, 4, "Restore80x87State", NULL },
    { 20, 2, "A20_Proc", (const void *)sys_a20_proc },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "SYSTEM",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
