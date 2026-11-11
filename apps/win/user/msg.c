/*
 * msg.c - message queue, input translation, timers, GetMessage family.
 *
 * One application, one thread: SendMessage is a direct call of the window
 * procedure. Input from the driver is turned into window messages when it
 * is polled (hit testing, capture, activation, double clicks), then queued
 * together with posted messages. WM_PAINT and WM_TIMER are generated when
 * the queue is empty, as in Windows.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

static UINT dblclk_time = 500;      /* ms; WIN.INI [windows] DoubleClickSpeed */

void WINAPI SetDoubleClickTime(UINT ms) { dblclk_time = ms ? (ms > 5000 ? 5000 : ms) : 500; }
UINT WINAPI GetDoubleClickTime(void) { return dblclk_time; }

/* the left and right buttons exchanged at the input, for everyone */
static BOOL swap_buttons;
BOOL WINAPI SwapMouseButton(BOOL swap)
{
    BOOL was = swap_buttons;
    swap_buttons = swap != 0;
    return was;
}
int user_buttons_swapped(void) { return swap_buttons; }

#define QSIZE 256

static MSG queue[QSIZE];
static int16_t qchar[QSIZE];     /* key-down: character at post time, -1 none */
static HTASK qtask[QSIZE];      /* task the message is for (0: any) */
static uint8_t qinput[QSIZE];   /* the keyboard's, the mouse's: not posted */
static int putting_input;
static int last_char = -1;       /* of the last key message retrieved */
static MSG last_key;
static int q_head, q_count;
static int ctrl_shift_pending;

static uint32_t now_ms(void) { return GetTickCount(); }

/* the task a window belongs to; 0 for the desktop (any task may serve) */
static HTASK wnd_task(HWND h)
{
    wwnd *w = user_wnd(h);
    return w ? w->task : 0;
}

static void q_put_t(const MSG *m, int ch, HTASK task)
{
    if (q_count == QSIZE)          /* full: drop the oldest */
        q_head = (q_head + 1) % QSIZE, q_count--;
    int i = (q_head + q_count) % QSIZE;
    queue[i] = *m;
    qchar[i] = (int16_t)ch;
    qtask[i] = task;
    qinput[i] = (uint8_t)putting_input;
    q_count++;
}

static void q_put_ch(const MSG *m, int ch) { q_put_t(m, ch, m->hwnd ? wnd_task(m->hwnd) : task_current()->h); }
static void q_put(const MSG *m) { q_put_ch(m, -1); }

static MSG *q_at(int i) { return &queue[(q_head + i) % QSIZE]; }

static void q_remove(int i)
{
    for (int k = i; k > 0; k--) {
        *q_at(k) = *q_at(k - 1);
        qchar[(q_head + k) % QSIZE] = qchar[(q_head + k - 1) % QSIZE];
        qtask[(q_head + k) % QSIZE] = qtask[(q_head + k - 1) % QSIZE];
        qinput[(q_head + k) % QSIZE] = qinput[(q_head + k - 1) % QSIZE];
    }
    q_head = (q_head + 1) % QSIZE;
    q_count--;
}

void user_post(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    MSG msg = { h, m, wp, lp, now_ms(), { U.mouse_x, U.mouse_y } };
    q_put(&msg);
}

void user_purge_window(HWND h)
{
    for (int i = q_count - 1; i >= 0; i--)
        if (q_at(i)->hwnd == h)
            q_remove(i);
}

BOOL WINAPI PostMessage(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (h == 0xFFFF) {
        for (wwnd *w = U.desktop->child; w; w = w->next)
            user_post(w->h, m, wp, lp);
        return TRUE;
    }
    if (h && !IsWindow(h))
        return FALSE;
    user_post(h, m, wp, lp);
    return TRUE;
}

BOOL WINAPI PostAppMessage(HTASK task, UINT m, WPARAM wp, LPARAM lp)
{
    if (!task_from_handle(task))
        return FALSE;
    MSG msg = { 0, m, wp, lp, now_ms(), { U.mouse_x, U.mouse_y } };
    q_put_t(&msg, -1, task);
    return TRUE;
}

