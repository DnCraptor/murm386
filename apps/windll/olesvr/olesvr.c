/*
 * olesvr.c - OLE 1.0 server and document registration for WINN.
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
#define SERVERS 8
#define DOCS 32
#define REQUEST_NOTICE 0x7ff0

typedef struct {
    HTASK task;
    uint32_t guest;
} server_owner;

static struct {
    server_owner owner;
    LHSERVER handle;
    LPOLESERVER native;
    BOOL revoking, exiting, revoke_requested;
    HSZ service;
    char cls[64];
} servers[SERVERS];

static struct {
    server_owner owner;
    LHSERVERDOC handle;
    LHSERVER server;
    LPOLESERVERDOC native;
    BOOL closing, revoked, opening, reading;
    char name[128];
} docs[DOCS];

/* Keep an old handle invalid even when a callback reuses its slot. */
static uint32_t next_handle = 0x53000000u;

static struct {
    HTASK task;
    DWORD id;
} endpoints[16];

static struct {
    DDEINITIALIZE init;
    DDEUNINITIALIZE uninit;
    DDECREATESTRINGHANDLE string;
    DDEFREESTRINGHANDLE free_string;
    DDEQUERYSTRING query;
    DDENAMESERVICE service;
    DDEQUERYCONVINFO conv_info;
    DDESETUSERHANDLE user_handle;
    DDECREATEDATAHANDLE create_data;
    DDEGETDATA get_data;
} dde;

static HDDEDATA CALLBACK system_callback(UINT type, UINT fmt, HCONV conv, HSZ topic,
                                         HSZ item, HDDEDATA data, DWORD d1, DWORD d2);

static DWORD endpoint_of(HTASK task)
{
    for (unsigned i = 0; i < sizeof(endpoints) / sizeof(*endpoints); i++)
        if (endpoints[i].task == task)
            return endpoints[i].id;
    return 0;
}

static DWORD open_endpoint(HTASK task)
{
    DWORD id = endpoint_of(task);
    if (id)
        return id;
    unsigned i = 0;
    while (i < sizeof(endpoints) / sizeof(*endpoints) && endpoints[i].task)
        i++;
    if (i == sizeof(endpoints) / sizeof(*endpoints))
        return 0;
    HINSTANCE module = LoadLibrary("DDEML");
    if (module <= 2)
        return 0;
#define DDE_IMPORT(field, type, name) \
    dde.field = (type)(void (*)(void))GetProcAddress(module, name); \
    if (!dde.field) goto fail
    DDE_IMPORT(init, DDEINITIALIZE, "DdeInitialize");
    DDE_IMPORT(uninit, DDEUNINITIALIZE, "DdeUninitialize");
    DDE_IMPORT(string, DDECREATESTRINGHANDLE, "DdeCreateStringHandle");
    DDE_IMPORT(free_string, DDEFREESTRINGHANDLE, "DdeFreeStringHandle");
    DDE_IMPORT(query, DDEQUERYSTRING, "DdeQueryString");
    DDE_IMPORT(service, DDENAMESERVICE, "DdeNameService");
    DDE_IMPORT(conv_info, DDEQUERYCONVINFO, "DdeQueryConvInfo");
    DDE_IMPORT(user_handle, DDESETUSERHANDLE, "DdeSetUserHandle");
    DDE_IMPORT(create_data, DDECREATEDATAHANDLE, "DdeCreateDataHandle");
    DDE_IMPORT(get_data, DDEGETDATA, "DdeGetData");
#undef DDE_IMPORT
    if (dde.init(&id, system_callback, CBF_SKIP_REGISTRATIONS | CBF_SKIP_UNREGISTRATIONS | CBF_SKIP_DISCONNECTS, 0))
        goto fail;
    /* Keep both DLLs resident until task exit, including during a DDE
     * request that invokes a server callback which revokes its server. */
    HINSTANCE self = LoadLibrary("OLESVR");
    if (self <= 2) {
        dde.uninit(id);
        goto fail;
    }
    endpoints[i].task = task;
    endpoints[i].id = id;
    return id;
fail:
    FreeLibrary(module);
    return 0;
}

