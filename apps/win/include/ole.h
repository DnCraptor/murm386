/*
 * ole.h - OLE 1.0's client library (OLECLI.DLL, a native DLL of WINN's:
 * apps/windll/olecli) for native callers, who take its functions with
 * LoadLibrary("OLECLI") and GetProcAddress. WINN has no OLE servers (no
 * registration database): a client's documents are registered, no object
 * is ever made.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_OLE_H
#define WINN_OLE_H

#include "windows.h"

typedef UINT OLESTATUS;
typedef LONG LHCLIENTDOC;
typedef WORD OLECLIPFORMAT;

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

#endif /* WINN_OLE_H */
