/*
 * olesvr.c - OLESVR.DLL, WINN's: OLE 1.0's server library with no clients
 * (WINN has no registration database, so no client finds a server). A
 * server registers itself and its documents, renames, saves, reverts and
 * revokes them; revoking one calls its Release method, as the library
 * does once no client holds it (none ever does), so a server waiting for
 * it goes on. A native DLL (ddi/windll.h): Win16 programs' calls of all of
 * OLESVR's (a server's methods are its own: no native ones).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "windows.h"
#include "ole.h"
#include "windll.h"

#define NE (__win_dll_ne)

#define SERVERS 8
#define DOCS 32
#define SERVER_HANDLE 0x53560000ul  /* "SV" and the index + 1 */
#define DOC_HANDLE 0x53440000ul     /* "SD" and the index + 1 */

static struct {
    uint16_t task;                  /* 0: free */
    uint32_t object;                /* its LPOLESERVER */
    char cls[64];
} servers[SERVERS];

static struct {
    uint16_t task;
    uint32_t object;                /* its LPOLESERVERDOC */
    int server;
    char name[128];
} docs[DOCS];

static int server_of(uint32_t h)
{
    uint32_t i = h - SERVER_HANDLE - 1;
    return h > SERVER_HANDLE && i < SERVERS && servers[i].task ? (int)i : -1;
}

static int doc_of(uint32_t h)
{
    uint32_t i = h - DOC_HANDLE - 1;
    return h > DOC_HANDLE && i < DOCS && docs[i].task ? (int)i : -1;
}

static int put32(win_dll_call *c, uint32_t far, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return far && NE->write(c, far, b, 4);
}

static int g_str(win_dll_call *c, uint32_t far, char *dst, uint32_t cap)
{
    if (!far) {
        dst[0] = 0;
        return 1;
    }
    return NE->string(c, far, dst, cap);
}

/* an object's method k (its vtable's k-th far pointer), called with the
 * object (OLESTATUS (CALLBACK *)(LPOBJ)) */
static void call_method(win_dll_call *c, uint16_t task, uint32_t object, int k)
{
    uint8_t p[4];
    if (!object || !NE->read(c, object, p, 4))
        return;
    uint32_t vtbl = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                    (uint32_t)p[3] << 24;
    if (!vtbl || !NE->read(c, vtbl + 4u * (uint32_t)k, p, 4))
        return;
    uint32_t proc = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                    (uint32_t)p[3] << 24;
    uint16_t words[2] = { (uint16_t)(object >> 16), (uint16_t)object };
    if (proc)
        NE->call(task, proc, words, 2);
}

#define SERVER_RELEASE 5            /* OLESERVERVTBL: Open, Create, CreateFromTemplate,
                                     * Edit, Exit, Release, Execute */
