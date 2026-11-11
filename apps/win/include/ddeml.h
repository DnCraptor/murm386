/*
 * ddeml.h - the DDE Management Library (DDEML.DLL, a native DLL of WINN's:
 * apps/windll/ddeml) for native callers, who take its functions with
 * LoadLibrary("DDEML") and GetProcAddress, as Windows 3.1's own.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_DDEML_H
#define WINN_DDEML_H

#include "windows.h"

typedef DWORD HCONVLIST;
typedef DWORD HCONV;
typedef DWORD HSZ;
typedef DWORD HDDEDATA;

#define CP_WINANSI      1004

#define XTYPF_NOBLOCK   0x0002
#define XTYPF_NODATA    0x0004
#define XTYPF_ACKREQ    0x0008

#define XCLASS_MASK         0xFC00
#define XCLASS_BOOL         0x1000
#define XCLASS_DATA         0x2000
#define XCLASS_FLAGS        0x4000
#define XCLASS_NOTIFICATION 0x8000

#define XTYP_ERROR          (0x00 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_ADVDATA        (0x10 | XCLASS_FLAGS)
#define XTYP_ADVREQ         (0x20 | XCLASS_DATA | XTYPF_NOBLOCK)
#define XTYP_ADVSTART       (0x30 | XCLASS_BOOL)
#define XTYP_ADVSTOP        (0x40 | XCLASS_NOTIFICATION)
#define XTYP_EXECUTE        (0x50 | XCLASS_FLAGS)
#define XTYP_CONNECT        (0x60 | XCLASS_BOOL | XTYPF_NOBLOCK)
#define XTYP_CONNECT_CONFIRM (0x70 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_XACT_COMPLETE  (0x80 | XCLASS_NOTIFICATION)
#define XTYP_POKE           (0x90 | XCLASS_FLAGS)
#define XTYP_REGISTER       (0xA0 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_REQUEST        (0xB0 | XCLASS_DATA)
#define XTYP_DISCONNECT     (0xC0 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_UNREGISTER     (0xD0 | XCLASS_NOTIFICATION | XTYPF_NOBLOCK)
#define XTYP_WILDCONNECT    (0xE0 | XCLASS_DATA | XTYPF_NOBLOCK)
#define XTYP_MASK           0xF0

#define XST_NULL            0
#define XST_CONNECTED       2

#define TIMEOUT_ASYNC   0xFFFFFFFFul
#define QID_SYNC        0xFFFFFFFFul

#define ST_CONNECTED    0x0001
#define ST_ADVISE       0x0002
#define ST_ISLOCAL      0x0004
#define ST_BLOCKED      0x0008
#define ST_CLIENT       0x0010
#define ST_TERMINATED   0x0020
#define ST_INLIST       0x0040
#define ST_BLOCKNEXT    0x0080
#define ST_ISSELF       0x0100

#define DMLERR_NO_ERROR             0x0000
#define DMLERR_ADVACKTIMEOUT        0x4000
#define DMLERR_BUSY                 0x4001
#define DMLERR_DATAACKTIMEOUT       0x4002
#define DMLERR_DLL_NOT_INITIALIZED  0x4003
#define DMLERR_DLL_USAGE            0x4004
#define DMLERR_EXECACKTIMEOUT       0x4005
#define DMLERR_INVALIDPARAMETER     0x4006
#define DMLERR_LOW_MEMORY           0x4007
#define DMLERR_MEMORY_ERROR         0x4008
#define DMLERR_NOTPROCESSED         0x4009
#define DMLERR_NO_CONV_ESTABLISHED  0x400A
#define DMLERR_POKEACKTIMEOUT       0x400B
#define DMLERR_POSTMSG_FAILED       0x400C
#define DMLERR_REENTRANCY           0x400D
#define DMLERR_SERVER_DIED          0x400E
#define DMLERR_SYS_ERROR            0x400F
#define DMLERR_UNADVACKTIMEOUT      0x4010
#define DMLERR_UNFOUND_QUEUE_ID     0x4011

#define DDE_FACK            0x8000
#define DDE_FBUSY           0x4000
#define DDE_FNOTPROCESSED   0x0000

#define CBR_BLOCK   ((HDDEDATA)0xFFFFFFFFul)

#define CBF_FAIL_SELFCONNECTIONS    0x00001000ul
#define CBF_FAIL_CONNECTIONS        0x00002000ul
#define CBF_FAIL_ADVISES            0x00004000ul
#define CBF_FAIL_EXECUTES           0x00008000ul
#define CBF_FAIL_POKES              0x00010000ul
#define CBF_FAIL_REQUESTS           0x00020000ul
#define CBF_FAIL_ALLSVRXACTIONS     0x0003F000ul
#define CBF_SKIP_CONNECT_CONFIRMS   0x00040000ul
#define CBF_SKIP_REGISTRATIONS      0x00080000ul
#define CBF_SKIP_UNREGISTRATIONS    0x00100000ul
#define CBF_SKIP_DISCONNECTS        0x00200000ul
#define CBF_SKIP_ALLNOTIFICATIONS   0x003C0000ul

#define APPCMD_CLIENTONLY   0x00000010ul
#define APPCMD_FILTERINITS  0x00000020ul
#define APPCLASS_MONITOR    0x00000001ul

#define EC_ENABLEALL    0
#define EC_ENABLEONE    ST_BLOCKNEXT
#define EC_DISABLE      ST_BLOCKED
#define EC_QUERYWAITING 2

#define DNS_REGISTER    1
#define DNS_UNREGISTER  2
#define DNS_FILTERON    4
#define DNS_FILTEROFF   8

#define HDATA_APPOWNED  1

#define SZDDESYS_TOPIC          "System"

typedef HDDEDATA (CALLBACK *PFNCALLBACK)(UINT type, UINT fmt, HCONV conv, HSZ hsz1, HSZ hsz2,
                                         HDDEDATA data, DWORD data1, DWORD data2);

typedef struct tagCONVCONTEXT {
    UINT cb, wFlags, wCountryID;
    int iCodePage;
    DWORD dwLangID, dwSecurity;
} CONVCONTEXT, *PCONVCONTEXT;

typedef struct tagCONVINFO {
    DWORD cb, hUser;
    HCONV hConvPartner;
    HSZ hszSvcPartner, hszServiceReq, hszTopic, hszItem;
    UINT wFmt, wType, wStatus, wConvst, wLastError;
    HCONVLIST hConvList;
    CONVCONTEXT ConvCtxt;
} CONVINFO, *PCONVINFO;

/* the functions' types, for GetProcAddress (their names: Windows') */
typedef UINT (WINAPI *DDEINITIALIZE)(LPDWORD inst, PFNCALLBACK cb, DWORD cmd, DWORD res);
typedef BOOL (WINAPI *DDEUNINITIALIZE)(DWORD inst);
typedef HCONV (WINAPI *DDECONNECT)(DWORD inst, HSZ service, HSZ topic, PCONVCONTEXT cc);
typedef BOOL (WINAPI *DDEDISCONNECT)(HCONV conv);
typedef HDDEDATA (WINAPI *DDECLIENTTRANSACTION)(LPBYTE data, DWORD cb, HCONV conv, HSZ item,
                                               UINT fmt, UINT type, DWORD timeout,
                                               LPDWORD result);
typedef BOOL (WINAPI *DDEPOSTADVISE)(DWORD inst, HSZ topic, HSZ item);
typedef HDDEDATA (WINAPI *DDECREATEDATAHANDLE)(DWORD inst, LPBYTE src, DWORD cb, DWORD off,
                                              HSZ item, UINT fmt, UINT cmd);
typedef DWORD (WINAPI *DDEGETDATA)(HDDEDATA data, LPBYTE dst, DWORD max, DWORD off);
typedef BOOL (WINAPI *DDEFREEDATAHANDLE)(HDDEDATA data);
typedef UINT (WINAPI *DDEGETLASTERROR)(DWORD inst);
typedef HSZ (WINAPI *DDECREATESTRINGHANDLE)(DWORD inst, LPCSTR s, int cp);
typedef BOOL (WINAPI *DDEFREESTRINGHANDLE)(DWORD inst, HSZ hsz);
typedef DWORD (WINAPI *DDEQUERYSTRING)(DWORD inst, HSZ hsz, LPSTR out, DWORD max, int cp);
typedef HDDEDATA (WINAPI *DDENAMESERVICE)(DWORD inst, HSZ hsz1, HSZ hsz2, UINT cmd);

#endif /* WINN_DDEML_H */
