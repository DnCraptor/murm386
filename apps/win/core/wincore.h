/*
 * wincore.h - internal structures of the native Windows core.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef MURM_WINCORE_H
#define MURM_WINCORE_H

#include "windows.h"
#include "../ddi/winddi.h"
#include "../ddi/winmod.h"

/* ---------------------------------------------------------------- handles */

enum {
    WOBJ_FREE = 0,
    WOBJ_PEN = OBJ_PEN,          /* 1 */
    WOBJ_BRUSH = OBJ_BRUSH,      /* 2 */
    WOBJ_FONT,
    WOBJ_BITMAP,
    WOBJ_REGION,
    WOBJ_PALETTE,
    WOBJ_DC,
    WOBJ_WINDOW,
    WOBJ_GLOBAL,
    WOBJ_LOCAL,
    WOBJ_CURSOR,
    WOBJ_MENU,
    WOBJ_CLASS,
    WOBJ_ACCEL,
    WOBJ_TASK,
    WOBJ_METAFILE,
    WOBJ_MODULE,                 /* a native DLL (windll.c) */
};

HANDLE win_handle_alloc(int type, void *obj);
void  *win_handle_get(HANDLE h, int type);   /* NULL if h is not that type */
int    win_handle_type(HANDLE h);
void   win_handle_free(HANDLE h);
/* Stock objects are never freed by DeleteObject. */
void   win_handle_set_stock(HANDLE h);
int    win_handle_is_stock(HANDLE h);

void  *win_alloc(size_t n);    /* zeroed */
void  *win_realloc(void *p, size_t n);
void   win_free(void *p);
char  *win_strdup(const char *s);

/* ---------------------------------------------------------------- regions */

/* A region is a list of disjoint rectangles plus their bounding box. */
typedef struct wrgn {
    int   n, cap;
    RECT *r;
    RECT  box;
} wrgn;

