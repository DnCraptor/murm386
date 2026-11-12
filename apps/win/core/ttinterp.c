/*
 * ttinterp.c - the TrueType instructions: a font's hints, run as Windows'
 * rasterizer runs them, so its glyphs fit the pixels at the size they are
 * drawn: the font program (fpgm) and the control value program (prep)
 * once a size, each glyph's own program then, its points moved in 26.6
 * fixed point; the whole instruction set (the vectors, the zones and the
 * twilight points, the rounding states, MDAP/MIAP/MDRP/MIRP/MSIRP,
 * IP/IUP/SHP/SHC/SHZ/SHPIX/ALIGNRP/ISECT, the DELTAs, the functions and
 * the instruction definitions, SCANCTRL/SCANTYPE, INSTCTRL), Microsoft's
 * own ways where the specification and Windows differ (as FreeType's
 * version 35 interpreter, which is Windows', has them). A composite
 * glyph's components hinted each, then its own instructions. GETINFO says
 * Windows 3.1's rasterizer (version 3), upright and not stretched.
 *
 * Each glyph's program starts from the state the control value program
 * left (its control values, storage and twilight points), so a glyph is
 * drawn the same whatever was drawn before. An error stops a program
 * where it is, its points as they are then (Windows' way); one in the
 * font's or the control value program leaves the size unhinted.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>

#include "wincore.h"
#include "truetype_int.h"

#ifndef TT_VERSION                  /* (a test's build: another's) */
#define TT_VERSION     3            /* GETINFO: Windows 3.1's rasterizer */
#endif
#define MAX_CALLS      64
#define MAX_RUN        2000000L     /* the instructions a program may run */
#define MAX_DEPTH      8            /* composites within composites */

#define TAG_ON         1
#define TAG_TX         2            /* touched in x */
#define TAG_TY         4            /* touched in y */

typedef int32_t F26;                /* 26.6 */

typedef struct {
    int32_t x, y;
} vec;

typedef struct {
    int       n, ncont;
    vec      *org, *cur, *orus;
    uint8_t  *tag;
    uint16_t *ends;
} zone;

typedef struct {
    int     range;                  /* 0 fpgm, 1 prep, 2 the glyph's */
    int32_t start, end;
    int     active;
} fdef;

typedef struct {
    int     rp0, rp1, rp2, gep0, gep1, gep2;
    int16_t pv[2], fv[2], dv[2];    /* 2.14 */
    int32_t loop, min_dist;
    int     round_state;
    int32_t period, phase, threshold;
    int     auto_flip;
    int32_t cvt_cutin, sw_cutin, sw_value;
    int32_t delta_base, delta_shift;
    int     instruct_control, scan_control;
    int32_t scan_type;
} gstate;

enum { R_HALF, R_GRID, R_DOUBLE, R_DOWN, R_UP = 4, R_OFF = 5, R_SUPER = 6, R_SUPER45 = 7 };

typedef struct {
    int     range;
    int32_t ret, start, end, count;
} call;

struct tthint {
    const ttfont *f;
    int      ppem;
    int32_t  scale;                 /* 16.16: font units to 26.6 */
    int      ok;                    /* the font's programs ran */
    /* the code */
    const uint8_t *code[3];
    int32_t  len[3];
    /* the size's state, and as the control value program left it */
    int      ncvt, nstore;
    int32_t *cvt, *cvt0, *store, *store0;
    zone     tw;                    /* the twilight points */
    vec     *tw0;                   /* their org and cur after prep */
    gstate   gs, gs0;
    fdef    *fdefs;
    int      nfdefs;
    struct {
        int  opc;
        fdef d;
    } idefs[64];
    int      nidefs;
    int32_t *stack;
    int      stack_max;
    /* a program running */
    zone     pts;                   /* the glyph's points (zone 1) */
    zone    *zp0, *zp1, *zp2;
    int32_t  fdotp;
    int      range;
    int32_t  ip, top;
    call     calls[MAX_CALLS];
    int      ncalls;
    int      error;
    long     budget;
    unsigned opc_now;               /* the instruction running */
    int      step;                  /* 0: it moved ip itself */
    int32_t  new_top;
    int32_t  scale_now;             /* the scale MD, IP and SSW use (1.0 for a composite's) */
};

/* ---------------------------------------------------------------- arithmetic */

static int32_t mulfix(int32_t a, int32_t b)
{
    int64_t ab = (int64_t)a * b;
    return (int32_t)((ab + 0x8000 - (ab < 0)) >> 16);
}

static int32_t divfix(int32_t a, int32_t b)
{
    int s = 1;
    uint64_t ua, ub, q;
    if (a < 0) { s = -s; ua = (uint64_t)(-(int64_t)a); } else ua = (uint64_t)a;
    if (b < 0) { s = -s; ub = (uint64_t)(-(int64_t)b); } else ub = (uint64_t)b;
    q = ub ? ((ua << 16) + (ub >> 1)) / ub : 0x7fffffffu;
    if (q > 0x7fffffffu)
        q = 0x7fffffffu;
    return s < 0 ? -(int32_t)q : (int32_t)q;
}

static int32_t muldiv_(int32_t a, int32_t b, int32_t c, int round)
{
    int s = 1;
    uint64_t ua, ub, uc, q;
    if (a < 0) { s = -s; ua = (uint64_t)(-(int64_t)a); } else ua = (uint64_t)a;
    if (b < 0) { s = -s; ub = (uint64_t)(-(int64_t)b); } else ub = (uint64_t)b;
    if (c < 0) { s = -s; uc = (uint64_t)(-(int64_t)c); } else uc = (uint64_t)c;
    q = uc ? (ua * ub + (round ? uc >> 1 : 0)) / uc : 0x7fffffffu;
    if (q > 0x7fffffffu)
        q = 0x7fffffffu;
    return s < 0 ? -(int32_t)q : (int32_t)q;
}

static int32_t muldiv(int32_t a, int32_t b, int32_t c) { return muldiv_(a, b, c, 1); }

static int32_t mulfix14(int32_t a, int32_t b)
{
    int64_t ab = (int64_t)a * b;
    ab += 0x2000 + (ab < 0 ? -1 : 0);
    return (int32_t)(ab >> 14);
}

static int32_t dot14(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int64_t a = (int64_t)ax * bx + (int64_t)ay * by;
    a += 0x2000 + (a < 0 ? -1 : 0);
    return (int32_t)(a >> 14);
}

static int msb32(uint32_t v)
{
    int n = 0;
    while (v >>= 1)
        n++;
    return n;
}

/* (x, y) as a unit vector, 2.14 ((0, 0): as it was); the length by
 * Newton's iterations in integers, to the last bit as Windows' and
 * FreeType's rasterizers have it */
static void normalize(int32_t vx, int32_t vy, int16_t out[2])
{
    if (!vx && !vy)
        return;
    uint32_t x = vx < 0 ? (uint32_t)-(int64_t)vx : (uint32_t)vx;
    uint32_t y = vy < 0 ? (uint32_t)-(int64_t)vy : (uint32_t)vy;
    int sx = vx < 0 ? -1 : 1, sy = vy < 0 ? -1 : 1;
    int32_t rx, ry;
    if (x == 0) {
        rx = 0;
        ry = sy * 0x10000;
    } else if (y == 0) {
        rx = sx * 0x10000;
        ry = 0;
    } else {
        uint32_t l = x > y ? x + (y >> 1) : y + (x >> 1);
        int shift = 31 - msb32(l);
        shift -= 15 + (l >= (0xAAAAAAAAu >> shift));
        if (shift > 0) {
            x <<= shift;
            y <<= shift;
            l = x > y ? x + (y >> 1) : y + (x >> 1);
        } else {
            x >>= -shift;
            y >>= -shift;
            l >>= -shift;
        }
        int32_t b = 0x10000 - (int32_t)l, z;
        int32_t x_ = (int32_t)x, y_ = (int32_t)y;
        uint32_t u, v;
        do {
            u = (uint32_t)(x_ + (int32_t)(((int64_t)x_ * b) >> 16));
            v = (uint32_t)(y_ + (int32_t)(((int64_t)y_ * b) >> 16));
            z = -(int32_t)(u * u + v * v) / 0x200;
            z = z * ((0x10000 + b) >> 8) / 0x10000;
            b += z;
        } while (z > 0);
        rx = sx < 0 ? -(int32_t)u : (int32_t)u;
        ry = sy < 0 ? -(int32_t)v : (int32_t)v;
    }
    out[0] = (int16_t)(rx / 4);
    out[1] = (int16_t)(ry / 4);
}

/* ---------------------------------------------------------------- vectors */

static void compute_fdotp(tthint *h)
{
    int32_t d = (int32_t)(((int64_t)h->gs.pv[0] * h->gs.fv[0] +
                           (int64_t)h->gs.pv[1] * h->gs.fv[1]) >> 14);
    h->fdotp = d > -0x400 && d < 0x400 ? 0x4000 : d;
}

static F26 project(const tthint *h, int32_t dx, int32_t dy)
{
    return dot14(dx, dy, h->gs.pv[0], h->gs.pv[1]);
}

static F26 dualproj(const tthint *h, int32_t dx, int32_t dy)
{
    return dot14(dx, dy, h->gs.dv[0], h->gs.dv[1]);
}

static void move_point(tthint *h, zone *z, int p, F26 d)
{
    if (h->gs.fv[0]) {
        z->cur[p].x += muldiv(d, h->gs.fv[0], h->fdotp);
        z->tag[p] |= TAG_TX;
    }
    if (h->gs.fv[1]) {
        z->cur[p].y += muldiv(d, h->gs.fv[1], h->fdotp);
        z->tag[p] |= TAG_TY;
    }
}

static void move_orig(tthint *h, zone *z, int p, F26 d)
{
    if (h->gs.fv[0])
        z->org[p].x += muldiv(d, h->gs.fv[0], h->fdotp);
    if (h->gs.fv[1])
        z->org[p].y += muldiv(d, h->gs.fv[1], h->fdotp);
}

/* ---------------------------------------------------------------- rounding */