static int service_server(HTASK task, HSZ service)
{
    if (service)
        for (int i = 0; i < SERVERS; i++)
            if (servers[i].owner.task == task && servers[i].service == service &&
                !servers[i].revoking)
                return i;
    return -1;
}

static void remove_server(int i)
{
    HTASK task = servers[i].owner.task;
    HSZ service = servers[i].service;
    memset(&servers[i], 0, sizeof(servers[i]));
    if (!service)
        return;
    for (int k = 0; k < SERVERS; k++)
        if (servers[k].owner.task == task && servers[k].service == service)
            return;
    dde.service(endpoint_of(task), service, 0, DNS_UNREGISTER);
}

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

static uint32_t guest_proc(server_owner owner, unsigned slot)
{
    unsigned offset = 4 * slot;
    uint8_t p[4];
    if (!NE || !NE->call || !NE->task_read || !(owner.guest >> 16) ||
        !NE->task_read((uint16_t)owner.task, owner.guest, p, sizeof(p)))
        return 0;
    uint32_t vtbl = read32(p);
    if (!(vtbl >> 16) || (vtbl & 0xffffu) > 0xfffcu - offset ||
        !NE->task_read((uint16_t)owner.task, vtbl + offset, p, sizeof(p)))
        return 0;
    return read32(p);
}

