/*
 * pe_user.c - a Win32 program's windows: its window classes and dialogs,
 * whose procedures are its own code (win_pe_call), its messages, painting,
 * resources (menus, dialogs, strings, accelerators, icons, cursors,
 * bitmaps of its PE files) and the GDI functions with structures of
 * Win32's layout.
 *
 * WINN's USER is Windows 3.1's: its messages pack their parameters as
 * Win16's do (WM_COMMAND's control and code in lParam ...), its controls'
 * messages are WM_USER + n. A Win32 program's are converted both ways:
 * what WINN sends to its procedures, what it sends or hands on to WINN's.
 * A message whose lParam points to a structure gets a copy of it in the
 * program's layout on its stack (WM_CREATE's CREATESTRUCT ...); handed
 * back to DefWindowProc, the native one is used again.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)
#include "wincore.h"
#include "pe_int.h"
#include "x86bridge.h"


void user_dialog_menu(HMENU menu);

/* Win16's messages WINN has no names for */
#define WM_VKEYTOITEM 0x002e
#define WM_CHARTOITEM 0x002f
#define WM_MENUCHAR   0x0120

#define PE_LENT 8
#define PE_USERDATA 64
#define PE_ACCELS 8
#define PE_IMAGES 32

typedef struct pe_user {
    struct pe_user *next;
    win_pe_ctx *ctx;
    HTASK task;
    struct {
        char name[64];
        uint32_t proc;
        uint32_t menu;              /* its menu: a number, */
        char menu_name[32];         /* or a name */
        int module;                 /* whose resources */
    } cls[PE_CLASSES];
    struct { HWND hwnd; uint32_t proc; WNDPROC old; } sub[PE_SUBCLASSED];
    struct { HWND hwnd; uint32_t proc, param; } dlg[PE_DIALOGS];
    uint32_t pending, pending_param;
    struct { HWND hwnd; UINT id; uint32_t proc; } timer[PE_TIMERS];
    struct { HWND hwnd; PAINTSTRUCT ps; uint32_t guest; } paint[PE_PAINTS];
    struct { HWND hwnd; uint32_t value; } userdata[PE_USERDATA];
    WNDPROC native[WIN_PE_NATIVE_PROCS];
    struct { uint32_t addr; void *native; UINT msg; } lent[PE_LENT];
    int lents;
    HACCEL accel[PE_ACCELS];
    struct { HANDLE h; uint8_t kind; int module; uint32_t id; char name[24]; } image[PE_IMAGES];
} pe_user;

static pe_user *programs;

static pe_user *user_of(HTASK task)
{
    for (pe_user *u = programs; u; u = u->next)
        if (u->task == task)
            return u;
    return NULL;
}

void pe_user_start(win_pe_ctx *ctx)
{
    pe_user *u = (pe_user *)calloc(1, sizeof(pe_user));
    if (!u)
        return;
    u->ctx = ctx;
    u->task = ctx->task;
    u->next = programs;
    programs = u;
    ctx->user = u;
}

void pe_user_end(win_pe_ctx *ctx)
{
    for (pe_user **p = &programs; *p; p = &(*p)->next)
        if (*p == ctx->user) {
            pe_user *u = *p;
            *p = u->next;
            for (int i = 0; i < PE_ACCELS; i++)
                if (u->accel[i])
                    DestroyAcceleratorTableN(u->accel[i]);
            for (int i = 0; i < PE_IMAGES; i++)
                if (u->image[i].h && u->image[i].kind != 3)
                    DestroyIcon(u->image[i].h);
            free(u);
            break;
        }
    ctx->user = NULL;
}

static int alive(pe_user *u)
{
    return u && !u->ctx->dead && !u->ctx->exited && !u->ctx->stopped;
}

/* ---------------------------------------------------------------- native procedures */

/* a native window procedure as the program holds it: a thunk of its own
 * (CallWindowProc's, or called as it is) */
static uint32_t native_token(pe_user *u, WNDPROC p)
{
    if (!p)
        return 0;
    for (unsigned i = 0; i < WIN_PE_NATIVE_PROCS; i++)
        if (u->native[i] == p || !u->native[i]) {
            u->native[i] = p;
            return win_pe_thunk(u->ctx->img, (uint16_t)(WIN_PE_SERVICE_NATIVE + i), 16);
        }
    return 0;
}

/* the native procedure of a token, NULL if proc is the program's own */
static WNDPROC token_native(pe_user *u, uint32_t proc)
{
    win_pe_image *img = u->ctx->img;
    for (unsigned i = 0; i < img->thunks; i++) {
        uint16_t s = img->thunk_service[i];
        if (s >= WIN_PE_SERVICE_NATIVE && s < WIN_PE_SERVICE_NATIVE + WIN_PE_NATIVE_PROCS &&
            img->thunk_block[i / WIN_PE_THUNKS_PER_BLOCK] + 16u * (i % WIN_PE_THUNKS_PER_BLOCK) == proc)
            return u->native[s - WIN_PE_SERVICE_NATIVE];
    }
    return NULL;
}

/* ---------------------------------------------------------------- messages: the parameters */

/* the kind of control a window is (its class): its messages' numbers */
enum { CTL_NONE, CTL_BUTTON, CTL_EDIT, CTL_LISTBOX, CTL_COMBOBOX, CTL_STATIC };

static int ctl_kind(HWND h)
{
    char cls[16];
    if (!h || !GetClassName(h, cls, sizeof(cls)))
        return CTL_NONE;
    return !lstrcmpi(cls, "BUTTON") ? CTL_BUTTON : !lstrcmpi(cls, "EDIT") ? CTL_EDIT :
           !lstrcmpi(cls, "LISTBOX") || !lstrcmpi(cls, "COMBOLBOX") ? CTL_LISTBOX :
           !lstrcmpi(cls, "COMBOBOX") ? CTL_COMBOBOX : !lstrcmpi(cls, "STATIC") ? CTL_STATIC :
           CTL_NONE;
}

/* Win32's numbers of a control's messages and Win16's (WM_USER + n) */
static UINT ctl_to_native(int kind, UINT msg)
{
    switch (kind) {
    case CTL_BUTTON: return msg >= 0xf0 && msg <= 0xf4 ? WM_USER + (msg - 0xf0) : msg;
    case CTL_EDIT: return msg >= 0xb0 && msg <= 0xd2 ? WM_USER + (msg - 0xb0) : msg;
    case CTL_LISTBOX: return msg >= 0x180 && msg <= 0x1a2 ? WM_USER + 1 + (msg - 0x180) : msg;
    case CTL_COMBOBOX: return msg >= 0x140 && msg <= 0x158 ? WM_USER + (msg - 0x140) : msg;
    case CTL_STATIC: return msg >= 0x170 && msg <= 0x173 ? WM_USER + (msg - 0x170) : msg;
    }
    return msg;
}

static UINT ctl_to_guest(int kind, UINT msg)
{
    if (msg < WM_USER)
        return msg;
    UINT n = msg - WM_USER;
    switch (kind) {
    case CTL_BUTTON: return n <= 4 ? 0xf0 + n : msg;
    case CTL_EDIT: return n <= 0x22 ? 0xb0 + n : msg;
    case CTL_LISTBOX: return n >= 1 && n <= 0x23 ? 0x180 + n - 1 : msg;
    case CTL_COMBOBOX: return n <= 0x18 ? 0x140 + n : msg;
    case CTL_STATIC: return n <= 3 ? 0x170 + n : msg;
    }
    return msg;
}

#define LO(x) ((uint32_t)(x) & 0xffffu)
#define HI(x) (((uint32_t)(x) >> 16) & 0xffffu)
#define LONG2(lo, hi) ((uint32_t)(LO(lo) | (uint32_t)(hi) << 16))

/* a native message as the program has it; kind the window's control kind */
static void to_guest(int kind, UINT *msg, uint32_t *wp, uint32_t *lp)
{
    uint32_t w = *wp, l = *lp;
    switch (*msg) {
    case WM_COMMAND: case WM_ACTIVATE: case WM_CHARTOITEM: case WM_VKEYTOITEM:
        *wp = LONG2(w, HI(l));
        *lp = LO(l);
        break;
    case WM_HSCROLL: case WM_VSCROLL:
        *wp = LONG2(w, LO(l));
        *lp = HI(l);
        break;
    case WM_MENUSELECT: case WM_MENUCHAR:
        *wp = LONG2(w, LO(l));
        *lp = HI(l);
        break;
    case WM_PARENTNOTIFY:
        if (LO(w) == WM_CREATE || LO(w) == WM_DESTROY) {
            *wp = LONG2(w, HI(l));
            *lp = LO(l);
        }
        break;
    case WM_CTLCOLOR:
        *msg = 0x132u + (HI(l) <= 6 ? HI(l) : 0);
        *lp = LO(l);
        break;
    default:
        *msg = ctl_to_guest(kind, *msg);
        if (kind == CTL_EDIT && *msg == 0xb1) {         /* EM_SETSEL: start, end */
            *wp = LO(l);
            *lp = HI(l);
        } else if (kind == CTL_EDIT && *msg == 0xb6) {  /* EM_LINESCROLL: cx, cy */
            *wp = HI(l);
            *lp = (uint32_t)(int32_t)(int16_t)LO(l);
        }
        break;
    }
}

