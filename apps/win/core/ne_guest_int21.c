/*
 * ne_guest_int21.c - DOS for a Win16 program: its INT 21h (and DOS3Call)
 * as its C runtime makes them (files, directories, the search, the date),
 * done with WINN's own file functions (file.c), in its task: its files are
 * closed when it ends. Its buffers and names are in its own memory, read
 * and written through its selectors and checked against them. The standard
 * handles 0-4 are the console Windows programs have none of: writes go
 * nowhere, a read gets nothing.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include "wincore.h"
#include "ne_guest_api.h"
#include "ne_guest_int.h"

#if defined(__arm__) || defined(__thumb__) || defined(WINN_NE_HOST)

/* DOS's error codes */
enum { E_FUNCTION = 1, E_NO_FILE = 2, E_NO_PATH = 3, E_ACCESS = 5, E_HANDLE = 6,
       E_NO_MORE = 18 };

#define STD_HANDLES 5

static int fail(ne_user *u, win_ne_dos_regs *r, uint16_t code)
{
    u->dos_error = code;
    r->carry = 1;
    r->ax = code;
    return 1;
}

static int ok(win_ne_dos_regs *r)
{
    r->carry = 0;
    return 1;
}

/* a name at sel:off */
static int name_at(win_ne_guest_api_context *ctx, uint16_t sel, uint16_t off, char *buf,
                   uint32_t cap)
{
    return win_ne_string(ctx, sel, off, buf, cap) && buf[0];
}

/* a handle of the program's files */
static int is_file(uint16_t h)
{
    return _llseek((HFILE)h, 0, 1) != HFILE_ERROR;
}

/* one of the standard handles (a file's handle may have the number, on
 * the host) */
static int is_std(uint16_t h)
{
    return h < STD_HANDLES && !is_file(h);
}

/* 3Fh, 40h: CX bytes at DS:DX, in pieces */
static int transfer(win_ne_guest_api_context *ctx, ne_user *u, win_ne_dos_regs *r, int write)
{
    uint8_t buf[256];
    if (is_std(r->bx)) {
        r->ax = write ? r->cx : 0;      /* no console */
        return ok(r);
    }
    if (!is_file(r->bx))
        return fail(u, r, E_HANDLE);
    uint32_t done = 0;
    while (done < r->cx) {
        uint32_t n = r->cx - done;
        if (n > sizeof(buf))
            n = sizeof(buf);
        uint16_t off = (uint16_t)(r->dx + done);
        UINT got;
        if (write) {
            if (!win_ne_read(ctx, r->ds, off, buf, n))
                return fail(u, r, E_ACCESS);
            got = _lwrite((HFILE)r->bx, buf, (UINT)n);
        } else {
            got = _lread((HFILE)r->bx, buf, (UINT)n);
            if (got != (UINT)HFILE_ERROR && got && !win_ne_write(ctx, r->ds, off, buf, got))
                return fail(u, r, E_ACCESS);
        }
        if (got == (UINT)HFILE_ERROR)
            return fail(u, r, E_ACCESS);
        done += got;
        if (got < n)
            break;
    }
    r->ax = (uint16_t)done;
    return ok(r);
}

/* the DTA: the PSP's 80h until the program sets one */
static uint32_t dta(win_ne_guest_api_context *ctx, ne_user *u)
{
    return u->dta ? u->dta : (uint32_t)ctx->image->psp.selector << 16 | 0x80u;
}

/* 4Eh, 4Fh: the search's next entry into the DTA (name at 30, attribute
 * at 21, time, date, size from 22); the entries are looked up anew each
 * time, so no search lives across the program's turns */
static int find(win_ne_guest_api_context *ctx, ne_user *u, win_ne_dos_regs *r)
{
    int want = u->find.next + 1;
    WINFIND_N *all = (WINFIND_N *)win_alloc((uint32_t)want * sizeof(WINFIND_N));
    if (!all)
        return fail(u, r, E_NO_MORE);
    int n = WinFindFilesN(u->find.spec, u->find.attr, all, want);
    if (n < want) {
        win_free(all);
        return fail(u, r, u->find.next ? E_NO_MORE : E_NO_FILE);
    }
    const WINFIND_N *f = &all[want - 1];
    uint8_t d[43];
    memset(d, 0, sizeof(d));
    d[21] = f->attr;
    wr16(d + 22, f->time);
    wr16(d + 24, f->date);
    wr32(d + 26, f->size);
    memcpy(d + 30, f->name, 12);
    win_free(all);
    uint32_t to = dta(ctx, u);
    if (!win_ne_write(ctx, (uint16_t)(to >> 16), (uint16_t)to, d, sizeof(d)))
        return fail(u, r, E_ACCESS);
    u->find.next = want;
    return ok(r);
}

