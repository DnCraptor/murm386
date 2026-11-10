/*
 * region.c - RECT helpers and regions as lists of disjoint rectangles.
 *
 * Regions in a window system are small (a few dozen rectangles), so a flat
 * list with pairwise operations is simpler and smaller than banded
 * y-x structures, and fast enough. Invariant: rectangles are non-empty and
 * do not overlap; box is their bounding box (empty when n == 0).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"

/* ---------------------------------------------------------------- RECT */

BOOL WINAPI SetRect(LPRECT rc, int l, int t, int r, int b)
{
    rc->left = l; rc->top = t; rc->right = r; rc->bottom = b;
    return TRUE;
}

BOOL WINAPI SetRectEmpty(LPRECT rc) { return SetRect(rc, 0, 0, 0, 0); }

BOOL WINAPI CopyRect(LPRECT d, const RECT *s) { *d = *s; return TRUE; }

BOOL WINAPI IsRectEmpty(const RECT *rc)
{
    return rc->left >= rc->right || rc->top >= rc->bottom;
}

BOOL WINAPI PtInRect(const RECT *rc, POINT pt)
{
    return pt.x >= rc->left && pt.x < rc->right &&
           pt.y >= rc->top && pt.y < rc->bottom;
}

BOOL WINAPI OffsetRect(LPRECT rc, int dx, int dy)
{
    rc->left += dx; rc->right += dx; rc->top += dy; rc->bottom += dy;
    return TRUE;
}

BOOL WINAPI InflateRect(LPRECT rc, int dx, int dy)
{
    rc->left -= dx; rc->right += dx; rc->top -= dy; rc->bottom += dy;
    return TRUE;
}

BOOL WINAPI IntersectRect(LPRECT d, const RECT *a, const RECT *b)
{
    RECT r;
    r.left = a->left > b->left ? a->left : b->left;
    r.top = a->top > b->top ? a->top : b->top;
    r.right = a->right < b->right ? a->right : b->right;
    r.bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (IsRectEmpty(&r)) {
        SetRectEmpty(d);
        return FALSE;
    }
    *d = r;
    return TRUE;
}

BOOL WINAPI UnionRect(LPRECT d, const RECT *a, const RECT *b)
{
    if (IsRectEmpty(a)) {
        if (IsRectEmpty(b)) {
            SetRectEmpty(d);
            return FALSE;
        }
        *d = *b;
        return TRUE;
    }
    if (IsRectEmpty(b)) {
        *d = *a;
        return TRUE;
    }
    RECT r;
    r.left = a->left < b->left ? a->left : b->left;
    r.top = a->top < b->top ? a->top : b->top;
    r.right = a->right > b->right ? a->right : b->right;
    r.bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
    *d = r;
    return TRUE;
}

