/*
 * trace.c - WINN's trace, for a program that hangs or misbehaves where no
 * debugger can look (SYSTEM.INI [debug] trace=1): the last calls programs
 * made of WINN's (the task, the caller's CS:IP, the call, DX:AX; the same
 * call from the same place again and again counted, not repeated) and the
 * places programs' turns ended at (where one that loops without a call
 * is), and the programs' memory. Ctrl+Alt+Shift+Esc or a normally
 * exiting Win16 task writes the accumulated trace to WINNTRAC.LOG in the
 * Windows directory, the oldest first.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "ne_exec.h"
#include "ne_guest.h"
#include "fontfile.h"

#define CALLS 256
#define TURNS 32
#define EVENTS 128
#define DEBUG_STRINGS 16

typedef struct {
    uint32_t ms, result, count;
    uint16_t task, service, cs, ip;
} tcall;

typedef struct {
    uint32_t ms;
    uint16_t task, cs, ip;
} tturn;

static tcall *calls;
static void dump_trace(const char *file, const char *trigger, BOOL before);
static tturn turns[TURNS];
static unsigned ncalls, nturns;     /* how many ever (the ring's next: % size) */

typedef struct {
    uint32_t ms, a, b;
    uint16_t task;
    const char *name;
} tevent;
static tevent events[EVENTS];
static unsigned nevents;

typedef struct {
    uint32_t ms;
    uint16_t task;
    char text[128];
} tdebug;
static tdebug *debug_strings;
static unsigned ndebug;

void win_trace_debug_string(LPCSTR text)
{
    if (!calls || !text)
        return;
    if (!debug_strings) {
        debug_strings = win_alloc(sizeof(*debug_strings) * DEBUG_STRINGS);
        if (!debug_strings)
            return;
    }
    tdebug *entry = &debug_strings[ndebug++ % DEBUG_STRINGS];
    entry->ms = GetTickCount();
    entry->task = (uint16_t)GetCurrentTask();
    lstrcpyn(entry->text, text, sizeof(entry->text));
}

void win_trace_event(const char *name, uint32_t a, uint32_t b)
{
    if (!calls)
        return;
    tevent *e = &events[nevents++ % EVENTS];
    e->ms = GetTickCount();
    e->task = (uint16_t)GetCurrentTask();
    e->name = name;
    e->a = a;
    e->b = b;
}

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
typedef struct {
    char name[64];
    BOOL native_dll;
} capture_target;

static capture_target capture_after, capture_before;

static void read_capture_target(capture_target *target, LPCSTR key)
{
    GetPrivateProfileString("debug", key, "", target->name, sizeof(target->name), "SYSTEM.INI");
    target->native_dll = strchr(target->name, '.') != NULL;
}

static void capture(const capture_target *target, uint16_t service, BOOL before)
{
    BOOL native_dll = service >= WIN_NE_SERVICE_DLL && service < WIN_NE_SERVICE_UNDEFINED;
    if (!target->name[0] || target->native_dll != native_dll)
        return;
    char native_name[sizeof(target->name)];
    const char *name = native_dll ? win_ne_native_name(service, native_name, sizeof(native_name)) :
                                   win_ne_service_name(service);
    if (name && !lstrcmpi(name, target->name))
        dump_trace(before ? "WINNENTR.LOG" : "WINNCALL.LOG", name, before);
}

static void on_enter(uint16_t service)
{
    capture(&capture_before, service, TRUE);
}

static void on_call(uint16_t service, uint16_t cs, uint16_t ip, uint32_t result)
{
    uint16_t task = (uint16_t)GetCurrentTask();
    tcall *t = ncalls ? &calls[(ncalls - 1) % CALLS] : NULL;
    if (!t || t->task != task || t->service != service || t->cs != cs || t->ip != ip) {
        t = &calls[ncalls++ % CALLS];
        *t = (tcall){ .task = task, .service = service, .cs = cs, .ip = ip };
    }
    t->count++;
    t->result = result;
    t->ms = GetTickCount();

    capture(&capture_after, service, FALSE);
}

static void on_turn(uint16_t cs, uint16_t ip)
{
    tturn *t = &turns[nturns++ % TURNS];
    t->ms = GetTickCount();
    t->task = (uint16_t)GetCurrentTask();
    t->cs = cs;
    t->ip = ip;
}
#endif

