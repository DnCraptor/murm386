/*
 * ne_guest_user.c - a Win16 program's windows: its window classes, whose
 * window procedure is its own code (called through win_ne_callback), its
 * messages, painting and drawing. Win16 structures in the guest (16-bit
 * fields, selector:offset pointers) are converted to and from the native
 * ones; handles are the native 16-bit handles as they are.
 *
 * A message's lParam goes to the window procedure as it is: when it is a
 * native pointer (WM_CREATE, WM_GETMINMAXINFO ...) the program can only
 * hand it on to DefWindowProc, which is what most do with those.
 *
 * The program's resources are read from its file when it asks for them:
 * menus (LoadMenu, a class's menu), accelerators, strings, dialogs (built
 * as native templates; their dialog procedures are its own code too).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "wincore.h"
#include "ne_guest_api.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

#define NE_CLASSES 16
#define NE_PAINTS  4
#define NE_DIALOGS 8
#define NE_ACCELS  4

typedef struct ne_user {
    struct ne_user *next;
    win_ne_guest_api_context *ctx;
    HTASK task;
    struct {
        char name[32];
        uint32_t proc;
        char menu[32];              /* its menu resource: a name, */
        uint16_t menu_id;           /* or a number */
    } cls[NE_CLASSES];
    struct { HWND hwnd; PAINTSTRUCT ps; } paint[NE_PAINTS];
    /* the dialog procedures of the dialogs open (nested, as DialogBox
     * runs), and the one of the dialog being made */
    struct { HWND hwnd; uint32_t proc; } dlg[NE_DIALOGS];
    int dialogs;
    uint32_t pending;
    HACCEL accel[NE_ACCELS];        /* destroyed at the end */
} ne_user;

static ne_user *programs;

void win_ne_user_start(win_ne_guest_api_context *ctx)
{
    ne_user *u = (ne_user *)win_alloc(sizeof(ne_user));     /* zeroed */
    if (!u)
        return;
    u->ctx = ctx;
    u->task = GetCurrentTask();
    u->next = programs;
    programs = u;
    ctx->user = u;
}

void win_ne_user_end(win_ne_guest_api_context *ctx)
{
    for (ne_user **p = &programs; *p; p = &(*p)->next)
        if (*p == ctx->user) {
            ne_user *u = *p;
            *p = u->next;
            for (int i = 0; i < NE_ACCELS; i++)
                if (u->accel[i])
                    DestroyAcceleratorTableN(u->accel[i]);
            win_free(u);
            break;
        }
    ctx->user = NULL;
}

/* The native window procedure of every class a program registers: its own
 * procedure, found by the window's task and class name. */
static LRESULT CALLBACK ne_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    HTASK task = GetWindowTask(h);
    char name[32];
    GetClassName(h, name, sizeof(name));
    for (ne_user *u = programs; u; u = u->next) {
        if (u->task != task)
            continue;
        win_ne_guest_api_context *c = u->ctx;
        if (c->dead || c->exited || c->stopped)
            break;
        for (int i = 0; i < NE_CLASSES; i++)
            if (u->cls[i].proc && !lstrcmpi(u->cls[i].name, name))
                return (LRESULT)(int32_t)win_ne_callback(c, u->cls[i].proc, (uint16_t)h,
                                                         (uint16_t)msg, (uint16_t)wp,
                                                         (uint32_t)lp);
        break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* The native dialog procedure of the program's dialogs: its own, bound to
 * the dialog at its first message (WM_SETFONT, WM_INITDIALOG ...). */
static BOOL CALLBACK ne_dlgproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    HTASK task = GetWindowTask(h);
    for (ne_user *u = programs; u; u = u->next) {
        if (u->task != task)
            continue;
        win_ne_guest_api_context *c = u->ctx;
        if (c->dead || c->exited || c->stopped)
            return FALSE;
        uint32_t proc = 0;
        for (int i = 0; i < u->dialogs; i++)
            if (u->dlg[i].hwnd == h)
                proc = u->dlg[i].proc;
        if (!proc && u->pending && u->dialogs < NE_DIALOGS) {
            proc = u->pending;
            u->dlg[u->dialogs].hwnd = h;
            u->dlg[u->dialogs++].proc = proc;
            u->pending = 0;
        }
        if (!proc)
            return FALSE;
        return (uint16_t)win_ne_callback(c, proc, (uint16_t)h, (uint16_t)msg, (uint16_t)wp,
                                         (uint32_t)lp) != 0;
    }
    return FALSE;
}