void WINAPI PostQuitMessage(int code)
{
    wtask *t = task_current();
    t->quit = 1;
    t->quit_code = code;
}

static unsigned hook_types;
static LRESULT (*hook_call)(int, int, WPARAM, LPARAM);

void user_set_hooks(unsigned types, LRESULT (*call)(int type, int code, WPARAM wp, LPARAM lp))
{
    hook_types = call ? types : 0;
    hook_call = call;
}

LRESULT user_hook(int type, int code, WPARAM wp, LPARAM lp)
{
    return hook_types & USER_HOOK_BIT(type) ? hook_call(type, code, wp, lp) : 0;
}

/* A modal loop's message (a dialog's, a menu's ...) to the message
 * filters: the system's, then the task's; nonzero, one of them took it */
BOOL WINAPI CallMsgFilter(LPMSG msg, int code)
{
    if (!msg)
        return FALSE;
    if (user_hook(WH_SYSMSGFILTER, code, 0, (LPARAM)(intptr_t)msg))
        return TRUE;
    return user_hook(WH_MSGFILTER, code, 0, (LPARAM)(intptr_t)msg) != 0;
}

/* WH_CALLWNDPROC's hooks with the message (they may subclass the window,
 * destroy it): its procedure then, NULL if it is gone */
static WNDPROC send_hook(HWND h, UINT m, WPARAM wp, LPARAM lp, int same)
{
    if (hook_types & USER_HOOK_BIT(WH_CALLWNDPROC)) {
        CWPSTRUCT cwp = { lp, wp, m, h };
        hook_call(WH_CALLWNDPROC, HC_ACTION, (WPARAM)same, (LPARAM)(intptr_t)&cwp);
    }
    wwnd *w = user_wnd(h);
    return w ? w->proc : NULL;
}

LRESULT WINAPI SendMessage(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (h == 0xFFFF) {
        for (wwnd *w = U.desktop->child, *n; w; w = n) {
            n = w->next;
            SendMessage(w->h, m, wp, lp);
        }
        return 0;
    }
    wwnd *w = user_wnd(h);
    if (!w || !w->proc)
        return 0;
    /* Keep the synchronous call, but attribute API calls inside its
     * window procedure to the window's owning task, not to the sender. */
    wtask *owner = task_from_handle(w->task);
    if (!owner || owner == task_current()) {
        WNDPROC proc = send_hook(h, m, wp, lp, 1);
        return proc ? proc(h, m, wp, lp) : 0;
    }
    wtask *previous = task_set_dispatch_owner(owner);
    WNDPROC proc = send_hook(h, m, wp, lp, 0);
    LRESULT result = proc ? proc(h, m, wp, lp) : 0;
    task_set_dispatch_owner(previous);
    return result;
}

LRESULT WINAPI CallWindowProc(WNDPROC proc, HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    return proc ? proc(h, m, wp, lp) : 0;
}

BOOL WINAPI InSendMessage(void) { return FALSE; }

/* ---------------------------------------------------------------- keyboard input */

static int key_down(int vk) { return (U.keys[vk & 0xFF] & 0x80) != 0; }

