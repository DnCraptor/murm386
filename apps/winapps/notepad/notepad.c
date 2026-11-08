/*
 * notepad.c - NOTEPAD.EXE: a plain text editor, as in Windows 3.1.
 *
 * The text is a multi-line EDIT control filling the window (up to 64 KB);
 * files are read and written with _lopen/_lread/_lwrite, chosen with the
 * common Open and Save As dialogs; Find is the common modeless dialog.
 * Lines ending in LF alone are given CR LF when a file is read. F5 puts in
 * the time and date.
 *
 *   NOTEPAD [file]
 */
#include "windows.h"
#include "commdlg.h"

#define IDM_NEW     101
#define IDM_OPEN    102
#define IDM_SAVE    103
#define IDM_SAVEAS  104
#define IDM_EXIT    105
#define IDM_UNDO    201
#define IDM_CUT     202
#define IDM_COPY    203
#define IDM_PASTE   204
#define IDM_DELETE  205
#define IDM_SELALL  206
#define IDM_WRAP    207
#define IDM_TIME    208
#define IDM_FIND    301
#define IDM_FINDNEXT 302
#define IDM_ABOUT   401

#define ID_EDIT     1
#define MAX_TEXT    65535L

static HINSTANCE hinst;
static HWND      hmain, hedit, hfind;
static char      file[128];         /* full path, "" = untitled */
static char      title[16];         /* file name shown */
static int       wrap;
static UINT      find_msg;
static FINDREPLACE fr;
static char      find_what[64];
static const char filter[] = "Text Files (*.TXT)\0*.txt\0All Files (*.*)\0*.*\0";

/* ---------------------------------------------------------------- helpers */

static void set_caption(void)
{
    char t[64];
    wsprintf(t, "Notepad - %s", file[0] ? title : "(Untitled)");
    SetWindowText(hmain, t);
}

static void error(const char *fmt, const char *arg)
{
    char msg[200];
    wsprintf(msg, fmt, arg);
    MessageBox(hmain, msg, "Notepad", MB_OK | MB_ICONEXCLAMATION);
}

static HWND make_edit(void)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL;
    if (!wrap)
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    RECT rc;
    GetClientRect(hmain, &rc);
    HWND e = CreateWindow("EDIT", "", style, 0, 0, rc.right, rc.bottom, hmain,
                          (HMENU)ID_EDIT, hinst, NULL);
    SendMessage(e, EM_LIMITTEXT, (WPARAM)MAX_TEXT, 0);
    return e;
}

/* The whole text, in a GlobalAlloc'd block (locked); len gets its size. */
static HGLOBAL text_of(char **p, int *len)
{
    *len = GetWindowTextLength(hedit);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (DWORD)*len + 1);
    if (!g)
        return 0;
    *p = (char *)GlobalLock(g);
    GetWindowText(hedit, *p, *len + 1);
    return g;
}

static void free_text(HGLOBAL g)
{
    GlobalUnlock(g);
    GlobalFree(g);
}

/* ---------------------------------------------------------------- files */

static int save_to(const char *path)
{
    char *p;
    int len;
    HGLOBAL g = text_of(&p, &len);
    if (!g) {
        error("Not enough memory to save %s.", path);
        return 0;
    }
    HFILE f = _lcreat(path, 0);
    int ok = f != HFILE_ERROR && (len == 0 || _lwrite(f, p, (UINT)len) == (UINT)len);
    if (f != HFILE_ERROR)
        _lclose(f);
    free_text(g);
    if (!ok) {
        error("Cannot write the file %s.\nThe disk may be full or write-protected.", path);
        return 0;
    }
    SendMessage(hedit, EM_SETMODIFY, FALSE, 0);
    return 1;
}

static void use_name(const char *path)
{
    lstrcpyn(file, path, sizeof(file));
    GetFileTitle(file, title, sizeof(title));
    set_caption();
}

static int save_as(void)
{
    char name[128];
    lstrcpy(name, file[0] ? title : "*.txt");
    OPENFILENAME o = { 0 };
    o.lStructSize = sizeof(o);
    o.hwndOwner = hmain;
    o.hInstance = hinst;
    o.lpstrFilter = filter;
    o.nFilterIndex = 1;
    o.lpstrFile = name;
    o.nMaxFile = sizeof(name);
    o.lpstrDefExt = "txt";
    o.Flags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileName(&o))
        return 0;
    if (!save_to(name))
        return 0;
    use_name(name);
    return 1;
}