/* ---------------------------------------------------------------- helpers */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }

typedef struct {
    win_ne_guest_api_context *ctx;
    win_ne_regs *cpu;
    int bad;
} args;

/* the argument word at SP + off (the first is at 4) */
static uint16_t w(args *a, uint32_t off)
{
    uint16_t v = 0;
    if (!win_ne_arg(a->ctx, a->cpu, off, &v))
        a->bad = 1;
    return v;
}

static uint32_t dw(args *a, uint32_t off) { return w(a, off) | (uint32_t)w(a, off + 2) << 16; }

/* a Win16 coordinate or size; CW_USEDEFAULT is 8000h there */
static int coord(uint16_t v) { return v == 0x8000u ? CW_USEDEFAULT : (int)(int16_t)v; }

/* a name, or an atom (selector 0): a native LPCSTR */
static LPCSTR name_or_atom(args *a, uint32_t far, char *buf, uint32_t cap)
{
    if (!(far >> 16))
        return MAKEINTRESOURCE((WORD)far);
    if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, buf, cap)) {
        a->bad = 1;
        buf[0] = 0;
    }
    return buf;
}

/* the program's hInstance (its DGROUP selector, from InitTask): its task */
static HINSTANCE instance(ne_user *u, uint16_t h) { return h ? (HINSTANCE)u->task : 0; }

/* Win16 MSG: hwnd, message, wParam (words), lParam, time (dwords), pt */
static void msg_to_guest(const MSG *m, uint8_t g[18])
{
    wr16(g, (uint16_t)m->hwnd);
    wr16(g + 2, (uint16_t)m->message);
    wr16(g + 4, (uint16_t)m->wParam);
    wr32(g + 6, (uint32_t)m->lParam);
    wr32(g + 10, m->time);
    wr16(g + 14, (uint16_t)m->pt.x);
    wr16(g + 16, (uint16_t)m->pt.y);
}

static int msg_from_guest(args *a, uint32_t far, MSG *m)
{
    uint8_t g[18];
    if (!win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, g, sizeof(g)))
        return a->bad = 1, 0;
    memset(m, 0, sizeof(*m));
    m->hwnd = rd16(g);
    m->message = rd16(g + 2);
    m->wParam = rd16(g + 4);
    m->lParam = (LPARAM)rd32(g + 6);
    m->time = rd32(g + 10);
    m->pt.x = (int16_t)rd16(g + 14);
    m->pt.y = (int16_t)rd16(g + 16);
    return 1;
}

/* ---------------------------------------------------------------- wsprintf */

/* USER.420 wsprintf(LPSTR, LPCSTR, ...): C arguments, words for %d %u %x
 * %c, dwords for %l., far pointers for %s */
