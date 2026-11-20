/*
 * olecli.c - OLE 1.0 client documents and static DIB objects.
 * Each task owns its documents, pictures and Win16 object pointers.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "ole.h"
#include "ddeml.h"
#include "windll.h"

#define NE (__win_dll_ne)

#define DOCS 32
#define DOC_HANDLE 0x4f4c0000ul      /* "OL" and the index + 1 */

static struct {
    uint16_t task;                  /* 0: free */
    unsigned creating;
    char name[128];
} docs[DOCS];

static int doc_of(LHCLIENTDOC h)
{
    unsigned long i = (unsigned long)h - DOC_HANDLE - 1;
    return (unsigned long)h > DOC_HANDLE && i < DOCS &&
           docs[i].task && docs[i].task == (uint16_t)GetCurrentTask() ? (int)i : -1;
}

static OLESTATUS WINAPI OleRegisterClientDoc(LPCSTR cls, LPCSTR doc, LONG reserved,
                                              LHCLIENTDOC *out)
{
    (void)cls; (void)reserved;
    if (!out || !doc)
        return OLE_ERROR_ADDRESS;
    *out = 0;
    for (int i = 0; i < DOCS; i++)
        if (!docs[i].task) {
            docs[i].task = (uint16_t)GetCurrentTask();
            lstrcpyn(docs[i].name, doc, sizeof(docs[i].name));
            *out = (LHCLIENTDOC)(DOC_HANDLE + (unsigned long)i + 1);
            return OLE_OK;
        }
    return OLE_ERROR_MEMORY;
}

static OLESTATUS WINAPI OleRenameClientDoc(LHCLIENTDOC h, LPCSTR name)
{
    int i = doc_of(h);
    if (i < 0)
        return OLE_ERROR_HANDLE;
    if (!name)
        return OLE_ERROR_ADDRESS;
    lstrcpyn(docs[i].name, name, sizeof(docs[i].name));
    return OLE_OK;
}

/* Static pictures have no server to notify on save or revert. */
static OLESTATUS WINAPI OleRevertClientDoc(LHCLIENTDOC h)
{
    return doc_of(h) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
}

static OLESTATUS WINAPI OleSavedClientDoc(LHCLIENTDOC h)
{
    return doc_of(h) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
}

/* Pictures own their packed DIB or WMF bytes independently of the clipboard. */
enum { DATA_PICTURE, DATA_IDENTITY, DATA_NATIVE, DATA_SLOTS };

typedef struct ole_session ole_session;
static OLESTATUS close_session(OLEOBJECT *o);

struct WINN_OLEOBJECT {
    LHCLIENTDOC doc;
    HGLOBAL image, descriptor;
    HGLOBAL stream_head, stream_tail; /* loaded links/embeddings; tail may be image */
    DWORD head_bytes, tail_bytes;
    WORD type;
    HMETAFILE metafile;
    int map_mode;
    DWORD size, bits, guest;
    HGLOBAL identity, native;
    DWORD guest_data[DATA_SLOTS];
    SIZE pixels;
    LONG width, height;              /* positive HIMETRIC extents */
    BOOL writing;
    ole_session *session;
    char name[128];
};

#define OBJECTS 32
#define STATIC_FIXED_HEADER 24
#define WMF_PREFIX 8
#define PICTURE_HEADER_MAX (STATIC_FIXED_HEADER + sizeof("METAFILEPICT") + WMF_PREFIX)
#define STREAM_CHUNK 4096

static OLEOBJECT objects[OBJECTS];

static OLEOBJECT *object_of(LPOLEOBJECT object)
{
    for (int i = 0; i < OBJECTS; i++)
        if (&objects[i] == object && objects[i].doc && doc_of(objects[i].doc) >= 0)
            return &objects[i];
    return NULL;
}

static LPOLEOBJECT new_object(LHCLIENTDOC doc, LPCSTR name)
{
    for (int i = 0; i < OBJECTS; i++)
        if (!objects[i].doc) {
            objects[i] = (OLEOBJECT){ .doc = doc, .type = OT_STATIC };
            lstrcpyn(objects[i].name, name, sizeof(objects[i].name));
            return &objects[i];
        }
    return NULL;
}

static int static_protocol(LPCSTR protocol)
{
    return protocol && !lstrcmpi(protocol, "Static");
}

static UINT static_format(UINT render, OLECLIPFORMAT fmt)
{
    if (render == olerender_format)
        return (fmt == CF_DIB || fmt == CF_METAFILEPICT) && IsClipboardFormatAvailable(fmt) ?
            fmt : 0;
    if (render == olerender_draw) {
        if (IsClipboardFormatAvailable(CF_DIB))
            return CF_DIB;
        if (IsClipboardFormatAvailable(CF_BITMAP))
            return CF_BITMAP;
        if (IsClipboardFormatAvailable(CF_METAFILEPICT))
            return CF_METAFILEPICT;
    }
    return 0;
}

static HGLOBAL copy_block(const void *src, DWORD n)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
    void *out = h ? GlobalLock(h) : NULL;
    if (!out) {
        if (h)
            GlobalFree(h);
        return 0;
    }
    memcpy(out, src, n);
    GlobalUnlock(h);
    return h;
}

static HGLOBAL copy_handle(HGLOBAL source, DWORD size)
{
    const void *p = GlobalLock(source);
    if (!p)
        return 0;
    HGLOBAL copy = copy_block(p, size);
    GlobalUnlock(source);
    return copy;
}

static HGLOBAL bitmap_to_dib(HBITMAP bitmap)
{
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    if (!GetDIBits(0, bitmap, 0, 0, NULL, &bi, DIB_RGB_COLORS))
        return 0;
    LONG w = bi.bmiHeader.biWidth, h = bi.bmiHeader.biHeight;
    if (w < 1 || w > 4096 || h < 1 || h > 4096)
        return 0;
    DWORD bytes = sizeof(BITMAPINFOHEADER) + (((DWORD)w * 3 + 3) & ~3u) * (DWORD)h;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes);
    BITMAPINFO *packed = dib ? GlobalLock(dib) : NULL;
    if (packed) {
        packed->bmiHeader = bi.bmiHeader;
        packed->bmiHeader.biBitCount = 24;
        int n = GetDIBits(0, bitmap, 0, (UINT)h, (uint8_t *)packed + sizeof(BITMAPINFOHEADER),
                          packed, DIB_RGB_COLORS);
        GlobalUnlock(dib);
        if (n == h)
            return dib;
    }
    if (dib)
        GlobalFree(dib);
    return 0;
}

static uint32_t read32(const uint8_t *p);
static uint16_t read16(const uint8_t *b);
static void write32(uint8_t *p, uint32_t v);

/* OLE Bitmap16 contains a fixed header and top-down, WORD-aligned DDB
 * scanlines. Normalize to the packed DIB understood by the rest of OLECLI. */
static OLESTATUS bitmap16_to_dib(HGLOBAL source, HGLOBAL *out)
{
    const uint8_t *p = GlobalLock(source);
    DWORD size = GlobalSize(source);
    OLESTATUS status = OLE_ERROR_FORMAT;
    *out = 0;
    if (!p)
        return status;
    if (size < 10)
        goto done;
    int w = (int16_t)read16(p + 2), h = (int16_t)read16(p + 4);
    int stride = (int16_t)read16(p + 6), depth = p[9];
    if (read16(p) || p[8] != 1 || w < 1 || h < 1 || w > 4096 || h > 4096 ||
        (depth != 1 && depth != 4 && depth != 8 && depth != 24))
        goto done;
    DWORD row = ((DWORD)w * depth + 7u) / 8u;
    if (stride < (int)((row + 1u) & ~1u) || (stride & 1))
        goto done;
    DWORD pixels = (DWORD)stride * (DWORD)h;
    /* Some Win16 producers retain the four-byte bmBits placeholder. */
    DWORD at = size == 10u + pixels ? 10 : 14;
    if (size < at || pixels != size - at)
        goto done;

    DWORD pitch = (row + 3u) & ~3u;
    DWORD colors = depth <= 8 ? 1u << depth : 0;
    DWORD offset = sizeof(BITMAPINFOHEADER) + colors * sizeof(RGBQUAD);
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, offset + pitch * h);
    BITMAPINFO *info = dib ? GlobalLock(dib) : NULL;
    status = OLE_ERROR_MEMORY;
    if (info) {
        info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info->bmiHeader.biBitCount = (WORD)depth;
        BOOL palette_ok = TRUE;
        if (colors) {
            HBITMAP pixel = CreateBitmap(1, 1, 1, 1, NULL);
            palette_ok = pixel && GetDIBits(0, pixel, 0, 0, NULL, info, DIB_RGB_COLORS) != 0;
            if (pixel)
                DeleteObject(pixel);
        }
        if (palette_ok) {
            info->bmiHeader.biWidth = w;
            info->bmiHeader.biHeight = h;
            info->bmiHeader.biPlanes = 1;
            info->bmiHeader.biSizeImage = pitch * h;
            uint8_t *bits = (uint8_t *)info + offset;
            for (int y = 0; y < h; y++)
                memcpy(bits + (DWORD)(h - y - 1) * pitch, p + at + (DWORD)y * stride, row);
            *out = dib;
            status = OLE_OK;
        }
        GlobalUnlock(dib);
    }
    if (dib && !*out)
        GlobalFree(dib);
done:
    GlobalUnlock(source);
    return status;
}

/* Return the first recorded mapping and window extent and validate the
 * complete record sequence before giving a stream to the WMF player. */
static int metafile_layout(const uint8_t *p, DWORD size, int *mode, SIZE *window)
{
    *mode = 0;
    *window = (SIZE){ 0, 0 };
    for (DWORD at = 18; at <= size && size - at >= 6;) {
        DWORD words = read32(p + at);
        if (words < 3 || words > (size - at) / 2)
            return 0;
        UINT op = read16(p + at + 4);
        if (!op)
            return words == 3 && at + 6 == size;
        if (!*mode && op == META_SETMAPMODE && words >= 4) {
            int m = (int16_t)read16(p + at + 6);
            if (m >= MM_TEXT && m <= MM_ANISOTROPIC)
                *mode = m;
        }
        if (!window->cx && op == META_SETWINDOWEXT && words >= 5) {
            int x = (int16_t)read16(p + at + 8), y = (int16_t)read16(p + at + 6);
            if (x && y)
                *window = (SIZE){ x > 0 ? x : -x, y > 0 ? y : -y };
        }
        at += words * 2;
    }
    return 0;
}

static HGLOBAL metafile_bits(HMETAFILE mf, int mode)
{
    HMETAFILE copy = CopyMetaFile(mf, NULL);
    HGLOBAL bits = copy ? GetMetaFileBits(copy) : 0;
    if (copy && !bits)
        DeleteMetaFile(copy);
    if (!bits)
        return 0;
    const uint8_t *p = GlobalLock(bits);
    int recorded_mode;
    SIZE window;
    DWORD n = GlobalSize(bits);
    HGLOBAL image = 0;
    if (p) {
        if (!metafile_layout(p, n, &recorded_mode, &window) ||
            (n >= 26 && read16(p + 22) == META_SETMAPMODE && read32(p + 18) >= 4)) {
            GlobalUnlock(bits);
            return bits;
        }
        /* A stream's eight-byte prefix is reserved. Keep a clipboard
         * picture's mapping in the WMF itself so a round trip preserves it. */
        image = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 8);
        uint8_t *out = image ? GlobalLock(image) : NULL;
        if (out) {
            memcpy(out, p, 18);
            write32(out + 6, n / 2 + 4);
            if (read32(out + 12) < 4)
                write32(out + 12, 4);
            uint8_t record[] = { 4, 0, 0, 0, META_SETMAPMODE & 0xff,
                                 META_SETMAPMODE >> 8, (uint8_t)mode, 0 };
            memcpy(out + 18, record, sizeof(record));
            memcpy(out + 26, p + 18, n - 18);
            GlobalUnlock(image);
        } else if (image) {
            GlobalFree(image);
            image = 0;
        }
    }
    if (p)
        GlobalUnlock(bits);
    GlobalFree(bits);
    return image;
}

static HGLOBAL metafile_descriptor(HMETAFILE mf, LONG width, LONG height)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, sizeof(METAFILEPICT));
    METAFILEPICT *p = h ? GlobalLock(h) : NULL;
    if (!p) {
        if (h)
            GlobalFree(h);
        return 0;
    }
    p->mm = MM_ANISOTROPIC;
    p->xExt = width;
    p->yExt = height;
    p->hMF = mf;
    GlobalUnlock(h);
    return h;
}

static LONG picture_extent(int mode, int value, int dpi)
{
    static const int units[] = { 0, 0, 254, 2540, 100, 1000, 1440 };
    if (value == INT32_MIN)
        return 0;
    if (value < 0)
        value = -value;
    if (mode == MM_ANISOTROPIC || mode == MM_ISOTROPIC)
        return value;
    int per_inch = mode == MM_TEXT ? dpi : units[mode];
    return MulDiv(value, 2540, per_inch > 0 ? per_inch : 96);
}

/* No suggested size is required for an anisotropic clipboard picture. Infer
 * one from its first complete window extent, which provides an aspect ratio. */
static int metafile_window(HGLOBAL image, LONG *width, LONG *height)
{
    const uint8_t *p = GlobalLock(image);
    if (!p)
        return 0;
    int mode;
    SIZE window;
    int ok = metafile_layout(p, GlobalSize(image), &mode, &window);
    GlobalUnlock(image);
    if (!ok || !window.cx)
        return 0;
    *width = window.cx;
    *height = window.cy;
    return 1;
}