static int save(void)
{
    if (!file[0])
        return save_as();
    return save_to(file);
}

/* 1 to go on (saved or not wanted), 0 to stay */
static int query_save(void)
{
    if (!SendMessage(hedit, EM_GETMODIFY, 0, 0))
        return 1;
    char msg[160];
    wsprintf(msg, "The text in the %s file has changed.\n\nDo you want to save the changes?",
             file[0] ? title : "(Untitled)");
    switch (MessageBox(hmain, msg, "Notepad", MB_YESNOCANCEL | MB_ICONEXCLAMATION)) {
    case IDYES: return save();
    case IDNO:  return 1;
    default:    return 0;
    }
}

static int load(const char *path)
{
    HFILE f = _lopen(path, OF_READ);
    if (f == HFILE_ERROR) {
        error("Cannot find the %s file.", path);
        return 0;
    }
    LONG size = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (size < 0 || size > MAX_TEXT) {
        _lclose(f);
        error("The %s file is too large for Notepad.", path);
        return 0;
    }
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (DWORD)size + 1);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (DWORD)size * 2 + 1);
    if (!g || !h) {
        if (g) GlobalFree(g);
        if (h) GlobalFree(h);
        _lclose(f);
        error("Not enough memory to open %s.", path);
        return 0;
    }
    char *raw = (char *)GlobalLock(g), *text = (char *)GlobalLock(h);
    LONG got = _hread(f, raw, size);
    _lclose(f);
    if (got != size) {
        free_text(g);
        free_text(h);
        error("Cannot read the %s file.", path);
        return 0;
    }
    /* LF alone -> CR LF; NUL bytes would end the text, they become spaces */
    LONG n = 0;
    for (LONG i = 0; i < size; i++) {
        char c = raw[i];
        if (c == '\n' && (i == 0 || raw[i - 1] != '\r'))
            text[n++] = '\r';
        text[n++] = c ? c : ' ';
    }
    text[n] = 0;
    free_text(g);
    if (n > MAX_TEXT) {
        free_text(h);
        error("The %s file is too large for Notepad.", path);
        return 0;
    }
    SetWindowText(hedit, text);
    free_text(h);
    SendMessage(hedit, EM_SETMODIFY, FALSE, 0);
    use_name(path);
    return 1;
}

static void open_file(void)
{
    if (!query_save())
        return;
    char name[128] = "";
    OPENFILENAME o = { 0 };
    o.lStructSize = sizeof(o);
    o.hwndOwner = hmain;
    o.hInstance = hinst;
    o.lpstrFilter = filter;
    o.nFilterIndex = 1;
    o.lpstrFile = name;
    o.nMaxFile = sizeof(name);
    o.lpstrDefExt = "txt";
    o.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileName(&o))
        load(name);
}

static void new_file(void)
{
    if (!query_save())
        return;
    SetWindowText(hedit, "");
    SendMessage(hedit, EM_SETMODIFY, FALSE, 0);
    file[0] = 0;
    set_caption();
}

static void set_wrap(int on)
{
    if (on == wrap)
        return;
    char *p;
    int len;
    HGLOBAL g = text_of(&p, &len);
    if (!g)
        return;
    int modified = (int)SendMessage(hedit, EM_GETMODIFY, 0, 0);
    DWORD sel = (DWORD)SendMessage(hedit, EM_GETSEL, 0, 0);
    wrap = on;
    HWND old = hedit;
    hedit = make_edit();
    SetWindowText(hedit, p);
    free_text(g);
    DestroyWindow(old);
    SendMessage(hedit, EM_SETMODIFY, (WPARAM)modified, 0);
    SendMessage(hedit, EM_SETSEL, 0, (LPARAM)sel);
    SetFocus(hedit);
}

/* ---------------------------------------------------------------- find */

static int is_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || (unsigned char)c >= 0xC0;
}

static char up(char c)
{
    char s[2] = { c, 0 };
    AnsiUpper(s);
    return s[0];
}

