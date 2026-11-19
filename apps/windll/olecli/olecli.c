/*
 * olecli.c - OLE 1.0 client documents and static DIB objects.
 * Each task owns its documents, pictures and Win16 object pointers.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "windows.h"
#include "ole.h"
#include "windll.h"

#define NE (__win_dll_ne)

#define DOCS 32
#define DOC_HANDLE 0x4f4c0000ul      /* "OL" and the index + 1 */

static struct {
    uint16_t task;                  /* 0: free */
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

/* Static pictures use a private packed DIB. Clipboard handles can disappear
 * as soon as the caller closes or replaces the clipboard. */
struct WINN_OLEOBJECT {
    LHCLIENTDOC doc;
    HGLOBAL image;
    DWORD size, bits, guest, guest_data;
    SIZE pixels;
    LONG width, height;              /* positive HIMETRIC extents */
    BOOL writing;
    char name[128];
};

#define OBJECTS 32
#define STATIC_HEADER 28
#define STREAM_CHUNK 4096

static OLEOBJECT objects[OBJECTS];

static OLEOBJECT *object_of(LPOLEOBJECT object)
{
    for (int i = 0; i < OBJECTS; i++)
        if (&objects[i] == object && objects[i].image && doc_of(objects[i].doc) >= 0)
            return &objects[i];
    return NULL;
}

static int static_protocol(LPCSTR protocol)
{
    return protocol && !lstrcmpi(protocol, "Static");
}

static UINT static_format(UINT render, OLECLIPFORMAT fmt)
{
    if (render == olerender_format)
        return fmt == CF_DIB && IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0;
    if (render == olerender_draw) {
        if (IsClipboardFormatAvailable(CF_DIB))
            return CF_DIB;
        if (IsClipboardFormatAvailable(CF_BITMAP))
            return CF_BITMAP;
    }
    return 0;
}

static OLESTATUS WINAPI OleQueryCreateFromClip(LPCSTR protocol, UINT render, OLECLIPFORMAT fmt)
{
    if (!static_protocol(protocol))
        return OLE_ERROR_FORMAT;
    return static_format(render, fmt) ? OLE_OK : OLE_ERROR_FORMAT;
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

/* Takes ownership of the private DIB even when creation fails. */
static OLESTATUS new_picture(HGLOBAL image, LHCLIENTDOC doc, LPCSTR name, LONG w, LONG h,
                             LPOLEOBJECT *out)
{
    SIZE pixels;
    DWORD bits;
    void *data = GlobalLock(image);
    DWORD size = data ? GetPackedDIBInfoN(data, GlobalSize(image), &pixels, &bits) : 0;
    if (data)
        GlobalUnlock(image);
    OLESTATUS status = !size ? OLE_ERROR_FORMAT : doc_of(doc) < 0 ? OLE_ERROR_HANDLE :
                       !name ? OLE_ERROR_ADDRESS : OLE_ERROR_MEMORY;
    if (status == OLE_ERROR_MEMORY) {
        for (int i = 0; i < OBJECTS; i++)
            if (!objects[i].image) {
                HDC screen = (w && h) ? 0 : GetDC(0);
                int dx = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 0;
                int dy = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 0;
                if (screen)
                    ReleaseDC(0, screen);
                objects[i] = (OLEOBJECT){ .doc = doc, .image = image, .size = size, .bits = bits,
                                          .pixels = pixels,
                                          .width = w ? w : MulDiv(pixels.cx, 2540, dx > 0 ? dx : 96),
                                          .height = h ? h : MulDiv(pixels.cy, 2540, dy > 0 ? dy : 96) };
                lstrcpyn(objects[i].name, name, sizeof(objects[i].name));
                *out = &objects[i];
                return OLE_OK;
            }
    }
    GlobalFree(image);
    return status;
}

static OLESTATUS WINAPI OleCreateFromClip(LPCSTR protocol, LPOLECLIENT client, LHCLIENTDOC doc,
                                         LPCSTR name, LPOLEOBJECT *out, UINT render,
                                         OLECLIPFORMAT format)
{
    (void)client;
    if (!out || !name)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (doc_of(doc) < 0)
        return OLE_ERROR_HANDLE;
    UINT fmt = static_protocol(protocol) ? static_format(render, format) : 0;
    if (!fmt)
        return OLE_ERROR_FORMAT;
    HANDLE data = GetClipboardData(fmt);
    if (!data)
        return OLE_ERROR_CLIPBOARD;
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
    return image ? new_picture(image, doc, name, 0, 0, out) : OLE_ERROR_MEMORY;
}

static void free_picture(OLEOBJECT *o, uint16_t task)
{
    if (o->guest_data)
        NE->task_free(task, o->guest_data);
    if (o->guest)
        NE->task_free(task, o->guest);
    GlobalFree(o->image);
    memset(o, 0, sizeof(*o));
}

static OLESTATUS WINAPI OleDelete(LPOLEOBJECT object)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->writing)
        return OLE_BUSY;
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
    const void *p = GlobalLock(o->image);
    HGLOBAL image = p ? copy_block(p, o->size) : 0;
    if (p)
        GlobalUnlock(o->image);
    return image ? new_picture(image, doc, name, o->width, o->height, out) : OLE_ERROR_MEMORY;
}