static F26 round_by(const tthint *h, F26 d, int state)
{
    F26 v;
    switch (state) {
    case R_HALF:
        if (d >= 0) {
            v = (d & ~63) + 32;
            if (v < 0) v = 32;
        } else {
            v = -((-d & ~63) + 32);
            if (v > 0) v = -32;
        }
        return v;
    case R_GRID:
        if (d >= 0) {
            v = (d + 32) & ~63;
            if (v < 0) v = 0;
        } else {
            v = -((-d + 32) & ~63);
            if (v > 0) v = 0;
        }
        return v;
    case R_DOUBLE:
        if (d >= 0) {
            v = (d + 16) & ~31;
            if (v < 0) v = 0;
        } else {
            v = -((-d + 16) & ~31);
            if (v > 0) v = 0;
        }
        return v;
    case R_DOWN:
        if (d >= 0) {
            v = d & ~63;
            if (v < 0) v = 0;
        } else {
            v = -(-d & ~63);
            if (v > 0) v = 0;
        }
        return v;
    case R_UP:
        if (d >= 0) {
            v = (d + 63) & ~63;
            if (v < 0) v = 0;
        } else {
            v = -((-d + 63) & ~63);
            if (v > 0) v = 0;
        }
        return v;
    case R_SUPER:
        if (d >= 0) {
            v = ((d - h->gs.phase + h->gs.threshold) & -h->gs.period) + h->gs.phase;
            if (v < 0) v = h->gs.phase;
        } else {
            v = -(((-d - h->gs.phase + h->gs.threshold) & -h->gs.period) + h->gs.phase);
            if (v > 0) v = -h->gs.phase;
        }
        return v;
    case R_SUPER45:
        if (!h->gs.period)
            return d;
        if (d >= 0) {
            v = (d - h->gs.phase + h->gs.threshold) / h->gs.period * h->gs.period + h->gs.phase;
            if (v < 0) v = h->gs.phase;
        } else {
            v = -((-d - h->gs.phase + h->gs.threshold) / h->gs.period * h->gs.period +
                  h->gs.phase);
            if (v > 0) v = -h->gs.phase;
        }
        return v;
    default:
        return d;
    }
}

static F26 round_(const tthint *h, F26 d) { return round_by(h, d, h->gs.round_state); }

/* SROUND, S45ROUND: grid 1.0 or sqrt(2)/2 (2.14) */
static void super_round(tthint *h, int32_t grid, int32_t sel)
{
    int32_t period;
    switch (sel & 0xc0) {
    case 0x00: period = grid / 2; break;
    case 0x40: period = grid; break;
    case 0x80: period = grid * 2; break;
    default: period = grid; break;
    }
    int32_t phase;
    switch (sel & 0x30) {
    case 0x00: phase = 0; break;
    case 0x10: phase = period >> 2; break;
    case 0x20: phase = period >> 1; break;
    default: phase = period * 3 / 4; break;
    }
    int32_t threshold = (sel & 0x0f) == 0 ? period - 1 : ((sel & 0x0f) - 4) * period / 8;
    h->gs.period = period >> 8;     /* 2.14 to 26.6 */
    h->gs.phase = phase >> 8;
    h->gs.threshold = threshold >> 8;
}

/* ---------------------------------------------------------------- the code */

/* an instruction's length in bytes; -1 past the end */
static int32_t ins_len(const uint8_t *c, int32_t ip, int32_t len)
{
    unsigned op = c[ip];
    int32_t n = 1;
    if (op == 0x40)
        n = ip + 1 < len ? 2 + c[ip + 1] : -1;
    else if (op == 0x41)
        n = ip + 1 < len ? 2 + 2 * c[ip + 1] : -1;
    else if (op >= 0xb0 && op <= 0xb7)
        n = 1 + (int32_t)(op - 0xaf);
    else if (op >= 0xb8 && op <= 0xbf)
        n = 1 + 2 * (int32_t)(op - 0xb7);
    return n < 0 || ip + n > len ? -1 : n;
}

/* the instructions' pops and pushes */
static const uint8_t popush[256] = {
    /* 00 */ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20,
             0x20, 0x20, 0x20, 0x20, 0x02, 0x02, 0x00, 0x50,
    /* 10 */ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
             0x00, 0x00, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10,
    /* 20 */ 0x12, 0x10, 0x00, 0x22, 0x01, 0x11, 0x10, 0x20,
             0x00, 0x10, 0x20, 0x10, 0x10, 0x00, 0x10, 0x10,
    /* 30 */ 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10,
             0x10, 0x00, 0x20, 0x20, 0x00, 0x00, 0x20, 0x20,
    /* 40 */ 0x00, 0x00, 0x20, 0x11, 0x20, 0x11, 0x11, 0x11,
             0x20, 0x21, 0x21, 0x01, 0x01, 0x00, 0x00, 0x10,
    /* 50 */ 0x21, 0x21, 0x21, 0x21, 0x21, 0x21, 0x11, 0x11,
             0x10, 0x00, 0x21, 0x21, 0x11, 0x10, 0x10, 0x10,
    /* 60 */ 0x21, 0x21, 0x21, 0x21, 0x11, 0x11, 0x11, 0x11,
             0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    /* 70 */ 0x20, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
             0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10,
    /* 80 */ 0x00, 0x20, 0x20, 0x00, 0x00, 0x10, 0x20, 0x20,
             0x11, 0x10, 0x33, 0x21, 0x21, 0x10, 0x20, 0x00,
    /* 90 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* a0 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* b0 */ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
             0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    /* c0 */ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
             0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
             0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
             0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    /* e0 */ 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
             0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
             0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
             0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
};

/* the opcodes the instruction set has (the rest: IDEF's or none) */
static int defined_op(unsigned op)
{
    return !(op == 0x28 || op == 0x7b || op == 0x83 || op == 0x84 ||
             (op >= 0x8f && op <= 0xaf));
}

static fdef *find_fdef(tthint *h, int32_t n)
{
    return n >= 0 && n < h->nfdefs && h->fdefs[n].active ? &h->fdefs[n] : NULL;
}

/* past the matching ENDF (FDEF, IDEF) or ELSE/EIF (IF): ip on the
 * instruction after it; 0 if there is none */
static int skip_to(tthint *h, int want_else)
{
    const uint8_t *c = h->code[h->range];
    int32_t len = h->len[h->range], ip = h->ip, depth = 0;
    int32_t n = ins_len(c, ip, len);
    if (n < 0)
        return 0;
    ip += n;
    while (ip < len) {
        unsigned op = c[ip];
        n = ins_len(c, ip, len);
        if (n < 0)
            return 0;
        if (want_else < 0) {                /* FDEF, IDEF: to ENDF */
            if (op == 0x2d) {
                h->ip = ip;
                return 1;
            }
            if (op == 0x2c || op == 0x89)   /* not within another */
                return 0;
        } else if (op == 0x58) {
            depth++;
        } else if (op == 0x59) {
            if (!depth) {
                h->ip = ip;
                return 1;
            }
            depth--;
        } else if (op == 0x1b && !depth && want_else) {
            h->ip = ip;
            return 1;
        }
        ip += n;
    }
    return 0;
}

/* ---------------------------------------------------------------- running */

#define FAIL() do { h->error = 1; return; } while (0)
#define BAD_PT(z, p) ((p) < 0 || (p) >= (z)->n)

static int zone_of(tthint *h, int32_t v, zone **z)
{
    if (v == 0)
        *z = &h->tw;
    else if (v == 1)
        *z = &h->pts;
    else
        return 0;
    return 1;
}

#define COORD(v, i) (xdir ? (v)[i].x : (v)[i].y)

static void set_coord(vec *v, int i, int xdir, int32_t val)
{
    if (xdir)
        v[i].x = val;
    else
        v[i].y = val;
}

/* IUP: the untouched points lo..hi between touched r1 and r2, in
 * proportion where they are between them (as they were in the font),
 * else moved as the nearer one */
static void iup_interp(zone *z, int xdir, int lo, int hi, int r1, int r2)
{
    if (lo > hi || r1 >= z->n || r2 >= z->n)
        return;
    int32_t org1 = COORD(z->org, r1), org2 = COORD(z->org, r2);
    int32_t cur1 = COORD(z->cur, r1), cur2 = COORD(z->cur, r2);
    int32_t orus1 = COORD(z->orus, r1), orus2 = COORD(z->orus, r2);
    if (orus1 > orus2) {
        int32_t t;
        t = org1; org1 = org2; org2 = t;
        t = cur1; cur1 = cur2; cur2 = t;
        t = orus1; orus1 = orus2; orus2 = t;
    }
    int32_t d1 = cur1 - org1, d2 = cur2 - org2;
    int valid = 0;
    int32_t sc = 0;
    for (int i = lo; i <= hi; i++) {
        int32_t x = COORD(z->org, i);
        if (x <= org1) {
            x += d1;
        } else if (x >= org2) {
            x += d2;
        } else if (cur1 == cur2 || orus1 == orus2) {
            x = cur1;
        } else {
            if (!valid) {
                sc = divfix(cur2 - cur1, orus2 - orus1);
                valid = 1;
            }
            x = cur1 + mulfix(COORD(z->orus, i) - orus1, sc);
        }
        set_coord(z->cur, i, xdir, x);
    }
}

static void ins_iup(tthint *h, int xdir)
{
    zone *z = &h->pts;
    uint8_t mask = xdir ? TAG_TX : TAG_TY;
    int p = 0;
    for (int c = 0; c < z->ncont; c++) {
        int end = z->ends[c], first = p;
        if (end >= z->n)
            end = z->n - 1;
        while (p <= end && !(z->tag[p] & mask))
            p++;
        if (p <= end) {
            int first_t = p, cur_t = p;
            for (p++; p <= end; p++)
                if (z->tag[p] & mask) {
                    iup_interp(z, xdir, cur_t + 1, p - 1, cur_t, p);
                    cur_t = p;
                }
            if (cur_t == first_t) {         /* one touched: the rest shifted */
                int32_t delta = COORD(z->cur, cur_t) - COORD(z->org, cur_t);
                if (delta)
                    for (int i = first; i <= end; i++)
                        if (i != cur_t)
                            set_coord(z->cur, i, xdir, COORD(z->cur, i) + delta);
            } else {                        /* round the contour's end */
                iup_interp(z, xdir, cur_t + 1, end, cur_t, first_t);
                if (first_t > first)
                    iup_interp(z, xdir, first, first_t - 1, cur_t, first_t);
            }
        }
        p = end + 1;
    }
}

