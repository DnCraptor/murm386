/*
 * olesvr.c - OLE 1.0 server and document registration for WINN.
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

typedef struct {
    HTASK task;
    uint32_t guest;
} server_owner;

static struct {
    server_owner owner;
    LHSERVER handle;
    LPOLESERVER native;
    BOOL revoking;
    char cls[64];
} servers[SERVERS];

static struct {
    server_owner owner;
    LHSERVERDOC handle;
    LHSERVER server;
    LPOLESERVERDOC native;
    BOOL closing, revoked;
    char name[128];
} docs[DOCS];

/* Keep an old handle invalid even when a callback reuses its slot. */
static uint32_t next_handle = 0x53000000u;

static int server_of(LHSERVER handle)
{
    HTASK task = GetCurrentTask();
    if (!handle || !task)
        return -1;
    for (int i = 0; i < SERVERS; i++)
        if (servers[i].owner.task == task && servers[i].handle == handle && !servers[i].revoking)
            return i;
    return -1;
}

static int doc_of(LHSERVERDOC handle)
{
    HTASK task = GetCurrentTask();
    if (!handle || !task)
        return -1;
    for (int i = 0; i < DOCS; i++)
        if (docs[i].owner.task == task && docs[i].handle == handle)
            return i;
    return -1;
}