/* 47h: the current directory of drive DL (0 the current one), without the
 * drive and the first backslash, into DS:SI (64 bytes) */
static int getcwd_(win_ne_guest_api_context *ctx, ne_user *u, win_ne_dos_regs *r)
{
    char cwd[80];
    if ((r->dx & 0xff) && (r->dx & 0xff) - 1 != win_file_drive(-1))
        return fail(u, r, 15);          /* only the current drive's */
    if (GetCurrentDirectoryN(cwd, sizeof(cwd)) < 3)
        return fail(u, r, E_NO_PATH);
    const char *p = cwd + 3;
    uint32_t n = (uint32_t)strlen(p) + 1;
    if (n > 64 || !win_ne_write(ctx, r->ds, r->si, p, n))
        return fail(u, r, E_ACCESS);
    r->ax = 0x0100;
    return ok(r);
}

static __attribute__((noinline)) int paths(win_ne_guest_api_context *ctx, ne_user *u,
                                           win_ne_dos_regs *r)
{
    char path[128], to[128];
    uint8_t ah = (uint8_t)(r->ax >> 8);
    if (!name_at(ctx, r->ds, r->dx, path, sizeof(path)))
        return fail(u, r, E_NO_PATH);
    switch (ah) {
    case 0x39:                          /* mkdir */
        return CreateDirectoryN(path) ? ok(r) : fail(u, r, E_ACCESS);
    case 0x3a:                          /* rmdir */
        return RemoveDirectoryN(path) ? ok(r) : fail(u, r, E_ACCESS);
    case 0x3b:                          /* chdir */
        return win_file_chdir(path) ? ok(r) : fail(u, r, E_NO_PATH);
    case 0x3c:                          /* create (truncate) */
    case 0x5b: {                        /* create new */
        HFILE f;
        if (ah == 0x5b && (f = _lopen(path, OF_READ)) != HFILE_ERROR) {
            _lclose(f);
            return fail(u, r, 80);      /* the file exists */
        }
        f = _lcreat(path, r->cx);
        if (f == HFILE_ERROR)
            return fail(u, r, E_ACCESS);
        if (r->cx & 0x01)
            win_file_attr(path, 1, r->cx & 0x27);
        r->ax = (uint16_t)f;
        return ok(r);
    }
    case 0x3d: {                        /* open, AL: the access (sharing ignored) */
        HFILE f = _lopen(path, r->ax & 3);
        if (f == HFILE_ERROR)
            return fail(u, r, win_file_attr(path, 0, 0) < 0 ? E_NO_FILE : E_ACCESS);
        r->ax = (uint16_t)f;
        return ok(r);
    }
    case 0x41:                          /* delete */
        return DeleteFileN(path) ? ok(r) :
               fail(u, r, win_file_attr(path, 0, 0) < 0 ? E_NO_FILE : E_ACCESS);
    case 0x43: {                        /* attributes: AL 0 get (CX), 1 set */
        int at = win_file_attr(path, (r->ax & 0xff) == 1, r->cx);
        if (at < 0)
            return fail(u, r, E_NO_FILE);
        r->cx = (uint16_t)at;
        return ok(r);
    }
    case 0x4e:                          /* find first: CX the attributes */
        lstrcpyn(u->find.spec, path, sizeof(u->find.spec));
        u->find.attr = r->cx;
        u->find.next = 0;
        return find(ctx, u, r);
    case 0x56:                          /* rename DS:DX to ES:DI */
        if (!name_at(ctx, r->es, r->di, to, sizeof(to)))
            return fail(u, r, E_NO_PATH);
        return MoveFileN(path, to) ? ok(r) :
               fail(u, r, win_file_attr(path, 0, 0) < 0 ? E_NO_FILE : E_ACCESS);
    }
    return 0;
}

static uint8_t bcd(unsigned v) { return (uint8_t)((v / 10u % 10u) << 4 | v % 10u); }

/* INT 2Fh, the multiplex, as Windows 3.1 in enhanced mode answers a
 * program: Windows there (its version GetVersion's), the idle calls, no
 * XMS, no CD-ROM extensions; the rest as without anyone to answer */
static void multiplex(win_ne_dos_regs *r)
{
    uint16_t version = (uint16_t)GetVersion();          /* 0A03h: 3.10 */
    switch (r->ax) {
    case 0x1600:                        /* enhanced mode: AL major, AH minor */
        r->ax = version;
        break;
    case 0x160a:                        /* the version (BX, major high) and mode (CX) */
        r->ax = 0;
        r->bx = (uint16_t)((version & 0xffu) << 8 | version >> 8);
        r->cx = 3;
        break;
    case 0x1680:                        /* release the time slice: done */
        r->ax &= 0xff00u;
        break;
    case 0x1683:                        /* the virtual machine: the system's */
        r->bx = 1;
        break;
    case 0x1686:                        /* in protected mode */
        r->ax = 0;
        break;
    case 0x1500:                        /* MSCDEX: no drives */
        r->bx = 0;
        break;
    case 0x4300:                        /* XMS: none for a Windows program */
    case 0x1a00:                        /* ANSI.SYS: none */
        r->ax &= 0xff00u;
        break;
    default:
        break;
    }
}