#undef COORD

/* SHP, SHC, SHZ: the reference point's displacement */
static int displacement(tthint *h, zone **z, int *ref, int32_t *dx, int32_t *dy)
{
    zone *zp;
    int p;
    if (h->opc_now & 1) {
        zp = h->zp0;
        p = h->gs.rp1;
    } else {
        zp = h->zp1;
        p = h->gs.rp2;
    }
    if (BAD_PT(zp, p))
        return 0;
    *z = zp;
    *ref = p;
    F26 d = project(h, zp->cur[p].x - zp->org[p].x, zp->cur[p].y - zp->org[p].y);
    *dx = muldiv(d, h->gs.fv[0], h->fdotp);
    *dy = muldiv(d, h->gs.fv[1], h->fdotp);
    return 1;
}

static void move_zp2(tthint *h, int p, int32_t dx, int32_t dy, int touch)
{
    if (h->gs.fv[0]) {
        h->zp2->cur[p].x += dx;
        if (touch)
            h->zp2->tag[p] |= TAG_TX;
    }
    if (h->gs.fv[1]) {
        h->zp2->cur[p].y += dy;
        if (touch)
            h->zp2->tag[p] |= TAG_TY;
    }
}

static int32_t read_cvt(tthint *h, int32_t i)
{
    if (i < 0 || i >= h->ncvt) {
        h->error = 1;
        return 0;
    }
    return h->cvt[i];
}

static void delta_p(tthint *h, int32_t base, int32_t *sp)
{
    int32_t n = h->stack[--*sp];
    if (n < 0 || n > *sp / 2 + 1) {
        h->error = 1;
        return;
    }
    for (int32_t k = 0; k < n; k++) {
        if (*sp < 2) {
            h->error = 1;
            return;
        }
        int32_t p = h->stack[--*sp], a = h->stack[--*sp];
        if (BAD_PT(h->zp0, p))
            continue;
        int32_t ppem = ((a & 0xf0) >> 4) + h->gs.delta_base + base;
        if (ppem != h->ppem)
            continue;
        int32_t m = (a & 0xf) - 8;
        if (m >= 0)
            m++;
        move_point(h, h->zp0, p, m * (64 >> h->gs.delta_shift));
    }
}

static void delta_c(tthint *h, int32_t base, int32_t *sp)
{
    int32_t n = h->stack[--*sp];
    if (n < 0 || n > *sp / 2 + 1) {
        h->error = 1;
        return;
    }
    for (int32_t k = 0; k < n; k++) {
        if (*sp < 2) {
            h->error = 1;
            return;
        }
        int32_t c = h->stack[--*sp], a = h->stack[--*sp];
        if (c < 0 || c >= h->ncvt)
            continue;
        int32_t ppem = ((a & 0xf0) >> 4) + h->gs.delta_base + base;
        if (ppem != h->ppem)
            continue;
        int32_t m = (a & 0xf) - 8;
        if (m >= 0)
            m++;
        h->cvt[c] += m * (64 >> h->gs.delta_shift);
    }
}

static void exec_one(tthint *h, unsigned op, int32_t *args, int32_t *sp);

/* run the range's code from its start */
static void run(tthint *h, int range)
{
    h->range = range;
    h->ip = 0;
    h->top = 0;
    h->ncalls = 0;
    h->error = 0;
    h->budget = MAX_RUN;
    h->zp0 = h->zp1 = h->zp2 = &h->pts;
    h->gs.gep0 = h->gs.gep1 = h->gs.gep2 = 1;
    h->gs.pv[0] = h->gs.fv[0] = h->gs.dv[0] = 0x4000;
    h->gs.pv[1] = h->gs.fv[1] = h->gs.dv[1] = 0;
    h->gs.round_state = R_GRID;
    h->gs.loop = 1;
    compute_fdotp(h);
    while (!h->error) {
        const uint8_t *c = h->code[h->range];
        int32_t len = h->len[h->range];
        if (h->ip >= len) {
            if (h->ncalls)          /* a function without its ENDF */
                h->error = 1;
            break;
        }
        if (--h->budget < 0) {
            h->error = 1;
            break;
        }
        unsigned op = c[h->ip];
        int32_t n = ins_len(c, h->ip, len);
        if (n < 0) {
            h->error = 1;
            break;
        }
        if (!defined_op(op)) {      /* an IDEF's, else none */
            int k = 0;
            while (k < h->nidefs && h->idefs[k].opc != (int)op)
                k++;
            if (k == h->nidefs || !h->idefs[k].d.active || h->ncalls == MAX_CALLS) {
                h->error = 1;
                break;
            }
            call *cl = &h->calls[h->ncalls++];
            cl->range = h->range;
            cl->ret = h->ip + 1;
            cl->start = h->idefs[k].d.start;
            cl->end = h->idefs[k].d.end;
            cl->count = 1;
            h->range = h->idefs[k].d.range;
            h->ip = h->idefs[k].d.start;
            continue;
        }
        int pops = popush[op] >> 4, pushes = popush[op] & 15;
        if (h->top < pops) {
            h->error = 1;
            break;
        }
        int32_t sp = h->top - pops;
        if (sp + pushes > h->stack_max) {
            h->error = 1;
            break;
        }
        h->opc_now = op;
        h->step = 1;
        h->new_top = sp + pushes;
        exec_one(h, op, h->stack + sp, &sp);
        if (h->error)
            break;
        h->top = h->new_top;
        if (h->step)
            h->ip += n;
    }
}

/* the loop instructions' points, popped one by one */
#define LOOP_POP(var)                                       \
    if (*sp < 1) {                                          \
        h->error = 1;                                       \
        return;                                             \
    }                                                       \
    int32_t var = h->stack[--*sp]

static void set_vectors(tthint *h, int16_t *v, int which)
{
    if (which & 1) { h->gs.pv[0] = v[0]; h->gs.pv[1] = v[1]; }
    if (which & 2) { h->gs.fv[0] = v[0]; h->gs.fv[1] = v[1]; }
    if (which & 4) { h->gs.dv[0] = v[0]; h->gs.dv[1] = v[1]; }
    compute_fdotp(h);
}

/* SPVTL, SFVTL: the line from zp2's p2 to zp1's p1 (perpendicular: odd) */
static void vector_to_line(tthint *h, int32_t a1, int32_t a2, int16_t out[2], unsigned op)
{
    if (BAD_PT(h->zp1, a2) || BAD_PT(h->zp2, a1)) {
        h->error = 1;
        return;
    }
    int32_t A = h->zp1->cur[a2].x - h->zp2->cur[a1].x;
    int32_t B = h->zp1->cur[a2].y - h->zp2->cur[a1].y;
    if (!A && !B) {
        A = 0x4000;
        op = 0;
    }
    if (op & 1) {
        int32_t C = B;
        B = A;
        A = -C;
    }
    normalize(A, B, out);
}