static __attribute__((noinline)) int ne_wsprintf(args *a)
{
    char out[1025];
    const int cap = (int)sizeof(out);
    uint32_t dst = dw(a, 4), fmtp = dw(a, 8);
    char fmt[256];
    if (a->bad || !win_ne_string(a->ctx, (uint16_t)(fmtp >> 16), (uint16_t)fmtp, fmt, sizeof(fmt)))
        return a->bad = 1, 0;
    uint32_t next = 12;
    int n = 0;
    for (const char *f = fmt; *f && n < cap - 1; f++) {
        if (*f != '%') {
            out[n++] = *f;
            continue;
        }
        f++;
        int left = 0, zero = 0, width = 0, lng = 0;
        for (; *f == '-' || *f == '0' || *f == '#'; f++)
            if (*f == '-') left = 1; else if (*f == '0') zero = 1;
        while (*f >= '0' && *f <= '9')
            width = width * 10 + (*f++ - '0');
        if (*f == 'l' || *f == 'L') {
            lng = 1;
            f++;
        }
        char tmp[64];
        const char *s = tmp;
        int len = 0;
        switch (*f) {
        case 'd': case 'i': case 'u': case 'x': case 'X': {
            uint32_t v = lng ? dw(a, next) : w(a, next);
            next += lng ? 4 : 2;
            int neg = 0;
            if ((*f == 'd' || *f == 'i') && (lng ? (int32_t)v < 0 : (int16_t)v < 0)) {
                neg = 1;
                v = lng ? (uint32_t)-(int32_t)v : (uint32_t)-(int16_t)v;
            }
            unsigned base = (*f == 'x' || *f == 'X') ? 16 : 10;
            const char *digits = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            char rev[16];
            int k = 0;
            do {
                rev[k++] = digits[v % base];
                v /= base;
            } while (v);
            if (neg)
                tmp[len++] = '-';
            while (k)
                tmp[len++] = rev[--k];
            break;
        }
        case 'c':
            tmp[len++] = (char)w(a, next);
            next += 2;
            break;
        case 's': {
            uint32_t p = dw(a, next);
            next += 4;
            if (!win_ne_string(a->ctx, (uint16_t)(p >> 16), (uint16_t)p, tmp, sizeof(tmp)))
                tmp[0] = 0;
            len = (int)strlen(tmp);
            break;
        }
        case 0:
            f--;
            continue;
        default:
            tmp[len++] = *f;
            break;
        }
        int pad = width > len ? width - len : 0;
        if (!left)
            for (; pad > 0 && n < cap - 1; pad--)
                out[n++] = zero && *f != 's' ? '0' : ' ';
        for (int i = 0; i < len && n < cap - 1; i++)
            out[n++] = s[i];
        for (; pad > 0 && n < cap - 1; pad--)
            out[n++] = ' ';
    }
    out[n] = 0;
    if (a->bad || !win_ne_write(a->ctx, (uint16_t)(dst >> 16), (uint16_t)dst, out, (uint32_t)n + 1))
        return a->bad = 1, 0;
    return n;
}

/* GDI.33 TextOut(HDC, int, int, LPCSTR, int) */
static __attribute__((noinline)) uint32_t ne_textout(args *a)
{
    int n = (int16_t)w(a, 4);
    uint32_t p = dw(a, 6);
    int y = (int16_t)w(a, 10), x = (int16_t)w(a, 12);
    HDC dc = w(a, 14);
    char text[512];
    if (n < 0 || n > (int)sizeof(text))
        n = n < 0 ? 0 : (int)sizeof(text);
    if (a->bad || !win_ne_read(a->ctx, (uint16_t)(p >> 16), (uint16_t)p, text, (uint32_t)n)) {
        a->bad = 1;
        return 0;
    }
    return (uint16_t)TextOut(dc, x, y, text, n);
}

static HMENU load_menu(args *a, uint32_t far);
static HMENU load_menu_named(args *a, const char *name);

/* USER.41 CreateWindow(class, title, style, x, y, cx, cy, parent, menu,
 * instance, param) */
static __attribute__((noinline)) uint32_t ne_createwindow(args *a, ne_user *u)
{
    char cls[64], title[128];
    LPCSTR c = name_or_atom(a, dw(a, 30), cls, sizeof(cls));
    uint32_t t = dw(a, 26);
    title[0] = 0;
    if (t && !win_ne_string(a->ctx, (uint16_t)(t >> 16), (uint16_t)t, title, sizeof(title)))
        a->bad = 1;
    DWORD style = dw(a, 22);
    int x = coord(w(a, 20)), y = coord(w(a, 18)), cx = coord(w(a, 16)), cy = coord(w(a, 14));
    HWND parent = w(a, 12);
    HMENU menu = w(a, 10);
    HINSTANCE inst = instance(u, w(a, 8));
    uint32_t param = dw(a, 4);
    if (a->bad)
        return 0;
    /* a top-level window without a menu of its own gets its class's */
    for (int i = 0; !menu && !(style & WS_CHILD) && c == cls && i < NE_CLASSES; i++)
        if (u->cls[i].proc && !lstrcmpi(u->cls[i].name, cls)) {
            if (u->cls[i].menu_id)
                menu = load_menu(a, u->cls[i].menu_id);
            else if (u->cls[i].menu[0])
                menu = load_menu_named(a, u->cls[i].menu);
        }
    return CreateWindow(c, title, style, x, y, cx, cy, parent, menu, inst, (LPVOID)(uintptr_t)param);
}