/* the program's message as WINN's takes it */
static void to_native(int kind, UINT *msg, uint32_t *wp, uint32_t *lp)
{
    uint32_t w = *wp, l = *lp;
    switch (*msg) {
    case WM_COMMAND: case WM_ACTIVATE: case WM_CHARTOITEM: case WM_VKEYTOITEM:
        *wp = LO(w);
        *lp = LONG2(l, HI(w));
        break;
    case WM_HSCROLL: case WM_VSCROLL: case WM_MENUSELECT: case WM_MENUCHAR:
        *wp = LO(w);
        *lp = LONG2(HI(w), l);
        break;
    case WM_PARENTNOTIFY:
        if (LO(w) == WM_CREATE || LO(w) == WM_DESTROY) {
            *wp = LO(w);
            *lp = LONG2(l, HI(w));
        }
        break;
    default:
        if (*msg >= 0x132 && *msg <= 0x138) {           /* WM_CTLCOLOR* */
            *lp = LONG2(l, *msg - 0x132u);
            *msg = WM_CTLCOLOR;
            break;
        }
        if (kind == CTL_EDIT && *msg == 0xb1) {
            *wp = 0;
            *lp = LONG2(w, l);
        } else if (kind == CTL_EDIT && *msg == 0xb6) {
            *wp = 0;
            *lp = LONG2(l, w);
        }
        *msg = ctl_to_native(kind, *msg);
        break;
    }
}

/* ---------------------------------------------------------------- messages: pointers */

/* Win32's CREATESTRUCT (48 bytes), its strings after it */
static uint32_t create_to_guest(pe_user *u, const CREATESTRUCT *cs, uint32_t at, uint8_t *g,
                                uint32_t cap)
{
    memset(g, 0, 48);
    pe_wr32(g, (uint32_t)(uintptr_t)cs->lpCreateParams);
    pe_wr32(g + 4, cs->hInstance == (HINSTANCE)u->task ? u->ctx->img->mod[0].base : cs->hInstance);
    pe_wr32(g + 8, cs->hMenu);
    pe_wr32(g + 12, cs->hwndParent);
    pe_wr32(g + 16, (uint32_t)cs->cy);
    pe_wr32(g + 20, (uint32_t)cs->cx);
    pe_wr32(g + 24, (uint32_t)cs->y);
    pe_wr32(g + 28, (uint32_t)cs->x);
    pe_wr32(g + 32, (uint32_t)cs->style);
    pe_wr32(g + 44, cs->dwExStyle);
    uint32_t n = 48;
    const char *s[2] = { cs->lpszName, cs->lpszClass };
    for (int i = 0; i < 2; i++) {
        if (i == 1 && (uintptr_t)s[i] < 0x10000u) {
            pe_wr32(g + 40, (uint32_t)(uintptr_t)s[i]);     /* an atom */
            continue;
        }
        size_t len = s[i] ? strlen(s[i]) : 0;
        if (len > cap - n - 1)
            len = cap - n - 1;
        if (len)
            memcpy(g + n, s[i], len);
        g[n + len] = 0;
        pe_wr32(g + (i ? 40 : 36), at + n);
        n += (uint32_t)len + 1;
    }
    return (n + 3) & ~3u;
}

/* WINDOWPOS: hwnd, hwndInsertAfter, x, y, cx, cy, flags */
static void windowpos(WINDOWPOS *n, uint8_t *g, int out)
{
    if (out) {
        pe_wr32(g, n->hwnd);
        pe_wr32(g + 4, n->hwndInsertAfter);
        pe_wr32(g + 8, (uint32_t)n->x);
        pe_wr32(g + 12, (uint32_t)n->y);
        pe_wr32(g + 16, (uint32_t)n->cx);
        pe_wr32(g + 20, (uint32_t)n->cy);
        pe_wr32(g + 24, n->flags);
    } else {
        n->hwnd = (HWND)pe_rd32(g);
        n->hwndInsertAfter = (HWND)pe_rd32(g + 4);
        n->x = (int)pe_rd32(g + 8);
        n->y = (int)pe_rd32(g + 12);
        n->cx = (int)pe_rd32(g + 16);
        n->cy = (int)pe_rd32(g + 20);
        n->flags = pe_rd32(g + 24);
    }
}

/* DRAWITEMSTRUCT: CtlType, CtlID, itemID, itemAction, itemState, hwndItem,
 * hDC, rcItem, itemData */
static void drawitem(const DRAWITEMSTRUCT *d, uint8_t *g)
{
    pe_wr32(g, d->CtlType);
    pe_wr32(g + 4, d->CtlID);
    pe_wr32(g + 8, d->itemID);
    pe_wr32(g + 12, d->itemAction);
    pe_wr32(g + 16, d->itemState);
    pe_wr32(g + 20, d->hwndItem);
    pe_wr32(g + 24, d->hDC);
    memcpy(g + 28, &d->rcItem, 16);
    pe_wr32(g + 44, d->itemData);
}

/* The program's window (dialog, subclass) procedure proc with a native
 * message: its parameters as Win32 has them, a copy where lParam points
 * to a structure it reads or fills. */
static LRESULT message(pe_user *u, uint32_t proc, HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    win_pe_ctx *c = u->ctx;
    WNDPROC native = token_native(u, proc);
    if (native)
        return CallWindowProc(native, h, msg, wp, lp);
    UINT m = msg;
    uint32_t w = (uint32_t)wp, l = (uint32_t)lp;
    to_guest(ctl_kind(h), &m, &w, &l);
    uint8_t blob[256];
    uint32_t len = 0, at = 0;
    int lend = 0;
    void *nat = (void *)(uintptr_t)lp;
    switch (msg) {
    case WM_CREATE: case WM_NCCREATE:
        if (!lp)
            break;
        at = win_pe_blob(c, sizeof(blob));
        len = create_to_guest(u, (const CREATESTRUCT *)nat, at, blob, sizeof(blob));
        lend = 1;
        break;
    case WM_GETMINMAXINFO:
        if (lp)
            memcpy(blob, nat, len = sizeof(MINMAXINFO));
        break;
    case WM_NCCALCSIZE:
        if (lp && !wp)
            memcpy(blob, nat, len = sizeof(RECT));
        break;
    case WM_WINDOWPOSCHANGING: case WM_WINDOWPOSCHANGED:
        if (lp) {
            windowpos((WINDOWPOS *)nat, blob, 1);
            len = 28;
            lend = 1;
        }
        break;
    case WM_DRAWITEM:
        if (lp) {
            drawitem((const DRAWITEMSTRUCT *)nat, blob);
            len = 48;
            lend = 1;
        }
        break;
    case WM_SETTEXT: case WM_WININICHANGE: {
        const char *s = (const char *)nat;
        size_t n = 0;
        while (s && n + 1 < sizeof(blob) && s[n])
            n++;
        if (s) {
            memcpy(blob, s, n);
            blob[n] = 0;
            len = (uint32_t)((n + 4) & ~3u);
        }
        break;
    }
    case WM_GETTEXT:
        if (lp && wp) {
            len = wp < sizeof(blob) ? (uint32_t)((wp + 3) & ~3u) : sizeof(blob);
            memset(blob, 0, len);
        }
        break;
    }
    if (len) {
        at = win_pe_blob(c, len);
        if (lend && u->lents < PE_LENT && at) {
            u->lent[u->lents].addr = at;
            u->lent[u->lents].native = nat;
            u->lent[u->lents++].msg = msg;
        } else {
            lend = 0;
        }
        if ((msg == WM_CREATE || msg == WM_NCCREATE) && lp)  /* the strings' place known now */
            create_to_guest(u, (const CREATESTRUCT *)nat, at, blob, sizeof(blob));
        l = at;
    }
    const uint32_t args[4] = { h, m, w, l };
    uint32_t r = win_pe_call(c, proc, args, 4, len ? blob : NULL, len, NULL);
    if (lend)
        u->lents--;
    if (!len)
        return (LRESULT)r;
    switch (msg) {                          /* what it set in its copy */
    case WM_GETMINMAXINFO: memcpy(nat, blob, sizeof(MINMAXINFO)); break;
    case WM_NCCALCSIZE: memcpy(nat, blob, sizeof(RECT)); break;
    case WM_WINDOWPOSCHANGING: windowpos((WINDOWPOS *)nat, blob, 0); break;
    case WM_GETTEXT: {
        uint32_t n = r < wp ? r : (uint32_t)wp - 1u;
        if (n >= len)
            n = len ? len - 1u : 0;
        memcpy(nat, blob, n);
        ((char *)nat)[n] = 0;
        break;
    }
    }
    return (LRESULT)r;
}

static int lent_of(pe_user *u, uint32_t addr, UINT msg)
{
    for (int i = u->lents - 1; i >= 0; i--)
        if (u->lent[i].addr == addr && u->lent[i].msg == msg)
            return i;
    return -1;
}

/* the controls' messages whose lParam is a string or a buffer (Win16's
 * numbers); a list's strings are no pointers when it draws them itself */
static int ctl_pointer(HWND h, int kind, UINT msg)
{
    switch (kind) {
    case CTL_EDIT:
        return msg == WM_USER + 2 || msg == WM_USER + 3 || msg == WM_USER + 4 ||
               msg == WM_USER + 0x12 || msg == WM_USER + 0x14 || msg == WM_USER + 0x1b;
    case CTL_LISTBOX: {
        int s = msg == WM_USER + 1 || msg == WM_USER + 2 || msg == WM_USER + 10 ||
                msg == WM_USER + 13 || msg == WM_USER + 16 || msg == WM_USER + 35;
        DWORD style = (DWORD)GetWindowLong(h, GWL_STYLE);
        if (s && (style & 0x30u) && !(style & 0x40u))   /* owner-drawn, no strings */
            return 0;
        return s || msg == WM_USER + 14 || msg == WM_USER + 18 || msg == WM_USER + 19 ||
               msg == WM_USER + 25;
    }
    case CTL_COMBOBOX: {
        int s = msg == WM_USER + 3 || msg == WM_USER + 8 || msg == WM_USER + 10 ||
                msg == WM_USER + 12 || msg == WM_USER + 13 || msg == WM_USER + 24;
        DWORD style = (DWORD)GetWindowLong(h, GWL_STYLE);
        if (s && (style & 0x30u) && !(style & 0x200u))
            return 0;
        return s || msg == WM_USER + 5 || msg == WM_USER + 18;
    }
    }
    return 0;
}

