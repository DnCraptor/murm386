/*
 * trace.c - WINN's trace, for a program that hangs or misbehaves where no
 * debugger can look (SYSTEM.INI [debug] trace=1): the last calls programs
 * made of WINN's (the task, the caller's CS:IP, the call, DX:AX; the same
 * call from the same place again and again counted, not repeated) and the
 * places programs' turns ended at (where one that loops without a call
 * is), and the programs' memory. Ctrl+Esc (the Task List) writes them to
 * WINNTRAC.LOG in the Windows directory, the oldest first.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "ne_exec.h"
#include "ne_guest.h"

#define CALLS 256
#define TURNS 32

typedef struct {
    uint32_t ms, result, count;
    uint16_t task, service, cs, ip;
} tcall;

typedef struct {
    uint32_t ms;
    uint16_t task, cs, ip;
} tturn;

static tcall *calls;
static tturn turns[TURNS];
static unsigned ncalls, nturns;     /* how many ever (the ring's next: % size) */

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
static void on_call(uint16_t service, uint16_t cs, uint16_t ip, uint32_t result)
{
    uint16_t task = (uint16_t)GetCurrentTask();
    if (ncalls) {
        tcall *last = &calls[(ncalls - 1) % CALLS];
        if (last->task == task && last->service == service && last->cs == cs &&
            last->ip == ip) {
            last->count++;
            last->result = result;
            last->ms = GetTickCount();
            return;
        }
    }
    tcall *t = &calls[ncalls++ % CALLS];
    t->ms = GetTickCount();
    t->result = result;
    t->count = 1;
    t->task = task;
    t->service = service;
    t->cs = cs;
    t->ip = ip;
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
    if (!calls)
        return;
    char path[160], line[160], name[32];
    win_path("WINNTRAC.LOG", path, sizeof(path));
    HFILE f = _lcreat(path, 0);
    if (f == HFILE_ERROR)
        return;
    wsprintf(line, "WINN trace at %lu ms: calls (ms task caller call -> DX:AX xcount)\r\n",
             (unsigned long)GetTickCount());
    put(f, line);
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
    win_ne_mem_info m;
    win_ne_guest_mem_info(&m);
    wsprintf(line, "memory: programs' extended %lu of %lu, DOS %lu; DOS's largest free %lu "
             "(with UMBs %lu)\r\n", (unsigned long)m.ext_used, (unsigned long)m.ext_size,
             (unsigned long)m.dos_used, (unsigned long)m.dos_free, (unsigned long)m.dos_free_umb);
    put(f, line);
#endif
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
    _lclose(f);
}