static void exec_one(tthint *h, unsigned op, int32_t *args, int32_t *sp)
{
    switch (op) {
    case 0x00: case 0x01: {                                 /* SVTCA */
        int16_t v[2] = { (int16_t)(op & 1 ? 0x4000 : 0), (int16_t)(op & 1 ? 0 : 0x4000) };
        set_vectors(h, v, 7);
        break;
    }
    case 0x02: case 0x03: {                                 /* SPVTCA */
        int16_t v[2] = { (int16_t)(op & 1 ? 0x4000 : 0), (int16_t)(op & 1 ? 0 : 0x4000) };
        set_vectors(h, v, 5);
        break;
    }
    case 0x04: case 0x05: {                                 /* SFVTCA */
        int16_t v[2] = { (int16_t)(op & 1 ? 0x4000 : 0), (int16_t)(op & 1 ? 0 : 0x4000) };
        set_vectors(h, v, 2);
        break;
    }
    case 0x06: case 0x07: {                                 /* SPVTL */
        int16_t v[2] = { h->gs.pv[0], h->gs.pv[1] };
        vector_to_line(h, args[1], args[0], v, op);
        set_vectors(h, v, 5);
        break;
    }
    case 0x08: case 0x09: {                                 /* SFVTL */
        int16_t v[2] = { h->gs.fv[0], h->gs.fv[1] };
        vector_to_line(h, args[1], args[0], v, op);
        set_vectors(h, v, 2);
        break;
    }
    case 0x0a: case 0x0b: {                                 /* SPVFS, SFVFS */
        int16_t v[2] = { 0, 0 };
        if (op == 0x0a) { v[0] = h->gs.pv[0]; v[1] = h->gs.pv[1]; }
        else { v[0] = h->gs.fv[0]; v[1] = h->gs.fv[1]; }
        normalize((int16_t)args[0], (int16_t)args[1], v);
        set_vectors(h, v, op == 0x0a ? 5 : 2);
        break;
    }
    case 0x0c:                                              /* GPV */
        args[0] = h->gs.pv[0];
        args[1] = h->gs.pv[1];
        break;
    case 0x0d:                                              /* GFV */
        args[0] = h->gs.fv[0];
        args[1] = h->gs.fv[1];
        break;
    case 0x0e:                                              /* SFVTPV */
        set_vectors(h, h->gs.pv, 2);
        break;
    case 0x0f: {                                            /* ISECT */
        int32_t p = args[0], a0 = args[1], a1 = args[2], b0 = args[3], b1 = args[4];
        if (BAD_PT(h->zp2, p) || BAD_PT(h->zp1, a0) || BAD_PT(h->zp1, a1) ||
            BAD_PT(h->zp0, b0) || BAD_PT(h->zp0, b1))
            FAIL();
        int32_t dbx = h->zp0->cur[b1].x - h->zp0->cur[b0].x, dby = h->zp0->cur[b1].y - h->zp0->cur[b0].y;
        int32_t dax = h->zp1->cur[a1].x - h->zp1->cur[a0].x, day = h->zp1->cur[a1].y - h->zp1->cur[a0].y;
        int32_t dx = h->zp0->cur[b0].x - h->zp1->cur[a0].x, dy = h->zp0->cur[b0].y - h->zp1->cur[a0].y;
        int32_t disc = muldiv(dax, -dby, 0x40) + muldiv(day, dbx, 0x40);
        int32_t dotp = muldiv(dax, dbx, 0x40) + muldiv(day, dby, 0x40);
        if (19 * (int64_t)(disc < 0 ? -disc : disc) > (dotp < 0 ? -(int64_t)dotp : dotp)) {
            int32_t val = muldiv(dx, -dby, 0x40) + muldiv(dy, dbx, 0x40);
            h->zp2->cur[p].x = h->zp1->cur[a0].x + muldiv(val, dax, disc);
            h->zp2->cur[p].y = h->zp1->cur[a0].y + muldiv(val, day, disc);
        } else {
            h->zp2->cur[p].x = (h->zp1->cur[a0].x + h->zp1->cur[a1].x + h->zp0->cur[b0].x +
                                h->zp0->cur[b1].x) / 4;
            h->zp2->cur[p].y = (h->zp1->cur[a0].y + h->zp1->cur[a1].y + h->zp0->cur[b0].y +
                                h->zp0->cur[b1].y) / 4;
        }
        h->zp2->tag[p] |= TAG_TX | TAG_TY;
        break;
    }
    case 0x10: h->gs.rp0 = args[0]; break;                  /* SRP0 */
    case 0x11: h->gs.rp1 = args[0]; break;                  /* SRP1 */
    case 0x12: h->gs.rp2 = args[0]; break;                  /* SRP2 */
    case 0x13:                                              /* SZP0 */
        if (!zone_of(h, args[0], &h->zp0)) FAIL();
        h->gs.gep0 = args[0];
        break;
    case 0x14:                                              /* SZP1 */
        if (!zone_of(h, args[0], &h->zp1)) FAIL();
        h->gs.gep1 = args[0];
        break;
    case 0x15:                                              /* SZP2 */
        if (!zone_of(h, args[0], &h->zp2)) FAIL();
        h->gs.gep2 = args[0];
        break;
    case 0x16:                                              /* SZPS */
        if (!zone_of(h, args[0], &h->zp0)) FAIL();
        h->zp1 = h->zp2 = h->zp0;
        h->gs.gep0 = h->gs.gep1 = h->gs.gep2 = args[0];
        break;
    case 0x17:                                              /* SLOOP */
        if (args[0] < 0) FAIL();
        h->gs.loop = args[0] > 0xffff ? 0xffff : args[0];
        break;
    case 0x18: h->gs.round_state = R_GRID; break;           /* RTG */
    case 0x19: h->gs.round_state = R_HALF; break;           /* RTHG */
    case 0x1a: h->gs.min_dist = args[0]; break;             /* SMD */
    case 0x1b:                                              /* ELSE: past its EIF */
        if (!skip_to(h, 0)) FAIL();
        break;
    case 0x1c:                                              /* JMPR */
    jump:
        if (!args[0] && !*sp) FAIL();                         /* forever */
        h->ip += args[0];
        if (h->ip < 0 || h->ip > h->len[h->range]) FAIL();
        h->step = 0;
        break;
    case 0x1d: h->gs.cvt_cutin = args[0]; break;            /* SCVTCI */
    case 0x1e: h->gs.sw_cutin = args[0]; break;             /* SSWCI */
    case 0x1f: h->gs.sw_value = mulfix(args[0], h->scale_now); break;   /* SSW */
    case 0x20: args[1] = args[0]; break;                    /* DUP */
    case 0x21: break;                                       /* POP */
    case 0x22: h->new_top = 0; break;                       /* CLEAR */
    case 0x23: {                                            /* SWAP */
        int32_t t = args[0];
        args[0] = args[1];
        args[1] = t;
        break;
    }
    case 0x24: args[0] = h->top; break;                     /* DEPTH */
    case 0x25: {                                            /* CINDEX */
        int32_t k = args[0];
        if (k <= 0 || k > *sp) FAIL();
        args[0] = h->stack[*sp - k];
        break;
    }
    case 0x26: {                                            /* MINDEX */
        int32_t k = args[0];
        if (k <= 0 || k > *sp) FAIL();
        int32_t v = h->stack[*sp - k];
        memmove(&h->stack[*sp - k], &h->stack[*sp - k + 1], sizeof(int32_t) * (size_t)(k - 1));
        h->stack[*sp - 1] = v;
        break;
    }
    case 0x27: {                                            /* ALIGNPTS */
        int32_t p1 = args[0], p2 = args[1];
        if (BAD_PT(h->zp1, p1) || BAD_PT(h->zp0, p2)) FAIL();
        F26 d = project(h, h->zp0->cur[p2].x - h->zp1->cur[p1].x,
                        h->zp0->cur[p2].y - h->zp1->cur[p1].y) / 2;
        move_point(h, h->zp1, p1, d);
        move_point(h, h->zp0, p2, -d);
        break;
    }
    case 0x29: {                                            /* UTP */
        int32_t p = args[0];
        if (BAD_PT(h->zp0, p)) FAIL();
        if (h->gs.fv[0]) h->zp0->tag[p] &= (uint8_t)~TAG_TX;
        if (h->gs.fv[1]) h->zp0->tag[p] &= (uint8_t)~TAG_TY;
        break;
    }
    case 0x2a: case 0x2b: {                                 /* LOOPCALL, CALL */
        int32_t fn = op == 0x2b ? args[0] : args[1], count = op == 0x2b ? 1 : args[0];
        fdef *d = find_fdef(h, fn);
        if (!d || h->ncalls == MAX_CALLS) FAIL();
        if (count <= 0)
            break;
        call *cl = &h->calls[h->ncalls++];
        cl->range = h->range;
        cl->ret = h->ip + 1;
        cl->start = d->start;
        cl->end = d->end;
        cl->count = count;
        h->range = d->range;
        h->ip = d->start;
        h->step = 0;
        break;
    }
    case 0x2c: {                                            /* FDEF */
        int32_t fn = args[0];
        if (fn < 0 || fn > 0xffff) FAIL();
        if (fn >= h->nfdefs) {
            int n = fn + 16;
            fdef *d = (fdef *)win_alloc(sizeof(fdef) * (size_t)n);
            if (!d) FAIL();
            memset(d, 0, sizeof(fdef) * (size_t)n);
            if (h->fdefs) {
                memcpy(d, h->fdefs, sizeof(fdef) * (size_t)h->nfdefs);
                win_free(h->fdefs);
            }
            h->fdefs = d;
            h->nfdefs = n;
        }
        fdef *d = &h->fdefs[fn];
        d->range = h->range;
        d->start = h->ip + 1;
        if (!skip_to(h, -1)) FAIL();
        d->end = h->ip;
        d->active = 1;
        break;
    }
    case 0x2d: {                                            /* ENDF */
        if (!h->ncalls) FAIL();
        call *cl = &h->calls[h->ncalls - 1];
        if (--cl->count > 0) {                              /* LOOPCALL: again */
            h->ip = cl->start;
        } else {
            h->range = cl->range;
            h->ip = cl->ret;
            h->ncalls--;
        }
        h->step = 0;
        break;
    }
    case 0x2e: case 0x2f: {                                 /* MDAP */
        int32_t p = args[0];
        if (BAD_PT(h->zp0, p)) FAIL();
        F26 d = 0;
        if (op & 1) {
            F26 cur = project(h, h->zp0->cur[p].x, h->zp0->cur[p].y);
            d = round_(h, cur) - cur;
        }
        move_point(h, h->zp0, p, d);
        h->gs.rp0 = h->gs.rp1 = p;
        break;
    }
    case 0x30: case 0x31:                                   /* IUP */
        ins_iup(h, op & 1);
        break;
    case 0x32: case 0x33: {                                 /* SHP */
        zone *z;
        int ref;
        int32_t dx, dy;
        if (!displacement(h, &z, &ref, &dx, &dy)) FAIL();
        for (; h->gs.loop > 0; h->gs.loop--) {
            LOOP_POP(p);
            if (BAD_PT(h->zp2, p)) FAIL();
            move_zp2(h, p, dx, dy, 1);
        }
        h->gs.loop = 1;
        h->new_top = *sp;
        break;
    }
    case 0x34: case 0x35: {                                 /* SHC */
        zone *z;
        int ref;
        int32_t dx, dy;
        int32_t c = args[0];
        if (c < 0 || c >= h->zp2->ncont) FAIL();
        if (!displacement(h, &z, &ref, &dx, &dy)) FAIL();
        int start = c ? h->zp2->ends[c - 1] + 1 : 0, limit = h->zp2->ends[c] + 1;
        for (int i = start; i < limit && i < h->zp2->n; i++)
            if (z != h->zp2 || ref != i)
                move_zp2(h, i, dx, dy, 1);
        break;
    }
    case 0x36: case 0x37: {                                 /* SHZ */
        zone *z, *target;
        int ref;
        int32_t dx, dy;
        if (!zone_of(h, args[0], &target)) FAIL();
        if (!displacement(h, &z, &ref, &dx, &dy)) FAIL();
        /* the phantom points stay */
        int limit = target->ncont ? target->ends[target->ncont - 1] + 1 : target->n;
        zone *keep = h->zp2;
        h->zp2 = target;
        for (int i = 0; i < limit && i < target->n; i++)
            if (z != target || ref != i)
                move_zp2(h, i, dx, dy, 0);
        h->zp2 = keep;
        break;
    }
    case 0x38: {                                            /* SHPIX */
        int32_t dx = mulfix14(args[0], h->gs.fv[0]), dy = mulfix14(args[0], h->gs.fv[1]);
        for (; h->gs.loop > 0; h->gs.loop--) {
            LOOP_POP(p);
            if (BAD_PT(h->zp2, p)) FAIL();
            move_zp2(h, p, dx, dy, 1);
        }
        h->gs.loop = 1;
        h->new_top = *sp;
        break;
    }
    case 0x39: {                                            /* IP */
        int twilight = !h->gs.gep0 || !h->gs.gep1 || !h->gs.gep2;
        if (BAD_PT(h->zp0, h->gs.rp1)) FAIL();
        const vec *ob = twilight ? &h->zp0->org[h->gs.rp1] : &h->zp0->orus[h->gs.rp1];
        const vec *cb = &h->zp0->cur[h->gs.rp1];
        int32_t old_range = 0, cur_range = 0;
        if (!BAD_PT(h->zp1, h->gs.rp2)) {
            const vec *o2 = twilight ? &h->zp1->org[h->gs.rp2] : &h->zp1->orus[h->gs.rp2];
            old_range = dualproj(h, o2->x - ob->x, o2->y - ob->y);    /* font units: a ratio */
            cur_range = project(h, h->zp1->cur[h->gs.rp2].x - cb->x,
                                h->zp1->cur[h->gs.rp2].y - cb->y);
        }
        for (; h->gs.loop > 0; h->gs.loop--) {
            LOOP_POP(p);
            if (BAD_PT(h->zp2, p))
                continue;
            const vec *o = twilight ? &h->zp2->org[p] : &h->zp2->orus[p];
            int32_t org_d = dualproj(h, o->x - ob->x, o->y - ob->y);
            int32_t cur_d = project(h, h->zp2->cur[p].x - cb->x, h->zp2->cur[p].y - cb->y);
            /* the references level (Windows' way): the point as far as it was */
            int32_t new_d = org_d ? (old_range ? muldiv(org_d, cur_range, old_range) : org_d) : 0;
            move_point(h, h->zp2, p, new_d - cur_d);
        }
        h->gs.loop = 1;
        h->new_top = *sp;
        break;
    }
    case 0x3a: case 0x3b: {                                 /* MSIRP */
        int32_t p = args[0];
        if (BAD_PT(h->zp1, p) || BAD_PT(h->zp0, h->gs.rp0)) FAIL();
        if (!h->gs.gep1) {                                  /* the twilight zone's */
            h->zp1->org[p] = h->zp0->org[h->gs.rp0];
            move_orig(h, h->zp1, p, args[1]);
            h->zp1->cur[p] = h->zp1->org[p];
        }
        F26 d = project(h, h->zp1->cur[p].x - h->zp0->cur[h->gs.rp0].x,
                        h->zp1->cur[p].y - h->zp0->cur[h->gs.rp0].y);
        move_point(h, h->zp1, p, args[1] - d);
        h->gs.rp1 = h->gs.rp0;
        h->gs.rp2 = p;
        if (op & 1)
            h->gs.rp0 = p;
        break;
    }
    case 0x3c: {                                            /* ALIGNRP */
        if (BAD_PT(h->zp0, h->gs.rp0)) FAIL();
        for (; h->gs.loop > 0; h->gs.loop--) {
            LOOP_POP(p);
            if (BAD_PT(h->zp1, p)) FAIL();
            F26 d = project(h, h->zp1->cur[p].x - h->zp0->cur[h->gs.rp0].x,
                            h->zp1->cur[p].y - h->zp0->cur[h->gs.rp0].y);
            move_point(h, h->zp1, p, -d);
        }
        h->gs.loop = 1;
        h->new_top = *sp;
        break;
    }
    case 0x3d: h->gs.round_state = R_DOUBLE; break;         /* RTDG */
    case 0x3e: case 0x3f: {                                 /* MIAP */
        int32_t c = args[1], p = args[0];
        if (BAD_PT(h->zp0, p)) FAIL();
        F26 d = read_cvt(h, c);
        if (h->error) return;
        if (!h->gs.gep0) {                                  /* the twilight zone's */
            h->zp0->org[p].x = mulfix14(d, h->gs.fv[0]);
            h->zp0->org[p].y = mulfix14(d, h->gs.fv[1]);
            h->zp0->cur[p] = h->zp0->org[p];
        }
        F26 org_d = project(h, h->zp0->cur[p].x, h->zp0->cur[p].y);
        if (op & 1) {
            int32_t diff = d - org_d;
            if ((diff < 0 ? -diff : diff) > h->gs.cvt_cutin)
                d = org_d;
            d = round_(h, d);
        }
        move_point(h, h->zp0, p, d - org_d);
        h->gs.rp0 = h->gs.rp1 = p;
        break;
    }
    case 0x40: case 0x41: {                                 /* NPUSHB, NPUSHW */
        const uint8_t *c = h->code[h->range] + h->ip;
        int n = c[1];
        if (h->top + n > h->stack_max) FAIL();
        for (int i = 0; i < n; i++)
            h->stack[h->top + i] = op == 0x40 ? c[2 + i] : (int16_t)tt_be16(c + 2 + 2 * i);
        h->new_top = h->top + n;
        break;
    }
    case 0x42:                                              /* WS */
        if (args[0] < 0 || args[0] >= h->nstore) FAIL();
        h->store[args[0]] = args[1];
        break;
    case 0x43:                                              /* RS */
        if (args[0] < 0 || args[0] >= h->nstore) FAIL();
        args[0] = h->store[args[0]];
        break;
    case 0x44:                                              /* WCVTP */
        if (args[0] < 0 || args[0] >= h->ncvt) FAIL();
        h->cvt[args[0]] = args[1];
        break;
    case 0x45:                                              /* RCVT */
        args[0] = read_cvt(h, args[0]);
        break;
    case 0x46: case 0x47: {                                 /* GC */
        int32_t p = args[0];
        if (BAD_PT(h->zp2, p)) FAIL();
        args[0] = op & 1 ? dualproj(h, h->zp2->org[p].x, h->zp2->org[p].y)
                         : project(h, h->zp2->cur[p].x, h->zp2->cur[p].y);
        break;
    }
    case 0x48: {                                            /* SCFS */
        int32_t p = args[0];
        if (BAD_PT(h->zp2, p)) FAIL();
        F26 k = project(h, h->zp2->cur[p].x, h->zp2->cur[p].y);
        move_point(h, h->zp2, p, args[1] - k);
        if (!h->gs.gep2)
            h->zp2->org[p] = h->zp2->cur[p];
        break;
    }
    case 0x49: case 0x4a: {                                 /* MD */
        int32_t k = args[1], l = args[0];
        F26 d = 0;
        if (!BAD_PT(h->zp0, l) && !BAD_PT(h->zp1, k)) {
            if (op & 1) {
                d = project(h, h->zp0->cur[l].x - h->zp1->cur[k].x,
                            h->zp0->cur[l].y - h->zp1->cur[k].y);
            } else if (!h->gs.gep0 || !h->gs.gep1) {
                d = dualproj(h, h->zp0->org[l].x - h->zp1->org[k].x,
                             h->zp0->org[l].y - h->zp1->org[k].y);
            } else {
                d = dualproj(h, h->zp0->orus[l].x - h->zp1->orus[k].x,
                             h->zp0->orus[l].y - h->zp1->orus[k].y);
                d = mulfix(d, h->scale_now);
            }
        }
        args[0] = d;
        break;
    }
    case 0x4b: args[0] = h->ppem; break;                    /* MPPEM */
    case 0x4c: args[0] = h->ppem * 64 * 3 / 4; break;       /* MPS: points (26.6), 96 dpi */
    case 0x4d: h->gs.auto_flip = 1; break;                  /* FLIPON */
    case 0x4e: h->gs.auto_flip = 0; break;                  /* FLIPOFF */
    case 0x4f: break;                                       /* DEBUG */
    case 0x50: args[0] = args[0] < args[1]; break;          /* LT */
    case 0x51: args[0] = args[0] <= args[1]; break;         /* LTEQ */
    case 0x52: args[0] = args[0] > args[1]; break;          /* GT */
    case 0x53: args[0] = args[0] >= args[1]; break;         /* GTEQ */
    case 0x54: args[0] = args[0] == args[1]; break;         /* EQ */
    case 0x55: args[0] = args[0] != args[1]; break;         /* NEQ */
    case 0x56: args[0] = (round_(h, args[0]) & 127) == 64; break;   /* ODD */
    case 0x57: args[0] = (round_(h, args[0]) & 127) == 0; break;    /* EVEN */
    case 0x58:                                              /* IF: else past ELSE or EIF */
        if (!args[0] && !skip_to(h, 1)) FAIL();
        break;
    case 0x59: break;                                       /* EIF */
    case 0x5a: args[0] = args[0] && args[1]; break;         /* AND */
    case 0x5b: args[0] = args[0] || args[1]; break;         /* OR */
    case 0x5c: args[0] = !args[0]; break;                   /* NOT */
    case 0x5d: case 0x71: case 0x72: {                      /* DELTAP1..3 */
        int32_t s = op == 0x5d ? 0 : op == 0x71 ? 16 : 32;
        *sp += 1;                                           /* its count, popped there */
        delta_p(h, s, sp);
        h->new_top = *sp;
        break;
    }
    case 0x5e: h->gs.delta_base = args[0]; break;           /* SDB */
    case 0x5f:                                              /* SDS */
        if (args[0] < 0 || args[0] > 6) FAIL();
        h->gs.delta_shift = args[0];
        break;
    case 0x60: args[0] += args[1]; break;                   /* ADD */
    case 0x61: args[0] -= args[1]; break;                   /* SUB */
    case 0x62:                                              /* DIV */
        if (!args[1]) FAIL();
        args[0] = muldiv_(args[0], 64, args[1], 0);
        break;
    case 0x63: args[0] = muldiv(args[0], args[1], 64); break;   /* MUL */
    case 0x64: if (args[0] < 0) args[0] = -args[0]; break;  /* ABS */
    case 0x65: args[0] = -args[0]; break;                   /* NEG */
    case 0x66: args[0] &= ~63; break;                       /* FLOOR */
    case 0x67: args[0] = (args[0] + 63) & ~63; break;       /* CEILING */
    case 0x68: case 0x69: case 0x6a: case 0x6b:             /* ROUND */
        args[0] = round_(h, args[0]);
        break;
    case 0x6c: case 0x6d: case 0x6e: case 0x6f: break;      /* NROUND */
    case 0x70:                                              /* WCVTF */
        if (args[0] < 0 || args[0] >= h->ncvt) FAIL();
        h->cvt[args[0]] = mulfix(args[1], h->scale);
        break;
    case 0x73: case 0x74: case 0x75: {                      /* DELTAC1..3 */
        int32_t s = op == 0x73 ? 0 : op == 0x74 ? 16 : 32;
        *sp += 1;
        delta_c(h, s, sp);
        h->new_top = *sp;
        break;
    }
    case 0x76: super_round(h, 0x4000, args[0]); h->gs.round_state = R_SUPER; break;
    case 0x77: super_round(h, 0x2d41, args[0]); h->gs.round_state = R_SUPER45; break;
    case 0x78:                                              /* JROT */
        if (args[1]) goto jump;
        break;
    case 0x79:                                              /* JROF */
        if (!args[1]) goto jump;
        break;
    case 0x7a: h->gs.round_state = R_OFF; break;            /* ROFF */
    case 0x7c: h->gs.round_state = R_UP; break;             /* RUTG */
    case 0x7d: h->gs.round_state = R_DOWN; break;           /* RDTG */
    case 0x7e: case 0x7f: break;                            /* SANGW, AA */
    case 0x80: {                                            /* FLIPPT */
        for (; h->gs.loop > 0; h->gs.loop--) {
            LOOP_POP(p);
            if (BAD_PT(&h->pts, p)) FAIL();
            h->pts.tag[p] ^= TAG_ON;
        }
        h->gs.loop = 1;
        h->new_top = *sp;
        break;
    }
    case 0x81: case 0x82: {                                 /* FLIPRGON, FLIPRGOFF */
        int32_t k = args[1], l = args[0];
        if (BAD_PT(&h->pts, k) || BAD_PT(&h->pts, l)) FAIL();
        for (int32_t i = l; i <= k; i++)
            if (op == 0x81)
                h->pts.tag[i] |= TAG_ON;
            else
                h->pts.tag[i] &= (uint8_t)~TAG_ON;
        break;
    }
    case 0x85: {                                            /* SCANCTRL */
        int32_t a = args[0] & 0xff;
        if (a == 0xff) {
            h->gs.scan_control = 1;
            break;
        }
        if (a == 0) {
            h->gs.scan_control = 0;
            break;
        }
        if ((args[0] & 0x100) && h->ppem <= a)
            h->gs.scan_control = 1;
        if ((args[0] & 0x800) && h->ppem > a)
            h->gs.scan_control = 0;
        break;
    }
    case 0x86: case 0x87: {                                 /* SDPVTL */
        int32_t p1 = args[1], p2 = args[0];
        if (BAD_PT(h->zp1, p2) || BAD_PT(h->zp2, p1)) FAIL();
        unsigned o = op;
        int32_t A = h->zp1->org[p2].x - h->zp2->org[p1].x, B = h->zp1->org[p2].y - h->zp2->org[p1].y;
        if (!A && !B) {
            A = 0x4000;
            o = 0;
        }
        if (o & 1) {
            int32_t C = B;
            B = A;
            A = -C;
        }
        normalize(A, B, h->gs.dv);
        o = op;
        A = h->zp1->cur[p2].x - h->zp2->cur[p1].x;
        B = h->zp1->cur[p2].y - h->zp2->cur[p1].y;
        if (!A && !B) {
            A = 0x4000;
            o = 0;
        }
        if (o & 1) {
            int32_t C = B;
            B = A;
            A = -C;
        }
        normalize(A, B, h->gs.pv);
        compute_fdotp(h);
        break;
    }
    case 0x88: {                                            /* GETINFO */
        int32_t k = 0;
        if (args[0] & 1)
            k = TT_VERSION;                                 /* upright, not stretched */
        args[0] = k;
        break;
    }
    case 0x89: {                                            /* IDEF */
        int k = 0;
        while (k < h->nidefs && h->idefs[k].opc != (int)args[0])
            k++;
        if (k == h->nidefs) {
            if (h->nidefs == (int)(sizeof(h->idefs) / sizeof(h->idefs[0]))) FAIL();
            h->nidefs++;
        }
        h->idefs[k].opc = (int)(args[0] & 0xff);
        h->idefs[k].d.range = h->range;
        h->idefs[k].d.start = h->ip + 1;
        if (!skip_to(h, -1)) FAIL();
        h->idefs[k].d.end = h->ip;
        h->idefs[k].d.active = 1;
        break;
    }
    case 0x8a: {                                            /* ROLL */
        int32_t a = args[2];
        args[2] = args[0];
        args[0] = args[1];
        args[1] = a;
        break;
    }
    case 0x8b: if (args[1] > args[0]) args[0] = args[1]; break;     /* MAX */
    case 0x8c: if (args[1] < args[0]) args[0] = args[1]; break;     /* MIN */
    case 0x8d: if (args[0] >= 0) h->gs.scan_type = args[0] & 0xffff; break;  /* SCANTYPE */
    case 0x8e: {                                            /* INSTCTRL: in prep only */
        int32_t k = args[1], l = args[0];
        if (k < 1 || k > 2) FAIL();
        if (h->range != 1)
            break;
        k = 1 << (k - 1);
        h->gs.instruct_control = (h->gs.instruct_control & ~k) | (l ? k : 0);
        break;
    }
    default:
        if (op >= 0xb0 && op <= 0xb7) {                     /* PUSHB */
            const uint8_t *c = h->code[h->range] + h->ip + 1;
            for (unsigned i = 0; i <= op - 0xb0; i++)
                args[i] = c[i];
        } else if (op >= 0xb8 && op <= 0xbf) {              /* PUSHW */
            const uint8_t *c = h->code[h->range] + h->ip + 1;
            for (unsigned i = 0; i <= op - 0xb8; i++)
                args[i] = (int16_t)tt_be16(c + 2 * i);
        } else if (op >= 0xc0 && op <= 0xdf) {              /* MDRP */
            int32_t p = args[0];
            if (BAD_PT(h->zp1, p) || BAD_PT(h->zp0, h->gs.rp0)) FAIL();
            F26 org_d;
            if (!h->gs.gep0 || !h->gs.gep1) {
                org_d = dualproj(h, h->zp1->org[p].x - h->zp0->org[h->gs.rp0].x,
                                 h->zp1->org[p].y - h->zp0->org[h->gs.rp0].y);
            } else {
                org_d = dualproj(h, h->zp1->orus[p].x - h->zp0->orus[h->gs.rp0].x,
                                 h->zp1->orus[p].y - h->zp0->orus[h->gs.rp0].y);
                org_d = mulfix(org_d, h->scale_now);
            }
            int32_t diff = org_d - h->gs.sw_value;
            if ((diff < 0 ? -diff : diff) < h->gs.sw_cutin)
                org_d = org_d >= 0 ? h->gs.sw_value : -h->gs.sw_value;
            F26 d = op & 4 ? round_(h, org_d) : org_d;
            if (op & 8) {
                if (org_d >= 0) {
                    if (d < h->gs.min_dist)
                        d = h->gs.min_dist;
                } else if (d > -h->gs.min_dist) {
                    d = -h->gs.min_dist;
                }
            }
            F26 cur_d = project(h, h->zp1->cur[p].x - h->zp0->cur[h->gs.rp0].x,
                                h->zp1->cur[p].y - h->zp0->cur[h->gs.rp0].y);
            move_point(h, h->zp1, p, d - cur_d);
            h->gs.rp1 = h->gs.rp0;
            h->gs.rp2 = p;
            if (op & 16)
                h->gs.rp0 = p;
        } else if (op >= 0xe0) {                            /* MIRP */
            int32_t p = args[0], c = args[1] + 1;
            if (BAD_PT(h->zp1, p) || BAD_PT(h->zp0, h->gs.rp0) || c < 0 || c > h->ncvt) FAIL();
            F26 cvt_d = c ? h->cvt[c - 1] : 0;
            int32_t diff = cvt_d - h->gs.sw_value;
            if ((diff < 0 ? -diff : diff) < h->gs.sw_cutin)
                cvt_d = cvt_d >= 0 ? h->gs.sw_value : -h->gs.sw_value;
            if (!h->gs.gep1) {                              /* the twilight zone's */
                h->zp1->org[p].x = h->zp0->org[h->gs.rp0].x + mulfix14(cvt_d, h->gs.fv[0]);
                h->zp1->org[p].y = h->zp0->org[h->gs.rp0].y + mulfix14(cvt_d, h->gs.fv[1]);
                h->zp1->cur[p] = h->zp1->org[p];
            }
            F26 org_d = dualproj(h, h->zp1->org[p].x - h->zp0->org[h->gs.rp0].x,
                                 h->zp1->org[p].y - h->zp0->org[h->gs.rp0].y);
            F26 cur_d = project(h, h->zp1->cur[p].x - h->zp0->cur[h->gs.rp0].x,
                                h->zp1->cur[p].y - h->zp0->cur[h->gs.rp0].y);
            if (h->gs.auto_flip && (org_d ^ cvt_d) < 0)
                cvt_d = -cvt_d;
            F26 d;
            if (op & 4) {
                if (h->gs.gep0 == h->gs.gep1) {
                    diff = cvt_d - org_d;
                    if ((diff < 0 ? -diff : diff) > h->gs.cvt_cutin)
                        cvt_d = org_d;
                }
                d = round_(h, cvt_d);
            } else {
                d = cvt_d;
            }
            if (op & 8) {
                if (org_d >= 0) {
                    if (d < h->gs.min_dist)
                        d = h->gs.min_dist;
                } else if (d > -h->gs.min_dist) {
                    d = -h->gs.min_dist;
                }
            }
            move_point(h, h->zp1, p, d - cur_d);
            h->gs.rp1 = h->gs.rp0;
            if (op & 16)
                h->gs.rp0 = p;
            h->gs.rp2 = p;
        } else {
            FAIL();
        }
        break;
    }
}

