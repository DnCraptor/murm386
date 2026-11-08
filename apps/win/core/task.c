/*
 * task.c - tasks and cooperative scheduling (Win16 model).
 *
 * Every application runs as a task with its own stack. A task gives up the
 * CPU only inside GetMessage/WaitMessage (when it has nothing to do),
 * PeekMessage with nothing found, Yield and WinExec; the scheduler then
 * runs the next task that has work. The kernel task (the program's own
 * stack) starts the shell, reaps finished tasks and idles.
 *
 * On RP2350 the firmware guards the native stack with MSPLIM, so a switch
 * moves SP and MSPLIM together with interrupts masked. The code is built
 * for ARMv6-M (Thumb-1), so the two MSPLIM instructions are emitted as
 * raw encodings. The host build uses ucontext.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"
#if defined(__arm__) || defined(__thumb__)
#include "dos-api.h"
#include "ne.h"
#include "ne_exec.h"
#endif

#if !(defined(__arm__) || defined(__thumb__))
#define TASK_HOST 1
#include <ucontext.h>
#endif

struct wtask_ctx {
#ifdef TASK_HOST
    ucontext_t uc;
#else
    uintptr_t sp;
#endif
    uintptr_t limit;            /* MSPLIM value while the task runs */
};

static wtask kernel_task;
static wtask *tasks = &kernel_task;   /* list, kernel first */
static wtask *cur = &kernel_task;
static HTASK shell_task;

/* The physical stack remains the sender's during synchronous SendMessage.
 * The owner is task-local, so task_yield() cannot leak it to another task. */
wtask *task_current(void) { return cur->dispatch_owner ? cur->dispatch_owner : cur; }
wtask *task_set_dispatch_owner(wtask *owner)
{
    wtask *previous = cur->dispatch_owner;
    cur->dispatch_owner = owner;
    return previous;
}
wtask *task_kernel(void) { return &kernel_task; }
wtask *task_from_handle(HTASK h)
{
    if (!h)
        return NULL;
    if (h == kernel_task.h)
        return &kernel_task;
    return (wtask *)win_handle_get(h, WOBJ_TASK);
}

HTASK WINAPI GetCurrentTask(void) { return task_current()->h; }

BOOL WINAPI IsTask(HTASK h)
{
    wtask *t = task_from_handle(h);
    return t && t->state != WTASK_DEAD;
}

UINT WINAPI GetNumTasks(void)
{
    UINT n = 0;
    for (wtask *t = tasks; t; t = t->next)
        if (t != &kernel_task && t->state != WTASK_DEAD)
            n++;
    return n;
}

/* ---------------------------------------------------------------- switching */

#ifdef TASK_HOST

static void task_start(void);

static void switch_to(wtask *t)
{
    wtask *from = cur;
    if (t == from)
        return;
    cur = t;
    swapcontext(&from->ctx->uc, &t->ctx->uc);
}

static void prepare(wtask *t)
{
    getcontext(&t->ctx->uc);
    t->ctx->uc.uc_stack.ss_sp = t->stack;
    t->ctx->uc.uc_stack.ss_size = t->stack_size;
    t->ctx->uc.uc_link = NULL;
    makecontext(&t->ctx->uc, task_start, 0);
}

static uintptr_t msplim_get(void) { return 0; }

#else

/* save the callee-saved registers on the old stack, switch SP and MSPLIM,
 * restore from the new one; a new task's stack is prepared to "return"
 * into task_start */
static void __attribute__((naked, noinline))
task_swap(uintptr_t *save_sp, uintptr_t new_sp, uintptr_t new_limit)
{
    __asm volatile (
        "push {r4-r7, lr}\n"
        "mov  r4, r8\n"
        "mov  r5, r9\n"
        "mov  r6, r10\n"
        "mov  r7, r11\n"
        "push {r4-r7}\n"
        "mrs  r3, primask\n"
        "cpsid i\n"
        "mov  r4, sp\n"
        "str  r4, [r0]\n"
        ".inst.w 0xF382880A\n"          /* msr msplim, r2 */
        "mov  sp, r1\n"
        "msr  primask, r3\n"
        "pop  {r4-r7}\n"
        "mov  r8, r4\n"
        "mov  r9, r5\n"
        "mov  r10, r6\n"
        "mov  r11, r7\n"
        "pop  {r4-r7, pc}\n");
}