/* Windows semantics: result is a only shrunk when b covers a full side. */
BOOL WINAPI SubtractRect(LPRECT d, const RECT *a, const RECT *b)
{
    RECT r = *a, x;
    if (IsRectEmpty(a)) {
        SetRectEmpty(d);
        return FALSE;
    }
    if (IntersectRect(&x, a, b)) {
        if (x.left == r.left && x.right == r.right) {
            if (x.top == r.top)
                r.top = x.bottom;
            else if (x.bottom == r.bottom)
                r.bottom = x.top;
        } else if (x.top == r.top && x.bottom == r.bottom) {
            if (x.left == r.left)
                r.left = x.right;
            else if (x.right == r.right)
                r.right = x.left;
        }
    }
    *d = r;
    if (IsRectEmpty(d)) {
        SetRectEmpty(d);
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI EqualRect(const RECT *a, const RECT *b)
{
    return a->left == b->left && a->top == b->top &&
           a->right == b->right && a->bottom == b->bottom;
}

/* ---------------------------------------------------------------- wrgn */

void wrgn_init(wrgn *g) { memset(g, 0, sizeof(*g)); }

void wrgn_clear(wrgn *g)
{
    g->n = 0;
    SetRectEmpty(&g->box);
}

void wrgn_free(wrgn *g)
{
    win_free(g->r);
    wrgn_init(g);
}

static int wrgn_reserve(wrgn *g, int n)
{
    if (n <= g->cap)
        return 1;
    int cap = g->cap ? g->cap : 4;
    while (cap < n)
        cap *= 2;
    RECT *r = (RECT *)win_realloc(g->r, sizeof(RECT) * (size_t)cap);
    if (!r)
        return 0;
    g->r = r;
    g->cap = cap;
    return 1;
}

static void wrgn_push(wrgn *g, int l, int t, int r, int b)
{
    if (l >= r || t >= b || !wrgn_reserve(g, g->n + 1))
        return;
    RECT *p = &g->r[g->n++];
    p->left = l; p->top = t; p->right = r; p->bottom = b;
}

static void wrgn_update_box(wrgn *g)
{
    SetRectEmpty(&g->box);
    for (int i = 0; i < g->n; i++)
        UnionRect(&g->box, &g->box, &g->r[i]);
}

/* Merge rectangles that share a full edge; keeps lists short after the
 * many splits subtraction produces. */
static void wrgn_coalesce(wrgn *g)
{
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < g->n; i++) {
            RECT *a = &g->r[i];
            for (int j = i + 1; j < g->n; j++) {
                RECT *b = &g->r[j];
                int merge = 0;
                if (a->left == b->left && a->right == b->right) {
                    if (a->bottom == b->top) { a->bottom = b->bottom; merge = 1; }
                    else if (b->bottom == a->top) { a->top = b->top; merge = 1; }
                } else if (a->top == b->top && a->bottom == b->bottom) {
                    if (a->right == b->left) { a->right = b->right; merge = 1; }
                    else if (b->right == a->left) { a->left = b->left; merge = 1; }
                }
                if (merge) {
                    g->r[j] = g->r[--g->n];
                    changed = 1;
                    j--;
                }
            }
        }
    }
}

static void wrgn_finish(wrgn *g)
{
    wrgn_coalesce(g);
    wrgn_update_box(g);
}

void wrgn_add(wrgn *g, const RECT *rc) { wrgn_push(g, rc->left, rc->top, rc->right, rc->bottom); }
void wrgn_done(wrgn *g) { wrgn_finish(g); }

void wrgn_set_rect(wrgn *g, const RECT *rc)
{
    g->n = 0;
    wrgn_push(g, rc->left, rc->top, rc->right, rc->bottom);
    wrgn_update_box(g);
}

void wrgn_copy(wrgn *d, const wrgn *s)
{
    if (d == s)
        return;
    d->n = 0;
    if (s->n && !wrgn_reserve(d, s->n)) {
        wrgn_clear(d);
        return;
    }
    if (s->n)
        memcpy(d->r, s->r, sizeof(RECT) * (size_t)s->n);
    d->n = s->n;
    d->box = s->box;
}

/* Append (a - b) pieces, up to four per rectangle. */
static void push_diff(wrgn *out, const RECT *a, const RECT *b)
{
    RECT x;
    if (!IntersectRect(&x, a, b)) {
        wrgn_push(out, a->left, a->top, a->right, a->bottom);
        return;
    }
    wrgn_push(out, a->left, a->top, a->right, x.top);              /* above */
    wrgn_push(out, a->left, x.bottom, a->right, a->bottom);        /* below */
    wrgn_push(out, a->left, x.top, x.left, x.bottom);              /* left */
    wrgn_push(out, x.right, x.top, a->right, x.bottom);            /* right */
}

/* out = list - rc, out must not alias list. */
static void diff_rect_into(wrgn *out, const RECT *list, int n, const RECT *rc)
{
    for (int i = 0; i < n; i++)
        push_diff(out, &list[i], rc);
}

void wrgn_diff_rect(wrgn *g, const RECT *rc)
{
    RECT x;
    if (!g->n || !IntersectRect(&x, &g->box, rc))
        return;
    wrgn t;
    wrgn_init(&t);
    diff_rect_into(&t, g->r, g->n, rc);
    wrgn_free(g);
    *g = t;
    wrgn_finish(g);
}