/* The program's message for a native procedure (fn; NULL: SendMessage,
 * DefWindowProc ...: call) with its parameters converted: a copy lent
 * back to the native structure, a pointer of the program's to WINN's
 * where the message has one. */
typedef LRESULT (WINAPI *native_fn)(HWND, UINT, WPARAM, LPARAM);

static __attribute__((noinline)) LRESULT send_native(pe_args *a, native_fn fn, WNDPROC proc, HWND h, UINT msg,
                           uint32_t wp, uint32_t lp)
{
    pe_user *u = a->ctx->user;
    int kind = ctl_kind(h);
    UINT m = msg;
    uint32_t w = wp, l = lp;
    to_native(kind, &m, &w, &l);
    int lent = u ? lent_of(u, lp, m) : -1;
    LPARAM nl = (LPARAM)l;
    if (lent >= 0) {
        nl = (LPARAM)(uintptr_t)u->lent[lent].native;
        if (m == WM_WINDOWPOSCHANGING || m == WM_WINDOWPOSCHANGED) {
            uint8_t *g = (uint8_t *)pe_buf(a, lp, 28);
            if (g)
                windowpos((WINDOWPOS *)u->lent[lent].native, g, 0);
        }
    } else if (l && (m == WM_SETTEXT || m == WM_GETTEXT || m == WM_WININICHANGE ||
                     m == WM_GETMINMAXINFO || (m == WM_NCCALCSIZE && !w) ||
                     ctl_pointer(h, kind, m))) {
        nl = (LPARAM)(uintptr_t)pe_buf(a, l, 1);
        if (!nl)
            return 0;
    }
    LRESULT r = proc ? CallWindowProc(proc, h, m, (WPARAM)w, nl) : fn(h, m, (WPARAM)w, nl);
    if (lent >= 0 && m == WM_WINDOWPOSCHANGING) {
        uint8_t *g = (uint8_t *)pe_buf(a, lp, 28);
        if (g)
            windowpos((WINDOWPOS *)u->lent[lent].native, g, 1);
    }
    /* EM_GETSEL's and CB_GETEDITSEL's: the start and the end where wParam
     * and lParam point */
    if ((kind == CTL_EDIT && msg == 0xb0) || (kind == CTL_COMBOBOX && msg == 0x140)) {
        uint8_t *s = wp ? (uint8_t *)pe_buf(a, wp, 4) : NULL;
        uint8_t *e = lp ? (uint8_t *)pe_buf(a, lp, 4) : NULL;
        if (s)
            pe_wr32(s, LO(r));
        if (e)
            pe_wr32(e, HI(r));
    }
    return r;
}

/* ---------------------------------------------------------------- window procedures */

static int own_class(pe_user *u, const char *name)
{
    for (int i = 0; i < PE_CLASSES; i++)
        if (u->cls[i].proc && !lstrcmpi(u->cls[i].name, name))
            return i;
    return -1;
}

static uint32_t window_proc(pe_user *u, HWND h)
{
    for (int i = 0; i < PE_SUBCLASSED; i++)
        if (u->sub[i].hwnd == h && u->sub[i].proc && !u->sub[i].old)
            return u->sub[i].proc;
    char name[64];
    GetClassName(h, name, sizeof(name));
    int c = own_class(u, name);
    return c >= 0 ? u->cls[c].proc : 0;
}

static LRESULT CALLBACK pe_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    pe_user *u = user_of(GetWindowTask(h));
    if (alive(u)) {
        uint32_t proc = window_proc(u, h);
        if (proc) {
            LRESULT r = message(u, proc, h, msg, wp, lp);
            if (msg == WM_NCDESTROY)
                for (int i = 0; i < PE_SUBCLASSED; i++)
                    if (u->sub[i].hwnd == h)
                        u->sub[i].hwnd = 0;
            return r;
        }
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* a native window (a control) the program subclassed */
static LRESULT CALLBACK pe_subproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    pe_user *u = user_of(GetWindowTask(h));
    for (pe_user *p = programs; !u && p; p = p->next)
        for (int i = 0; i < PE_SUBCLASSED; i++)
            if (p->sub[i].hwnd == h)
                u = p;
    for (int i = 0; u && i < PE_SUBCLASSED; i++)
        if (u->sub[i].hwnd == h && u->sub[i].old) {
            WNDPROC old = u->sub[i].old;
            if (!alive(u))
                return CallWindowProc(old, h, msg, wp, lp);
            LRESULT r = message(u, u->sub[i].proc, h, msg, wp, lp);
            if (msg == WM_NCDESTROY)
                u->sub[i].hwnd = 0;
            return r;
        }
    return DefWindowProc(h, msg, wp, lp);
}

static BOOL CALLBACK pe_dlgproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    pe_user *u = user_of(GetWindowTask(h));
    if (!alive(u))
        return FALSE;
    int slot = -1;
    for (int i = 0; i < PE_DIALOGS; i++)
        if (u->dlg[i].hwnd == h)
            slot = i;
    for (int i = 0; slot < 0 && u->pending && i < PE_DIALOGS; i++)
        if (!u->dlg[i].hwnd) {
            slot = i;
            u->dlg[i].hwnd = h;
            u->dlg[i].proc = u->pending;
            u->dlg[i].param = u->pending_param;
            u->pending = 0;
        }
    if (slot < 0)
        return FALSE;
    if (msg == WM_INITDIALOG)
        lp = (LPARAM)u->dlg[slot].param;
    BOOL r = message(u, u->dlg[slot].proc, h, msg, wp, lp) != 0;
    if (msg == WM_NCDESTROY)
        u->dlg[slot].hwnd = 0;
    return r;
}

static void CALLBACK pe_timerproc(HWND h, UINT msg, UINT id, DWORD time)
{
    pe_user *u = user_of(h ? GetWindowTask(h) : GetCurrentTask());
    for (int i = 0; alive(u) && i < PE_TIMERS; i++)
        if (u->timer[i].proc && u->timer[i].hwnd == h && u->timer[i].id == id) {
            const uint32_t args[4] = { h, msg, id, time };
            win_pe_call(u->ctx, u->timer[i].proc, args, 4, NULL, 0, NULL);
            return;
        }
}

typedef struct { pe_user *u; uint32_t proc, lp; } enum_ctx;

static BOOL CALLBACK pe_enumproc(HWND h, LPARAM lp)
{
    const enum_ctx *e = (const enum_ctx *)(uintptr_t)lp;
    const uint32_t args[2] = { h, e->lp };
    return win_pe_call(e->u->ctx, e->proc, args, 2, NULL, 0, NULL) != 0;
}

/* ---------------------------------------------------------------- resources */

enum { RT_CURSOR_ = 1, RT_BITMAP_ = 2, RT_ICON_ = 3, RT_MENU_ = 4, RT_DIALOG_ = 5,
       RT_STRING_ = 6, RT_ACCELERATOR_ = 9, RT_GROUP_CURSOR_ = 12, RT_GROUP_ICON_ = 14 };

#define RT(n) ((const char *)(uintptr_t)(n))

/* a resource of the module of an instance: its bytes (the program's
 * memory) and size */
static const uint8_t *resource(pe_args *a, int m, int type, const char *name, uint32_t *n)
{
    uint32_t data = 0;
    if (m < 0 || !win_pe_resource(a->ctx->img, m, RT(type), name, &data, n))
        return NULL;
    return (const uint8_t *)win_pe_ptr(a->ctx->img, data, *n);
}

/* a Unicode string of a resource as a native one; *p past it */
static const char *ustr(const uint8_t **p, const uint8_t *end, char *out, size_t cap)
{
    size_t n = 0;
    while (*p + 2 <= end) {
        uint16_t c = pe_rd16(*p);
        *p += 2;
        if (!c) {
            out[n] = 0;
            return out;
        }
        if (n + 1 < cap)
            out[n++] = c < 256 ? (char)c : '?';
    }
    return NULL;
}

/* a name or a number (FFFFh, then it) of a template: a native LPCSTR */
static const char *sz_or_ord(const uint8_t **p, const uint8_t *end, char *out, size_t cap)
{
    if (*p + 2 > end)
        return NULL;
    if (pe_rd16(*p) == 0xffff) {
        if (*p + 4 > end)
            return NULL;
        uint16_t v = pe_rd16(*p + 2);
        *p += 4;
        return (const char *)(uintptr_t)v;
    }
    return ustr(p, end, out, cap);
}

static const uint8_t *menu_items(HMENU m, const uint8_t *p, const uint8_t *end, int ex)
{
    for (;;) {
        char text[128];
        UINT flags, id = 0, last;
        if (ex) {                           /* MENUEX: type, state, id, flags (word), text */
            p = (const uint8_t *)(((uintptr_t)p + 3) & ~(uintptr_t)3);
            if (p + 14 > end)
                return NULL;
            UINT type = pe_rd32(p), state = pe_rd32(p + 4);
            id = pe_rd32(p + 8);
            UINT res = pe_rd16(p + 12);
            p += 14;
            if (!ustr(&p, end, text, sizeof(text)))
                return NULL;
            last = res & 0x80u;
            flags = (type & 0x900u ? MF_SEPARATOR : 0) | (state & 0x0bu);
            if (res & 1u) {
                p = (const uint8_t *)(((uintptr_t)p + 3) & ~(uintptr_t)3);
                p += 4;                     /* its help id */
                HMENU sub = CreatePopupMenu();
                if (!(p = menu_items(sub, p, end, 1)))
                    return NULL;
                AppendMenu(m, flags | MF_POPUP, (UINT)sub, text);
            } else if ((type & 0x800u) || (!id && !text[0])) {
                AppendMenu(m, MF_SEPARATOR, 0, NULL);
            } else {
                AppendMenu(m, flags, id, text);
            }
        } else {
            if (p + 2 > end)
                return NULL;
            flags = pe_rd16(p);
            p += 2;
            if (!(flags & MF_POPUP)) {
                if (p + 2 > end)
                    return NULL;
                id = pe_rd16(p);
                p += 2;
            }
            if (!ustr(&p, end, text, sizeof(text)))
                return NULL;
            last = flags & 0x80u;
            UINT f = flags & ~0x80u;
            if (flags & MF_POPUP) {
                HMENU sub = CreatePopupMenu();
                if (!(p = menu_items(sub, p, end, 0)))
                    return NULL;
                AppendMenu(m, f, (UINT)sub, text);
            } else if (!f && !id && !text[0]) {
                AppendMenu(m, MF_SEPARATOR, 0, NULL);
            } else {
                AppendMenu(m, f, id, text);
            }
        }
        if (last)
            return p;
    }
}