/* DPMI's errors */
enum { DPMI_UNSUPPORTED = 0x8001, DPMI_NO_DESCRIPTOR = 0x8011, DPMI_NO_MEMORY = 0x8013,
       DPMI_BAD_SELECTOR = 0x8022, DPMI_DOS_MEMORY = 0x0008 };

static void dpmi_fail(win_ne_dos_regs *r, uint16_t code)
{
    r->ax = code;
    r->carry = 1;
}

/* a descriptor as the CPU has it: base, limit (in pages past 1 MB), a
 * code or data segment of ring 3 */
static void descriptor(uint8_t d[8], uint32_t base, uint32_t limit, int code)
{
    int pages = limit > 0xfffffu;
    if (pages)
        limit >>= 12;
    d[0] = (uint8_t)limit;
    d[1] = (uint8_t)(limit >> 8);
    d[2] = (uint8_t)base;
    d[3] = (uint8_t)(base >> 8);
    d[4] = (uint8_t)(base >> 16);
    d[5] = (uint8_t)(code ? 0xfau : 0xf2u);
    d[6] = (uint8_t)((limit >> 16 & 0x0fu) | (pages ? 0x80u : 0u));
    d[7] = (uint8_t)(base >> 24);
}

/* INT 31h, DPMI 0.9 as Windows gives a program of its: the descriptors
 * (the program's own selectors, as AllocSelector's), DOS memory (as
 * GlobalDOSAlloc's), the version and the free memory; the interrupt
 * vectors and locking taken and kept by nobody (WINN has its own IDT and
 * no paging); real mode calls and callbacks, linear memory: errors */