static OLESTATUS WINAPI OleEqual(LPOLEOBJECT a, LPOLEOBJECT b)
{
    OLEOBJECT *x = object_of(a), *y = object_of(b);
    if (!x || !y)
        return OLE_ERROR_OBJECT;
    if (x == y)
        return OLE_OK;
    if (x->width != y->width || x->height != y->height || x->size != y->size)
        return OLE_ERROR_NOT_EQUAL;
    const void *p = GlobalLock(x->image), *q = GlobalLock(y->image);
    OLESTATUS status = p && q && !memcmp(p, q, x->size) ? OLE_OK : OLE_ERROR_NOT_EQUAL;
    if (q)
        GlobalUnlock(y->image);
    if (p)
        GlobalUnlock(x->image);
    return status;
}

static OLESTATUS WINAPI OleCopyToClipboard(LPOLEOBJECT object)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    const void *p = GlobalLock(o->image);
    HGLOBAL copy = p ? copy_block(p, o->size) : 0;
    if (p)
        GlobalUnlock(o->image);
    if (!copy)
        return OLE_ERROR_MEMORY;
    if (SetClipboardData(CF_DIB, copy))
        return OLE_OK;
    GlobalFree(copy);
    return OLE_ERROR_CLIPBOARD;
}

static OLESTATUS WINAPI OleQueryType(LPOLEOBJECT object, LONG *type)
{
    if (!object_of(object))
        return OLE_ERROR_OBJECT;
    if (!type)
        return OLE_ERROR_ADDRESS;
    *type = OT_STATIC;
    return OLE_OK;
}

static OLESTATUS WINAPI OleQueryBounds(LPOLEOBJECT object, RECT *bounds)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (!bounds)
        return OLE_ERROR_ADDRESS;
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
    int64_t w = (int64_t)bounds->right - bounds->left, h = (int64_t)bounds->bottom - bounds->top;
    if (!w || !h || w <= INT32_MIN || w > INT32_MAX || h <= INT32_MIN || h > INT32_MAX)
        return OLE_ERROR_DRAW;
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

static OLECLIPFORMAT WINAPI OleEnumFormats(LPOLEOBJECT object, OLECLIPFORMAT previous)
{
    return !previous && object_of(object) ? CF_DIB : 0;
}