static uintptr_t __attribute__((naked, noinline)) msplim_get(void)
{
    __asm volatile (
        ".inst.w 0xF3EF800A\n"          /* mrs r0, msplim */
        "bx   lr\n");
}

static void task_start(void);

static void switch_to(wtask *t)
{
    wtask *from = cur;
    if (t == from)
        return;
    cur = t;
    task_swap(&from->ctx->sp, t->ctx->sp, t->ctx->limit);
}

static void prepare(wtask *t)
{
    uintptr_t top = ((uintptr_t)t->stack + t->stack_size) & ~(uintptr_t)7;
    uint32_t *f = (uint32_t *)(top - 36);
    memset(f, 0, 36);
    f[8] = (uint32_t)(uintptr_t)task_start;     /* popped into pc (Thumb bit set) */
    t->ctx->sp = (uintptr_t)f;
}

#endif

static struct wtask_ctx kernel_ctx;

#ifdef TASK_HOST
win_app_entry win_host_app(const char *prog) __attribute__((weak));
win_app_entry win_host_app(const char *prog) { (void)prog; return NULL; }
const winapp_info *win_host_app_info(const char *prog) __attribute__((weak));
const winapp_info *win_host_app_info(const char *prog) { (void)prog; return NULL; }
#endif

/* The name and icons of an application file, win_alloc'd (the host test's
 * are built in). */
winapp_info *win_app_info(const char *path)
{
#ifdef TASK_HOST
    const winapp_info *i = win_host_app_info(path);
    winapp_info *out = i ? (winapp_info *)win_alloc(i->size) : NULL;
    if (out)
        memcpy(out, i, i->size);
    return out;
#else
    return win_ez_read_info(path);
#endif
}

void task_init(void)
{
    memset(&kernel_task, 0, sizeof(kernel_task));
    kernel_task.ctx = &kernel_ctx;
    kernel_task.ctx->limit = msplim_get();
    kernel_task.state = WTASK_RUNNING;
    kernel_task.h = win_handle_alloc(WOBJ_TASK, &kernel_task);
    lstrcpy(kernel_task.name, "KERNEL");
    tasks = &kernel_task;
    cur = &kernel_task;
}

/* ---------------------------------------------------------------- life cycle */

static void task_start(void)
{
    wtask *t = cur;
    t->state = WTASK_RUNNING;
    t->exit_code = t->run(t);
    user_task_ended(t->h);
    win_files_task_ended(t->h);
    t->state = WTASK_DEAD;
    switch_to(&kernel_task);
    for (;;)
        ;                       /* never resumed */
}

wtask *task_create(const char *name, int (*run)(wtask *), uint32_t stack_size)
{
    wtask *t = (wtask *)win_alloc(sizeof(wtask) + sizeof(struct wtask_ctx));
    if (!t)
        return NULL;
    t->ctx = (struct wtask_ctx *)(t + 1);
    if (stack_size < 8192)
        stack_size = 8192;
    t->stack_size = stack_size;
    t->stack = (uint8_t *)win_alloc(stack_size);
    t->h = t->stack ? win_handle_alloc(WOBJ_TASK, t) : 0;
    if (!t->h) {
        win_free(t->stack);
        win_free(t);
        return NULL;
    }
    lstrcpyn(t->name, name ? name : "", sizeof(t->name));
    t->run = run;
    t->state = WTASK_NEW;
    t->ctx->limit = (uintptr_t)t->stack + 64;
    prepare(t);
    /* append: round robin follows creation order */
    wtask **pp = &tasks;
    while (*pp)
        pp = &(*pp)->next;
    *pp = t;
    if (!shell_task)
        shell_task = t->h;
    return t;
}