/* ---------------------------------------------------------------- a size */

static const gstate default_gs = {
    .pv = { 0x4000, 0 }, .fv = { 0x4000, 0 }, .dv = { 0x4000, 0 },
    .loop = 1, .min_dist = 64, .round_state = R_GRID, .auto_flip = 1, .cvt_cutin = 68,
    .delta_base = 9, .delta_shift = 3, .gep0 = 1, .gep1 = 1, .gep2 = 1,
};

static int zone_alloc(zone *z, int n, int ncont)
{
    memset(z, 0, sizeof(*z));
    size_t sz = (sizeof(vec) * 3 + 1) * (size_t)(n ? n : 1) + sizeof(uint16_t) * (size_t)(ncont + 1);
    uint8_t *m = (uint8_t *)win_alloc(sz);
    if (!m)
        return 0;
    memset(m, 0, sz);
    z->org = (vec *)m;
    z->cur = z->org + n;
    z->orus = z->cur + n;
    z->ends = (uint16_t *)(z->orus + n);
    z->tag = (uint8_t *)(z->ends + ncont + 1);
    z->n = n;
    z->ncont = ncont;
    return 1;
}

static void zone_free(zone *z)
{
    win_free(z->org);
    memset(z, 0, sizeof(*z));
}

void tt_hint_free(tthint *h)
{
    if (!h)
        return;
    win_free(h->cvt);
    win_free(h->fdefs);
    win_free(h->stack);
    win_free(h->tw0);
    zone_free(&h->tw);
    win_free(h);
}

