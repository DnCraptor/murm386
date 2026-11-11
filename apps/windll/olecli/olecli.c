/*
 * olecli.c - OLECLI.DLL, WINN's: OLE 1.0's client library with no
 * servers (WINN has no registration database, so no class has one). A
 * client's documents are registered, renamed, saved, reverted and
 * revoked; nothing on the clipboard can become an object (the queries say
 * OLE_ERROR_FORMAT), no object is ever made (OleCreate and the rest:
 * OLE_ERROR_CLASS, the object NULL), and a call about an object has none
 * (OLE_ERROR_OBJECT). A native DLL (ddi/windll.h): its functions for
 * native callers, and Win16 programs' calls of all of OLECLI's.
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
    return (unsigned long)h > DOC_HANDLE && i < DOCS && docs[i].task ? (int)i : -1;
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

static OLESTATUS WINAPI OleRevokeClientDoc(LHCLIENTDOC h)
{
    int i = doc_of(h);
    if (i < 0)
        return OLE_ERROR_HANDLE;
    docs[i].task = 0;
    return OLE_OK;
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

/* saved, reverted: the document has no objects to tell */
static OLESTATUS WINAPI OleRevertClientDoc(LHCLIENTDOC h)
{
    return doc_of(h) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
}

static OLESTATUS WINAPI OleSavedClientDoc(LHCLIENTDOC h)
{
    return doc_of(h) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
}

/* nothing on the clipboard is an object of a server's */
static OLESTATUS WINAPI OleQueryCreateFromClip(LPCSTR protocol, UINT render, OLECLIPFORMAT f)
{
    (void)protocol; (void)render; (void)f;
    return OLE_ERROR_FORMAT;
}

static OLESTATUS WINAPI OleQueryLinkFromClip(LPCSTR protocol, UINT render, OLECLIPFORMAT f)
{
    (void)protocol; (void)render; (void)f;
    return OLE_ERROR_FORMAT;
}

static DWORD WINAPI OleQueryClientVersion(void) { return 0x0001; }    /* OLE 1.0 */

