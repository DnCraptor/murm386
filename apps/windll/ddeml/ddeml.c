/*
 * ddeml.c - DDEML.DLL, WINN's: the DDE Management Library between the
 * programs WINN runs, native and Win16 alike. A native DLL
 * (ddi/windll.h): its functions for native callers, and Win16 programs'
 * calls of them.
 *
 * As Windows 3.1's: instances (DdeInitialize: one's callback, its
 * filters), services they register (DdeNameService), conversations
 * (DdeConnect: the server's callback asked with XTYP_CONNECT), the
 * client's transactions (request, poke, execute, advise links; an
 * asynchronous one completes as the client takes its messages), advise
 * loops (DdePostAdvise), data handles, string handles (global atoms, as
 * Windows has them). Each instance has a hidden window of its task: its
 * callback is called through it, so in its task as Windows does
 * (SendMessage).
 *
 * Win16 programs' and Win32 programs' (USER32's Dde* functions, core/pe_dlls.c)
 * calls: their callbacks called in their tasks, DdeAccessData a copy of
 * the handle's bytes in the program's memory (DdeUnaccessData takes it
 * back).
 *
 * WINN has no DDE between machines (NetDDE) and no conversations with
 * programs speaking DDE's messages themselves.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include "windows.h"
#include "ddeml.h"
#include "windll.h"

#define INSTS     16
#define CONVS     32
#define LISTS     16
#define LINKS     8
#define DATAS     64
#define SERVICES  8
#define PENDING   8

#define CONV_TAG  0x43000000u           /* HCONV: tag, generation, index + 1 */
#define LIST_TAG  0x4c000000u           /* HCONVLIST: tag, generation, index + 1 */
#define DATA_TAG  0x44000000u           /* HDDEDATA */

#define NE (__win_dll_ne)

#define WM_DDEML_CALLBACK (WM_USER + 0x0D0)
#define WM_DDEML_READY (WM_USER + 0x0D1)

typedef struct {
    int used;
    DWORD generation;
    HTASK task;
    PFNCALLBACK native;             /* a native caller's callback, or */
    uint32_t proc;                  /* a Win16 program's (selector:offset) */
    DWORD cmd;                      /* APPCMD_, CBF_ */
    HWND hwnd;
    UINT error;
    int closing;                    /* DdeUninitialize in progress */
    int unfiltered;                 /* DNS_FILTEROFF */
    HSZ services[SERVICES];
} instance;

typedef struct {
    HSZ item;
    UINT fmt, type;                 /* XTYP_ADVSTART's flags */
} advise_link;

typedef struct {
    int used;
    HCONV handle;
    int inst;
    int partner;                    /* the other end, -1 when it went */
    int client, closing;
    UINT callback_state;           /* ST_BLOCKED or ST_BLOCKNEXT */
    int reconnect_from;             /* old server slot + 1 during XTYP_DISCONNECT */
    HSZ service, topic;
    HCONVLIST list;
    CONVCONTEXT context;
    DWORD user;
    advise_link links[LINKS];       /* a server's */
} conversation;

typedef struct {
    HDDEDATA handle;
    int inst;
    uint8_t *bytes;
    DWORD size;
    HSZ item;
    UINT fmt;
    int app_owned;
    int locked;
    uint16_t at_task;               /* DdeAccessData's copy in a program's memory */
    uint32_t at;
} data_handle;

/* A transaction waiting for the server or a notification waiting for the client.
 * A synchronous transaction retains its slot until the waiting caller resumes. */
typedef struct {
    DWORD order, id, status;
    HCONV target, client;
    UINT type, fmt, error;
    HSZ item;
    HDDEDATA data;
    int sync, running, posted, cancelled;
} pending;

typedef struct {
    HCONVLIST handle;
    int inst;
    int busy, closing;               /* operations that invoke callbacks */
    unsigned count;
    uint8_t members[CONVS];
} conversation_list;

static conversation_list lists[LISTS];
static DWORD list_generation, conv_generation, inst_generation;
static instance insts[INSTS];
static conversation convs[CONVS];
static data_handle datas[DATAS];
static pending pend[PENDING];
static DWORD next_id, next_order, data_generation;
static ATOM wnd_class;