/* A task that gave the CPU away in the middle of something (WinExec,
 * Yield, PeekMessage) continues as soon as possible; one waiting for
 * messages runs when it has some. The kernel waits while its loop runs. */
static int has_work(wtask *t)
{
    if (t->state == WTASK_DEAD)
        return 0;
    if (t->state == WTASK_NEW || !t->waiting)
        return 1;
    /* The kernel task normally runs its scheduler without fetching GUI
     * messages. But it may itself own a modal dialog (e.g. an error from
     * WINN's startup code). Wake it for its queued messages in that case. */
    if (t == &kernel_task && t->waiting != WTASK_WAIT_MESSAGE)
        return 0;
    return user_task_has_work(t->h);
}

/* Run another task that has something to do; 1 if one ran. */
int task_yield(void)
{
    wtask *start = cur->next ? cur->next : tasks;
    wtask *t = start;
    do {
        if (t != cur && has_work(t)) {
            switch_to(t);
            return 1;
        }
        t = t->next ? t->next : tasks;
    } while (t != start);
    return 0;
}

void task_run_now(wtask *t)
{
    if (t && t->state != WTASK_DEAD)
        switch_to(t);
}

static void reap(void)
{
    for (wtask **pp = &tasks; *pp;) {
        wtask *t = *pp;
        if (t->state == WTASK_DEAD && t != cur) {
            *pp = t->next;
            win_handle_free(t->h);
            win_free(t->stack);
            win_free(t->module);
            for (int i = 0; i < WINAPP_MAX_ICONS; i++)
                if (t->icons[i])
                    DestroyIcon(t->icons[i]);
            win_free(t->info);
            win_free(t);
            continue;
        }
        pp = &t->next;
    }
}

/* The kernel task: runs the others until the shell (the first task) ends,
 * then ends the rest. */
void task_kernel_loop(void)
{
    kernel_task.waiting = WTASK_WAIT_IDLE;
    for (;;) {
        reap();
        if (!IsTask(shell_task))
            break;
        user_poll_input();
        if (!task_yield()) {
            win_logo_end();             /* the shell has painted behind the logo */
            if (win_sys.input)
                win_sys.input->idle();
        }
    }
    /* the shell is gone: Windows ends, every task gets WM_QUIT */
    for (int round = 0; round < 1000 && GetNumTasks(); round++) {
        for (wtask *t = tasks; t; t = t->next)
            if (t != &kernel_task && t->state != WTASK_DEAD)
                t->quit = 1, t->waiting = 0;
        task_yield();
        reap();
    }
    kernel_task.waiting = WTASK_WAIT_NONE;
}

void task_quit_all(void)
{
    for (wtask *t = tasks; t; t = t->next)
        if (t != &kernel_task && t->state != WTASK_DEAD) {
            t->quit = 1;
            t->waiting = 0;
        }
}

/* ---------------------------------------------------------------- applications */

static int run_app(wtask *t)
{
    win_app_host *h = &t->app;
    h->magic = WINAPP_MAGIC;
    h->version = WINAPP_VERSION;
    h->size = sizeof(*h);
    h->modules = win_export_tables;
    h->hinstance = t->h;
    h->cmdline = t->cmdline;
    h->show = t->show;
    return t->entry(h);
}

#if defined(__arm__) || defined(__thumb__)
/* Run the restricted NE entry on the current WINN ARM stack. No native task
 * switching occurs before the guest has returned to Real Mode. */
static int run_ne_app(wtask *t)
{
    uint32_t result = 0;
    int rc = win_ne_execute_basic((const char *)t->module, &result);
    char msg[128];
    if (rc)
        wsprintf(msg, "NE program %s: unsupported or failed (%d)", t->name, rc);
    else
        wsprintf(msg, "NE program %s completed. DX:AX=%08lx", t->name,
                 (unsigned long)result);
    win_logo_end();
    MessageBox(0, msg, "WINN Win16", MB_OK | (rc ? MB_ICONSTOP : MB_ICONINFORMATION));
    return rc ? 1 : 0;
}
#endif