static void dpmi(win_ne_guest_api_context *ctx, win_ne_dos_regs *r)
{
    win_ne_guest_image *g = (win_ne_guest_image *)ctx->image;
    uint32_t base, limit;
    int code;
    r->carry = 0;
    if (!g) {
        dpmi_fail(r, DPMI_UNSUPPORTED);
        return;
    }
    switch (r->ax) {
    case 0x0000: {                      /* CX descriptors in a row: AX the first */
        uint16_t sel = r->cx ? win_ne_guest_selector_array(g, r->cx) : 0;
        if (!sel)
            dpmi_fail(r, DPMI_NO_DESCRIPTOR);
        else
            r->ax = sel;
        break;
    }
    case 0x0001:                        /* free BX */
        if (!win_ne_guest_global_free(g, r->bx))
            dpmi_fail(r, DPMI_BAD_SELECTOR);
        break;
    case 0x0002: {                      /* the real mode segment BX: AX its selector */
        uint16_t sel = win_ne_guest_selector_alloc(g, 0, 0);
        if (!sel || !win_ne_guest_selector_set(g, sel, (uint32_t)r->bx << 4, 0xffffu, 0))
            dpmi_fail(r, DPMI_NO_DESCRIPTOR);
        else
            r->ax = sel;
        break;
    }
    case 0x0003:                        /* the selectors' increment */
        r->ax = 8;
        break;
    case 0x0006:                        /* BX's base: CX:DX */
        if (!win_ne_guest_selector_get(g, r->bx, &base, &limit, &code)) {
            dpmi_fail(r, DPMI_BAD_SELECTOR);
            break;
        }
        r->cx = (uint16_t)(base >> 16);
        r->dx = (uint16_t)base;
        break;
    case 0x0007: case 0x0008:           /* BX's base, its limit: CX:DX */
        if (!win_ne_guest_selector_get(g, r->bx, &base, &limit, &code) ||
            !win_ne_guest_selector_set(g, r->bx,
                                       r->ax == 7 ? (uint32_t)r->cx << 16 | r->dx : base,
                                       r->ax == 8 ? (uint32_t)r->cx << 16 | r->dx : limit, code))
            dpmi_fail(r, DPMI_BAD_SELECTOR);
        break;
    case 0x0009:                        /* BX's access rights CL: code or data */
        if (!win_ne_guest_selector_get(g, r->bx, &base, &limit, &code) ||
            !(r->cx & 0x10u) || (r->cx & 0x60u) != 0x60u ||
            !win_ne_guest_selector_set(g, r->bx, base, limit, (r->cx & 0x08u) != 0))
            dpmi_fail(r, DPMI_BAD_SELECTOR);
        break;
    case 0x000a: {                      /* a data alias of BX: AX */
        uint16_t sel = win_ne_guest_selector_get(g, r->bx, &base, &limit, &code) ?
                       win_ne_guest_selector_alloc(g, r->bx, 0) : 0;
        if (!sel)
            dpmi_fail(r, DPMI_BAD_SELECTOR);
        else
            r->ax = sel;
        break;
    }
    case 0x000b: case 0x000c: {         /* BX's descriptor to, from ES:DI */
        uint8_t d[8];
        if (!win_ne_guest_selector_get(g, r->bx, &base, &limit, &code)) {
            dpmi_fail(r, DPMI_BAD_SELECTOR);
            break;
        }
        if (r->ax == 0x0b) {
            descriptor(d, base, limit, code);
            if (!win_ne_write(ctx, r->es, r->di, d, 8))
                dpmi_fail(r, DPMI_BAD_SELECTOR);
            break;
        }
        if (!win_ne_read(ctx, r->es, r->di, d, 8) || !(d[5] & 0x10u) ||
            (d[5] & 0x60u) != 0x60u) {
            dpmi_fail(r, DPMI_BAD_SELECTOR);
            break;
        }
        base = (uint32_t)d[2] | (uint32_t)d[3] << 8 | (uint32_t)d[4] << 16 |
               (uint32_t)d[7] << 24;
        limit = (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)(d[6] & 0x0fu) << 16;
        if (d[6] & 0x80u)
            limit = limit << 12 | 0xfffu;
        if (!win_ne_guest_selector_set(g, r->bx, base, limit, (d[5] & 0x08u) != 0))
            dpmi_fail(r, DPMI_BAD_SELECTOR);
        break;
    }
    case 0x0100: {                      /* BX paragraphs of DOS memory: AX segment, DX selector */
        uint16_t sel = r->bx ? win_ne_guest_global_alloc(g, (uint32_t)r->bx << 4) : 0;
        win_ne_guest_segment *s = sel ? win_ne_guest_global(g, sel) : NULL;
        if (s && !(s->linear & 15u) && s->linear < 0x100000u) {
            r->ax = (uint16_t)(s->linear >> 4);
            r->dx = sel;
            break;
        }
        if (sel)
            win_ne_guest_global_free(g, sel);
        dpmi_fail(r, DPMI_DOS_MEMORY);
        r->bx = 0;                      /* the largest there is: unknown */
        break;
    }
    case 0x0101:                        /* free DOS memory DX */
        if (!win_ne_guest_global_free(g, r->dx))
            dpmi_fail(r, 0x0009);       /* the DOS error: a bad block */
        break;
    case 0x0200: case 0x0204:           /* an interrupt's vector BL: CX:DX, none */
        r->cx = r->dx = 0;
        break;
    case 0x0201: case 0x0205:           /* ... set: taken, nobody calls it */
    case 0x0600: case 0x0601: case 0x0602: case 0x0603:   /* locked, unlocked: no paging */
    case 0x0702: case 0x0703:
    case 0x0e01:                        /* the coprocessor's emulation: none needed */
        break;
    case 0x0400:                        /* 0.90; a 386 (32 bits), no real mode reflection by
                                         * virtual 86; the PICs' bases */
        r->ax = 0x005a;
        r->bx = 0x0005;
        r->cx = (uint16_t)((r->cx & 0xff00u) | 3u);
        r->dx = 0x0870;
        break;
    case 0x0500: {                      /* free memory: ES:DI, 48 bytes */
        uint8_t info[48];
        memset(info, 0xff, sizeof(info));
        uint32_t free = 0x00080000u, pages = free >> 12;
        for (int i = 0; i < 4; i++) {
            info[i] = (uint8_t)(free >> (8 * i));       /* the largest block */
            info[4 + i] = (uint8_t)(pages >> (8 * i));  /* unlocked pages */
            info[8 + i] = (uint8_t)(pages >> (8 * i));  /* lockable pages */
        }
        if (!win_ne_write(ctx, r->es, r->di, info, sizeof(info)))
            dpmi_fail(r, DPMI_BAD_SELECTOR);
        break;
    }
    case 0x0900: case 0x0901: case 0x0902:   /* the virtual interrupt flag: it was on */
        r->ax = (uint16_t)((r->ax & 0xff00u) | 1u);
        break;
    case 0x0e00:                        /* the coprocessor: there, a 487's, no emulation */
        r->ax = 0x0045;
        break;
    case 0x0501:                        /* linear memory */
        dpmi_fail(r, DPMI_NO_MEMORY);
        break;
    default:                            /* real mode calls, callbacks, the rest */
        dpmi_fail(r, DPMI_UNSUPPORTED);
        break;
    }
}

