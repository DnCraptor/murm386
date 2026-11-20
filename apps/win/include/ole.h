/*
 * ole.h - OLE 1.0's client library (OLECLI.DLL, a native DLL of WINN's:
 * apps/windll/olecli) for native callers, who take its functions with
 * LoadLibrary("OLECLI") and GetProcAddress. WINN has no OLE servers (no
 * registration database): client documents can hold static pictures and saved linked/embedded objects.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_OLE_H
#define WINN_OLE_H

#include "windows.h"

typedef UINT OLESTATUS;
typedef LONG LHCLIENTDOC;
typedef WORD OLECLIPFORMAT;

typedef struct WINN_OLEOBJECT OLEOBJECT, *LPOLEOBJECT;
typedef struct WINN_OLECLIENT OLECLIENT, *LPOLECLIENT;

typedef struct WINN_OLESTREAM OLESTREAM, *LPOLESTREAM;
typedef struct {
    DWORD (CALLBACK *Get)(LPOLESTREAM stream, void *data, DWORD size);
    DWORD (CALLBACK *Put)(LPOLESTREAM stream, const void *data, DWORD size);
} OLESTREAMVTBL;
struct WINN_OLESTREAM {
    const OLESTREAMVTBL *lpstbl;
};

#define OT_LINK 1
#define OT_EMBEDDED 2
#define OT_STATIC 3

#define OLE_OK                  0
#define OLE_WAIT_FOR_RELEASE    1
#define OLE_BUSY                2
#define OLE_ERROR_PROTECT_ONLY  3
#define OLE_ERROR_MEMORY        4
#define OLE_ERROR_STREAM        5
#define OLE_ERROR_STATIC        6
#define OLE_ERROR_BLANK         7
#define OLE_ERROR_DRAW          8
#define OLE_ERROR_METAFILE      9
#define OLE_ERROR_ABORT         10
#define OLE_ERROR_CLIPBOARD     11
#define OLE_ERROR_FORMAT        12
#define OLE_ERROR_OBJECT        13
#define OLE_ERROR_OPTION        14
#define OLE_ERROR_PROTOCOL      15
#define OLE_ERROR_ADDRESS       16
#define OLE_ERROR_NOT_EQUAL     17
#define OLE_ERROR_HANDLE        18
#define OLE_ERROR_GENERIC       19
#define OLE_ERROR_CLASS         20
#define OLE_ERROR_SYNTAX        21
#define OLE_ERROR_DATATYPE      22
#define OLE_ERROR_PALETTE       23
#define OLE_ERROR_NOT_LINK      24
#define OLE_ERROR_NOT_EMPTY     25
#define OLE_ERROR_SIZE          26
#define OLE_ERROR_DRIVE         27
#define OLE_ERROR_NETWORK       28
#define OLE_ERROR_NAME          29
#define OLE_ERROR_TEMPLATE      30
#define OLE_ERROR_NEW           31
#define OLE_ERROR_EDIT          32
#define OLE_ERROR_OPEN          33
#define OLE_ERROR_NOT_OPEN      34
#define OLE_ERROR_LAUNCH        35
#define OLE_ERROR_COMM          36
#define OLE_ERROR_TERMINATE     37
#define OLE_ERROR_COMMAND       38
#define OLE_ERROR_SHOW          39
#define OLE_ERROR_DOVERB        40

/* the render options of OleCreate and the rest */
#define olerender_none          0
#define olerender_draw          1
#define olerender_format        2

typedef enum {
    oleupdate_always,
    oleupdate_onsave,
    oleupdate_oncall
} OLEOPT_UPDATE;

/* the functions' types, for GetProcAddress (their names: Windows') */
typedef OLESTATUS (WINAPI *OLEREGISTERCLIENTDOC)(LPCSTR cls, LPCSTR doc, LONG reserved,
                                                 LHCLIENTDOC *out);
typedef OLESTATUS (WINAPI *OLEREVOKECLIENTDOC)(LHCLIENTDOC doc);
typedef OLESTATUS (WINAPI *OLERENAMECLIENTDOC)(LHCLIENTDOC doc, LPCSTR name);
typedef OLESTATUS (WINAPI *OLEREVERTCLIENTDOC)(LHCLIENTDOC doc);
typedef OLESTATUS (WINAPI *OLESAVEDCLIENTDOC)(LHCLIENTDOC doc);
typedef OLESTATUS (WINAPI *OLEQUERYCREATEFROMCLIP)(LPCSTR protocol, UINT render,
                                                   OLECLIPFORMAT format);
