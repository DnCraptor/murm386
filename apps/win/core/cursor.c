/*
 * cursor.c - software mouse cursor on the screen surface.
 *
 * The cursor is painted into the framebuffer with a save-under buffer.
 * GDI calls cursor_exclude() with the area it is about to change and
 * cursor_unexclude() when done; the cursor is lifted only if it overlaps.
 * Masks are 32x32, 1 bpp, MSB = leftmost, 4 bytes per row:
 *   AND 0 XOR 0 black, AND 0 XOR 1 white, AND 1 XOR 0 screen, AND 1 XOR 1 inverse.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

static int exclude_depth;
/* A launch keeps the hourglass on screen while other USER cursor changes
 * are deferred. Nested WinExec calls share the same visible cursor. */
static unsigned wait_depth;
static const uint8_t *wait_and, *wait_xor;
static int wait_hx, wait_hy;

static void cur_rect(RECT *r)
{
    r->left = win_sys.cur_x - win_sys.cur_hot_x;
    r->top = win_sys.cur_y - win_sys.cur_hot_y;
    r->right = r->left + 32;
    r->bottom = r->top + 32;
}

static void flush(const RECT *r)
{
    RECT s = { 0, 0, win_sys.screen.width, win_sys.screen.height }, x;
    if (win_sys.disp && win_sys.disp->flush && !win_sys.screen_real && IntersectRect(&x, r, &s))
        win_sys.disp->flush(x.left, x.top, x.right, x.bottom);
}

static void cur_erase(void)
{
    if (!win_sys.cur_drawn)
        return;
    wsurf *s = &win_sys.screen;
    const RECT *r = &win_sys.cur_rect;
    for (int y = r->top; y < r->bottom; y++) {
        if (y < 0 || y >= s->height)
            continue;
        for (int x = r->left; x < r->right; x++)
            if (x >= 0 && x < s->width)
                s->bits[y * s->pitch + x] = win_sys.cur_save[(y - r->top) * 32 + (x - r->left)];
    }
    win_sys.cur_drawn = 0;
    flush(r);
}

static void cur_draw(void)
{
    if (win_sys.cur_drawn || win_sys.cur_shown < 0 || !win_sys.cur_and || !win_sys.screen_ok)
        return;
    wsurf *s = &win_sys.screen;
    RECT r;
    cur_rect(&r);
    win_sys.cur_rect = r;
    for (int y = r.top; y < r.bottom; y++) {
        int my = y - r.top;
        if (y < 0 || y >= s->height)
            continue;
        uint8_t *row = s->bits + y * s->pitch;
        for (int x = r.left; x < r.right; x++) {
            int mx = x - r.left;
            if (x < 0 || x >= s->width)
                continue;
            uint8_t d = row[x];
            win_sys.cur_save[my * 32 + mx] = d;
            int bit = 7 - (mx & 7), o = my * 4 + (mx >> 3);
            int a = (win_sys.cur_and[o] >> bit) & 1, xo = (win_sys.cur_xor[o] >> bit) & 1;
            row[x] = (uint8_t)((a ? d : 0) ^ (xo ? 0xFF : 0));
        }
    }
    win_sys.cur_drawn = 1;
    flush(&r);
}

void cursor_exclude(const RECT *rc)
{
    exclude_depth++;
    RECT x;
    if (win_sys.cur_drawn && (!rc || IntersectRect(&x, rc, &win_sys.cur_rect)))
        cur_erase();
}

void cursor_unexclude(void)
{
    if (exclude_depth > 0)
        exclude_depth--;
    if (!exclude_depth)
        cur_draw();
}

void cursor_move(int x, int y)
{
    if (x == win_sys.cur_x && y == win_sys.cur_y)
        return;
    cur_erase();
    win_sys.cur_x = x;
    win_sys.cur_y = y;
    if (!exclude_depth)
        cur_draw();
}

void cursor_set_shape(const uint8_t *and_mask, const uint8_t *xor_mask, int hx, int hy)
{
    if (wait_depth) {
        wait_and = and_mask;
        wait_xor = xor_mask;
        wait_hx = hx;
        wait_hy = hy;
        return;
    }
    if (and_mask == win_sys.cur_and && xor_mask == win_sys.cur_xor)
        return;
    cur_erase();
    win_sys.cur_and = and_mask;
    win_sys.cur_xor = xor_mask;
    win_sys.cur_hot_x = hx;
    win_sys.cur_hot_y = hy;
    if (!exclude_depth)
        cur_draw();
}

void cursor_wait_begin(void)
{
    if (wait_depth) {
        ++wait_depth;
        return;
    }
    wait_and = win_sys.cur_and;
    wait_xor = win_sys.cur_xor;
    wait_hx = win_sys.cur_hot_x;
    wait_hy = win_sys.cur_hot_y;
    cursor_select_builtin(WCUR_WAIT);
    wait_depth = 1;
}

void cursor_wait_end(void)
{
    if (!wait_depth || --wait_depth)
        return;
    cursor_set_shape(wait_and, wait_xor, wait_hx, wait_hy);
}