void win_ne_bios(win_ne_guest_api_context *ctx, unsigned vector, win_ne_dos_regs *r)
{
    uint8_t ah = (uint8_t)(r->ax >> 8), al = (uint8_t)r->ax;
    switch (vector) {
    case 0x10:                          /* video: VGA's answers, nothing set */
        if (ah == 0x0f)                 /* the mode: 12h, 80 columns, page 0 */
            r->ax = 0x5012, r->bx &= 0x00ffu;
        else if (r->ax == 0x1a00)       /* the display combination: VGA, colour */
            r->ax = 0x001a, r->bx = 0x0008;
        else if (ah == 0x12 && (r->bx & 0xff) == 0x10)  /* EGA's: colour, 256K */
            r->bx = 0x0003, r->cx = 0x0009;
        break;
    case 0x11:                          /* the equipment: the coprocessor, 80x25 colour */
        r->ax = 0x0022;
        break;
    case 0x12:                          /* conventional memory: 640K */
        r->ax = 640;
        break;
    case 0x16:                          /* the keyboard: the program has its messages */
        if (ah == 0x00 || ah == 0x10) { /* a key: none comes this way */
            r->ax = 0;
        } else if (ah == 0x01 || ah == 0x11) {  /* none waiting */
            r->zero = 1;
        } else if (ah == 0x02 || ah == 0x12) {  /* the shift keys' state */
            uint8_t s = 0;
            if (GetKeyState(VK_SHIFT) < 0) s |= 0x02;
            if (GetKeyState(VK_CONTROL) < 0) s |= 0x04;
            if (GetKeyState(VK_MENU) < 0) s |= 0x08;
            if (GetKeyState(VK_SCROLL) & 1) s |= 0x10;
            if (GetKeyState(VK_NUMLOCK) & 1) s |= 0x20;
            if (GetKeyState(VK_CAPITAL) & 1) s |= 0x40;
            r->ax = (uint16_t)((r->ax & 0xff00u) | s);
        }
        break;
    case 0x1a: {                        /* the clock */
        SYSTEMTIME_N t;
        GetLocalTimeN(&t);
        if (ah == 0x00) {               /* ticks since midnight, 18.2 a second */
            uint32_t ms = ((t.wHour * 60u + t.wMinute) * 60u + t.wSecond) * 1000u +
                          t.wMilliseconds;
            uint32_t ticks = (uint32_t)((uint64_t)ms * 1193180u / 65536000u);
            r->cx = (uint16_t)(ticks >> 16);
            r->dx = (uint16_t)ticks;
            r->ax = (uint16_t)(r->ax & 0xff00u);
        } else if (ah == 0x02) {        /* the RTC's time, BCD */
            r->cx = (uint16_t)(bcd(t.wHour) << 8 | bcd(t.wMinute));
            r->dx = (uint16_t)(bcd(t.wSecond) << 8);
            r->carry = 0;
        } else if (ah == 0x04) {        /* the RTC's date, BCD */
            r->cx = (uint16_t)(bcd(t.wYear / 100u) << 8 | bcd(t.wYear % 100u));
            r->dx = (uint16_t)(bcd(t.wMonth) << 8 | bcd(t.wDay));
            r->carry = 0;
        }
        break;
    }
    case 0x2f:
        multiplex(r);
        break;
    case 0x31:
        dpmi(ctx, r);
        break;
    case 0x33:                          /* the mouse: no DOS driver, the program has its messages */
        if (r->ax == 0x0000 || r->ax == 0x0021)
            r->ax = 0;
        break;
    default:                            /* DOS's others (22h ... 2Eh): as nobody's there */
        break;
    }
    (void)al;
}