#define DOC_RELEASE 5               /* OLESERVERDOCVTBL: Save, Close, SetHostNames,
                                     * SetDocDimensions, GetObject, Release, ... */

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    uint16_t task = (uint16_t)GetCurrentTask();
    *v = OLE_OK;
    switch (ordinal) {
    case 2: {           /* OleRegisterServer(LPCSTR, LPOLESERVER, LHSERVER FAR *, HINSTANCE, use) */
        char cls[64];
        uint32_t out = NE->arg32(c, 8);
        int i = 0;
        while (i < SERVERS && servers[i].task)
            i++;
        if (!g_str(c, NE->arg32(c, 16), cls, sizeof(cls)) || !cls[0] || !out ||
            !NE->arg32(c, 12)) {
            *v = OLE_ERROR_ADDRESS;
            break;
        }
        if (i == SERVERS) {
            *v = OLE_ERROR_MEMORY;
            break;
        }
        servers[i].task = task;
        servers[i].object = NE->arg32(c, 12);
        lstrcpyn(servers[i].cls, cls, sizeof(servers[i].cls));
        if (!put32(c, out, SERVER_HANDLE + (uint32_t)i + 1)) {
            servers[i].task = 0;
            *v = OLE_ERROR_ADDRESS;
        }
        break;
    }
    case 3: {                           /* OleRevokeServer(LHSERVER) */
        int i = server_of(NE->arg32(c, 4));
        if (i < 0) {
            *v = OLE_ERROR_HANDLE;
            break;
        }
        for (int d = 0; d < DOCS; d++)  /* its documents go with it */
            if (docs[d].task && docs[d].server == i) {
                docs[d].task = 0;
                call_method(c, task, docs[d].object, DOC_RELEASE);
            }
        servers[i].task = 0;
        call_method(c, task, servers[i].object, SERVER_RELEASE);
        break;
    }
    case 4:                             /* OleBlockServer(LHSERVER) */
        *v = server_of(NE->arg32(c, 4)) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
        break;
    case 5: {                           /* OleUnblockServer(LHSERVER, BOOL FAR *): none waiting */
        uint8_t no[2] = { 0, 0 };
        if (server_of(NE->arg32(c, 8)) < 0)
            *v = OLE_ERROR_HANDLE;
        else if (!NE->arg32(c, 4) || !NE->write(c, NE->arg32(c, 4), no, 2))
            *v = OLE_ERROR_ADDRESS;
        break;
    }
    case 6: {           /* OleRegisterServerDoc(LHSERVER, LPCSTR, LPOLESERVERDOC, LHSERVERDOC FAR *) */
        char name[128];
        int s = server_of(NE->arg32(c, 16)), i = 0;
        uint32_t out = NE->arg32(c, 4);
        while (i < DOCS && docs[i].task)
            i++;
        if (s < 0) {
            *v = OLE_ERROR_HANDLE;
            break;
        }
        if (!g_str(c, NE->arg32(c, 12), name, sizeof(name)) || !out) {
            *v = OLE_ERROR_ADDRESS;
            break;
        }
        if (i == DOCS) {
            *v = OLE_ERROR_MEMORY;
            break;
        }
        docs[i].task = task;
        docs[i].object = NE->arg32(c, 8);
        docs[i].server = s;
        lstrcpyn(docs[i].name, name, sizeof(docs[i].name));
        if (!put32(c, out, DOC_HANDLE + (uint32_t)i + 1)) {
            docs[i].task = 0;
            *v = OLE_ERROR_ADDRESS;
        }
        break;
    }
    case 7: {                           /* OleRevokeServerDoc(LHSERVERDOC) */
        int i = doc_of(NE->arg32(c, 4));
        if (i < 0) {
            *v = OLE_ERROR_HANDLE;
            break;
        }
        docs[i].task = 0;
        call_method(c, task, docs[i].object, DOC_RELEASE);
        break;
    }
    case 8: {                           /* OleRenameServerDoc(LHSERVERDOC, LPCSTR) */
        char name[128];
        int i = doc_of(NE->arg32(c, 8));
        if (i < 0)
            *v = OLE_ERROR_HANDLE;
        else if (!g_str(c, NE->arg32(c, 4), name, sizeof(name)))
            *v = OLE_ERROR_ADDRESS;
        else
            lstrcpyn(docs[i].name, name, sizeof(docs[i].name));
        break;
    }
    case 9: case 10:                    /* OleRevertServerDoc, OleSavedServerDoc: no client to tell */
        *v = doc_of(NE->arg32(c, 4)) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
        break;
    case 11:                            /* OleRevokeObject(LPOLECLIENT): no client has one */
        break;
    case 12:                            /* OleQueryServerVersion() */
        *v = 0x0001;
        return WINDLL_DWORD;
    default:
        return WINDLL_NONE;
    }
    return WINDLL_WORD;
}

static void task_end(uint16_t task)
{
    for (int i = 0; i < SERVERS; i++)
        if (servers[i].task == task)
            servers[i].task = 0;
    for (int i = 0; i < DOCS; i++)
        if (docs[i].task == task)
            docs[i].task = 0;
}

static const win_dll_export exports[] = {
    { 2, 16, "OleRegisterServer", NULL },
    { 3, 4, "OleRevokeServer", NULL },
    { 4, 4, "OleBlockServer", NULL },
    { 5, 8, "OleUnblockServer", NULL },
    { 6, 16, "OleRegisterServerDoc", NULL },
    { 7, 4, "OleRevokeServerDoc", NULL },
    { 8, 8, "OleRenameServerDoc", NULL },
    { 9, 4, "OleRevertServerDoc", NULL },
    { 10, 4, "OleSavedServerDoc", NULL },
    { 11, 4, "OleRevokeObject", NULL },
    { 12, 0, "OleQueryServerVersion", NULL },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "OLESVR",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