static uint16_t rd16le(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }
static uint32_t rd32le(const uint8_t *b) { return rd16le(b) | (uint32_t)rd16le(b + 2) << 16; }
static void wr16le(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void wr32le(uint8_t *b, uint32_t v) { wr16le(b, (uint16_t)v); wr16le(b + 2, (uint16_t)(v >> 16)); }

/* ---------------------------------------------------------------- handles */

static instance *inst_of(DWORD id)
{
    return id >= 1 && id <= INSTS && insts[id - 1].used ? &insts[id - 1] : NULL;
}

static int conv_index(HCONV h)
{
    unsigned i = (h & 0xffu) - 1u;
    return i < CONVS && convs[i].used && convs[i].handle == h ? (int)i : -1;
}

static HCONV conv_handle(int i) { return i >= 0 ? convs[i].handle : 0; }

static data_handle *data_of(HDDEDATA h)
{
    unsigned i = (h & 0xffu) - 1u;
    return h && i < DATAS && datas[i].handle == h ? &datas[i] : NULL;
}

static HDDEDATA data_new(int inst, const uint8_t *src, DWORD size, HSZ item, UINT fmt, int owned)
{
    for (int i = 0; i < DATAS; i++)
        if (!datas[i].handle) {
            data_handle *d = &datas[i];
            d->bytes = (uint8_t *)malloc(size ? size : 1);
            if (!d->bytes)
                return 0;
            if (src)
                memcpy(d->bytes, src, size);
            else
                memset(d->bytes, 0, size ? size : 1);
            d->handle = DATA_TAG | (++data_generation & 0xffffu) << 8 | (DWORD)(i + 1);
            d->inst = inst;
            d->size = size;
            d->item = item;
            d->fmt = fmt;
            d->app_owned = owned;
            d->locked = 0;
            return d->handle;
        }
    return 0;
}

static void data_free(HDDEDATA h)
{
    data_handle *d = data_of(h);
    if (d) {
        if (d->at && NE && NE->size >= WIN_DLL_NE_BASE_SIZE && NE->task_free)
            NE->task_free(d->at_task, d->at);
        free(d->bytes);
        memset(d, 0, sizeof(*d));
    }
}

/* one DDEML gave a callback, which the callback did not keep */
static void data_release(HDDEDATA h)
{
    data_handle *d = data_of(h);
    if (d && !d->app_owned)
        data_free(h);
}

/* ---------------------------------------------------------------- callbacks */

typedef struct {
    int inst;
    UINT type, fmt;
    HCONV conv;
    HSZ hsz1, hsz2;
    HDDEDATA data;
    DWORD data1, data2;
    HDDEDATA result;
} callback_call;

static uint32_t guest_context(uint16_t task, const CONVCONTEXT *cc)
{
    if (NE->size < WIN_DLL_NE_BASE_SIZE || !NE->task_alloc || !NE->task_free || !NE->task_write)
        return 0;
    int flat = NE->task_flat && NE->task_flat(task);
    uint8_t b[36] = {0};
    unsigned n = flat ? sizeof(b) : 16;
    if (flat) {
        wr32le(b, n);
        wr32le(b + 4, cc->wFlags);
        wr32le(b + 8, cc->wCountryID);
        wr32le(b + 12, (DWORD)cc->iCodePage);
        wr32le(b + 16, cc->dwLangID);
        wr32le(b + 20, cc->dwSecurity);
    } else {
        wr16le(b, n);
        wr16le(b + 2, (WORD)cc->wFlags);
        wr16le(b + 4, (WORD)cc->wCountryID);
        wr16le(b + 6, (WORD)cc->iCodePage);
        wr32le(b + 8, cc->dwLangID);
        wr32le(b + 12, cc->dwSecurity);
    }
    uint32_t at = NE->task_alloc(task, n);
    if (at && !NE->task_write(task, at, b, n)) {
        NE->task_free(task, at);
        at = 0;
    }
    return at;
}

static HDDEDATA invoke(const callback_call *c)
{
    instance *in = &insts[c->inst];
    if (in->native)
        return in->native(c->type, c->fmt, c->conv, c->hsz1, c->hsz2, c->data, c->data1, c->data2);
    if (!in->proc || !NE || NE->size < WIN_DLL_NE_BASE_SIZE || !NE->call)
        return 0;
    uint16_t task = (uint16_t)in->task;
    DWORD data1 = c->data1;
    int has_context = data1 && (c->type == XTYP_CONNECT || c->type == XTYP_WILDCONNECT);
    if (has_context) {
        data1 = guest_context(task, (const CONVCONTEXT *)(uintptr_t)data1);
        if (!data1)
            return 0;
    }
    /* (UINT, UINT, HCONV, HSZ, HSZ, HDDEDATA, DWORD, DWORD) */
    const uint16_t w[14] = {
        (uint16_t)c->type, (uint16_t)c->fmt,
        (uint16_t)(c->conv >> 16), (uint16_t)c->conv,
        (uint16_t)(c->hsz1 >> 16), (uint16_t)c->hsz1,
        (uint16_t)(c->hsz2 >> 16), (uint16_t)c->hsz2,
        (uint16_t)(c->data >> 16), (uint16_t)c->data,
        (uint16_t)(data1 >> 16), (uint16_t)data1,
        (uint16_t)(c->data2 >> 16), (uint16_t)c->data2,
    };
    HDDEDATA result = NE->call(task, in->proc, w, 14);
    if (has_context)
        NE->task_free(task, data1);
    return result;
}

/* what the instance's filters keep from its callback */
static int filtered(const instance *in, UINT type)
{
    DWORD f = in->cmd;
    switch (type & ~(XTYPF_NODATA | XTYPF_ACKREQ)) {
    case XTYP_CONNECT: case XTYP_WILDCONNECT:
        return (f & CBF_FAIL_CONNECTIONS) != 0;
    case XTYP_ADVSTART: case XTYP_ADVSTOP:
        return (f & CBF_FAIL_ADVISES) != 0;
    case XTYP_EXECUTE:  return (f & CBF_FAIL_EXECUTES) != 0;
    case XTYP_POKE:     return (f & CBF_FAIL_POKES) != 0;
    case XTYP_REQUEST: case XTYP_ADVREQ:
        return (f & CBF_FAIL_REQUESTS) != 0;
    case XTYP_CONNECT_CONFIRM: return (f & CBF_SKIP_CONNECT_CONFIRMS) != 0;
    case XTYP_REGISTER:        return (f & CBF_SKIP_REGISTRATIONS) != 0;
    case XTYP_UNREGISTER:      return (f & CBF_SKIP_UNREGISTRATIONS) != 0;
    case XTYP_DISCONNECT:      return (f & CBF_SKIP_DISCONNECTS) != 0;
    }
    return 0;
}

/* the instance's callback, in its task (through its window) */
static HDDEDATA callback(int inst, UINT type, UINT fmt, HCONV conv, HSZ hsz1, HSZ hsz2,
                         HDDEDATA data, DWORD data1, DWORD data2)
{
    instance *in = &insts[inst];
    if (!in->used || filtered(in, type))
        return 0;
    callback_call c = { inst, type, fmt, conv, hsz1, hsz2, data, data1, data2, 0 };
    if (in->hwnd && IsWindow(in->hwnd))
        SendMessage(in->hwnd, WM_DDEML_CALLBACK, 0, (LPARAM)(uintptr_t)&c);
    else
        c.result = invoke(&c);
    return (type & XTYPF_NOBLOCK) && c.result == CBR_BLOCK ? 0 : c.result;
}

static void drain_callbacks(int inst, HCONV h);

static LRESULT CALLBACK ddeml_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DDEML_CALLBACK && lp) {
        callback_call *c = (callback_call *)(uintptr_t)lp;
        c->result = invoke(c);
        return 0;
    }
    if (msg == WM_DDEML_READY && wp < PENDING) {
        pending *p = &pend[wp];
        int c = conv_index(p->target);
        if (p->posted && p->order == (DWORD)lp && c >= 0 && insts[convs[c].inst].hwnd == h) {
            p->posted = 0;
            drain_callbacks(convs[c].inst, 0);
        }
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- instances */

static UINT init(LPDWORD id, PFNCALLBACK native, uint32_t proc, DWORD cmd)
{
    if (!id)
        return DMLERR_INVALIDPARAMETER;
    instance *in = inst_of(*id);
    if (*id) {                          /* its flags again */
        if (!in)
            return DMLERR_INVALIDPARAMETER;
        in->cmd = (in->cmd & ~0xfffff000ul) | (cmd & 0xfffff000ul);
        return DMLERR_NO_ERROR;
    }
    if (!native && !proc)
        return DMLERR_INVALIDPARAMETER;
    if (!wnd_class) {
        WNDCLASS wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = ddeml_wndproc;
        wc.lpszClassName = "WinnDdeml";
        wnd_class = RegisterClass(&wc);
        if (!wnd_class)
            return DMLERR_SYS_ERROR;
    }
    for (int i = 0; i < INSTS; i++)
        if (!insts[i].used) {
            in = &insts[i];
            memset(in, 0, sizeof(*in));
            in->hwnd = CreateWindow("WinnDdeml", "DDEML", WS_POPUP, 0, 0, 0, 0, 0, 0, 0, NULL);
            if (!in->hwnd)
                return DMLERR_SYS_ERROR;
            in->used = 1;
            in->generation = ++inst_generation;
            in->task = GetCurrentTask();
            in->native = native;
            in->proc = proc;
            in->cmd = cmd;
            *id = (DWORD)(i + 1);
            return DMLERR_NO_ERROR;
        }
    return DMLERR_LOW_MEMORY;
}

static UINT WINAPI DdeInitialize(LPDWORD id, PFNCALLBACK cb, DWORD cmd, DWORD res)
{
    (void)res;
    return init(id, cb, 0, cmd);
}

static UINT WINAPI DdeGetLastError(DWORD id)
{
    instance *in = inst_of(id);
    if (!in)
        return DMLERR_DLL_NOT_INITIALIZED;
    UINT e = in->error;
    in->error = DMLERR_NO_ERROR;
    return e;
}

static void set_error(int inst, UINT e)
{
    if (inst >= 0 && inst < INSTS && insts[inst].used)
        insts[inst].error = e;
}

/* ---------------------------------------------------------------- strings */

static HSZ WINAPI DdeCreateStringHandle(DWORD id, LPCSTR s, int cp)
{
    (void)cp;
    instance *in = inst_of(id);
    if (!in || !s || !s[0])
        return 0;
    ATOM a = GlobalAddAtom(s);
    if (!a)
        in->error = DMLERR_SYS_ERROR;
    return a;
}

static BOOL WINAPI DdeFreeStringHandle(DWORD id, HSZ hsz)
{
    return inst_of(id) && hsz && !GlobalDeleteAtom((ATOM)hsz);
}

static BOOL WINAPI DdeKeepStringHandle(DWORD id, HSZ hsz)
{
    char s[256];
    return inst_of(id) && hsz && hsz <= 0xffffu && GlobalGetAtomName((ATOM)hsz, s, sizeof(s)) &&
           GlobalAddAtom(s) == (ATOM)hsz;
}

/* into out (max bytes; NULL: none): its length */
static DWORD WINAPI DdeQueryString(DWORD id, HSZ hsz, LPSTR out, DWORD max, int cp)
{
    char s[256];
    (void)cp;
    if (!inst_of(id) || !hsz)
        return 0;
    UINT n = GlobalGetAtomName((ATOM)hsz, s, sizeof(s));
    if (!out)
        return n;
    if (!max)
        return 0;
    lstrcpyn(out, s, (int)(max < sizeof(s) ? max : sizeof(s)));
    return (DWORD)strlen(out);
}

static int WINAPI DdeCmpStringHandles(HSZ a, HSZ b)
{
    return a == b ? 0 : a < b ? -1 : 1;
}

/* ---------------------------------------------------------------- data */

static HDDEDATA WINAPI DdeCreateDataHandle(DWORD id, LPBYTE src, DWORD cb, DWORD off, HSZ item,
                                           UINT fmt, UINT cmd)
{
    instance *in = inst_of(id);
    if (!in)
        return 0;
    HDDEDATA h = data_new((int)id - 1, NULL, off + cb, item, fmt, (cmd & HDATA_APPOWNED) != 0);
    if (!h) {
        in->error = DMLERR_MEMORY_ERROR;
        return 0;
    }
    if (src && cb)
        memcpy(data_of(h)->bytes + off, src, cb);
    return h;
}

static HDDEDATA WINAPI DdeAddData(HDDEDATA h, LPBYTE src, DWORD cb, DWORD off)
{
    data_handle *d = data_of(h);
    if (!d)
        return 0;
    if (off + cb > d->size) {
        uint8_t *b = (uint8_t *)realloc(d->bytes, off + cb);
        if (!b) {
            set_error(d->inst, DMLERR_MEMORY_ERROR);
            return 0;
        }
        memset(b + d->size, 0, off + cb - d->size);
        d->bytes = b;
        d->size = off + cb;
    }
    if (src && cb)
        memcpy(d->bytes + off, src, cb);
    return h;
}

/* its bytes from off on, max of them (dst NULL: its size) */
static DWORD WINAPI DdeGetData(HDDEDATA h, LPBYTE dst, DWORD max, DWORD off)
{
    data_handle *d = data_of(h);
    if (!d)
        return 0;
    if (!dst)
        return d->size;
    if (off >= d->size)
        return 0;
    DWORD n = d->size - off < max ? d->size - off : max;
    memcpy(dst, d->bytes + off, n);
    return n;
}

static LPBYTE WINAPI DdeAccessData(HDDEDATA h, LPDWORD size)
{
    data_handle *d = data_of(h);
    if (!d)
        return NULL;
    if (size)
        *size = d->size;
    d->locked++;
    return d->bytes;
}

static BOOL WINAPI DdeUnaccessData(HDDEDATA h)
{
    data_handle *d = data_of(h);
    if (!d)
        return FALSE;
    if (d->locked)
        d->locked--;
    return TRUE;
}

static BOOL WINAPI DdeFreeDataHandle(HDDEDATA h)
{
    if (!data_of(h))
        return FALSE;
    data_free(h);
    return TRUE;
}

/* ---------------------------------------------------------------- services */

static int serves(const instance *in, HSZ service)
{
    if (in->cmd & APPCMD_CLIENTONLY)
        return 0;
    for (int k = 0; k < SERVICES; k++)
        if (in->services[k] && (!service || in->services[k] == service))
            return 1;
    return in->unfiltered;
}

static HSZ first_service(const instance *in)
{
    for (int k = 0; k < SERVICES; k++)
        if (in->services[k])
            return in->services[k];
    return 0;
}

/* the others hear of it (XTYP_REGISTER, XTYP_UNREGISTER) */
static void tell_others(int inst, UINT type, HSZ service)
{
    for (int i = 0; i < INSTS; i++)
        if (i != inst && insts[i].used)
            callback(i, type, 0, 0, service, service, 0, 0, 0);
}

static HDDEDATA WINAPI DdeNameService(DWORD id, HSZ hsz1, HSZ hsz2, UINT cmd)
{
    (void)hsz2;
    instance *in = inst_of(id);
    int inst = (int)id - 1;
    if (!in)
        return 0;
    if (cmd & DNS_FILTERON)
        in->unfiltered = 0;
    if (cmd & DNS_FILTEROFF)
        in->unfiltered = 1;
    if (cmd & DNS_REGISTER) {
        if (!hsz1) {
            in->error = DMLERR_INVALIDPARAMETER;
            return 0;
        }
        int k = 0;
        while (k < SERVICES && in->services[k] && in->services[k] != hsz1)
            k++;
        if (k == SERVICES) {
            in->error = DMLERR_LOW_MEMORY;
            return 0;
        }
        if (in->services[k] != hsz1) {
            if (!DdeKeepStringHandle(id, hsz1)) {
                in->error = DMLERR_INVALIDPARAMETER;
                return 0;
            }
            in->services[k] = hsz1;
            tell_others(inst, XTYP_REGISTER, hsz1);
        }
    } else if (cmd & DNS_UNREGISTER) {
        for (int k = 0; k < SERVICES; k++)
            if (in->services[k] && (!hsz1 || in->services[k] == hsz1)) {
                HSZ s = in->services[k];
                in->services[k] = 0;
                tell_others(inst, XTYP_UNREGISTER, s);
                GlobalDeleteAtom((ATOM)s);
            }
    }
    return 1;
}

/* ---------------------------------------------------------------- conversations */

static int conv_new(int inst, int client, HSZ service, HSZ topic)
{
    for (int i = 0; i < CONVS; i++)
        if (!convs[i].used) {
            memset(&convs[i], 0, sizeof(convs[i]));
            convs[i].used = 1;
            convs[i].handle = CONV_TAG | (++conv_generation & 0xffffu) << 8 | (DWORD)(i + 1);
            convs[i].inst = inst;
            convs[i].client = client;
            convs[i].service = service;
            convs[i].topic = topic;
            convs[i].partner = -1;
            return i;
        }
    return -1;
}

static conversation_list *list_of(HCONVLIST handle)
{
    unsigned i = (handle & 0xffu) - 1u;
    return i < LISTS && lists[i].handle == handle ? &lists[i] : NULL;
}

static conversation_list *list_new(int inst)
{
    for (unsigned i = 0; i < LISTS; i++)
        if (!lists[i].handle && !lists[i].busy) {
            lists[i].handle = LIST_TAG | (++list_generation & 0xffffu) << 8 | (i + 1u);
            lists[i].inst = inst;
            return &lists[i];
        }
    return NULL;
}

static int can_connect(int client, int server, HSZ service)
{
    const instance *in = &insts[client], *sv = &insts[server];
    return in->used && !in->closing && sv->used && !sv->closing && serves(sv, service) &&
           (client != server || !(in->cmd & CBF_FAIL_SELFCONNECTIONS));
}

static HCONV connect_pair(int inst, int server, HSZ service, HSZ topic, const CONVCONTEXT *cc,
                          conversation_list *list)
{
    if (list) {
        for (unsigned i = 0; i < list->count; i++) {
            const conversation *cv = &convs[list->members[i]];
            if (cv->service == service && cv->topic == topic && cv->partner >= 0 &&
                convs[cv->partner].inst == server)
                return 0;
        }
    }
    DWORD id = (DWORD)(inst + 1);
    if (service && !DdeKeepStringHandle(id, service))
        return 0;
    if (topic && !DdeKeepStringHandle(id, topic)) {
        if (service)
            GlobalDeleteAtom((ATOM)service);
        return 0;
    }
    int c = conv_new(inst, 1, service, topic);
    int v = c >= 0 ? conv_new(server, 0, service, topic) : -1;
    if (v < 0) {
        if (c >= 0)
            convs[c].used = 0;
        if (service)
            GlobalDeleteAtom((ATOM)service);
        if (topic)
            GlobalDeleteAtom((ATOM)topic);
        set_error(inst, DMLERR_LOW_MEMORY);
        return 0;
    }
    convs[c].partner = v;
    convs[v].partner = c;
    convs[c].context = convs[v].context = *cc;
    if (list) {
        convs[c].list = list->handle;
        list->members[list->count++] = (uint8_t)c;
    }
    HCONV h = conv_handle(c);
    callback(server, XTYP_CONNECT_CONFIRM, 0, conv_handle(v), topic, service, 0, 0, server == inst);
    return conv_index(h) >= 0 ? h : 0;
}

static CONVCONTEXT connect_context(const CONVCONTEXT *cc)
{
    CONVCONTEXT ctx = { .cb = sizeof(ctx), .iCodePage = CP_WINANSI };
    if (cc) {
        ctx = *cc;
        ctx.cb = sizeof(ctx);
    }
    return ctx;
}

static HCONV WINAPI DdeConnect(DWORD id, HSZ service, HSZ topic, PCONVCONTEXT cc)
{
    instance *in = inst_of(id);
    if (!in || in->closing)
        return 0;
    int inst = (int)id - 1;
    CONVCONTEXT ctx = connect_context(cc);
    DWORD client = in->generation;
    for (int s = 0; s < INSTS; s++) {
        if (!can_connect(inst, s, service))
            continue;
        HSZ name = service ? service : first_service(&insts[s]);
        DWORD server = insts[s].generation;
        if (callback(s, XTYP_CONNECT, 0, 0, topic, name, 0, (DWORD)(uintptr_t)&ctx, s == inst) &&
            in->generation == client && insts[s].generation == server && can_connect(inst, s, service))
            return connect_pair(inst, s, name, topic, &ctx, NULL);
        if (in->generation != client || in->closing)
            return 0;
    }
    in->error = DMLERR_NO_CONV_ESTABLISHED;
    return 0;
}

static void drop_pending(pending *p)
{
    data_release(p->data);
    if (p->item)
        GlobalDeleteAtom((ATOM)p->item);
    memset(p, 0, sizeof(*p));
}

static void cancel_pending(pending *p, UINT error)
{
    if (p->running) {
        p->cancelled = 1;
        p->error = error;
    } else if (p->sync) {
        p->target = 0;
        p->error = error;
    } else {
        drop_pending(p);
    }
}

static void drop_conversation_pending(HCONV h)
{
    for (int i = 0; i < PENDING; i++)
        if (pend[i].target && (pend[i].target == h || pend[i].client == h))
            cancel_pending(&pend[i], DMLERR_SERVER_DIED);
}

static void drop_links(conversation *cv)
{
    for (int i = 0; i < LINKS; i++)
        if (cv->links[i].item)
            GlobalDeleteAtom((ATOM)cv->links[i].item);
}

static void conv_end(int c, int tell)
{
    conversation *cv = &convs[c];
    if (cv->closing)
        return;
    int p = cv->partner;
    int linked = p >= 0 && convs[p].used && convs[p].partner == c;
    int client = linked && convs[p].client ? p : c;
    conversation_list *list = list_of(convs[client].list);
    if (list)
        for (unsigned i = 0; i < list->count; i++)
            if (list->members[i] == client) {
                --list->count;
                memmove(list->members + i, list->members + i + 1, list->count - i);
                break;
            }
    cv->closing = 1;
    cv->partner = -1;
    drop_conversation_pending(conv_handle(c));
    if (linked) {
        convs[p].partner = -1;
        convs[p].closing = 1;
        drop_conversation_pending(conv_handle(p));
        if (tell) {
            if (convs[p].client && !cv->client)
                convs[p].reconnect_from = c + 1;
            callback(convs[p].inst, XTYP_DISCONNECT, 0, conv_handle(p), 0, 0, 0, 0,
                     convs[p].inst == cv->inst);
        }
        drop_links(&convs[p]);
        memset(&convs[p], 0, sizeof(convs[p]));
    }
    drop_links(cv);
    if (cv->service)
        GlobalDeleteAtom((ATOM)cv->service);
    if (cv->topic)
        GlobalDeleteAtom((ATOM)cv->topic);
    memset(cv, 0, sizeof(*cv));
}

static BOOL WINAPI DdeDisconnect(HCONV h)
{
    int c = conv_index(h);
    if (c < 0 || convs[c].closing)
        return FALSE;
    conv_end(c, 1);
    return TRUE;
}

static HDDEDATA client_transaction(int c, HDDEDATA given, HSZ item, UINT fmt, UINT type,
                                   DWORD timeout, LPDWORD result);

static HCONV WINAPI DdeReconnect(HCONV h)
{
    int c = conv_index(h);
    if (c < 0)
        return 0;
    conversation *cv = &convs[c];
    int inst = cv->inst;
    instance *in = &insts[inst];
    if (in->task != GetCurrentTask())
        return 0;
    int from = cv->reconnect_from - 1;
    if (from < 0) {
        set_error(inst, DMLERR_INVALIDPARAMETER);
        return 0;
    }

    /* Both old slots stay reserved until XTYP_DISCONNECT returns. */
    const conversation *old_server = &convs[from];
    int server = old_server->inst;
    DWORD client_gen = in->generation, server_gen = insts[server].generation;
    HSZ service = cv->service, topic = cv->topic;
    CONVCONTEXT ctx = cv->context;
    cv->reconnect_from = 0;
    if (!can_connect(inst, server, service) ||
        !callback(server, XTYP_CONNECT, 0, 0, topic, service, 0,
                  (DWORD)(uintptr_t)&ctx, server == inst) ||
        in->generation != client_gen || insts[server].generation != server_gen ||
        !can_connect(inst, server, service)) {
        cv->reconnect_from = from + 1;
        if (in->generation == client_gen)
            set_error(inst, DMLERR_NO_CONV_ESTABLISHED);
        return 0;
    }

    conversation_list *list = list_of(cv->list);
    if (list && list->closing)
        list = NULL;
    in->error = DMLERR_NO_ERROR;
    HCONV fresh = connect_pair(inst, server, service, topic, &ctx, list);
    if (!fresh) {
        cv->reconnect_from = from + 1;
        if (in->generation == client_gen && !in->error)
            in->error = DMLERR_NO_CONV_ESTABLISHED;
        return 0;
    }

    for (int i = 0; i < LINKS; i++) {
        int next = conv_index(fresh);
        if (next < 0)
            return 0;
        const advise_link *link = &old_server->links[i];
        if (link->item)
            client_transaction(next, 0, link->item, link->fmt, link->type, 0, NULL);
    }
    if (conv_index(fresh) < 0)
        return 0;
    in->error = DMLERR_NO_ERROR;
    return fresh;
}

static void list_close(conversation_list *list, int tell)
{
    if (list->closing)
        return;
    list->closing = 1;
    while (list->count)
        conv_end(list->members[list->count - 1], tell);
    memset(list, 0, sizeof(*list));
}

static HCONVLIST WINAPI DdeConnectList(DWORD id, HSZ service, HSZ topic, HCONVLIST handle,
                                       PCONVCONTEXT cc)
{
    instance *in = inst_of(id);
    if (!in || in->closing)
        return 0;
    int inst = (int)id - 1;
    conversation_list *list = handle ? list_of(handle) : list_new(inst);
    if (!list || list->inst != inst || list->busy || list->closing) {
        in->error = !handle ? DMLERR_LOW_MEMORY :
                    list && list->inst == inst ? DMLERR_REENTRANCY : DMLERR_INVALIDPARAMETER;
        return 0;
    }
    handle = list->handle;
    list->busy = 1;
    in->error = DMLERR_NO_ERROR;
    CONVCONTEXT ctx = connect_context(cc);
    DWORD context = (DWORD)(uintptr_t)&ctx;
    for (int s = 0; s < INSTS && list->handle == handle; s++) {
        if (!can_connect(inst, s, service))
            continue;
        DWORD server = insts[s].generation;
        if (service && topic) {
            if (callback(s, XTYP_CONNECT, 0, 0, topic, service, 0, context, s == inst) &&
                list->handle == handle && insts[s].generation == server && can_connect(inst, s, service))
                connect_pair(inst, s, service, topic, &ctx, list);
            continue;
        }

        HDDEDATA h = callback(s, XTYP_WILDCONNECT, 0, 0, topic, service, 0, context, s == inst);
        data_handle *d = data_of(h);
        if (!d)
            continue;
        /* A confirmation callback can release the original data handle. */
        DWORD n = d->size;
        uint8_t *pairs = malloc(n ? n : 1);
        if (pairs)
            memcpy(pairs, d->bytes, n);
        data_release(h);
        if (!pairs) {
            set_error(inst, DMLERR_LOW_MEMORY);
            break;
        }
        for (DWORD i = 0; n - i >= 8 && list->handle == handle && insts[s].generation == server; i += 8) {
            HSZ svc = rd32le(pairs + i), top = rd32le(pairs + i + 4);
            if (!svc || !top)
                break;
            if ((!service || service == svc) && (!topic || topic == top) && can_connect(inst, s, svc))
                connect_pair(inst, s, svc, top, &ctx, list);
        }
        free(pairs);
    }
    if (list->handle != handle)
        return 0;                       /* a callback uninitialized the client */
    list->busy = 0;
    if (!list->count) {
        list_close(list, 0);
        if (!in->error)
            in->error = DMLERR_NO_CONV_ESTABLISHED;
        return 0;
    }
    in->error = DMLERR_NO_ERROR;
    return handle;
}

static HCONV WINAPI DdeQueryNextServer(HCONVLIST handle, HCONV prev)
{
    conversation_list *list = list_of(handle);
    if (!list || list->closing)
        return 0;
    unsigned i = 0;
    if (prev) {
        while (i < list->count && conv_handle(list->members[i]) != prev)
            i++;
        if (i == list->count)
            return 0;
        i++;
    }
    return i < list->count ? conv_handle(list->members[i]) : 0;
}

static BOOL WINAPI DdeDisconnectList(HCONVLIST handle)
{
    conversation_list *list = list_of(handle);
    if (!list || list->busy || list->closing)
        return FALSE;
    list_close(list, 1);
    return TRUE;
}

static BOOL WINAPI DdeSetUserHandle(HCONV h, DWORD id, DWORD user)
{
    (void)id;
    int c = conv_index(h);
    if (c < 0)
        return FALSE;
    convs[c].user = user;
    return TRUE;
}

static UINT WINAPI DdeQueryConvInfo(HCONV h, DWORD id, PCONVINFO ci)
{
    (void)id;
    int c = conv_index(h);
    if (c < 0 || !ci)
        return 0;
    conversation *cv = &convs[c];
    CONVINFO info;
    memset(&info, 0, sizeof(info));
    info.cb = sizeof(info);
    info.hUser = cv->user;
    info.hConvPartner = cv->partner >= 0 ? conv_handle(cv->partner) : 0;
    info.hszSvcPartner = info.hszServiceReq = cv->service;
    info.hszTopic = cv->topic;
    info.wStatus = (UINT)(ST_ISLOCAL | (cv->client ? ST_CLIENT : 0) |
                          (cv->partner < 0 ? ST_TERMINATED : ST_CONNECTED) |
                          (cv->partner >= 0 && convs[cv->partner].inst == cv->inst ? ST_ISSELF : 0));
    info.wStatus |= cv->callback_state;
    info.hConvList = cv->list;
    info.ConvCtxt = cv->context;
    if (cv->list)
        info.wStatus |= ST_INLIST;
    info.wConvst = cv->partner >= 0 ? XST_CONNECTED : XST_NULL;
    const conversation *server = cv;
    if (cv->client) {
        int p = cv->partner >= 0 ? cv->partner : cv->reconnect_from - 1;
        server = p >= 0 ? &convs[p] : NULL;
    }
    for (int k = 0; server && k < LINKS; k++)
        if (server->links[k].item)
            info.wStatus |= ST_ADVISE;
    DWORD n = ci->cb && ci->cb < sizeof(info) ? ci->cb : sizeof(info);
    memcpy(ci, &info, n);
    ci->cb = n;
    return (UINT)n;
}

/* ---------------------------------------------------------------- transactions */

static int advise_slot(const conversation *server, HSZ item, UINT fmt)
{
    int free_slot = -1;
    for (int i = 0; i < LINKS; i++) {
        const advise_link *link = &server->links[i];
        if (link->item == item && link->fmt == fmt)
            return i;
        if (!link->item && free_slot < 0)
            free_slot = i;
    }
    return free_slot;
}

/* the client's transaction with the server: its result (a data handle,
 * TRUE, 0, or CBR_BLOCK). The queue retains the input until completion. */
static HDDEDATA transact(int c, HDDEDATA given, HSZ item, UINT fmt, UINT type, DWORD *status)
{
    conversation *cv = &convs[c];
    int p = cv->partner;
    *status = 0;
    if (p < 0 || !convs[p].used) {
        return 0;
    }
    int s = convs[p].inst;
    HCONV sh = conv_handle(p), ch = conv_handle(c);
    HDDEDATA r = 0;
    switch (type & ~(XTYPF_NODATA | XTYPF_ACKREQ)) {
    case XTYP_REQUEST:
        r = callback(s, XTYP_REQUEST, fmt, sh, cv->topic, item, 0, 0, 0);
        if (r == CBR_BLOCK)
            return r;
        if (r && data_of(r) && conv_index(ch) == c) {          /* the client's to free: the server's own a copy */
            data_handle *d = data_of(r);
            if (d->app_owned)
                r = data_new(cv->inst, d->bytes, d->size, d->item, d->fmt, 0);
            else
                d->inst = cv->inst;
            *status = r ? DDE_FACK : 0;
        } else {
            data_release(r);
            r = 0;
        }
        break;
    case XTYP_POKE:
    case XTYP_EXECUTE: {
        UINT t = (type & XTYP_MASK) == (XTYP_POKE & XTYP_MASK) ? XTYP_POKE : XTYP_EXECUTE;
        HDDEDATA a = callback(s, t, t == XTYP_POKE ? fmt : 0, sh, cv->topic,
                              t == XTYP_POKE ? item : 0, given, 0, 0);
        if (a == CBR_BLOCK)
            return a;
        *status = (DWORD)(a & 0xffffu);
        r = (a & DDE_FACK) ? 1 : 0;
        break;
    }
    case XTYP_ADVSTART: {
        if (!item || advise_slot(&convs[p], item, fmt) < 0 ||
            !DdeKeepStringHandle((DWORD)(cv->inst + 1), item))
            break;
        HDDEDATA answer = callback(s, type, fmt, sh, cv->topic, item, 0, 0, 0);
        if (answer && answer != CBR_BLOCK && conv_index(sh) == p) {
            int k = advise_slot(&convs[p], item, fmt);
            if (k >= 0) {
                advise_link *link = &convs[p].links[k];
                if (link->item)
                    GlobalDeleteAtom((ATOM)link->item);
                *link = (advise_link){ item, fmt, type };
                *status = DDE_FACK;
                r = 1;
            }
        }
        if (!r)
            GlobalDeleteAtom((ATOM)item);
        if (answer == CBR_BLOCK)
            return answer;
        break;
    }
    case XTYP_ADVSTOP:
        for (int k = 0; k < LINKS; k++)
            if (convs[p].links[k].item == item && item && convs[p].links[k].fmt == fmt) {
                if (callback(s, XTYP_ADVSTOP, fmt, sh, cv->topic, item, 0, 0, 0) == CBR_BLOCK)
                    return CBR_BLOCK;
                advise_link *link = &convs[p].links[k];
                if (conv_index(sh) == p && link->item == item && link->fmt == fmt) {
                    memset(link, 0, sizeof(*link));
                    GlobalDeleteAtom((ATOM)item);
                }
                *status = DDE_FACK;
                r = 1;
                break;
            }
        break;
    }
    return r;
}

static DWORD advance(DWORD *counter)
{
    if (!++*counter)
        ++*counter;
    return *counter;
}

static pending *queue_callback(int inst, HCONV target, UINT type, UINT fmt, HSZ item,
                               HDDEDATA data)
{
    pending *p = NULL;
    for (int i = 0; i < PENDING; i++)
        if (!pend[i].order) {
            p = &pend[i];
            break;
        }
    if (!p || (item && !DdeKeepStringHandle((DWORD)(inst + 1), item))) {
        set_error(inst, p ? DMLERR_INVALIDPARAMETER : DMLERR_BUSY);
        data_release(data);
        return NULL;
    }
    *p = (pending){ .order = advance(&next_order), .target = target,
                    .type = type, .fmt = fmt, .item = item, .data = data };
    return p;
}

static void run_pending(pending *p, int c)
{
    conversation *cv = &convs[c];
    HCONV target = p->target;
    int notify = p->type == XTYP_ADVDATA || p->type == XTYP_XACT_COMPLETE;
    int client = conv_index(p->client);
    DWORD status = 0;

    if (cv->callback_state == ST_BLOCKNEXT)
        cv->callback_state = ST_BLOCKED;
    p->running = 1;
    HDDEDATA result = notify ?
        callback(cv->inst, p->type, p->fmt, target, cv->topic, p->item, p->data, p->id, p->status) :
        transact(client, p->data, p->item, p->fmt, p->type, &status);
    p->running = 0;

    if (result == CBR_BLOCK && !p->cancelled && conv_index(target) >= 0) {
        cv->callback_state = ST_BLOCKED;
        return;
    }
    if (notify) {
        drop_pending(p);
        return;
    }
    if (p->cancelled) {
        if (p->data != result)
            data_release(result);
        cancel_pending(p, p->error);
        return;
    }
    if (p->data != result)
        data_release(p->data);
    p->data = result;
    p->status = status;
    p->target = 0;
    if (p->sync) {
        p->error = result ? DMLERR_NO_ERROR : DMLERR_NOTPROCESSED;
        return;
    }
    client = conv_index(p->client);
    if (client < 0) {
        drop_pending(p);
        return;
    }
    int inst = convs[client].inst;
    p->type = XTYP_XACT_COMPLETE;
    p->target = p->client;
    p->order = advance(&next_order);
    p->posted = 1;
    if (!PostMessage(insts[inst].hwnd, WM_DDEML_READY, (WPARAM)(p - pend), (LPARAM)p->order)) {
        set_error(inst, DMLERR_POSTMSG_FAILED);
        drop_pending(p);
    }
}

static int waiting_for(const pending *p, int inst, HCONV h)
{
    if (p->running || p->cancelled || !p->target || (h && p->target != h))
        return -1;
    int c = conv_index(p->target);
    return c >= 0 && convs[c].inst == inst ? c : -1;
}

static void drain_callbacks(int inst, HCONV h)
{
    DWORD generation = insts[inst].generation;
    while (insts[inst].generation == generation && !insts[inst].closing) {
        /* Leave callbacks for a conversation already dispatching on its stack. */
        pending *first = NULL;
        int target = -1;
        for (int i = 0; i < PENDING; i++) {
            pending *p = &pend[i];
            int c = waiting_for(p, inst, h);
            if (c < 0 || p->posted || convs[c].callback_state == ST_BLOCKED)
                continue;
            int prior = 0;
            for (int k = 0; k < PENDING; k++)
                if (pend[k].target == p->target &&
                    (pend[k].running ||
                     (pend[k].posted && (int32_t)(pend[k].order - p->order) < 0)))
                    prior = 1;
            if (!prior && (!first || (int32_t)(p->order - first->order) < 0)) {
                first = p;
                target = c;
            }
        }
        if (!first)
            return;
        run_pending(first, target);
    }
}

static BOOL WINAPI DdeEnableCallback(DWORD id, HCONV h, UINT cmd)
{
    instance *in = inst_of(id);
    if (!in || in->closing)
        return FALSE;
    int inst = (int)id - 1;
    int c = conv_index(h);
    if ((h && (c < 0 || convs[c].inst != inst || convs[c].closing)) ||
        (cmd != EC_QUERYWAITING && cmd != EC_ENABLEALL && cmd != EC_ENABLEONE && cmd != EC_DISABLE)) {
        in->error = DMLERR_INVALIDPARAMETER;
        return FALSE;
    }
    in->error = DMLERR_NO_ERROR;
    if (cmd == EC_QUERYWAITING) {
        for (int i = 0; i < PENDING; i++)
            if (waiting_for(&pend[i], inst, h) >= 0)
                return TRUE;
        return FALSE;
    }
    for (int i = 0; i < CONVS; i++)
        if (convs[i].used && !convs[i].closing && convs[i].inst == inst && (!h || i == c))
            convs[i].callback_state = cmd;
    if (cmd != EC_DISABLE)
        drain_callbacks(inst, h);
    return TRUE;
}

static BOOL WINAPI DdeAbandonTransaction(DWORD id, HCONV h, DWORD xid)
{
    instance *in = inst_of(id);
    if (!in)
        return FALSE;
    int c = conv_index(h), inst = (int)id - 1;
    if (h && (c < 0 || convs[c].inst != inst || !convs[c].client)) {
        in->error = DMLERR_INVALIDPARAMETER;
        return FALSE;
    }
    if (!h)
        xid = 0;
    BOOL found = FALSE;
    for (int i = 0; i < PENDING; i++) {
        pending *p = &pend[i];
        int client = conv_index(p->client);
        if (p->id && !p->cancelled && client >= 0 && convs[client].inst == inst &&
            (!h || p->client == h) && (!xid || p->id == xid)) {
            cancel_pending(p, DMLERR_NO_ERROR);
            found = TRUE;
        }
    }
    in->error = !xid || found ? DMLERR_NO_ERROR : DMLERR_UNFOUND_QUEUE_ID;
    return !in->error;
}

static UINT timeout_error(UINT type)
{
    switch (type & ~(XTYPF_NODATA | XTYPF_ACKREQ)) {
    case XTYP_ADVSTART: return DMLERR_ADVACKTIMEOUT;
    case XTYP_ADVSTOP:  return DMLERR_UNADVACKTIMEOUT;
    case XTYP_POKE:     return DMLERR_POKEACKTIMEOUT;
    case XTYP_EXECUTE:  return DMLERR_EXECACKTIMEOUT;
    default:            return DMLERR_DATAACKTIMEOUT;
    }
}

static HDDEDATA client_transaction(int c, HDDEDATA given, HSZ item, UINT fmt, UINT type,
                                   DWORD timeout, LPDWORD result)
{
    conversation *cv = &convs[c];
    int inst = cv->inst, server = cv->partner;
    int sync = timeout != TIMEOUT_ASYNC;
    UINT error = DMLERR_INVALIDPARAMETER;
    switch (type & ~(XTYPF_NODATA | XTYPF_ACKREQ)) {
    case XTYP_ADVSTART: case XTYP_ADVSTOP: case XTYP_EXECUTE: case XTYP_POKE: case XTYP_REQUEST:
        if (cv->client)
            error = server < 0 ? DMLERR_SERVER_DIED : DMLERR_NO_ERROR;
        break;
    }
    if (error) {
        data_release(given);
        set_error(inst, error);
        return 0;
    }

    if (sync)
        for (int i = 0; i < PENDING; i++) {
            int owner = conv_index(pend[i].client);
            if (pend[i].sync && owner >= 0 &&
                insts[convs[owner].inst].task == insts[inst].task) {
                data_release(given);
                set_error(inst, DMLERR_REENTRANCY);
                return 0;
            }
        }

    data_handle *input = data_of(given);
    if (input && input->app_owned) {
        given = data_new(inst, input->bytes, input->size, input->item, input->fmt, 0);
        if (!given) {
            set_error(inst, DMLERR_MEMORY_ERROR);
            return 0;
        }
    }
    DWORD generation = insts[inst].generation, started = GetTickCount();
    pending *p = queue_callback(inst, conv_handle(server), type, fmt, item, given);
    if (!p)
        return 0;
    p->client = conv_handle(c);
    p->sync = sync;
    DWORD order = p->order, id = sync ? 0 : advance(&next_id);
    p->id = id;
    if (result)
        *result = id;
    drain_callbacks(convs[server].inst, p->target);
    if (!sync)
        return 1;

    while (p->order == order && p->target && GetTickCount() - started < timeout) {
        MSG msg;
        if (!PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            continue;
        if (msg.message == WM_QUIT) {
            PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (p->order != order)
        return 0;
    HDDEDATA answer = 0;
    if (p->target) {
        error = timeout_error(type);
    } else {
        error = p->error;
        if (!error) {
            answer = p->data;
            p->data = 0;
        }
    }
    if (result)
        *result = p->status;
    drop_pending(p);
    if (insts[inst].generation == generation)
        set_error(inst, error);
    return answer;
}

static HDDEDATA WINAPI DdeClientTransaction(LPBYTE data, DWORD cb, HCONV h, HSZ item, UINT fmt,
                                            UINT type, DWORD timeout, LPDWORD result)
{
    int c = conv_index(h);
    if (c < 0)
        return 0;
    HDDEDATA given = 0;
    if (cb == 0xffffffffu)
        given = (HDDEDATA)(uintptr_t)data;
    else if (data && ((type & XTYP_MASK) == (XTYP_POKE & XTYP_MASK) ||
                      (type & XTYP_MASK) == (XTYP_EXECUTE & XTYP_MASK))) {
        given = data_new(convs[c].inst, data, cb, item, fmt, 0);
        if (!given) {
            set_error(convs[c].inst, DMLERR_MEMORY_ERROR);
            return 0;
        }
    }
    return client_transaction(c, given, item, fmt, type, timeout, result);
}

static BOOL WINAPI DdePostAdvise(DWORD id, HSZ topic, HSZ item)
{
    if (!inst_of(id))
        return FALSE;
    int inst = (int)id - 1;
    BOOL ok = TRUE;
    for (int c = 0; c < CONVS; c++) {
        conversation *cv = &convs[c];
        if (!cv->used || cv->client || cv->partner < 0 || cv->inst != inst ||
            (topic && cv->topic != topic))
            continue;
        HCONV h = conv_handle(c);
        for (int k = 0; k < LINKS; k++) {
            advise_link l = cv->links[k];
            if (!l.item || (item && l.item != item))
                continue;
            HDDEDATA d = callback(inst, XTYP_ADVREQ, l.fmt, conv_handle(c), cv->topic, l.item, 0,
                                  0, 0);
            if (conv_index(h) == c && cv->partner >= 0) {
                int p = cv->partner, client = convs[p].inst;
                HDDEDATA given = 0;
                if (!(l.type & XTYPF_NODATA)) {
                    data_handle *src = data_of(d);
                    if (src && src->app_owned) {
                        given = data_new(client, src->bytes, src->size, src->item, src->fmt, 0);
                        if (!given) {
                            set_error(inst, DMLERR_MEMORY_ERROR);
                            ok = FALSE;
                        }
                    } else if (src) {
                        given = d;
                        src->inst = client;
                        d = 0;
                    }
                }
                if (given || (l.type & XTYPF_NODATA)) {
                    HCONV target = conv_handle(p);
                    if (queue_callback(inst, target, XTYP_ADVDATA, l.fmt, l.item, given))
                        drain_callbacks(client, target);
                    else
                        ok = FALSE;
                }
            }
            data_release(d);
            if (conv_index(h) < 0)
                break;
        }
    }
    return ok;
}

/* its conversations ended (the others told, when they may be), its data,
 * its services and its window gone */
static void uninit(int inst, int tell)
{
    instance *in = &insts[inst];
    if (in->closing)
        return;
    in->closing = 1;
    for (int i = 0; i < LISTS; i++)
        if (lists[i].handle && lists[i].inst == inst)
            list_close(&lists[i], tell);
    for (int c = 0; c < CONVS; c++)
        if (convs[c].used && convs[c].inst == inst)
            conv_end(c, tell);
    for (int i = 0; i < DATAS; i++)
        if (datas[i].handle && datas[i].inst == inst)
            data_free(datas[i].handle);
    for (int k = 0; k < SERVICES; k++)
        if (in->services[k]) {
            HSZ service = in->services[k];
            in->services[k] = 0;
            if (tell)
                tell_others(inst, XTYP_UNREGISTER, service);
            GlobalDeleteAtom((ATOM)service);
        }
    if (tell && in->hwnd && IsWindow(in->hwnd))
        DestroyWindow(in->hwnd);
    memset(in, 0, sizeof(*in));
}

static BOOL WINAPI DdeUninitialize(DWORD id)
{
    if (!inst_of(id))
        return FALSE;
    uninit((int)id - 1, 1);
    return TRUE;
}

/* ---------------------------------------------------------------- Win16 programs */

static void put32(win_dll_call *c, uint32_t far, uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    if (far >> 16)
        NE->write(c, far, b, 4);
}

static uint32_t get32(win_dll_call *c, uint32_t far)
{
    uint8_t b[4];
    if (!(far >> 16) || !NE->read(c, far, b, 4))
        return 0;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

/* the program's bytes (n of them, 64K at most) as a new handle's */
static HDDEDATA guest_data(win_dll_call *c, int inst, uint32_t far, DWORD n, DWORD off, HSZ item,
                           UINT fmt, int owned)
{
    if (n > 0x10000u)
        n = 0x10000u;
    uint8_t *tmp = (uint8_t *)malloc(n ? n : 1);
    if (!tmp || (far && n && !NE->read(c, far, tmp, n))) {
        free(tmp);
        return 0;
    }
    HDDEDATA h = data_new(inst, NULL, off + n, item, fmt, owned);
    if (h && far && n)
        memcpy(data_of(h)->bytes + off, tmp, n);
    free(tmp);
    return h;
}

/* the caller a Win32 program: its "far" pointers flat (no 64K segments) */
static int flat(void)
{
    return NE->size >= WIN_DLL_NE_BASE_SIZE && NE->task_flat &&
           NE->task_flat((uint16_t)GetCurrentTask());
}

static int read_context(win_dll_call *c, uint32_t far, CONVCONTEXT *cc)
{
    uint8_t b[24];
    int is_flat = flat();
    unsigned n = is_flat ? sizeof(b) : 16;
    if (!NE->read(c, far, b, n) || (is_flat ? rd32le(b) : rd16le(b)) < n)
        return 0;
    cc->cb = sizeof(*cc);
    cc->wFlags = is_flat ? rd32le(b + 4) : rd16le(b + 2);
    cc->wCountryID = is_flat ? rd32le(b + 8) : rd16le(b + 4);
    cc->iCodePage = is_flat ? (int32_t)rd32le(b + 12) : (int16_t)rd16le(b + 6);
    cc->dwLangID = rd32le(b + (is_flat ? 16 : 8));
    cc->dwSecurity = rd32le(b + (is_flat ? 20 : 12));
    return 1;
}

static int ne_call(win_dll_call *c, uint16_t ordinal, uint32_t *v)
{
    switch (ordinal) {
    case 2: {                           /* DdeInitialize(LPDWORD, PFNCALLBACK, DWORD, DWORD) */
        uint32_t idp = NE->arg32(c, 16);
        DWORD id = get32(c, idp);
        UINT r = idp ? init(&id, NULL, NE->arg32(c, 12), NE->arg32(c, 8))
                     : DMLERR_INVALIDPARAMETER;
        if (!r)
            put32(c, idp, id);
        *v = r;
        return WINDLL_WORD;
    }
    case 3:                             /* DdeUninitialize(DWORD) */
        *v = DdeUninitialize(NE->arg32(c, 4));
        return WINDLL_WORD;
    case 4:                             /* DdeConnectList */
    case 7: {                           /* DdeConnect */
        unsigned off = ordinal == 4 ? 4 : 0;
        DWORD id = NE->arg32(c, 16 + off);
        HSZ service = NE->arg32(c, 12 + off), topic = NE->arg32(c, 8 + off);
        uint32_t far = NE->arg32(c, 4);
        CONVCONTEXT cc;
        *v = 0;
        if (far && !read_context(c, far, &cc)) {
            set_error((int)id - 1, DMLERR_INVALIDPARAMETER);
        } else if (ordinal == 4) {
            *v = DdeConnectList(id, service, topic, NE->arg32(c, 8), far ? &cc : NULL);
        } else {
            *v = DdeConnect(id, service, topic, far ? &cc : NULL);
        }
        return WINDLL_DWORD;
    }
    case 5:                             /* DdeQueryNextServer(list, prev) */
        *v = DdeQueryNextServer(NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_DWORD;
    case 6:                             /* DdeDisconnectList(list) */
        *v = DdeDisconnectList(NE->arg32(c, 4));
        return WINDLL_WORD;
    case 8:                             /* DdeDisconnect(HCONV) */
        *v = DdeDisconnect(NE->arg32(c, 4));
        return WINDLL_WORD;
    case 9: {                           /* DdeQueryConvInfo(HCONV, DWORD, LPCONVINFO): Win16's,
                                         * 58 bytes */
        uint32_t far = NE->arg32(c, 4);
        CONVINFO ci;
        uint8_t b[58];
        memset(&ci, 0, sizeof(ci));
        ci.cb = sizeof(ci);
        DWORD cb = get32(c, far);
        *v = 0;
        if (!far || !DdeQueryConvInfo(NE->arg32(c, 12), NE->arg32(c, 8), &ci))
            return WINDLL_WORD;
        if (!cb || cb > sizeof(b))
            cb = sizeof(b);
        memset(b, 0, sizeof(b));
        wr32le(b, cb);
        wr32le(b + 4, ci.hUser);
        wr32le(b + 8, ci.hConvPartner);
        wr32le(b + 12, ci.hszSvcPartner);
        wr32le(b + 16, ci.hszServiceReq);
        wr32le(b + 20, ci.hszTopic);
        wr32le(b + 24, ci.hszItem);
        wr16le(b + 28, (uint16_t)ci.wFmt);
        wr16le(b + 30, (uint16_t)ci.wType);
        wr16le(b + 32, (uint16_t)ci.wStatus);
        wr16le(b + 34, (uint16_t)ci.wConvst);
        wr16le(b + 36, (uint16_t)ci.wLastError);
        wr32le(b + 38, ci.hConvList);
        wr16le(b + 42, 16);
        wr16le(b + 44, (WORD)ci.ConvCtxt.wFlags);
        wr16le(b + 46, (WORD)ci.ConvCtxt.wCountryID);
        wr16le(b + 48, (WORD)ci.ConvCtxt.iCodePage);
        wr32le(b + 50, ci.ConvCtxt.dwLangID);
        wr32le(b + 54, ci.ConvCtxt.dwSecurity);
        *v = NE->write(c, far, b, cb) ? cb : 0;
        return WINDLL_WORD;
    }
    case 10:                            /* DdeSetUserHandle(HCONV, DWORD, DWORD) */
        *v = DdeSetUserHandle(NE->arg32(c, 12), NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 11: {                          /* DdeClientTransaction(LPBYTE, DWORD, HCONV, HSZ, UINT,
                                         * UINT, DWORD, LPDWORD) */
        uint32_t data = NE->arg32(c, 28), result_far = NE->arg32(c, 4);
        DWORD cb = NE->arg32(c, 24), timeout = NE->arg32(c, 8), result = 0;
        HCONV h = NE->arg32(c, 20);
        HSZ item = NE->arg32(c, 16);
        UINT fmt = NE->arg16(c, 14), type = NE->arg16(c, 12);
        int ci = conv_index(h);
        *v = 0;
        if (ci < 0)
            return WINDLL_DWORD;
        HDDEDATA given = 0;
        if (cb == 0xffffffffu)
            given = data;
        else if (data && ((type & XTYP_MASK) == (XTYP_POKE & XTYP_MASK) ||
                          (type & XTYP_MASK) == (XTYP_EXECUTE & XTYP_MASK))) {
            given = guest_data(c, convs[ci].inst, data, cb, 0, item, fmt, 0);
            if (!given) {
                set_error(convs[ci].inst, DMLERR_MEMORY_ERROR);
                return WINDLL_DWORD;
            }
        }
        *v = client_transaction(ci, given, item, fmt, type, timeout, &result);
        put32(c, result_far, result);
        return WINDLL_DWORD;
    }
    case 12:                            /* DdeAbandonTransaction(inst, HCONV, DWORD) */
        *v = DdeAbandonTransaction(NE->arg32(c, 12), NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 13:                            /* DdePostAdvise(inst, HSZ, HSZ) */
        *v = DdePostAdvise(NE->arg32(c, 12), NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 14: {                          /* DdeCreateDataHandle(inst, LPBYTE, cb, off, HSZ, UINT,
                                         * UINT) */
        DWORD id = NE->arg32(c, 24);
        *v = 0;
        if (inst_of(id))
            *v = guest_data(c, (int)id - 1, NE->arg32(c, 20), NE->arg32(c, 16), NE->arg32(c, 12),
                            NE->arg32(c, 8), NE->arg16(c, 6), NE->arg16(c, 4) & HDATA_APPOWNED);
        if (!*v && inst_of(id))
            set_error((int)id - 1, DMLERR_MEMORY_ERROR);
        return WINDLL_DWORD;
    }
    case 15: {                          /* DdeAddData(HDDEDATA, LPBYTE, cb, off) */
        HDDEDATA h = NE->arg32(c, 16);
        DWORD n = NE->arg32(c, 8), off = NE->arg32(c, 4);
        uint32_t src = NE->arg32(c, 12);
        uint8_t *tmp = n && n <= 0x10000u ? (uint8_t *)malloc(n) : NULL;
        *v = 0;
        if (tmp && NE->read(c, src, tmp, n))
            *v = DdeAddData(h, tmp, n, off);
        else if (!n)
            *v = DdeAddData(h, NULL, 0, off);
        free(tmp);
        return WINDLL_DWORD;
    }
    case 16: {                          /* DdeGetData(HDDEDATA, LPBYTE, max, off) */
        HDDEDATA h = NE->arg32(c, 16);
        uint32_t dst = NE->arg32(c, 12);
        DWORD max = NE->arg32(c, 8), off = NE->arg32(c, 4);
        data_handle *d = data_of(h);
        *v = 0;
        if (!d)
            return WINDLL_DWORD;
        if (!dst) {
            *v = d->size;
            return WINDLL_DWORD;
        }
        if (!flat() && max > 0x10000u - (uint16_t)dst)
            max = 0x10000u - (uint16_t)dst;
        if (off < d->size) {
            DWORD n = d->size - off < max ? d->size - off : max;
            if (NE->write(c, dst, d->bytes + off, n))
                *v = n;
        }
        return WINDLL_DWORD;
    }
    case 17: {                          /* DdeAccessData(HDDEDATA, LPDWORD): a copy in the
                                         * program's memory (a Win16 program's 64K of it) */
        data_handle *d = data_of(NE->arg32(c, 8));
        uint16_t task = (uint16_t)GetCurrentTask();
        DWORD n = d ? d->size : 0;
        *v = 0;
        if (!d || (!flat() && n > 0xfff0u) || NE->size < WIN_DLL_NE_BASE_SIZE || !NE->task_alloc)
            return WINDLL_DWORD;
        if (d->at && d->at_task != task) {
            NE->task_free(d->at_task, d->at);
            d->at = 0;
        }
        if (!d->at) {
            d->at = NE->task_alloc(task, n ? n : 1);
            d->at_task = task;
        }
        if (!d->at || (n && !NE->task_write(task, d->at, d->bytes, n)))
            return WINDLL_DWORD;
        put32(c, NE->arg32(c, 4), n);
        d->locked++;
        *v = d->at;
        return WINDLL_DWORD;
    }
    case 18: {                          /* DdeUnaccessData(HDDEDATA): the copy's bytes back */
        data_handle *d = data_of(NE->arg32(c, 4));
        if (d && d->at && d->locked && d->size)
            NE->task_read(d->at_task, d->at, d->bytes, d->size);
        *v = DdeUnaccessData(NE->arg32(c, 4));
        return WINDLL_WORD;
    }
    case 19:                            /* DdeFreeDataHandle(HDDEDATA) */
        *v = DdeFreeDataHandle(NE->arg32(c, 4));
        return WINDLL_WORD;
    case 20:                            /* DdeGetLastError(inst) */
        *v = DdeGetLastError(NE->arg32(c, 4));
        return WINDLL_WORD;
    case 21: {                          /* DdeCreateStringHandle(inst, LPCSTR, int) */
        char s[256];
        *v = NE->string(c, NE->arg32(c, 6), s, sizeof(s))
                 ? DdeCreateStringHandle(NE->arg32(c, 10), s, (int16_t)NE->arg16(c, 4)) : 0;
        return WINDLL_DWORD;
    }
    case 22:                            /* DdeFreeStringHandle(inst, HSZ) */
        *v = DdeFreeStringHandle(NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 23: {                          /* DdeQueryString(inst, HSZ, LPSTR, max, int) */
        char s[256];
        uint32_t out = NE->arg32(c, 10);
        DWORD max = NE->arg32(c, 6);
        DWORD id = NE->arg32(c, 18);
        HSZ hsz = NE->arg32(c, 14);
        if (!out) {
            *v = DdeQueryString(id, hsz, NULL, 0, 0);
            return WINDLL_DWORD;
        }
        *v = DdeQueryString(id, hsz, s, max < sizeof(s) ? max : sizeof(s), 0);
        if (max)
            NE->write(c, out, s, *v + 1);
        return WINDLL_DWORD;
    }
    case 24:                            /* DdeKeepStringHandle(inst, HSZ) */
        *v = DdeKeepStringHandle(NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 26:                            /* DdeEnableCallback(inst, HCONV, UINT) */
        *v = DdeEnableCallback(NE->arg32(c, 10), NE->arg32(c, 6), NE->arg16(c, 4));
        return WINDLL_WORD;
    case 27:                            /* DdeNameService(inst, HSZ, HSZ, UINT) */
        *v = DdeNameService(NE->arg32(c, 14), NE->arg32(c, 10), NE->arg32(c, 6),
                            NE->arg16(c, 4));
        return WINDLL_DWORD;
    case 36:                            /* DdeCmpStringHandles(HSZ, HSZ) */
        *v = (uint16_t)DdeCmpStringHandles(NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_WORD;
    case 37:                            /* DdeReconnect(HCONV) */
        *v = DdeReconnect(NE->arg32(c, 4));
        return WINDLL_DWORD;
    }
    return WINDLL_NONE;
}

/* a task ended: its instances go, no callback into it, the others' ends
 * of its conversations terminated */
static void task_end(uint16_t task)
{
    for (int i = 0; i < INSTS; i++)
        if (insts[i].used && (uint16_t)insts[i].task == task)
            uninit(i, 0);
}

static void term(void)
{
    for (int i = 0; i < INSTS; i++)
        if (insts[i].used)
            uninit(i, 0);
    if (wnd_class)
        UnregisterClass("WinnDdeml", 0);
    wnd_class = 0;
}

static const win_dll_export exports[] = {
    { 2, 16, "DdeInitialize", (const void *)DdeInitialize },
    { 3, 4, "DdeUninitialize", (const void *)DdeUninitialize },
    { 4, 20, "DdeConnectList", (const void *)DdeConnectList },
    { 5, 8, "DdeQueryNextServer", (const void *)DdeQueryNextServer },
    { 6, 4, "DdeDisconnectList", (const void *)DdeDisconnectList },
    { 7, 16, "DdeConnect", (const void *)DdeConnect },
    { 8, 4, "DdeDisconnect", (const void *)DdeDisconnect },
    { 9, 12, "DdeQueryConvInfo", (const void *)DdeQueryConvInfo },
    { 10, 12, "DdeSetUserHandle", (const void *)DdeSetUserHandle },
    { 11, 28, "DdeClientTransaction", (const void *)DdeClientTransaction },
    { 12, 12, "DdeAbandonTransaction", (const void *)DdeAbandonTransaction },
    { 13, 12, "DdePostAdvise", (const void *)DdePostAdvise },
    { 14, 24, "DdeCreateDataHandle", (const void *)DdeCreateDataHandle },
    { 15, 16, "DdeAddData", (const void *)DdeAddData },
    { 16, 16, "DdeGetData", (const void *)DdeGetData },
    { 17, 8, "DdeAccessData", (const void *)DdeAccessData },
    { 18, 4, "DdeUnaccessData", (const void *)DdeUnaccessData },
    { 19, 4, "DdeFreeDataHandle", (const void *)DdeFreeDataHandle },
    { 20, 4, "DdeGetLastError", (const void *)DdeGetLastError },
    { 21, 10, "DdeCreateStringHandle", (const void *)DdeCreateStringHandle },
    { 22, 8, "DdeFreeStringHandle", (const void *)DdeFreeStringHandle },
    { 23, 18, "DdeQueryString", (const void *)DdeQueryString },
    { 24, 8, "DdeKeepStringHandle", (const void *)DdeKeepStringHandle },
    { 26, 10, "DdeEnableCallback", (const void *)DdeEnableCallback },
    { 27, 14, "DdeNameService", (const void *)DdeNameService },
    { 36, 8, "DdeCmpStringHandles", (const void *)DdeCmpStringHandles },
    { 37, 4, "DdeReconnect", (const void *)DdeReconnect },
};

static const win_dll_info info = {
    WINDLL_VERSION, sizeof(win_dll_info), "DDEML",
    exports, sizeof(exports) / sizeof(exports[0]), ne_call, task_end, term, NULL,
};

const win_dll_info *WinDllInit(void) { return &info; }
