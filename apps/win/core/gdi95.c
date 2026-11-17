/*
 * gdi95.c - GDI's functions a Win16 program may call that are no drawing
 * of WINN's own: Windows 95's Bezier curves, charset, layout and halftone
 * palette, Windows 3.1's priority queues (printer drivers sort with them)
 * and the print spooler's calls (WINN has no printer: every job fails as
 * Windows' does with none set up).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

/* ---------------------------------------------------------------- Bezier curves */

#define BEZ_STEPS 16

/* the curve from p[0] through p[1], p[2] to p[3], as BEZ_STEPS lines: the
 * points after p[0] into out */
static void bezier(const POINT *p, POINT *out)
{
    for (int i = 1; i <= BEZ_STEPS; i++) {
        long t = i, u = BEZ_STEPS - i, n = (long)BEZ_STEPS * BEZ_STEPS * BEZ_STEPS;
        long a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        out[i - 1].x = (int)((a * p[0].x + b * p[1].x + c * p[2].x + d * p[3].x + n / 2) / n);
        out[i - 1].y = (int)((a * p[0].y + b * p[1].y + c * p[2].y + d * p[3].y + n / 2) / n);
    }
}

/* the curves after start: each three points of pts (count of them) */
static BOOL curves(HDC hdc, POINT start, const POINT *pts, int count)
{
    if (!pts || count < 0 || count % 3)
        return FALSE;
    POINT seg[4], line[1 + BEZ_STEPS];
    seg[0] = start;
    for (int k = 0; k < count; k += 3) {
        memcpy(seg + 1, pts + k, 3 * sizeof(POINT));
        line[0] = seg[0];
        bezier(seg, line + 1);
        if (!Polyline(hdc, line, 1 + BEZ_STEPS))
            return FALSE;
        seg[0] = seg[3];
    }
    return TRUE;
}

BOOL WINAPI PolyBezier(HDC hdc, const POINT *pts, int count)
{
    return pts && count >= 1 && curves(hdc, pts[0], pts + 1, count - 1);
}

/* from the current position, which ends at the last point */
BOOL WINAPI PolyBezierTo(HDC hdc, const POINT *pts, int count)
{
    POINT cur;
    if (!pts || count < 3 || !GetCurrentPositionEx(hdc, &cur) || !curves(hdc, cur, pts, count))
        return FALSE;
    MoveTo(hdc, pts[count - 1].x, pts[count - 1].y);
    return TRUE;
}

/* ---------------------------------------------------------------- DCs */

UINT WINAPI GetTextCharset(HDC hdc)
{
    TEXTMETRIC tm;
    return GetTextMetrics(hdc, &tm) ? tm.tmCharSet : 0;
}

/* left to right only: no mirrored layout */
DWORD WINAPI SetLayout(HDC hdc, DWORD layout)
{
    (void)layout;
    return gdi_dc(hdc) ? 0 : GDI_ERROR;
}

DWORD WINAPI GetLayout(HDC hdc) { return gdi_dc(hdc) ? 0 : GDI_ERROR; }

/* the system's 20 colours, then a 6 x 6 x 6 cube */
HPALETTE WINAPI CreateHalftonePalette(HDC hdc)
{
    (void)hdc;
    static const uint8_t sys[20][3] = {
        { 0, 0, 0 }, { 128, 0, 0 }, { 0, 128, 0 }, { 128, 128, 0 }, { 0, 0, 128 },
        { 128, 0, 128 }, { 0, 128, 128 }, { 192, 192, 192 }, { 192, 220, 192 },
        { 166, 202, 240 }, { 255, 251, 240 }, { 160, 160, 164 }, { 128, 128, 128 },
        { 255, 0, 0 }, { 0, 255, 0 }, { 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 },
        { 0, 255, 255 }, { 255, 255, 255 },
    };
    LOGPALETTE *lp = win_alloc(sizeof(LOGPALETTE) + 255 * sizeof(PALETTEENTRY));
    if (!lp)
        return 0;
    lp->palVersion = 0x300;
    lp->palNumEntries = 236;
    for (int i = 0; i < 20; i++) {
        lp->palPalEntry[i].peRed = sys[i][0];
        lp->palPalEntry[i].peGreen = sys[i][1];
        lp->palPalEntry[i].peBlue = sys[i][2];
    }
    for (int i = 0; i < 216; i++) {
        PALETTEENTRY *e = &lp->palPalEntry[20 + i];
        e->peRed = (BYTE)(i / 36 * 51);
        e->peGreen = (BYTE)(i / 6 % 6 * 51);
        e->peBlue = (BYTE)(i % 6 * 51);
    }
    HPALETTE h = CreatePalette(lp);
    win_free(lp);
    return h;
}