static HMENU menu_from(const uint8_t *d, uint32_t n)
{
    if (!d || n < 4)
        return 0;
    int ex = pe_rd16(d) == 1;
    HMENU m = CreateMenu();
    if (!menu_items(m, d + 4 + (ex ? pe_rd16(d + 2) : 0), d + n, ex)) {
        DestroyMenu(m);
        return 0;
    }
    return m;
}

static HMENU load_menu(pe_args *a, int m, const char *name)
{
    uint32_t n = 0;
    const uint8_t *d = resource(a, m, RT_MENU_, name, &n);
    return menu_from(d, n);
}

static __attribute__((noinline)) HACCEL load_accelerators(pe_args *a, pe_user *u, int m, const char *name)
{
    uint32_t n;
    const uint8_t *d = resource(a, m, RT_ACCELERATOR_, name, &n);
    ACCEL_N acc[64];
    int k = 0;
    for (uint32_t i = 0; d && i + 8 <= n && k < 64; i += 8) {
        acc[k].fVirt = (BYTE)(d[i] & 0x1f);
        acc[k].key = pe_rd16(d + i + 2);
        acc[k++].cmd = pe_rd16(d + i + 4);
        if (d[i] & 0x80)
            break;
    }
    HACCEL h = k ? CreateAcceleratorTableN(acc, k) : 0;
    for (int i = 0; h && i < PE_ACCELS; i++)
        if (!u->accel[i]) {
            u->accel[i] = h;
            break;
        }
    return h;
}

/* an icon (kind 2), cursor (1) or bitmap (3) of a module, once each */
static __attribute__((noinline)) HANDLE load_image(pe_args *a, pe_user *u, int kind, int m, const char *name)
{
    uint32_t id = (uintptr_t)name < 0x10000u ? (uint32_t)(uintptr_t)name : 0;
    const char *nm = id ? "" : name;
    int free_slot = -1;
    for (int i = 0; i < PE_IMAGES; i++) {
        if (!u->image[i].h) {
            if (free_slot < 0)
                free_slot = i;
            continue;
        }
        if (kind != 3 && u->image[i].kind == kind && u->image[i].module == m &&
            u->image[i].id == id && !lstrcmpi(u->image[i].name, nm))
            return u->image[i].h;
    }
    uint32_t n;
    HANDLE h = 0;
    if (kind == 3) {
        const uint8_t *d = resource(a, m, RT_BITMAP_, name, &n);
        return d ? gdi_bitmap_from_dib(d, n) : 0;
    }
    const uint8_t *g = resource(a, m, kind == 1 ? RT_GROUP_CURSOR_ : RT_GROUP_ICON_, name, &n);
    uint16_t entry;
    if (g && win_group_pick(g, n, kind == 1, &entry)) {
        const uint8_t *d = resource(a, m, kind == 1 ? RT_CURSOR_ : RT_ICON_,
                                    (const char *)(uintptr_t)entry, &n);
        if (d && kind == 1 && n > 4)
            h = win_cursor_from_dib(d + 4, n - 4, pe_rd16(d), pe_rd16(d + 2));
        else if (d && kind == 2)
            h = win_icon_from_dib(d, n);
    }
    if (h && free_slot >= 0) {
        u->image[free_slot].h = h;
        u->image[free_slot].kind = (uint8_t)kind;
        u->image[free_slot].module = m;
        u->image[free_slot].id = id;
        lstrcpyn(u->image[free_slot].name, nm, sizeof(u->image[free_slot].name));
    }
    return h;
}

/* LoadString: string id is the (id % 16)th of block id / 16 + 1, each a
 * length (a word) and its Unicode characters */
static __attribute__((noinline)) uint32_t load_string(pe_args *a, int m, uint32_t id, uint32_t buf, int32_t max)
{
    uint32_t n;
    const uint8_t *d = resource(a, m, RT_STRING_, (const char *)(uintptr_t)(id / 16 + 1), &n);
    uint32_t at = 0;
    for (unsigned i = 0; d && i < id % 16u && at + 2 <= n; i++)
        at += 2u + 2u * pe_rd16(d + at);
    if (max <= 0)
        return 0;
    char *out = (char *)pe_buf(a, buf, (uint32_t)max);
    if (!out)
        return 0;
    uint32_t len = 0;
    if (d && at + 2 <= n) {
        len = pe_rd16(d + at);
        if (at + 2u + 2u * len > n)
            len = (n - at - 2u) / 2u;
        if (len > (uint32_t)max - 1u)
            len = (uint32_t)max - 1u;
        for (uint32_t i = 0; i < len; i++) {
            uint16_t c = pe_rd16(d + at + 2 + 2 * i);
            out[i] = c < 256 ? (char)c : '?';
        }
    }
    out[len] = 0;
    return len;
}

/* ---------------------------------------------------------------- dialogs */

/* A Win32 dialog template (DLGTEMPLATE or DLGTEMPLATEEX, Unicode) as
 * WINN's; the dialog made (modeless) or run. */
static __attribute__((noinline)) int dialog(pe_args *a, pe_user *u, int m, const uint8_t *d, uint32_t n, HWND owner,
                  uint32_t proc, uint32_t param, int modeless)
{
    static const char *const builtin[] = { "BUTTON", "EDIT", "STATIC", "LISTBOX",
                                           "SCROLLBAR", "COMBOBOX" };
    if (!d || n < 18)
        return -1;
    const uint8_t *p = d, *end = d + n;
    int ex = pe_rd16(d) == 1 && pe_rd16(d + 2) == 0xffff;
    DLGTEMPLATE_N t;
    memset(&t, 0, sizeof(t));
    unsigned count;
    if (ex) {
        if (n < 26)
            return -1;
        t.style = pe_rd32(p + 12);
        count = pe_rd16(p + 16);
        p += 18;
    } else {
        t.style = pe_rd32(p);
        count = pe_rd16(p + 8);
        p += 10;
    }
    t.x = (int16_t)pe_rd16(p);
    t.y = (int16_t)pe_rd16(p + 2);
    t.cx = (int16_t)pe_rd16(p + 4);
    t.cy = (int16_t)pe_rd16(p + 6);
    p += 8;
    /* the strings: an arena, so the template's pointers stay */
    char *arena = (char *)win_alloc(8192);
    DLGITEM_N *items = (DLGITEM_N *)win_alloc(sizeof(DLGITEM_N) * 64);
    int r = -1;
    HMENU hmenu = 0;
    size_t used = 0;
#define TAKE(sz) (used + (sz) <= 8192 ? arena + (used += (sz)) - (sz) : NULL)
    if (!arena || !items)
        goto done;
    char *buf = TAKE(64);
    const char *menu = sz_or_ord(&p, end, buf, 64);
    buf = TAKE(64);
    const char *cls = buf ? sz_or_ord(&p, end, buf, 64) : NULL;
    buf = TAKE(128);
    const char *caption = buf ? ustr(&p, end, buf, 128) : NULL;
    if (!buf || !caption || (!cls && p > end))
        goto done;
    t.klass = cls && ((uintptr_t)cls < 0x10000u || *cls) ? cls : NULL;
    t.caption = caption;
    if (menu && !(t.style & WS_CHILD) && ((uintptr_t)menu < 0x10000u || *menu))
        hmenu = load_menu(a, m, menu);
    if (t.style & DS_SETFONT) {
        if (p + 2 > end)
            goto done;
        t.pointsize = pe_rd16(p);
        p += ex ? 6 : 2;
        buf = TAKE(64);
        if (!buf || !(t.font = ustr(&p, end, buf, 64)))
            goto done;
    }
    unsigned k = 0;
    for (; k < count && k < 64; k++) {
        DLGITEM_N *it = &items[k];
        p = d + ((p - d + 3) & ~3);
        if (p + (ex ? 24 : 18) > end)
            goto done;
        if (ex) {
            it->style = pe_rd32(p + 8);
            p += 12;
        } else {
            it->style = pe_rd32(p);
            p += 8;
        }
        it->x = (int16_t)pe_rd16(p);
        it->y = (int16_t)pe_rd16(p + 2);
        it->cx = (int16_t)pe_rd16(p + 4);
        it->cy = (int16_t)pe_rd16(p + 6);
        it->id = (int16_t)pe_rd16(p + 8);
        p += ex ? 12 : 10;
        buf = TAKE(32);
        const char *kl = buf ? sz_or_ord(&p, end, buf, 32) : NULL;
        if (!kl && p > end)
            goto done;
        it->klass = (uintptr_t)kl < 0x10000u ? ((uintptr_t)kl >= 0x80 && (uintptr_t)kl < 0x86 ?
                                                 builtin[(uintptr_t)kl - 0x80] : "STATIC") : kl;
        buf = TAKE(128);
        const char *text = buf ? sz_or_ord(&p, end, buf, 128) : NULL;
        int icon = !lstrcmpi(it->klass, "STATIC") && (it->style & 0x0f) == SS_ICON;
        if ((uintptr_t)text < 0x10000u) {
            HICON ic = icon && text ? (HICON)load_image(a, u, 2, m, text) : 0;
            if (ic) {
                char *s = TAKE(8);
                if (s)
                    wsprintf(s, "\001%04X", (UINT)ic);
                it->text = s ? s : "";
            } else {
                it->text = "";
            }
        } else {
            it->text = text;
            if (icon && text[0]) {
                HICON ic = (HICON)load_image(a, u, 2, m, text);
                char *s = ic ? TAKE(8) : NULL;
                if (s) {
                    wsprintf(s, "\001%04X", (UINT)ic);
                    it->text = s;
                }
            }
        }
        if (p + 2 > end)
            goto done;
        p += 2 + pe_rd16(p);                /* its creation data */
    }
    t.cdit = (BYTE)k;
    {
        uint32_t outer = u->pending, outer_param = u->pending_param;
        u->pending = proc;
        u->pending_param = param;
        user_dialog_menu(hmenu);
        HINSTANCE inst = (HINSTANCE)u->task;
        r = modeless ? (int)CreateDialogIndirectN(inst, &t, items, owner, pe_dlgproc)
                     : DialogBoxIndirectN(inst, &t, items, owner, pe_dlgproc);
        u->pending = outer;
        u->pending_param = outer_param;
        hmenu = 0;
    }
done:
#undef TAKE
    if (hmenu)
        DestroyMenu(hmenu);
    win_free(items);
    win_free(arena);
    return r;
}