/* Takes ownership of the packed image even when creation fails. */
static OLESTATUS new_picture(HGLOBAL image, int mode, LHCLIENTDOC doc, LPCSTR name,
                             LONG w, LONG h, LPOLEOBJECT *out)
{
    SIZE pixels = {0};
    DWORD bits = 0, size = 0;
    HMETAFILE mf = 0;
    const uint8_t *data = GlobalLock(image);
    OLESTATUS status = OLE_ERROR_FORMAT;
    if (data) {
        DWORD n = GlobalSize(image);
        if (!mode) {
            size = GetPackedDIBInfoN(data, n, &pixels, &bits);
        } else if (n >= 18 && read32(data + 6) >= 9 && read32(data + 6) <= n / 2) {
            size = read32(data + 6) * 2;
            int recorded_mode;
            SIZE window;
            if (metafile_layout(data, size, &recorded_mode, &window)) {
                mode = !recorded_mode && window.cx ? MM_ANISOTROPIC : MM_TEXT;
                HGLOBAL copy = copy_block(data, size);
                if (copy) {
                    mf = SetMetaFileBits(copy);
                    if (!mf)
                        GlobalFree(copy);
                } else {
                    status = OLE_ERROR_MEMORY;
                }
            }
        }
        GlobalUnlock(image);
    }
    if (size && (!mode || mf)) {
        status = doc_of(doc) < 0 ? OLE_ERROR_HANDLE : !name ? OLE_ERROR_ADDRESS : OLE_ERROR_MEMORY;
        if (status == OLE_ERROR_MEMORY) {
            OLEOBJECT *o = new_object(doc, name);
            if (o) {
                HDC screen = (w && h) ? 0 : GetDC(0);
                int dx = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 0;
                int dy = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 0;
                if (screen)
                    ReleaseDC(0, screen);
                o->image = image;
                o->metafile = mf;
                o->map_mode = mode;
                o->size = size;
                o->bits = bits;
                o->pixels = pixels;
                o->width = w ? w : MulDiv(pixels.cx, 2540, dx > 0 ? dx : 96);
                o->height = h ? h : MulDiv(pixels.cy, 2540, dy > 0 ? dy : 96);
                *out = o;
                return OLE_OK;
            }
        }
    }
    if (mf)
        DeleteMetaFile(mf);
    GlobalFree(image);
    return status;
}

static OLESTATUS create_metafile_picture(HGLOBAL data, LHCLIENTDOC doc, LPCSTR name,
                                          LPOLEOBJECT *out)
{
    if (GlobalSize(data) < sizeof(METAFILEPICT))
        return OLE_ERROR_FORMAT;
    const METAFILEPICT *p = GlobalLock(data);
    if (!p)
        return OLE_ERROR_FORMAT;
    METAFILEPICT mf = *p;
    GlobalUnlock(data);
    if (mf.mm < MM_TEXT || mf.mm > MM_ANISOTROPIC || !IsValidMetaFile(mf.hMF))
        return OLE_ERROR_FORMAT;
    HGLOBAL image = metafile_bits(mf.hMF, mf.mm);
    if (!image)
        return OLE_ERROR_MEMORY;
    HDC screen = mf.mm == MM_TEXT ? GetDC(0) : 0;
    LONG w = picture_extent(mf.mm, mf.xExt, screen ? GetDeviceCaps(screen, LOGPIXELSX) : 0);
    LONG h = picture_extent(mf.mm, mf.yExt, screen ? GetDeviceCaps(screen, LOGPIXELSY) : 0);
    if (screen)
        ReleaseDC(0, screen);
    if (w <= 0 || h <= 0 || (mf.mm == MM_ISOTROPIC && mf.xExt < 0 && mf.yExt < 0)) {
        LONG x, y;
        if (!metafile_window(image, &x, &y)) {
            GlobalFree(image);
            return OLE_ERROR_FORMAT;
        }
        if (mf.mm == MM_ISOTROPIC && mf.xExt < 0 && mf.yExt < 0 && w > 0 && h > 0) {
            x = MulDiv(x, 2540, 96);
            h = MulDiv(x, h, w);
            w = x;
        } else {
            if (w <= 0) w = MulDiv(x, 2540, 96);
            if (h <= 0) h = MulDiv(y, 2540, 96);
        }
    }
    return new_picture(image, mf.mm, doc, name, w > 0 ? w : 1, h > 0 ? h : 1, out);
}

static OLESTATUS create_clip_picture(LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *out, UINT fmt)
{
    HANDLE data = GetClipboardData(fmt);
    if (!data)
        return OLE_ERROR_CLIPBOARD;
    if (fmt == CF_METAFILEPICT)
        return create_metafile_picture(data, doc, name, out);
    HGLOBAL image = 0;
    if (fmt == CF_BITMAP) {
        image = bitmap_to_dib(data);
    } else {
        void *p = GlobalLock(data);
        DWORD n = p ? GetPackedDIBInfoN(p, GlobalSize(data), NULL, NULL) : 0;
        if (n)
            image = copy_block(p, n);
        if (p)
            GlobalUnlock(data);
        if (!n)
            return OLE_ERROR_FORMAT;
    }
    return image ? new_picture(image, 0, doc, name, 0, 0, out) : OLE_ERROR_MEMORY;
}

static void free_picture(OLEOBJECT *o, uint16_t task)
{
    /* Task teardown also tears down that task's DDE instances. */
    free(o->session);
    for (unsigned i = 0; i < DATA_SLOTS; i++)
        if (o->guest_data[i])
            NE->task_free(task, o->guest_data[i]);
    if (o->guest)
        NE->task_free(task, o->guest);
    if (o->descriptor)
        GlobalFree(o->descriptor);
    if (o->identity)
        GlobalFree(o->identity);
    if (o->native)
        GlobalFree(o->native);
    if (o->metafile)
        DeleteMetaFile(o->metafile);
    if (o->image)
        GlobalFree(o->image);
    if (o->stream_head)
        GlobalFree(o->stream_head);
    if (o->stream_tail)
        GlobalFree(o->stream_tail);
    memset(o, 0, sizeof(*o));
}

static OLESTATUS WINAPI OleDelete(LPOLEOBJECT object)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->writing)
        return OLE_BUSY;
    if (o->session) {
        o->writing = TRUE;
        OLESTATUS status = close_session(o);
        o->writing = FALSE;
        if (status != OLE_OK)
            return status;
    }
    free_picture(o, (uint16_t)GetCurrentTask());
    return OLE_OK;
}

static OLESTATUS WINAPI OleClone(LPOLEOBJECT object, LPOLECLIENT client, LHCLIENTDOC doc,
                                  LPCSTR name, LPOLEOBJECT *out)
{
    (void)client;
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!name)
        return OLE_ERROR_ADDRESS;
    if (doc_of(doc) < 0)
        return OLE_ERROR_HANDLE;

    HGLOBAL head = 0, tail = 0;
    OLESTATUS status = OLE_ERROR_MEMORY;
    if (o->stream_head && !(head = copy_handle(o->stream_head, o->head_bytes)))
        goto done;
    if (o->stream_tail && !(tail = copy_handle(o->stream_tail, o->tail_bytes)))
        goto done;
    if (o->image) {
        DWORD bytes = o->stream_head && !o->stream_tail ? o->tail_bytes : o->size;
        HGLOBAL image = copy_handle(o->image, bytes);
        if (!image)
            goto done;
        status = new_picture(image, o->map_mode, doc, name, o->width, o->height, out);
    } else {
        *out = new_object(doc, name);
        status = *out ? OLE_OK : OLE_ERROR_MEMORY;
    }
    if (status == OLE_OK) {
        OLEOBJECT *copy = *out;
        copy->type = o->type;
        copy->stream_head = head;
        copy->stream_tail = tail;
        copy->head_bytes = o->head_bytes;
        copy->tail_bytes = o->tail_bytes;
        head = tail = 0;
    }
done:
    if (head)
        GlobalFree(head);
    if (tail)
        GlobalFree(tail);
    return status;
}

static BOOL equal_blocks(HGLOBAL a, HGLOBAL b, DWORD size)
{
    if (!size)
        return TRUE;
    const void *p = GlobalLock(a), *q = GlobalLock(b);
    BOOL same = p && q && !memcmp(p, q, size);
    if (q)
        GlobalUnlock(b);
    if (p)
        GlobalUnlock(a);
    return same;
}

static HGLOBAL stream_payload(const OLEOBJECT *o)
{
    return o->stream_tail ? o->stream_tail : o->image;
}

static OLESTATUS WINAPI OleEqual(LPOLEOBJECT a, LPOLEOBJECT b)
{
    OLEOBJECT *x = object_of(a), *y = object_of(b);
    if (!x || !y)
        return OLE_ERROR_OBJECT;
    if (x == y)
        return OLE_OK;
    if (x->type != y->type)
        return OLE_ERROR_NOT_EQUAL;
    if (x->stream_head)
        return x->head_bytes == y->head_bytes && x->tail_bytes == y->tail_bytes &&
               equal_blocks(x->stream_head, y->stream_head, x->head_bytes) &&
               equal_blocks(stream_payload(x), stream_payload(y), x->tail_bytes) ?
            OLE_OK : OLE_ERROR_NOT_EQUAL;
    return x->width == y->width && x->height == y->height && x->size == y->size &&
           x->map_mode == y->map_mode && equal_blocks(x->image, y->image, x->size) ?
        OLE_OK : OLE_ERROR_NOT_EQUAL;
}

static OLESTATUS WINAPI OleQueryType(LPOLEOBJECT object, LONG *type)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!type)
        return OLE_ERROR_ADDRESS;
    *type = o->type;
    return OLE_OK;
}

static OLESTATUS WINAPI OleQueryBounds(LPOLEOBJECT object, RECT *bounds)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!bounds)
        return OLE_ERROR_ADDRESS;
    if (!o->image)
        return OLE_ERROR_BLANK;
    *bounds = (RECT){ 0, 0, o->width, -o->height };
    return OLE_OK;
}

static OLESTATUS WINAPI OleDraw(LPOLEOBJECT object, HDC dc, const RECT *bounds, const RECT *clip,
                                HDC target)
{
    (void)target;
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!bounds)
        return OLE_ERROR_ADDRESS;
    if (!o->image)
        return OLE_ERROR_BLANK;
    int64_t w = (int64_t)bounds->right - bounds->left, h = (int64_t)bounds->bottom - bounds->top;
    if (!w || !h || w <= INT32_MIN || w > INT32_MAX || h <= INT32_MIN || h > INT32_MAX)
        return OLE_ERROR_DRAW;
    if (o->metafile) {
        int saved = SaveDC(dc);
        if (!saved)
            return OLE_ERROR_DRAW;
        if (clip)
            IntersectClipRect(dc, clip->left, clip->top, clip->right, clip->bottom);
        BOOL ok = PlayMetaFileRectN(dc, o->metafile, bounds, o->map_mode, o->width, o->height);
        RestoreDC(dc, saved);
        return ok ? OLE_OK : OLE_ERROR_DRAW;
    }
    const uint8_t *dib = GlobalLock(o->image);
    if (!dib)
        return OLE_ERROR_BLANK;
    int saved = clip ? SaveDC(dc) : 0;
    int drawn = 0;
    if (!clip || saved) {
        if (clip)
            IntersectClipRect(dc, clip->left, clip->top, clip->right, clip->bottom);
        drawn = StretchDIBits(dc, bounds->left, bounds->top, (int)w, (int)h, 0, 0,
                              o->pixels.cx, o->pixels.cy, dib + o->bits, (const BITMAPINFO *)dib,
                              DIB_RGB_COLORS, SRCCOPY);
    }
    if (saved)
        RestoreDC(dc, saved);
    GlobalUnlock(o->image);
    return drawn > 0 ? OLE_OK : OLE_ERROR_DRAW;
}

static OLESTATUS WINAPI OleRename(LPOLEOBJECT object, LPCSTR name)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!name)
        return OLE_ERROR_ADDRESS;
    lstrcpyn(o->name, name, sizeof(o->name));
    return OLE_OK;
}

static OLESTATUS WINAPI OleEnumObjects(LHCLIENTDOC doc, LPOLEOBJECT *object)
{
    if (doc_of(doc) < 0)
        return OLE_ERROR_HANDLE;
    if (!object)
        return OLE_ERROR_ADDRESS;
    int next = 0;
    if (*object) {
        OLEOBJECT *previous = object_of(*object);
        if (!previous || previous->doc != doc)
            return OLE_ERROR_OBJECT;
        next = (int)(previous - objects) + 1;
    }
    *object = NULL;
    for (; next < OBJECTS; next++)
        if (objects[next].doc == doc) {
            *object = &objects[next];
            break;
        }
    return OLE_OK;
}

static OLESTATUS WINAPI OleQueryName(LPOLEOBJECT object, LPSTR name, UINT *count)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!count)
        return OLE_ERROR_ADDRESS;
    UINT n = (UINT)strlen(o->name) + 1, capacity = *count;
    *count = n;
    if (!name)
        return OLE_OK;
    if (capacity < n)
        return OLE_ERROR_SIZE;
    memcpy(name, o->name, n);
    return OLE_OK;
}

static OLESTATUS WINAPI OleQuerySize(LPOLEOBJECT object, DWORD *size)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!size)
        return OLE_ERROR_ADDRESS;
    if (o->stream_head)
        *size = o->head_bytes + o->tail_bytes;
    else
        *size = STATIC_FIXED_HEADER + o->size + (o->metafile ? sizeof("METAFILEPICT") + WMF_PREFIX :
                                                              sizeof("DIB"));
    return OLE_OK;
}

static OLESTATUS WINAPI OleQueryReleaseStatus(LPOLEOBJECT object)
{
    return object_of(object) ? OLE_OK : OLE_ERROR_OBJECT;
}

static uint32_t read32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void write32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

typedef struct {
    LPOLESTREAM native;
    uint16_t task;
    uint32_t stream, proc, buffer;
} picture_stream;

static int stream_transfer(picture_stream *s, void *data, DWORD count, int put)
{
    uint8_t *p = data;
    while (count) {
        DWORD n = count < STREAM_CHUNK ? count : STREAM_CHUNK;
        DWORD got;
        if (s->native) {
            LPOLESTREAM st = s->native;
            got = put ? st->lpstbl->Put(st, p, n) : st->lpstbl->Get(st, p, n);
        } else {
            uint32_t buf = s->buffer;
            uint16_t args[] = { (uint16_t)(s->stream >> 16), (uint16_t)s->stream,
                                (uint16_t)(buf >> 16), (uint16_t)buf,
                                (uint16_t)(n >> 16), (uint16_t)n };
            if (put && !NE->task_write(s->task, buf, p, n))
                return 0;
            got = NE->call(s->task, s->proc, args, 6);
            if (!put && got == n && !NE->task_read(s->task, buf, p, n))
                return 0;
        }
        if (got != n)
            return 0;
        p += n;
        count -= n;
    }
    return 1;
}

static int write_block(picture_stream *stream, HGLOBAL block, DWORD size)
{
    if (!size)
        return 1;
    void *bytes = GlobalLock(block);
    if (!bytes)
        return 0;
    int ok = stream_transfer(stream, bytes, size, 1);
    GlobalUnlock(block);
    return ok;
}

