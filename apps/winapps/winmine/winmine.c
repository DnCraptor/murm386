/*
 * winmine.c - WINMINE.EXE: Minesweeper, as in Windows 3.1.
 *
 * Left button opens a square, right button marks it (flag, then "?" when
 * Marks are on); both buttons together, or the middle one, on an opened
 * number open the squares around it when enough of them are flagged. The
 * first square opened is never a mine. The face starts a new game (F2 too).
 * Levels, the window's place and the best times live in WINMINE.INI.
 */
#include <string.h>
#include "windows.h"

#define INI       "WINMINE.INI"
#define CELL      16
#define MAXROWS   24
#define MAXCOLS   30
#define MARGIN    9
#define HEAD_H    37
#define LED_W     41
#define LED_H     25
#define FACE      26

#define IDM_NEW     100
#define IDM_BEGIN   101
#define IDM_INTER   102
#define IDM_EXPERT  103
#define IDM_CUSTOM  104
#define IDM_MARKS   105
#define IDM_BEST    106
#define IDM_EXIT    107
#define IDM_ABOUT   110

/* a square: the mine count around it, or these */
#define C_MINE    0x80
#define C_OPEN    0x40
#define C_FLAG    0x10
#define C_QUEST   0x20
#define C_COUNT   0x0F

enum { PLAYING, WON, LOST };
enum { FACE_SMILE, FACE_OH, FACE_DEAD, FACE_COOL };

static HINSTANCE hinst;
static HWND      hwnd;
static BYTE      board[MAXROWS][MAXCOLS];
static int       rows = 9, cols = 9, mines = 10, level;   /* level 0..2, 3 custom */
static int       marks = 1;
static int       state, started, opened, flags, seconds;
static int       exploded_r = -1, exploded_c = -1;
static int       down_l, down_r, chord, face_down, press_r = -1, press_c = -1;
static int       face = FACE_SMILE;
static DWORD     seed;

static const struct { int r, c, m; } levels[3] = { { 9, 9, 10 }, { 16, 16, 40 }, { 16, 30, 99 } };
static const char *const level_names[3] = { "Beginner", "Intermediate", "Expert" };

/* ---------------------------------------------------------------- geometry */

static int client_w(void) { return 2 * MARGIN + 6 + cols * CELL; }
static int client_h(void) { return MARGIN + HEAD_H + 6 + 6 + rows * CELL + MARGIN; }
static int board_x(void) { return MARGIN + 3; }
static int board_y(void) { return MARGIN + HEAD_H + 6 + 3; }

static void face_rect(RECT *r)
{
    int x = (client_w() - FACE) / 2, y = MARGIN + (HEAD_H - FACE) / 2;
    SetRect(r, x, y, x + FACE, y + FACE);
}

static void cell_rect(int r, int c, RECT *rc)
{
    int x = board_x() + c * CELL, y = board_y() + r * CELL;
    SetRect(rc, x, y, x + CELL, y + CELL);
}

static int cell_at(int x, int y, int *r, int *c)
{
    x -= board_x();
    y -= board_y();
    if (x < 0 || y < 0)
        return 0;
    *c = x / CELL;
    *r = y / CELL;
    return *r < rows && *c < cols;
}

/* ---------------------------------------------------------------- drawing */

static void fill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    RECT rc = { l, t, r, b };
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

/* a 3D edge of width w: light top-left, dark bottom-right (or the other way) */
static void edge(HDC dc, int l, int t, int r, int b, int w, int raised)
{
    COLORREF hi = RGB(255, 255, 255), lo = RGB(128, 128, 128);
    COLORREF tl = raised ? hi : lo, br = raised ? lo : hi;
    for (int i = 0; i < w; i++) {
        fill(dc, l + i, t + i, r - i, t + i + 1, tl);
        fill(dc, l + i, t + i, l + i + 1, b - i, tl);
        fill(dc, l + i + 1, b - i - 1, r - i, b - i, br);
        fill(dc, r - i - 1, t + i + 1, r - i, b - i, br);
    }
}

