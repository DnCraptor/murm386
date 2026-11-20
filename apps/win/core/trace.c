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

typedef struct {
    uint32_t ms, result, count;
    uint16_t task, service, cs, ip;
} tcall;

typedef struct {
    uint32_t ms;
    uint16_t task, cs, ip;
} tturn;

static tcall *calls;
static char capture_call[64];
static void dump_trace(const char *file, const char *trigger);
static tturn turns[TURNS];
static unsigned ncalls, nturns;     /* how many ever (the ring's next: % size) */

typedef struct {
    uint32_t ms, a, b;
    uint16_t task;
    const char *name;
} tevent;
static tevent events[EVENTS];
static unsigned nevents;

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

    if (capture_call[0]) {
        char native_name[sizeof(capture_call)];
        const char *name = service >= WIN_NE_SERVICE_DLL && service < WIN_NE_SERVICE_UNDEFINED ?
            win_ne_native_name(service, native_name, sizeof(native_name)) : win_ne_service_name(service);
        if (name && !lstrcmpi(name, capture_call))
            dump_trace("WINNCALL.LOG", name);
    }
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
    GetPrivateProfileString("debug", "trace.call", "", capture_call, sizeof(capture_call), "SYSTEM.INI");
    calls = (tcall *)win_alloc(sizeof(tcall) * CALLS);
    if (!calls)
        return;
    memset(calls, 0, sizeof(tcall) * CALLS);
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
    if (ncalls || nturns || nevents)
        dump_trace("WINNTRAC.LOG", NULL);
}

static void dump_trace(const char *file, const char *trigger)
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
        wsprintf(line, "captured after %s\r\n", trigger);
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
    _lclose(f);
}