static void find_next(void)
{
    if (!find_what[0]) {
        SendMessage(hmain, WM_COMMAND, IDM_FIND, 0);
        return;
    }
    char *p;
    int len;
    HGLOBAL g = text_of(&p, &len);
    if (!g)
        return;
    DWORD sel = (DWORD)SendMessage(hedit, EM_GETSEL, 0, 0);
    int a = LOWORD(sel), b = HIWORD(sel);
    int n = lstrlen(find_what);
    int down = (fr.Flags & FR_DOWN) != 0, mcase = (fr.Flags & FR_MATCHCASE) != 0;
    int whole = (fr.Flags & FR_WHOLEWORD) != 0;
    int found = -1;
    for (int i = down ? b : a - 1; down ? i + n <= len : i >= 0; i += down ? 1 : -1) {
        if (i + n > len)
            continue;
        int k = 0;
        while (k < n && (mcase ? p[i + k] == find_what[k] : up(p[i + k]) == up(find_what[k])))
            k++;
        if (k < n)
            continue;
        if (whole && ((i > 0 && is_word_char(p[i - 1])) || (i + n < len && is_word_char(p[i + n]))))
            continue;
        found = i;
        break;
    }
    free_text(g);
    if (found < 0) {
        error("Cannot find \"%s\"", find_what);
        return;
    }
    SendMessage(hedit, EM_SETSEL, 0, MAKELPARAM(found, found + n));
}