static void poly(HDC dc, const POINT *p, int n, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    Polygon(dc, p, n);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void ellipse(HDC dc, int l, int t, int r, int b, COLORREF fillc, COLORREF line)
{
    HBRUSH br = CreateSolidBrush(fillc);
    HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    Ellipse(dc, l, t, r, b);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void lines(HDC dc, const POINT *p, int n, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveTo(dc, p[0].x, p[0].y);
    for (int i = 1; i < n; i++)
        LineTo(dc, p[i].x, p[i].y);
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void draw_mine(HDC dc, int x, int y)
{
    static const POINT spikes[4][2] = {
        { { 2, 8 }, { 15, 8 } }, { { 8, 2 }, { 8, 15 } }, { { 4, 4 }, { 13, 13 } }, { { 12, 4 }, { 3, 13 } },
    };
    for (int i = 0; i < 4; i++) {
        POINT p[2] = { { x + spikes[i][0].x, y + spikes[i][0].y }, { x + spikes[i][1].x, y + spikes[i][1].y } };
        lines(dc, p, 2, RGB(0, 0, 0));
    }
    ellipse(dc, x + 4, y + 4, x + 13, y + 13, RGB(0, 0, 0), RGB(0, 0, 0));
    fill(dc, x + 6, y + 6, x + 8, y + 8, RGB(255, 255, 255));
}

static void draw_flag(HDC dc, int x, int y)
{
    POINT tri[3] = { { x + 9, y + 3 }, { x + 9, y + 9 }, { x + 4, y + 6 } };
    poly(dc, tri, 3, RGB(255, 0, 0));
    fill(dc, x + 9, y + 3, x + 10, y + 12, RGB(0, 0, 0));
    fill(dc, x + 7, y + 11, x + 11, y + 12, RGB(0, 0, 0));
    fill(dc, x + 5, y + 12, x + 13, y + 14, RGB(0, 0, 0));
}

static void draw_text(HDC dc, int x, int y, const char *s, COLORREF c)
{
    RECT r = { x, y, x + CELL, y + CELL };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawText(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_cell(HDC dc, int r, int c)
{
    static const COLORREF numc[9] = {
        0, RGB(0, 0, 255), RGB(0, 128, 0), RGB(255, 0, 0), RGB(0, 0, 128),
        RGB(128, 0, 0), RGB(0, 128, 128), RGB(0, 0, 0), RGB(128, 128, 128),
    };
    RECT rc;
    cell_rect(r, c, &rc);
    int x = rc.left, y = rc.top;
    BYTE v = board[r][c];
    int pressed = !(v & (C_OPEN | C_FLAG)) && state == PLAYING &&
                  ((chord && r >= press_r - 1 && r <= press_r + 1 && c >= press_c - 1 && c <= press_c + 1) ||
                   (!chord && r == press_r && c == press_c));
    int show = (v & C_OPEN) || (state == LOST && ((v & C_MINE) || (v & C_FLAG)));
    if (!show && !pressed) {
        fill(dc, x, y, x + CELL, y + CELL, RGB(192, 192, 192));
        edge(dc, x, y, x + CELL, y + CELL, 2, 1);
        if (v & C_FLAG)
            draw_flag(dc, x, y);
        else if (v & C_QUEST)
            draw_text(dc, x, y, "?", RGB(0, 0, 0));
        return;
    }
    int boom = r == exploded_r && c == exploded_c;
    fill(dc, x, y, x + CELL, y + CELL, boom ? RGB(255, 0, 0) : RGB(192, 192, 192));
    fill(dc, x, y, x + CELL, y + 1, RGB(128, 128, 128));
    fill(dc, x, y, x + 1, y + CELL, RGB(128, 128, 128));
    if (pressed && !show) {
        if (v & C_QUEST)
            draw_text(dc, x + 1, y + 1, "?", RGB(0, 0, 0));
        return;
    }
    if (state == LOST && !(v & C_OPEN)) {
        if ((v & C_FLAG) && !(v & C_MINE)) {        /* a wrong flag */
            draw_mine(dc, x, y);
            POINT a[2] = { { x + 3, y + 3 }, { x + 13, y + 13 } }, b[2] = { { x + 13, y + 3 }, { x + 3, y + 13 } };
            lines(dc, a, 2, RGB(255, 0, 0));
            lines(dc, b, 2, RGB(255, 0, 0));
        } else if (v & C_FLAG) {
            fill(dc, x, y, x + CELL, y + CELL, RGB(192, 192, 192));
            edge(dc, x, y, x + CELL, y + CELL, 2, 1);
            draw_flag(dc, x, y);
        } else {
            draw_mine(dc, x, y);
        }
        return;
    }
    if (v & C_MINE) {
        draw_mine(dc, x, y);
        return;
    }
    int n = v & C_COUNT;
    if (n) {
        char s[2] = { (char)('0' + n), 0 };
        draw_text(dc, x + 1, y + 1, s, numc[n]);
        draw_text(dc, x + 2, y + 1, s, numc[n]);    /* bold */
    }
}

/* a seven-segment digit; d < 0 is the minus sign, d > 9 blank */
static void draw_digit(HDC dc, int x, int y, int d)
{
    static const BYTE segs[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    BYTE on = d < 0 ? 0x40 : d > 9 ? 0 : segs[d];
    /* a, b, c, d, e, f, g as rectangles in a 13 x 23 cell */
    static const RECT r[7] = {
        { 2, 1, 11, 3 }, { 10, 2, 12, 11 }, { 10, 12, 12, 21 }, { 2, 20, 11, 22 },
        { 1, 12, 3, 21 }, { 1, 2, 3, 11 }, { 2, 10, 11, 12 },
    };
    for (int i = 0; i < 7; i++)
        fill(dc, x + r[i].left, y + r[i].top, x + r[i].right, y + r[i].bottom,
             (on >> i) & 1 ? RGB(255, 0, 0) : RGB(0, 0, 0));
}

static void draw_led(HDC dc, int x, int v)
{
    int y = MARGIN + (HEAD_H - LED_H) / 2;
    edge(dc, x, y, x + LED_W, y + LED_H, 1, 0);
    fill(dc, x + 1, y + 1, x + LED_W - 1, y + LED_H - 1, RGB(0, 0, 0));
    if (v > 999) v = 999;
    if (v < -99) v = -99;
    int neg = v < 0, a = neg ? -v : v;
    int d0 = neg ? -1 : a / 100, d1 = (a / 10) % 10, d2 = a % 10;
    draw_digit(dc, x + 1, y + 1, d0);
    draw_digit(dc, x + 14, y + 1, d1);
    draw_digit(dc, x + 27, y + 1, d2);
}

static void draw_face(HDC dc)
{
    RECT f;
    face_rect(&f);
    int x = f.left, y = f.top;
    fill(dc, x, y, f.right, f.bottom, RGB(192, 192, 192));
    fill(dc, x, y, f.right, y + 1, RGB(128, 128, 128));
    fill(dc, x, y, x + 1, f.bottom, RGB(128, 128, 128));
    fill(dc, x, f.bottom - 1, f.right, f.bottom, RGB(128, 128, 128));
    fill(dc, f.right - 1, y, f.right, f.bottom, RGB(128, 128, 128));
    if (face_down)
        edge(dc, x + 1, y + 1, f.right - 1, f.bottom - 1, 1, 0);
    else
        edge(dc, x + 1, y + 1, f.right - 1, f.bottom - 1, 2, 1);
    int o = face_down ? 1 : 0;
    int cx = x + 13 + o, cy = y + 13 + o;
    ellipse(dc, cx - 8, cy - 8, cx + 9, cy + 9, RGB(255, 255, 0), RGB(0, 0, 0));
    COLORREF k = RGB(0, 0, 0);
    switch (face) {
    case FACE_DEAD: {
        POINT e1[2] = { { cx - 5, cy - 4 }, { cx - 1, cy } }, e2[2] = { { cx - 1, cy - 4 }, { cx - 5, cy } };
        POINT e3[2] = { { cx + 1, cy - 4 }, { cx + 5, cy } }, e4[2] = { { cx + 5, cy - 4 }, { cx + 1, cy } };
        lines(dc, e1, 2, k); lines(dc, e2, 2, k); lines(dc, e3, 2, k); lines(dc, e4, 2, k);
        POINT m[4] = { { cx - 4, cy + 5 }, { cx - 2, cy + 3 }, { cx + 2, cy + 3 }, { cx + 5, cy + 6 } };
        lines(dc, m, 4, k);
        break;
    }
    case FACE_COOL: {
        fill(dc, cx - 6, cy - 4, cx + 7, cy - 3, k);
        fill(dc, cx - 5, cy - 3, cx - 1, cy, k);
        fill(dc, cx + 2, cy - 3, cx + 6, cy, k);
        POINT m[4] = { { cx - 4, cy + 3 }, { cx - 2, cy + 5 }, { cx + 2, cy + 5 }, { cx + 5, cy + 2 } };
        lines(dc, m, 4, k);
        break;
    }
    default:
        fill(dc, cx - 4, cy - 4, cx - 2, cy - 2, k);
        fill(dc, cx + 3, cy - 4, cx + 5, cy - 2, k);
        if (face == FACE_OH) {
            ellipse(dc, cx - 2, cy + 2, cx + 3, cy + 7, RGB(255, 255, 0), k);
        } else {
            POINT m[4] = { { cx - 4, cy + 3 }, { cx - 2, cy + 5 }, { cx + 2, cy + 5 }, { cx + 5, cy + 2 } };
            lines(dc, m, 4, k);
        }
        break;
    }
}

static void paint(HDC dc)
{
    int w = client_w(), h = client_h();
    fill(dc, 0, 0, w, h, RGB(192, 192, 192));
    edge(dc, 0, 0, w, h, 3, 1);
    edge(dc, MARGIN, MARGIN, w - MARGIN, MARGIN + HEAD_H, 2, 0);
    edge(dc, MARGIN, board_y() - 3, w - MARGIN, board_y() + rows * CELL + 3, 3, 0);
    draw_led(dc, MARGIN + 6, mines - flags);
    draw_led(dc, w - MARGIN - 6 - LED_W, seconds);
    draw_face(dc);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            draw_cell(dc, r, c);
}

static void redraw_cell(int r, int c)
{
    if (r < 0 || c < 0 || r >= rows || c >= cols)
        return;
    HDC dc = GetDC(hwnd);
    draw_cell(dc, r, c);
    ReleaseDC(hwnd, dc);
}

static void redraw_around(int r, int c)
{
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            redraw_cell(r + dr, c + dc);
}

static void redraw_head(void)
{
    HDC dc = GetDC(hwnd);
    draw_led(dc, MARGIN + 6, mines - flags);
    draw_led(dc, client_w() - MARGIN - 6 - LED_W, seconds);
    draw_face(dc);
    ReleaseDC(hwnd, dc);
}

static void set_face(int f)
{
    if (face != f) {
        face = f;
        redraw_head();
    }
}

/* ---------------------------------------------------------------- the game */

static DWORD rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 8) & 0xFFFFFF;
}

static void new_game(void)
{
    memset(board, 0, sizeof(board));
    state = PLAYING;
    started = opened = flags = seconds = 0;
    exploded_r = exploded_c = -1;
    press_r = press_c = -1;
    chord = down_l = down_r = face_down = 0;
    face = FACE_SMILE;
    KillTimer(hwnd, 1);
    InvalidateRect(hwnd, NULL, FALSE);
}

/* the mines, placed at the first click: never on (r0, c0) */
static void lay_mines(int r0, int c0)
{
    seed ^= GetTickCount();
    int left = mines;
    while (left) {
        int r = (int)(rnd() % (DWORD)rows), c = (int)(rnd() % (DWORD)cols);
        if ((r == r0 && c == c0) || (board[r][c] & C_MINE))
            continue;
        board[r][c] |= C_MINE;
        left--;
    }
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int rr = r + dr, cc = c + dc;
                    if (rr >= 0 && cc >= 0 && rr < rows && cc < cols && (board[rr][cc] & C_MINE))
                        n++;
                }
            board[r][c] |= (BYTE)n;
        }
}

static void save_best(int lv);

static void game_won(void)
{
    state = WON;
    KillTimer(hwnd, 1);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            if ((board[r][c] & C_MINE) && !(board[r][c] & C_FLAG)) {
                board[r][c] = (BYTE)((board[r][c] & ~C_QUEST) | C_FLAG);
                flags++;
                redraw_cell(r, c);
            }
    face = FACE_COOL;
    redraw_head();
    /* the name is asked once the mouse button is up and free again */
    if (level < 3)
        PostMessage(hwnd, WM_USER + 1, 0, 0);
}

static void game_lost(int r, int c)
{
    state = LOST;
    exploded_r = r;
    exploded_c = c;
    KillTimer(hwnd, 1);
    face = FACE_DEAD;
    InvalidateRect(hwnd, NULL, FALSE);
}

/* open one square; a zero opens its neighbours too */
static void open_cell(int r0, int c0)
{
    static short stack[MAXROWS * MAXCOLS];
    int sp = 0;
    if (board[r0][c0] & (C_OPEN | C_FLAG))
        return;
    if (board[r0][c0] & C_MINE) {
        board[r0][c0] |= C_OPEN;
        game_lost(r0, c0);
        return;
    }
    stack[sp++] = (short)(r0 * MAXCOLS + c0);
    board[r0][c0] = (BYTE)((board[r0][c0] & ~C_QUEST) | C_OPEN);
    opened++;
    while (sp) {
        int i = stack[--sp], r = i / MAXCOLS, c = i % MAXCOLS;
        redraw_cell(r, c);
        if (board[r][c] & C_COUNT)
            continue;
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                int rr = r + dr, cc = c + dc;
                if (rr < 0 || cc < 0 || rr >= rows || cc >= cols)
                    continue;
                BYTE v = board[rr][cc];
                if (v & (C_OPEN | C_FLAG | C_MINE))
                    continue;
                board[rr][cc] = (BYTE)((v & ~C_QUEST) | C_OPEN);
                opened++;
                stack[sp++] = (short)(rr * MAXCOLS + cc);
            }
    }
    if (opened == rows * cols - mines)
        game_won();
}

static void start_if_needed(int r, int c)
{
    if (started)
        return;
    started = 1;
    lay_mines(r, c);
    seconds = 1;
    SetTimer(hwnd, 1, 1000, NULL);
    redraw_head();
}

static void reveal(int r, int c)
{
    if (state != PLAYING || (board[r][c] & (C_OPEN | C_FLAG)))
        return;
    start_if_needed(r, c);
    open_cell(r, c);
}

/* both buttons on a number: its neighbours when enough are flagged */
static void do_chord(int r, int c)
{
    if (state != PLAYING || !(board[r][c] & C_OPEN))
        return;
    int n = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
            int rr = r + dr, cc = c + dc;
            if (rr >= 0 && cc >= 0 && rr < rows && cc < cols && (board[rr][cc] & C_FLAG))
                n++;
        }
    if (n != (board[r][c] & C_COUNT))
        return;
    for (int dr = -1; dr <= 1 && state == PLAYING; dr++)
        for (int dc = -1; dc <= 1 && state == PLAYING; dc++) {
            int rr = r + dr, cc = c + dc;
            if (rr >= 0 && cc >= 0 && rr < rows && cc < cols)
                open_cell(rr, cc);
        }
}