/* ---------------------------------------------------------------- wsprintf */

/* wvsprintf: the arguments dwords (a double two), %s a string of the
 * program's, %S (%ls) a Unicode one */
static __attribute__((noinline)) uint32_t format(pe_args *a, char *out, uint32_t cap, const char *fmt, uint32_t list)
{
    win_pe_image *img = a->ctx->img;
    uint32_t n = 0;
    while (*fmt && n + 1 < cap) {
        if (*fmt != '%') {
            out[n++] = *fmt++;
            continue;
        }
        const char *start = fmt++;
        if (*fmt == '%') {
            out[n++] = '%';
            fmt++;
            continue;
        }
        char spec[24];
        size_t s = 0;
        spec[s++] = '%';
        while (*fmt == '-' || *fmt == '0' || *fmt == '#' || *fmt == ' ' || *fmt == '+')
            if (s < 8) spec[s++] = *fmt++; else fmt++;
        while (*fmt >= '0' && *fmt <= '9')
            if (s < 14) spec[s++] = *fmt++; else fmt++;
        if (*fmt == '.') {
            if (s < 15) spec[s++] = *fmt;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9')
                if (s < 20) spec[s++] = *fmt++; else fmt++;
        }
        int wide = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'L' || *fmt == 'w') {
            wide = *fmt == 'l' || *fmt == 'w';
            fmt++;
        }
        char conv = *fmt ? *fmt++ : 0;
        const uint8_t *ap = (const uint8_t *)win_pe_ptr(img, list, 4);
        uint32_t v = ap ? pe_rd32(ap) : 0;
        char piece[300];
        piece[0] = 0;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'c': case 'o':
            spec[s++] = conv == 'i' ? 'd' : conv;
            spec[s] = 0;
            if (conv == 'c')
                pe_fmt(piece, sizeof(piece), spec, (int)(char)v);
            else if (conv == 'd')
                pe_fmt(piece, sizeof(piece), spec, (int)v);
            else
                pe_fmt(piece, sizeof(piece), spec, (unsigned)v);
            list += 4;
            break;
        case 's': case 'S': {
            char str[256];
            int uni = conv == 'S' || wide;
            size_t k = 0;
            if (!v) {
                pe_fmt(str, sizeof(str), "(null)");
            } else {
                for (; k + 1 < sizeof(str); k++) {
                    const uint8_t *c = (const uint8_t *)win_pe_ptr(img, v + (uni ? 2u : 1u) * k,
                                                                   uni ? 2u : 1u);
                    unsigned ch = c ? (uni ? pe_rd16(c) : *c) : 0;
                    if (!ch)
                        break;
                    str[k] = ch < 256 ? (char)ch : '?';
                }
                str[k] = 0;
            }
            spec[s++] = 's';
            spec[s] = 0;
            pe_fmt(piece, sizeof(piece), spec, str);
            list += 4;
            break;
        }
        default:                            /* as it is */
            pe_fmt(piece, sizeof(piece), "%.*s", (int)(fmt - start), start);
            break;
        }
        for (const char *q = piece; *q && n + 1 < cap; q++)
            out[n++] = *q;
    }
    out[n] = 0;
    return n;
}

/* ---------------------------------------------------------------- GDI's structures */

/* Win32's TEXTMETRICA: 11 LONGs, then its characters and bytes */
static void textmetric(const TEXTMETRIC *t, uint8_t *g)
{
    memset(g, 0, 56);
    const int v[11] = { t->tmHeight, t->tmAscent, t->tmDescent, t->tmInternalLeading,
                        t->tmExternalLeading, t->tmAveCharWidth, t->tmMaxCharWidth, t->tmWeight,
                        t->tmOverhang, t->tmDigitizedAspectX, t->tmDigitizedAspectY };
    for (int i = 0; i < 11; i++)
        pe_wr32(g + 4 * i, (uint32_t)v[i]);
    g[44] = t->tmFirstChar;
    g[45] = t->tmLastChar;
    g[46] = t->tmDefaultChar;
    g[47] = t->tmBreakChar;
    g[48] = t->tmItalic;
    g[49] = t->tmUnderlined;
    g[50] = t->tmStruckOut;
    g[51] = t->tmPitchAndFamily;
    g[52] = t->tmCharSet;
}

/* ---------------------------------------------------------------- the services */

/* a message from the program's MSG (28 bytes) */
static int msg_in(pe_args *a, uint32_t p, MSG *m)
{
    const uint8_t *g = (const uint8_t *)pe_buf(a, p, 28);
    if (!g)
        return 0;
    m->hwnd = (HWND)pe_rd32(g);
    m->message = pe_rd32(g + 4);
    uint32_t w = pe_rd32(g + 8), l = pe_rd32(g + 12);
    UINT msg = m->message;
    to_native(ctl_kind(m->hwnd), &msg, &w, &l);
    m->message = msg;
    m->wParam = w;
    m->lParam = (LPARAM)l;
    m->time = pe_rd32(g + 16);
    m->pt.x = (int)pe_rd32(g + 20);
    m->pt.y = (int)pe_rd32(g + 24);
    return 1;
}

static void msg_out(pe_args *a, uint32_t p, const MSG *m)
{
    uint8_t *g = (uint8_t *)pe_buf(a, p, 28);
    if (!g)
        return;
    UINT msg = m->message;
    uint32_t w = (uint32_t)m->wParam, l = (uint32_t)m->lParam;
    to_guest(ctl_kind(m->hwnd), &msg, &w, &l);
    pe_wr32(g, m->hwnd);
    pe_wr32(g + 4, msg);
    pe_wr32(g + 8, w);
    pe_wr32(g + 12, l);
    pe_wr32(g + 16, m->time);
    pe_wr32(g + 20, (uint32_t)m->pt.x);
    pe_wr32(g + 24, (uint32_t)m->pt.y);
}

/* WNDCLASSA at p (40 bytes; WNDCLASSEXA's past its size): the class
 * registered with pe_wndproc, its procedure and menu kept */
static __attribute__((noinline)) uint32_t register_class(pe_args *a, pe_user *u, uint32_t p)
{
    const uint8_t *g = (const uint8_t *)pe_buf(a, p, 40);
    if (!g)
        return 0;
    int i = 0;
    while (i < PE_CLASSES && u->cls[i].proc)
        i++;
    uint32_t name = pe_rd32(g + 36);
    if (i == PE_CLASSES)
        return 0;
    if (name < 0x10000u) {
        if (!GetAtomName((ATOM)name, u->cls[i].name, sizeof(u->cls[i].name)))
            return 0;
    } else {
        const char *s = pe_str(a, name);
        if (!s)
            return 0;
        lstrcpyn(u->cls[i].name, s, sizeof(u->cls[i].name));
    }
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = pe_rd32(g);
    wc.lpfnWndProc = pe_wndproc;
    wc.cbClsExtra = (int)pe_rd32(g + 8);
    wc.cbWndExtra = (int)pe_rd32(g + 12);
    wc.hInstance = pe_instance(u->ctx, pe_rd32(g + 16));
    wc.hIcon = (HICON)pe_rd32(g + 20);
    wc.hCursor = (HCURSOR)pe_rd32(g + 24);
    wc.hbrBackground = (HBRUSH)pe_rd32(g + 28);
    wc.lpszClassName = u->cls[i].name;
    ATOM v = RegisterClass(&wc);
    if (!v)
        return 0;
    u->cls[i].proc = pe_rd32(g + 4);
    u->cls[i].module = pe_module_of_instance(u->ctx, pe_rd32(g + 16));
    uint32_t menu = pe_rd32(g + 32);
    u->cls[i].menu = menu < 0x10000u ? menu : 0;
    u->cls[i].menu_name[0] = 0;
    if (menu >= 0x10000u) {
        const char *s = pe_str(a, menu);
        if (s)
            lstrcpyn(u->cls[i].menu_name, s, sizeof(u->cls[i].menu_name));
    }
    return v;
}

static __attribute__((noinline)) uint32_t create_window(pe_args *a, pe_user *u)
{
    uint32_t exstyle = pe_arg(a, 0), cls = pe_arg(a, 1), title = pe_arg(a, 2);
    uint32_t style = pe_arg(a, 3);
    int x = (int)pe_arg(a, 4), y = (int)pe_arg(a, 5), cx = (int)pe_arg(a, 6), cy = (int)pe_arg(a, 7);
    HWND parent = (HWND)pe_arg(a, 8);
    HMENU menu = (HMENU)pe_arg(a, 9);
    uint32_t inst = pe_arg(a, 10), param = pe_arg(a, 11);
    char cname[64];
    const char *c = cls < 0x10000u ? (GetAtomName((ATOM)cls, cname, sizeof(cname)) ? cname : NULL)
                                   : pe_str(a, cls);
    const char *t = title ? pe_str(a, title) : "";
    if (a->bad || !c)
        return 0;
    int k = (style & WS_CHILD) ? -1 : own_class(u, c);
    if (!menu && k >= 0) {
        if (u->cls[k].menu)
            menu = load_menu(a, u->cls[k].module, (const char *)(uintptr_t)u->cls[k].menu);
        else if (u->cls[k].menu_name[0])
            menu = load_menu(a, u->cls[k].module, u->cls[k].menu_name);
    }
    return CreateWindowEx(exstyle, c, t ? t : "", style, x, y, cx, cy, parent, menu,
                          pe_instance(u->ctx, inst ? inst : 1u), (LPVOID)(uintptr_t)param);
}