/* ---------------------------------------------------------------- window */

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == find_msg && find_msg) {
        FINDREPLACE *f = (FINDREPLACE *)(intptr_t)lp;
        if (f->Flags & FR_DIALOGTERM)
            hfind = 0;
        else if (f->Flags & FR_FINDNEXT)
            find_next();
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        hmain = h;
        hedit = make_edit();
        return hedit ? 0 : -1;
    case WM_SIZE:
        MoveWindow(hedit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SETFOCUS:
        SetFocus(hedit);
        return 0;
    case WM_INITMENUPOPUP: {
        HMENU m = (HMENU)wp;
        DWORD sel = (DWORD)SendMessage(hedit, EM_GETSEL, 0, 0);
        UINT has_sel = LOWORD(sel) != HIWORD(sel) ? MF_ENABLED : MF_GRAYED;
        EnableMenuItem(m, IDM_UNDO, SendMessage(hedit, EM_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_CUT, has_sel);
        EnableMenuItem(m, IDM_COPY, has_sel);
        EnableMenuItem(m, IDM_DELETE, has_sel);
        EnableMenuItem(m, IDM_PASTE, IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_GRAYED);
        CheckMenuItem(m, IDM_WRAP, wrap ? MF_CHECKED : MF_UNCHECKED);
        return 0;
    }
    case WM_COMMAND:
        if (wp == ID_EDIT) {
            if (HIWORD(lp) == EN_MAXTEXT)
                error("%s", "This file is too large for Notepad.");
            return 0;
        }
        switch (wp) {
        case IDM_NEW:    new_file(); break;
        case IDM_OPEN:   open_file(); break;
        case IDM_SAVE:   save(); break;
        case IDM_SAVEAS: save_as(); break;
        case IDM_EXIT:   SendMessage(h, WM_CLOSE, 0, 0); break;
        case IDM_UNDO:   SendMessage(hedit, WM_UNDO, 0, 0); break;
        case IDM_CUT:    SendMessage(hedit, WM_CUT, 0, 0); break;
        case IDM_COPY:   SendMessage(hedit, WM_COPY, 0, 0); break;
        case IDM_PASTE:  SendMessage(hedit, WM_PASTE, 0, 0); break;
        case IDM_DELETE: SendMessage(hedit, WM_CLEAR, 0, 0); break;
        case IDM_SELALL: SendMessage(hedit, EM_SETSEL, 0, MAKELPARAM(0, -1)); break;
        case IDM_WRAP:   set_wrap(!wrap); break;
        case IDM_TIME: {
            SYSTEMTIME_N t;
            GetLocalTimeN(&t);
            char s[40];
            int h = t.wHour % 12;
            wsprintf(s, "%d:%02d %s %d/%d/%d", h ? h : 12, t.wMinute, t.wHour < 12 ? "AM" : "PM",
                     t.wMonth, t.wDay, t.wYear);
            SendMessage(hedit, EM_REPLACESEL, 0, (LPARAM)(intptr_t)s);
            break;
        }
        case IDM_FIND:
            if (hfind) {
                SetFocus(hfind);
                break;
            }
            fr.lStructSize = sizeof(fr);
            fr.hwndOwner = h;
            fr.hInstance = hinst;
            fr.lpstrFindWhat = find_what;
            fr.wFindWhatLen = sizeof(find_what);
            if (!(fr.Flags & FR_DOWN) && !find_what[0])
                fr.Flags |= FR_DOWN;
            hfind = FindText(&fr);
            break;
        case IDM_FINDNEXT: find_next(); break;
        case IDM_ABOUT:
            MessageBox(h, "Notepad\nNative Windows 3.x API for murm386.", "About Notepad",
                       MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    case WM_CLOSE:
        if (query_save())
            DestroyWindow(h);
        return 0;
    case WM_QUERYENDSESSION:
        return query_save();
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    hinst = inst;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = MainProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "Notepad";
        if (!RegisterClass(&wc))
            return 1;
    }
    find_msg = RegisterWindowMessage(FINDMSGSTRING);
    fr.Flags = FR_DOWN;

    HMENU mfile = CreatePopupMenu();
    AppendMenu(mfile, MF_STRING, IDM_NEW, "&New");
    AppendMenu(mfile, MF_STRING, IDM_OPEN, "&Open...");
    AppendMenu(mfile, MF_STRING, IDM_SAVE, "&Save");
    AppendMenu(mfile, MF_STRING, IDM_SAVEAS, "Save &As...");
    AppendMenu(mfile, MF_SEPARATOR, 0, NULL);
    AppendMenu(mfile, MF_STRING, IDM_EXIT, "E&xit");
    HMENU medit = CreatePopupMenu();
    AppendMenu(medit, MF_STRING, IDM_UNDO, "&Undo\tCtrl+Z");
    AppendMenu(medit, MF_SEPARATOR, 0, NULL);
    AppendMenu(medit, MF_STRING, IDM_CUT, "Cu&t\tCtrl+X");
    AppendMenu(medit, MF_STRING, IDM_COPY, "&Copy\tCtrl+C");
    AppendMenu(medit, MF_STRING, IDM_PASTE, "&Paste\tCtrl+V");
    AppendMenu(medit, MF_STRING, IDM_DELETE, "De&lete\tDel");
    AppendMenu(medit, MF_SEPARATOR, 0, NULL);
    AppendMenu(medit, MF_STRING, IDM_SELALL, "Select &All");
    AppendMenu(medit, MF_STRING, IDM_TIME, "Time/&Date\tF5");
    AppendMenu(medit, MF_SEPARATOR, 0, NULL);
    AppendMenu(medit, MF_STRING, IDM_WRAP, "&Word Wrap");
    HMENU msearch = CreatePopupMenu();
    AppendMenu(msearch, MF_STRING, IDM_FIND, "&Find...");
    AppendMenu(msearch, MF_STRING, IDM_FINDNEXT, "Find &Next\tF3");
    HMENU mhelp = CreatePopupMenu();
    AppendMenu(mhelp, MF_STRING, IDM_ABOUT, "&About Notepad...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)mfile, "&File");
    AppendMenu(bar, MF_POPUP, (UINT)medit, "&Edit");
    AppendMenu(bar, MF_POPUP, (UINT)msearch, "&Search");
    AppendMenu(bar, MF_POPUP, (UINT)mhelp, "&Help");

    static const ACCEL_N keys[] = {
        { FVIRTKEY, VK_F3, IDM_FINDNEXT },
        { FVIRTKEY, VK_F5, IDM_TIME },
    };
    HACCEL acc = CreateAcceleratorTableN(keys, 2);

    HWND h = CreateWindow("Notepad", "Notepad", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                          CW_USEDEFAULT, CW_USEDEFAULT, 0, bar, inst, NULL);
    if (!h)
        return 1;
    set_caption();
    ShowWindow(h, show);
    UpdateWindow(h);
    /* NOTEPAD file */
    while (*cmd == ' ')
        cmd++;
    if (*cmd) {
        char path[128];
        lstrcpyn(path, cmd, sizeof(path));
        int n = lstrlen(path);
        while (n && path[n - 1] == ' ')
            path[--n] = 0;
        if (!load(path))
            set_caption();
    }

    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        if (hfind && IsDialogMessage(hfind, &m))
            continue;
        if (TranslateAccelerator(h, acc, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    DestroyAcceleratorTableN(acc);
    return (int)m.wParam;
}