static void toggle_mark(int r, int c)
{
    if (state != PLAYING || (board[r][c] & C_OPEN))
        return;
    BYTE v = board[r][c];
    if (v & C_FLAG) {
        v = (BYTE)(v & ~C_FLAG);
        flags--;
        if (marks)
            v |= C_QUEST;
    } else if (v & C_QUEST) {
        v = (BYTE)(v & ~C_QUEST);
    } else {
        v |= C_FLAG;
        flags++;
    }
    board[r][c] = v;
    redraw_cell(r, c);
    redraw_head();
}

static void set_press(int r, int c)
{
    if (r == press_r && c == press_c)
        return;
    int or_ = press_r, oc = press_c;
    press_r = r;
    press_c = c;
    if (chord) {
        redraw_around(or_, oc);
        redraw_around(r, c);
    } else {
        redraw_cell(or_, oc);
        redraw_cell(r, c);
    }
}

/* ---------------------------------------------------------------- settings and best times */

static void fit_window(void)
{
    RECT r = { 0, 0, client_w(), client_h() };
    AdjustWindowRect(&r, GetWindowLong(hwnd, GWL_STYLE), TRUE);
    RECT w;
    GetWindowRect(hwnd, &w);
    int cx = r.right - r.left, cy = r.bottom - r.top;
    int x = w.left, y = w.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    if (x + cx > sw) x = sw - cx;
    if (y + cy > sh) y = sh - cy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    SetWindowPos(hwnd, 0, x, y, cx, cy, SWP_NOZORDER);
}