static DWORD picture_header(const OLEOBJECT *o, DWORD type, uint8_t *header)
{
    if (!type) {
        write32(header, 0x501);
        write32(header + 4, 0);
        return 8;
    }
    const char *cls = o->metafile ? "METAFILEPICT" : "DIB";
    DWORD len = (DWORD)strlen(cls) + 1, prefix = o->metafile ? WMF_PREFIX : 0;
    memset(header, 0, STATIC_FIXED_HEADER + len + prefix);
    write32(header, 0x501);
    write32(header + 4, type);
    write32(header + 8, len);
    memcpy(header + 12, cls, len);
    write32(header + 12 + len, (DWORD)o->width);
    write32(header + 16 + len, (DWORD)-o->height);
    write32(header + 20 + len, o->size + prefix);
    return STATIC_FIXED_HEADER + len + prefix;
}

static OLESTATUS save_picture(LPOLEOBJECT object, picture_stream *stream)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->writing)
        return OLE_BUSY;
    if (o->stream_head) {
        o->writing = TRUE;
        int ok = write_block(stream, o->stream_head, o->head_bytes) &&
                 write_block(stream, stream_payload(o), o->tail_bytes);
        o->writing = FALSE;
        return ok ? OLE_OK : OLE_ERROR_STREAM;
    }
    uint8_t header[PICTURE_HEADER_MAX];
    DWORD bytes = picture_header(o, OT_STATIC, header);
    void *image = GlobalLock(o->image);
    if (!image)
        return OLE_ERROR_BLANK;
    o->writing = TRUE;
    int ok = stream_transfer(stream, header, bytes, 1) &&
             stream_transfer(stream, image, o->size, 1);
    o->writing = FALSE;
    GlobalUnlock(o->image);
    return ok ? OLE_OK : OLE_ERROR_STREAM;
}

/* Retain the wire header (including strings, native data and presentation
 * metadata) independently of the decoded preview. DIB and WMF payloads
 * already live in the picture's buffer and do not need a second copy. */
typedef struct {
    HGLOBAL block;
    DWORD used;
} stream_header;

static OLESTATUS read_part(picture_stream *s, stream_header *head, void *out, DWORD count)
{
    if (!head)
        return stream_transfer(s, out, count, 0) ? OLE_OK : OLE_ERROR_STREAM;
    if (count > UINT32_MAX - head->used)
        return OLE_ERROR_SIZE;
    DWORD needed = head->used + count, capacity = GlobalSize(head->block);
    if (needed > capacity) {
        /* Leave room for the next small fields without doubling large
         * embedded native blobs on a memory-constrained target. */
        DWORD grown = needed <= UINT32_MAX - 128 ? needed + 128 : needed;
        HGLOBAL block = head->block ? GlobalReAlloc(head->block, grown, GMEM_MOVEABLE) :
                                      GlobalAlloc(GMEM_MOVEABLE, grown);
        if (!block && grown != needed)
            block = head->block ? GlobalReAlloc(head->block, needed, GMEM_MOVEABLE) :
                                  GlobalAlloc(GMEM_MOVEABLE, needed);
        if (!block)
            return OLE_ERROR_MEMORY;
        head->block = block;
    }
    uint8_t *p = GlobalLock(head->block);
    if (!p)
        return OLE_ERROR_MEMORY;
    int ok = stream_transfer(s, p + head->used, count, 0);
    if (ok && out)
        memcpy(out, p + head->used, count);
    GlobalUnlock(head->block);
    if (!ok)
        return OLE_ERROR_STREAM;
    head->used = needed;
    return OLE_OK;
}

static OLESTATUS read_opaque_field(picture_stream *s, stream_header *head)
{
    uint8_t b[4];
    OLESTATUS status = read_part(s, head, b, sizeof(b));
    if (status != OLE_OK || !read32(b))
        return status;
    return read_part(s, head, NULL, read32(b));
}

static OLESTATUS read_envelope(picture_stream *s, stream_header *head, WORD type)
{
    /* Class, topic, item, then native data or the network name. */
    for (unsigned i = 0; i < 4; i++) {
        OLESTATUS status = read_opaque_field(s, head);
        if (status != OLE_OK)
            return status;
    }
    if (type == OT_LINK)
        return read_part(s, head, NULL, 8);   /* reserved/network type and link update option */
    return OLE_OK;
}

typedef struct {
    UINT format;
    LONG width, height;
    DWORD bytes, prefix;
} stream_picture;

static OLESTATUS read_presentation(picture_stream *s, stream_header *head, stream_picture *pic)
{
    uint8_t b[12];
    if (head) {
        OLESTATUS status = read_part(s, head, b, 8);
        if (status != OLE_OK || read32(b + 4) == 0)
            return status;                   /* OLE 1.0 null presentation */
        if (read32(b + 4) != 5)
            return OLE_ERROR_FORMAT;
    }
    OLESTATUS status = read_part(s, head, b, 4);
    if (status != OLE_OK)
        return status;
    DWORD len = read32(b);
    uint8_t cls[sizeof("METAFILEPICT")];
    if (len && len <= sizeof(cls)) {
        status = read_part(s, head, cls, len);
        if (status != OLE_OK)
            return status;
        if (len == sizeof("DIB") && !memcmp(cls, "DIB", len))
            pic->format = CF_DIB;
        else if (len == sizeof("BITMAP") && !memcmp(cls, "BITMAP", len))
            pic->format = CF_BITMAP;
        else if (len == sizeof("METAFILEPICT") && !memcmp(cls, "METAFILEPICT", len))
            pic->format = CF_METAFILEPICT;
    } else if (len && head) {
        status = read_part(s, head, NULL, len);
        if (status != OLE_OK)
            return status;
    }
    if (pic->format) {
        status = read_part(s, head, b, sizeof(b));
        if (status != OLE_OK)
            return status;
        LONG h = (LONG)read32(b + 4);
        pic->width = (LONG)read32(b);
        pic->bytes = read32(b + 8);
        if (h == INT32_MIN || ((pic->bytes || !head) &&
            (pic->width <= 0 || (pic->format == CF_METAFILEPICT && !h))))
            return OLE_ERROR_FORMAT;
        pic->height = h < 0 ? -h : h;
        if (pic->format == CF_METAFILEPICT && pic->bytes)
            pic->prefix = WMF_PREFIX;
    } else if (head) {
        status = read_part(s, head, b, 4);
        if (status != OLE_OK)
            return status;
        DWORD fmt = read32(b);
        /* This generic variant can have a trailer whose presence depends
         * on the enclosing stream boundary, which an OLESTREAM lacks. */
        if (fmt == CF_METAFILEPICT)
            return OLE_ERROR_FORMAT;
        if (fmt == CF_DIB || fmt == CF_BITMAP)
            pic->format = (UINT)fmt;
        if (!fmt) {
            status = read_opaque_field(s, head); /* registered clipboard format name */
            if (status != OLE_OK)
                return status;
        }
        status = read_part(s, head, b, 4);
        if (status != OLE_OK)
            return status;
        pic->bytes = read32(b);
    } else {
        return OLE_ERROR_FORMAT;
    }
    if (pic->bytes || !head) {
        DWORD min = pic->format == CF_METAFILEPICT ? 18 + WMF_PREFIX :
                    pic->format == CF_DIB ? 12 : pic->format == CF_BITMAP ? 10 : 0;
        if (pic->bytes < min)
            return OLE_ERROR_FORMAT;
    }
    if (pic->prefix) {
        status = read_part(s, head, b, pic->prefix);
        if (status != OLE_OK)
            return status;
        pic->bytes -= pic->prefix;
    }
    return OLE_OK;
}

static OLESTATUS read_payload(picture_stream *s, DWORD size, HGLOBAL *out)
{
    *out = 0;
    if (!size)
        return OLE_OK;
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size);
    void *p = block ? GlobalLock(block) : NULL;
    if (!p) {
        if (block)
            GlobalFree(block);
        return OLE_ERROR_MEMORY;
    }
    int ok = stream_transfer(s, p, size, 0);
    GlobalUnlock(block);
    if (!ok) {
        GlobalFree(block);
        return OLE_ERROR_STREAM;
    }
    *out = block;
    return OLE_OK;
}

static OLESTATUS load_picture(picture_stream *stream, LPCSTR protocol, LHCLIENTDOC doc,
                              LPCSTR name, LPOLEOBJECT *out)
{
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (doc_of(doc) < 0)
        return OLE_ERROR_HANDLE;
    BOOL editing = protocol && !lstrcmpi(protocol, "StdFileEditing");
    if (!editing && !static_protocol(protocol))
        return OLE_ERROR_PROTOCOL;
    if (!name)
        return OLE_ERROR_ADDRESS;

    uint8_t b[8];
    if (!stream_transfer(stream, b, sizeof(b), 0))
        return OLE_ERROR_STREAM;
    DWORD type = read32(b + 4);
    if (type == OT_STATIC && (read32(b) & 0xffffff00u) != 0x500u)
        return OLE_ERROR_FORMAT;
    if (type != OT_STATIC && type != OT_LINK && type != OT_EMBEDDED)
        return OLE_ERROR_FORMAT;
    if (type != OT_STATIC && !editing)
        return OLE_ERROR_PROTOCOL;

    stream_header head = {0};
    HGLOBAL raw = 0, dib = 0;
    stream_picture pic = {0};
    OLESTATUS status = OLE_OK;
    if (type != OT_STATIC) {
        head.block = copy_block(b, sizeof(b));
        if (!head.block)
            return OLE_ERROR_MEMORY;
        head.used = sizeof(b);
        status = read_envelope(stream, &head, (WORD)type);
    }
    if (status == OLE_OK)
        status = read_presentation(stream, head.block ? &head : NULL, &pic);
    if (status == OLE_OK && pic.bytes > UINT32_MAX - head.used)
        status = OLE_ERROR_SIZE;
    if (status == OLE_OK)
        status = read_payload(stream, pic.bytes, &raw);
    if (status != OLE_OK)
        goto done;

    if (doc_of(doc) < 0) {
        status = OLE_ERROR_HANDLE;
        goto done;
    }
    if (raw && pic.format) {
        if (pic.format == CF_BITMAP) {
            status = bitmap16_to_dib(raw, &dib);
            if (status != OLE_OK)
                goto done;
        } else {
            dib = raw;
            raw = 0;                         /* new_picture takes ownership */
        }
        status = new_picture(dib, pic.format == CF_METAFILEPICT ? MM_ANISOTROPIC : 0,
                             doc, name, pic.width, pic.height, out);
    } else {
        *out = new_object(doc, name);
        status = *out ? OLE_OK : OLE_ERROR_MEMORY;
    }
    if (status == OLE_OK && head.block) {
        OLEOBJECT *o = *out;
        o->type = (WORD)type;
        o->stream_head = head.block;
        o->stream_tail = raw;
        o->head_bytes = head.used;
        o->tail_bytes = pic.bytes;
        head.block = raw = 0;
    }
done:
    if (head.block)
        GlobalFree(head.block);
    if (raw)
        GlobalFree(raw);
    return status;
}

static OLESTATUS WINAPI OleSaveToStream(LPOLEOBJECT object, LPOLESTREAM stream)
{
    if (!stream || !stream->lpstbl || !stream->lpstbl->Put)
        return OLE_ERROR_ADDRESS;
    picture_stream s = { .native = stream };
    return save_picture(object, &s);
}

static OLESTATUS WINAPI OleLoadFromStream(LPOLESTREAM stream, LPCSTR protocol, LPOLECLIENT client,
                                         LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *out)
{
    (void)client;
    if (!stream || !stream->lpstbl || !stream->lpstbl->Get) {
        if (out)
            *out = NULL;
        return OLE_ERROR_ADDRESS;
    }
    picture_stream s = { .native = stream };
    return load_picture(&s, protocol, doc, name, out);
}

static OLESTATUS WINAPI OleRevokeClientDoc(LHCLIENTDOC h)
{
    int i = doc_of(h);
    if (i < 0)
        return OLE_ERROR_HANDLE;
    if (docs[i].creating)
        return OLE_BUSY;
    for (int j = 0; j < OBJECTS; j++)
        if (objects[j].doc == h)
            return OLE_ERROR_NOT_EMPTY;
    docs[i].task = 0;
    return OLE_OK;
}

typedef struct { UINT owner, object, native; } ole_clip_formats;
typedef struct { const uint8_t *text; DWORD size; } ole_clip_string;

static UINT clipboard_format(LPCSTR name, BOOL publish)
{
    UINT format = FindAtom(name);
    return format || !publish ? format : RegisterClipboardFormat(name);
}

static ole_clip_formats clipboard_formats(BOOL publish)
{
    return (ole_clip_formats){ clipboard_format("OwnerLink", publish),
                               clipboard_format("ObjectLink", FALSE),
                               clipboard_format("Native", publish) };
}

static BOOL clip_has(UINT format)
{
    return format && IsClipboardFormatAvailable(format);
}

static OLESTATUS query_clip_object(LPCSTR protocol, UINT render, UINT format, BOOL link)
{
    if (!link && static_protocol(protocol))
        return static_format(render, format) ? OLE_OK : OLE_ERROR_FORMAT;
    if (!protocol || lstrcmpi(protocol, "StdFileEditing"))
        return OLE_ERROR_FORMAT;
    if (render != olerender_none && render != olerender_draw && render != olerender_format)
        return OLE_ERROR_OPTION;
    if (render == olerender_format && !static_format(render, format))
        return OLE_ERROR_FORMAT;
    ole_clip_formats f = clipboard_formats(FALSE);
    return clip_has(f.object) || (!link && clip_has(f.owner)) ? OLE_OK : OLE_ERROR_FORMAT;
}

static OLESTATUS WINAPI OleQueryCreateFromClip(LPCSTR protocol, UINT render, OLECLIPFORMAT format)
{
    return query_clip_object(protocol, render, format, FALSE);
}

static OLESTATUS WINAPI OleQueryLinkFromClip(LPCSTR protocol, UINT render, OLECLIPFORMAT format)
{
    return query_clip_object(protocol, render, format, TRUE);
}

static BOOL clipboard_triplet(const uint8_t *p, DWORD size, ole_clip_string fields[3])
{
    for (unsigned i = 0; i < 3; i++) {
        const uint8_t *end = memchr(p, 0, size);
        if (!end)
            return FALSE;
        DWORD len = (DWORD)(end - p) + 1;
        fields[i] = (ole_clip_string){ p, len };
        p += len;
        size -= len;
    }
    return size && !*p && fields[0].size > 1;
}

static BOOL stream_triplet(const uint8_t *p, DWORD size, ole_clip_string fields[3],
                           const uint8_t **tail, DWORD *tail_size)
{
    if (size < 8)
        return FALSE;
    p += 8;
    size -= 8;
    for (unsigned i = 0; i < 3; i++) {
        if (size < 4)
            return FALSE;
        DWORD len = read32(p);
        p += 4;
        size -= 4;
        if (len > size || (len && (p[len - 1] || memchr(p, 0, len - 1))))
            return FALSE;
        fields[i] = (ole_clip_string){ p, len };
        p += len;
        size -= len;
    }
    *tail = p;
    *tail_size = size;
    return fields[0].size > 1;
}