void win_ddi_key_event(int vk, int sc, int down)
{
    vk &= 0xFF;
    /* keypad without NumLock: navigation keys */
    if (!(sc & 0x100) && !(U.keys[VK_NUMLOCK] & 1)) {
        static const uint8_t nav[10] = { VK_INSERT, VK_END, VK_DOWN, VK_NEXT, VK_LEFT,
                                         VK_CLEAR, VK_RIGHT, VK_HOME, VK_UP, VK_PRIOR };
        if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD0 + 9)
            vk = nav[vk - VK_NUMPAD0];
        else if (vk == VK_DECIMAL)
            vk = VK_DELETE;
    }
    int was = key_down(vk);
    if (down) {
        U.keys[vk] |= 0x80;
        if (!was)
            U.keys[vk] ^= 1;
    } else {
        U.keys[vk] &= 0x7F;
    }

    /* Ctrl+Shift alone switches the keyboard layout */
    if (vk == VK_SHIFT || vk == VK_CONTROL) {
        if (down && key_down(VK_SHIFT) && key_down(VK_CONTROL))
            ctrl_shift_pending = 1;
        else if (!down && ctrl_shift_pending) {
            ctrl_shift_pending = 0;
            U.layout ^= 1;
        }
    } else if (down) {
        ctrl_shift_pending = 0;
    }

    int alt = key_down(VK_MENU), ctrl = key_down(VK_CONTROL);
    /* Ctrl+Esc belongs to the system, whatever has the focus: the Task List */
    if (vk == VK_ESCAPE && ctrl && !alt) {
        if (down && !was && U.desktop)
            user_post(U.desktop->h, WM_SYSCOMMAND, SC_TASKLIST, 0);
        return;
    }
    wwnd *target = U.focus;
    int sys = (alt && !ctrl) || vk == VK_F10 || (vk == VK_MENU);
    if (!target) {
        target = U.active;
        sys = 1;
    }
    if (!target)
        return;
    UINT m = sys ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (down ? WM_KEYDOWN : WM_KEYUP);
    LPARAM lp = 1 | ((LPARAM)(sc & 0xFF) << 16) | ((sc & 0x100) ? (1L << 24) : 0) |
                (alt ? (1L << 29) : 0) | (was ? (1L << 30) : 0) |
                (down ? 0 : (LPARAM)(1UL << 31));
    /* the character depends on the shift state now, not when the
     * application gets around to TranslateMessage */
    unsigned char ch;
    int c = (down && user_vk_to_char(vk, sc, &ch)) ? ch : -1;
    MSG msg = { target->h, m, (WPARAM)vk, lp, now_ms(), { U.mouse_x, U.mouse_y } };
    putting_input = 1;
    q_put_ch(&msg, c);
    putting_input = 0;
}

int WINAPI GetKeyState(int vk)
{
    /* a 16-bit value in Win16: "down" is the sign bit, so "< 0" tests it */
    uint8_t s = U.keys[vk & 0xFF];
    return (int16_t)(((s & 0x80) ? 0x8000 : 0) | (s & 1));
}

int WINAPI GetAsyncKeyState(int vk) { return (int16_t)(GetKeyState(vk) & 0x8000); }

BOOL WINAPI TranslateMessage(const MSG *msg)
{
    if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN)
        return FALSE;
    unsigned char ch;
    if (last_char >= 0 && msg->hwnd == last_key.hwnd && msg->message == last_key.message &&
        msg->wParam == last_key.wParam && msg->lParam == last_key.lParam) {
        ch = (unsigned char)last_char;
    } else {
        int sc = (int)((msg->lParam >> 16) & 0xFF) | ((msg->lParam & (1L << 24)) ? 0x100 : 0);
        if (!user_vk_to_char((int)msg->wParam, sc, &ch))
            return FALSE;
    }
    UINT m = msg->message == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR;
    MSG c = *msg;
    c.message = m;
    c.wParam = ch;
    /* goes to the front so it follows its key-down */
    if (q_count == QSIZE)
        q_count--;
    q_head = (q_head + QSIZE - 1) % QSIZE;
    queue[q_head] = c;
    qchar[q_head] = -1;
    qtask[q_head] = wnd_task(c.hwnd);
    q_count++;
    return TRUE;
}

/* ---------------------------------------------------------------- mouse input */

static unsigned mk_flags(void)
{
    unsigned f = 0;
    if (U.buttons & 1) f |= MK_LBUTTON;
    if (U.buttons & 2) f |= MK_RBUTTON;
    if (U.buttons & 4) f |= MK_MBUTTON;
    if (key_down(VK_SHIFT)) f |= MK_SHIFT;
    if (key_down(VK_CONTROL)) f |= MK_CONTROL;
    return f;
}