/* the most rows and columns that fit on the screen */
static void max_size(int *mr, int *mc)
{
    RECT r = { 0, 0, 0, 0 };
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE);
    int fw = r.right - r.left, fh = r.bottom - r.top;
    *mr = (GetSystemMetrics(SM_CYSCREEN) - fh - (client_h() - rows * CELL)) / CELL;
    *mc = (GetSystemMetrics(SM_CXSCREEN) - fw - (client_w() - cols * CELL)) / CELL;
    if (*mr > MAXROWS) *mr = MAXROWS;
    if (*mc > MAXCOLS) *mc = MAXCOLS;
}

static void set_size(int lv, int r, int c, int m)
{
    int mr, mc;
    max_size(&mr, &mc);
    if (r < 8) r = 8;
    if (c < 8) c = 8;
    if (r > mr) r = mr;
    if (c > mc) c = mc;
    if (m < 10) m = 10;
    if (m > (r - 1) * (c - 1)) m = (r - 1) * (c - 1);
    level = lv;
    rows = r;
    cols = c;
    mines = m;
}

static void write_int(const char *key, int v)
{
    char s[12];
    wsprintf(s, "%d", v);
    WritePrivateProfileString("Minesweeper", key, s, INI);
}

static void save_settings(void)
{
    write_int("Difficulty", level);
    write_int("Height", rows);
    write_int("Width", cols);
    write_int("Mines", mines);
    write_int("Mark", marks);
    if (hwnd && !IsIconic(hwnd)) {
        RECT r;
        GetWindowRect(hwnd, &r);
        write_int("Xpos", r.left);
        write_int("Ypos", r.top);
    }
}

