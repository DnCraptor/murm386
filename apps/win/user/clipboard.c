/*
 * clipboard.c - the clipboard (USER) and RegisterWindowMessage.
 *
 * One set of formats, each a global memory handle that belongs to the
 * clipboard once set. A handle of 0 is delayed rendering: the owner gets
 * WM_RENDERFORMAT when someone asks for the data. CF_TEXT and CF_OEMTEXT
 * stand in for each other (the same bytes; text here is Windows-1251 and
 * no conversion is made).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "user.h"

#define CLIP_MAX 8

static struct { UINT fmt; HANDLE data; } clip[CLIP_MAX];
static int   nclip;
static HWND  clip_owner, clip_open;
static HTASK clip_task;
static int   is_open;

BOOL WINAPI OpenClipboard(HWND h)
{
    if (is_open && clip_task != GetCurrentTask())
        return FALSE;
    is_open = 1;
    clip_open = h;
    clip_task = GetCurrentTask();
    return TRUE;
}

BOOL WINAPI CloseClipboard(void)
{
    if (!is_open)
        return FALSE;
    is_open = 0;
    clip_open = 0;
    clip_task = 0;
    return TRUE;
}

BOOL WINAPI EmptyClipboard(void)
{
    if (!is_open)
        return FALSE;
    if (clip_owner && IsWindow(clip_owner))
        SendMessage(clip_owner, WM_DESTROYCLIPBOARD, 0, 0);
    for (int i = 0; i < nclip; i++)
        if (clip[i].data)
            GlobalFree(clip[i].data);
    nclip = 0;
    clip_owner = clip_open;
    return TRUE;
}

HWND WINAPI GetClipboardOwner(void)
{
    return clip_owner && IsWindow(clip_owner) ? clip_owner : 0;
}

static int find(UINT fmt)
{
    for (int i = 0; i < nclip; i++)
        if (clip[i].fmt == fmt)
            return i;
    return -1;
}

HANDLE WINAPI SetClipboardData(UINT fmt, HANDLE data)
{
    if (!is_open || !fmt)
        return 0;
    int i = find(fmt);
    if (i < 0) {
        if (nclip == CLIP_MAX)
            return 0;
        i = nclip++;
        clip[i].fmt = fmt;
    } else if (clip[i].data && clip[i].data != data) {
        GlobalFree(clip[i].data);
    }
    clip[i].data = data;
    return data;
}

HANDLE WINAPI GetClipboardData(UINT fmt)
{
    if (!is_open)
        return 0;
    int i = find(fmt);
    if (i < 0 && (fmt == CF_TEXT || fmt == CF_OEMTEXT))
        i = find(fmt == CF_TEXT ? CF_OEMTEXT : CF_TEXT);
    if (i < 0)
        return 0;
    if (!clip[i].data && clip_owner && IsWindow(clip_owner))
        SendMessage(clip_owner, WM_RENDERFORMAT, clip[i].fmt, 0);   /* fills it in */
    return clip[i].data;
}

int WINAPI CountClipboardFormats(void) { return nclip; }

UINT WINAPI EnumClipboardFormats(UINT fmt)
{
    if (!is_open)
        return 0;
    if (!fmt)
        return nclip ? clip[0].fmt : 0;
    int i = find(fmt);
    return i >= 0 && i + 1 < nclip ? clip[i + 1].fmt : 0;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT fmt)
{
    if (find(fmt) >= 0)
        return TRUE;
    return (fmt == CF_TEXT && find(CF_OEMTEXT) >= 0) || (fmt == CF_OEMTEXT && find(CF_TEXT) >= 0);
}

/* Registered formats and messages are string atoms (0xC000 and up). */
UINT WINAPI RegisterClipboardFormat(LPCSTR name) { return name && *name ? AddAtom(name) : 0; }

int WINAPI GetClipboardFormatName(UINT fmt, LPSTR buf, int size)
{
    return fmt >= 0xC000 ? (int)GetAtomName((ATOM)fmt, buf, size) : 0;
}

UINT WINAPI RegisterWindowMessage(LPCSTR name) { return name && *name ? AddAtom(name) : 0; }

/* A task ended: it can no longer hold the clipboard open. */
void user_clipboard_task_ended(HTASK task)
{
    if (is_open && clip_task == task)
        CloseClipboard();
}

/* ---------------------------------------------------------------- text helpers for controls */

/* Put n bytes of text on the clipboard for window h. */
int user_clipboard_put_text(HWND h, const char *s, int n)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (DWORD)n + 1);
    if (!g)
        return 0;
    char *p = (char *)GlobalLock(g);
    memcpy(p, s, (size_t)n);
    p[n] = 0;
    GlobalUnlock(g);
    if (!OpenClipboard(h)) {
        GlobalFree(g);
        return 0;
    }
    EmptyClipboard();
    SetClipboardData(CF_TEXT, g);
    CloseClipboard();
    return 1;
}

/* The clipboard's text, a copy the caller frees with win_free, or NULL. */
char *user_clipboard_get_text(HWND h)
{
    if (!OpenClipboard(h))
        return NULL;
    char *out = NULL;
    HANDLE g = GetClipboardData(CF_TEXT);
    const char *p = g ? (const char *)GlobalLock(g) : NULL;
    if (p) {
        DWORD max = GlobalSize(g);
        DWORD n = 0;
        while (n < max && p[n])
            n++;
        out = (char *)win_alloc(n + 1);
        if (out) {
            memcpy(out, p, n);
            out[n] = 0;
        }
        GlobalUnlock(g);
    }
    CloseClipboard();
    return out;
}