static void post_mouse(wwnd *w, int hit, UINT client_msg, int x, int y)
{
    UINT m;
    WPARAM wp;
    LPARAM lp;
    if (hit == HTCLIENT) {
        m = client_msg;
        wp = mk_flags();
        lp = MAKELPARAM(x - w->client.left, y - w->client.top);
    } else {
        m = client_msg - WM_MOUSEMOVE + WM_NCMOUSEMOVE;
        wp = (WPARAM)hit;
        lp = MAKELPARAM(x, y);
    }
    if (client_msg == WM_MOUSEMOVE && q_count) {
        MSG *last = q_at(q_count - 1);
        if (last->hwnd == w->h && last->message == m) {
            last->wParam = wp;
            last->lParam = lp;
            last->time = now_ms();
            last->pt.x = x;
            last->pt.y = y;
            return;
        }
    }
    putting_input = 1;
    user_post(w->h, m, wp, lp);
    putting_input = 0;
}

static void mouse_button(int x, int y, int btn, int down)
{
    static const UINT downs[3] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN };
    UINT m = down ? downs[btn] : downs[btn] + 1;
    wwnd *w;
    int hit;
    if (U.capture) {
        w = U.capture;
        hit = HTCLIENT;
    } else {
        w = user_window_from_point(x, y, &hit);
        if (!w)
            return;
        if (hit == HTERROR) {
            if (down)
                MessageBeep(0);
            /* a click on a window disabled by a modal dialog brings the
             * application forward with the dialog active */
            if (down)
                user_switch_to(user_toplevel(w));
            return;
        }
        SendMessage(w->h, WM_SETCURSOR, w->h, MAKELPARAM(hit, m));
        if (down && w != U.desktop) {
            wwnd *top = user_toplevel(w);
            if (top != U.active && top != U.desktop) {
                LRESULT r = SendMessage(w->h, WM_MOUSEACTIVATE, top->h, MAKELPARAM(hit, m));
                if (r != MA_NOACTIVATE && IsWindow(top->h))
                    SetActiveWindow(top->h);
                if (r == MA_ACTIVATEANDEAT || !IsWindow(w->h))
                    return;
            }
        }
    }
    if (down) {
        uint32_t t = now_ms();
        int dbl = (w->cls->wc.style & CS_DBLCLKS) || hit != HTCLIENT;
        if (dbl && U.last_click_hwnd == w->h && U.last_click_msg == (int)m &&
            t - U.last_click_time < dblclk_time &&
            (x - U.last_click_x) * (x - U.last_click_x) < 16 &&
            (y - U.last_click_y) * (y - U.last_click_y) < 16) {
            m += 2;                                  /* xBUTTONDBLCLK */
            U.last_click_hwnd = 0;
        } else {
            U.last_click_hwnd = w->h;
            U.last_click_msg = (int)m;
            U.last_click_time = t;
            U.last_click_x = x;
            U.last_click_y = y;
        }
    }
    post_mouse(w, hit, m, x, y);
}

void win_ddi_mouse_event(int x, int y, unsigned buttons)
{
    int sw = win_sys.screen.width, sh = win_sys.screen.height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= sw) x = sw - 1;
    if (y >= sh) y = sh - 1;
    int moved = x != U.mouse_x || y != U.mouse_y;
    U.mouse_x = x;
    U.mouse_y = y;
    cursor_move(x, y);
    if (moved) {
        wwnd *w;
        int hit;
        if (U.capture) {
            w = U.capture;
            hit = HTCLIENT;
        } else {
            w = user_window_from_point(x, y, &hit);
            if (w && hit != HTERROR)
                SendMessage(w->h, WM_SETCURSOR, w->h, MAKELPARAM(hit, WM_MOUSEMOVE));
            else if (w)
                user_cursor_builtin(WCUR_ARROW);
        }
        if (w && hit != HTERROR)
            post_mouse(w, hit, WM_MOUSEMOVE, x, y);
    }
    if (swap_buttons)
        buttons = (buttons & ~3u) | ((buttons & 1u) << 1) | ((buttons >> 1) & 1u);
    unsigned changed = buttons ^ U.buttons;
    for (int b = 0; b < 3; b++) {
        if (!(changed & (1u << b)))
            continue;
        U.buttons ^= 1u << b;
        mouse_button(x, y, b, (buttons >> b) & 1);
        U.keys[b == 0 ? VK_LBUTTON : b == 1 ? VK_RBUTTON : VK_MBUTTON] =
            (uint8_t)(((buttons >> b) & 1) ? 0x80 : 0);
    }
}