typedef OLESTATUS (WINAPI *OLEQUERYLINKFROMCLIP)(LPCSTR protocol, UINT render,
                                                 OLECLIPFORMAT format);
typedef DWORD (WINAPI *OLEQUERYCLIENTVERSION)(void);
typedef BOOL (WINAPI *OLEISDCMETA)(HDC dc);

typedef OLESTATUS (WINAPI *OLECREATEFROMCLIP)(LPCSTR protocol, LPOLECLIENT client,
    LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *object, UINT render, OLECLIPFORMAT format);
typedef OLECREATEFROMCLIP OLECREATELINKFROMCLIP;
typedef OLESTATUS (WINAPI *OLECREATE)(LPCSTR protocol, LPOLECLIENT client, LPCSTR cls,
    LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *object, UINT render, OLECLIPFORMAT format);
typedef OLESTATUS (WINAPI *OLEDELETE)(LPOLEOBJECT object);
typedef OLESTATUS (WINAPI *OLECLONE)(LPOLEOBJECT object, LPOLECLIENT client,
    LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *copy);
typedef OLESTATUS (WINAPI *OLEEQUAL)(LPOLEOBJECT first, LPOLEOBJECT second);
typedef OLESTATUS (WINAPI *OLEDRAW)(LPOLEOBJECT object, HDC dc, const RECT *bounds,
    const RECT *clip, HDC target);
typedef OLESTATUS (WINAPI *OLECOPYTOCLIPBOARD)(LPOLEOBJECT object);
typedef OLESTATUS (WINAPI *OLEQUERYTYPE)(LPOLEOBJECT object, LONG *type);
typedef OLESTATUS (WINAPI *OLEQUERYBOUNDS)(LPOLEOBJECT object, RECT *bounds);
typedef OLECLIPFORMAT (WINAPI *OLEENUMFORMATS)(LPOLEOBJECT object, OLECLIPFORMAT previous);
typedef OLESTATUS (WINAPI *OLEGETDATA)(LPOLEOBJECT object, OLECLIPFORMAT format, HANDLE *data);
typedef OLESTATUS (WINAPI *OLESETDATA)(LPOLEOBJECT object, OLECLIPFORMAT format, HANDLE data);
typedef OLESTATUS (WINAPI *OLEGETLINKUPDATEOPTIONS)(LPOLEOBJECT object, OLEOPT_UPDATE *option);
typedef OLESTATUS (WINAPI *OLESETLINKUPDATEOPTIONS)(LPOLEOBJECT object, OLEOPT_UPDATE option);
typedef OLESTATUS (WINAPI *OLEOBJECTCONVERT)(LPOLEOBJECT object, LPCSTR protocol,
    LPOLECLIENT client, LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *converted);
typedef OLESTATUS (WINAPI *OLEENUMOBJECTS)(LHCLIENTDOC doc, LPOLEOBJECT *object);
typedef OLESTATUS (WINAPI *OLERENAME)(LPOLEOBJECT object, LPCSTR name);
typedef OLESTATUS (WINAPI *OLEQUERYNAME)(LPOLEOBJECT object, LPSTR name, UINT *count);
typedef OLESTATUS (WINAPI *OLEQUERYSIZE)(LPOLEOBJECT object, DWORD *size);
typedef OLESTATUS (WINAPI *OLESAVETOSTREAM)(LPOLEOBJECT object, LPOLESTREAM stream);
typedef OLESTATUS (WINAPI *OLELOADFROMSTREAM)(LPOLESTREAM stream, LPCSTR protocol,
    LPOLECLIENT client, LHCLIENTDOC doc, LPCSTR name, LPOLEOBJECT *object);

/* OLESVR.DLL, for native servers. These tables live in server-owned
 * memory. A Win16 server supplies the original FAR PASCAL tables. */
typedef LONG LHSERVER;
typedef LONG LHSERVERDOC;
typedef struct WINN_OLESERVER OLESERVER, *LPOLESERVER;
typedef struct WINN_OLESERVERDOC OLESERVERDOC, *LPOLESERVERDOC;

typedef enum {
    OLE_SERVER_MULTI,
    OLE_SERVER_SINGLE
} OLE_SERVER_USE;