/* ShowCursor semantics: counter, visible while >= 0. */
int cursor_show(int show)
{
    win_sys.cur_shown += show ? 1 : -1;
    if (win_sys.cur_shown < 0)
        cur_erase();
    else if (!exclude_depth)
        cur_draw();
    return win_sys.cur_shown;
}

/* ---------------------------------------------------------------- shapes */

/* '.' screen, 'X' black, 'O' white, '#' inverse; rows up to 32 chars. */
typedef struct {
    int hx, hy;
    const char *rows[24];
} cursor_art;

static const cursor_art art_arrow = { 0, 0, {
    "X",
    "XX",
    "XOX",
    "XOOX",
    "XOOOX",
    "XOOOOX",
    "XOOOOOX",
    "XOOOOOOX",
    "XOOOOOOOX",
    "XOOOOOOOOX",
    "XOOOOOXXXXX",
    "XOOXOOX",
    "XOX.XOOX",
    "XX..XOOX",
    "X....XOOX",
    ".....XOOX",
    "......XOOX",
    "......XOOX",
    ".......XX",
    NULL } };

static const cursor_art art_ibeam = { 4, 8, {
    "XXX.XXX",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "...X",
    "XXX.XXX",
    NULL } };

static const cursor_art art_wait = { 7, 11, {
    "XXXXXXXXXXXXXXX",
    "XOOOOOOOOOOOOOX",
    "XXXXXXXXXXXXXXX",
    ".XOOOOOOOOOOOX",
    ".XOOXOXOXOXOOX",
    "..XOOXOXOXOOX",
    "...XOOXOXOOX",
    "....XOOXOOX",
    ".....XOOOX",
    "......XOX",
    "......XOX",
    ".....XOOOX",
    "....XOOOOOX",
    "...XOOOXOOOX",
    "..XOOOOOOOOOX",
    ".XOOOOXOXOOOOX",
    ".XOOOXOXOXOOOX",
    ".XOOXOXOXOXOOX",
    "XXXXXXXXXXXXXXX",
    "XOOOOOOOOOOOOOX",
    "XXXXXXXXXXXXXXX",
    NULL } };

static const cursor_art art_cross = { 7, 7, {
    ".......#",
    ".......#",
    ".......#",
    ".......#",
    ".......#",
    ".......#",
    "",
    "######.#.######",
    "",
    ".......#",
    ".......#",
    ".......#",
    ".......#",
    ".......#",
    ".......#",
    NULL } };

static const cursor_art art_size_we = { 10, 4, {
    "...X.......X",
    "..XX.......XX",
    ".XOXXXXXXXXXOX",
    "XOOOOOOOOOOOOOX",
    "XOOOOOOOOOOOOOX",
    ".XOXXXXXXXXXOX",
    "..XX.......XX",
    "...X.......X",
    NULL } };

static const cursor_art art_size_ns = { 4, 7, {
    "...XX",
    "..XOOX",
    ".XOOOOX",
    "XXXOOXXX",
    "...XOX",
    "...XOX",
    "...XOX",
    "...XOX",
    "...XOX",
    "...XOX",
    "XXXOOXXX",
    ".XOOOOX",
    "..XOOX",
    "...XX",
    NULL } };

typedef struct {
    uint8_t and_m[128], xor_m[128];
    int hx, hy, ready;
} cursor_bits;

static cursor_bits shapes[WCUR_COUNT];

static const cursor_art *const arts[WCUR_COUNT] = {
    &art_arrow, &art_ibeam, &art_wait, &art_cross, &art_size_we, &art_size_ns,
};

static void build(cursor_bits *b, const cursor_art *a)
{
    memset(b->and_m, 0xFF, sizeof(b->and_m));
    memset(b->xor_m, 0x00, sizeof(b->xor_m));
    for (int y = 0; y < 24 && a->rows[y]; y++) {
        const char *r = a->rows[y];
        for (int x = 0; x < 32 && r[x]; x++) {
            int o = y * 4 + (x >> 3);
            uint8_t m = (uint8_t)(0x80 >> (x & 7));
            switch (r[x]) {
            case 'X': b->and_m[o] &= (uint8_t)~m; break;
            case 'O': b->and_m[o] &= (uint8_t)~m; b->xor_m[o] |= m; break;
            case '#': b->xor_m[o] |= m; break;
            default: break;
            }
        }
    }
    b->hx = a->hx;
    b->hy = a->hy;
    b->ready = 1;
}

void cursor_select_builtin(int id)
{
    if (id < 0 || id >= WCUR_COUNT)
        id = WCUR_ARROW;
    cursor_bits *b = &shapes[id];
    if (!b->ready)
        build(b, arts[id]);
    cursor_set_shape(b->and_m, b->xor_m, b->hx, b->hy);
}

void cursor_init(void)
{
    exclude_depth = 0;
    wait_depth = 0;
    win_sys.cur_drawn = 0;
    win_sys.cur_shown = 0;
    win_sys.cur_x = win_sys.screen.width / 2;
    win_sys.cur_y = win_sys.screen.height / 2;
    cursor_select_builtin(WCUR_ARROW);
}