void win_trace_init(void)
{
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)  /* Win16 programs run */
    if (calls || !GetPrivateProfileInt("debug", "trace", 0, "SYSTEM.INI"))
        return;
    /* Keep the previous session's dump until there is a new trace to write. */
    read_capture_target(&capture_after, "trace.call");
    read_capture_target(&capture_before, "trace.call.before");
    calls = (tcall *)win_alloc(sizeof(tcall) * CALLS);
    if (!calls)
        return;
    memset(calls, 0, sizeof(tcall) * CALLS);
    if (capture_before.name[0])
        win_ne_trace_enter = on_enter;
    win_ne_trace_call = on_call;
    win_ne_trace_turn = on_turn;
#endif
}

static void put(HFILE f, const char *s)
{
    _lwrite(f, s, (UINT)lstrlen(s));
}

void win_trace_dump(void)
{
    if (ncalls || nturns || nevents || ndebug)
        dump_trace("WINNTRAC.LOG", NULL, FALSE);
}

static void dump_trace(const char *file, const char *trigger, BOOL before)
{
    char path[160], line[160], name[32];
    win_path(file, path, sizeof(path));
    HFILE f = _lcreat(path, 0);
    if (f == HFILE_ERROR)
        return;
    wsprintf(line, "WINN trace at %lu ms: calls (ms task caller call -> DX:AX xcount)\r\n",
             (unsigned long)GetTickCount());
    put(f, line);
    if (trigger) {
        wsprintf(line, "captured %s %s\r\n", before ? "before" : "after", trigger);
        put(f, line);
    }
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
    win_ne_mem_info m;
    win_ne_guest_mem_info(&m);
    wsprintf(line, "memory: programs' extended %lu of %lu, DOS %lu; DOS's largest free %lu "
             "(with UMBs %lu)\r\n", (unsigned long)m.ext_used, (unsigned long)m.ext_size,
             (unsigned long)m.dos_used, (unsigned long)m.dos_free, (unsigned long)m.dos_free_umb);
    put(f, line);
#endif
    font_tt_stats(line);
    put(f, line);
    unsigned first = ncalls > CALLS ? ncalls - CALLS : 0;
    for (unsigned i = first; i < ncalls; i++) {
        const tcall *t = &calls[i % CALLS];
        const char *n = win_ne_service_name(t->service);
        if (!n) {
            wsprintf(name, "#%04X", t->service);
            n = name;
        }
        wsprintf(line, "%8lu %04X %04X:%04X %s -> %08lX x%lu\r\n", (unsigned long)t->ms,
                 t->task, t->cs, t->ip, n, (unsigned long)t->result, (unsigned long)t->count);
        put(f, line);
    }
    put(f, "turns ended at (ms task CS:IP)\r\n");
    first = nturns > TURNS ? nturns - TURNS : 0;
    for (unsigned i = first; i < nturns; i++) {
        const tturn *t = &turns[i % TURNS];
        wsprintf(line, "%8lu %04X %04X:%04X\r\n", (unsigned long)t->ms, t->task, t->cs, t->ip);
        put(f, line);
    }
    put(f, "events (ms task event arg1 arg2)\r\n");
    first = nevents > EVENTS ? nevents - EVENTS : 0;
    for (unsigned i = first; i < nevents; i++) {
        const tevent *e = &events[i % EVENTS];
        wsprintf(line, "%8lu %04X %s %08lX %08lX\r\n",
                 (unsigned long)e->ms, e->task, e->name,
                 (unsigned long)e->a, (unsigned long)e->b);
        put(f, line);
    }
    if (ndebug) {
        put(f, "debug strings (ms task text)\r\n");
        first = ndebug > DEBUG_STRINGS ? ndebug - DEBUG_STRINGS : 0;
        for (unsigned i = first; i < ndebug; i++) {
            const tdebug *entry = &debug_strings[i % DEBUG_STRINGS];
            wsprintf(line, "%8lu %04X %s\r\n", (unsigned long)entry->ms,
                     entry->task, entry->text);
            put(f, line);
        }
    }
    _lclose(f);
}