void WINAPI GetCursorPos(LPPOINT pt)
{
    pt->x = U.mouse_x;
    pt->y = U.mouse_y;
}

void WINAPI SetCursorPos(int x, int y)
{
    U.mouse_x = x;
    U.mouse_y = y;
    cursor_move(x, y);
}

/* ---------------------------------------------------------------- timers */

typedef struct {
    HWND      hwnd;
    UINT      id;
    UINT      elapse;
    uint32_t  due;
    TIMERPROC proc;
    int       used, pending;
    HTASK     task;
} wtimer;

#define NTIMERS 32
static wtimer timers[NTIMERS];
static UINT next_timer_id = 0x100;

UINT WINAPI SetTimer(HWND h, UINT id, UINT elapse, TIMERPROC proc)
{
    if (elapse < 10)
        elapse = 10;
    if (!h)
        id = next_timer_id++;
    for (int i = 0; i < NTIMERS; i++)
        if (timers[i].used && timers[i].hwnd == h && timers[i].id == id) {
            timers[i].elapse = elapse;
            timers[i].due = now_ms() + elapse;
            timers[i].proc = proc;
            return id ? id : 1;
        }
    for (int i = 0; i < NTIMERS; i++)
        if (!timers[i].used) {
            timers[i] = (wtimer){ h, id, elapse, now_ms() + elapse, proc, 1, 0,
                                  h ? wnd_task(h) : task_current()->h };
            return id ? id : 1;
        }
    return 0;
}

BOOL WINAPI KillTimer(HWND h, UINT id)
{
    for (int i = 0; i < NTIMERS; i++)
        if (timers[i].used && timers[i].hwnd == h && timers[i].id == id) {
            timers[i].used = 0;
            return TRUE;
        }
    return FALSE;
}

void user_kill_window_timers(HWND h)
{
    for (int i = 0; i < NTIMERS; i++)
        if (timers[i].used && timers[i].hwnd == h)
            timers[i].used = 0;
}

static int timer_due(HTASK task)
{
    uint32_t t = now_ms();
    for (int i = 0; i < NTIMERS; i++)
        if (timers[i].used && timers[i].task == task && (int32_t)(t - timers[i].due) >= 0)
            return 1;
    return 0;
}