static uint8_t *write_triplet(uint8_t *p, const ole_clip_string fields[3])
{
    for (unsigned i = 0; i < 3; i++) {
        write32(p, fields[i].size);
        memcpy(p + 4, fields[i].text, fields[i].size);
        p += 4 + fields[i].size;
    }
    return p;
}

static BOOL native_before_owner(ole_clip_formats f)
{
    for (UINT fmt = EnumClipboardFormats(0); fmt; fmt = EnumClipboardFormats(fmt)) {
        if (fmt == f.owner)
            return FALSE;
        if (fmt == f.native)
            return TRUE;
    }
    return FALSE;
}

/* The allocation leaves room for the presentation header. */
static OLESTATUS make_object_header(const ole_clip_string fields[3], HGLOBAL native,
                                     WORD type, HGLOBAL *out, DWORD *used)
{
    DWORD native_size = native ? GlobalSize(native) : 0;
    if (type == OT_EMBEDDED && !native_size)
        return OLE_ERROR_FORMAT;
    OLESTATUS status;

    uint64_t size = 20ull + fields[0].size + fields[1].size + fields[2].size;
    size += type == OT_EMBEDDED ? 4ull + native_size : 12ull;
    if (size > UINT32_MAX - PICTURE_HEADER_MAX)
        return OLE_ERROR_SIZE;
    HGLOBAL head = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)size + PICTURE_HEADER_MAX);
    uint8_t *p = head ? GlobalLock(head) : NULL;
    status = OLE_ERROR_MEMORY;
    if (p) {
        write32(p, 0x501);
        write32(p + 4, type);
        p = write_triplet(p + 8, fields);
        if (type == OT_EMBEDDED) {
            const void *data = GlobalLock(native);
            if (data) {
                write32(p, native_size);
                memcpy(p + 4, data, native_size);
                GlobalUnlock(native);
                status = OLE_OK;
            } else {
                status = OLE_ERROR_CLIPBOARD;
            }
        } else {
            /* No network name or update hint is supplied by ObjectLink. */
            memset(p, 0, 12);
            status = OLE_OK;
        }
        GlobalUnlock(head);
    }
    if (status == OLE_OK) {
        *out = head;
        *used = (DWORD)size;
    } else if (head) {
        GlobalFree(head);
    }
    return status;
}

static OLESTATUS make_clip_header(HGLOBAL meta, HGLOBAL native, WORD type, HGLOBAL *out, DWORD *used)
{
    const uint8_t *source = GlobalLock(meta);
    if (!source)
        return OLE_ERROR_CLIPBOARD;
    ole_clip_string fields[3];
    OLESTATUS status = OLE_ERROR_FORMAT;
    if (clipboard_triplet(source, GlobalSize(meta), fields) &&
        (type != OT_LINK || fields[1].size > 1))
        status = make_object_header(fields, native, type, out, used);
    GlobalUnlock(meta);
    return status;
}

static OLESTATUS finish_stream_object(WORD type, LPOLEOBJECT *out, HGLOBAL head, DWORD used)
{
    OLEOBJECT *o = *out;
    uint8_t *data = GlobalLock(head);
    if (!data) {
        GlobalFree(head);
        OleDelete(o);
        *out = NULL;
        return OLE_ERROR_MEMORY;
    }
    used += picture_header(o, o->image ? 5 : 0, data + used);
    GlobalUnlock(head);
    if (o->size > UINT32_MAX - used) {
        GlobalFree(head);
        OleDelete(o);
        *out = NULL;
        return OLE_ERROR_SIZE;
    }
    o->type = type;
    o->stream_head = head;
    o->head_bytes = used;
    o->tail_bytes = o->size;
    return OLE_OK;
}

static OLESTATUS create_clip_object(LPCSTR protocol, LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *out,
                                     UINT render, OLECLIPFORMAT format, BOOL link)
{
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (!name)
        return OLE_ERROR_ADDRESS;
    if (doc_of(doc) < 0)
        return OLE_ERROR_HANDLE;
    OLESTATUS status = query_clip_object(protocol, render, format, link);
    if (status != OLE_OK)
        return status;
    UINT picture = static_format(render, format);
    if (!link && static_protocol(protocol))
        return create_clip_picture(doc, name, out, picture);

    ole_clip_formats f = clipboard_formats(FALSE);
    BOOL embed = !link && clip_has(f.owner) && clip_has(f.native) && native_before_owner(f);
    UINT identity = embed || (!link && !clip_has(f.object)) ? f.owner : f.object;
    HGLOBAL native = embed ? GetClipboardData(f.native) : 0;
    if (embed && !native)
        return OLE_ERROR_CLIPBOARD;
    HGLOBAL meta = GetClipboardData(identity);
    if (!meta)
        return OLE_ERROR_CLIPBOARD;
    HGLOBAL head = 0;
    DWORD used = 0;
    WORD type = embed ? OT_EMBEDDED : OT_LINK;
    status = make_clip_header(meta, native, type, &head, &used);
    if (status != OLE_OK)
        return status;

    if (picture) {
        status = create_clip_picture(doc, name, out, picture);
    } else {
        *out = new_object(doc, name);
        status = *out ? OLE_OK : OLE_ERROR_MEMORY;
    }
    if (status != OLE_OK) {
        GlobalFree(head);
        return status;
    }
    return finish_stream_object(type, out, head, used);
}

/* A create transaction snapshots the server's new embedded document.
 * Keep all DDE handles local to the synchronous call. */
typedef struct {
    HINSTANCE dll;
    DWORD inst;
    DDEUNINITIALIZE uninit;
    DDECREATESTRINGHANDLE string;
    DDEFREESTRINGHANDLE free_string;
    DDECONNECT connect;
    DDEQUERYCONVINFO conv_info;
    DDEDISCONNECT disconnect;
    DDECLIENTTRANSACTION transaction;
    DDEGETDATA get_data;
    DDEGETLASTERROR last_error;
    DDEFREEDATAHANDLE free_data;
} ole_dde;

static HDDEDATA CALLBACK ole_dde_callback(UINT type, UINT fmt, HCONV conv, HSZ topic,
                                          HSZ item, HDDEDATA data, DWORD d1, DWORD d2)
{
    (void)type; (void)fmt; (void)conv; (void)topic;
    (void)item; (void)data; (void)d1; (void)d2;
    return 0;
}

static BOOL ole_dde_begin(ole_dde *dde)
{
    dde->dll = LoadLibrary("DDEML");
    if (dde->dll <= 2)
        return FALSE;
#define DDE_BIND(member, type, name) \
    dde->member = (type)(void (*)(void))GetProcAddress(dde->dll, name); \
    if (!dde->member) goto failed
    DDEINITIALIZE init = (DDEINITIALIZE)(void (*)(void))GetProcAddress(dde->dll, "DdeInitialize");
    if (!init)
        goto failed;
    DDE_BIND(uninit, DDEUNINITIALIZE, "DdeUninitialize");
    DDE_BIND(string, DDECREATESTRINGHANDLE, "DdeCreateStringHandle");
    DDE_BIND(free_string, DDEFREESTRINGHANDLE, "DdeFreeStringHandle");
    DDE_BIND(connect, DDECONNECT, "DdeConnect");
    DDE_BIND(conv_info, DDEQUERYCONVINFO, "DdeQueryConvInfo");
    DDE_BIND(disconnect, DDEDISCONNECT, "DdeDisconnect");
    DDE_BIND(transaction, DDECLIENTTRANSACTION, "DdeClientTransaction");
    DDE_BIND(get_data, DDEGETDATA, "DdeGetData");
    DDE_BIND(last_error, DDEGETLASTERROR, "DdeGetLastError");
    DDE_BIND(free_data, DDEFREEDATAHANDLE, "DdeFreeDataHandle");
#undef DDE_BIND
    dde->inst = 0;                      /* a new DDEML instance */
    if (!init(&dde->inst, ole_dde_callback, APPCMD_CLIENTONLY | CBF_SKIP_ALLNOTIFICATIONS, 0))
        return TRUE;
failed:
    FreeLibrary(dde->dll);
    return FALSE;
}

static HCONV ole_connect(const ole_dde *dde, HSZ service, LPCSTR topic)
{
    HSZ name = dde->string(dde->inst, topic, CP_WINANSI);
    if (!name)
        return 0;
    HCONV conv = dde->connect(dde->inst, service, name, NULL);
    dde->free_string(dde->inst, name);
    return conv;
}

static HCONV ole_connect_system(const ole_dde *dde, HSZ service)
{
    HCONV conv = ole_connect(dde, service, "OLESystem");
    return conv ? conv : ole_connect(dde, service, "System");
}

static void ole_debug_status(LPCSTR operation, DWORD status, DWORD detail)
{
    char message[96];
    wsprintf(message, "OLECLI %s: %08lX %08lX", operation,
             (unsigned long)status, (unsigned long)detail);
    OutputDebugString(message);
}

static OLESTATUS ole_start_server(const ole_dde *dde, HSZ service, LPCSTR cls, HCONV *out,
                                  BOOL *started)
{
    *out = ole_connect_system(dde, service);
    if (*out)
        return OLE_OK;

    char key[128], command[512 + sizeof(" /Embedding")];
    wsprintf(key, "%s\\protocol\\StdFileEditing\\server", cls);
    if (!WinRegQueryClassStringN(key, command, 512) || !command[0])
        return OLE_ERROR_CLASS;
    lstrcat(command, " /Embedding");
    UINT task = WinExec(command, SW_HIDE);
    if (task < 32)
        return OLE_ERROR_LAUNCH;
    *started = TRUE;

    /* The service may be registered before the server can create a document. */
    DWORD start = GetTickCount();
    BOOL waiting;
    do {
        waiting = NE && NE->size >= sizeof(*NE) && NE->task_starting &&
                  NE->task_starting((uint16_t)task);
        if (!waiting) {
            *out = ole_connect_system(dde, service);
            if (*out)
                return OLE_OK;
        }
    } while (GetTickCount() - start < 2000 && Yield());
    ole_debug_status(waiting ? "server still starting" : "system conversation",
                     task, waiting ? 0 : dde->last_error(dde->inst));
    return OLE_ERROR_COMM;
}

static OLESTATUS ole_transaction_error(const ole_dde *dde, DWORD flags, OLESTATUS rejected)
{
    UINT error = dde->last_error(dde->inst);
    ole_debug_status("DDE transaction", error, flags);
    if ((flags & DDE_FBUSY) || error == DMLERR_BUSY)
        return OLE_BUSY;
    if (error == DMLERR_MEMORY_ERROR || error == DMLERR_LOW_MEMORY)
        return OLE_ERROR_MEMORY;
    if (error == DMLERR_NOTPROCESSED || error == DMLERR_NO_ERROR)
        return rejected;
    return OLE_ERROR_COMM;
}

static OLESTATUS ole_execute(const ole_dde *dde, HCONV conv, LPSTR command,
                                     OLESTATUS failure)
{
    DWORD flags = 0;
    if (dde->transaction((LPBYTE)command, (DWORD)strlen(command) + 1, conv,
                         0, 0, XTYP_EXECUTE, 2000, &flags))
        return OLE_OK;
    return ole_transaction_error(dde, flags, failure);
}

static OLESTATUS ole_request_block(const ole_dde *dde, HCONV conv, HSZ item, UINT format, HGLOBAL *out)
{
    if (!format)
        return OLE_ERROR_MEMORY;
    HDDEDATA data = dde->transaction(NULL, 0, conv, item, format, XTYP_REQUEST, 2000, NULL);
    if (!data)
        return ole_transaction_error(dde, 0, OLE_ERROR_FORMAT);

    DWORD bytes = dde->get_data(data, NULL, 0, 0);
    HGLOBAL block = bytes ? GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes) : 0;
    LPBYTE dst = block ? GlobalLock(block) : NULL;
    OLESTATUS status = bytes ? OLE_ERROR_MEMORY : OLE_ERROR_BLANK;
    if (dst) {
        status = dde->get_data(data, dst, bytes, 0) == bytes ? OLE_OK : OLE_ERROR_COMM;
        GlobalUnlock(block);
    }
    dde->free_data(data);
    if (status == OLE_OK)
        *out = block;
    else if (block)
        GlobalFree(block);
    return status;
}

static OLESTATUS ole_close_document(const ole_dde *dde, HCONV sys, HCONV doc, LPCSTR topic)
{
    if (doc)
        return ole_execute(dde, doc, "[StdCloseDocument]", OLE_ERROR_TERMINATE);
    char command[sizeof("[StdCloseDocument(\"\")]") + 40];
    wsprintf(command, "[StdCloseDocument(\"%s\")]", topic);
    return ole_execute(dde, sys, command, OLE_ERROR_TERMINATE);
}

static OLESTATUS create_server_snapshot(LPCSTR cls, LHCLIENTDOC doc, LPCSTR name,
                                         UINT render, LPOLEOBJECT *out,
                                         HGLOBAL native, HGLOBAL image)
{
    const ole_clip_string identity[] = {
        { (const uint8_t *)cls, (DWORD)strlen(cls) + 1 },
        { (const uint8_t *)"", 1 }, { (const uint8_t *)"", 1 }
    };
    HGLOBAL head = 0;
    DWORD used = 0;
    OLESTATUS status = make_object_header(identity, native, OT_EMBEDDED, &head, &used);
    if (status != OLE_OK) {
        if (image)
            GlobalFree(image);
        return status;
    }
    if (image) {
        status = new_picture(image, 0, doc, name, 0, 0, out);
        if (status == OLE_ERROR_FORMAT && render == olerender_draw)
            image = 0;
    }
    if (!image) {
        *out = new_object(doc, name);
        status = *out ? OLE_OK : OLE_ERROR_MEMORY;
    }
    if (status == OLE_OK)
        return finish_stream_object(OT_EMBEDDED, out, head, used);
    GlobalFree(head);
    return status;
}

static void make_topic_name(char *topic)
{
    static DWORD next_topic;
    wsprintf(topic, "WINN-OLE-%04X-%08X", (UINT)GetCurrentTask(), (unsigned)++next_topic);
}