static uint32_t read32(const uint8_t b[4])
{
    return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static int put32(win_dll_call *c, uint32_t far, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return far && NE->write(c, far, b, sizeof(b));
}

static OLESTATUS guest_method(server_owner owner, unsigned slot, OLESTATUS missing)
{
    unsigned offset = 4 * slot;
    uint8_t p[4];
    if (!NE || !NE->call || !NE->task_read || !(owner.guest >> 16) ||
        !NE->task_read((uint16_t)owner.task, owner.guest, p, sizeof(p)))
        return OLE_ERROR_OBJECT;
    uint32_t vtbl = read32(p);
    if (!(vtbl >> 16) || (vtbl & 0xffffu) > 0xfffcu - offset ||
        !NE->task_read((uint16_t)owner.task, vtbl + offset, p, sizeof(p)))
        return OLE_ERROR_OBJECT;
    uint32_t proc = read32(p);
    uint16_t words[] = { (uint16_t)(owner.guest >> 16), (uint16_t)owner.guest };
    return proc >> 16 ? (WORD)NE->call((uint16_t)owner.task, proc, words, 2) : missing;
}

static OLESTATUS register_server(LPCSTR cls, LPOLESERVER native, uint32_t guest,
                                 LHSERVER *out, HINSTANCE instance, OLE_SERVER_USE use)
{
    (void)instance;                     /* used when routing client requests */
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = 0;
    if (!cls || !cls[0] || (!native && !(guest >> 16)))
        return OLE_ERROR_ADDRESS;
    if (use != OLE_SERVER_SINGLE && use != OLE_SERVER_MULTI)
        return OLE_ERROR_OPTION;
    if (strlen(cls) >= sizeof(servers[0].cls))
        return OLE_ERROR_NAME;
    for (int i = 0; i < SERVERS; i++)
        if (!servers[i].owner.task) {
            servers[i].owner = (server_owner){ GetCurrentTask(), guest };
            servers[i].handle = (LHSERVER)++next_handle;
            servers[i].native = native;
            lstrcpyn(servers[i].cls, cls, sizeof(servers[i].cls));
            *out = servers[i].handle;
            return OLE_OK;
        }
    return OLE_ERROR_MEMORY;
}

static OLESTATUS WINAPI OleRegisterServer(LPCSTR cls, LPOLESERVER server,
                                           LHSERVER *out, HINSTANCE instance, OLE_SERVER_USE use)
{
    return register_server(cls, server, 0, out, instance, use);
}

static OLESTATUS register_doc(LHSERVER server, LPCSTR name, LPOLESERVERDOC native,
                              uint32_t guest, LHSERVERDOC *out)
{
    if (!out)
        return OLE_ERROR_ADDRESS;
    *out = 0;
    int s = server_of(server);
    if (s < 0)
        return OLE_ERROR_HANDLE;
    if (!name || (!native && !(guest >> 16)))
        return OLE_ERROR_ADDRESS;
    if (strlen(name) >= sizeof(docs[0].name))
        return OLE_ERROR_NAME;
    if (!!native != !!servers[s].native)
        return OLE_ERROR_HANDLE;
    for (int i = 0; i < DOCS; i++)
        if (!docs[i].owner.task) {
            docs[i].owner = (server_owner){ GetCurrentTask(), guest };
            docs[i].handle = (LHSERVERDOC)++next_handle;
            docs[i].server = server;
            docs[i].native = native;
            lstrcpyn(docs[i].name, name, sizeof(docs[i].name));
            *out = docs[i].handle;
            return OLE_OK;
        }
    return OLE_ERROR_MEMORY;
}

static OLESTATUS WINAPI OleRegisterServerDoc(LHSERVER server, LPCSTR name,
                                              LPOLESERVERDOC doc, LHSERVERDOC *out)
{
    return register_doc(server, name, doc, 0, out);
}

enum { DOC_CLOSE = 1, DOC_RELEASE = 5, SERVER_RELEASE = 5 };

static void revoke_doc(int i)
{
    server_owner owner = docs[i].owner;
    LPOLESERVERDOC doc = docs[i].native;
    /* A Release can call registration or revocation again. Never touch
     * the old slot after the callback. */
    memset(&docs[i], 0, sizeof(docs[i]));
    if (!doc)
        guest_method(owner, DOC_RELEASE, OLE_OK);
    else if (doc->lpvtbl && doc->lpvtbl->Release)
        doc->lpvtbl->Release(doc);
}

static OLESTATUS WINAPI OleRevokeServerDoc(LHSERVERDOC doc)
{
    int i = doc_of(doc);
    if (i < 0)
        return OLE_ERROR_HANDLE;
    if (docs[i].closing) {
        docs[i].revoked = TRUE;
        return OLE_WAIT_FOR_RELEASE;
    }
    revoke_doc(i);
    return OLE_OK;
}

static OLESTATUS close_doc(int i)
{
    LHSERVERDOC handle = docs[i].handle;
    LPOLESERVERDOC doc = docs[i].native;
    docs[i].closing = TRUE;
    OLESTATUS status;
    if (!doc)
        status = guest_method(docs[i].owner, DOC_CLOSE, OLE_OK);
    else
        status = doc->lpvtbl && doc->lpvtbl->Close ? doc->lpvtbl->Close(doc) : OLE_OK;
    if (docs[i].handle == handle) {
        if (status == OLE_OK || docs[i].revoked)
            revoke_doc(i);
        else
            docs[i].closing = FALSE;
    }
    return status;
}

static OLESTATUS WINAPI OleRevokeServer(LHSERVER handle)
{
    int i = server_of(handle);
    if (i < 0)
        return OLE_ERROR_HANDLE;

    server_owner owner = servers[i].owner;
    LPOLESERVER server = servers[i].native;
    servers[i].revoking = TRUE;
    for (int d = 0; d < DOCS; d++)
        if (docs[d].owner.task == owner.task && docs[d].server == handle) {
            OLESTATUS status = close_doc(d);
            if (servers[i].handle != handle)
                return OLE_ERROR_HANDLE;
            if (status != OLE_OK) {
                servers[i].revoking = FALSE;
                return status;
            }
        }
    memset(&servers[i], 0, sizeof(servers[i]));
    if (!server)
        guest_method(owner, SERVER_RELEASE, OLE_OK);
    else if (server->lpvtbl && server->lpvtbl->Release)
        server->lpvtbl->Release(server);
    return OLE_OK;
}

static OLESTATUS WINAPI OleRenameServerDoc(LHSERVERDOC doc, LPCSTR name)
{
    int i = doc_of(doc);
    if (i < 0)
        return OLE_ERROR_HANDLE;
    if (!name)
        return OLE_ERROR_ADDRESS;
    if (strlen(name) >= sizeof(docs[i].name))
        return OLE_ERROR_NAME;
    lstrcpyn(docs[i].name, name, sizeof(docs[i].name));
    return OLE_OK;
}

static DWORD WINAPI OleQueryServerVersion(void)
{
    return 0x0001;
}

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    *v = OLE_OK;
    switch (ordinal) {
    case 2: {
        char cls[sizeof(servers[0].cls) + 1];
        LHSERVER handle;
        uint32_t out = NE->arg32(c, 8), name = NE->arg32(c, 16);
        if (!put32(c, out, 0) || !name || !NE->string(c, name, cls, sizeof(cls))) {
            *v = OLE_ERROR_ADDRESS;
            break;
        }
        *v = register_server(cls, NULL, NE->arg32(c, 12), &handle,
                             NE->arg16(c, 6), NE->arg16(c, 4));
        if (*v == OLE_OK && !put32(c, out, (uint32_t)handle)) {
            memset(&servers[server_of(handle)], 0, sizeof(servers[0]));
            *v = OLE_ERROR_ADDRESS;
        }
        break;
    }
    case 3:
        *v = OleRevokeServer((LHSERVER)NE->arg32(c, 4));
        break;
    case 4:                             /* OleBlockServer */
        *v = server_of((LHSERVER)NE->arg32(c, 4)) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
        break;
    case 5: {                           /* OleUnblockServer: no queue yet */
        uint8_t no[2] = { 0, 0 };
        uint32_t out = NE->arg32(c, 4);
        if (server_of((LHSERVER)NE->arg32(c, 8)) < 0)
            *v = OLE_ERROR_HANDLE;
        else if (!out || !NE->write(c, out, no, sizeof(no)))
            *v = OLE_ERROR_ADDRESS;
        break;
    }
    case 6: {
        char name[sizeof(docs[0].name) + 1];
        LHSERVERDOC handle;
        uint32_t out = NE->arg32(c, 4), far_name = NE->arg32(c, 12);
        if (!put32(c, out, 0) || !far_name || !NE->string(c, far_name, name, sizeof(name))) {
            *v = OLE_ERROR_ADDRESS;
            break;
        }
        *v = register_doc((LHSERVER)NE->arg32(c, 16), name, NULL, NE->arg32(c, 8), &handle);
        if (*v == OLE_OK && !put32(c, out, (uint32_t)handle)) {
            memset(&docs[doc_of(handle)], 0, sizeof(docs[0]));
            *v = OLE_ERROR_ADDRESS;
        }
        break;
    }
    case 7:
        *v = OleRevokeServerDoc((LHSERVERDOC)NE->arg32(c, 4));
        break;
    case 8: {
        char name[sizeof(docs[0].name) + 1];
        uint32_t far = NE->arg32(c, 4);
        *v = far && NE->string(c, far, name, sizeof(name)) ?
            OleRenameServerDoc((LHSERVERDOC)NE->arg32(c, 8), name) : OLE_ERROR_ADDRESS;
        break;
    }
    case 9: case 10:                    /* OleRevertServerDoc, OleSavedServerDoc */
        *v = doc_of((LHSERVERDOC)NE->arg32(c, 4)) < 0 ? OLE_ERROR_HANDLE : OLE_OK;
        break;
    case 11:                            /* OleRevokeObject */
        break;
    case 12:
        *v = OleQueryServerVersion();
        return WINDLL_DWORD;
    default:
        return WINDLL_NONE;
    }
    return WINDLL_WORD;
}

static void task_end(uint16_t task)
{
    for (int i = 0; i < DOCS; i++)
        if (docs[i].owner.task == task)
            memset(&docs[i], 0, sizeof(docs[i]));
    for (int i = 0; i < SERVERS; i++)
        if (servers[i].owner.task == task)
            memset(&servers[i], 0, sizeof(servers[i]));
}

static const win_dll_export exports[] = {
    { 2, 16, "OleRegisterServer", (const void *)OleRegisterServer },
    { 3, 4, "OleRevokeServer", (const void *)OleRevokeServer },
    { 4, 4, "OleBlockServer", NULL },
    { 5, 8, "OleUnblockServer", NULL },
    { 6, 16, "OleRegisterServerDoc", (const void *)OleRegisterServerDoc },
    { 7, 4, "OleRevokeServerDoc", (const void *)OleRevokeServerDoc },
    { 8, 8, "OleRenameServerDoc", (const void *)OleRenameServerDoc },
    { 9, 4, "OleRevertServerDoc", NULL },
    { 10, 4, "OleSavedServerDoc", NULL },
    { 11, 4, "OleRevokeObject", NULL },
    { 12, 0, "OleQueryServerVersion", (const void *)OleQueryServerVersion },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "OLESVR",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
