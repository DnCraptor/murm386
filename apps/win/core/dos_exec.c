/*
 * dos_exec.c - run a full-screen FDOS child from WINN.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"

int win_dos_is_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    uint8_t h[64];
    size_t n = fread(h, 1, sizeof(h), f);
    int mz = n >= 2 && ((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M'));
    int executable = mz && n >= 28;
    if (executable && n == sizeof(h) && (h[24] >= 0x40 || h[25])) {
        uint32_t at = (uint32_t)h[60] | (uint32_t)h[61] << 8 |
                      (uint32_t)h[62] << 16 | (uint32_t)h[63] << 24;
        if (at >= sizeof(h) && at <= INT32_MAX && !fseek(f, (long)at, SEEK_SET) &&
            fread(h, 1, 4, f) == 4) {
            /* Do not fall through to the DOS stub of a Windows image
             * rejected by the NE/PE loader. DOS extenders can use LE. */
            if ((h[0] == 'N' && h[1] == 'E') || !memcmp(h, "PE\0\0", 4))
                executable = 0;
        }
    }
    if (!mz && n && !(n >= 2 && !memcmp(h, "EZ", 2)) &&
        !(n >= 4 && !memcmp(h, "\177ELF", 4))) {
        size_t len = strlen(path);
        if (len >= 4 && !lstrcmpi(path + len - 4, ".COM") && !fseek(f, 0, SEEK_END)) {
            long size = ftell(f);
            executable = size > 0 && size <= 0xff00;
        }
    }
    fclose(f);
    return executable;
}

#if defined(__arm__) || defined(__thumb__)
#include "dos.h"
#include "dos_mem.h"
#include "user.h"

/* AH=47h in FDOS copies at most FDOS_PATHLEN bytes. */
#define DOS_PATH_BYTES 128
#define DOS_DRIVES 26

typedef struct {
    uint8_t params[22];                /* EXEC parameter block, including LOAD's output area */
    char filename[DOS_PATH_BYTES];
    uint8_t tail[128];                 /* length, up to 126 bytes, CR */
    uint8_t fcb[2][37];
    char directory[DOS_PATH_BYTES];
} dos_exec_block;

typedef struct {
    char directory[DOS_DRIVES][DOS_PATH_BYTES];
    uint8_t drive;
} dos_directories;

static int dos_call(unsigned seg, union REGS *r)
{
    struct SREGS s;
    segread(&s);
    s.ds = s.es = (uint16_t)seg;
    int386x(0x21, r, r, &s);
    return !r->x.cflag;
}

static void put_far(uint8_t *p, uint16_t seg, unsigned offset)
{
    p[0] = (uint8_t)offset;
    p[1] = (uint8_t)(offset >> 8);
    p[2] = (uint8_t)seg;
    p[3] = (uint8_t)(seg >> 8);
}

static void prepare_exec(dos_exec_block *b, uint16_t seg, const char *path, const char *args)
{
    memset(b, 0, sizeof(*b));           /* environment segment 0: inherit */
    strcpy(b->filename, path);
    size_t n = strlen(args);
    if (n) {
        b->tail[1] = ' ';
        memcpy(b->tail + 2, args, n);
        n++;
    }
    b->tail[0] = (uint8_t)n;
    b->tail[n + 1] = '\r';

    put_far(b->params + 2, seg, offsetof(dos_exec_block, tail));
    put_far(b->params + 6, seg, offsetof(dos_exec_block, fcb[0]));
    put_far(b->params + 10, seg, offsetof(dos_exec_block, fcb[1]));

    union REGS r = {0};
    unsigned si = offsetof(dos_exec_block, tail) + 1;
    for (unsigned i = 0; i < 2; i++) {
        r.w.ax = 0x2901;               /* skip leading separator, parse default FCB */
        r.w.si = (uint16_t)si;
        r.w.di = (uint16_t)(offsetof(dos_exec_block, fcb) + i * sizeof(b->fcb[0]));
        dos_call(seg, &r);
        si = r.w.si;
    }
}

static int get_directory(dos_exec_block *b, uint16_t seg, unsigned drive, char *out)
{
    union REGS r = {0};
    r.h.ah = 0x47;
    r.h.dl = (uint8_t)(drive + 1);
    r.w.si = offsetof(dos_exec_block, directory);
    if (!dos_call(seg, &r)) {
        out[0] = 0;
        return 0;
    }
    out[0] = (char)('A' + drive);
    out[1] = ':';
    out[2] = '\\';
    lstrcpyn(out + 3, b->directory, DOS_PATH_BYTES - 3);
    return 1;
}

static int change_directory(dos_exec_block *b, uint16_t seg, const char *path)
{
    if (strlen(path) >= sizeof(b->directory))
        return 0;
    strcpy(b->directory, path);
    union REGS r = {0};
    r.h.ah = 0x3b;
    r.w.dx = offsetof(dos_exec_block, directory);
    return dos_call(seg, &r);
}

static unsigned set_drive(unsigned drive)
{
    union REGS r = {0};
    r.h.ah = 0x0e;
    r.h.dl = (uint8_t)drive;
    int386(0x21, &r, &r);
    r.h.ah = 0x19;
    int386(0x21, &r, &r);
    return r.h.al;
}

static void save_directories(dos_exec_block *b, uint16_t seg, dos_directories *saved)
{
    union REGS r = {0};
    r.h.ah = 0x19;
    int386(0x21, &r, &r);
    saved->drive = r.h.al;
    for (unsigned i = 0; i < DOS_DRIVES; i++)
        get_directory(b, seg, i, saved->directory[i]);
}