static OLESTATUS guest_method(server_owner owner, unsigned slot, OLESTATUS missing)
{
    uint32_t proc = guest_proc(owner, slot);
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
    int i = 0;
    while (i < SERVERS && servers[i].owner.task)
        i++;
    if (i == SERVERS)
        return OLE_ERROR_MEMORY;

    HTASK task = GetCurrentTask();
    DWORD id = open_endpoint(task);
    if (!id)
        return OLE_ERROR_COMM;
    HSZ service = dde.string(id, cls, CP_WINANSI);
    if (!service)
        return OLE_ERROR_MEMORY;

    BOOL already = FALSE;
    for (int k = 0; k < SERVERS; k++)
        if (servers[k].owner.task == task && servers[k].service == service)
            already = TRUE;
    LHSERVER handle = (LHSERVER)++next_handle;
    servers[i].owner = (server_owner){ task, guest };
    servers[i].handle = handle;
    servers[i].native = native;
    servers[i].service = service;
    lstrcpyn(servers[i].cls, cls, sizeof(servers[i].cls));
    HDDEDATA registered = already || dde.service(id, service, 0, DNS_REGISTER);
    dde.free_string(id, service);
    if (!registered || servers[i].handle != handle) {
        if (servers[i].handle == handle)
            memset(&servers[i], 0, sizeof(servers[i]));
        return OLE_ERROR_COMM;
    }
    *out = handle;
    return OLE_OK;
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
    if (docs[i].closing || docs[i].opening || docs[i].reading) {
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

    if (servers[i].exiting) {
        servers[i].revoke_requested = TRUE;
        return OLE_WAIT_FOR_RELEASE;
    }
    for (int d = 0; d < DOCS; d++)
        if (docs[d].server == handle && (docs[d].opening || docs[d].reading))
            return OLE_BUSY;
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
    remove_server(i);
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

enum { SERVER_OPEN, SERVER_CREATE, SERVER_TEMPLATE, SERVER_EDIT, SERVER_EXIT };

static int service_doc(HTASK task, HSZ service, LPCSTR name)
{
    for (int i = 0; i < DOCS; i++)
        if (docs[i].owner.task == task && !docs[i].opening && !docs[i].closing && !docs[i].revoked &&
            !lstrcmpi(docs[i].name, name)) {
            int server = server_of(docs[i].server);
            if (server >= 0 && servers[server].service == service)
                return i;
        }
    return -1;
}

static OLESTATUS create_guest_doc(server_owner owner, unsigned method, LHSERVERDOC handle,
                                  LPCSTR cls, LPCSTR name, LPCSTR template, uint32_t *out)
{
    uint32_t proc = guest_proc(owner, method);
    if (!(proc >> 16))
        return OLE_ERROR_COMMAND;
    uint16_t task = (uint16_t)owner.task;
    if (!NE->task_alloc || !NE->task_free || !NE->task_write)
        return OLE_ERROR_COMM;

    uint8_t params[4 + sizeof(servers[0].cls) + 2 * sizeof(docs[0].name)] = {0};
    unsigned cls_at = 4, name_at = cls_at + strlen(cls) + 1;
    unsigned tpl_at = name_at + strlen(name) + 1;
    strcpy((char *)params + cls_at, cls);
    strcpy((char *)params + name_at, name);
    if (template)
        strcpy((char *)params + tpl_at, template);
    uint32_t buf = NE->task_alloc(task, sizeof(params));
    if (!buf)
        return OLE_ERROR_MEMORY;

    uint16_t words[12] = { (WORD)(owner.guest >> 16), (WORD)owner.guest,
                           (WORD)((DWORD)handle >> 16), (WORD)handle };
    unsigned n = 4;
    if (method != SERVER_OPEN) {
        words[n++] = (WORD)(buf >> 16);
        words[n++] = (WORD)(buf + cls_at);
    }
    words[n++] = (WORD)(buf >> 16);
    words[n++] = (WORD)(buf + name_at);
    if (method == SERVER_TEMPLATE) {
        words[n++] = (WORD)(buf >> 16);
        words[n++] = (WORD)(buf + tpl_at);
    }
    words[n++] = (WORD)(buf >> 16);
    words[n++] = (WORD)buf;
    OLESTATUS status = OLE_ERROR_ADDRESS;
    if (NE->task_write(task, buf, params, sizeof(params))) {
        status = (WORD)NE->call(task, proc, words, (int)n);
        if (status == OLE_OK) {
            if (NE->task_read(task, buf, params, 4))
                *out = read32(params);
            else
                status = OLE_ERROR_ADDRESS;
        }
    }
    NE->task_free(task, buf);
    return status;
}

static OLESTATUS create_native_doc(LPOLESERVER server, unsigned method, LHSERVERDOC handle,
                                   LPCSTR cls, LPCSTR name, LPCSTR template, LPOLESERVERDOC *out)
{
    const OLESERVERVTBL *vt = server->lpvtbl;
    if (vt)
        switch (method) {
        case SERVER_OPEN:
            if (vt->Open)
                return vt->Open(server, handle, name, out);
            break;
        case SERVER_CREATE:
            if (vt->Create)
                return vt->Create(server, handle, cls, name, out);
            break;
        case SERVER_TEMPLATE:
            if (vt->CreateFromTemplate)
                return vt->CreateFromTemplate(server, handle, cls, name, template, out);
            break;
        case SERVER_EDIT:
            if (vt->Edit)
                return vt->Edit(server, handle, cls, name, out);
            break;
        }
    return OLE_ERROR_COMMAND;
}

static OLESTATUS system_document(int s, unsigned method, LPCSTR name, LPCSTR template)
{
    HTASK task = servers[s].owner.task;
    if (service_doc(task, servers[s].service, name) >= 0)
        return method == SERVER_OPEN ? OLE_OK : OLE_ERROR_NAME;
    for (int d = 0; d < DOCS; d++)
        if (docs[d].owner.task == task && docs[d].opening &&
            docs[d].server == servers[s].handle && !lstrcmpi(docs[d].name, name))
            return OLE_BUSY;
    int i = 0;
    while (i < DOCS && docs[i].owner.task)
        i++;
    if (i == DOCS)
        return OLE_ERROR_MEMORY;

    LHSERVERDOC handle = (LHSERVERDOC)++next_handle;
    docs[i].owner.task = task;
    docs[i].handle = handle;
    docs[i].server = servers[s].handle;
    docs[i].opening = TRUE;
    lstrcpyn(docs[i].name, name, sizeof(docs[i].name));

    LPOLESERVERDOC native = NULL;
    uint32_t guest = 0;
    LPOLESERVER server = servers[s].native;
    OLESTATUS status = server ?
        create_native_doc(server, method, handle, servers[s].cls, name, template, &native) :
        create_guest_doc(servers[s].owner, method, handle, servers[s].cls, name, template, &guest);
    if (docs[i].handle != handle)
        return OLE_ERROR_HANDLE;
    if (status != OLE_OK || (!native && !(guest >> 16))) {
        memset(&docs[i], 0, sizeof(docs[i]));
        return status == OLE_OK ? OLE_ERROR_OBJECT : status;
    }
    docs[i].opening = FALSE;
    docs[i].native = native;
    docs[i].owner.guest = guest;
    if (docs[i].revoked) {
        revoke_doc(i);
        return OLE_ERROR_OBJECT;
    }
    return OLE_OK;
}

static OLESTATUS system_exit(int s)
{
    if (servers[s].exiting)
        return OLE_BUSY;
    LHSERVER handle = servers[s].handle;
    servers[s].exiting = TRUE;
    LPOLESERVER server = servers[s].native;
    OLESTATUS status;
    if (server)
        status = server->lpvtbl && server->lpvtbl->Exit ? server->lpvtbl->Exit(server) :
                                                          OLE_ERROR_COMMAND;
    else
        status = guest_method(servers[s].owner, SERVER_EXIT, OLE_ERROR_COMMAND);
    if (servers[s].handle == handle) {
        servers[s].exiting = FALSE;
        if (servers[s].revoke_requested) {
            servers[s].revoke_requested = FALSE;
            OLESTATUS result = OleRevokeServer(handle);
            if (status == OLE_OK)
                status = result;
        }
    }
    return status;
}

static void spaces(char **p)
{
    while (**p == ' ' || **p == '\t')
        (*p)++;
}

/* OLE's system execute vocabulary uses [command("arg",...)] groups.
 * Parse in place in a private copy of the DDE data. */
static OLESTATUS system_command(int s, char **text)
{
    static const struct { LPCSTR name; unsigned argc; } verbs[] = {
        { "StdOpenDocument", 1 }, { "StdNewDocument", 2 },
        { "StdNewFromTemplate", 3 }, { "StdEditDocument", 1 }, { "StdExit", 0 },
    };
    char *p = *text;
    if (*p++ != '[')
        return OLE_ERROR_SYNTAX;
    spaces(&p);
    char *verb = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '(' && *p != ']')
        p++;
    char delimiter = *p;
    if (!delimiter)
        return OLE_ERROR_SYNTAX;
    *p = 0;
    unsigned method = 0;
    while (method < sizeof(verbs) / sizeof(*verbs) && lstrcmpi(verb, verbs[method].name))
        method++;
    *p = delimiter;
    if (method == sizeof(verbs) / sizeof(*verbs))
        return OLE_ERROR_COMMAND;
    unsigned argc = verbs[method].argc;
    LPCSTR arg[3] = {0};
    spaces(&p);
    if (*p == '(') {
        p++;
        for (unsigned i = 0; i < argc; i++) {
            spaces(&p);
            if (i && *p++ != ',')
                return OLE_ERROR_SYNTAX;
            spaces(&p);
            if (*p++ != '"')
                return OLE_ERROR_SYNTAX;
            char *end = strchr(p, '"');
            if (!end || end == p)
                return OLE_ERROR_SYNTAX;
            if ((size_t)(end - p) >= sizeof(docs[0].name))
                return OLE_ERROR_NAME;
            *end = 0;
            arg[i] = p;
            p = end + 1;
        }
        spaces(&p);
        if (*p++ != ')')
            return OLE_ERROR_SYNTAX;
    } else if (argc) {
        return OLE_ERROR_SYNTAX;
    }
    spaces(&p);
    if (*p++ != ']')
        return OLE_ERROR_SYNTAX;
    spaces(&p);
    *text = p;

    if (method == SERVER_EXIT)
        return system_exit(s);
    BOOL has_class = method == SERVER_CREATE || method == SERVER_TEMPLATE;
    if (has_class && lstrcmpi(servers[s].cls, arg[0]))
        return OLE_ERROR_CLASS;
    return system_document(s, method, arg[has_class ? 1 : 0], arg[2]);
}

static HDDEDATA system_execute(HTASK task, HSZ service, HDDEDATA data)
{
    DWORD bytes = dde.get_data(data, NULL, 0, 0);
    char *text = bytes ? malloc(bytes) : NULL;
    if (!text)
        return DDE_FNOTPROCESSED;
    OLESTATUS status = OLE_ERROR_SYNTAX;
    if (dde.get_data(data, (LPBYTE)text, bytes, 0) == bytes && memchr(text, 0, bytes)) {
        char *p = text;
        spaces(&p);
        int first = service_server(task, service);
        if (*p && first >= 0) {
            LHSERVER handle = servers[first].handle;
            do {
                int s = server_of(handle);
                status = s >= 0 ? system_command(s, &p) : OLE_ERROR_HANDLE;
            } while (status == OLE_OK && *p);
        }
    }
    free(text);
    if (status == OLE_OK)
        return DDE_FACK;
    return status == OLE_BUSY ? DDE_FBUSY : DDE_FNOTPROCESSED;
}

static HDDEDATA system_item(DWORD id, HTASK task, HSZ service, HSZ item)
{
    char key[32];
    if (!dde.query(id, item, key, sizeof(key), CP_WINANSI))
        return 0;
    char topics[sizeof(docs) / sizeof(*docs) * sizeof(docs[0].name) + sizeof("OLESystem\tSystem")];
    LPCSTR text;
    if (!lstrcmpi(key, "Topics")) {
        strcpy(topics, "OLESystem\tSystem");
        size_t used = strlen(topics);
        for (int i = 0; i < DOCS; i++)
            if (docs[i].owner.task == task && docs[i].name[0] &&
                service_doc(task, service, docs[i].name) == i) {
                topics[used++] = '\t';
                size_t len = strlen(docs[i].name);
                memcpy(topics + used, docs[i].name, len + 1);
                used += len;
            }
        text = topics;
    } else if (!lstrcmpi(key, "Protocols")) {
        text = "StdFileEditing\tEmbedding";
    } else if (!lstrcmpi(key, "SysItems")) {
        text = "Topics\tProtocols\tStatus\tFormats\tSysItems";
    } else if (!lstrcmpi(key, "Formats")) {
        text = "TEXT";
    } else if (!lstrcmpi(key, "Status")) {
        text = "Ready";
    } else {
        return 0;
    }
    return dde.create_data(id, (LPBYTE)text, (DWORD)strlen(text) + 1, 0, item, CF_TEXT, 0);
}

/* Synchronous document requests hold one client reference until the
 * result has been copied into DDEML. */
typedef struct request_client {
    OLECLIENT native;
    uint32_t guest;
    HTASK task;
    BOOL revoked;
    struct request_client *next;
} request_client;

static request_client *active_client;

static request_client *find_client(LPOLECLIENT native, uint32_t guest)
{
    HTASK task = GetCurrentTask();
    for (request_client *p = active_client; p; p = p->next)
        if (p->task == task && (native ? native == &p->native : guest && guest == p->guest))
            return p;
    return NULL;
}

static int CALLBACK request_notice(LPOLECLIENT client, UINT notice, LPOLEOBJECT object)
{
    (void)notice;
    (void)object;
    request_client *p = find_client(client, 0);
    return p && !p->revoked;
}

static const OLECLIENTVTBL request_client_vt = { request_notice };

static OLESTATUS revoke_object(LPOLECLIENT client, uint32_t far)
{
    request_client *p = find_client(client, far);
    if (!p)
        return OLE_ERROR_OBJECT;
    p->revoked = TRUE;
    return OLE_WAIT_FOR_RELEASE;
}

static OLESTATUS WINAPI OleRevokeObject(LPOLECLIENT client)
{
    return revoke_object(client, 0);
}

static BOOL memory_format(UINT format)
{
    switch (format) {
    case CF_TEXT: case CF_SYLK: case CF_DIF: case CF_TIFF: case CF_OEMTEXT:
    case CF_DIB: case CF_PENDATA: case CF_RIFF: case CF_WAVE: case CF_DSPTEXT:
        return TRUE;
    default:
        return format >= 0xc000u && format <= 0xffffu;
    }
}

static HDDEDATA document_native(int doc, DWORD id, HSZ item, LPCSTR name, WORD format,
                                 request_client *client)
{
    LPOLESERVERDOC server_doc = docs[doc].native;
    const OLESERVERDOCVTBL *vt = server_doc->lpvtbl;
    LPOLEOBJECT object = NULL;
    if (!vt || !vt->GetObject ||
        vt->GetObject(server_doc, name, &object, &client->native) != OLE_OK || !object)
        return 0;

    const OLESERVEROBJECTVTBL *objvt = ((const OLESERVEROBJECT *)(const void *)object)->lpvtbl;
    HANDLE handle = 0;
    HDDEDATA result = 0;
    if (!client->revoked && !docs[doc].revoked && objvt && objvt->GetData &&
        objvt->GetData(object, format, &handle) == OLE_OK && handle) {
        if (!client->revoked && !docs[doc].revoked) {
            LPBYTE bytes = GlobalLock(handle);
            if (bytes) {
                result = dde.create_data(id, bytes, GlobalSize(handle), 0, item, format, 0);
                GlobalUnlock(handle);
            }
        }
        GlobalFree(handle);
    }
    if (objvt && objvt->Release)
        objvt->Release(object);
    return result;
}

/* Win16 Globals exceeding 64 KiB use a tile selector for each window. */
static HDDEDATA guest_request_data(DWORD id, HSZ item, WORD format, uint16_t task, WORD handle)
{
    DWORD bytes = NE->task_global_size(task, handle);
    if (!bytes)
        return 0;
    LPBYTE copy = malloc(bytes);
    HDDEDATA result = 0;
    if (!copy)
        return 0;
    for (DWORD offset = 0; offset < bytes;) {
        DWORD chunk = bytes - offset;
        if (chunk > 0x10000u)
            chunk = 0x10000u;
        uint32_t sel = (handle | 3u) + (offset >> 16) * 8u;
        if (sel > UINT16_MAX || !NE->task_read(task, sel << 16, copy + offset, chunk))
            goto done;
        offset += chunk;
    }
    result = dde.create_data(id, copy, bytes, 0, item, format, 0);
done:
    free(copy);
    return result;
}

static HDDEDATA document_guest(int doc, DWORD id, HSZ item, LPCSTR name, WORD format,
                                request_client *client)
{
    server_owner owner = docs[doc].owner;
    uint32_t get_object = guest_proc(owner, 4);
    if (!(get_object >> 16) || NE->size < sizeof(*NE) || !NE->task_global_size ||
        !NE->task_alloc || !NE->task_free || !NE->task_write || !NE->task_proc)
        return 0;

    uint16_t task = (uint16_t)owner.task;
    uint32_t notice = NE->task_proc(task, "OLESVR", REQUEST_NOTICE);
    if (!notice)
        return 0;

    enum { CLIENT = 0, VTABLE = 4, OUT_OBJECT = 8, OUT_DATA = 12, NAME = 14 };
    uint8_t params[NAME + 256] = {0};
    uint32_t buf = NE->task_alloc(task, sizeof(params));
    if (!buf)
        return 0;
    client->guest = buf + CLIENT;
    uint32_t writes[] = { buf + VTABLE, notice };
    for (unsigned j = 0; j < 2; j++)
        for (unsigned k = 0; k < 4; k++)
            params[4 * j + k] = (uint8_t)(writes[j] >> (8 * k));
    if (name)
        strcpy((char *)params + NAME, name);
    WORD sel = (WORD)(buf >> 16);
    WORD args[] = { (WORD)(owner.guest >> 16), (WORD)owner.guest,
                    name ? sel : 0, name ? (WORD)(buf + NAME) : 0,
                    sel, (WORD)(buf + OUT_OBJECT), sel, (WORD)(buf + CLIENT) };
    HDDEDATA result = 0;
    if (!NE->task_write(task, buf, params, sizeof(params)) ||
        (WORD)NE->call(task, get_object, args, 8) != OLE_OK ||
        !NE->task_read(task, buf + OUT_OBJECT, params, 4))
        goto done;

    server_owner object = { owner.task, read32(params) };
    if (!(object.guest >> 16))
        goto done;
    uint32_t get_data = guest_proc(object, 4);
    if (!client->revoked && !docs[doc].revoked && (get_data >> 16)) {
        WORD words[] = { (WORD)(object.guest >> 16), (WORD)object.guest, format,
                         sel, (WORD)(buf + OUT_DATA) };
        if ((WORD)NE->call(task, get_data, words, 5) == OLE_OK &&
            NE->task_read(task, buf + OUT_DATA, params, 2)) {
            WORD handle = (WORD)(params[0] | params[1] << 8);
            if (handle && NE->task_global_size(task, handle)) {
                if (!client->revoked && !docs[doc].revoked)
                    result = guest_request_data(id, item, format, task, handle);
                NE->task_free(task, (uint32_t)handle << 16);
            }
        }
    }
    guest_method(object, 1, OLE_OK);
done:
    client->guest = 0;
    NE->task_free(task, buf);
    return result;
}

static HDDEDATA document_request(int i, DWORD id, HSZ item, UINT format)
{
    if (i < 0 || docs[i].opening || docs[i].closing || docs[i].reading || docs[i].revoked ||
        !memory_format(format))
        return 0;

    char name[256] = {0};
    if (item && !dde.query(id, item, name, sizeof(name), CP_WINANSI))
        return 0;
    LPCSTR object_name = name[0] ? name : NULL;
    LHSERVERDOC handle = docs[i].handle;
    request_client client = { .native = { &request_client_vt }, .task = GetCurrentTask(),
                              .next = active_client };
    active_client = &client;
    docs[i].reading = TRUE;
    HDDEDATA result = docs[i].native ?
        document_native(i, id, item, object_name, (WORD)format, &client) :
        document_guest(i, id, item, object_name, (WORD)format, &client);
    active_client = client.next;
    if (docs[i].handle == handle) {
        docs[i].reading = FALSE;
        if (docs[i].revoked)
            revoke_doc(i);
    }
    return result;
}

static HDDEDATA CALLBACK system_callback(UINT type, UINT fmt, HCONV conv, HSZ topic,
                                         HSZ item, HDDEDATA data, DWORD d1, DWORD d2)
{
    (void)d1; (void)d2;
    if (type != XTYP_CONNECT && type != XTYP_CONNECT_CONFIRM &&
        type != XTYP_REQUEST && type != XTYP_EXECUTE)
        return 0;
    HTASK task = GetCurrentTask();
    DWORD id = endpoint_of(task);
    char name[256];
    if (!id || !dde.query(id, topic, name, sizeof(name), CP_WINANSI))
        return 0;
    BOOL system = !lstrcmpi(name, "System") || !lstrcmpi(name, "OLESystem");
    CONVINFO info = { .cb = sizeof(info) };
    HSZ service = item;
    if (type == XTYP_REQUEST || type == XTYP_EXECUTE) {
        if (!dde.conv_info(conv, QID_SYNC, &info))
            return 0;
        service = info.hszServiceReq;
    }
    if (service_server(task, service) < 0)
        return 0;
    if (type == XTYP_CONNECT)
        return system || service_doc(task, service, name) >= 0;
    if (type == XTYP_CONNECT_CONFIRM) {
        int i = system ? -1 : service_doc(task, service, name);
        if (i >= 0)
            dde.user_handle(conv, QID_SYNC, (DWORD)docs[i].handle);
        return 0;
    }
    if (!system)
        return type == XTYP_REQUEST ? document_request(doc_of((LHSERVERDOC)info.hUser), id, item, fmt) :
                                      0;
    if (type == XTYP_EXECUTE)
        return system_execute(task, service, data);
    return fmt == CF_TEXT ? system_item(id, task, service, item) : 0;
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
            remove_server(server_of(handle));
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
    case 11:
        *v = revoke_object(NULL, NE->arg32(c, 4));
        break;
    case REQUEST_NOTICE: {
        request_client *p = find_client(NULL, NE->arg32(c, 10));
        *v = p && !p->revoked;
        break;
    }
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
    /* DDEML ends its instances as part of the same task teardown. */
    for (unsigned i = 0; i < sizeof(endpoints) / sizeof(*endpoints); i++)
        if (endpoints[i].task == task)
            memset(&endpoints[i], 0, sizeof(endpoints[i]));
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
    { 11, 4, "OleRevokeObject", (const void *)OleRevokeObject },
    { 12, 0, "OleQueryServerVersion", (const void *)OleQueryServerVersion },
    { REQUEST_NOTICE, 10, "WinnOleRequestNotice", NULL },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "OLESVR",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, NULL, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