/* ---------------------------------------------------------------- resources */

enum { RT_MENU_ = 4, RT_DIALOG_ = 5, RT_STRING_ = 6, RT_ACCELERATOR_ = 9 };

/* a resource named by a LPCSTR of the program: a number (selector 0) or a
 * name; malloc'd bytes */
static uint8_t *resource(args *a, uint16_t type, uint32_t far, uint32_t *n)
{
    char name[64];
    uint8_t *d = NULL;
    if (far >> 16) {
        if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, name, sizeof(name)))
            return a->bad = 1, NULL;
        win_ne_resource(a->ctx->path, type, 0, name, &d, n);
    } else {
        win_ne_resource(a->ctx->path, type, (uint16_t)far, NULL, &d, n);
    }
    return d;
}

/* a NUL-terminated string at *p within [*p, end): it, and *p past it */
static const char *res_string(const uint8_t **p, const uint8_t *end)
{
    const uint8_t *z = *p < end ? memchr(*p, 0, (size_t)(end - *p)) : NULL;
    if (!z)
        return NULL;
    const char *s = (const char *)*p;
    *p = z + 1;
    return s;
}

/* Win16 menu items (after the 4-byte header): flags (MF_POPUP: a popup
 * whose items follow; MF_END 80h: the last of its level), the id (not for
 * a popup), the text; an empty item without flags is a separator */
static const uint8_t *menu_items(HMENU m, const uint8_t *p, const uint8_t *end)
{
    for (;;) {
        if (p + 2 > end)
            return NULL;
        UINT flags = rd16(p);
        p += 2;
        UINT id = 0;
        if (!(flags & MF_POPUP)) {
            if (p + 2 > end)
                return NULL;
            id = rd16(p);
            p += 2;
        }
        const char *text = res_string(&p, end);
        if (!text)
            return NULL;
        UINT f = flags & ~0x80u;
        if (flags & MF_POPUP) {
            HMENU sub = CreatePopupMenu();
            p = menu_items(sub, p, end);
            if (!p)
                return NULL;
            AppendMenu(m, f, (UINT)sub, text);
        } else if (!f && !id && !*text) {
            AppendMenu(m, MF_SEPARATOR, 0, NULL);
        } else {
            AppendMenu(m, f, id, text);
        }
        if (flags & 0x80u)
            return p;
    }
}

static HMENU load_menu(args *a, uint32_t far)
{
    uint32_t n;
    uint8_t *d = resource(a, RT_MENU_, far, &n);
    if (!d)
        return 0;
    HMENU m = CreateMenu();
    if (n < 4 || !menu_items(m, d + 4, d + n)) {
        DestroyMenu(m);
        m = 0;
    }
    free(d);
    return m;
}

/* a menu by a name of WINN's own (a class's) */
static HMENU load_menu_named(args *a, const char *name)
{
    uint32_t n;
    uint8_t *d = NULL;
    win_ne_resource(a->ctx->path, RT_MENU_, 0, name, &d, &n);
    if (!d)
        return 0;
    HMENU m = CreateMenu();
    if (n < 4 || !menu_items(m, d + 4, d + n)) {
        DestroyMenu(m);
        m = 0;
    }
    free(d);
    return m;
}