int win_ne_dos(win_ne_guest_api_context *ctx, win_ne_dos_regs *r)
{
    ne_user *u = ctx ? (ne_user *)ctx->user : NULL;
    if (!u)
        return 0;
    uint8_t ah = (uint8_t)(r->ax >> 8), al = (uint8_t)r->ax;
    switch (ah) {
    case 0x0e:                          /* select drive DL: AL the number of drives */
        r->ax = (uint16_t)((r->ax & 0xff00) | (uint8_t)win_file_drive(r->dx & 0xff));
        return 1;
    case 0x19:                          /* the current drive */
        r->ax = (uint16_t)((r->ax & 0xff00) | (uint8_t)win_file_drive(-1));
        return 1;
    case 0x1a:                          /* the DTA: DS:DX */
        u->dta = (uint32_t)r->ds << 16 | r->dx;
        return 1;
    case 0x2f: {                        /* the DTA: ES:BX */
        uint32_t d = dta(ctx, u);
        r->es = (uint16_t)(d >> 16);
        r->bx = (uint16_t)d;
        return 1;
    }
    case 0x2a: case 0x2c: {             /* the date, the time */
        SYSTEMTIME_N t;
        GetLocalTimeN(&t);
        if (ah == 0x2a) {
            r->cx = t.wYear;
            r->dx = (uint16_t)(t.wMonth << 8 | t.wDay);
            r->ax = (uint16_t)(r->ax & 0xff00) | (uint8_t)t.wDayOfWeek;
        } else {
            r->cx = (uint16_t)(t.wHour << 8 | t.wMinute);
            r->dx = (uint16_t)(t.wSecond << 8 | t.wMilliseconds / 10);
        }
        return 1;
    }
    case 0x25:                          /* its own interrupt handlers: none */
        return 1;
    case 0x35:                          /* an interrupt's handler: none */
        r->es = r->bx = 0;
        return 1;
    case 0x33:                          /* Ctrl-Break checking: off */
        if (al == 0)
            r->dx = (uint16_t)(r->dx & 0xff00);
        return 1;
    case 0x36: {                        /* free space of drive DL */
        DWORD free_b, total;
        if (!GetDiskFreeSpaceN(r->dx & 0xff, &free_b, &total)) {
            r->ax = 0xffff;
            return 1;
        }
        r->ax = 8;                      /* sectors per cluster: 4K clusters */
        r->cx = 512;
        r->bx = (uint16_t)(free_b / 4096u > 0xffffu ? 0xffffu : free_b / 4096u);
        r->dx = (uint16_t)(total / 4096u > 0xffffu ? 0xffffu : total / 4096u);
        return 1;
    }
    case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x41: case 0x43:
    case 0x4e: case 0x56: case 0x5b:
        return paths(ctx, u, r);
    case 0x3e:                          /* close */
        if (is_std(r->bx))
            return ok(r);
        return _lclose((HFILE)r->bx) != HFILE_ERROR ? ok(r) : fail(u, r, E_HANDLE);
    case 0x3f:
        return transfer(ctx, u, r, 0);
    case 0x40:
        return transfer(ctx, u, r, 1);
    case 0x42: {                        /* seek: AL the origin, CX:DX; DX:AX */
        if (is_std(r->bx)) {
            r->ax = r->dx = 0;
            return ok(r);
        }
        LONG at = al > 2 ? HFILE_ERROR :
                  _llseek((HFILE)r->bx, (LONG)((uint32_t)r->cx << 16 | r->dx), al);
        if (at == HFILE_ERROR)
            return fail(u, r, al > 2 ? E_FUNCTION : E_HANDLE);
        r->ax = (uint16_t)at;
        r->dx = (uint16_t)((uint32_t)at >> 16);
        return ok(r);
    }
    case 0x44:                          /* IOCTL */
        if (al == 0) {                  /* device information */
            if (is_std(r->bx))
                r->dx = r->bx == 0 ? 0x80d1 : r->bx == 1 || r->bx == 2 ? 0x80d2 : 0x80c0;
            else if (is_file(r->bx))
                r->dx = (uint16_t)win_file_drive(-1);   /* a file on the drive */
            else
                return fail(u, r, E_HANDLE);
            r->ax = r->dx;
            return ok(r);
        }
        if (al == 1)                    /* set: nothing to set */
            return ok(r);
        if (al == 6 || al == 7) {       /* input, output status: ready */
            r->ax = (uint16_t)(r->ax & 0xff00) | 0xff;
            return ok(r);
        }
        return 0;
    case 0x47:
        return getcwd_(ctx, u, r);
    case 0x4f:                          /* find next */
        if (!u->find.spec[0])
            return fail(u, r, E_NO_MORE);
        return find(ctx, u, r);
    case 0x51: case 0x62:               /* its PSP */
        r->bx = ctx->image->psp.selector;
        return 1;
    case 0x57: {                        /* a file's date and time: AL 0 get, 1 set */
        WORD date = r->dx, time_ = r->cx;
        if (al > 1 || !win_file_time((HFILE)r->bx, al, &date, &time_))
            return fail(u, r, al > 1 ? E_FUNCTION : E_HANDLE);
        r->cx = time_;
        r->dx = date;
        return ok(r);
    }
    case 0x59:                          /* the last error: code, class, action, locus */
        r->ax = u->dos_error;
        r->bx = 0x0d01;                 /* class "not found"/action retry */
        r->cx = (uint16_t)(r->cx & 0xff) | 0x0200;     /* locus: block device */
        return 1;
    case 0x66: {                        /* code pages: AL 1 get (BX, DX), 2 set (BX) */
        UINT active = r->bx, system = 0;
        if ((al != 1 && al != 2) || !win_file_codepage(al == 2, &active, &system))
            return fail(u, r, al != 1 && al != 2 ? E_FUNCTION : 0x25);
        if (al == 1) {
            r->bx = (uint16_t)active;
            r->dx = (uint16_t)system;
        }
        return ok(r);
    }
    case 0x67:                          /* the number of handles: as it is */
        return ok(r);
    case 0x68:                          /* commit: written already */
        return is_std(r->bx) || is_file(r->bx) ? ok(r) : fail(u, r, E_HANDLE);
    }
    return 0;
}