static OLESTATUS WINAPI OleGetData(LPOLEOBJECT object, OLECLIPFORMAT fmt, HANDLE *out)
{
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = 0;
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (fmt != CF_DIB)
        return OLE_ERROR_FORMAT;
    *out = o->image;
    return OLE_OK;
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
        if (objects[next].image && objects[next].doc == doc) {
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
    *size = STATIC_HEADER + o->size;
    return OLE_OK;
}

static OLESTATUS WINAPI OleQueryOpen(LPOLEOBJECT object)
{
    return object_of(object) ? OLE_ERROR_NOT_OPEN : OLE_ERROR_OBJECT;
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

static OLESTATUS save_picture(LPOLEOBJECT object, picture_stream *stream)
{
    OLEOBJECT *o = object_of(object);
    if (!o)
        return OLE_ERROR_OBJECT;
    if (o->writing)
        return OLE_BUSY;
    uint8_t header[STATIC_HEADER] = {0};
    write32(header, 0x501);
    write32(header + 4, OT_STATIC);
    write32(header + 8, 4);
    memcpy(header + 12, "DIB", 4);
    write32(header + 16, (DWORD)o->width);
    write32(header + 20, (DWORD)-o->height);
    write32(header + 24, o->size);
    void *image = GlobalLock(o->image);
    if (!image)
        return OLE_ERROR_BLANK;
    o->writing = TRUE;
    int ok = stream_transfer(stream, header, sizeof(header), 1) &&
             stream_transfer(stream, image, o->size, 1);
    o->writing = FALSE;
    GlobalUnlock(o->image);
    return ok ? OLE_OK : OLE_ERROR_STREAM;
}

static OLESTATUS load_picture(picture_stream *stream, LPCSTR protocol, LHCLIENTDOC doc,
                              LPCSTR name, LPOLEOBJECT *out)
{
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = NULL;
    if (doc_of(doc) < 0)
        return OLE_ERROR_HANDLE;
    if (!static_protocol(protocol) && (!protocol || lstrcmpi(protocol, "StdFileEditing")))
        return OLE_ERROR_PROTOCOL;
    uint8_t b[16];
    if (!stream_transfer(stream, b, 8, 0))
        return OLE_ERROR_STREAM;
    DWORD version = read32(b);
    if ((version & 0xffffff00u) != 0x500u || read32(b + 4) != OT_STATIC)
        return OLE_ERROR_FORMAT;
    if (!stream_transfer(stream, b, 4, 0))
        return OLE_ERROR_STREAM;
    if (read32(b) != 4)
        return OLE_ERROR_FORMAT;
    if (!stream_transfer(stream, b, sizeof(b), 0))
        return OLE_ERROR_STREAM;
    LONG w = (LONG)read32(b + 4), h = (LONG)read32(b + 8);
    DWORD n = read32(b + 12);
    if (memcmp(b, "DIB", 4) || w <= 0 || h == INT32_MIN || n < 12)
        return OLE_ERROR_FORMAT;
    HGLOBAL image = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n);
    void *p = image ? GlobalLock(image) : NULL;
    if (!p) {
        if (image)
            GlobalFree(image);
        return OLE_ERROR_MEMORY;
    }
    int ok = stream_transfer(stream, p, n, 0);
    GlobalUnlock(image);
    if (!ok) {
        GlobalFree(image);
        return OLE_ERROR_STREAM;
    }
    return new_picture(image, doc, name, w, h < 0 ? -h : h, out);
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
    for (int j = 0; j < OBJECTS; j++)
        if (objects[j].image && objects[j].doc == h)
            return OLE_ERROR_NOT_EMPTY;
    docs[i].task = 0;
    return OLE_OK;
}

static OLESTATUS WINAPI OleQueryLinkFromClip(LPCSTR protocol, UINT render, OLECLIPFORMAT f)
{
    (void)protocol; (void)render; (void)f;
    return OLE_ERROR_FORMAT;
}

static DWORD WINAPI OleQueryClientVersion(void) { return 0x0001; }    /* OLE 1.0 */

static BOOL WINAPI OleIsDcMeta(HDC dc) { return GetDeviceCaps(dc, TECHNOLOGY) == DT_METAFILE; }

static LPOLEOBJECT WINAPI OleQueryProtocol(LPOLEOBJECT o, LPCSTR protocol)
{
    return object_of(o) && static_protocol(protocol) ? o : NULL;
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
    { 11, 24, "OleCreateLinkFromClip", 8, R_CREATE, NULL },
    { 12, 24, "OleCreateFromClip", 0, R_OWN, (const void *)OleCreateFromClip },
    { 13, 4, "OleCopyToClipboard", 0, R_OWN, (const void *)OleCopyToClipboard },
    { 14, 8, "OleQueryType", 0, R_OWN, (const void *)OleQueryType },
    { 15, 12, "OleSetHostNames", 0, R_OWN, (const void *)OleSetHostNames },
    { 16, 6, "OleSetTargetDevice", 0, R_OBJECT, NULL },
    { 17, 8, "OleSetBounds", 0, R_OBJECT, NULL },
    { 18, 8, "OleQueryBounds", 0, R_OWN, (const void *)OleQueryBounds },
    { 19, 16, "OleDraw", 0, R_OWN, (const void *)OleDraw },
    { 20, 4, "OleQueryOpen", 0, R_OWN, (const void *)OleQueryOpen },
    { 21, 16, "OleActivate", 0, R_OBJECT, NULL },
    { 22, 4, "OleUpdate", 0, R_OBJECT, NULL },
    { 23, 4, "OleReconnect", 0, R_OBJECT, NULL },
    { 24, 8, "OleGetLinkUpdateOptions", 0, R_OBJECT, NULL },
    { 25, 6, "OleSetLinkUpdateOptions", 0, R_OBJECT, NULL },
    { 26, 6, "OleEnumFormats", 0, R_OWN, (const void *)OleEnumFormats },
    { 27, 4, "OleClose", 0, R_OBJECT, NULL },
    { 28, 10, "OleGetData", 0, R_OWN, (const void *)OleGetData },
    { 29, 8, "OleSetData", 0, R_OBJECT, NULL },
    { 30, 8, "OleQueryProtocol", 0, R_OWN, (const void *)OleQueryProtocol },
    { 31, 4, "OleQueryOutOfDate", 0, R_OBJECT, NULL },
    { 32, 24, "OleObjectConvert", 4, R_CREATE, NULL },
    { 33, 28, "OleCreateFromTemplate", 8, R_CREATE, NULL },
    { 34, 28, "OleCreate", 8, R_CREATE, NULL },
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
    if (!NE || NE->size < sizeof(*NE) || !NE->task_proc ||
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
    if (o->size > 0x10000u)
        return OLE_ERROR_SIZE;
    if (!o->guest_data) {
        uint16_t task = (uint16_t)GetCurrentTask();
        uint32_t far = NE->task_alloc(task, o->size);
        if (!far)
            return OLE_ERROR_MEMORY;
        const void *p = GlobalLock(image);
        int ok = p && NE->task_write(task, far, p, o->size);
        if (p)
            GlobalUnlock(image);
        if (!ok) {
            NE->task_free(task, far);
            return OLE_ERROR_MEMORY;
        }
        o->guest_data = far;
    }
    return put16(c, out, (uint16_t)(o->guest_data >> 16)) ? OLE_OK : OLE_ERROR_ADDRESS;
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
    case R_OBJECT:
        *v = guest_object(NE->arg32(c, funcs[k].args)) ?
            OLE_ERROR_STATIC : OLE_ERROR_OBJECT;
        return WINDLL_WORD;
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
    case 9:
        *v = OLE_ERROR_FORMAT;
        return WINDLL_WORD;
    case 10: {                        /* OleQueryCreateFromClip */
        char protocol[32];
        *v = g_str(c, NE->arg32(c, 8), protocol, sizeof(protocol)) ?
            OleQueryCreateFromClip(protocol, NE->arg16(c, 6), NE->arg16(c, 4)) : OLE_ERROR_ADDRESS;
        return WINDLL_WORD;
    }
    case 12: {                        /* OleCreateFromClip */
        char protocol[32], name[128];
        uint32_t out = NE->arg32(c, 8);
        LPOLEOBJECT object = NULL;
        if (!put32(c, out, 0) || !g_str(c, NE->arg32(c, 24), protocol, sizeof(protocol)) ||
            !g_str(c, NE->arg32(c, 12), name, sizeof(name))) {
            *v = OLE_ERROR_ADDRESS;
        } else {
            OLESTATUS result = OleCreateFromClip(protocol, NULL, (LHCLIENTDOC)NE->arg32(c, 16),
                                                name, &object, NE->arg16(c, 6), NE->arg16(c, 4));
            *v = guest_created(c, out, result, object);
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
    case 26:
        *v = OleEnumFormats(guest_object(NE->arg32(c, 6)), NE->arg16(c, 4));
        return WINDLL_WORD;
    case 28:
        *v = guest_data(c, guest_object(NE->arg32(c, 10)), NE->arg16(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 30: {
        char protocol[32];
        LPOLEOBJECT o = guest_object(NE->arg32(c, 8));
        if (g_str(c, NE->arg32(c, 4), protocol, sizeof(protocol)) && OleQueryProtocol(o, protocol))
            *v = o->guest;
        return WINDLL_DWORD;
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
                if (objects[j].image && objects[j].doc == h)
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