static BOOL WINAPI OleIsDcMeta(HDC dc) { return GetDeviceCaps(dc, TECHNOLOGY) == DT_METAFILE; }

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
    { 2, 4, "OleDelete", 0, R_OBJECT, NULL },
    { 3, 8, "OleSaveToStream", 0, R_OBJECT, NULL },
    { 4, 24, "OleLoadFromStream", 4, R_CREATE, NULL },
    { 6, 20, "OleClone", 4, R_CREATE, NULL },
    { 7, 24, "OleCopyFromLink", 4, R_CREATE, NULL },
    { 8, 8, "OleEqual", 0, R_OBJECT, NULL },
    { 9, 8, "OleQueryLinkFromClip", 0, R_OWN, (const void *)OleQueryLinkFromClip },
    { 10, 8, "OleQueryCreateFromClip", 0, R_OWN, (const void *)OleQueryCreateFromClip },
    { 11, 24, "OleCreateLinkFromClip", 8, R_CREATE, NULL },
    { 12, 24, "OleCreateFromClip", 8, R_CREATE, NULL },
    { 13, 4, "OleCopyToClipboard", 0, R_OBJECT, NULL },
    { 14, 8, "OleQueryType", 0, R_OBJECT, NULL },
    { 15, 12, "OleSetHostNames", 0, R_OBJECT, NULL },
    { 16, 6, "OleSetTargetDevice", 0, R_OBJECT, NULL },
    { 17, 8, "OleSetBounds", 0, R_OBJECT, NULL },
    { 18, 8, "OleQueryBounds", 0, R_OBJECT, NULL },
    { 19, 16, "OleDraw", 0, R_OBJECT, NULL },
    { 20, 4, "OleQueryOpen", 0, R_OBJECT, NULL },
    { 21, 16, "OleActivate", 0, R_OBJECT, NULL },
    { 22, 4, "OleUpdate", 0, R_OBJECT, NULL },
    { 23, 4, "OleReconnect", 0, R_OBJECT, NULL },
    { 24, 8, "OleGetLinkUpdateOptions", 0, R_OBJECT, NULL },
    { 25, 6, "OleSetLinkUpdateOptions", 0, R_OBJECT, NULL },
    { 26, 6, "OleEnumFormats", 0, R_OWN, NULL },
    { 27, 4, "OleClose", 0, R_OBJECT, NULL },
    { 28, 10, "OleGetData", 0, R_OBJECT, NULL },
    { 29, 8, "OleSetData", 0, R_OBJECT, NULL },
    { 30, 8, "OleQueryProtocol", 0, R_OWN, NULL },
    { 31, 4, "OleQueryOutOfDate", 0, R_OBJECT, NULL },
    { 32, 24, "OleObjectConvert", 4, R_CREATE, NULL },
    { 33, 28, "OleCreateFromTemplate", 8, R_CREATE, NULL },
    { 34, 28, "OleCreate", 8, R_CREATE, NULL },
    { 35, 4, "OleQueryReleaseStatus", 0, R_OBJECT, NULL },
    { 36, 4, "OleQueryReleaseError", 0, R_OBJECT, NULL },
    { 37, 4, "OleQueryReleaseMethod", 0, R_OWN, NULL },
    { 38, 32, "OleCreateFromFile", 8, R_CREATE, NULL },
    { 39, 36, "OleCreateLinkFromFile", 8, R_CREATE, NULL },
    { 40, 4, "OleRelease", 0, R_OBJECT, NULL },
    { 41, 16, "OleRegisterClientDoc", 0, R_OWN, (const void *)OleRegisterClientDoc },
    { 42, 4, "OleRevokeClientDoc", 0, R_OWN, (const void *)OleRevokeClientDoc },
    { 43, 8, "OleRenameClientDoc", 0, R_OWN, (const void *)OleRenameClientDoc },
    { 44, 4, "OleRevertClientDoc", 0, R_OWN, (const void *)OleRevertClientDoc },
    { 45, 4, "OleSavedClientDoc", 0, R_OWN, (const void *)OleSavedClientDoc },
    { 46, 8, "OleRename", 0, R_OBJECT, NULL },
    { 47, 8, "OleEnumObjects", 0, R_OWN, NULL },
    { 48, 12, "OleQueryName", 0, R_OBJECT, NULL },
    { 49, 8, "OleSetColorScheme", 0, R_OBJECT, NULL },
    { 50, 6, "OleRequestData", 0, R_OBJECT, NULL },
    { 54, 8, "OleLockServer", 0, R_OBJECT, NULL },
    { 55, 4, "OleUnlockServer", 0, R_OWN, NULL },
    { 56, 8, "OleQuerySize", 0, R_OBJECT, NULL },
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
        *v = OLE_ERROR_OBJECT;
        return WINDLL_WORD;
    case R_CREATE:
        put32(c, NE->arg32(c, funcs[k].made_at), 0);
        *v = OLE_ERROR_CLASS;
        return WINDLL_WORD;
    }
    switch (ordinal) {
    case 9: case 10:                    /* OleQueryLinkFromClip, OleQueryCreateFromClip */
        *v = OLE_ERROR_FORMAT;
        return WINDLL_WORD;
    case 26:                            /* OleEnumFormats(obj, format): no more */
        *v = 0;
        return WINDLL_WORD;
    case 30:                            /* OleQueryProtocol(obj, LPCSTR): none */
        return WINDLL_DWORD;
    case 37:                            /* OleQueryReleaseMethod(obj): OLE_NONE */
        *v = 0;
        return WINDLL_WORD;
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
        if (!put32(c, out, (uint32_t)h))
            *v = OLE_ERROR_ADDRESS;
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
    case 47:                            /* OleEnumObjects(LHCLIENTDOC, LPOLEOBJECT FAR *): none */
        if (doc_of((LHCLIENTDOC)NE->arg32(c, 8)) < 0) {
            *v = OLE_ERROR_HANDLE;
            return WINDLL_WORD;
        }
        *v = put32(c, NE->arg32(c, 4), 0) ? OLE_OK : OLE_ERROR_ADDRESS;
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
        if (docs[i].task == task)
            docs[i].task = 0;
}

static win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "OLECLI",
    exports, NFUNCS, ne_call, task_end, NULL, NULL,
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