/* ---------------------------------------------------------------- KERNEL and KEYBOARD */

/* KERNEL.74 OpenFile(LPCSTR, OFSTRUCT FAR *, UINT): file.c's, the
 * program's OFSTRUCT (its name when reopening) in and out; given: that name
 * instead of the program's */
__attribute__((noinline)) uint16_t ne_open_file(args *a, const char *given)
{
    UINT mode = w(a, 4);
    uint32_t ofs = dw(a, 6), name_far = dw(a, 10);
    OFSTRUCT of;
    char name[128];
    _Static_assert(sizeof(OFSTRUCT) == 136, "OFSTRUCT as Win16's");
    memset(&of, 0, sizeof(of));
    name[0] = 0;
    if (a->bad)
        return (uint16_t)HFILE_ERROR;
    if (given) {
        lstrcpyn(name, given, sizeof(name));
        mode &= ~OF_REOPEN;
    } else if (mode & OF_REOPEN) {
        if (!win_ne_read(a->ctx, (uint16_t)(ofs >> 16), (uint16_t)ofs, &of, sizeof(of)))
            return a->bad = 1, (uint16_t)HFILE_ERROR;
        of.szPathName[sizeof(of.szPathName) - 1] = 0;
    } else if (!win_ne_string(a->ctx, (uint16_t)(name_far >> 16), (uint16_t)name_far, name,
                              sizeof(name))) {
        return a->bad = 1, (uint16_t)HFILE_ERROR;
    }
    HFILE f = OpenFile(name, &of, mode);
    if (!win_ne_write(a->ctx, (uint16_t)(ofs >> 16), (uint16_t)ofs, &of, sizeof(of)))
        a->bad = 1;
    return (uint16_t)f;
}

/* The program's DOS environment, made before it runs (a C startup reads it
 * through its PSP's 2Ch, no call of KERNEL's): WINN's own strings in a
 * block of the program's, then as DOS 3 has it the count 1 and the
 * program's path; its selector in the PSP. KERNEL.131 GetDOSEnvironment
 * gives the same block. */
void ne_environment(win_ne_guest_api_context *ctx, ne_user *u)
{
    if (u->env)
        return;
    const char *path = ctx->path ? ctx->path : "";
    int room = 4096, tail = 2 + lstrlen(path) + 1;
    char *env = (char *)win_alloc((size_t)(room + tail));
    if (!env)
        return;
    int n = win_file_environment(env, room);
    if (n < 2) {                        /* none: the empty one */
        env[0] = env[1] = 0;
        n = 2;
    }
    env[n] = 1;
    env[n + 1] = 0;
    lstrcpy(env + n + 2, path);
    n += tail;
    win_ne_guest_image *g = (win_ne_guest_image *)ctx->image;
    uint16_t sel = win_ne_guest_global_alloc(g, (uint32_t)n);
    if (sel && win_ne_write(ctx, sel, 0, env, (uint32_t)n)) {
        uint8_t w2[2];
        wr16(w2, sel);
        win_ne_write(ctx, g->psp.selector, 0x2c, w2, 2);
        u->env = sel;
    }
    win_free(env);
}

static uint32_t dos_environment(args *a, ne_user *u)
{
    ne_environment(a->ctx, u);
    return (uint32_t)u->env << 16;
}

/* KEYBOARD's AnsiToOem/OemToAnsi: n bytes (0: up to the NUL) from src to
 * dst, in pieces */
static void convert(args *a, uint32_t src, uint32_t dst, uint32_t n, int to_oem)
{
    uint8_t buf[128];
    int nul = !n;
    if (nul)
        n = 0x10000u;
    for (uint32_t done = 0; done < n && !a->bad; ) {
        uint32_t k = n - done < sizeof(buf) ? n - done : sizeof(buf);
        uint16_t so = (uint16_t)((uint16_t)src + done), d = (uint16_t)((uint16_t)dst + done);
        if (nul) {                          /* a string: byte by byte up to the NUL */
            k = 1;
        }
        if (!win_ne_read(a->ctx, (uint16_t)(src >> 16), so, buf, k)) {
            a->bad = 1;
            break;
        }
        for (uint32_t i = 0; i < k; i++)
            buf[i] = to_oem ? win_ansi_to_oem(buf[i]) : win_oem_to_ansi(buf[i]);
        if (!win_ne_write(a->ctx, (uint16_t)(dst >> 16), d, buf, k)) {
            a->bad = 1;
            break;
        }
        done += k;
        if (nul && !buf[0])
            break;
    }
}