static OLESTATUS WINAPI OleCreate(LPCSTR protocol, LPOLECLIENT client, LPCSTR cls,
                                   LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *out,
                                   UINT render, OLECLIPFORMAT format)
{
    (void)client;
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (!name || !cls)
        return OLE_ERROR_ADDRESS;
    if (!protocol || lstrcmpi(protocol, "StdFileEditing"))
        return OLE_ERROR_PROTOCOL;
    if (!*cls || strlen(cls) >= 64 || strchr(cls, '"') || strchr(cls, '\\') || strchr(cls, '/'))
        return OLE_ERROR_CLASS;
    if (render != olerender_none && render != olerender_draw && render != olerender_format)
        return OLE_ERROR_OPTION;
    if (render == olerender_format && format != CF_DIB)
        return OLE_ERROR_FORMAT;
    int d = doc_of(doc);
    if (d < 0)
        return OLE_ERROR_HANDLE;

    ole_dde dde;
    if (!ole_dde_begin(&dde)) {
        ole_debug_status("OleCreate DDE initialization", OLE_ERROR_COMM, 0);
        return OLE_ERROR_COMM;
    }

    char topic[40], command[160];
    make_topic_name(topic);
    HSZ service = dde.string(dde.inst, cls, CP_WINANSI);
    HCONV sys = 0, data = 0;
    HGLOBAL native = 0, image = 0;
    OLESTATUS status = OLE_ERROR_MEMORY;
    BOOL created = FALSE, started = FALSE;
    LPCSTR stage = "OleCreate service handle";
    docs[d].creating++;
    if (!service)
        goto done;
    stage = "OleCreate server startup";
    status = ole_start_server(&dde, service, cls, &sys, &started);
    if (status != OLE_OK)
        goto done;
    wsprintf(command, "[StdNewDocument(\"%s\",\"%s\")]", cls, topic);
    stage = "OleCreate StdNewDocument";
    status = ole_execute(&dde, sys, command, OLE_ERROR_NEW);
    if (status != OLE_OK)
        goto done;
    created = TRUE;
    stage = "OleCreate document conversation";
    data = ole_connect(&dde, service, topic);
    status = OLE_ERROR_COMM;
    if (!data) {
        ole_debug_status("DdeConnect document", dde.last_error(dde.inst), 0);
        goto done;
    }
    stage = "OleCreate native data";
    status = ole_request_block(&dde, data, 0, clipboard_format("Native", TRUE), &native);
    if (status != OLE_OK)
        goto done;
    if (render != olerender_none) {
        stage = "OleCreate presentation";
        status = ole_request_block(&dde, data, 0, CF_DIB, &image);
        if (status != OLE_OK &&
            (render == olerender_format || (status != OLE_ERROR_FORMAT && status != OLE_ERROR_BLANK)))
            goto done;
    }
    status = OLE_OK;
done:
    if (status != OLE_OK)
        ole_debug_status(stage, status, 0);
    if (created) {
        OLESTATUS closed = ole_close_document(&dde, sys, data, topic);
        if (closed != OLE_OK && status == OLE_OK) {
            ole_debug_status("OleCreate close document", closed, 0);
            status = closed;
        }
    }
    if (data)
        dde.disconnect(data);
    if (started && sys && status != OLE_OK)
        ole_execute(&dde, sys, "[StdExit]", OLE_ERROR_TERMINATE);
    if (sys)
        dde.disconnect(sys);
    if (service)
        dde.free_string(dde.inst, service);
    dde.uninit(dde.inst);
    FreeLibrary(dde.dll);
    if (status == OLE_OK) {
        status = create_server_snapshot(cls, doc, name, render, out, native, image);
        image = 0;                    /* snapshot creation consumes it */
        if (status != OLE_OK)
            ole_debug_status("OleCreate snapshot", status, 0);
    }
    if (image)
        GlobalFree(image);
    if (native)
        GlobalFree(native);
    docs[d].creating--;
    return status;
}

static OLESTATUS WINAPI OleCreateFromClip(LPCSTR protocol, LPOLECLIENT client, LHCLIENTDOC doc,
                                         LPCSTR name, LPOLEOBJECT *out, UINT render,
                                         OLECLIPFORMAT format)
{
    (void)client;
    return create_clip_object(protocol, doc, name, out, render, format, FALSE);
}

static OLESTATUS WINAPI OleCreateLinkFromClip(LPCSTR protocol, LPOLECLIENT client, LHCLIENTDOC doc,
                                             LPCSTR name, LPOLEOBJECT *out, UINT render,
                                             OLECLIPFORMAT format)
{
    (void)client;
    return create_clip_object(protocol, doc, name, out, render, format, TRUE);
}

static HGLOBAL clipboard_identity(ole_clip_string fields[3])
{
    uint64_t total = 1;
    for (unsigned i = 0; i < 3; i++)
        total += fields[i].size ? fields[i].size : 1;
    if (total > UINT32_MAX)
        return 0;
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, (DWORD)total);
    uint8_t *p = block ? GlobalLock(block) : NULL;
    if (!p) {
        if (block)
            GlobalFree(block);
        return 0;
    }
    for (unsigned i = 0; i < 3; i++) {
        DWORD len = fields[i].size;
        if (len)
            memcpy(p, fields[i].text, len);
        p += len ? len : 1;
    }
    GlobalUnlock(block);
    return block;
}

/* Build fresh handles for clipboard transfers and object-owned handles
 * for OleGetData from the same persisted fields. */
static OLESTATUS stream_data(OLEOBJECT *o, BOOL native, HGLOBAL *out)
{
    const uint8_t *p = GlobalLock(o->stream_head);
    if (!p)
        return OLE_ERROR_MEMORY;
    ole_clip_string fields[3];
    const uint8_t *tail;
    DWORD n;
    OLESTATUS status = OLE_ERROR_FORMAT;
    if (stream_triplet(p, o->head_bytes, fields, &tail, &n)) {
        DWORD size = n >= 4 ? read32(tail) : 0;
        if (!native || (size && size <= n - 4)) {
            *out = native ? copy_block(tail + 4, size) : clipboard_identity(fields);
            status = *out ? OLE_OK : OLE_ERROR_MEMORY;
        }
    }
    GlobalUnlock(o->stream_head);
    return status;
}

static void object_formats(const OLEOBJECT *o, OLECLIPFORMAT formats[DATA_SLOTS])
{
    memset(formats, 0, DATA_SLOTS * sizeof(*formats));
    if (o->image)
        formats[DATA_PICTURE] = o->metafile ? CF_METAFILEPICT : CF_DIB;
    if (o->type == OT_EMBEDDED) {
        formats[DATA_NATIVE] = clipboard_format("Native", TRUE);
        formats[DATA_IDENTITY] = clipboard_format("OwnerLink", TRUE);
    } else if (o->type == OT_LINK) {
        formats[DATA_IDENTITY] = clipboard_format("ObjectLink", TRUE);
    }
}

static OLECLIPFORMAT WINAPI OleEnumFormats(LPOLEOBJECT object, OLECLIPFORMAT previous)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return 0;
    OLECLIPFORMAT formats[DATA_SLOTS];
    object_formats(o, formats);
    for (unsigned i = 0; i < DATA_SLOTS; i++) {
        if (formats[i] && !previous)
            return formats[i];
        if (formats[i] == previous)
            previous = 0;
    }
    return 0;
}

static OLESTATUS WINAPI OleGetData(LPOLEOBJECT object, OLECLIPFORMAT fmt, HANDLE *out)
{
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = 0;
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    OLECLIPFORMAT formats[DATA_SLOTS];
    object_formats(o, formats);
    if (fmt && (fmt == formats[DATA_IDENTITY] || fmt == formats[DATA_NATIVE])) {
        BOOL native = fmt == formats[DATA_NATIVE];
        HGLOBAL *cached = native ? &o->native : &o->identity;
        if (!*cached) {
            OLESTATUS status = stream_data(o, native, cached);
            if (status != OLE_OK)
                return status;
        }
        *out = *cached;
        return OLE_OK;
    }
    if (!o->image && (fmt == CF_DIB || fmt == CF_METAFILEPICT))
        return OLE_ERROR_BLANK;
    if (!fmt || fmt != formats[DATA_PICTURE])
        return OLE_ERROR_FORMAT;
    if (o->metafile) {
        if (!o->descriptor)
            o->descriptor = metafile_descriptor(o->metafile, o->width, o->height);
        if (!o->descriptor)
            return OLE_ERROR_MEMORY;
        *out = o->descriptor;
    } else {
        *out = o->image;
    }
    return OLE_OK;
}

static OLESTATUS set_data_status(OLEOBJECT *o, OLECLIPFORMAT format)
{
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (!format || format != clipboard_format("ObjectLink", FALSE))
        return OLE_ERROR_NOT_OPEN;
    if (o->type != OT_LINK)
        return OLE_ERROR_NOT_LINK;
    return (o->writing || o->session) ? OLE_BUSY : OLE_OK;
}

/* Locate the update hint after the length-prefixed network name and
 * reserved DWORD. Everything from the hint onward survives a relink. */
static DWORD link_hint_offset(const uint8_t *tail, DWORD size)
{
    if (size < 12 || read32(tail) > size - 12)
        return 0;
    return 8 + read32(tail);
}

static OLESTATUS replace_link_source(OLEOBJECT *o, const uint8_t *data, DWORD size)
{
    ole_clip_string next[3], previous[3];
    if (!clipboard_triplet(data, size, next) || next[1].size <= 1)
        return OLE_ERROR_FORMAT;
    const uint8_t *head = GlobalLock(o->stream_head);
    if (!head)
        return OLE_ERROR_MEMORY;
    const uint8_t *tail;
    DWORD n, hint;
    HGLOBAL replacement = 0;
    OLESTATUS status = OLE_ERROR_FORMAT;
    if (!stream_triplet(head, o->head_bytes, previous, &tail, &n) ||
        !(hint = link_hint_offset(tail, n)))
        goto done;

    /* Network metadata belongs to the old topic. Retain it only when
     * the new identity still refers to exactly that topic. */
    BOOL same_topic = previous[1].size == next[1].size &&
                      !memcmp(previous[1].text, next[1].text, next[1].size);
    DWORD suffix = same_topic ? n : n - hint + 8;
    uint64_t total = 20ull + next[0].size + next[1].size + next[2].size + suffix;
    status = OLE_ERROR_SIZE;
    if (total + o->tail_bytes > UINT32_MAX)
        goto done;
    replacement = GlobalAlloc(GMEM_MOVEABLE, (DWORD)total);
    uint8_t *p = replacement ? GlobalLock(replacement) : NULL;
    status = OLE_ERROR_MEMORY;
    if (!p)
        goto done;
    memcpy(p, head, 8);
    p = write_triplet(p + 8, next);
    if (same_topic) {
        memcpy(p, tail, n);
    } else {
        memset(p, 0, 8);
        memcpy(p + 8, tail + hint, n - hint);
    }
    GlobalUnlock(replacement);
    status = OLE_OK;
done:
    GlobalUnlock(o->stream_head);
    if (status != OLE_OK) {
        if (replacement)
            GlobalFree(replacement);
        return status;
    }
    GlobalFree(o->stream_head);
    o->stream_head = replacement;
    o->head_bytes = GlobalSize(replacement);
    return OLE_OK;
}

static void invalidate_identity(OLEOBJECT *o)
{
    if (o->identity) {
        GlobalFree(o->identity);
        o->identity = 0;
    }
    if (o->guest_data[DATA_IDENTITY]) {
        NE->task_free((uint16_t)GetCurrentTask(), o->guest_data[DATA_IDENTITY]);
        o->guest_data[DATA_IDENTITY] = 0;
    }
}

static OLESTATUS WINAPI OleSetData(LPOLEOBJECT object, OLECLIPFORMAT format, HANDLE data)
{
    OLEOBJECT *o = object_of(object);
    OLESTATUS status = set_data_status(o, format);
    if (status != OLE_OK)
        return status;
    const uint8_t *bytes = GlobalLock(data);
    if (!bytes)
        return OLE_ERROR_HANDLE;
    status = replace_link_source(o, bytes, GlobalSize(data));
    GlobalUnlock(data);
    if (status == OLE_OK)
        invalidate_identity(o);
    return status;
}

static OLESTATUS clipboard_object_data(OLEOBJECT *o, ole_clip_formats f,
                                      WINNCLIPDATA *items, UINT *count)
{
    HGLOBAL data;
    if (!f.owner || (o->type == OT_EMBEDDED && !f.native))
        return OLE_ERROR_MEMORY;
    if (o->type == OT_EMBEDDED) {
        OLESTATUS status = stream_data(o, TRUE, &data);
        if (status != OLE_OK)
            return status;
        items[(*count)++] = (WINNCLIPDATA){ f.native, data };
    }
    OLESTATUS status = stream_data(o, FALSE, &data);
    if (status == OLE_OK)
        items[(*count)++] = (WINNCLIPDATA){ f.owner, data };
    return status;
}

static OLESTATUS link_update_option(LPOLEOBJECT object, OLEOPT_UPDATE *option, BOOL set)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->type != OT_LINK)
        return OLE_ERROR_NOT_LINK;
    if (!option)
        return OLE_ERROR_ADDRESS;
    if (set && (unsigned)*option > oleupdate_oncall)
        return OLE_ERROR_OPTION;
    if (set && o->writing)
        return OLE_BUSY;

    uint8_t *p = GlobalLock(o->stream_head);
    if (!p)
        return OLE_ERROR_MEMORY;
    ole_clip_string fields[3];
    const uint8_t *tail;
    DWORD n, hint;
    OLESTATUS status = OLE_ERROR_FORMAT;
    if (stream_triplet(p, o->head_bytes, fields, &tail, &n) &&
        (hint = link_hint_offset(tail, n))) {
        uint8_t *wire = p + (tail - p) + hint;
        /* The Win16 enum starts at zero. The wire hint starts at one,
         * and absent or unrecognized hints default to automatic updates. */
        if (set)
            write32(wire, (DWORD)*option + 1);
        else {
            DWORD value = read32(wire);
            *option = value >= 1 && value <= 3 ? (OLEOPT_UPDATE)(value - 1) : oleupdate_always;
        }
        status = OLE_OK;
    }
    GlobalUnlock(o->stream_head);
    return status;
}

static OLESTATUS WINAPI OleGetLinkUpdateOptions(LPOLEOBJECT object, OLEOPT_UPDATE *option)
{
    return link_update_option(object, option, FALSE);
}

static OLESTATUS WINAPI OleSetLinkUpdateOptions(LPOLEOBJECT object, OLEOPT_UPDATE option)
{
    return link_update_option(object, &option, TRUE);
}

struct ole_session {
    ole_dde dde;
    HCONV conv;
    HSZ item;
    char item_name[256];
};

static BOOL session_open(const ole_session *session)
{
    if (!session)
        return FALSE;
    CONVINFO info = { .cb = sizeof(info) };
    return session->dde.conv_info(session->conv, QID_SYNC, &info) &&
           (info.wStatus & ST_CONNECTED);
}