/* GDI.365: the default palette the DC's again */
UINT WINAPI RealizeDefaultPalette(HDC hdc)
{
    if (!gdi_dc(hdc))
        return 0;
    SelectPalette(hdc, GetStockObject(DEFAULT_PALETTE), FALSE);
    return RealizePalette(hdc);
}

/* ---------------------------------------------------------------- priority queues */

#define PQS 8
#define PQ_HANDLE(i) ((HANDLE)(uintptr_t)(0x5101u + (i)))

static struct {
    int size, count;
    struct { int tag, key; } *e;        /* kept sorted, the smallest key first */
} pq[PQS];

static int pq_index(HANDLE h)
{
    unsigned i = (unsigned)((uintptr_t)h - 0x5101u);
    return i < PQS && pq[i].e ? (int)i : -1;
}

HANDLE WINAPI CreatePQ(int size)
{
    if (size <= 0)
        return 0;
    for (int i = 0; i < PQS; i++)
        if (!pq[i].e) {
            pq[i].e = win_alloc((size_t)size * sizeof(*pq[i].e));
            if (!pq[i].e)
                return 0;
            pq[i].size = size;
            pq[i].count = 0;
            return PQ_HANDLE(i);
        }
    return 0;
}

/* the smallest key's tag, it stays; -1 empty or no queue */
int WINAPI MinPQ(HANDLE h)
{
    int i = pq_index(h);
    return i >= 0 && pq[i].count ? pq[i].e[0].tag : -1;
}

/* the same, taken out */
int WINAPI ExtractPQ(HANDLE h)
{
    int i = pq_index(h);
    if (i < 0 || !pq[i].count)
        return -1;
    int tag = pq[i].e[0].tag;
    memmove(pq[i].e, pq[i].e + 1, (size_t)--pq[i].count * sizeof(*pq[i].e));
    return tag;
}

/* TRUE, or FALSE when full */
int WINAPI InsertPQ(HANDLE h, int tag, int key)
{
    int i = pq_index(h);
    if (i < 0 || pq[i].count >= pq[i].size)
        return FALSE;
    int at = pq[i].count;
    while (at > 0 && pq[i].e[at - 1].key > key)
        at--;
    memmove(pq[i].e + at + 1, pq[i].e + at, (size_t)(pq[i].count - at) * sizeof(*pq[i].e));
    pq[i].e[at].tag = tag;
    pq[i].e[at].key = key;
    pq[i].count++;
    return TRUE;
}

/* grown (shrunk) by more entries: the new size, -1 if it cannot */
int WINAPI SizePQ(HANDLE h, int more)
{
    int i = pq_index(h);
    int size = i >= 0 ? pq[i].size + more : -1;
    if (i < 0 || size < pq[i].count || size <= 0)
        return -1;
    void *e = win_realloc(pq[i].e, (size_t)size * sizeof(*pq[i].e));
    if (!e)
        return -1;
    pq[i].e = e;
    pq[i].size = size;
    return size;
}

int WINAPI DeletePQ(HANDLE h)
{
    int i = pq_index(h);
    if (i < 0)
        return FALSE;
    win_free(pq[i].e);
    pq[i].e = NULL;
    return TRUE;
}

/* ---------------------------------------------------------------- the spooler */

int WINAPI OpenJob(LPCSTR port, LPCSTR title, HDC hdc)
{
    (void)port; (void)title; (void)hdc;
    return SP_ERROR;
}

int WINAPI StartSpoolPage(HANDLE job) { (void)job; return SP_ERROR; }
int WINAPI EndSpoolPage(HANDLE job) { (void)job; return SP_ERROR; }
int WINAPI CloseJob(HANDLE job) { (void)job; return SP_ERROR; }
int WINAPI DeleteJob(HANDLE job, int flags) { (void)job; (void)flags; return SP_ERROR; }
int WINAPI DeleteSpoolPage(HANDLE job) { (void)job; return SP_ERROR; }

int WINAPI WriteSpool(HANDLE job, LPCSTR data, int cb)
{
    (void)job; (void)data; (void)cb;
    return SP_ERROR;
}

int WINAPI WriteDialog(HANDLE job, LPCSTR text, int cb)
{
    (void)job; (void)text; (void)cb;
    return SP_ERROR;
}

/* no job in the queue */
int WINAPI GetSpoolJob(int option, LONG param)
{
    (void)option; (void)param;
    return 0;
}

int WINAPI QueryJob(HANDLE job, int what)
{
    (void)job; (void)what;
    return SP_ERROR;
}

HANDLE WINAPI SpoolFile(LPCSTR printer, LPCSTR port, LPCSTR job, LPCSTR file)
{
    (void)printer; (void)port; (void)job; (void)file;
    return (HANDLE)(uintptr_t)(UINT)SP_ERROR;
}