/* Win16 accelerators: 5 bytes each, flags (80h: the last), key, command */
static HACCEL load_accelerators(args *a, ne_user *u, uint32_t far)
{
    uint32_t n;
    uint8_t *d = resource(a, RT_ACCELERATOR_, far, &n);
    if (!d)
        return 0;
    ACCEL_N acc[64];
    int k = 0;
    for (uint32_t i = 0; i + 5 <= n && k < 64; i += 5) {
        acc[k].fVirt = (BYTE)(d[i] & 0x1f);
        acc[k].key = rd16(d + i + 1);
        acc[k++].cmd = rd16(d + i + 3);
        if (d[i] & 0x80)
            break;
    }
    free(d);
    HACCEL h = k ? CreateAcceleratorTableN(acc, k) : 0;
    for (int i = 0; h && i < NE_ACCELS; i++)
        if (!u->accel[i]) {
            u->accel[i] = h;
            break;
        }
    return h;
}

/* USER.176 LoadString(HINSTANCE, UINT, LPSTR, int): string id is the
 * (id % 16)th of block id / 16 + 1, each a length byte and the text */
static uint16_t load_string(args *a)
{
    int max = (int16_t)w(a, 4);
    uint32_t buf = dw(a, 6);
    uint16_t id = w(a, 10);
    if (a->bad || max <= 0)
        return 0;
    uint32_t n;
    uint8_t *d = resource(a, RT_STRING_, (uint16_t)(id / 16 + 1), &n);
    uint32_t at = 0;
    for (unsigned i = 0; d && i < id % 16u && at < n; i++)
        at += 1u + d[at];
    char s[256];
    int len = 0;
    if (d && at < n) {
        len = d[at];
        if (at + 1u + (uint32_t)len > n)
            len = (int)(n - at - 1u);
        if (len > max - 1)
            len = max - 1;
        memcpy(s, d + at + 1, (size_t)len);
    }
    free(d);
    s[len] = 0;
    if (!win_ne_write(a->ctx, (uint16_t)(buf >> 16), (uint16_t)buf, s, (uint32_t)len + 1))
        a->bad = 1;
    return (uint16_t)len;
}

/* USER.87 DialogBox(HINSTANCE, LPCSTR, HWND, DLGPROC): the Win16 template
 * (style, count, x, y, cx, cy, menu, class, caption, [point size, face];
 * then each item: x, y, cx, cy, id, style, class (80h.. the built-in
 * ones), text, extra bytes) made a native one */
static __attribute__((noinline)) int dialog_box(args *a, ne_user *u)
{
    static const char *const builtin[] = { "BUTTON", "EDIT", "STATIC", "LISTBOX",
                                           "SCROLLBAR", "COMBOBOX" };
    uint32_t proc = dw(a, 4);
    HWND owner = w(a, 8);
    uint32_t name = dw(a, 10);
    if (a->bad)
        return -1;
    uint32_t n;
    uint8_t *d = resource(a, RT_DIALOG_, name, &n);
    if (!d)
        return -1;
    const uint8_t *p = d, *end = d + n;
    DLGTEMPLATE_N t;
    DLGITEM_N items[64];
    memset(&t, 0, sizeof(t));
    int r = -1, k = 0;
    if (n < 13)
        goto done;
    t.style = rd32(p);
    t.cdit = p[4];
    t.x = (int16_t)rd16(p + 5);
    t.y = (int16_t)rd16(p + 7);
    t.cx = (int16_t)rd16(p + 9);
    t.cy = (int16_t)rd16(p + 11);
    p += 13;
    if (p < end && *p == 0xff)                  /* a menu by number: none here */
        p += 3;
    else if (!res_string(&p, end))
        goto done;
    const char *cls = res_string(&p, end), *caption = res_string(&p, end);
    if (!cls || !caption)
        goto done;
    t.klass = *cls ? cls : NULL;
    t.caption = caption;
    if (t.style & DS_SETFONT) {
        if (p + 2 > end)
            goto done;
        t.pointsize = rd16(p);
        p += 2;
        t.font = res_string(&p, end);
        if (!t.font)
            goto done;
    }
    for (; k < t.cdit && k < 64; k++) {
        DLGITEM_N *it = &items[k];
        if (p + 15 > end)
            goto done;
        it->x = (int16_t)rd16(p);
        it->y = (int16_t)rd16(p + 2);
        it->cx = (int16_t)rd16(p + 4);
        it->cy = (int16_t)rd16(p + 6);
        it->id = (int16_t)rd16(p + 8);
        it->style = rd32(p + 10);
        p += 14;
        if (*p >= 0x80) {
            it->klass = *p - 0x80u < 6 ? builtin[*p - 0x80u] : "STATIC";
            p++;
        } else if (!(it->klass = res_string(&p, end))) {
            goto done;
        }
        if (p < end && *p == 0xff) {            /* an icon by number: not yet */
            it->text = "";
            p += 3;
        } else if (!(it->text = res_string(&p, end))) {
            goto done;
        }
        if (p >= end)
            goto done;
        p += 1 + *p;                            /* extra bytes */
    }
    t.cdit = (BYTE)k;
    {
        uint32_t outer = u->pending;
        int open = u->dialogs;
        u->pending = proc;
        r = DialogBoxIndirectN(instance(u, 1), &t, items, owner, ne_dlgproc);
        u->pending = outer;
        u->dialogs = open;                      /* it is closed */
    }
done:
    free(d);
    return r;
}