static void disconnect_session(OLEOBJECT *o)
{
    ole_session *s = o->session;
    if (!s)
        return;
    o->session = NULL;
    if (s->conv)
        s->dde.disconnect(s->conv);
    if (s->item)
        s->dde.free_string(s->dde.inst, s->item);
    s->dde.uninit(s->dde.inst);
    FreeLibrary(s->dde.dll);
    free(s);
}

static OLESTATUS close_session(OLEOBJECT *o)
{
    if (o->type == OT_EMBEDDED && session_open(o->session)) {
        OLESTATUS status = ole_execute(&o->session->dde, o->session->conv,
                                             "[StdCloseDocument]", OLE_ERROR_TERMINATE);
        if (status != OLE_OK)
            return status;
    }
    disconnect_session(o);
    return OLE_OK;
}

static OLESTATUS WINAPI OleQueryOpen(LPOLEOBJECT object)
{
    OLEOBJECT *o = object_of(object);
    return !o ? OLE_ERROR_OBJECT : session_open(o->session) ? OLE_OK : OLE_ERROR_NOT_OPEN;
}

static OLESTATUS poke_native(OLEOBJECT *o, const ole_session *s)
{
    HGLOBAL data = 0;
    OLESTATUS status = stream_data(o, TRUE, &data);
    if (status != OLE_OK)
        return status;
    LPBYTE bytes = GlobalLock(data);
    if (bytes) {
        DWORD flags = 0;
        UINT format = clipboard_format("Native", TRUE);
        if (format && s->dde.transaction(bytes, GlobalSize(data), s->conv, 0, format,
                                         XTYP_POKE, 2000, &flags))
            status = OLE_OK;
        else
            status = format ? ole_transaction_error(&s->dde, flags, OLE_ERROR_EDIT) : OLE_ERROR_MEMORY;
        GlobalUnlock(data);
    } else {
        status = OLE_ERROR_MEMORY;
    }
    GlobalFree(data);
    return status;
}

static BOOL session_name(char *out, size_t capacity, const ole_clip_string *field)
{
    if (field->size > capacity)
        return FALSE;
    out[0] = 0;
    if (field->size)
        memcpy(out, field->text, field->size);
    return !strchr(out, '"');
}

static OLESTATUS connect_object(OLEOBJECT *o)
{
    char cls[64], topic[256];
    ole_session *s = calloc(1, sizeof(*s));
    if (!s)
        return OLE_ERROR_MEMORY;
    const uint8_t *head = GlobalLock(o->stream_head), *tail;
    ole_clip_string fields[3];
    DWORD n;
    BOOL link = o->type == OT_LINK;
    BOOL valid = head && stream_triplet(head, o->head_bytes, fields, &tail, &n) &&
        session_name(cls, sizeof(cls), &fields[0]) && !strchr(cls, '\\') && !strchr(cls, '/');
    if (valid && link)
        valid = session_name(topic, sizeof(topic), &fields[1]) && topic[0] &&
                session_name(s->item_name, sizeof(s->item_name), &fields[2]);
    if (head)
        GlobalUnlock(o->stream_head);
    if (!valid || !ole_dde_begin(&s->dde)) {
        free(s);
        return valid ? OLE_ERROR_COMM : OLE_ERROR_FORMAT;
    }
    o->session = s;
    ole_dde *dde = &s->dde;
    HSZ service = dde->string(dde->inst, cls, CP_WINANSI);
    HCONV sys = 0;
    BOOL created = FALSE, started = FALSE;
    char command[300];
    OLESTATUS status = OLE_ERROR_MEMORY;
    if (!service)
        goto done;
    if (link) {
        if (s->item_name[0]) {
            s->item = dde->string(dde->inst, s->item_name, CP_WINANSI);
            if (!s->item)
                goto done;
        }
        s->conv = ole_connect(dde, service, topic);
        if (s->conv) {
            status = OLE_OK;
            goto done;
        }
    } else {
        make_topic_name(topic);
    }
    status = ole_start_server(dde, service, cls, &sys, &started);
    if (link && started)
        s->conv = ole_connect(dde, service, topic);
    if (s->conv) {
        status = OLE_OK;
        goto done;
    }
    if (status != OLE_OK)
        goto done;

    wsprintf(command, link ? "[StdOpenDocument(\"%s\")]" : "[StdEditDocument(\"%s\")]", topic);
    status = ole_execute(dde, sys, command, link ? OLE_ERROR_OPEN : OLE_ERROR_EDIT);
    if (status != OLE_OK)
        goto done;
    created = !link;
    s->conv = ole_connect(dde, service, topic);
    status = !s->conv ? OLE_ERROR_COMM : link ? OLE_OK : poke_native(o, s);
done:
    if (status != OLE_OK && sys && created)
        ole_close_document(dde, sys, s->conv, topic);
    if (status != OLE_OK && sys && started)
        ole_execute(dde, sys, "[StdExit]", OLE_ERROR_TERMINATE);
    if (sys)
        dde->disconnect(sys);
    if (service)
        dde->free_string(dde->inst, service);
    if (status != OLE_OK)
        disconnect_session(o);
    return status;
}

static OLESTATUS WINAPI OleActivate(LPOLEOBJECT object, UINT verb, BOOL show, BOOL focus,
                                     HWND window, const RECT *rect)
{
    (void)window; (void)rect;          /* OLE 1 opens an external window */
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (o->writing)
        return OLE_BUSY;
    if (verb > UINT16_MAX)
        return OLE_ERROR_DOVERB;

    o->writing = TRUE;
    BOOL opened = !session_open(o->session);
    OLESTATUS status = OLE_OK;
    if (opened) {
        if (o->session)
            disconnect_session(o);
        status = connect_object(o);
    }
    if (status == OLE_OK) {
        ole_session *s = o->session;
        char command[320];
        wsprintf(command, "[StdDoVerbItem(\"%s\",%u,%s,%s)]", s->item_name, verb,
                 show ? "TRUE" : "FALSE", focus ? "FALSE" : "TRUE");
        status = ole_execute(&s->dde, s->conv, command, OLE_ERROR_DOVERB);
        if (status != OLE_OK && opened)
            close_session(o);
    }
    o->writing = FALSE;
    return status;
}

static OLESTATUS stage_dib(HGLOBAL image, OLEOBJECT *picture)
{
    const void *bytes = GlobalLock(image);
    if (!bytes)
        return OLE_ERROR_MEMORY;
    picture->size = GetPackedDIBInfoN(bytes, GlobalSize(image), &picture->pixels, &picture->bits);
    GlobalUnlock(image);
    if (!picture->size)
        return OLE_ERROR_FORMAT;
    HDC screen = GetDC(0);
    int dx = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 0;
    int dy = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 0;
    if (screen)
        ReleaseDC(0, screen);
    picture->image = image;
    picture->width = MulDiv(picture->pixels.cx, 2540, dx > 0 ? dx : 96);
    picture->height = MulDiv(picture->pixels.cy, 2540, dy > 0 ? dy : 96);
    return OLE_OK;
}

static void clear_guest_data(OLEOBJECT *o, unsigned slot)
{
    if (o->guest_data[slot]) {
        NE->task_free((uint16_t)GetCurrentTask(), o->guest_data[slot]);
        o->guest_data[slot] = 0;
    }
}

static OLESTATUS install_update(OLEOBJECT *o, HGLOBAL native, HGLOBAL image)
{
    OLEOBJECT picture = {0};
    if (image) {
        OLESTATUS status = stage_dib(image, &picture);
        if (status != OLE_OK)
            return status;
    }
    const uint8_t *head = GlobalLock(o->stream_head);
    if (!head)
        return OLE_ERROR_MEMORY;
    const uint8_t *tail;
    ole_clip_string fields[3];
    DWORD n, prefix, size = 0, used = 0;
    HGLOBAL replacement = 0;
    OLESTATUS status = OLE_ERROR_FORMAT;
    if (!stream_triplet(head, o->head_bytes, fields, &tail, &n))
        goto done;
    if (o->type == OT_EMBEDDED) {
        if (n < 4 || read32(tail) > n - 4)
            goto done;
        prefix = (DWORD)(tail - head) + 4 + read32(tail);
        status = make_object_header(fields, native, OT_EMBEDDED, &replacement, &used);
        if (status != OLE_OK)
            goto done;
    } else {
        DWORD hint = link_hint_offset(tail, n);
        if (!hint)
            goto done;
        prefix = (DWORD)(tail - head) + hint + 4;
        used = prefix;
    }

    uint8_t presentation[PICTURE_HEADER_MAX];
    DWORD presentation_size = image ? picture_header(&picture, 5, presentation)
                                    : o->head_bytes - prefix;
    DWORD payload_size = image ? picture.size : o->tail_bytes;
    status = OLE_ERROR_SIZE;
    if ((uint64_t)used + presentation_size + payload_size > UINT32_MAX)
        goto done;
    size = used + presentation_size;
    HGLOBAL block = replacement ? GlobalReAlloc(replacement, size, GMEM_MOVEABLE)
                                : GlobalAlloc(GMEM_MOVEABLE, size);
    if (block)
        replacement = block;
    uint8_t *dst = block ? GlobalLock(block) : NULL;
    status = OLE_ERROR_MEMORY;
    if (!dst)
        goto done;
    memcpy(dst, head, o->type == OT_LINK ? prefix : 8);
    memcpy(dst + used, image ? presentation : head + prefix, presentation_size);
    GlobalUnlock(replacement);
    status = OLE_OK;
done:
    GlobalUnlock(o->stream_head);
    if (status != OLE_OK) {
        if (replacement)
            GlobalFree(replacement);
        return status;
    }

    GlobalFree(o->stream_head);
    o->stream_head = replacement;
    o->head_bytes = size;
    if (o->native) {
        GlobalFree(o->native);
        o->native = 0;
    }
    clear_guest_data(o, DATA_NATIVE);
    if (image) {
        clear_guest_data(o, DATA_PICTURE);
        if (o->descriptor)
            GlobalFree(o->descriptor);
        if (o->metafile)
            DeleteMetaFile(o->metafile);
        if (o->image)
            GlobalFree(o->image);
        if (o->stream_tail)
            GlobalFree(o->stream_tail);
        o->descriptor = 0;
        o->metafile = 0;
        o->map_mode = 0;
        o->stream_tail = 0;
        o->image = image;
        o->size = o->tail_bytes = picture.size;
        o->bits = picture.bits;
        o->pixels = picture.pixels;
        o->width = picture.width;
        o->height = picture.height;
    }
    return OLE_OK;
}

static OLESTATUS update_open_object(OLEOBJECT *o)
{
    ole_session *s = o->session;
    HGLOBAL native = 0, image = 0;
    OLESTATUS status = OLE_OK;
    if (o->type == OT_EMBEDDED)
        status = ole_request_block(&s->dde, s->conv, 0, clipboard_format("Native", TRUE), &native);
    if (status == OLE_OK) {
        status = ole_request_block(&s->dde, s->conv, s->item, CF_DIB, &image);
        if (native && (status == OLE_ERROR_FORMAT || status == OLE_ERROR_BLANK))
            status = OLE_OK;
    }
    if (status == OLE_OK)
        status = install_update(o, native, image);
    if (native)
        GlobalFree(native);
    if (image && status != OLE_OK)
        GlobalFree(image);
    return status;
}

static OLESTATUS WINAPI OleUpdate(LPOLEOBJECT object)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (o->writing)
        return OLE_BUSY;
    BOOL temporary = !session_open(o->session);
    if (temporary && o->type != OT_LINK)
        return OLE_ERROR_NOT_OPEN;

    o->writing = TRUE;
    OLESTATUS status = OLE_OK;
    if (temporary) {
        disconnect_session(o);
        status = connect_object(o);
    }
    if (status == OLE_OK)
        status = update_open_object(o);
    if (temporary)
        disconnect_session(o);
    o->writing = FALSE;
    return status;
}

static OLESTATUS WINAPI OleClose(LPOLEOBJECT object)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->type == OT_STATIC)
        return OLE_ERROR_STATIC;
    if (o->writing)
        return OLE_BUSY;
    if (!o->session)
        return OLE_ERROR_NOT_OPEN;

    o->writing = TRUE;
    OLESTATUS status = OLE_OK;
    if (o->type == OT_EMBEDDED && session_open(o->session))
        status = update_open_object(o);
    if (status == OLE_OK)
        status = close_session(o);
    o->writing = FALSE;
    return status;
}

static OLESTATUS WINAPI OleObjectConvert(LPOLEOBJECT object, LPCSTR protocol, LPOLECLIENT client,
                                         LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *out)
{
    (void)client;
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!static_protocol(protocol))
        return OLE_ERROR_PROTOCOL;
    if (!name)
        return OLE_ERROR_ADDRESS;
    if (doc_of(doc) < 0)
        return OLE_ERROR_HANDLE;
    if (!o->image)
        return OLE_ERROR_BLANK;
    HGLOBAL image = copy_handle(o->image, o->size);
    return image ? new_picture(image, o->map_mode, doc, name, o->width, o->height, out) :
                   OLE_ERROR_MEMORY;
}

static OLESTATUS WINAPI OleCopyToClipboard(LPOLEOBJECT object)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    WINNCLIPDATA items[6];
    UINT count = 0;
    HMETAFILE mf = 0;
    ole_clip_formats f = clipboard_formats(o->stream_head != 0);
    OLESTATUS status = o->stream_head ? clipboard_object_data(o, f, items, &count) : OLE_OK;
    if (status == OLE_OK && o->image) {
        if (o->metafile) {
            mf = CopyMetaFile(o->metafile, NULL);
            HGLOBAL data = mf ? metafile_descriptor(mf, o->width, o->height) : 0;
            if (data)
                items[count++] = (WINNCLIPDATA){ CF_METAFILEPICT, data };
            else
                status = OLE_ERROR_MEMORY;
        } else {
            HGLOBAL data = copy_handle(o->image, o->size);
            if (data)
                items[count++] = (WINNCLIPDATA){ CF_DIB, data };
            else
                status = OLE_ERROR_MEMORY;
        }
    }
    if (status == OLE_OK) {
        if (count) {
            const UINT formats[] = { f.owner, f.object, f.native, CF_DIB, CF_BITMAP, CF_METAFILEPICT };
            for (unsigned i = 0; i < sizeof(formats) / sizeof(*formats); i++) {
                if (!formats[i])
                    continue;
                UINT j = 0;
                while (j < count && items[j].format != formats[i])
                    j++;
                if (j == count)
                    items[count++] = (WINNCLIPDATA){ formats[i], 0 };
            }
            if (SetClipboardDataBatchN(items, count))
                return OLE_OK;
            status = OLE_ERROR_CLIPBOARD;
        } else {
            status = OLE_ERROR_BLANK;
        }
    }
    for (UINT i = 0; i < count; i++)
        if (items[i].handle)
            GlobalFree(items[i].handle);
    if (mf)
        DeleteMetaFile(mf);
    return status;
}