tthint *tt_hint_new(const ttfont *f, int ppem)
{
    tthint *h = (tthint *)win_alloc(sizeof(*h));
    if (!h)
        return NULL;
    memset(h, 0, sizeof(*h));
    h->f = f;
    h->ppem = ppem < 1 ? 1 : ppem;
    h->scale = divfix(h->ppem * 64, f->info.em > 0 ? f->info.em : 2048);
    h->scale_now = h->scale;
    uint32_t len = 0, at;
    int twilight = 0, storage = 0, funcs = 0, stack = 0;
    uint32_t mlen = 0, mat = tt_tab(f, "maxp", &mlen);
    if (mat && mlen >= 32 && tt_be32(f->d + mat) >= 0x10000u) {
        const uint8_t *m = f->d + mat;
        twilight = (int)tt_be16(m + 16);
        storage = (int)tt_be16(m + 18);
        funcs = (int)tt_be16(m + 20);
        stack = (int)tt_be16(m + 24);
    }
    at = tt_tab(f, "fpgm", &len);
    h->code[0] = at ? f->d + at : NULL;
    h->len[0] = at ? (int32_t)len : 0;
    at = tt_tab(f, "prep", &len);
    h->code[1] = at ? f->d + at : NULL;
    h->len[1] = at ? (int32_t)len : 0;
    at = tt_tab(f, "cvt ", &len);
    h->ncvt = at ? (int)(len / 2) : 0;
    h->nstore = storage;
    h->stack_max = stack + 32 < 64 ? 64 : stack + 32 > 0x4000 ? 0x4000 : stack + 32;
    h->nfdefs = funcs;
    h->cvt = (int32_t *)win_alloc(sizeof(int32_t) * (size_t)(2 * h->ncvt + 2 * h->nstore + 2));
    h->stack = (int32_t *)win_alloc(sizeof(int32_t) * (size_t)h->stack_max);
    h->fdefs = funcs ? (fdef *)win_alloc(sizeof(fdef) * (size_t)funcs) : NULL;
    if (!h->cvt || !h->stack || (funcs && !h->fdefs) || !zone_alloc(&h->tw, twilight, 0)) {
        tt_hint_free(h);
        return NULL;
    }
    h->tw0 = (vec *)win_alloc(sizeof(vec) * 2 * (size_t)(twilight ? twilight : 1));
    if (!h->tw0) {
        tt_hint_free(h);
        return NULL;
    }
    if (funcs)
        memset(h->fdefs, 0, sizeof(fdef) * (size_t)funcs);
    h->cvt0 = h->cvt + h->ncvt;
    h->store = h->cvt0 + h->ncvt;
    h->store0 = h->store + h->nstore;
    memset(h->store, 0, sizeof(int32_t) * (size_t)h->nstore);
    for (int i = 0; i < h->ncvt; i++)
        h->cvt[i] = mulfix(tt_bes16(f->d + at + 2 * i), h->scale);
    zone_alloc(&h->pts, 0, 0);
    h->gs = default_gs;
    h->ok = 1;
    if (h->len[0]) {                        /* the font program: its functions */
        run(h, 0);
        if (h->error)
            h->ok = 0;
    }
    if (h->ok && h->len[1]) {               /* the control value program */
        h->gs = default_gs;
        run(h, 1);
        if (h->error)
            h->ok = 0;
    }
    zone_free(&h->pts);
    /* what the control value program may not leave set (Windows' way):
     * the vectors, the reference points, the zones, the loop */
    h->gs.pv[0] = h->gs.fv[0] = h->gs.dv[0] = 0x4000;
    h->gs.pv[1] = h->gs.fv[1] = h->gs.dv[1] = 0;
    h->gs.rp0 = h->gs.rp1 = h->gs.rp2 = 0;
    h->gs.gep0 = h->gs.gep1 = h->gs.gep2 = 1;
    h->gs.loop = 1;
    if (h->gs.instruct_control & 2)         /* its settings not for the glyphs */
        h->gs = default_gs;
    h->gs0 = h->gs;
    memcpy(h->cvt0, h->cvt, sizeof(int32_t) * (size_t)h->ncvt);
    memcpy(h->store0, h->store, sizeof(int32_t) * (size_t)h->nstore);
    memcpy(h->tw0, h->tw.org, sizeof(vec) * (size_t)twilight);
    memcpy(h->tw0 + twilight, h->tw.cur, sizeof(vec) * (size_t)twilight);
    return h;
}