/* "PROG.EXE args" -> program file and argument text */
static void split_cmd(LPCSTR cmd, char *prog, int psize, const char **args)
{
    while (*cmd == ' ')
        cmd++;
    int n = 0;
    while (*cmd && *cmd != ' ' && n < psize - 1)
        prog[n++] = *cmd++;
    prog[n] = 0;
    while (*cmd == ' ')
        cmd++;
    *args = cmd;
}

UINT WINAPI WinExec(LPCSTR cmd, UINT show)
{
    char prog[80], path[128];
    const char *args;
    split_cmd(cmd ? cmd : "", prog, sizeof(prog), &args);
    if (!prog[0])
        return 2;
    int dot = 0;
    for (const char *p = prog; *p; p++)
        if (*p == '.')
            dot = 1;
        else if (*p == '\\' || *p == '/')
            dot = 0;
    if (!dot && lstrlen(prog) + 4 < (int)sizeof(prog))
        lstrcat(prog, ".EXE");
    win_path(prog, path, sizeof(path));

    win_ez_image img;
    int ne_guest = 0;
#ifdef TASK_HOST
    /* the host test has its applications built in */
    memset(&img, 0, sizeof(img));
    img.entry = (void *)win_host_app(prog);
    int rc = 0;
    if (!img.entry) {               /* a file that is not one of them: a DOS program */
        FILE *f = fopen(path, "rb");
        rc = f ? -2 : -1;
        if (f)
            fclose(f);
    }
#else
    int rc = win_ez_load(path, &img);
    if (!rc && !win_ez_entry_is_app(&img)) {
        win_free(img.raw);          /* a DOS program or a driver */
        rc = -2;
    }
    if (rc == -2) {
        /* NE is an x86 Win16 image, not an ARM EZ image. The entry checker
         * rejects all unsupported guest instructions before CR0.PE=1. */
        win_ne_info ni;
        PC *pc = get_PC();
        if (pc && pc->cpu && pc->cpu->gen >= 3 &&
            win_ne_scan(path, &ni, NULL, NULL) == WIN_NE_OK &&
            ni.target_os == 2 && ni.segment_count == 2 &&
            ni.cs_index && ni.ss_index && ni.cs_index != ni.ss_index) {
            ne_guest = 1;
            memset(&img, 0, sizeof(img));
            rc = 0;
        }
    }
#endif
    if (rc == -1)
        return 2;                   /* file not found */
    if (rc)
        return 11;                  /* invalid .EXE */
    /* the module name is the file name without directory and extension */
    const char *base = prog;
    for (const char *p = prog; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    char name[16];
    int k = 0;
    while (base[k] && base[k] != '.' && k < 15) {
        name[k] = base[k];
        k++;
    }
    name[k] = 0;
    int (*runner)(wtask *) = run_app;
    void *ne_path = NULL;
#if defined(__arm__) || defined(__thumb__)
    if (ne_guest) {
        ne_path = win_strdup(path);  /* freed by the ordinary task reaper */
        if (!ne_path)
            return 8;
        runner = run_ne_app;
    }
#endif
    wtask *t = task_create(name, runner, img.stack_size ? img.stack_size : 32768);
    if (!t) {
        win_free(ne_path);
        win_free(img.raw);
        return 8;                   /* out of memory */
    }
    t->module = ne_guest ? ne_path : img.raw;
    t->entry = ne_guest ? NULL : (int (*)(const win_app_host *))img.entry;
    t->info = ne_guest ? NULL : win_app_info(path);
    lstrcpyn(t->cmdline, args, sizeof(t->cmdline));
    t->show = (int)show;
    /* let it start right away, as Windows does */
    task_run_now(t);
    return t->h;
}