static DWORD WINAPI OleQueryClientVersion(void) { return 0x0001; }    /* OLE 1.0 */

static BOOL WINAPI OleIsDcMeta(HDC dc) { return GetDeviceCaps(dc, TECHNOLOGY) == DT_METAFILE; }

static LPOLEOBJECT WINAPI OleQueryProtocol(LPOLEOBJECT o, LPCSTR protocol)
{
    return object_of(o) && o->type == OT_STATIC && static_protocol(protocol) ? o : NULL;
}

static OLESTATUS WINAPI OleSetHostNames(LPOLEOBJECT o, LPCSTR app, LPCSTR name)
{
    if (!object_of(o))
        return OLE_ERROR_OBJECT;
    return app && name ? OLE_OK : OLE_ERROR_ADDRESS;
}

/* ================================================================ Win16 programs */

/* each export: its ordinal, the bytes of its arguments, the place of its
 * LPOLEOBJECT FAR * when it makes an object (0: none), and how it ends */
enum {
    R_OBJECT,                       /* about an object: OLE_ERROR_OBJECT */
    R_CREATE,                       /* makes one: OLE_ERROR_CLASS, the object NULL */
    R_OWN                           /* its own case in ne_call */
};

static const struct {
    uint16_t ordinal, args;
    const char *name;
    uint8_t made_at, kind;
    const void *native;
} funcs[] = {
    { 2, 4, "OleDelete", 0, R_OWN, (const void *)OleDelete },
    { 3, 8, "OleSaveToStream", 0, R_OWN, (const void *)OleSaveToStream },
    { 4, 24, "OleLoadFromStream", 0, R_OWN, (const void *)OleLoadFromStream },
    { 6, 20, "OleClone", 0, R_OWN, (const void *)OleClone },
    { 7, 24, "OleCopyFromLink", 4, R_CREATE, NULL },
    { 8, 8, "OleEqual", 0, R_OWN, (const void *)OleEqual },
    { 9, 8, "OleQueryLinkFromClip", 0, R_OWN, (const void *)OleQueryLinkFromClip },
    { 10, 8, "OleQueryCreateFromClip", 0, R_OWN, (const void *)OleQueryCreateFromClip },
    { 11, 24, "OleCreateLinkFromClip", 0, R_OWN, (const void *)OleCreateLinkFromClip },
    { 12, 24, "OleCreateFromClip", 0, R_OWN, (const void *)OleCreateFromClip },
    { 13, 4, "OleCopyToClipboard", 0, R_OWN, (const void *)OleCopyToClipboard },
    { 14, 8, "OleQueryType", 0, R_OWN, (const void *)OleQueryType },
    { 15, 12, "OleSetHostNames", 0, R_OWN, (const void *)OleSetHostNames },
    { 16, 6, "OleSetTargetDevice", 0, R_OBJECT, NULL },
    { 17, 8, "OleSetBounds", 0, R_OBJECT, NULL },
    { 18, 8, "OleQueryBounds", 0, R_OWN, (const void *)OleQueryBounds },
    { 19, 16, "OleDraw", 0, R_OWN, (const void *)OleDraw },
    { 20, 4, "OleQueryOpen", 0, R_OWN, (const void *)OleQueryOpen },
    { 21, 16, "OleActivate", 0, R_OWN, (const void *)OleActivate },
    { 22, 4, "OleUpdate", 0, R_OWN, (const void *)OleUpdate },
    { 23, 4, "OleReconnect", 0, R_OBJECT, NULL },
    { 24, 8, "OleGetLinkUpdateOptions", 0, R_OWN, (const void *)OleGetLinkUpdateOptions },
    { 25, 6, "OleSetLinkUpdateOptions", 0, R_OWN, (const void *)OleSetLinkUpdateOptions },
    { 26, 6, "OleEnumFormats", 0, R_OWN, (const void *)OleEnumFormats },
    { 27, 4, "OleClose", 0, R_OWN, (const void *)OleClose },
    { 28, 10, "OleGetData", 0, R_OWN, (const void *)OleGetData },
    { 29, 8, "OleSetData", 0, R_OWN, (const void *)OleSetData },
    { 30, 8, "OleQueryProtocol", 0, R_OWN, (const void *)OleQueryProtocol },
    { 31, 4, "OleQueryOutOfDate", 0, R_OBJECT, NULL },
    { 32, 24, "OleObjectConvert", 0, R_OWN, (const void *)OleObjectConvert },
    { 33, 28, "OleCreateFromTemplate", 8, R_CREATE, NULL },
    { 34, 28, "OleCreate", 0, R_OWN, (const void *)OleCreate },
    { 35, 4, "OleQueryReleaseStatus", 0, R_OWN, (const void *)OleQueryReleaseStatus },
    { 36, 4, "OleQueryReleaseError", 0, R_OWN, (const void *)OleQueryReleaseStatus },
    { 37, 4, "OleQueryReleaseMethod", 0, R_OWN, NULL },
    { 38, 32, "OleCreateFromFile", 8, R_CREATE, NULL },
    { 39, 36, "OleCreateLinkFromFile", 8, R_CREATE, NULL },
    { 40, 4, "OleRelease", 0, R_OWN, (const void *)OleDelete },
    { 41, 16, "OleRegisterClientDoc", 0, R_OWN, (const void *)OleRegisterClientDoc },
    { 42, 4, "OleRevokeClientDoc", 0, R_OWN, (const void *)OleRevokeClientDoc },
    { 43, 8, "OleRenameClientDoc", 0, R_OWN, (const void *)OleRenameClientDoc },
    { 44, 4, "OleRevertClientDoc", 0, R_OWN, (const void *)OleRevertClientDoc },
    { 45, 4, "OleSavedClientDoc", 0, R_OWN, (const void *)OleSavedClientDoc },
    { 46, 8, "OleRename", 0, R_OWN, (const void *)OleRename },
    { 47, 8, "OleEnumObjects", 0, R_OWN, (const void *)OleEnumObjects },
    { 48, 12, "OleQueryName", 0, R_OWN, (const void *)OleQueryName },
    { 49, 8, "OleSetColorScheme", 0, R_OBJECT, NULL },
    { 50, 6, "OleRequestData", 0, R_OBJECT, NULL },
    { 54, 8, "OleLockServer", 0, R_OWN, NULL },
    { 55, 4, "OleUnlockServer", 0, R_OWN, NULL },
    { 56, 8, "OleQuerySize", 0, R_OWN, (const void *)OleQuerySize },
    { 57, 8, "OleExecute", 0, R_OBJECT, NULL },
    { 58, 30, "OleCreateInvisible", 10, R_CREATE, NULL },
    { 59, 0, "OleQueryClientVersion", 0, R_OWN, (const void *)OleQueryClientVersion },
    { 60, 2, "OleIsDcMeta", 0, R_OWN, (const void *)OleIsDcMeta },
};

#define NFUNCS (sizeof(funcs) / sizeof(funcs[0]))

static win_dll_export exports[NFUNCS];

static int g_str(win_dll_call *c, uint32_t far, char *dst, uint32_t cap)
{
    if (!far) {
        dst[0] = 0;
        return 1;
    }
    return NE->string(c, far, dst, cap);
}

static int put32(win_dll_call *c, uint32_t far, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return far && NE->write(c, far, b, 4);
}

static LPOLEOBJECT guest_object(uint32_t far)
{
    if (!far)
        return NULL;
    for (int i = 0; i < OBJECTS; i++)
        if (objects[i].guest == far && object_of(&objects[i]))
            return &objects[i];
    return NULL;
}

static int guest_attach(LPOLEOBJECT o)
{
    if (!NE || NE->size < WIN_DLL_NE_PROC_SIZE || !NE->task_proc ||
        !NE->task_alloc || !NE->task_free || !NE->task_write)
        return 0;
    /* OLEOBJECT starts with a far pointer to its OLEOBJECTVTBL. These
     * methods have the same Win16 frames as the corresponding exports. */
    static const uint8_t methods[] = {
        30, 40, 0, 0, 28, 29, 16, 17, 26, 49, 2, 15, 3, 6, 0, 8, 13, 19, 21,
        0, 27, 22, 23, 32, 24, 25, 46, 48, 14, 18, 56, 20, 31, 35, 36, 37, 50, 0,
    };
    uint16_t task = (uint16_t)GetCurrentTask();
    uint8_t bytes[4 + 4 * sizeof(methods)] = {0};
    uint32_t far = NE->task_alloc(task, sizeof(bytes));
    if (!far)
        return 0;
    write32(bytes, far + 4);
    for (unsigned i = 0; i < sizeof(methods); i++)
        if (methods[i]) {
            uint32_t entry = NE->task_proc(task, "OLECLI", methods[i]);
            if (!entry) {
                NE->task_free(task, far);
                return 0;
            }
            write32(bytes + 4 + 4 * i, entry);
        }
    if (!NE->task_write(task, far, bytes, sizeof(bytes))) {
        NE->task_free(task, far);
        return 0;
    }
    o->guest = far;
    return 1;
}

static OLESTATUS guest_created(win_dll_call *c, uint32_t out, OLESTATUS status, LPOLEOBJECT o)
{
    if (status != OLE_OK)
        return status;
    if (!guest_attach(o))
        status = OLE_ERROR_MEMORY;
    else if (!put32(c, out, o->guest))
        status = OLE_ERROR_ADDRESS;
    if (status != OLE_OK)
        OleDelete(o);
    return status;
}

static int get32(win_dll_call *c, uint32_t far, uint32_t *value)
{
    uint8_t b[4];
    if (!far || !NE->read(c, far, b, sizeof(b)))
        return 0;
    *value = read32(b);
    return 1;
}

static int put16(win_dll_call *c, uint32_t far, uint16_t value)
{
    uint8_t b[] = { (uint8_t)value, (uint8_t)(value >> 8) };
    return far && NE->write(c, far, b, sizeof(b));
}

static uint16_t read16(const uint8_t *b)
{
    return (uint16_t)(b[0] | b[1] << 8);
}

static int get_rect(win_dll_call *c, uint32_t far, RECT *r)
{
    uint8_t b[8];
    if (!far || !NE->read(c, far, b, sizeof(b)))
        return 0;
    *r = (RECT){ (int16_t)read16(b), (int16_t)read16(b + 2),
                 (int16_t)read16(b + 4), (int16_t)read16(b + 6) };
    return 1;
}

static OLESTATUS guest_bounds(win_dll_call *c, LPOLEOBJECT o, uint32_t far)
{
    RECT bounds;
    OLESTATUS status = OleQueryBounds(o, &bounds);
    if (status != OLE_OK)
        return status;
    if (bounds.right > INT16_MAX || bounds.bottom < INT16_MIN)
        return OLE_ERROR_SIZE;
    WORD w = (WORD)bounds.right, h = (WORD)bounds.bottom;
    uint8_t b[] = { 0, 0, 0, 0, (uint8_t)w, (uint8_t)(w >> 8),
                   (uint8_t)h, (uint8_t)(h >> 8) };
    return far && NE->write(c, far, b, sizeof(b)) ? OLE_OK : OLE_ERROR_ADDRESS;
}

static OLESTATUS guest_data(win_dll_call *c, LPOLEOBJECT o, UINT format, uint32_t out)
{
    if (!put16(c, out, 0))
        return OLE_ERROR_ADDRESS;
    HANDLE image;
    OLESTATUS status = OleGetData(o, (OLECLIPFORMAT)format, &image);
    if (status != OLE_OK)
        return status;
    unsigned slot = image == o->identity ? DATA_IDENTITY :
                    image == o->native ? DATA_NATIVE : DATA_PICTURE;
    BOOL metafile = format == CF_METAFILEPICT;
    DWORD n = metafile ? 8 : slot == DATA_PICTURE ? o->size : GlobalSize(image);
    if (!o->guest_data[slot]) {
        uint16_t task = (uint16_t)GetCurrentTask();
        uint32_t far = NE->task_alloc(task, n);
        if (!far)
            return OLE_ERROR_MEMORY;
        int ok;
        if (metafile) {
            WORD w = o->width <= INT16_MAX ? (WORD)o->width : 0;
            WORD h = o->height <= INT16_MAX ? (WORD)o->height : 0;
            uint8_t picture[] = { MM_ANISOTROPIC, 0, (uint8_t)w, (uint8_t)(w >> 8),
                                  (uint8_t)h, (uint8_t)(h >> 8),
                                  (uint8_t)o->metafile, (uint8_t)(o->metafile >> 8) };
            ok = NE->task_write(task, far, picture, n);
        } else {
            const void *p = GlobalLock(image);
            ok = p && NE->task_write(task, far, p, n);
            if (p)
                GlobalUnlock(image);
        }
        if (!ok) {
            NE->task_free(task, far);
            return OLE_ERROR_MEMORY;
        }
        o->guest_data[slot] = far;
    }
    return put16(c, out, (uint16_t)(o->guest_data[slot] >> 16)) ? OLE_OK : OLE_ERROR_ADDRESS;
}

static OLESTATUS guest_set_data(win_dll_call *c, LPOLEOBJECT o, WORD format, WORD handle)
{
    OLESTATUS status = set_data_status(o, format);
    if (status != OLE_OK)
        return status;
    if (NE->size < WIN_DLL_NE_GLOBAL_SIZE || !NE->task_global_size)
        return OLE_ERROR_GENERIC;
    DWORD size = NE->task_global_size((uint16_t)GetCurrentTask(), handle);
    if (!size)
        return OLE_ERROR_HANDLE;

    HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
    uint8_t *bytes = copy ? GlobalLock(copy) : NULL;
    status = OLE_ERROR_MEMORY;
    if (bytes) {
        uint32_t far = (uint32_t)(handle | 3u) << 16;
        status = NE->read(c, far, bytes, size) ? replace_link_source(o, bytes, size) :
                                                 OLE_ERROR_ADDRESS;
        GlobalUnlock(copy);
    }
    if (copy)
        GlobalFree(copy);
    if (status == OLE_OK)
        invalidate_identity(o);
    return status;
}