/* ---------------------------------------------------------------- a glyph */

/* the points a glyph and its components have: hinted, in 26.6 */
typedef struct {
    int       n, ncont, cap, ccap;
    vec      *pt;
    uint8_t  *on;
    uint16_t *ends;
    vec       pp[4];                /* the phantom points */
} gpts;

static int gpts_room(gpts *g, int n, int ncont)
{
    if (g->n + n > g->cap) {
        int cap = g->n + n + 64;
        vec *p = (vec *)win_alloc(sizeof(vec) * (size_t)cap);
        uint8_t *o = (uint8_t *)win_alloc((size_t)cap);
        if (!p || !o) {
            win_free(p);
            win_free(o);
            return 0;
        }
        if (g->n) {
            memcpy(p, g->pt, sizeof(vec) * (size_t)g->n);
            memcpy(o, g->on, (size_t)g->n);
        }
        win_free(g->pt);
        win_free(g->on);
        g->pt = p;
        g->on = o;
        g->cap = cap;
    }
    if (g->ncont + ncont > g->ccap) {
        int cap = g->ncont + ncont + 16;
        uint16_t *e = (uint16_t *)win_alloc(sizeof(uint16_t) * (size_t)cap);
        if (!e)
            return 0;
        if (g->ncont)
            memcpy(e, g->ends, sizeof(uint16_t) * (size_t)g->ncont);
        win_free(g->ends);
        g->ends = e;
        g->ccap = cap;
    }
    return 1;
}

static void gpts_free(gpts *g)
{
    win_free(g->pt);
    win_free(g->on);
    win_free(g->ends);
    memset(g, 0, sizeof(*g));
}

/* the phantom points in font units: the origin, the advance (pp1, pp2),
 * the top and the bottom (pp3, pp4) */
static void phantoms(const ttfont *f, unsigned glyph, int xmin, vec pp[4])
{
    int adv, lsb;
    tt_hmetrics(f, glyph, &adv, &lsb);
    int asc = tt_bes16(f->d + f->hhea + 4), desc = tt_bes16(f->d + f->hhea + 6);
    pp[0].x = xmin - lsb;
    pp[0].y = 0;
    pp[1].x = pp[0].x + adv;
    pp[1].y = 0;
    pp[2].x = adv / 2;
    pp[2].y = asc;
    pp[3].x = adv / 2;
    pp[3].y = desc;
}

/* the glyph program's run over zone pts (its n - 4 points and 4 phantoms) */
static void hint_zone(tthint *h, const uint8_t *ins, unsigned nins, int composite)
{
    zone *z = &h->pts;
    memcpy(z->org, z->cur, sizeof(vec) * (size_t)z->n);
    if (composite)
        memcpy(z->orus, z->cur, sizeof(vec) * (size_t)z->n);
    int n = z->n;                           /* the phantoms on the grid */
    z->cur[n - 4].x = (z->cur[n - 4].x + 32) & ~63;
    z->cur[n - 3].x = (z->cur[n - 3].x + 32) & ~63;
    z->cur[n - 2].y = (z->cur[n - 2].y + 32) & ~63;
    z->cur[n - 1].y = (z->cur[n - 1].y + 32) & ~63;
    if (!nins || !h->ok || (h->gs0.instruct_control & 1))
        return;
    h->code[2] = ins;
    h->len[2] = (int32_t)nins;
    h->gs = h->gs0;
    h->scale_now = composite ? 0x10000 : h->scale;
    run(h, 2);                              /* an error: the points as they are */
    h->code[2] = NULL;
    h->len[2] = 0;
}