void wrgn_and_rect(wrgn *g, const RECT *rc)
{
    int o = 0;
    for (int i = 0; i < g->n; i++) {
        RECT x;
        if (IntersectRect(&x, &g->r[i], rc))
            g->r[o++] = x;
    }
    g->n = o;
    wrgn_finish(g);
}

void wrgn_or_rect(wrgn *g, const RECT *rc)
{
    if (IsRectEmpty(rc))
        return;
    wrgn_diff_rect(g, rc);       /* make room so the list stays disjoint */
    wrgn_push(g, rc->left, rc->top, rc->right, rc->bottom);
    wrgn_finish(g);
}

void wrgn_and(wrgn *d, const wrgn *a, const wrgn *b)
{
    wrgn t;
    wrgn_init(&t);
    for (int i = 0; i < a->n; i++)
        for (int j = 0; j < b->n; j++) {
            RECT x;
            if (IntersectRect(&x, &a->r[i], &b->r[j]))
                wrgn_push(&t, x.left, x.top, x.right, x.bottom);
        }
    wrgn_finish(&t);
    wrgn_free(d);
    *d = t;
}

void wrgn_diff(wrgn *d, const wrgn *a, const wrgn *b)
{
    wrgn t;
    wrgn_init(&t);
    wrgn_copy(&t, a);
    for (int j = 0; j < b->n && t.n; j++)
        wrgn_diff_rect(&t, &b->r[j]);
    wrgn_free(d);
    *d = t;
}

void wrgn_or(wrgn *d, const wrgn *a, const wrgn *b)
{
    wrgn t;
    wrgn_init(&t);
    wrgn_copy(&t, a);
    for (int j = 0; j < b->n; j++)
        wrgn_or_rect(&t, &b->r[j]);
    wrgn_free(d);
    *d = t;
}

void wrgn_xor(wrgn *d, const wrgn *a, const wrgn *b)
{
    wrgn ab, ba;
    wrgn_init(&ab);
    wrgn_init(&ba);
    wrgn_diff(&ab, a, b);
    wrgn_diff(&ba, b, a);
    /* the two parts are disjoint: concatenate */
    if (wrgn_reserve(&ab, ab.n + ba.n)) {
        if (ba.n)
            memcpy(ab.r + ab.n, ba.r, sizeof(RECT) * (size_t)ba.n);
        ab.n += ba.n;
    }
    wrgn_free(&ba);
    wrgn_finish(&ab);
    wrgn_free(d);
    *d = ab;
}

void wrgn_offset(wrgn *g, int dx, int dy)
{
    for (int i = 0; i < g->n; i++)
        OffsetRect(&g->r[i], dx, dy);
    if (g->n)
        OffsetRect(&g->box, dx, dy);
}

int wrgn_type(const wrgn *g)
{
    return g->n == 0 ? NULLREGION : g->n == 1 ? SIMPLEREGION : COMPLEXREGION;
}

int wrgn_pt_in(const wrgn *g, int x, int y)
{
    POINT p = { x, y };
    if (!PtInRect(&g->box, p))
        return 0;
    for (int i = 0; i < g->n; i++)
        if (PtInRect(&g->r[i], p))
            return 1;
    return 0;
}

int wrgn_rect_in(const wrgn *g, const RECT *rc)
{
    RECT x;
    if (!IntersectRect(&x, &g->box, rc))
        return 0;
    for (int i = 0; i < g->n; i++)
        if (IntersectRect(&x, &g->r[i], rc))
            return 1;
    return 0;
}

static long wrgn_area(const wrgn *g)
{
    long a = 0;
    for (int i = 0; i < g->n; i++)
        a += (long)(g->r[i].right - g->r[i].left) * (g->r[i].bottom - g->r[i].top);
    return a;
}

int wrgn_equal(const wrgn *a, const wrgn *b)
{
    if (a->n == 0 || b->n == 0)
        return a->n == b->n;
    if (!EqualRect(&a->box, &b->box) || wrgn_area(a) != wrgn_area(b))
        return 0;
    /* equal area and a inside b means equal */
    wrgn t;
    wrgn_init(&t);
    wrgn_diff(&t, a, b);
    int eq = t.n == 0;
    wrgn_free(&t);
    return eq;
}