static int guest_stream(win_dll_call *c, uint32_t far, int put, picture_stream *s)
{
    uint32_t vt;
    if (!NE || NE->size < WIN_DLL_NE_BASE_SIZE || !NE->call || !NE->task_alloc ||
        !NE->task_read || !NE->task_write || !NE->task_free ||
        !get32(c, far, &vt) || !vt || (vt & 0xffffu) > 0xfff8u ||
        !get32(c, vt + (put ? 4 : 0), &s->proc) || !s->proc)
        return 0;
    s->task = (uint16_t)GetCurrentTask();
    s->stream = far;
    s->buffer = NE->task_alloc(s->task, STREAM_CHUNK);
    return s->buffer != 0;
}


static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    unsigned k = 0;
    while (k < NFUNCS && funcs[k].ordinal != ordinal)
        k++;
    if (k == NFUNCS)
        return WINDLL_NONE;
    *v = 0;
    switch (funcs[k].kind) {
    case R_OBJECT: {
        LPOLEOBJECT o = guest_object(NE->arg32(c, funcs[k].args));
        *v = !o ? OLE_ERROR_OBJECT : o->type == OT_STATIC ? OLE_ERROR_STATIC : OLE_ERROR_NOT_OPEN;
        return WINDLL_WORD;
    }
    case R_CREATE:
        put32(c, NE->arg32(c, funcs[k].made_at), 0);
        *v = OLE_ERROR_CLASS;
        return WINDLL_WORD;
    }
    switch (ordinal) {
    case 2: case 40:                    /* OleDelete / OleRelease */
        *v = OleDelete(guest_object(NE->arg32(c, 4)));
        return WINDLL_WORD;
    case 3: {                          /* OleSaveToStream */
        LPOLEOBJECT o = guest_object(NE->arg32(c, 8));
        picture_stream s = {0};
        if (!o)
            *v = OLE_ERROR_OBJECT;
        else if (!guest_stream(c, NE->arg32(c, 4), 1, &s))
            *v = OLE_ERROR_STREAM;
        else {
            *v = save_picture(o, &s);
            NE->task_free(s.task, s.buffer);
        }
        return WINDLL_WORD;
    }
    case 4: {                          /* OleLoadFromStream */
        char protocol[32], name[128];
        uint32_t out = NE->arg32(c, 4);
        picture_stream s = {0};
        LPOLEOBJECT object = NULL;
        if (!put32(c, out, 0) || !g_str(c, NE->arg32(c, 20), protocol, sizeof(protocol)) ||
            !g_str(c, NE->arg32(c, 8), name, sizeof(name))) {
            *v = OLE_ERROR_ADDRESS;
        } else if (!guest_stream(c, NE->arg32(c, 24), 0, &s)) {
            *v = OLE_ERROR_STREAM;
        } else {
            OLESTATUS result = load_picture(&s, protocol, (LHCLIENTDOC)NE->arg32(c, 12),
                                            name, &object);
            NE->task_free(s.task, s.buffer);
            *v = guest_created(c, out, result, object);
        }
        return WINDLL_WORD;
    }
    case 6: {                          /* OleClone */
        char name[128];
        uint32_t out = NE->arg32(c, 4);
        LPOLEOBJECT object = NULL;
        if (!put32(c, out, 0) || !g_str(c, NE->arg32(c, 8), name, sizeof(name))) {
            *v = OLE_ERROR_ADDRESS;
        } else {
            OLESTATUS result = OleClone(guest_object(NE->arg32(c, 20)), NULL,
                                        (LHCLIENTDOC)NE->arg32(c, 12), name, &object);
            *v = guest_created(c, out, result, object);
        }
        return WINDLL_WORD;
    }
    case 8:
        *v = OleEqual(guest_object(NE->arg32(c, 8)), guest_object(NE->arg32(c, 4)));
        return WINDLL_WORD;
    case 9: case 10: {
        char protocol[32];
        *v = g_str(c, NE->arg32(c, 8), protocol, sizeof(protocol)) ?
            query_clip_object(protocol, NE->arg16(c, 6), NE->arg16(c, 4), ordinal == 9) :
            OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 11: case 12: {
        char protocol[32], name[128];
        uint32_t out = NE->arg32(c, 8);
        LPOLEOBJECT object = NULL;
        if (!put32(c, out, 0) || !g_str(c, NE->arg32(c, 24), protocol, sizeof(protocol)) ||
            !g_str(c, NE->arg32(c, 12), name, sizeof(name))) {
            *v = OLE_ERROR_ADDRESS;
        } else {
            OLESTATUS result = create_clip_object(protocol, (LHCLIENTDOC)NE->arg32(c, 16),
                                                 name, &object, NE->arg16(c, 6),
                                                 NE->arg16(c, 4), ordinal == 11);
            *v = guest_created(c, out, result, object);
        }
        return WINDLL_WORD;
    }
    case 34: {
        char protocol[32], cls[65], name[128];
        uint32_t out = NE->arg32(c, 8);
        LPOLEOBJECT object = NULL;
        if (!put32(c, out, 0) || !NE->arg32(c, 12) ||
            !g_str(c, NE->arg32(c, 28), protocol, sizeof(protocol)) ||
            !g_str(c, NE->arg32(c, 20), cls, sizeof(cls)) ||
            !g_str(c, NE->arg32(c, 12), name, sizeof(name))) {
            *v = OLE_ERROR_ADDRESS;
        } else {
            OLESTATUS status = OleCreate(protocol, NULL, cls, (LHCLIENTDOC)NE->arg32(c, 16),
                                         name, &object, NE->arg16(c, 6), NE->arg16(c, 4));
            *v = guest_created(c, out, status, object);
        }
        return WINDLL_WORD;
    }
    case 13:
        *v = OleCopyToClipboard(guest_object(NE->arg32(c, 4)));
        return WINDLL_WORD;
    case 14: case 56: {                /* OleQueryType / OleQuerySize */
        LPOLEOBJECT o = guest_object(NE->arg32(c, 8));
        LONG type = 0;
        DWORD size = 0;
        *v = ordinal == 14 ? OleQueryType(o, &type) : OleQuerySize(o, &size);
        if (!*v && !put32(c, NE->arg32(c, 4), ordinal == 14 ? (DWORD)type : size))
            *v = OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 15: {
        char app[128], host[128];
        *v = g_str(c, NE->arg32(c, 8), app, sizeof(app)) &&
             g_str(c, NE->arg32(c, 4), host, sizeof(host)) ?
            OleSetHostNames(guest_object(NE->arg32(c, 12)), app, host) : OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 18:
        *v = guest_bounds(c, guest_object(NE->arg32(c, 8)), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 19: {                        /* OleDraw */
        RECT bounds, clip;
        uint32_t clip_far = NE->arg32(c, 6);
        *v = get_rect(c, NE->arg32(c, 10), &bounds) &&
             (!clip_far || get_rect(c, clip_far, &clip)) ?
            OleDraw(guest_object(NE->arg32(c, 16)), NE->arg16(c, 14), &bounds,
                    clip_far ? &clip : NULL, NE->arg16(c, 4)) : OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 20:
        *v = OleQueryOpen(guest_object(NE->arg32(c, 4)));
        return WINDLL_WORD;
    case 21: {
        RECT rect;
        uint32_t far = NE->arg32(c, 4);
        *v = far && !get_rect(c, far, &rect) ? OLE_ERROR_ADDRESS :
            OleActivate(guest_object(NE->arg32(c, 16)), NE->arg16(c, 14), NE->arg16(c, 12),
                        NE->arg16(c, 10), NE->arg16(c, 8), far ? &rect : NULL);
        return WINDLL_WORD;
    }
    case 22:
        *v = OleUpdate(guest_object(NE->arg32(c, 4)));
        return WINDLL_WORD;
    case 27:
        *v = OleClose(guest_object(NE->arg32(c, 4)));
        return WINDLL_WORD;
    case 24: {
        OLEOPT_UPDATE option;
        uint32_t out = NE->arg32(c, 4);
        *v = OleGetLinkUpdateOptions(guest_object(NE->arg32(c, 8)), out ? &option : NULL);
        if (*v == OLE_OK && !put16(c, out, (uint16_t)option))
            *v = OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 25:
        *v = OleSetLinkUpdateOptions(guest_object(NE->arg32(c, 6)), NE->arg16(c, 4));
        return WINDLL_WORD;
    case 26:
        *v = OleEnumFormats(guest_object(NE->arg32(c, 6)), NE->arg16(c, 4));
        return WINDLL_WORD;
    case 28:
        *v = guest_data(c, guest_object(NE->arg32(c, 10)), NE->arg16(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 29:
        *v = guest_set_data(c, guest_object(NE->arg32(c, 8)), NE->arg16(c, 6), NE->arg16(c, 4));
        return WINDLL_WORD;
    case 30: {
        char protocol[32];
        LPOLEOBJECT o = guest_object(NE->arg32(c, 8));
        if (g_str(c, NE->arg32(c, 4), protocol, sizeof(protocol)) && OleQueryProtocol(o, protocol))
            *v = o->guest;
        return WINDLL_DWORD;
    }
    case 32: {
        char protocol[32], name[128];
        uint32_t out = NE->arg32(c, 4), far_name = NE->arg32(c, 8);
        if (!put32(c, out, 0) || !far_name || !g_str(c, far_name, name, sizeof(name)) ||
            !g_str(c, NE->arg32(c, 20), protocol, sizeof(protocol))) {
            *v = OLE_ERROR_ADDRESS;
        } else {
            LPOLEOBJECT picture = NULL;
            OLESTATUS status = OleObjectConvert(guest_object(NE->arg32(c, 24)), protocol, NULL,
                (LHCLIENTDOC)NE->arg32(c, 12), name, &picture);
            *v = guest_created(c, out, status, picture);
        }
        return WINDLL_WORD;
    }
    case 35: case 36:
        *v = OleQueryReleaseStatus(guest_object(NE->arg32(c, 4)));
        return WINDLL_WORD;
    case 37:                           /* no outstanding asynchronous method */
        return WINDLL_WORD;
    case 46: {
        char name[128];
        *v = g_str(c, NE->arg32(c, 4), name, sizeof(name)) ?
            OleRename(guest_object(NE->arg32(c, 8)), name) : OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 47: {                         /* OleEnumObjects */
        uint32_t out = NE->arg32(c, 4), far;
        if (!get32(c, out, &far)) {
            *v = OLE_ERROR_ADDRESS;
        } else {
            LPOLEOBJECT object = guest_object(far);
            *v = far && !object ? OLE_ERROR_OBJECT :
                OleEnumObjects((LHCLIENTDOC)NE->arg32(c, 8), &object);
            if (!*v && !put32(c, out, object ? object->guest : 0))
                *v = OLE_ERROR_ADDRESS;
        }
        return WINDLL_WORD;
    }
    case 48: {                         /* OleQueryName */
        uint32_t size = NE->arg32(c, 4), name = NE->arg32(c, 8);
        uint8_t b[2];
        char text[128];
        if (!size || !NE->read(c, size, b, sizeof(b))) {
            *v = OLE_ERROR_ADDRESS;
        } else {
            UINT count = read16(b);
            *v = OleQueryName(guest_object(NE->arg32(c, 12)), name ? text : NULL, &count);
            if ((*v == OLE_OK || *v == OLE_ERROR_SIZE) && !put16(c, size, (uint16_t)count))
                *v = OLE_ERROR_ADDRESS;
            if (!*v && name && !NE->write(c, name, text, count))
                *v = OLE_ERROR_ADDRESS;
        }
        return WINDLL_WORD;
    }
    case 41: {                          /* OleRegisterClientDoc(cls, doc, LONG, LHCLIENTDOC FAR *) */
        char cls[64], doc[128];
        LHCLIENTDOC h = 0;
        uint32_t out = NE->arg32(c, 4);
        if (!g_str(c, NE->arg32(c, 16), cls, sizeof(cls)) ||
            !g_str(c, NE->arg32(c, 12), doc, sizeof(doc)) || !out) {
            *v = OLE_ERROR_ADDRESS;
            return WINDLL_WORD;
        }
        *v = OleRegisterClientDoc(cls, doc, (LONG)NE->arg32(c, 8), &h);
        if (!put32(c, out, (uint32_t)h)) {
            if (h)
                OleRevokeClientDoc(h);
            *v = OLE_ERROR_ADDRESS;
        }
        return WINDLL_WORD;
    }
    case 42: case 44: case 45:          /* OleRevokeClientDoc, OleRevertClientDoc, OleSavedClientDoc */
        *v = ordinal == 42 ? OleRevokeClientDoc((LHCLIENTDOC)NE->arg32(c, 4)) :
             ordinal == 44 ? OleRevertClientDoc((LHCLIENTDOC)NE->arg32(c, 4)) :
             OleSavedClientDoc((LHCLIENTDOC)NE->arg32(c, 4));
        return WINDLL_WORD;
    case 43: {                          /* OleRenameClientDoc(LHCLIENTDOC, LPCSTR) */
        char name[128];
        *v = g_str(c, NE->arg32(c, 4), name, sizeof(name)) ?
             OleRenameClientDoc((LHCLIENTDOC)NE->arg32(c, 8), name) : OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 54:
        *v = OLE_ERROR_CLASS;
        return WINDLL_WORD;
    case 55:                            /* OleUnlockServer(LHSERVER): none was locked */
        *v = OLE_ERROR_HANDLE;
        return WINDLL_WORD;
    case 59:                            /* OleQueryClientVersion() */
        *v = OleQueryClientVersion();
        return WINDLL_DWORD;
    case 60:                            /* OleIsDcMeta(HDC) */
        *v = (uint32_t)OleIsDcMeta((HDC)NE->arg16(c, 4));
        return WINDLL_WORD;
    }
    return WINDLL_NONE;
}

static void task_end(uint16_t task)
{
    for (int i = 0; i < DOCS; i++)
        if (docs[i].task == task) {
            LHCLIENTDOC h = (LHCLIENTDOC)(DOC_HANDLE + (unsigned long)i + 1);
            for (int j = 0; j < OBJECTS; j++)
                if (objects[j].doc == h)
                    free_picture(&objects[j], task);
            docs[i].task = 0;
        }
}

static void term(void)
{
    for (int i = 0; i < DOCS; i++)
        if (docs[i].task)
            task_end(docs[i].task);
}

static win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "OLECLI",
    exports, NFUNCS, ne_call, task_end, term, NULL,
};

const win_dll_info *WinDllInit(void)
{
    for (unsigned i = 0; i < NFUNCS; i++) {
        exports[i].ordinal = funcs[i].ordinal;
        exports[i].argbytes = funcs[i].args;
        exports[i].name = funcs[i].name;
        exports[i].native = funcs[i].native;
    }
    return &info;
}