static int load(tthint *h, unsigned glyph, gpts *out, int depth)
{
    const ttfont *f = h->f;
    const uint8_t *g, *end;
    vec pp[4];
    if (depth > MAX_DEPTH)
        return 0;
    if (!tt_glyph_at(f, glyph, &g, &end)) {         /* empty: its phantoms only */
        phantoms(f, glyph, 0, pp);
        for (int i = 0; i < 4; i++) {
            out->pp[i].x = mulfix(pp[i].x, h->scale);
            out->pp[i].y = mulfix(pp[i].y, h->scale);
        }
        out->pp[0].x = (out->pp[0].x + 32) & ~63;
        out->pp[1].x = (out->pp[1].x + 32) & ~63;
        return 1;
    }
    int xmin = tt_bes16(g + 2);
    phantoms(f, glyph, xmin, pp);
    if (tt_bes16(g) >= 0) {                         /* simple */
        ttsimple s;
        if (!tt_simple(g, end, &s))
            return 0;
        int n = s.npts + 4;
        if (!zone_alloc(&h->pts, n, s.ncont)) {
            tt_simple_free(&s);
            return 0;
        }
        zone *z = &h->pts;
        for (int i = 0; i < s.npts; i++) {
            z->orus[i].x = s.x[i];
            z->orus[i].y = s.y[i];
            z->tag[i] = s.on[i] & 1;
        }
        for (int i = 0; i < 4; i++)
            z->orus[s.npts + i] = pp[i];
        for (int i = 0; i < n; i++) {
            z->cur[i].x = mulfix(z->orus[i].x, h->scale);
            z->cur[i].y = mulfix(z->orus[i].y, h->scale);
        }
        memcpy(z->ends, s.ends, sizeof(uint16_t) * (size_t)s.ncont);
        hint_zone(h, s.ins, s.nins, 0);
        int ok = gpts_room(out, s.npts, s.ncont);
        if (ok) {
            int base = out->n;
            for (int i = 0; i < s.npts; i++) {
                out->pt[base + i] = z->cur[i];
                out->on[base + i] = z->tag[i] & TAG_ON;
            }
            for (int c = 0; c < s.ncont; c++)
                out->ends[out->ncont + c] = (uint16_t)(base + s.ends[c]);
            out->n += s.npts;
            out->ncont += s.ncont;
            for (int i = 0; i < 4; i++)
                out->pp[i] = z->cur[s.npts + i];
        }
        zone_free(&h->pts);
        tt_simple_free(&s);
        return ok;
    }
    /* composite: its components, each hinted and moved, then its own
     * instructions over them all */
    int first = out->n, first_c = out->ncont;
    vec cpp[4];
    for (int i = 0; i < 4; i++) {
        cpp[i].x = mulfix(pp[i].x, h->scale);
        cpp[i].y = mulfix(pp[i].y, h->scale);
    }
    const uint8_t *p = g + 10;
    unsigned flags;
    do {
        if (p + 4 > end)
            return 0;
        flags = tt_be16(p);
        unsigned part = tt_be16(p + 2);
        p += 4;
        int32_t a1, a2;
        if (flags & 1) {
            if (p + 4 > end)
                return 0;
            a1 = flags & 2 ? tt_bes16(p) : (int32_t)tt_be16(p);
            a2 = flags & 2 ? tt_bes16(p + 2) : (int32_t)tt_be16(p + 2);
            p += 4;
        } else {
            if (p + 2 > end)
                return 0;
            a1 = flags & 2 ? (int8_t)p[0] : p[0];
            a2 = flags & 2 ? (int8_t)p[1] : p[1];
            p += 2;
        }
        int32_t m[4] = { 0x10000, 0, 0, 0x10000 }, have_m = 0;  /* 16.16 */
        if (flags & 8) {
            if (p + 2 > end)
                return 0;
            m[0] = m[3] = tt_bes16(p) * 4;
            p += 2;
            have_m = 1;
        } else if (flags & 0x40) {
            if (p + 4 > end)
                return 0;
            m[0] = tt_bes16(p) * 4;
            m[3] = tt_bes16(p + 2) * 4;
            p += 4;
            have_m = 1;
        } else if (flags & 0x80) {
            if (p + 8 > end)
                return 0;
            m[0] = tt_bes16(p) * 4;
            m[1] = tt_bes16(p + 2) * 4;
            m[2] = tt_bes16(p + 4) * 4;
            m[3] = tt_bes16(p + 6) * 4;
            p += 8;
            have_m = 1;
        }
        int base = out->n;
        vec keep[4];
        memcpy(keep, out->pp, sizeof(keep));
        if (!load(h, part, out, depth + 1))
            return 0;
        if (!(flags & 0x200))                       /* USE_MY_METRICS: the part's */
            memcpy(out->pp, keep, sizeof(keep));
        else
            memcpy(cpp, out->pp, sizeof(cpp));
        if (have_m)
            for (int i = base; i < out->n; i++) {
                int32_t x = out->pt[i].x, y = out->pt[i].y;
                out->pt[i].x = mulfix(x, m[0]) + mulfix(y, m[2]);
                out->pt[i].y = mulfix(x, m[1]) + mulfix(y, m[3]);
            }
        int32_t dx, dy;
        if (flags & 2) {                            /* an offset */
            dx = mulfix(a1, h->scale);
            dy = mulfix(a2, h->scale);
            if (flags & 4) {                        /* ROUND_XY_TO_GRID */
                dx = (dx + 32) & ~63;
                dy = (dy + 32) & ~63;
            }
        } else {                                    /* a point on a point */
            int32_t k = first + a1, l = base + a2;
            if (k < first || k >= base || l >= out->n)
                return 0;
            dx = out->pt[k].x - out->pt[l].x;
            dy = out->pt[k].y - out->pt[l].y;
        }
        for (int i = base; i < out->n; i++) {
            out->pt[i].x += dx;
            out->pt[i].y += dy;
        }
    } while (flags & 0x20);
    unsigned nins = 0;
    const uint8_t *ins = NULL;
    if ((flags & 0x100) && p + 2 <= end) {          /* WE_HAVE_INSTRUCTIONS */
        nins = tt_be16(p);
        ins = p + 2;
        if (ins + nins > end)
            nins = 0;
    }
    int n = out->n - first, nc = out->ncont - first_c;
    if (!nins) {                                    /* none: its phantoms as they are */
        memcpy(out->pp, cpp, sizeof(cpp));
        return 1;
    }
    if (!zone_alloc(&h->pts, n + 4, nc))
        return 0;
    zone *z = &h->pts;
    for (int i = 0; i < n; i++) {
        z->cur[i] = out->pt[first + i];
        z->tag[i] = out->on[first + i];
    }
    for (int i = 0; i < 4; i++)
        z->cur[n + i] = cpp[i];
    for (int c = 0; c < nc; c++)
        z->ends[c] = (uint16_t)(out->ends[first_c + c] - first);
    hint_zone(h, ins, nins, 1);
    for (int i = 0; i < n; i++) {
        out->pt[first + i] = z->cur[i];
        out->on[first + i] = z->tag[i] & TAG_ON;
    }
    for (int i = 0; i < 4; i++)
        out->pp[i] = z->cur[n + i];
    zone_free(&h->pts);
    return 1;
}

int tt_hint_ppem(const tthint *h) { return h ? h->ppem : 0; }

void tt_path_free(ttpath *p)
{
    win_free(p->x);
    memset(p, 0, sizeof(*p));
}

int tt_hint_path(tthint *h, unsigned glyph, ttpath *out, int *advance, int *dropout)
{
    gpts g;
    memset(&g, 0, sizeof(g));
    memset(out, 0, sizeof(*out));
    *advance = 0;
    *dropout = TT_DROPOUT_NONE;
    if (!h)
        return 0;
    /* the state the control value program left */
    memcpy(h->cvt, h->cvt0, sizeof(int32_t) * (size_t)h->ncvt);
    memcpy(h->store, h->store0, sizeof(int32_t) * (size_t)h->nstore);
    memcpy(h->tw.org, h->tw0, sizeof(vec) * (size_t)h->tw.n);
    memcpy(h->tw.cur, h->tw0 + h->tw.n, sizeof(vec) * (size_t)h->tw.n);
    memset(h->tw.tag, 0, (size_t)h->tw.n);
    h->gs = h->gs0;
    if (!load(h, glyph, &g, 0)) {
        gpts_free(&g);
        return 0;
    }
    int32_t ox = g.pp[0].x;
    *advance = (g.pp[1].x - g.pp[0].x + 32) >> 6;
    int hd = tt_hdmx(h->f, h->ppem, glyph);
    if (hd >= 0)
        *advance = hd;
    if (h->gs.scan_control)
        *dropout = h->gs.scan_type == 0 ? TT_DROPOUT_SIMPLE
                 : h->gs.scan_type == 1 ? TT_DROPOUT_SIMPLE_NOSTUBS
                 : h->gs.scan_type == 4 ? TT_DROPOUT_SMART
                 : h->gs.scan_type == 5 ? TT_DROPOUT_SMART_NOSTUBS
                 : TT_DROPOUT_NONE;
    if (g.n) {
        size_t sz = (sizeof(int32_t) * 2 + 1) * (size_t)g.n + sizeof(uint16_t) * (size_t)g.ncont;
        uint8_t *m = (uint8_t *)win_alloc(sz);
        if (!m) {
            gpts_free(&g);
            return 0;
        }
        out->x = (int32_t *)m;
        out->y = out->x + g.n;
        out->ends = (uint16_t *)(out->y + g.n);
        out->on = (uint8_t *)(out->ends + g.ncont);
        out->n = g.n;
        out->ncont = g.ncont;
        for (int i = 0; i < g.n; i++) {
            out->x[i] = g.pt[i].x - ox;
            out->y[i] = g.pt[i].y;
            out->on[i] = g.on[i];
        }
        memcpy(out->ends, g.ends, sizeof(uint16_t) * (size_t)g.ncont);
    }
    gpts_free(&g);
    return 1;
}

int tt_hint_glyph(tthint *h, unsigned glyph, tt_sink sink, void *ctx, int *advance, int *dropout)
{
    ttpath p;
    if (!tt_hint_path(h, glyph, &p, advance, dropout))
        return 0;
    int drawn = 0;
    if (p.n && sink) {
        float *px = (float *)win_alloc(sizeof(float) * 2 * (size_t)p.n);
        if (px) {
            float *py = px + p.n;
            for (int i = 0; i < p.n; i++) {
                px[i] = (float)p.x[i] / 64.0f;
                py[i] = (float)p.y[i] / 64.0f;
            }
            tt_emit(px, py, p.on, p.ends, p.ncont, p.n, sink, ctx);
            win_free(px);
            drawn = 1;
        }
    }
    tt_path_free(&p);
    return drawn || !sink;
}

