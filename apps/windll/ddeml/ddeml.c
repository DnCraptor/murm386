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
 * WINN has no DDE between machines (NetDDE) and no conversations with
 * programs speaking DDE's messages themselves; a callback that blocks
 * (CBR_BLOCK, DdeEnableCallback) is not queued.
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
#define LINKS     8
#define DATAS     64
#define SERVICES  8
#define PENDING   8

#define CONV_TAG  0x43000000u           /* HCONV: tag | index + 1 */
#define DATA_TAG  0x44000000u           /* HDDEDATA */

#define WM_DDEML_CALLBACK (WM_USER + 0x0D0)
#define WM_DDEML_COMPLETE (WM_USER + 0x0D1)

typedef struct {
    int used;
    HTASK task;
    PFNCALLBACK native;             /* a native caller's callback, or */
    uint32_t proc;                  /* a Win16 program's (selector:offset) */
    DWORD cmd;                      /* APPCMD_, CBF_ */
    HWND hwnd;
    UINT error;
    int unfiltered;                 /* DNS_FILTEROFF */
    HSZ services[SERVICES];
} instance;

typedef struct {
    HSZ item;
    UINT fmt, type;                 /* XTYP_ADVSTART's flags */
} advise_link;

typedef struct {
    int used;
    int inst;
    int partner;                    /* the other end, -1 when it went */
    int client;
    HSZ service, topic;
    DWORD user;
    advise_link links[LINKS];       /* a server's */
} conversation;

typedef struct {
    int used;
    int inst;
    uint8_t *bytes;
    DWORD size;
    HSZ item;
    UINT fmt;
    int app_owned;
    int locked;
} data_handle;

/* an asynchronous transaction done: XTYP_XACT_COMPLETE to the client */
typedef struct {
    int used, inst, conv;
    UINT fmt;
    HSZ item;
    HDDEDATA data;
    DWORD id, result;
} pending;

static instance insts[INSTS];
static conversation convs[CONVS];
static data_handle datas[DATAS];
static pending pend[PENDING];
static DWORD next_id = 1;
static ATOM wnd_class;

/* ---------------------------------------------------------------- handles */

static instance *inst_of(DWORD id)
{
    return id >= 1 && id <= INSTS && insts[id - 1].used ? &insts[id - 1] : NULL;
}

static int conv_index(HCONV h)
{
    unsigned i = (unsigned)(h & 0xffffu) - 1u;
    return (h & 0xffff0000u) == CONV_TAG && i < CONVS && convs[i].used ? (int)i : -1;
}

static HCONV conv_handle(int i) { return i >= 0 ? CONV_TAG | (DWORD)(i + 1) : 0; }

static data_handle *data_of(HDDEDATA h)
{
    unsigned i = (unsigned)(h & 0xffffu) - 1u;
    return (h & 0xffff0000u) == DATA_TAG && i < DATAS && datas[i].used ? &datas[i] : NULL;
}

static HDDEDATA data_new(int inst, const uint8_t *src, DWORD size, HSZ item, UINT fmt, int owned)
{
    for (int i = 0; i < DATAS; i++)
        if (!datas[i].used) {
            data_handle *d = &datas[i];
            d->bytes = (uint8_t *)malloc(size ? size : 1);
            if (!d->bytes)
                return 0;
            if (src)
                memcpy(d->bytes, src, size);
            else
                memset(d->bytes, 0, size ? size : 1);
            d->used = 1;
            d->inst = inst;
            d->size = size;
            d->item = item;
            d->fmt = fmt;
            d->app_owned = owned;
            d->locked = 0;
            return DATA_TAG | (DWORD)(i + 1);
        }
    return 0;
}