typedef struct {
    OLESTATUS (CALLBACK *Open)(LPOLESERVER, LHSERVERDOC, LPCSTR, LPOLESERVERDOC *);
    OLESTATUS (CALLBACK *Create)(LPOLESERVER, LHSERVERDOC, LPCSTR, LPCSTR, LPOLESERVERDOC *);
    OLESTATUS (CALLBACK *CreateFromTemplate)(LPOLESERVER, LHSERVERDOC, LPCSTR, LPCSTR,
                                              LPCSTR, LPOLESERVERDOC *);
    OLESTATUS (CALLBACK *Edit)(LPOLESERVER, LHSERVERDOC, LPCSTR, LPCSTR, LPOLESERVERDOC *);
    OLESTATUS (CALLBACK *Exit)(LPOLESERVER);
    OLESTATUS (CALLBACK *Release)(LPOLESERVER);
    OLESTATUS (CALLBACK *Execute)(LPOLESERVER, HGLOBAL);
} OLESERVERVTBL;

typedef struct {
    OLESTATUS (CALLBACK *Save)(LPOLESERVERDOC);
    OLESTATUS (CALLBACK *Close)(LPOLESERVERDOC);
    OLESTATUS (CALLBACK *SetHostNames)(LPOLESERVERDOC, LPCSTR, LPCSTR);
    OLESTATUS (CALLBACK *SetDocDimensions)(LPOLESERVERDOC, const RECT *);
    OLESTATUS (CALLBACK *GetObject)(LPOLESERVERDOC, LPCSTR, LPOLEOBJECT *, LPOLECLIENT);
    OLESTATUS (CALLBACK *Release)(LPOLESERVERDOC);
    OLESTATUS (CALLBACK *SetColorScheme)(LPOLESERVERDOC, const LOGPALETTE *);
    OLESTATUS (CALLBACK *Execute)(LPOLESERVERDOC, HGLOBAL);
} OLESERVERDOCVTBL;

struct WINN_OLESERVER { const OLESERVERVTBL *lpvtbl; };
struct WINN_OLESERVERDOC { const OLESERVERDOCVTBL *lpvtbl; };

/* Server-owned objects returned by GetObject start with this vtable.
 * Client objects obtained from OLECLI remain opaque to native callers. */
typedef struct {
    int (CALLBACK *CallBack)(LPOLECLIENT, UINT, LPOLEOBJECT);
} OLECLIENTVTBL;

struct WINN_OLECLIENT { const OLECLIENTVTBL *lpvtbl; };

typedef struct {
    LPVOID (CALLBACK *QueryProtocol)(LPOLEOBJECT, LPCSTR);
    OLESTATUS (CALLBACK *Release)(LPOLEOBJECT);
    OLESTATUS (CALLBACK *Show)(LPOLEOBJECT, BOOL);
    OLESTATUS (CALLBACK *DoVerb)(LPOLEOBJECT, UINT, BOOL, BOOL);
    OLESTATUS (CALLBACK *GetData)(LPOLEOBJECT, OLECLIPFORMAT, HANDLE *);
    /* The server owns the supplied global block once called. */
    OLESTATUS (CALLBACK *SetData)(LPOLEOBJECT, OLECLIPFORMAT, HANDLE);
} OLESERVEROBJECTVTBL;

typedef struct { const OLESERVEROBJECTVTBL *lpvtbl; } OLESERVEROBJECT;
typedef OLESTATUS (WINAPI *OLEREVOKEOBJECT)(LPOLECLIENT);

typedef OLESTATUS (WINAPI *OLEREGISTERSERVER)(LPCSTR, LPOLESERVER, LHSERVER *, HINSTANCE,
                                              OLE_SERVER_USE);
typedef OLESTATUS (WINAPI *OLEREVOKESERVER)(LHSERVER);
typedef OLESTATUS (WINAPI *OLEREGISTERSERVERDOC)(LHSERVER, LPCSTR, LPOLESERVERDOC, LHSERVERDOC *);
typedef OLESTATUS (WINAPI *OLEREVOKESERVERDOC)(LHSERVERDOC);
typedef OLESTATUS (WINAPI *OLERENAMESERVERDOC)(LHSERVERDOC, LPCSTR);
typedef DWORD (WINAPI *OLEQUERYSERVERVERSION)(void);

#endif /* WINN_OLE_H */
