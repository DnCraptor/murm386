/*
 * hlpfile.h - a Windows 3.1 help file (.HLP), read: its internal files,
 * the topics (their text decompressed, phrase by phrase), the fonts, the
 * contexts, the keywords, the bitmaps.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINHELP_HLPFILE_H
#define WINHELP_HLPFILE_H

#include <stdint.h>
#include "windows.h"

#define HLP_NONE 0xFFFFFFFFul

typedef struct {
    char face[32];
    uint8_t attr;                   /* 1 bold, 2 italic, 4 underline, 8 strike-out */
    uint8_t half_points;
    uint8_t family;                 /* 1 modern, 2 roman, 3 swiss, 4 script, 5 decorative */
    COLORREF color;
} hlp_font;

typedef struct {
    DWORD offset;                   /* its topic offset (TOPICOFFSET) */
    DWORD pos;                      /* its header record (TOPICPOS) */
} hlp_page;

/* a bitmap: a DIB (top of the rows at the end, 4-byte rows) */
typedef struct {
    BITMAPINFO *info;               /* with its colours */
    uint8_t *bits;
    int w, h;
} hlp_bitmap;

#define HLP_CACHE 4
#define HLP_BITMAPS 64

typedef struct hlp_file {
    HFILE f;
    char path[144];
    char title[80];
    DWORD contents;                 /* a topic offset */
    uint16_t minor, flags;
    uint8_t *dir;                   /* the internal directory (a B-tree) */
    DWORD dir_len;
    /* the phrases: their text, where each starts */
    uint16_t nphrases;
    uint16_t *phr_off;
    uint8_t *phr_text;
    DWORD phr_len;
    /* |TOPIC: its 4K blocks, each 16K at most once expanded */
    DWORD topic_at, topic_len;
    int nblocks;
    struct { int blk; DWORD len; uint8_t *buf; uint32_t used; } cache[HLP_CACHE];
    uint32_t tick;
    int nfonts;
    hlp_font *fonts;
    int npages;
    hlp_page *pages;
    int nctx;
    DWORD *ctx;                     /* hash, offset pairs */
    char *config;                   /* [CONFIG]'s macros, zero-ended, one after another */
    DWORD config_len;
    hlp_bitmap bitmaps[HLP_BITMAPS];
} hlp_file;

/* the file opened (its name as WinHelp has it): NULL if it is none */
hlp_file *hlp_open(const char *path);
void hlp_close(hlp_file *h);

/* an internal file ("|TOPIC", "|bm0"): where its data starts in the file
 * and its length; 0 if there is none */
DWORD hlp_find(hlp_file *h, const char *name, DWORD *len);
/* ... read whole (malloc'd; NULL for none) */
uint8_t *hlp_load(hlp_file *h, const char *name, DWORD *len);

/* n bytes of |TOPIC from a TOPICPOS on, the blocks' headers skipped;
 * the number read */
DWORD hlp_topic_read(hlp_file *h, DWORD pos, uint8_t *dst, DWORD n);
/* a record's LinkData2 (dst: size bytes), phrases expanded */
void hlp_expand(hlp_file *h, const uint8_t *src, DWORD n, uint8_t *dst, DWORD size);

/* the page a topic offset is in (-1 for none); of a context string's
 * hash, of a context number ([MAP]), the contents' */
int hlp_page_of(hlp_file *h, DWORD offset);
DWORD hlp_hash(const char *context);
DWORD hlp_context(hlp_file *h, DWORD hash);
DWORD hlp_context_number(hlp_file *h, DWORD id);
/* the title of the page's topic (|TTLBTREE, else its header) */
void hlp_page_title(hlp_file *h, int page, char *out, int cap);

/* a bitmap (|bm<n>), or one in a topic (data of n bytes): NULL if none */
const hlp_bitmap *hlp_bitmap_of(hlp_file *h, int index);
int hlp_picture(const uint8_t *data, DWORD n, hlp_bitmap *out);
void hlp_bitmap_free(hlp_bitmap *b);

/* the keywords (|KWBTREE): each, its topics' offsets (|KWDATA) */
typedef struct {
    char word[80];
    uint16_t count;
    DWORD at;                       /* in |KWDATA */
} hlp_keyword;
int hlp_keywords(hlp_file *h, hlp_keyword **out);
int hlp_keyword_topics(hlp_file *h, const hlp_keyword *k, DWORD *out, int cap);

/* the compressed numbers of a record's LinkData1 */
long hlp_clong(const uint8_t **p);
unsigned long hlp_culong(const uint8_t **p);
int hlp_cshort(const uint8_t **p);
unsigned hlp_cushort(const uint8_t **p);

static inline uint16_t hlp_u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t hlp_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#endif