/* SetWindowLong(GWL_WNDPROC): the program's procedure for a window of
 * its own classes, or a native one subclassed; the one before as the
 * program holds it */
static uint32_t subclass(pe_user *u, HWND h, uint32_t proc)
{
    WNDPROC native = token_native(u, proc);
    WNDPROC cur = (WNDPROC)(intptr_t)GetWindowLong(h, GWL_WNDPROC);
    uint32_t old;
    int slot = -1, free_slot = -1;
    for (int i = 0; i < PE_SUBCLASSED; i++) {
        if (u->sub[i].hwnd == h)
            slot = i;
        else if (!u->sub[i].hwnd && free_slot < 0)
            free_slot = i;
    }
    if (cur == pe_wndproc)
        old = window_proc(u, h);
    else if (cur == pe_subproc && slot >= 0)
        old = u->sub[slot].proc;
    else
        old = native_token(u, cur);
    if (native) {                           /* a native one back */
        if (slot >= 0)
            u->sub[slot].hwnd = 0;
        SetWindowLong(h, GWL_WNDPROC, (LONG)(intptr_t)native);
        return old;
    }
    if (slot < 0)
        slot = free_slot;
    if (slot < 0)
        return 0;
    u->sub[slot].hwnd = h;
    u->sub[slot].proc = proc;
    if (cur == pe_wndproc) {
        u->sub[slot].old = NULL;            /* its class's, replaced */
    } else {
        if (cur != pe_subproc)
            u->sub[slot].old = cur;
        SetWindowLong(h, GWL_WNDPROC, (LONG)(intptr_t)pe_subproc);
    }
    return old;
}

static uint32_t userdata(pe_user *u, HWND h, int set, uint32_t v)
{
    int free_slot = -1;
    for (int i = 0; i < PE_USERDATA; i++) {
        if (u->userdata[i].hwnd == h && IsWindow(h)) {
            uint32_t old = u->userdata[i].value;
            if (set)
                u->userdata[i].value = v;
            return old;
        }
        if ((!u->userdata[i].hwnd || !IsWindow(u->userdata[i].hwnd)) && free_slot < 0)
            free_slot = i;
    }
    if (set && free_slot >= 0) {
        u->userdata[free_slot].hwnd = h;
        u->userdata[free_slot].value = v;
    }
    return 0;
}

static uint32_t window_long(pe_args *a, pe_user *u, int set)
{
    HWND h = (HWND)pe_arg(a, 0);
    int index = (int)pe_arg(a, 1);
    uint32_t v = pe_arg(a, 2);
    switch (index) {
    case -4:                                /* GWL_WNDPROC */
        if (set)
            return subclass(u, h, v);
        {
            WNDPROC cur = (WNDPROC)(intptr_t)GetWindowLong(h, GWL_WNDPROC);
            if (cur == pe_wndproc)
                return window_proc(u, h);
            for (int i = 0; cur == pe_subproc && i < PE_SUBCLASSED; i++)
                if (u->sub[i].hwnd == h)
                    return u->sub[i].proc;
            return native_token(u, cur);
        }
    case -6: {                              /* GWL_HINSTANCE */
        uint32_t i = GetWindowWord(h, -6);
        return i == (uint32_t)u->task ? u->ctx->img->mod[0].base : i;
    }
    case -8: case -12:                      /* GWL_HWNDPARENT, GWL_ID */
        return set ? SetWindowWord(h, index, (WORD)v) : GetWindowWord(h, index);
    case -21:                               /* GWL_USERDATA */
        return userdata(u, h, set, v);
    case 4:                                 /* DWL_DLGPROC of its dialogs */
        for (int i = 0; i < PE_DIALOGS; i++)
            if (u->dlg[i].hwnd == h) {
                uint32_t old = u->dlg[i].proc;
                if (set)
                    u->dlg[i].proc = v;
                return old;
            }
        break;
    }
    return set ? (uint32_t)SetWindowLong(h, index, (LONG)v) : (uint32_t)GetWindowLong(h, index);
}

/* GetClassInfo(Ex): WNDCLASSA at buf (WNDCLASSEXA's past its size) */
static uint32_t class_info(pe_args *a, pe_user *u, uint32_t inst, uint32_t name, uint8_t *g)
{
    char cname[64];
    const char *c = name < 0x10000u ? (GetAtomName((ATOM)name, cname, sizeof(cname)) ? cname : NULL)
                                    : pe_str(a, name);
    WNDCLASS wc;
    if (!c || !g || !GetClassInfo(pe_instance(u->ctx, inst), c, &wc))
        return 0;
    int k = own_class(u, c);
    pe_wr32(g, wc.style);
    pe_wr32(g + 4, k >= 0 ? u->cls[k].proc : native_token(u, wc.lpfnWndProc));
    pe_wr32(g + 8, (uint32_t)wc.cbClsExtra);
    pe_wr32(g + 12, (uint32_t)wc.cbWndExtra);
    pe_wr32(g + 16, k >= 0 ? inst : 0);
    pe_wr32(g + 20, wc.hIcon);
    pe_wr32(g + 24, wc.hCursor);
    pe_wr32(g + 28, wc.hbrBackground);
    pe_wr32(g + 32, k >= 0 ? u->cls[k].menu : 0);
    pe_wr32(g + 36, name);
    return 1;
}

/* the module an instance's resources are of; -1 the system's (0) */
static int res_module(pe_args *a, uint32_t inst)
{
    return inst ? pe_module_of_instance(a->ctx, inst) : -1;
}

/* the calls with buffers of their own: not in pe_user_api()'s frame,
 * which nested window procedures stack up */
static __attribute__((noinline)) int user_more(pe_args *a, pe_user *u, uint16_t service,
                                               uint32_t *eax)
{
    win_pe_ctx *ctx = a->ctx;
    uint32_t v = 0;
    switch (service) {
    case PEF_GetObjectA: {
        HGDIOBJ obj = (HGDIOBJ)pe_arg(a, 0);
        int32_t size = (int32_t)pe_arg(a, 1);
        uint32_t buf = pe_arg(a, 2);
        uint8_t tmp[sizeof(LOGFONT) + sizeof(BITMAP) + 16];
        int n = GetObject(obj, sizeof(tmp), tmp);
        if (n <= 0)
            break;
        uint8_t g[60];
        int gn = n;
        memset(g, 0, sizeof(g));
        if (n == (int)sizeof(BITMAP)) {     /* BITMAP: LONGs, WORDs, the bits */
            const BITMAP *b = (const BITMAP *)tmp;
            pe_wr32(g, (uint32_t)b->bmType);
            pe_wr32(g + 4, (uint32_t)b->bmWidth);
            pe_wr32(g + 8, (uint32_t)b->bmHeight);
            pe_wr32(g + 12, (uint32_t)b->bmWidthBytes);
            pe_wr16(g + 16, b->bmPlanes);
            pe_wr16(g + 18, b->bmBitsPixel);
            gn = 24;
        } else {
            memcpy(g, tmp, (size_t)(n < 60 ? n : 60));
            gn = n < 60 ? n : 60;
        }
        if (!buf) {
            v = (uint32_t)gn;
            break;
        }
        if (size < gn)
            gn = size;
        uint8_t *p = gn > 0 ? (uint8_t *)pe_buf(a, buf, (uint32_t)gn) : NULL;
        if (p) {
            memcpy(p, g, (size_t)gn);
            v = (uint32_t)gn;
        }
        break;
    }
    case PEF_GetTextMetricsA: {
        uint8_t *g = (uint8_t *)pe_buf(a, pe_arg(a, 1), 56);
        TEXTMETRIC tm;
        if (g && GetTextMetrics((HDC)pe_arg(a, 0), &tm)) {
            textmetric(&tm, g);
            v = 1;
        }
        break;
    }
    case PEF_MsgWaitForMultipleObjects: {
        /* none of its objects is ever set here: input, or its time */
        uint32_t ms = pe_arg(a, 3);
        DWORD start = GetTickCount();
        MSG m;
        for (;;) {
            if (PeekMessage(&m, 0, 0, 0, PM_NOREMOVE)) {
                v = pe_arg(a, 0);           /* WAIT_OBJECT_0 + n: input */
                break;
            }
            if (ms != 0xffffffffu && GetTickCount() - start >= ms) {
                v = 0x102;
                break;
            }
            if (ctx->yield && ctx->yield()) {
                ctx->stopped = 1;
                return WINX86_SERVICE_EXIT;
            }
        }
        break;
    }
    case PEF_GetClassInfoA:
        v = class_info(a, u, pe_arg(a, 0), pe_arg(a, 1), (uint8_t *)pe_buf(a, pe_arg(a, 2), 40));
        break;
    case PEF_GetClassInfoExA: {
        uint8_t *g = (uint8_t *)pe_buf(a, pe_arg(a, 2), 48);
        v = g && class_info(a, u, pe_arg(a, 0), pe_arg(a, 1), g + 4);
        if (v)
            pe_wr32(g + 44, pe_rd32(g + 24));        /* hIconSm: the icon */
        break;
    }
    case PEF_UnregisterClassA: {
        uint32_t n = pe_arg(a, 0);
        char cname[64];
        const char *c = n < 0x10000u ? (GetAtomName((ATOM)n, cname, sizeof(cname)) ? cname : NULL)
                                     : pe_str(a, n);
        if (!c)
            break;
        int k = own_class(u, c);
        v = UnregisterClass(c, pe_instance(ctx, pe_arg(a, 1)));
        if (v && k >= 0)
            u->cls[k].proc = 0;
        break;
    }
    default:
        return 0;
    }
    *eax = v;
    return 1;
}