static void choose_level(int lv)
{
    if (lv < 3)
        set_size(lv, levels[lv].r, levels[lv].c, levels[lv].m);
    new_game();
    fit_window();
    save_settings();
}

#define IDC_H     200
#define IDC_W     201
#define IDC_M     202
#define IDC_NAME  203
#define IDC_T0    210
#define IDC_RESET 220
#define IDC_TEXT  221

static BOOL CALLBACK CustomProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemInt(dlg, IDC_H, (UINT)rows, FALSE);
        SetDlgItemInt(dlg, IDC_W, (UINT)cols, FALSE);
        SetDlgItemInt(dlg, IDC_M, (UINT)mines, FALSE);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            BOOL ok;
            int r = (int)GetDlgItemInt(dlg, IDC_H, &ok, FALSE);
            int c = (int)GetDlgItemInt(dlg, IDC_W, &ok, FALSE);
            int m = (int)GetDlgItemInt(dlg, IDC_M, &ok, FALSE);
            set_size(3, r, c, m);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

#define NUMEDIT (ES_LEFT | WS_BORDER | WS_TABSTOP | WS_VISIBLE)

static const DLGTEMPLATE_N custom_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 8, 30, 30, 132, 66,
    NULL, NULL, "Custom Field", 0, NULL
};
static const DLGITEM_N custom_items[] = {
    { 8, 8, 34, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Height:" },
    { 44, 6, 24, 12, IDC_H, NUMEDIT, "EDIT", "" },
    { 8, 26, 34, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Width:" },
    { 44, 24, 24, 12, IDC_W, NUMEDIT, "EDIT", "" },
    { 8, 44, 34, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Mines:" },
    { 44, 42, 24, 12, IDC_M, NUMEDIT, "EDIT", "" },
    { 80, 8, 44, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 80, 28, 44, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static char best_name[32];

static BOOL CALLBACK NameProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        char t[96];
        wsprintf(t, "You have the fastest time for %s level. Please type your name:", level_names[level]);
        SetDlgItemText(dlg, IDC_TEXT, t);
        SetDlgItemText(dlg, IDC_NAME, best_name);
        SendDlgItemMessage(dlg, IDC_NAME, EM_LIMITTEXT, sizeof(best_name) - 1, 0);
        SendDlgItemMessage(dlg, IDC_NAME, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL) {
            GetDlgItemText(dlg, IDC_NAME, best_name, sizeof(best_name));
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static const DLGTEMPLATE_N name_tpl = {
    WS_POPUP | WS_CAPTION | DS_MODALFRAME, 3, 40, 40, 140, 70,
    NULL, NULL, "Congratulations", 0, NULL
};
static const DLGITEM_N name_items[] = {
    { 8, 6, 124, 26, IDC_TEXT, SS_CENTER | WS_VISIBLE, "STATIC", "" },
    { 20, 34, 100, 12, IDC_NAME, ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "EDIT", "" },
    { 48, 52, 44, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
};

static void save_best(int lv)
{
    char key[8];
    wsprintf(key, "Time%d", lv + 1);
    int best = (int)GetPrivateProfileInt("Minesweeper", key, 999, INI);
    if (seconds >= best)
        return;
    char nkey[8];
    wsprintf(nkey, "Name%d", lv + 1);
    GetPrivateProfileString("Minesweeper", nkey, "Anonymous", best_name, sizeof(best_name), INI);
    DialogBoxIndirectN(hinst, &name_tpl, name_items, hwnd, NameProc);
    if (!best_name[0])
        lstrcpy(best_name, "Anonymous");
    write_int(key, seconds);
    WritePrivateProfileString("Minesweeper", nkey, best_name, INI);
    SendMessage(hwnd, WM_COMMAND, IDM_BEST, 0);
}

static void show_best(HWND dlg)
{
    for (int i = 0; i < 3; i++) {
        char key[8], nkey[8], name[32], t[80];
        wsprintf(key, "Time%d", i + 1);
        wsprintf(nkey, "Name%d", i + 1);
        int s = (int)GetPrivateProfileInt("Minesweeper", key, 999, INI);
        GetPrivateProfileString("Minesweeper", nkey, "Anonymous", name, sizeof(name), INI);
        wsprintf(t, "%-14s%4d seconds   %s", level_names[i], s, name);
        SetDlgItemText(dlg, IDC_T0 + i, t);
    }
}

static BOOL CALLBACK BestProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        show_best(dlg);
        SetFocus(GetDlgItem(dlg, IDOK));     /* Enter is OK, not Reset Scores */
        return FALSE;
    case WM_COMMAND:
        if (wp == IDC_RESET) {
            for (int i = 0; i < 3; i++) {
                char key[8];
                wsprintf(key, "Time%d", i + 1);
                WritePrivateProfileString("Minesweeper", key, "999", INI);
                wsprintf(key, "Name%d", i + 1);
                WritePrivateProfileString("Minesweeper", key, "Anonymous", INI);
            }
            show_best(dlg);
            return TRUE;
        }
        if (wp == IDOK || wp == IDCANCEL) {
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static const DLGTEMPLATE_N best_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 5, 30, 30, 196, 76,
    NULL, NULL, "Fastest Mine Sweepers", 0, NULL
};
static const DLGITEM_N best_items[] = {
    { 8, 8, 180, 9, IDC_T0, SS_LEFT | WS_VISIBLE, "STATIC", "" },
    { 8, 20, 180, 9, IDC_T0 + 1, SS_LEFT | WS_VISIBLE, "STATIC", "" },
    { 8, 32, 180, 9, IDC_T0 + 2, SS_LEFT | WS_VISIBLE, "STATIC", "" },
    { 30, 54, 60, 14, IDC_RESET, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "&Reset Scores" },
    { 106, 54, 60, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
};

/* ---------------------------------------------------------------- the window */

static void buttons_up(void)
{
    int r = press_r, c = press_c, was_chord = chord;
    press_r = press_c = -1;
    chord = 0;
    if (was_chord) {
        redraw_around(r, c);
        if (r >= 0)
            do_chord(r, c);
    } else if (r >= 0) {
        redraw_cell(r, c);
        reveal(r, c);
    }
    if (state == PLAYING)
        set_face(FACE_SMILE);
}

static LRESULT CALLBACK MineProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_CREATE:
        hwnd = h;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_USER + 1:
        save_best(level);
        return 0;
    case WM_TIMER:
        if (state == PLAYING && started && seconds < 999) {
            seconds++;
            redraw_head();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        RECT f;
        face_rect(&f);
        POINT p = { x, y };
        if (msg == WM_LBUTTONDOWN && PtInRect(&f, p)) {
            face_down = 1;
            redraw_head();
            SetCapture(h);
            return 0;
        }
        if (state != PLAYING)
            return 0;
        int r, c, on = cell_at(x, y, &r, &c);
        if (msg == WM_LBUTTONDOWN) down_l = 1;
        if (msg == WM_RBUTTONDOWN) down_r = 1;
        if (msg == WM_MBUTTONDOWN || (down_l && down_r)) {
            chord = 1;
        } else if (msg == WM_RBUTTONDOWN) {
            if (on)
                toggle_mark(r, c);
            return 0;
        }
        SetCapture(h);
        set_face(FACE_OH);
        if (on)
            set_press(r, c);
        if (chord)
            redraw_around(press_r, press_c);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!face_down && state == PLAYING && (chord || (wp & MK_LBUTTON))) {
            int r, c;
            if (cell_at(x, y, &r, &c))
                set_press(r, c);
            else
                set_press(-1, -1);
        }
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        if (face_down && msg == WM_LBUTTONUP) {
            RECT f;
            face_rect(&f);
            POINT p = { x, y };
            face_down = 0;
            ReleaseCapture();
            if (PtInRect(&f, p))
                new_game();
            else
                redraw_head();
            return 0;
        }
        if (msg == WM_LBUTTONUP) down_l = 0;
        if (msg == WM_RBUTTONUP) down_r = 0;
        if (state == PLAYING && (chord || msg == WM_LBUTTONUP) && (press_r >= 0 || chord)) {
            buttons_up();
        } else if (chord) {
            chord = 0;
        }
        if (!down_l && !down_r)
            ReleaseCapture();
        return 0;
    case WM_COMMAND:
        switch (wp) {
        case IDM_NEW:    new_game(); return 0;
        case IDM_BEGIN:
        case IDM_INTER:
        case IDM_EXPERT: choose_level((int)wp - IDM_BEGIN); return 0;
        case IDM_CUSTOM:
            if (DialogBoxIndirectN(hinst, &custom_tpl, custom_items, h, CustomProc) == IDOK)
                choose_level(3);
            return 0;
        case IDM_MARKS:
            marks = !marks;
            save_settings();
            return 0;
        case IDM_BEST:
            DialogBoxIndirectN(hinst, &best_tpl, best_items, h, BestProc);
            return 0;
        case IDM_EXIT:
            DestroyWindow(h);
            return 0;
        case IDM_ABOUT:
            MessageBox(h, "Minesweeper\nNative Windows 3.x API for murm386.", "About Minesweeper",
                       MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_INITMENUPOPUP: {
        HMENU m = (HMENU)wp;
        for (int i = 0; i < 4; i++)
            CheckMenuItem(m, (UINT)(IDM_BEGIN + i), i == level ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(m, IDM_MARKS, marks ? MF_CHECKED : MF_UNCHECKED);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(h, 1);
        save_settings();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)cmd;
    hinst = inst;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.style = 0;               /* quick clicks are clicks, not double clicks */
        wc.lpfnWndProc = MineProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = 0;
        wc.lpszClassName = "Minesweeper";
        if (!RegisterClass(&wc))
            return 1;
    }
    seed = GetTickCount();
    level = (int)GetPrivateProfileInt("Minesweeper", "Difficulty", 0, INI);
    if (level < 0 || level > 3)
        level = 0;
    marks = (int)GetPrivateProfileInt("Minesweeper", "Mark", 1, INI) != 0;
    if (level < 3) {
        rows = levels[level].r;
        cols = levels[level].c;
        mines = levels[level].m;
    } else {
        rows = (int)GetPrivateProfileInt("Minesweeper", "Height", 9, INI);
        cols = (int)GetPrivateProfileInt("Minesweeper", "Width", 9, INI);
        mines = (int)GetPrivateProfileInt("Minesweeper", "Mines", 10, INI);
    }
    set_size(level, rows, cols, mines);

    HMENU game = CreatePopupMenu();
    AppendMenu(game, MF_STRING, IDM_NEW, "&New\tF2");
    AppendMenu(game, MF_SEPARATOR, 0, NULL);
    AppendMenu(game, MF_STRING, IDM_BEGIN, "&Beginner");
    AppendMenu(game, MF_STRING, IDM_INTER, "&Intermediate");
    AppendMenu(game, MF_STRING, IDM_EXPERT, "&Expert");
    AppendMenu(game, MF_STRING, IDM_CUSTOM, "&Custom...");
    AppendMenu(game, MF_SEPARATOR, 0, NULL);
    AppendMenu(game, MF_STRING, IDM_MARKS, "&Marks (?)");
    AppendMenu(game, MF_SEPARATOR, 0, NULL);
    AppendMenu(game, MF_STRING, IDM_BEST, "Best &Times...");
    AppendMenu(game, MF_SEPARATOR, 0, NULL);
    AppendMenu(game, MF_STRING, IDM_EXIT, "E&xit");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About Minesweeper...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)game, "&Game");
    AppendMenu(bar, MF_POPUP, (UINT)help, "&Help");

    static const ACCEL_N keys[] = { { FVIRTKEY, VK_F2, IDM_NEW } };
    HACCEL acc = CreateAcceleratorTableN(keys, 1);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, client_w(), client_h() };
    AdjustWindowRect(&r, style, TRUE);
    int x = (int)GetPrivateProfileInt("Minesweeper", "Xpos", 80, INI);
    int y = (int)GetPrivateProfileInt("Minesweeper", "Ypos", 60, INI);
    HWND h = CreateWindow("Minesweeper", "Minesweeper", style, x, y, r.right - r.left, r.bottom - r.top,
                          0, bar, inst, NULL);
    if (!h)
        return 1;
    fit_window();
    new_game();
    ShowWindow(h, show);
    UpdateWindow(h);

    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        if (TranslateAccelerator(h, acc, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    DestroyAcceleratorTableN(acc);
    return (int)m.wParam;
}