static int timer_message(MSG *m, HWND filter, HTASK task, int remove)
{
    uint32_t t = now_ms();
    for (int i = 0; i < NTIMERS; i++) {
        wtimer *tm = &timers[i];
        if (!tm->used || tm->task != task || (int32_t)(t - tm->due) < 0)
            continue;
        if (filter && tm->hwnd != filter && !IsChild(filter, tm->hwnd))
            continue;
        if (remove)
            tm->due = t + tm->elapse;
        m->hwnd = tm->hwnd;
        m->message = WM_TIMER;
        m->wParam = tm->id;
        m->lParam = (LPARAM)(intptr_t)tm->proc;
        m->time = t;
        m->pt.x = U.mouse_x;
        m->pt.y = U.mouse_y;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- retrieval */

static int matches(const MSG *m, HWND h, UINT first, UINT last)
{
    if (h && m->hwnd != h && !IsChild(h, m->hwnd))
        return 0;
    if ((first || last) && (m->message < first || m->message > last))
        return 0;
    return 1;
}

static void pump_input(void)
{
    if (win_sys.input)
        win_sys.input->poll();
    user_caret_tick(now_ms());
    sound_service(now_ms());
}

/* One pass; 1 = message, 0 = nothing, -1 = WM_QUIT. */
/* A button went down in a child window: its parents hear of it
 * (WM_PARENTNOTIFY, position in each parent's client coordinates), as the
 * message is taken from the queue, on the receiving task. */
static void parent_notify(const MSG *m)
{
    UINT msg = m->message;
    if (msg >= WM_NCMOUSEMOVE && msg <= WM_NCMBUTTONDBLCLK)
        msg += WM_MOUSEMOVE - WM_NCMOUSEMOVE;
    if (msg != WM_LBUTTONDOWN && msg != WM_RBUTTONDOWN && msg != WM_MBUTTONDOWN)
        return;
    for (wwnd *w = user_wnd(m->hwnd); w && (w->style & WS_CHILD) && w->parent &&
         !(w->exstyle & WS_EX_NOPARENTNOTIFY); w = w->parent) {
        wwnd *p = w->parent;
        HWND ph = p->h;
        SendMessage(ph, WM_PARENTNOTIFY, msg,
                    MAKELPARAM(m->pt.x - p->client.left, m->pt.y - p->client.top));
        if (!IsWindow(ph) || !IsWindow(w->h))
            break;
    }
}

static int quit_message(MSG *out, wtask *t, int remove)
{
    memset(out, 0, sizeof(*out));
    out->message = WM_QUIT;
    out->wParam = (WPARAM)t->quit_code;
    if (remove)
        t->quit = 0;
    return -1;
}

/* The keyboard's and the mouse's hooks with input a task takes (removed:
 * HC_ACTION, looked at: HC_NOREMOVE), the keyboard's and the mouse's own,
 * not a message posted: nonzero, it is to be thrown away. Not again while
 * one of them runs. */
static int input_hooking;

static int input_hooked_away(const MSG *m, int remove)
{
    int key = m->message >= WM_KEYDOWN && m->message <= WM_SYSKEYUP &&
              m->message != WM_CHAR && m->message != WM_DEADCHAR &&
              m->message != WM_SYSCHAR && m->message != WM_SYSDEADCHAR;
    int nc = m->message >= WM_NCMOUSEMOVE && m->message <= WM_NCMBUTTONDBLCLK;
    int mouse = nc || (m->message >= WM_MOUSEFIRST && m->message <= WM_MOUSELAST);
    if (input_hooking || (!key && !mouse) ||
        !(hook_types & USER_HOOK_BIT(key ? WH_KEYBOARD : WH_MOUSE)))
        return 0;
    int code = remove ? HC_ACTION : HC_NOREMOVE;
    LRESULT r;
    input_hooking = 1;
    if (key) {
        r = hook_call(WH_KEYBOARD, code, m->wParam, m->lParam);
    } else {
        MOUSEHOOKSTRUCT mh = { m->pt, m->hwnd, nc ? (UINT)m->wParam : (UINT)HTCLIENT, 0 };
        r = hook_call(WH_MOUSE, code, m->message, (LPARAM)(intptr_t)&mh);
    }
    input_hooking = 0;
    return r != 0;
}

/* the queued message just like m, if it is still there: gone */
static void q_drop(const MSG *m)
{
    for (int i = 0; i < q_count; i++)
        if (!memcmp(q_at(i), m, sizeof(*m))) {
            q_remove(i);
            return;
        }
}

static int get_one(MSG *out, HWND h, UINT first, UINT last, int remove)
{
    pump_input();
    wtask *me = task_current();
    /* A committed exit must not drain queued GUI input or begin a paint. */
    if (U.shutting_down && me->quit)
        return quit_message(out, me, remove);
    for (int i = 0; i < q_count; i++) {
        MSG *m = q_at(i);
        if (m->hwnd && !IsWindow(m->hwnd)) {
            q_remove(i--);
            continue;
        }
        HTASK owner = qtask[(q_head + i) % QSIZE];
        if (owner && owner != me->h)
            continue;
        if (!matches(m, h, first, last))
            continue;
        MSG got = *m;
        int ch = qchar[(q_head + i) % QSIZE], input = qinput[(q_head + i) % QSIZE];
        if (remove)
            q_remove(i);
        if (input && input_hooked_away(&got, remove)) {   /* the queue may have changed */
            if (!remove)
                q_drop(&got);
            i = -1;
            continue;
        }
        *out = got;
        if (got.message == WM_KEYDOWN || got.message == WM_SYSKEYDOWN) {
            last_key = got;
            last_char = ch;
        }
        if (remove)
            parent_notify(out);
        return 1;
    }
    if (me->quit)
        return quit_message(out, me, remove);
    if (!first || (WM_PAINT >= first && WM_PAINT <= last)) {
        wwnd *w = user_find_paint(me->h);
        if (w && (!h || w->h == h || IsChild(h, w->h))) {
            memset(out, 0, sizeof(*out));
            out->hwnd = w->h;
            out->message = WM_PAINT;
            out->time = now_ms();
            out->pt.x = U.mouse_x;
            out->pt.y = U.mouse_y;
            return 1;
        }
    }
    if (!first || (WM_TIMER >= first && WM_TIMER <= last)) {
        MSG t;
        if (timer_message(&t, h, me->h, remove)) {
            *out = t;
            return 1;
        }
    }
    return 0;
}

/* What the task's queue has (QS_*), current and new alike: input (keys,
 * the mouse's moves and buttons), posted messages, a timer due, a window
 * to paint. Sent messages are delivered at once: never any. */
DWORD WINAPI GetQueueStatus(UINT flags)
{
    pump_input();
    wtask *me = task_current();
    UINT has = 0;
    for (int i = 0; i < q_count; i++) {
        HTASK owner = qtask[(q_head + i) % QSIZE];
        UINT m = q_at(i)->message;
        if (owner && owner != me->h)
            continue;
        if (m >= WM_KEYFIRST && m <= WM_KEYLAST)
            has |= QS_KEY;
        else if (m == WM_MOUSEMOVE || m == WM_NCMOUSEMOVE)
            has |= QS_MOUSEMOVE;
        else if ((m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) ||
                 (m >= WM_NCMOUSEMOVE && m <= WM_NCMBUTTONDBLCLK))
            has |= QS_MOUSEBUTTON;
        else
            has |= QS_POSTMESSAGE;
    }
    if (timer_due(me->h))
        has |= QS_TIMER;
    if (user_find_paint(me->h))
        has |= QS_PAINT;
    has &= flags;
    return MAKELONG(has, has);
}

BOOL WINAPI GetInputState(void)
{
    return (LOWORD(GetQueueStatus(QS_INPUT)) & QS_INPUT) != 0;
}

/* a sent message is handled as it is sent: nobody waits for a reply */
void WINAPI ReplyMessage(LRESULT result) { (void)result; }

/* the keys as WINN has them: bit 7 down, bit 0 toggled */
void WINAPI GetKeyboardState(BYTE *keys) { memcpy(keys, U.keys, sizeof(U.keys)); }
void WINAPI SetKeyboardState(const BYTE *keys) { memcpy(U.keys, keys, sizeof(U.keys)); }

/* Nothing for this task: let the others run, or idle when nobody can. */
static void wait_turn(void)
{
    wtask *me = task_current();
    task_started(me);
    me->waiting = WTASK_WAIT_MESSAGE;
    if (!task_yield() && win_sys.input)
        win_sys.input->idle();
    me->waiting = WTASK_WAIT_NONE;
}

/* WH_GETMESSAGE's hooks with a message retrieved (they may change it) */
static void got_message(MSG *msg)
{
    if (hook_types & USER_HOOK_BIT(WH_GETMESSAGE))
        hook_call(WH_GETMESSAGE, HC_ACTION, 0, (LPARAM)(intptr_t)msg);
}

BOOL WINAPI GetMessage(LPMSG msg, HWND h, UINT first, UINT last)
{
    for (;;) {
        int r = get_one(msg, h, first, last, 1);
        if (r) {
            got_message(msg);
            U.cur_msg = *msg;
            return r > 0;
        }
        wait_turn();
    }
}

/* Win16: finding nothing yields to the other tasks (unless PM_NOYIELD),
 * then gives the system its idle turn (time goes on for a program waiting
 * in a PeekMessage loop, Write's blinking caret, though it keeps having
 * work). USER's own loops (menus, capture) idle themselves: user_peek,
 * without. */
static BOOL peek(LPMSG msg, HWND h, UINT first, UINT last, UINT flags, int idle)
{
    int r = get_one(msg, h, first, last, flags & PM_REMOVE);
    if (r) {
        got_message(msg);
        U.cur_msg = *msg;
    } else {
        task_started(task_current());   /* a game's loop: nothing waiting for it */
        if (!(flags & PM_NOYIELD)) {
            task_yield();
            if (idle && win_sys.input)
                win_sys.input->idle();
        }
    }
    return r != 0;
}

BOOL WINAPI PeekMessage(LPMSG msg, HWND h, UINT first, UINT last, UINT flags)
{
    return peek(msg, h, first, last, flags, 1);
}

BOOL user_peek(LPMSG msg, HWND h, UINT first, UINT last, UINT flags)
{
    return peek(msg, h, first, last, flags, 0);
}

BOOL WINAPI WaitMessage(void)
{
    MSG m;
    while (!get_one(&m, 0, 0, 0, 0))
        wait_turn();
    return TRUE;
}

BOOL WINAPI Yield(void)
{
    pump_input();
    task_yield();
    return TRUE;
}

/* ---------------------------------------------------------------- tasks */

void user_poll_input(void) { pump_input(); }

int user_task_has_work(HTASK task)
{
    wtask *t = task_from_handle(task);
    if (t && t->quit)
        return 1;
    for (int i = 0; i < q_count; i++)
        if (qtask[(q_head + i) % QSIZE] == task || !qtask[(q_head + i) % QSIZE])
            return 1;
    return user_find_paint(task) != NULL || timer_due(task);
}

/* Ask every top-level window, then end all applications (Windows ends
 * with its shell). */
BOOL WINAPI ExitWindows(DWORD reserved, UINT code)
{
    (void)reserved; (void)code;
    if (U.shutting_down)
        return TRUE;
    /* Show progress through potentially slow task queries and SD writes. */
    cursor_wait_begin();
    for (wwnd *w = U.desktop->child; w; w = w->next)
        if (!SendMessage(w->h, WM_QUERYENDSESSION, 0, 0)) {
            cursor_wait_end();
            return FALSE;
        }
    /* Keep the last complete frame (including a confirmation dialog) until
     * the display is shut down; no half-painted desktop underneath it. */
    U.shutting_down = 1;
    for (wwnd *w = U.desktop->child, *n; w; w = n) {
        n = w->next;
        SendMessage(w->h, WM_ENDSESSION, TRUE, 0);
    }
    task_quit_all();
    return TRUE;
}

/* A task ended: its windows, timers and messages go. */
void user_task_ended(HTASK task)
{
    user_clipboard_task_ended(task);
    for (int again = 1; again;) {
        again = 0;
        for (wwnd *w = U.desktop->child; w; w = w->next)
            if (w->task == task) {
                DestroyWindow(w->h);
                again = 1;
                break;
            }
    }
    for (int i = 0; i < NTIMERS; i++)
        if (timers[i].used && timers[i].task == task)
            timers[i].used = 0;
    user_unregister_task_classes(task);
    for (int i = q_count - 1; i >= 0; i--)
        if (qtask[(q_head + i) % QSIZE] == task)
            q_remove(i);
}

DWORD WINAPI GetMessagePos(void) { return (DWORD)MAKELONG(U.cur_msg.pt.x, U.cur_msg.pt.y); }
LONG WINAPI GetMessageTime(void) { return (LONG)U.cur_msg.time; }

LONG WINAPI DispatchMessage(const MSG *msg)
{
    if (msg->message == WM_TIMER && msg->lParam) {
        TIMERPROC p = (TIMERPROC)(intptr_t)msg->lParam;
        p(msg->hwnd, WM_TIMER, (UINT)msg->wParam, GetTickCount());
        return 0;
    }
    if (!msg->hwnd)
        return 0;
    wwnd *w = user_wnd(msg->hwnd);
    if (!w)
        return 0;
    return w->proc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

int user_pump_one(void)
{
    MSG m;
    if (!GetMessage(&m, 0, 0, 0)) {
        PostQuitMessage((int)m.wParam);
        return 0;
    }
    TranslateMessage(&m);
    DispatchMessage(&m);
    return 1;
}

