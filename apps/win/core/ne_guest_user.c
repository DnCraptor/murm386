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
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

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
    ne_environment(ctx, u);
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
            for (int i = 0; i < NE_IMAGES; i++)
                if (u->image[i].h)
                    DestroyIcon(u->image[i].h);
            ne_call_end(u);
            ne_sys_end(u);
            win_free(u);
            break;
        }
    ctx->user = NULL;
}

ne_user *ne_user_of(HTASK task)
{
    for (ne_user *u = programs; u; u = u->next)
        if (u->task == task)
            return u;
    return NULL;
}

/* a window of one of the program's classes */
int ne_own_window(ne_user *u, HWND h)
{
    char name[32];
    if (GetWindowTask(h) != u->task || !GetClassName(h, name, sizeof(name)))
        return 0;
    for (int i = 0; i < NE_CLASSES; i++)
        if (u->cls[i].proc && !lstrcmpi(u->cls[i].name, name))
            return 1;
    return 0;
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
                return ne_message(u, u->cls[i].proc, h, msg, wp, lp);
        break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* The native dialog procedure of the program's dialogs: its own, bound to
 * the dialog at its first message (WM_SETFONT, WM_INITDIALOG ...) until it
 * is destroyed. */
static BOOL CALLBACK ne_dlgproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    HTASK task = GetWindowTask(h);
    for (ne_user *u = programs; u; u = u->next) {
        if (u->task != task)
            continue;
        win_ne_guest_api_context *c = u->ctx;
        if (c->dead || c->exited || c->stopped)
            return FALSE;
        int slot = -1;
        for (int i = 0; i < NE_DIALOGS; i++)
            if (u->dlg[i].hwnd == h)
                slot = i;
        for (int i = 0; slot < 0 && u->pending && i < NE_DIALOGS; i++)
            if (!u->dlg[i].hwnd) {
                slot = i;
                u->dlg[i].hwnd = h;
                u->dlg[i].proc = u->pending;
                u->dlg[i].param = u->pending_param;
                u->pending = 0;
            }
        if (slot < 0)
            return FALSE;
        if (msg == WM_INITDIALOG)       /* DialogBoxParam's ... */
            lp = (LPARAM)u->dlg[slot].param;
        BOOL r = (uint16_t)ne_message(u, u->dlg[slot].proc, h, msg, wp, lp) != 0;
        if (msg == WM_NCDESTROY)
            u->dlg[slot].hwnd = 0;
        return r;
    }
    return FALSE;
}

/* ---------------------------------------------------------------- helpers */

/* ---------------------------------------------------------------- wsprintf */

/* USER.420 wsprintf(LPSTR, LPCSTR, ...): C arguments, words for %d %u %x
 * %c, dwords for %l., far pointers for %s */
/* an argument of wsprintf (on the stack, from 12 on) or wvsprintf (at
 * list, from 0 on) */
static uint16_t fw(args *a, uint32_t list, uint32_t off)
{
    uint8_t b[2] = { 0, 0 };
    if (!list)
        return w(a, off);
    if (!win_ne_read(a->ctx, (uint16_t)(list >> 16), (uint16_t)list + off - 12u, b, 2))
        a->bad = 1;
    return rd16(b);
}

static uint32_t fdw(args *a, uint32_t list, uint32_t off)
{
    return fw(a, list, off) | (uint32_t)fw(a, list, off + 2) << 16;
}