/* ---------------------------------------------------------------- services */

int win_ne_guest_user(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint16_t service,
                      uint32_t *value, int *dword)
{
    ne_user *u = ctx ? (ne_user *)ctx->user : NULL;
    if (!u)
        return 0;
    args a = { ctx, cpu, 0 };
    uint32_t v = 0;
    switch (service) {
    case 0x1006:                        /* PostQuitMessage(int) */
        PostQuitMessage((int16_t)w(&a, 4));
        break;
    case 0x1027: {                      /* BeginPaint(HWND, LPPAINTSTRUCT) */
        uint32_t ps = dw(&a, 4);
        HWND h = w(&a, 8);
        int i = 0;
        while (i < NE_PAINTS && u->paint[i].hwnd)
            i++;
        if (a.bad || i == NE_PAINTS)
            break;
        PAINTSTRUCT *p = &u->paint[i].ps;
        HDC dc = BeginPaint(h, p);
        u->paint[i].hwnd = h;
        uint8_t g[32];
        memset(g, 0, sizeof(g));
        wr16(g, (uint16_t)p->hdc);
        wr16(g + 2, (uint16_t)p->fErase);
        wr16(g + 4, (uint16_t)p->rcPaint.left);
        wr16(g + 6, (uint16_t)p->rcPaint.top);
        wr16(g + 8, (uint16_t)p->rcPaint.right);
        wr16(g + 10, (uint16_t)p->rcPaint.bottom);
        if (!win_ne_write(ctx, (uint16_t)(ps >> 16), (uint16_t)ps, g, sizeof(g)))
            a.bad = 1;
        v = dc;
        break;
    }
    case 0x1028: {                      /* EndPaint(HWND, const PAINTSTRUCT FAR *) */
        HWND h = w(&a, 8);
        for (int i = 0; i < NE_PAINTS; i++)
            if (u->paint[i].hwnd == h && h) {
                EndPaint(h, &u->paint[i].ps);
                u->paint[i].hwnd = 0;
                break;
            }
        break;
    }
    case 0x1029:                        /* CreateWindow */
        v = ne_createwindow(&a, u);
        break;
    case 0x102a:                        /* ShowWindow(HWND, int) */
        v = (uint16_t)ShowWindow(w(&a, 6), (int16_t)w(&a, 4));
        break;
    case 0x1035:                        /* DestroyWindow(HWND) */
        v = (uint16_t)DestroyWindow(w(&a, 4));
        break;
    case 0x1039: {                      /* RegisterClass(const WNDCLASS FAR *) */
        uint32_t p = dw(&a, 4);
        uint8_t g[26];
        if (a.bad || !win_ne_read(ctx, (uint16_t)(p >> 16), (uint16_t)p, g, sizeof(g))) {
            a.bad = 1;
            break;
        }
        int i = 0;
        while (i < NE_CLASSES && u->cls[i].proc)
            i++;
        uint32_t name = rd32(g + 22);
        if (i == NE_CLASSES || !(name >> 16) ||
            !win_ne_string(ctx, (uint16_t)(name >> 16), (uint16_t)name, u->cls[i].name,
                           sizeof(u->cls[i].name)))
            break;                      /* an atom for a name: not yet */
        WNDCLASS wc;
        memset(&wc, 0, sizeof(wc));
        wc.style = rd16(g);
        wc.lpfnWndProc = ne_wndproc;
        wc.cbClsExtra = (int16_t)rd16(g + 6);
        wc.cbWndExtra = (int16_t)rd16(g + 8);
        wc.hInstance = instance(u, rd16(g + 10));
        wc.hIcon = rd16(g + 12);
        wc.hCursor = rd16(g + 14);
        wc.hbrBackground = rd16(g + 16);
        wc.lpszClassName = u->cls[i].name;
        v = RegisterClass(&wc);
        if (v) {
            u->cls[i].proc = rd32(g + 2);
            uint32_t menu = rd32(g + 18);       /* the class's menu, by name or number */
            u->cls[i].menu[0] = 0;
            u->cls[i].menu_id = (uint16_t)(menu >> 16 ? 0 : menu);
            if ((menu >> 16) && !win_ne_string(ctx, (uint16_t)(menu >> 16), (uint16_t)menu,
                                               u->cls[i].menu, sizeof(u->cls[i].menu)))
                u->cls[i].menu[0] = 0;
        }
        break;
    }
    case 51:                            /* MakeProcInstance(FARPROC, HINSTANCE): itself */
        v = dw(&a, 6);                  /* (a callback gets DS anyway) */
        *dword = 1;
        break;
    case 52:                            /* FreeProcInstance(FARPROC) */
        break;
    case 0x1057:                        /* DialogBox(HINSTANCE, LPCSTR, HWND, DLGPROC) */
        v = (uint16_t)dialog_box(&a, u);
        break;
    case 0x1058:                        /* EndDialog(HWND, int) */
        v = (uint16_t)EndDialog(w(&a, 6), (int16_t)w(&a, 4));
        break;
    case 0x105b:                        /* GetDlgItem(HWND, int) */
        v = GetDlgItem(w(&a, 6), (int16_t)w(&a, 4));
        break;
    case 0x105c: {                      /* SetDlgItemText(HWND, int, LPCSTR) */
        uint32_t p = dw(&a, 4);
        char text[256];
        if (!a.bad && win_ne_string(ctx, (uint16_t)(p >> 16), (uint16_t)p, text, sizeof(text)))
            SetDlgItemText(w(&a, 10), (int16_t)w(&a, 8), text);
        else
            a.bad = 1;
        break;
    }
    case 0x105d: {                      /* GetDlgItemText(HWND, int, LPSTR, int) */
        int max = (int16_t)w(&a, 4);
        uint32_t p = dw(&a, 6);
        char text[256];
        if (a.bad || max <= 0)
            break;
        if (max > (int)sizeof(text))
            max = (int)sizeof(text);
        v = (uint16_t)GetDlgItemText(w(&a, 12), (int16_t)w(&a, 10), text, max);
        if (!win_ne_write(ctx, (uint16_t)(p >> 16), (uint16_t)p, text, v + 1u))
            a.bad = 1;
        break;
    }
    case 0x105e:                        /* SetDlgItemInt(HWND, int, UINT, BOOL) */
        SetDlgItemInt(w(&a, 10), (int16_t)w(&a, 8),
                      w(&a, 4) ? (UINT)(int16_t)w(&a, 6) : w(&a, 6), w(&a, 4));
        break;
    case 0x105f: {                      /* GetDlgItemInt(HWND, int, BOOL FAR *, BOOL) */
        BOOL sign = w(&a, 4), ok = FALSE;
        uint32_t p = dw(&a, 6);
        v = (uint16_t)GetDlgItemInt(w(&a, 12), (int16_t)w(&a, 10), &ok, sign);
        uint8_t g[2] = { (uint8_t)ok, 0 };
        if (p && !win_ne_write(ctx, (uint16_t)(p >> 16), (uint16_t)p, g, 2))
            a.bad = 1;
        break;
    }
    case 0x1096:                        /* LoadMenu(HINSTANCE, LPCSTR) */
        v = load_menu(&a, dw(&a, 4));
        break;
    case 0x10b0:                        /* LoadString(HINSTANCE, UINT, LPSTR, int) */
        v = load_string(&a);
        break;
    case 0x10b1:                        /* LoadAccelerators(HINSTANCE, LPCSTR) */
        v = load_accelerators(&a, u, dw(&a, 4));
        break;
    case 0x10b2: {                      /* TranslateAccelerator(HWND, HACCEL, LPMSG) */
        MSG m;
        if (msg_from_guest(&a, dw(&a, 4), &m))
            v = (uint16_t)TranslateAccelerator(w(&a, 10), w(&a, 8), &m);
        break;
    }
    case 0x106b:                        /* DefWindowProc(HWND, UINT, WPARAM, LPARAM) */
        v = (uint32_t)DefWindowProc(w(&a, 12), w(&a, 10), w(&a, 8), (LPARAM)dw(&a, 4));
        *dword = 1;
        break;
    case 0x106c: {                      /* GetMessage(LPMSG, HWND, UINT, UINT) */
        uint32_t p = dw(&a, 10);
        HWND h = w(&a, 8);
        UINT lo = w(&a, 6), hi = w(&a, 4);
        if (a.bad)
            break;
        MSG m;
        v = (uint16_t)GetMessage(&m, h, lo, hi);
        uint8_t g[18];
        msg_to_guest(&m, g);
        if (!win_ne_write(ctx, (uint16_t)(p >> 16), (uint16_t)p, g, sizeof(g)))
            a.bad = 1;
        break;
    }
    case 0x1071: {                      /* TranslateMessage(const MSG FAR *) */
        MSG m;
        if (msg_from_guest(&a, dw(&a, 4), &m))
            v = (uint16_t)TranslateMessage(&m);
        break;
    }
    case 0x1072: {                      /* DispatchMessage(const MSG FAR *) */
        MSG m;
        if (msg_from_guest(&a, dw(&a, 4), &m))
            v = (uint32_t)DispatchMessage(&m);
        *dword = 1;
        break;
    }
    case 0x107c:                        /* UpdateWindow(HWND) */
        UpdateWindow(w(&a, 4));
        break;
    case 0x107d: {                      /* InvalidateRect(HWND, const RECT FAR *, BOOL) */
        BOOL erase = w(&a, 4);
        uint32_t p = dw(&a, 6);
        HWND h = w(&a, 10);
        RECT r, *pr = NULL;
        uint8_t g[8];
        if (p) {
            if (!win_ne_read(ctx, (uint16_t)(p >> 16), (uint16_t)p, g, sizeof(g))) {
                a.bad = 1;
                break;
            }
            SetRect(&r, (int16_t)rd16(g), (int16_t)rd16(g + 2), (int16_t)rd16(g + 4),
                    (int16_t)rd16(g + 6));
            pr = &r;
        }
        if (!a.bad)
            InvalidateRect(h, pr, erase);
        break;
    }
    case 0x10ad:                        /* LoadCursor(HINSTANCE, LPCSTR) */
    case 0x10ae: {                      /* LoadIcon(HINSTANCE, LPCSTR) */
        uint32_t name = dw(&a, 4);
        uint16_t inst = w(&a, 8);
        /* the system's (resources of its own: not yet) */
        if (!a.bad && !inst && !(name >> 16))
            v = service == 0x10ad ? LoadCursor(0, MAKEINTRESOURCE((WORD)name))
                                  : LoadIcon(0, MAKEINTRESOURCE((WORD)name));
        break;
    }
    case 0x11a4:                        /* wsprintf(LPSTR, LPCSTR, ...) */
        v = (uint16_t)ne_wsprintf(&a);
        break;
    case 0x3021:                        /* TextOut(HDC, int, int, LPCSTR, int) */
        v = ne_textout(&a);
        break;
    case 0x3057:                        /* GetStockObject(int) */
        v = GetStockObject((int16_t)w(&a, 4));
        break;
    default:
        return 0;
    }
    if (a.bad)
        ctx->invalid_args = 1;
    *value = v;
    return 1;
}

#endif