static int apply_directories(dos_exec_block *b, uint16_t seg, const dos_directories *saved,
                             unsigned drive, const char *workdir)
{
    const wtask *t = task_current();
    for (unsigned i = 0; i < DOS_DRIVES; i++) {
        const char *dir = i == drive && workdir ? workdir : t->dos_cwd[i];
        char root[] = { (char)('A' + i), ':', '\\', 0 };
        if (!dir && (saved->directory[i][0] || i == drive))
            dir = root;
        if (dir && (!saved->directory[i][0] ||
                    (lstrcmpi(dir, saved->directory[i]) && !change_directory(b, seg, dir))))
            return 0;
    }
    return set_drive(drive) == drive;
}

static void restore_directories(dos_exec_block *b, uint16_t seg, const dos_directories *saved)
{
    char current[DOS_PATH_BYTES];
    for (unsigned i = 0; i < DOS_DRIVES; i++)
        if (saved->directory[i][0] && get_directory(b, seg, i, current) &&
            lstrcmpi(current, saved->directory[i]) &&
            !change_directory(b, seg, saved->directory[i]))
            win_trace_event("dos-cwd-restore", i, 0);
    set_drive(saved->drive);
}

static unsigned current_mode(void)
{
    union REGS r = {0};
    r.h.ah = 0x0f;
    int386(0x10, &r, &r);
    return r.h.al & 0x7f;
}

static void set_mode(unsigned mode)
{
    union REGS r = {0};
    r.h.al = (uint8_t)mode;
    int386(0x10, &r, &r);
}

static void flush_keyboard(void)
{
    union REGS r = {0};
    r.w.ax = 0x0c00;
    int386(0x21, &r, &r);
}

static void wait_for_return(void)
{
    union REGS r = {0};
    r.h.ah = 0x0f;
    int386(0x10, &r, &r);
    unsigned mode = r.h.al & 0x7f;
    unsigned page = r.h.bh;
    /* Keep text output intact. If the child left graphics active, get a
     * visible console before asking for a key. */
    if (mode > 3 && mode != 7) {
        set_mode(3);
        page = 0;
    }
    /* BIOS output and input also work with DOS handles redirected. */
    const char *prompt = "\r\nPress any key to return to Windows.";
    for (; *prompt; ++prompt) {
        r.w.ax = 0x0e00 | (uint8_t)*prompt;
        r.w.bx = (page << 8) | 7;
        int386(0x10, &r, &r);
    }
    r.w.ax = 0x1000;                   /* consume one full keystroke */
    int386(0x16, &r, &r);
}

static UINT execute(uint16_t seg)
{
    union REGS r = {0};
    r.w.ax = 0x4b00;
    r.w.dx = offsetof(dos_exec_block, filename);
    r.w.bx = offsetof(dos_exec_block, params);
    if (!dos_call(seg, &r)) {
        win_trace_event("dos-exec-error", r.w.ax, 0);
        switch (r.w.ax) {
        case 2: case 3: return 2;
        case 5: return 5;
        case 8: return 8;
        default: return 11;
        }
    }
    r.w.ax = 0x4d00;
    int386(0x21, &r, &r);
    win_trace_event("dos-exit", r.h.al, r.h.ah);
    return 33;                         /* launched, independently of DOS ERRORLEVEL */
}

UINT win_dos_exec(const char *path, const char *args, BOOL use_exe_dir)
{
    if (strlen(path) >= DOS_PATH_BYTES || strlen(args) > 125)
        return 11;
    if (!win_sys.screen_ok)
        return 5;
    char directory[DOS_PATH_BYTES];
    int drive = task_current()->dos_drive;
    if (use_exe_dir) {
        drive = win_file_exe_dir(path, directory, sizeof(directory));
        if (drive < 0)
            return 2;
    }
    dos_directories *saved = win_alloc(sizeof(*saved));
    if (!saved)
        return 8;
    dos_exec_block *b = dos_alloc_low(sizeof(*b));
    if (!b) {
        win_free(saved);
        return 8;
    }
    uint16_t seg = dos_ptr_segment(b);
    prepare_exec(b, seg, path, args);
    save_directories(b, seg, saved);

    /* EXEC is synchronous. Leave the GUI tasks and their windows in
     * memory and give the child exclusive access to the DOS devices. */
    win_logo_end();
    BOOL input = EnableHardwareInput(FALSE);
    cursor_exclude(NULL);
    win_sys.screen_ok = 0;
    if (win_sys.input)
        win_sys.input->disable();
    if (win_sys.sound)
        win_sys.sound->disable();
    const win_display_driver *disp = win_sys.disp;
    disp->disable();
    unsigned mode = current_mode();    /* the driver's original mode */
    if (mode != 3)
        set_mode(3);

    UINT rc = apply_directories(b, seg, saved, (unsigned)drive, use_exe_dir ? directory : NULL)
              ? execute(seg) : 2;
    restore_directories(b, seg, saved);
    dos_free_low(b);
    win_free(saved);
    flush_keyboard();
    if (rc >= 32) {
        wait_for_return();
        flush_keyboard();
    }
    if (win_sys.sound) {
        win_sys.sound->disable();
        win_sys.sound->enable();
    }

    /* Re-enable from the same DOS mode as before the handoff, so the
     * driver's eventual disable() still restores the caller's mode. */
    set_mode(mode);
    if (!disp->enable(win_sys.screen.width, win_sys.screen.height, win_sys.screen.bpp)) {
        win_sys.screen = *disp->surface();
        win_sys.screen_ok = 1;
        gdi_palette_reset();
        if (win_sys.input && win_sys.input->enable(win_sys.screen.width, win_sys.screen.height))
            win_sys.input = NULL;
        user_repaint_all();
    } else {
        win_trace_event("dos-video-restore", mode, 0);
        task_quit_all();
    }
    EnableHardwareInput(input);
    cursor_unexclude();
    return rc;
}
#endif