int pe_user_api(pe_args *a, uint16_t service, uint32_t *eax)
{
    win_pe_ctx *ctx = a->ctx;
    pe_user *u = ctx->user;
    if (!u)
        return 0;
    uint32_t v = 0;
    if (service >= WIN_PE_SERVICE_NATIVE && service < WIN_PE_SERVICE_NATIVE + WIN_PE_NATIVE_PROCS) {
        WNDPROC p = u->native[service - WIN_PE_SERVICE_NATIVE];
        *eax = p ? (uint32_t)send_native(a, NULL, p, (HWND)pe_arg(a, 0), pe_arg(a, 1),
                                         pe_arg(a, 2), pe_arg(a, 3)) : 0;
        return 1;
    }
    switch (service) {
    case PEF_MessageBoxA: {
        const char *text = pe_str(a, pe_arg(a, 1));
        const char *cap = pe_arg(a, 2) ? pe_str(a, pe_arg(a, 2)) : "Error";
        if (!a->bad)
            v = (uint32_t)MessageBox((HWND)pe_arg(a, 0), text ? text : "", cap ? cap : "",
                                     pe_arg(a, 3));
        break;
    }
    case PEF_RegisterClassA: v = register_class(a, u, pe_arg(a, 0)); break;
    case PEF_RegisterClassExA: v = register_class(a, u, pe_arg(a, 0) + 4u); break;
    case PEF_CreateWindowExA: v = create_window(a, u); break;
    case PEF_DefWindowProcA:
        v = (uint32_t)send_native(a, DefWindowProc, NULL, (HWND)pe_arg(a, 0), pe_arg(a, 1),
                                  pe_arg(a, 2), pe_arg(a, 3));
        break;
    case PEF_DefDlgProcA:
        v = (uint32_t)send_native(a, DefDlgProc, NULL, (HWND)pe_arg(a, 0), pe_arg(a, 1),
                                  pe_arg(a, 2), pe_arg(a, 3));
        break;
    case PEF_CallWindowProcA: {
        uint32_t proc = pe_arg(a, 0);
        WNDPROC native = token_native(u, proc);
        if (native) {
            v = (uint32_t)send_native(a, NULL, native, (HWND)pe_arg(a, 1), pe_arg(a, 2),
                                      pe_arg(a, 3), pe_arg(a, 4));
        } else if (proc) {
            const uint32_t args[4] = { pe_arg(a, 1), pe_arg(a, 2), pe_arg(a, 3), pe_arg(a, 4) };
            v = win_pe_call(ctx, proc, args, 4, NULL, 0, NULL);
        }
        break;
    }
    case PEF_SendMessageA:
        v = (uint32_t)send_native(a, SendMessage, NULL, (HWND)pe_arg(a, 0), pe_arg(a, 1),
                                  pe_arg(a, 2), pe_arg(a, 3));
        break;
    case PEF_SendDlgItemMessageA: {
        HWND item = GetDlgItem((HWND)pe_arg(a, 0), (int)pe_arg(a, 1));
        v = item ? (uint32_t)send_native(a, SendMessage, NULL, item, pe_arg(a, 2), pe_arg(a, 3),
                                         pe_arg(a, 4)) : 0;
        break;
    }
    case PEF_PostMessageA:
    case PEF_PostThreadMessageA: {
        HWND h = service == PEF_PostMessageA ? (HWND)pe_arg(a, 0) : 0;
        UINT m = pe_arg(a, 1);
        uint32_t w = pe_arg(a, 2), l = pe_arg(a, 3);
        to_native(ctl_kind(h), &m, &w, &l);
        v = PostMessage(h, m, (WPARAM)w, (LPARAM)l);
        break;
    }
    case PEF_PostQuitMessage: PostQuitMessage((int)pe_arg(a, 0)); v = 0; break;
    case PEF_GetMessageA:
    case PEF_PeekMessageA: {
        MSG m;
        uint32_t p = pe_arg(a, 0);
        if (!pe_buf(a, p, 28))
            break;
        UINT first = pe_arg(a, 2), last = pe_arg(a, 3);
        v = service == PEF_GetMessageA
            ? (uint32_t)GetMessage(&m, (HWND)pe_arg(a, 1), first, last)
            : (uint32_t)PeekMessage(&m, (HWND)pe_arg(a, 1), first, last, pe_arg(a, 4));
        if (v || service == PEF_GetMessageA)
            msg_out(a, p, &m);
        if (!alive(u) && ctx->stopped) {
            *eax = 0;
            return WINX86_SERVICE_EXIT;
        }
        break;
    }
    case PEF_TranslateMessage: {
        MSG m;
        v = msg_in(a, pe_arg(a, 0), &m) && TranslateMessage(&m);
        break;
    }
    case PEF_DispatchMessageA: {
        MSG m;
        if (msg_in(a, pe_arg(a, 0), &m))
            v = (uint32_t)DispatchMessage(&m);
        break;
    }
    case PEF_IsDialogMessageA: {
        MSG m;
        v = msg_in(a, pe_arg(a, 1), &m) && IsDialogMessage((HWND)pe_arg(a, 0), &m);
        break;
    }
    case PEF_TranslateAcceleratorA: {
        MSG m;
        v = msg_in(a, pe_arg(a, 2), &m) &&
            TranslateAccelerator((HWND)pe_arg(a, 0), (HACCEL)pe_arg(a, 1), &m);
        break;
    }
    case PEF_GetMessagePos: v = GetMessagePos(); break;
    case PEF_WaitMessage: WaitMessage(); v = 1; break;
    case PEF_BeginPaint: {
        HWND h = (HWND)pe_arg(a, 0);
        uint8_t *g = (uint8_t *)pe_buf(a, pe_arg(a, 1), 64);
        int i = 0;
        while (i < PE_PAINTS && u->paint[i].hwnd && u->paint[i].hwnd != h)
            i++;
        if (!g || i == PE_PAINTS)
            break;
        u->paint[i].hwnd = h;
        HDC dc = BeginPaint(h, &u->paint[i].ps);
        memset(g, 0, 64);
        pe_wr32(g, dc);
        pe_wr32(g + 4, (uint32_t)u->paint[i].ps.fErase);
        memcpy(g + 8, &u->paint[i].ps.rcPaint, 16);
        v = dc;
        break;
    }
    case PEF_EndPaint: {
        HWND h = (HWND)pe_arg(a, 0);
        for (int i = 0; i < PE_PAINTS; i++)
            if (u->paint[i].hwnd == h) {
                EndPaint(h, &u->paint[i].ps);
                u->paint[i].hwnd = 0;
                v = 1;
                break;
            }
        break;
    }
    case PEF_GetWindowLongA: v = window_long(a, u, 0); break;
    case PEF_SetWindowLongA: v = window_long(a, u, 1); break;
    case PEF_GetClassLongA:
    case PEF_SetClassLongA: {
        HWND h = (HWND)pe_arg(a, 0);
        int index = (int)pe_arg(a, 1);
        if (index == -24) {                 /* GCL_WNDPROC */
            char name[64];
            GetClassName(h, name, sizeof(name));
            int k = own_class(u, name);
            if (k >= 0) {
                v = u->cls[k].proc;
                if (service == PEF_SetClassLongA)
                    u->cls[k].proc = pe_arg(a, 2);
            } else {
                v = native_token(u, (WNDPROC)(intptr_t)GetClassLong(h, index));
            }
        } else if (index == -16) {          /* GCL_HMODULE */
            v = u->ctx->img->mod[0].base;
        } else {
            v = service == PEF_SetClassLongA ? (uint32_t)SetClassLong(h, index, (LONG)pe_arg(a, 2))
                                             : (uint32_t)GetClassLong(h, index);
            if (index <= -10 && index >= -14 && service == PEF_GetClassLongA)
                v = GetClassWord(h, index); /* the brush, cursor, icon */
        }
        break;
    }
    case PEF_SetTimer: {
        HWND h = (HWND)pe_arg(a, 0);
        UINT id = pe_arg(a, 1);
        uint32_t proc = pe_arg(a, 3);
        if (!proc) {
            v = SetTimer(h, id, pe_arg(a, 2), NULL);
            break;
        }
        int i = 0;
        while (i < PE_TIMERS && u->timer[i].proc &&
               !(u->timer[i].hwnd == h && u->timer[i].id == id))
            i++;
        if (i == PE_TIMERS)
            break;
        v = SetTimer(h, id, pe_arg(a, 2), pe_timerproc);
        if (v) {
            u->timer[i].hwnd = h;
            u->timer[i].id = h ? id : v;
            u->timer[i].proc = proc;
        }
        break;
    }
    case PEF_KillTimer: {
        HWND h = (HWND)pe_arg(a, 0);
        UINT id = pe_arg(a, 1);
        for (int i = 0; i < PE_TIMERS; i++)
            if (u->timer[i].proc && u->timer[i].hwnd == h && u->timer[i].id == id)
                u->timer[i].proc = 0;
        v = KillTimer(h, id);
        break;
    }
    case PEF_LoadIconA:
    case PEF_LoadCursorA:
    case PEF_LoadBitmapA: {
        uint32_t inst = pe_arg(a, 0), n = pe_arg(a, 1);
        const char *name = n < 0x10000u ? (const char *)(uintptr_t)n : pe_str(a, n);
        int kind = service == PEF_LoadIconA ? 2 : service == PEF_LoadCursorA ? 1 : 3;
        if (a->bad)
            break;
        if (!inst)
            v = kind == 2 ? LoadIcon(0, name) : kind == 1 ? LoadCursor(0, name) : 0;
        else
            v = load_image(a, u, kind, res_module(a, inst), name);
        break;
    }
    case PEF_LoadImageA: {
        uint32_t inst = pe_arg(a, 0), n = pe_arg(a, 1), type = pe_arg(a, 2);
        const char *name = n < 0x10000u ? (const char *)(uintptr_t)n : pe_str(a, n);
        if (a->bad || type > 2)
            break;
        int kind = type == 0 ? 3 : type == 1 ? 2 : 1;
        if (pe_arg(a, 5) & 0x10u)           /* LR_LOADFROMFILE */
            v = type == 0 ? 0 : win_image_file(name, type == 2);
        else if (!inst)
            v = kind == 2 ? LoadIcon(0, name) : kind == 1 ? LoadCursor(0, name) : 0;
        else
            v = load_image(a, u, kind, res_module(a, inst), name);
        break;
    }
    case PEF_LoadMenuA: {
        uint32_t n = pe_arg(a, 1);
        const char *name = n < 0x10000u ? (const char *)(uintptr_t)n : pe_str(a, n);
        if (!a->bad)
            v = load_menu(a, res_module(a, pe_arg(a, 0)), name);
        break;
    }
    case PEF_LoadAcceleratorsA: {
        uint32_t n = pe_arg(a, 1);
        const char *name = n < 0x10000u ? (const char *)(uintptr_t)n : pe_str(a, n);
        if (!a->bad)
            v = load_accelerators(a, u, res_module(a, pe_arg(a, 0)), name);
        break;
    }
    case PEF_LoadStringA:
        v = load_string(a, res_module(a, pe_arg(a, 0)), pe_arg(a, 1) & 0xffffu, pe_arg(a, 2),
                        (int32_t)pe_arg(a, 3));
        break;
    case PEF_DialogBoxParamA:
    case PEF_CreateDialogParamA: {
        uint32_t n = pe_arg(a, 1), size;
        const char *name = n < 0x10000u ? (const char *)(uintptr_t)n : pe_str(a, n);
        int m = res_module(a, pe_arg(a, 0));
        const uint8_t *d = a->bad ? NULL : resource(a, m, RT_DIALOG_, name, &size);
        int r = dialog(a, u, m, d, d ? size : 0, (HWND)pe_arg(a, 2), pe_arg(a, 3),
                       pe_arg(a, 4), service == PEF_CreateDialogParamA);
        v = service == PEF_CreateDialogParamA && r < 0 ? 0 : (uint32_t)r;
        break;
    }
    case PEF_DialogBoxIndirectParamA:
    case PEF_CreateDialogIndirectParamA: {
        const uint8_t *d = (const uint8_t *)pe_buf(a, pe_arg(a, 1), 18);
        win_pe_mem *mem = d ? win_pe_mem_of(ctx->img, pe_arg(a, 1)) : NULL;
        uint32_t size = mem ? mem->base + mem->size - pe_arg(a, 1) : 0;
        if (size > 0x10000u)
            size = 0x10000u;
        int r = dialog(a, u, res_module(a, pe_arg(a, 0)), d, size, (HWND)pe_arg(a, 2),
                       pe_arg(a, 3), pe_arg(a, 4), service == PEF_CreateDialogIndirectParamA);
        v = service == PEF_CreateDialogIndirectParamA && r < 0 ? 0 : (uint32_t)r;
        break;
    }
    case PEF_wsprintfA:
    case PEF_wvsprintfA: {
        uint32_t buf = pe_arg(a, 0);
        const char *fmt = pe_str(a, pe_arg(a, 1));
        uint32_t list = service == PEF_wsprintfA ? a->esp + 12u : pe_arg(a, 2);
        char out[1025];
        if (!fmt)
            break;
        v = format(a, out, sizeof(out), fmt, list);
        char *p = (char *)pe_buf(a, buf, v + 1u);
        if (p)
            memcpy(p, out, v + 1u);
        break;
    }
    case PEF_CharUpperA:
    case PEF_CharLowerA: {
        uint32_t p = pe_arg(a, 0);
        int up = service == PEF_CharUpperA;
        if (p < 0x10000u) {                 /* a character */
            char c[2] = { (char)p, 0 };
            if (up) AnsiUpperBuff(c, 1); else AnsiLowerBuff(c, 1);
            v = (uint8_t)c[0];
            break;
        }
        char *s = (char *)pe_str(a, p);
        if (s) {
            if (up) AnsiUpperBuff(s, (UINT)strlen(s)); else AnsiLowerBuff(s, (UINT)strlen(s));
        }
        v = p;
        break;
    }
    case PEF_CharUpperBuffA:
    case PEF_CharLowerBuffA: {
        uint32_t n = pe_arg(a, 1);
        char *s = n ? (char *)pe_buf(a, pe_arg(a, 0), n) : NULL;
        if (s) {
            if (service == PEF_CharUpperBuffA) AnsiUpperBuff(s, n); else AnsiLowerBuff(s, n);
        }
        v = s ? n : 0;
        break;
    }
    case PEF_EnumWindows:
    case PEF_EnumChildWindows: {
        int child = service == PEF_EnumChildWindows;
        enum_ctx e = { u, pe_arg(a, child), pe_arg(a, child + 1) };
        v = child ? EnumChildWindows((HWND)pe_arg(a, 0), pe_enumproc, (LPARAM)(uintptr_t)&e)
                  : EnumWindows(pe_enumproc, (LPARAM)(uintptr_t)&e);
        break;
    }
    case PEF_GetWindowTextA: {
        int32_t max = (int32_t)pe_arg(a, 2);
        char *s = max > 0 ? (char *)pe_buf(a, pe_arg(a, 1), (uint32_t)max) : NULL;
        v = s ? (uint32_t)GetWindowText((HWND)pe_arg(a, 0), s, max) : 0;
        break;
    }
    case PEF_WindowFromPoint: {
        POINT pt = { (int)pe_arg(a, 0), (int)pe_arg(a, 1) };
        v = WindowFromPoint(pt);
        break;
    }
    case PEF_ChildWindowFromPoint: {
        POINT pt = { (int)pe_arg(a, 1), (int)pe_arg(a, 2) };
        v = ChildWindowFromPoint((HWND)pe_arg(a, 0), pt);
        break;
    }
    case PEF_MapWindowPoints: {
        uint32_t n = pe_arg(a, 3);
        POINT *p = n ? (POINT *)pe_buf(a, pe_arg(a, 2), 8u * n) : NULL;
        if (p) {
            POINT o = { 0, 0 };
            MapWindowPoints((HWND)pe_arg(a, 0), (HWND)pe_arg(a, 1), p, n);
            MapWindowPoints((HWND)pe_arg(a, 0), (HWND)pe_arg(a, 1), &o, 1);
            v = LONG2(o.x, o.y);
        }
        break;
    }
    case PEF_GetWindowPlacement:
    case PEF_SetWindowPlacement: {
        WINDOWPLACEMENT *p = (WINDOWPLACEMENT *)pe_buf(a, pe_arg(a, 1), sizeof(WINDOWPLACEMENT));
        if (p)
            v = service == PEF_GetWindowPlacement ? GetWindowPlacement((HWND)pe_arg(a, 0), p)
                                                  : SetWindowPlacement((HWND)pe_arg(a, 0), p);
        break;
    }
    case PEF_SystemParametersInfoA: {
        uint32_t p = pe_arg(a, 2);
        void *buf = p ? pe_buf(a, p, 4) : NULL;
        if (!a->bad)
            v = SystemParametersInfo(pe_arg(a, 0), pe_arg(a, 1), buf ? buf : (void *)(uintptr_t)p,
                                     pe_arg(a, 3));
        break;
    }

    /* GDI */
    case PEF_GetStockObject: v = GetStockObject((int)pe_arg(a, 0)); break;
    case PEF_TextOutA:
    case PEF_ExtTextOutA: {
        int ext = service == PEF_ExtTextOutA;
        int32_t n = (int32_t)pe_arg(a, ext ? 6 : 4);
        const char *s = n > 0 ? (const char *)pe_buf(a, pe_arg(a, ext ? 5 : 3), (uint32_t)n) : "";
        if (!s)
            break;
        if (!ext) {
            v = TextOut((HDC)pe_arg(a, 0), (int)pe_arg(a, 1), (int)pe_arg(a, 2), s, n);
            break;
        }
        const RECT *rc = pe_arg(a, 4) ? (const RECT *)pe_buf(a, pe_arg(a, 4), 16) : NULL;
        const int *dx = pe_arg(a, 7) && n > 0 ? (const int *)pe_buf(a, pe_arg(a, 7), 4u * (uint32_t)n) : NULL;
        if (!a->bad)
            v = ExtTextOut((HDC)pe_arg(a, 0), (int)pe_arg(a, 1), (int)pe_arg(a, 2), pe_arg(a, 3),
                           rc, s, (UINT)(n > 0 ? n : 0), dx);
        break;
    }
    case PEF_GetTextExtentPoint32A:
    case PEF_GetTextExtentPointA: {
        int32_t n = (int32_t)pe_arg(a, 2);
        const char *s = n > 0 ? (const char *)pe_buf(a, pe_arg(a, 1), (uint32_t)n) : "";
        SIZE *sz = (SIZE *)pe_buf(a, pe_arg(a, 3), 8);
        if (s && sz)
            v = GetTextExtentPoint((HDC)pe_arg(a, 0), s, n > 0 ? n : 0, sz);
        break;
    }
    case PEF_CreateFontIndirectA: {
        const LOGFONT *lf = (const LOGFONT *)pe_buf(a, pe_arg(a, 0), sizeof(LOGFONT));
        v = lf ? CreateFontIndirect(lf) : 0;
        break;
    }
    case PEF_CreateDIBSection:
        v = 0;                              /* not yet */
        break;
    default:
        return user_more(a, u, service, eax);
    }
    *eax = v;
    return 1;
}
#endif