void wrgn_init(wrgn *g);
void wrgn_clear(wrgn *g);                         /* empty, keeps storage */
void wrgn_free(wrgn *g);
void wrgn_set_rect(wrgn *g, const RECT *rc);
void wrgn_copy(wrgn *d, const wrgn *s);
void wrgn_and(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_or(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_diff(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_xor(wrgn *d, const wrgn *a, const wrgn *b);
void wrgn_and_rect(wrgn *g, const RECT *rc);      /* in place */
void wrgn_diff_rect(wrgn *g, const RECT *rc);     /* in place */
void wrgn_or_rect(wrgn *g, const RECT *rc);       /* in place */
void wrgn_add(wrgn *g, const RECT *rc);           /* one not touching the others */
void wrgn_done(wrgn *g);                          /* after the adds */
void wrgn_offset(wrgn *g, int dx, int dy);
int  wrgn_type(const wrgn *g);
int  wrgn_pt_in(const wrgn *g, int x, int y);
int  wrgn_rect_in(const wrgn *g, const RECT *rc);
int  wrgn_equal(const wrgn *a, const wrgn *b);

/* ---------------------------------------------------------------- raster */

typedef win_surface wsurf;

/* What fills an area: a brush realized for one DC. */
enum { WFILL_NONE, WFILL_SOLID, WFILL_PATTERN };
typedef struct wfill {
    int     kind;
    uint8_t solid;          /* WFILL_SOLID: palette index */
    uint8_t pat[64];        /* WFILL_PATTERN: 8x8 indices */
    uint8_t pat_mask[64];   /* 1 = pixel drawn (hatch with TRANSPARENT bk) */
    int     masked;
    int     org_x, org_y;   /* brush origin, device coordinates */
} wfill;

/* All raster operations clip against a list of device rectangles. */
typedef struct wclip {
    const RECT *r;
    int         n;
} wclip;

/* rop2: R2_* ; rop3: the ROP3 index byte (high word of a raster op). */
int  dib_rop2_to_rop3(int rop2);          /* pen/brush as P, ignores S */
void dib_fill_rect(wsurf *s, const wclip *c, const RECT *rc, const wfill *f, int rop3);
void dib_set_pixel(wsurf *s, const wclip *c, int x, int y, uint8_t color, int rop2);
uint8_t dib_get_pixel(const wsurf *s, int x, int y);
void dib_hline(wsurf *s, const wclip *c, int x0, int x1, int y, uint8_t color, int rop2);
void dib_line(wsurf *s, const wclip *c, int x0, int y0, int x1, int y1,
              uint8_t color, int rop2, int style, int skip_last);
/* src may equal dst (overlap handled). For 1 <-> 8 bpp conversions:
 * mono->color: 1 bits become bk, 0 bits become fg (Windows rule);
 * color->mono: pixels equal to bk become 1. */
void dib_blit(wsurf *d, const wclip *c, int dx, int dy, int w, int h,
              const wsurf *s, int sx, int sy, const wfill *pat, int rop3,
              uint8_t fg, uint8_t bk);
void dib_stretch(wsurf *d, const wclip *c, int dx, int dy, int dw, int dh,
                 const wsurf *s, int sx, int sy, int sw, int sh,
                 const wfill *pat, int rop3, uint8_t fg, uint8_t bk);
/* Draw the 1 bits of a glyph (LSB = leftmost, one byte per row, width<=8)
 * in color; bold adds a second pass one pixel to the right. */
void dib_glyph8(wsurf *s, const wclip *c, int x, int y, const uint8_t *rows,
                int h, int w, int skip_left, uint8_t color, int bold);
/* ... any glyph: w x h, rows of pitch bytes (bit 7 the leftmost), each
 * pixel scale x scale; base > y: italic, slanted about that line */
void dib_glyph(wsurf *s, const wclip *c, int x, int y, const uint8_t *rows, int pitch,
               int w, int h, int scale, int base, uint8_t color);

/* ---------------------------------------------------------------- GDI */

typedef struct wpen {
    int      style, width;
    COLORREF color;
} wpen;

enum { WBR_NULL, WBR_SOLID, WBR_HATCH, WBR_PATTERN };
typedef struct wbrush {
    int      style;
    COLORREF color;
    int      hatch;
    HBITMAP  pattern;       /* WBR_PATTERN (copied 8x8) */
    uint8_t  pat8[64];      /* pattern indices (8bpp source) */
    uint8_t  patmono[8];    /* pattern rows (1bpp source), LSB unused */
    int      pat_is_mono;
} wbrush;

/* a font realized at one height on the device (gdi_text.c): a face of
 * the font files' (fontfile.h) or the VGA glyphs (face NULL), made larger
 * by a whole number, bold and italic made if the face is not */
typedef struct wreal {
    struct wface *face;
    int      key;           /* the height asked for, in pixels (lfHeight's sign) */
    int      scale, bold, italic, fixed;
    int      height, ascent, ileading, eleading, avgw, maxw;
} wreal;

typedef struct wfont {
    LOGFONT  lf;
    int      fixed;         /* fixed pitch (the VGA glyphs) */
    int      bold;
    int      height;        /* realized cell height (the VGA glyphs) */
    wreal    real;          /* the last realization */
} wfont;

typedef struct wbitmap {
    wsurf    s;
    int      selected;      /* selected into a memory DC */
    int      dim_x, dim_y;  /* SetBitmapDimension's (0.1 mm) */
} wbitmap;

/* Bitmap row size as Win16 reports it (rows padded to 16 bits). */
int      gdi_bitmap_pitch(int w, int bpp);

typedef struct wpalette {
    int          n;
    PALETTEENTRY e[256];
    uint8_t      map[256];  /* logical index -> system palette index */
    int          realized;
} wpalette;

struct wwnd;

typedef struct wdc {
    wsurf   *surf;          /* screen surface or bitmap surface */
    int      is_screen;
    HBITMAP  bitmap;        /* memory DC */
    HWND     hwnd;          /* window DC (USER) */
    int      org_x, org_y;  /* device origin inside the surface */
    int      vp_x, vp_y, win_x, win_y;
    int      mapmode;       /* MM_*: logical = (device - vp) * win ext / vp ext + win */
    int      wex, wey, vex, vey;
    wrgn     vis;           /* visible area, surface coordinates (USER) */
    int      has_clip;
    wrgn     clip;          /* app clip region, surface coordinates */
    wrgn     eff;           /* vis & clip, rebuilt when dirty */
    int      eff_dirty;
    HPEN     pen;
    HBRUSH   brush;
    HFONT    font;
    HPALETTE pal;
    COLORREF text, bk;
    int      bkmode, rop2, polyfill;
    int      stretch;       /* SetStretchBltMode's (noted) */
    int      char_extra;    /* SetTextCharacterExtra's: added to each character */
    int      break_extra, break_count;  /* SetTextJustification's (noted) */
    UINT     align;
    int      brush_x, brush_y;
    POINT    cur;
    int      save_level;
    struct wdc *saved;
    struct wmrec *meta;     /* CreateMetaFile's: what it records (metafile.c) */
} wdc;

/* Screen and system state shared by GDI and USER. */
typedef struct wsys {
    const win_display_driver *disp;
    const win_input_driver   *input;
    wsurf     screen;
    int       screen_ok;
    uint8_t  *screen_real;      /* the logo is up: drawing goes to a copy, screen.bits */
    PALETTEENTRY syspal[256];
    HGDIOBJ   stock[17];
    /* software cursor */
    int       cur_x, cur_y, cur_hot_x, cur_hot_y;
    int       cur_shown;        /* ShowCursor counter >= 0 */
    int       cur_drawn;        /* on screen now */
    int       cur_excluded;
    const uint8_t *cur_and, *cur_xor;   /* 32x32 1bpp masks, MSB left */
    uint8_t   cur_save[32 * 32];
    RECT      cur_rect;
} wsys;

extern wsys win_sys;

/* winsys.c */
int      win_init(const char *display, const char *input, int want_w, int want_h, int want_bpp);
void     win_shutdown(void);
int      win_boot(const char *exe_path, const win_display_driver *const *disp,
                  const win_input_driver *in);
int      user_init(void);                        /* user/dialog.c */

/* profile.c */
void     win_set_home(const char *exe_path);
void     win_path(const char *name, char *out, int size);
/* profile.c: the Windows directory's file, or the system one's if only it
 * has it */
void     win_find_path(const char *name, char *out, int size);

/* logo.c */
void     win_logo_skip(void);
void     win_logo_show(void);
void     win_logo_end(void);

/* gdi.c */
int      gdi_init(void);
void     gdi_palette_reset(void);
void     gdi_shutdown(void);
uint8_t  gdi_color_index(wdc *dc, COLORREF c);
wdc     *gdi_dc(HDC h);
HDC      gdi_create_screen_dc(HWND hwnd);
void     gdi_reset_dc(HDC h);                           /* GetDC defaults */
void     gdi_set_dc_origin(HDC h, int x, int y);
void     gdi_set_dc_vis(HDC h, const wrgn *vis);          /* surface coords */
const wclip *gdi_dc_clip(wdc *dc, wclip *tmp);
void     gdi_lp_to_dp(wdc *dc, int *x, int *y);
void     gdi_realize_brush(wdc *dc, HBRUSH br, wfill *f);
wrgn    *gdi_region(HRGN h);
void     gdi_begin_draw(wdc *dc, const RECT *dev_bounds);   /* cursor exclusion */
void     gdi_end_draw(wdc *dc, const RECT *dev_bounds);     /* flush, cursor */
/* USER installs the icon overlay; NULL in standalone GDI tests. */
extern void (*gdi_overlay_hook)(HWND source, const RECT *damage);
void     gdi_text_init(void);
const wfont *gdi_font(HFONT h);
/* the mapping mode's scale for sizes: device -> logical, logical -> device
 * (signed: an axis may turn over) */
int      gdi_dx_to_lx(const wdc *dc, int dx);
int      gdi_dy_to_ly(const wdc *dc, int dy);
int      gdi_lx_to_dx(const wdc *dc, int lx);
int      gdi_ly_to_dy(const wdc *dc, int ly);
int      gdi_char_width(const wfont *f, unsigned char ch);
BOOL     gdi_font_filed(HFONT h);               /* a font file's face, not the VGA glyphs */

/* metafile.c: a metafile DC's records. mf_rec: n words as the function
 * has its arguments (a DWORD as MF_DW's two), kept in the record's order
 * (the last argument first); TRUE once recorded */
BOOL     mf_rec(wdc *dc, int fn, int n, ...);
#define  MF_DW(v) (int)((DWORD)(v) >> 16), (int)((DWORD)(v) & 0xffffu)
#define  MF_REC(dc, ...) do { if ((dc)->meta) mf_rec(dc, __VA_ARGS__); } while (0)
void     mf_select(wdc *dc, HGDIOBJ obj);       /* SELECTOBJECT, its object made */
int      mf_object(wdc *dc, HGDIOBJ obj);       /* its index; -1 */
void     mf_forget(HGDIOBJ obj);                /* DeleteObject: DELETEOBJECT */
void     mf_dc_gone(wdc *dc);                   /* DeleteDC of one recording */
BOOL     mf_text(wdc *dc, int x, int y, UINT opts, const RECT *rc, LPCSTR s, int n,
                 const int *dx, int ext);
BOOL     mf_points(wdc *dc, const POINT *pts, const int *counts, int npoly, int fn);
BOOL     mf_dib(wdc *dc, int fn, const int *words, int n, const BITMAPINFO *bi,
                const void *bits, UINT usage, int lines);
BOOL     mf_blt(wdc *dc, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh,
                DWORD rop, int stretch);
BOOL     mf_region_op(wdc *dc, int fn, HRGN rgn, HBRUSH br, int w, int h);
/* file.c: a file of size bytes; a whole file's bytes (win_alloc's, at most
 * max), NULL if not */
BOOL     win_file_save(const char *path, const void *p, uint32_t size);
int      win_file_codepage(int set, UINT *active, UINT *system);    /* DOS's (66h) */
uint8_t *win_file_load(const char *path, uint32_t *size, uint32_t max);
/* a metafile's bits (its header first), and one of a copy of them */
const uint8_t *mf_bits(HMETAFILE mf, uint32_t *size);
HMETAFILE mf_from_bits(const uint8_t *p, uint32_t size);
/* EnumMetaFile's way: each record to fn (0: stop), the table of the
 * objects the records made; mf_play_record plays one */
typedef int (*mf_enum_fn)(void *ctx, HDC h, HGDIOBJ *table, int n, const uint8_t *rec,
                          uint32_t bytes);
BOOL     mf_enum(HDC h, HMETAFILE mf, mf_enum_fn fn, void *ctx);
void     mf_play_record(HDC h, HGDIOBJ *table, int n, const uint8_t *rec, uint32_t bytes);

/* cursor.c */
void     cursor_init(void);
void     cursor_move(int x, int y);
void     cursor_exclude(const RECT *rc);
void     cursor_unexclude(void);
void     cursor_set_shape(const uint8_t *and_mask, const uint8_t *xor_mask, int hx, int hy);
int      cursor_show(int show);                 /* ShowCursor counter */
void     cursor_wait_begin(void);              /* nested WinExec loading */
void     cursor_wait_end(void);
enum { WCUR_ARROW, WCUR_IBEAM, WCUR_WAIT, WCUR_CROSS, WCUR_SIZEWE, WCUR_SIZENS, WCUR_COUNT };
void     cursor_select_builtin(int id);
/* icon.c: an icon (32x32, 16 colours) and a cursor (32x32 masks) made from
 * bits, freed with DestroyIcon */
HICON    user_icon_create(const winapp_icon *ic);
HCURSOR  user_cursor_create(const uint8_t and_m[128], const uint8_t xor_m[128], int hx, int hy);
/* a built-in cursor set as SetCursor sets it: GetCursor, SetCursor's old
 * one are then its handle */
void     user_cursor_builtin(int id);

/* gdi.c: device-independent bitmaps (Win16 resources) */
typedef struct {
    const uint8_t *pal, *bits;
    int w, h, bpp, pitch, colors, entry, top_down;
} gdi_dib;
int      gdi_dib_parse(const uint8_t *d, uint32_t n, int halves, gdi_dib *out);
uint32_t gdi_dib_value(const gdi_dib *b, int half, int x, int y);
COLORREF gdi_dib_pixel(const gdi_dib *b, int x, int y);
HBITMAP  gdi_bitmap_from_dib(const uint8_t *d, uint32_t n);

/* ne_res.c: a Win16 program's (file's) icons and description, as an EZ
 * application's; its icon, cursor and bitmap resources by number or name */
winapp_info *win_ne_app_info(const char *path);
HICON    win_ne_icon(const char *path, uint16_t id, const char *name);
HCURSOR  win_ne_cursor(const char *path, uint16_t id, const char *name);
HBITMAP  win_ne_bitmap(const char *path, uint16_t id, const char *name);

/* ---------------------------------------------------------------- tasks (task.c) */

enum { WTASK_NEW, WTASK_RUNNING, WTASK_DEAD };
/* The kernel's normal scheduler wait must not wake for GUI messages;
 * a modal GetMessage on the kernel stack must. */
enum { WTASK_WAIT_NONE = 0, WTASK_WAIT_IDLE = 1, WTASK_WAIT_MESSAGE = 2 };

struct wtask_ctx;

typedef struct wtask {
    struct wtask *next;
    HTASK     h;
    int       state;
    int       waiting;          /* WTASK_WAIT_*; kernel idle vs modal input */
    int       quit, quit_code;  /* PostQuitMessage */
    int       exit_code;
    char      name[16];
    char      cmdline[128];
    int       show;
    uint8_t  *stack;
    uint32_t  stack_size;
    void     *module;           /* EZ image memory */
    winapp_info *info;          /* its name and icons, or NULL */
    HICON     icons[WINAPP_MAX_ICONS];   /* made from info by LoadIcon */
    win_app_entry entry;
    win_app_host  app;
    int     (*run)(struct wtask *);
    struct wtask_ctx *ctx;
    struct wtask *dispatch_owner;  /* logical owner during cross-task SendMessage */
    int       starting;         /* WinExec's hourglass is its until it first waits */
} wtask;

void   task_init(void);
wtask *task_current(void);
wtask *task_set_dispatch_owner(wtask *owner); /* returns previous owner */
wtask *task_kernel(void);
HTASK  task_next(HTASK h);                  /* 0: the first; 0 after the last */
wtask *task_from_handle(HTASK h);
wtask *task_create(const char *name, int (*run)(wtask *), uint32_t stack_size);
int    task_yield(void);                 /* 1 if another task ran */
void   task_run_now(wtask *t);
void   task_kernel_loop(void);
void   task_quit_all(void);              /* WM_QUIT for every application */
void   task_started(wtask *t);           /* it waits for input: its hourglass ends */

/* user/msg.c */
int    user_task_has_work(HTASK h);
void   user_task_ended(HTASK h);
void   user_poll_input(void);

/* module.c: an EZ image loaded and relocated */
typedef struct {
    void     *raw;              /* allocation to free */
    uint8_t  *base;
    void     *entry;
    uint32_t  image_size;       /* image memory size, for entry bounds */
    uint32_t  stack_size;       /* from the EZ header, 0 = default */
} win_ez_image;
int    win_ez_load(const char *path, win_ez_image *img);
winapp_info *win_ez_read_info(const char *path);        /* win_alloc'd or NULL */
winapp_info *win_app_info(const char *path);            /* task.c: also the host's */
/* the SDK's application entry starts with a branch over WINAPP_MAGIC */
int    win_ez_entry_is_app(const win_ez_image *img);
int    win_ez_is_app(const char *path);    /* the file, without loading it */

/* windll.c: native DLLs (ddi/windll.h). A slot while loaded; the
 * current task's uses of one, all of a task's let go at its end */
#define WIN_DLL_SLOTS 12
int    win_dll_load(const char *name);   /* its slot, used once more; -1 none */
void   win_dll_unref(int slot);
int    win_dll_find(const char *name);   /* loaded: its slot; -1 */
int    win_dll_use(const char *name);    /* ... used by this task (once, until its end) */
/* a DLL's registered message to a Win16 window: the program's lParam, 0 */
uint32_t win_dll_ne_lparam(uint32_t msg, uint32_t lparam);
int    win_dll_of(HANDLE h);             /* a module handle's slot; -1 */
HANDLE win_dll_handle(int slot);
const char *win_dll_path(int slot);
const struct win_dll_info *win_dll_get(int slot);
const struct win_dll_export *win_dll_export_of(int slot, uint16_t ordinal, const char *name);
#define WIN_DLL_EXPORTS 1024            /* a DLL's exports: fewer */
const struct win_dll_export *win_dll_export_at(int slot, unsigned n);
void   win_dll_task_end(HTASK task);

/* gdi.c: set by USER, for FillRect(dc, rc, (HBRUSH)(COLOR_xxx + 1)) */
extern HBRUSH (*gdi_sys_color_brush)(int index);

/* file.c */
void   win_files_task_ended(HTASK task);
void   win_files_init(void);
/* file.c: DOS's functions for Win16 programs (INT 21h 3Bh, 19h/0Eh, 43h,
 * 57h; the environment) */
BOOL   win_file_chdir(LPCSTR path);
int    win_file_drive(int set);
int    win_file_attr(LPCSTR path, int set, UINT attr);
BOOL   win_file_time(HFILE f, int set, WORD *date, WORD *time_);
int    win_file_environment(char *buf, int size);
/* gdi_text.c: a character between Windows-1251 (ANSI) and CP866 (OEM) */
uint8_t win_ansi_to_oem(uint8_t c);
uint8_t win_oem_to_ansi(uint8_t c);

#if !(defined(__arm__) || defined(__thumb__))
/* host tests: applications built into the harness, by file name */
win_app_entry win_host_app(const char *prog);
#endif

/* exports.c (generated) */
extern const void *const *const win_export_tables[WINMOD_COUNT];

#endif /* MURM_WINCORE_H */