int ne_dos_service(args *a, ne_user *u, uint16_t service, uint32_t *v, int *dword)
{
    switch (service) {
    case 74:                            /* OpenFile */
        *v = ne_open_file(a, NULL);
        return 1;
    case 131:                           /* GetDOSEnvironment */
        *v = dos_environment(a, u);
        *dword = 1;
        return 1;
    case 207:                           /* IsDBCSLeadByte: no double-byte characters */
        *v = 0;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 5:   /* AnsiToOem(LPCSTR, LPSTR) */
    case WIN_NE_SERVICE_KEYBOARD + 6:   /* OemToAnsi */
        convert(a, dw(a, 8), dw(a, 4), 0, service == WIN_NE_SERVICE_KEYBOARD + 5);
        *v = 0xffff;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 134: /* AnsiToOemBuff(LPCSTR, LPSTR, UINT) */
    case WIN_NE_SERVICE_KEYBOARD + 135: { /* OemToAnsiBuff */
        uint32_t n = w(a, 4);
        convert(a, dw(a, 10), dw(a, 6), n ? n : 0x10000u, service == WIN_NE_SERVICE_KEYBOARD + 134);
        return 1;
    }
    case WIN_NE_SERVICE_KEYBOARD + 130: { /* GetKeyboardType(int): an enhanced one */
        int16_t which = (int16_t)w(a, 4);
        *v = which == 0 ? 4 : which == 2 ? 12 : 0;
        return 1;
    }
    case WIN_NE_SERVICE_KEYBOARD + 132: /* GetKBCodePage: the OEM code page */
        *v = 866;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 1: {  /* Inquire(KBINFO FAR *): its size */
        static const uint8_t kb[6] = { 0, 0, 0, 0, 0, 0 };   /* no double-byte ranges */
        uint32_t far = dw(a, 4);
        *v = far && win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, kb, sizeof(kb)) ?
             sizeof(kb) : 0;
        return 1;
    }
    case WIN_NE_SERVICE_KEYBOARD + 2:   /* Enable(proc, state): the driver's, WINN's own */
    case WIN_NE_SERVICE_KEYBOARD + 3:   /* Disable */
    case WIN_NE_SERVICE_KEYBOARD + 100: /* ScreenSwitchEnable(WORD) */
    case WIN_NE_SERVICE_KEYBOARD + 127: /* NewTable */
        *v = 0;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 7:   /* SetSpeed(int): the rate it has */
        *v = 0;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 126: /* GetTableSeg: none */
    case WIN_NE_SERVICE_KEYBOARD + 136: /* EnableKBSysReq(WORD): was off */
        *v = 0;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 137: /* GetBIOSKeyProc: none */
        *v = 0;
        *dword = 1;
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 4: {  /* ToAscii(vk, scan, LPBYTE state, LPVOID buf, flags) */
        uint8_t keys[256];
        uint32_t st = dw(a, 10), buf = dw(a, 6);
        uint16_t vk = w(a, 16), scan = w(a, 14);
        unsigned char ch;
        *v = 0;
        if (!win_ne_read(a->ctx, (uint16_t)(st >> 16), (uint16_t)st, keys, sizeof(keys))) {
            a->bad = 1;
            return 1;
        }
        if (!(scan & 0x8000u) && vk < 256 && user_key_char(keys, vk, &ch)) {
            uint8_t out[2] = { ch, 0 };
            *v = win_ne_write(a->ctx, (uint16_t)(buf >> 16), (uint16_t)buf, out, 2) ? 1 : 0;
        }
        return 1;
    }
    case WIN_NE_SERVICE_KEYBOARD + 128: { /* OemKeyScan(WORD oem): scan code, shift state */
        int k = user_char_key((unsigned char)win_oem_to_ansi((uint8_t)w(a, 4)));
        UINT sc = k >= 0 ? user_map_key((UINT)k & 0xffu, 0) : 0;
        *v = k < 0 || !sc ? 0xffffffffu : sc | (uint32_t)(((unsigned)k >> 8) & 1u) << 17;
        *dword = 1;
        return 1;
    }
    case WIN_NE_SERVICE_KEYBOARD + 129: /* VkKeyScan(char): key | shift state << 8, -1 */
        *v = (uint16_t)user_char_key((unsigned char)w(a, 4));
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 131: /* MapVirtualKey(code, type) */
        *v = user_map_key(w(a, 6), w(a, 4));
        return 1;
    case WIN_NE_SERVICE_KEYBOARD + 133: { /* GetKeyNameText(LONG, LPSTR, int) */
        char name[32];
        int cap = (int16_t)w(a, 4);
        uint32_t far = dw(a, 6);
        int n = user_key_name((LONG)dw(a, 10), name, cap < (int)sizeof(name) ? cap : (int)sizeof(name));
        *v = 0;
        if (cap > 0 && far &&
            win_ne_write(a->ctx, (uint16_t)(far >> 16), (uint16_t)far, name, (uint32_t)n + 1))
            *v = (uint16_t)n;
        return 1;
    }
    }
    return 0;
}

#endif