static void data_free(HDDEDATA h)
{
    data_handle *d = data_of(h);
    if (d) {
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

#define NE (__win_dll_ne)

static HDDEDATA invoke(const callback_call *c)
{
    instance *in = &insts[c->inst];
    if (in->native)
        return in->native(c->type, c->fmt, c->conv, c->hsz1, c->hsz2, c->data, c->data1, c->data2);
    if (!in->proc || !NE || NE->size < sizeof(win_dll_ne) || !NE->call)
        return 0;
    /* (UINT, UINT, HCONV, HSZ, HSZ, HDDEDATA, DWORD, DWORD) */
    const uint16_t w[14] = {
        (uint16_t)c->type, (uint16_t)c->fmt,
        (uint16_t)(c->conv >> 16), (uint16_t)c->conv,
        (uint16_t)(c->hsz1 >> 16), (uint16_t)c->hsz1,
        (uint16_t)(c->hsz2 >> 16), (uint16_t)c->hsz2,
        (uint16_t)(c->data >> 16), (uint16_t)c->data,
        (uint16_t)(c->data1 >> 16), (uint16_t)c->data1,
        (uint16_t)(c->data2 >> 16), (uint16_t)c->data2,
    };
    return NE->call((uint16_t)in->task, in->proc, w, 14);
}

/* what the instance's filters keep from its callback */
static int filtered(const instance *in, UINT type)
{
    DWORD f = in->cmd;
    switch (type) {
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
    return c.result == CBR_BLOCK ? 0 : c.result;
}

static LRESULT CALLBACK ddeml_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DDEML_CALLBACK && lp) {
        callback_call *c = (callback_call *)(uintptr_t)lp;
        c->result = invoke(c);
        return 0;
    }
    if (msg == WM_DDEML_COMPLETE && wp < PENDING && pend[wp].used) {
        pending p = pend[wp];
        pend[wp].used = 0;
        int ci = p.conv;
        if (convs[ci].used && convs[ci].inst == p.inst)
            callback(p.inst, XTYP_XACT_COMPLETE, p.fmt, conv_handle(ci), convs[ci].topic, p.item,
                     p.data, p.id, p.result);
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
    return inst_of(id) && hsz && GlobalGetAtomName((ATOM)hsz, s, sizeof(s)) &&
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
    return service && in->unfiltered;
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
            in->services[k] = hsz1;
            tell_others(inst, XTYP_REGISTER, hsz1);
        }
    } else if (cmd & DNS_UNREGISTER) {
        for (int k = 0; k < SERVICES; k++)
            if (in->services[k] && (!hsz1 || in->services[k] == hsz1)) {
                HSZ s = in->services[k];
                in->services[k] = 0;
                tell_others(inst, XTYP_UNREGISTER, s);
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
            convs[i].inst = inst;
            convs[i].client = client;
            convs[i].service = service;
            convs[i].topic = topic;
            convs[i].partner = -1;
            return i;
        }
    return -1;
}

static HCONV WINAPI DdeConnect(DWORD id, HSZ service, HSZ topic, PCONVCONTEXT cc)
{
    (void)cc;
    instance *in = inst_of(id);
    int inst = (int)id - 1;
    if (!in)
        return 0;
    for (int s = 0; s < INSTS; s++) {
        instance *sv = &insts[s];
        if (!sv->used || !serves(sv, service) ||
            (s == inst && (in->cmd & CBF_FAIL_SELFCONNECTIONS)))
            continue;
        HSZ name = service ? service : first_service(sv);
        if (!callback(s, XTYP_CONNECT, 0, 0, topic, name, 0, 0, s == inst))
            continue;
        int c = conv_new(inst, 1, name, topic), v = c >= 0 ? conv_new(s, 0, name, topic) : -1;
        if (v < 0) {
            if (c >= 0)
                convs[c].used = 0;
            in->error = DMLERR_LOW_MEMORY;
            return 0;
        }
        convs[c].partner = v;
        convs[v].partner = c;
        callback(s, XTYP_CONNECT_CONFIRM, 0, conv_handle(v), topic, name, 0, 0, s == inst);
        if (!convs[c].used)
            return 0;                   /* gone in it */
        return conv_handle(c);
    }
    in->error = DMLERR_NO_CONV_ESTABLISHED;
    return 0;
}

static void conv_end(int c, int tell)
{
    int p = convs[c].partner;
    convs[c].used = 0;
    if (p >= 0 && convs[p].used && convs[p].partner == c) {
        convs[p].partner = -1;
        if (tell)
            callback(convs[p].inst, XTYP_DISCONNECT, 0, conv_handle(p), 0, 0, 0, 0,
                     convs[p].inst == convs[c].inst);
        convs[p].used = 0;
    }
}

static BOOL WINAPI DdeDisconnect(HCONV h)
{
    int c = conv_index(h);
    if (c < 0)
        return FALSE;
    conv_end(c, 1);
    return TRUE;
}

static HCONV WINAPI DdeReconnect(HCONV h)
{
    (void)h;
    return 0;
}

static HCONVLIST WINAPI DdeConnectList(DWORD id, HSZ service, HSZ topic, HCONVLIST list,
                                       PCONVCONTEXT cc)
{
    (void)service; (void)topic; (void)list; (void)cc;
    set_error((int)id - 1, DMLERR_NO_CONV_ESTABLISHED);
    return 0;
}

static HCONV WINAPI DdeQueryNextServer(HCONVLIST list, HCONV prev)
{
    (void)list; (void)prev;
    return 0;
}

static BOOL WINAPI DdeDisconnectList(HCONVLIST list)
{
    (void)list;
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
    info.wStatus = (UINT)(ST_CONNECTED | ST_ISLOCAL | (cv->client ? ST_CLIENT : 0) |
                          (cv->partner < 0 ? ST_TERMINATED : 0) |
                          (cv->partner >= 0 && convs[cv->partner].inst == cv->inst ? ST_ISSELF : 0));
    info.wConvst = cv->partner >= 0 ? XST_CONNECTED : XST_NULL;
    for (int k = 0; k < LINKS && !cv->client; k++)
        if (cv->links[k].item)
            info.wStatus |= ST_ADVISE;
    DWORD n = ci->cb && ci->cb < sizeof(info) ? ci->cb : sizeof(info);
    memcpy(ci, &info, n);
    ci->cb = n;
    return (UINT)n;
}

static BOOL WINAPI DdeEnableCallback(DWORD id, HCONV h, UINT cmd)
{
    (void)h; (void)cmd;
    return inst_of(id) != NULL;
}

static BOOL WINAPI DdeAbandonTransaction(DWORD id, HCONV h, DWORD xid)
{
    (void)h;
    for (int i = 0; i < PENDING; i++)
        if (pend[i].used && pend[i].inst == (int)id - 1 && (!xid || pend[i].id == xid)) {
            data_release(pend[i].data);
            pend[i].used = 0;
        }
    return inst_of(id) != NULL;
}

/* ---------------------------------------------------------------- transactions */

/* the client's transaction with the server: its result (a data handle,
 * TRUE, or 0), *status DDE_FACK ... The data given is the system's then. */
static HDDEDATA transact(int c, HDDEDATA given, HSZ item, UINT fmt, UINT type, DWORD *status)
{
    conversation *cv = &convs[c];
    int p = cv->partner;
    *status = 0;
    if (p < 0 || !convs[p].used) {
        data_release(given);
        set_error(cv->inst, DMLERR_SERVER_DIED);
        return 0;
    }
    int s = convs[p].inst;
    HCONV sh = conv_handle(p);
    HDDEDATA r = 0;
    switch (type & ~(XTYPF_NODATA | XTYPF_ACKREQ)) {
    case XTYP_REQUEST:
        r = callback(s, XTYP_REQUEST, fmt, sh, cv->topic, item, 0, 0, 0);
        if (r && data_of(r)) {          /* the client's to free: the server's own a copy */
            data_handle *d = data_of(r);
            if (d->app_owned)
                r = data_new(cv->inst, d->bytes, d->size, d->item, d->fmt, 0);
            else
                d->inst = cv->inst;
            *status = r ? DDE_FACK : 0;
        } else {
            r = 0;
        }
        break;
    case XTYP_POKE:
    case XTYP_EXECUTE: {
        UINT t = (type & XTYP_MASK) == (XTYP_POKE & XTYP_MASK) ? XTYP_POKE : XTYP_EXECUTE;
        HDDEDATA a = callback(s, t, t == XTYP_POKE ? fmt : 0, sh, cv->topic,
                              t == XTYP_POKE ? item : 0, given, 0, 0);
        *status = (DWORD)(a & 0xffffu);
        r = (a & DDE_FACK) ? 1 : 0;
        break;
    }
    case XTYP_ADVSTART: {
        int k = 0;
        while (k < LINKS && convs[p].links[k].item &&
               !(convs[p].links[k].item == item && convs[p].links[k].fmt == fmt))
            k++;
        if (k < LINKS && callback(s, XTYP_ADVSTART, fmt, sh, cv->topic, item, 0, 0, 0)) {
            convs[p].links[k].item = item;
            convs[p].links[k].fmt = fmt;
            convs[p].links[k].type = type;
            *status = DDE_FACK;
            r = 1;
        }
        break;
    }
    case XTYP_ADVSTOP:
        for (int k = 0; k < LINKS; k++)
            if (convs[p].links[k].item == item && convs[p].links[k].fmt == fmt) {
                memset(&convs[p].links[k], 0, sizeof(convs[p].links[k]));
                callback(s, XTYP_ADVSTOP, fmt, sh, cv->topic, item, 0, 0, 0);
                *status = DDE_FACK;
                r = 1;
            }
        break;
    default:
        data_release(given);
        set_error(cv->inst, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    data_release(given);
    if (!r)
        set_error(cv->inst, DMLERR_NOTPROCESSED);
    return r;
}

/* data given as bytes (a handle when cb is -1) */
static HDDEDATA client_transaction(int c, HDDEDATA given, HSZ item, UINT fmt, UINT type,
                                   DWORD timeout, LPDWORD result)
{
    DWORD status;
    if (!convs[c].client) {
        data_release(given);
        set_error(convs[c].inst, DMLERR_INVALIDPARAMETER);
        return 0;
    }
    HDDEDATA r = transact(c, given, item, fmt, type, &status);
    if (timeout != TIMEOUT_ASYNC) {
        if (result)
            *result = status;
        return r;
    }
    /* asynchronous: done already, its XTYP_XACT_COMPLETE as the client
     * takes its messages */
    for (int i = 0; i < PENDING; i++)
        if (!pend[i].used) {
            pending *p = &pend[i];
            p->used = 1;
            p->inst = convs[c].inst;
            p->conv = c;
            p->fmt = fmt;
            p->item = item;
            p->data = r > 1 ? r : 0;
            p->id = next_id++;
            if (!next_id)
                next_id = 1;
            p->result = status;
            if (result)
                *result = p->id;
            PostMessage(insts[p->inst].hwnd, WM_DDEML_COMPLETE, (WPARAM)i, 0);
            return 1;
        }
    data_release(r > 1 ? r : 0);
    set_error(convs[c].inst, DMLERR_BUSY);
    return 0;
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
                      (type & XTYP_MASK) == (XTYP_EXECUTE & XTYP_MASK)))
        given = data_new(convs[c].inst, data, cb, item, fmt, 0);
    return client_transaction(c, given, item, fmt, type, timeout, result);
}

static BOOL WINAPI DdePostAdvise(DWORD id, HSZ topic, HSZ item)
{
    if (!inst_of(id))
        return FALSE;
    int inst = (int)id - 1;
    for (int c = 0; c < CONVS; c++) {
        conversation *cv = &convs[c];
        if (!cv->used || cv->client || cv->inst != inst || (topic && cv->topic != topic))
            continue;
        for (int k = 0; k < LINKS; k++) {
            advise_link l = cv->links[k];
            if (!l.item || (item && l.item != item))
                continue;
            HDDEDATA d = callback(inst, XTYP_ADVREQ, l.fmt, conv_handle(c), cv->topic, l.item, 0,
                                  0, 0);
            int p = cv->partner;
            if (p >= 0 && convs[p].used)
                callback(convs[p].inst, XTYP_ADVDATA, l.fmt, conv_handle(p), cv->topic, l.item,
                         (l.type & XTYPF_NODATA) ? 0 : d, 0, 0);
            data_release(d);
            if (!cv->used)
                break;
        }
    }
    return TRUE;
}

/* its conversations ended (the others told, when they may be), its data,
 * its services and its window gone */
static void uninit(int inst, int tell)
{
    instance *in = &insts[inst];
    for (int c = 0; c < CONVS; c++)
        if (convs[c].used && convs[c].inst == inst)
            conv_end(c, tell);
    for (int i = 0; i < DATAS; i++)
        if (datas[i].used && datas[i].inst == inst)
            data_free(DATA_TAG | (DWORD)(i + 1));
    for (int i = 0; i < PENDING; i++)
        if (pend[i].used && pend[i].inst == inst)
            pend[i].used = 0;
    for (int k = 0; k < SERVICES; k++)
        if (in->services[k] && tell)
            tell_others(inst, XTYP_UNREGISTER, in->services[k]);
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

static void wr16le(uint8_t *b, uint16_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static void wr32le(uint8_t *b, uint32_t v) { wr16le(b, (uint16_t)v); wr16le(b + 2, (uint16_t)(v >> 16)); }

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
    case 4:                             /* DdeConnectList(inst, svc, topic, list, LPCONVCONTEXT) */
        *v = DdeConnectList(NE->arg32(c, 20), NE->arg32(c, 16), NE->arg32(c, 12),
                            NE->arg32(c, 8), NULL);
        return WINDLL_DWORD;
    case 5:                             /* DdeQueryNextServer(list, prev) */
        *v = DdeQueryNextServer(NE->arg32(c, 8), NE->arg32(c, 4));
        return WINDLL_DWORD;
    case 6:                             /* DdeDisconnectList(list) */
        *v = DdeDisconnectList(NE->arg32(c, 4));
        return WINDLL_WORD;
    case 7:                             /* DdeConnect(inst, svc, topic, LPCONVCONTEXT) */
        *v = DdeConnect(NE->arg32(c, 16), NE->arg32(c, 12), NE->arg32(c, 8), NULL);
        return WINDLL_DWORD;
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
        wr16le(b + 42, 16);             /* its CONVCONTEXT's size */
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
                          (type & XTYP_MASK) == (XTYP_EXECUTE & XTYP_MASK)))
            given = guest_data(c, convs[ci].inst, data, cb, 0, item, fmt, 0);
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
        if (max > 0x10000u - (uint16_t)dst)
            max = 0x10000u - (uint16_t)dst;
        if (off < d->size) {
            DWORD n = d->size - off < max ? d->size - off : max;
            if (NE->write(c, dst, d->bytes + off, n))
                *v = n;
        }
        return WINDLL_DWORD;
    }
    case 17:                            /* DdeAccessData: no memory of the program's for it */
        *v = 0;
        return WINDLL_DWORD;
    case 18:                            /* DdeUnaccessData(HDDEDATA) */
        *v = DdeUnaccessData(NE->arg32(c, 4));
        return WINDLL_WORD;
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