__attribute__((noinline)) int ne_format(args *a, uint32_t dst, uint32_t fmtp, uint32_t list)
{
    char out[1025];
    const int cap = (int)sizeof(out);
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
            uint32_t v = lng ? fdw(a, list, next) : fw(a, list, next);
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
            tmp[len++] = (char)fw(a, list, next);
            next += 2;
            break;
        case 's': {
            uint32_t p = fdw(a, list, next);
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
/* CreateWindow(class, title, style, x, y, cx, cy, parent, menu, inst,
 * param); CreateWindowEx's the extended style before them */
static __attribute__((noinline)) uint32_t ne_createwindow(args *a, ne_user *u, int ex)
{
    DWORD exstyle = ex ? dw(a, 34) : 0;
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
    return CreateWindowEx(exstyle, c, title, style, x, y, cx, cy, parent, menu, inst,
                          (LPVOID)(uintptr_t)param);
}

/* ---------------------------------------------------------------- resources */

enum { RT_MENU_ = 4, RT_DIALOG_ = 5, RT_STRING_ = 6, RT_ACCELERATOR_ = 9 };

/* a resource named by a LPCSTR of the program: a number (selector 0) or a
 * name; malloc'd bytes */
/* the file the module of a->inst (the program, or one of its DLLs) has its
 * resources in */
static const char *res_path(args *a)
{
    return win_ne_guest_path_of(a->ctx->image, a->inst, a->ctx->path);
}

static uint8_t *resource(args *a, uint16_t type, uint32_t far, uint32_t *n)
{
    char name[64];
    uint8_t *d = NULL;
    if (far >> 16) {
        if (!win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, name, sizeof(name)))
            return a->bad = 1, NULL;
        win_ne_resource(res_path(a), type, 0, name, &d, n);
    } else {
        win_ne_resource(res_path(a), type, (uint16_t)far, NULL, &d, n);
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
    win_ne_resource(res_path(a), RT_MENU_, 0, name, &d, &n);
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
static __attribute__((noinline)) HACCEL load_accelerators(args *a, ne_user *u, uint32_t far)
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
static __attribute__((noinline)) uint16_t load_string(args *a)
{
    int max = (int16_t)w(a, 4);
    uint32_t buf = dw(a, 6);
    uint16_t id = w(a, 10);
    a->inst = w(a, 12);
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

/* a dialog template in the program's memory: at a far pointer (at most
 * 4K of it, to its segment's end), or a global block's (HGLOBAL) */
static uint8_t *template_at(args *a, uint32_t far, int global, uint32_t *n)
{
    win_ne_guest_image *g = (win_ne_guest_image *)a->ctx->image;
    uint32_t base, limit;
    int code;
    if (global)
        far = far << 16;                /* the block's selector, offset 0 */
    if (!win_ne_guest_selector_get(g, (uint16_t)(far >> 16), &base, &limit, &code) ||
        (uint16_t)far > limit)
        return NULL;
    *n = limit - (uint16_t)far + 1u;
    if (*n > 4096u)
        *n = 4096u;
    uint8_t *d = (uint8_t *)malloc(*n);
    if (d && !win_ne_read(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, d, *n)) {
        free(d);
        d = NULL;
    }
    return d;
}

/* USER.87 DialogBox(HINSTANCE, LPCSTR, HWND, DLGPROC) and the rest: the
 * Win16 template (style, count, x, y, cx, cy, menu, class, caption, [point
 * size, face]; then each item: x, y, cx, cy, id, style, class (80h.. the
 * built-in ones), text, extra bytes) made a native one. Its arguments from
 * at (the dialog procedure's): HWND, the template, HINSTANCE; the template
 * a resource's name, a far pointer, a global block (how); an lParam for
 * WM_INITDIALOG before them (param). */
enum { TPL_RESOURCE, TPL_FAR, TPL_GLOBAL };

static __attribute__((noinline)) int dialog_box(args *a, ne_user *u, int modeless, int how,
                                                int param)
{
    static const char *const builtin[] = { "BUTTON", "EDIT", "STATIC", "LISTBOX",
                                           "SCROLLBAR", "COMBOBOX" };
    uint32_t at = param ? 8 : 4;
    uint32_t init = param ? dw(a, 4) : 0;
    uint32_t proc = dw(a, at);
    HWND owner = w(a, at + 4);
    uint32_t name = how == TPL_GLOBAL ? w(a, at + 6) : dw(a, at + 6);
    a->inst = w(a, at + (how == TPL_GLOBAL ? 8 : 10));
    if (a->bad)
        return -1;
    uint32_t n;
    uint8_t *d = how == TPL_RESOURCE ? resource(a, RT_DIALOG_, name, &n)
                                     : template_at(a, name, how == TPL_GLOBAL, &n);
    if (!d)
        return -1;
    const uint8_t *p = d, *end = d + n;
    DLGTEMPLATE_N t;
    DLGITEM_N items[64];
    memset(&t, 0, sizeof(t));
    int r = -1, k = 0;
    HMENU hmenu = 0;
    if (n < 13)
        goto done;
    t.style = rd32(p);
    t.cdit = p[4];
    t.x = (int16_t)rd16(p + 5);
    t.y = (int16_t)rd16(p + 7);
    t.cx = (int16_t)rd16(p + 9);
    t.cy = (int16_t)rd16(p + 11);
    p += 13;
    const char *menu = NULL;
    uint16_t menu_id = 0;
    if (p + 3 <= end && *p == 0xff) {           /* its menu: a number or a name */
        menu_id = rd16(p + 1);
        p += 3;
    } else if (!(menu = res_string(&p, end))) {
        goto done;
    }
    const char *cls = res_string(&p, end), *caption = res_string(&p, end);
    if (!cls || !caption)
        goto done;
    t.klass = *cls ? cls : NULL;
    t.caption = caption;
    if (!(t.style & WS_CHILD)) {                /* ... or its class's, as a window's */
        if (menu_id)
            hmenu = load_menu(a, menu_id);
        else if (menu && *menu)
            hmenu = load_menu_named(a, menu);
        for (int i = 0; !hmenu && t.klass && i < NE_CLASSES; i++)
            if (u->cls[i].proc && !lstrcmpi(u->cls[i].name, t.klass)) {
                if (u->cls[i].menu_id)
                    hmenu = load_menu(a, u->cls[i].menu_id);
                else if (u->cls[i].menu[0])
                    hmenu = load_menu_named(a, u->cls[i].menu);
            }
    }
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
        uint32_t outer = u->pending, outer_param = u->pending_param;
        u->pending = proc;
        u->pending_param = init;
        user_dialog_menu(hmenu);
        r = modeless ? (int)CreateDialogIndirectN(instance(u, 1), &t, items, owner, ne_dlgproc)
                     : DialogBoxIndirectN(instance(u, 1), &t, items, owner, ne_dlgproc);
        u->pending = outer;
        u->pending_param = outer_param;
        hmenu = 0;                            /* the window's now */
    }
done:
    if (hmenu)
        DestroyMenu(hmenu);
    free(d);
    return r;
}

/* ---------------------------------------------------------------- services */

/* LoadCursor, LoadIcon, LoadBitmap: the system's (instance 0, a number),
 * or the program's resources (ne_res.c); its icons and cursors once each,
 * destroyed when it ends */
static __attribute__((noinline)) uint16_t load_image(args *a, ne_user *u, uint16_t service)
{
    uint32_t far = dw(a, 4);
    uint16_t inst = w(a, 8);
    char name[32];
    if (a->bad)
        return 0;
    if (!inst) {
        if (far >> 16)
            return 0;
        return service == 0x10ad ? LoadCursor(0, MAKEINTRESOURCE((WORD)far)) :
               service == 0x10ae ? LoadIcon(0, MAKEINTRESOURCE((WORD)far)) : 0;
    }
    uint16_t id = (uint16_t)far;
    name[0] = 0;
    if ((far >> 16) && !win_ne_string(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, name,
                                      sizeof(name)))
        return a->bad = 1, 0;
    const char *nm = (far >> 16) ? name : NULL;
    a->inst = inst;
    const char *path = res_path(a);
    if (service == 0x10af)
        return win_ne_bitmap(path, id, nm);
    int kind = service == 0x10ad ? 1 : 2, free_slot = -1;
    for (int i = 0; i < NE_IMAGES; i++) {
        if (!u->image[i].h) {
            if (free_slot < 0)
                free_slot = i;
            continue;
        }
        if (u->image[i].kind == kind && u->image[i].id == (nm ? 0 : id) &&
            (win_ne_guest_dll_of(a->ctx->image, u->image[i].inst) ==
             win_ne_guest_dll_of(a->ctx->image, inst)) &&
            !lstrcmpi(u->image[i].name, name))
            return u->image[i].h;
    }
    HANDLE h = kind == 1 ? win_ne_cursor(path, id, nm) : win_ne_icon(path, id, nm);
    if (h && free_slot >= 0) {
        u->image[free_slot].h = h;
        u->image[free_slot].kind = (uint8_t)kind;
        u->image[free_slot].id = nm ? 0 : id;
        u->image[free_slot].inst = inst;
        lstrcpyn(u->image[free_slot].name, name, sizeof(u->image[free_slot].name));
    }
    return h;
}

/* the dialog item texts, each in a frame of its own (not in the
 * dispatcher's, which nested window procedures stack up) */
static __attribute__((noinline)) void set_dlg_text(args *a, win_ne_guest_api_context *ctx)
{
    uint32_t p = dw(a, 4);
    char text[256];
    if (!a->bad && win_ne_string(ctx, (uint16_t)(p >> 16), (uint16_t)p, text, sizeof(text)))
        SetDlgItemText(w(a, 10), (int16_t)w(a, 8), text);
    else
        a->bad = 1;
}

static __attribute__((noinline)) uint16_t get_dlg_text(args *a, win_ne_guest_api_context *ctx)
{
    int max = (int16_t)w(a, 4);
    uint32_t p = dw(a, 6);
    char text[256];
    if (a->bad || max <= 0)
        return 0;
    if (max > (int)sizeof(text))
        max = (int)sizeof(text);
    uint16_t v = (uint16_t)GetDlgItemText(w(a, 12), (int16_t)w(a, 10), text, max);
    if (!win_ne_write(ctx, (uint16_t)(p >> 16), (uint16_t)p, text, v + 1u))
        a->bad = 1;
    return v;
}

int win_ne_guest_user(win_ne_guest_api_context *ctx, win_ne_regs *cpu, uint16_t service,
                      uint32_t *value, int *dword)
{
    ne_user *u = ctx ? (ne_user *)ctx->user : NULL;
    if (!u)
        return 0;
    args a = { ctx, cpu, 0, 0 };
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
    case 0x1029: case 0x11c4:           /* CreateWindow, CreateWindowEx */
        v = ne_createwindow(&a, u, service == 0x11c4);
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
    case 0x1059:                        /* CreateDialog */
        v = (uint16_t)dialog_box(&a, u, service == 0x1059, TPL_RESOURCE, 0);
        break;
    case 0x10ef: case 0x10f1:           /* DialogBoxParam, CreateDialogParam(..., LPARAM) */
        v = (uint16_t)dialog_box(&a, u, service == 0x10f1, TPL_RESOURCE, 1);
        break;
    case 0x10da: case 0x10f0:           /* DialogBoxIndirect(HINSTANCE, HGLOBAL, HWND, DLGPROC),
                                         * DialogBoxIndirectParam */
        v = (uint16_t)dialog_box(&a, u, 0, TPL_GLOBAL, service == 0x10f0);
        break;
    case 0x10db: case 0x10f2:           /* CreateDialogIndirect(HINSTANCE, const void FAR *,
                                         * HWND, DLGPROC), CreateDialogIndirectParam */
        v = (uint16_t)dialog_box(&a, u, 1, TPL_FAR, service == 0x10f2);
        break;
    case 0x1058:                        /* EndDialog(HWND, int) */
        v = (uint16_t)EndDialog(w(&a, 6), (int16_t)w(&a, 4));
        break;
    case 0x105b:                        /* GetDlgItem(HWND, int) */
        v = GetDlgItem(w(&a, 6), (int16_t)w(&a, 4));
        break;
    case 0x105c:                        /* SetDlgItemText(HWND, int, LPCSTR) */
        set_dlg_text(&a, ctx);
        break;
    case 0x105d:                        /* GetDlgItemText(HWND, int, LPSTR, int) */
        v = get_dlg_text(&a, ctx);
        break;
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
        a.inst = w(&a, 8);
        v = load_menu(&a, dw(&a, 4));
        break;
    case 0x10dc: {                      /* LoadMenuIndirect(const void FAR *): a template */
        uint32_t n;
        uint8_t *d = template_at(&a, dw(&a, 4), 0, &n);
        HMENU m = d ? CreateMenu() : 0;
        if (m && (n < 4 || !menu_items(m, d + 4, d + n))) {
            DestroyMenu(m);
            m = 0;
        }
        free(d);
        v = m;
        break;
    }
    case 0x10b0:                        /* LoadString(HINSTANCE, UINT, LPSTR, int) */
        v = load_string(&a);
        break;
    case 0x10b1:                        /* LoadAccelerators(HINSTANCE, LPCSTR) */
        a.inst = w(&a, 8);
        v = load_accelerators(&a, u, dw(&a, 4));
        break;
    case 0x10b2: {                      /* TranslateAccelerator(HWND, HACCEL, LPMSG) */
        MSG m;
        if (msg_from_guest(&a, dw(&a, 4), &m))
            v = (uint16_t)TranslateAccelerator(w(&a, 10), w(&a, 8), &m);
        break;
    }
    case 0x106b:                        /* DefWindowProc(HWND, UINT, WPARAM, LPARAM) */
        v = (uint32_t)ne_defproc(&a, u, DefWindowProc, w(&a, 12), w(&a, 10), w(&a, 8), dw(&a, 4));
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
        m.lParam = (LPARAM)ne_dde_in(ctx, u, m.message, (uint32_t)m.lParam, 1);
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
    case 0x10ae:                        /* LoadIcon(HINSTANCE, LPCSTR) */
    case 0x10af:                        /* LoadBitmap(HINSTANCE, LPCSTR) */
        v = load_image(&a, u, service);
        break;
    case 0x11a4:                        /* wsprintf(LPSTR, LPCSTR, ...) */
        v = (uint16_t)ne_format(&a, dw(&a, 4), dw(&a, 8), 0);
        break;
    case 0x3021:                        /* TextOut(HDC, int, int, LPCSTR, int) */
        v = ne_textout(&a);
        break;
    case 0x3057:                        /* GetStockObject(int) */
        v = GetStockObject((int16_t)w(&a, 4));
        break;
    default:
        if (!ne_call(&a, u, service, &v, dword))
            return 0;
        break;
    }
    if (a.bad)
        ctx->invalid_args = 1;
    *value = v;
    return 1;
}

#endif
