/*
 * winfile.c - WINFILE.EXE: the File Manager.
 *
 * An MDI frame of directory windows, as in Windows 3.1: each has the drive
 * bar on top, the directory tree on the left and the contents of the
 * selected directory on the right, with the status bar of the frame below.
 *
 *   Enter, double click   open: a directory, a program, a document (through
 *                         WIN.INI [Extensions]: "txt=notepad.exe ^.txt")
 *   F7 F8 Del             move, copy, delete (directories with everything in
 *                         them); File menu: rename, create directory, run
 *   Tree                  + - * expand and collapse, Left goes to the parent
 *   Backspace             the parent directory
 *   Tab                   tree, contents, drive bar in turn
 *   Ctrl+letter           another drive
 *   Shift, Ctrl + click   select several files
 *
 * The settings (view, sort, status bar) live in WINFILE.INI.
 */
#include <string.h>
#include "windows.h"

#define INI       "WINFILE.INI"
#define MAXPATH   128
#define MAXNODES  256
#define MAXENTS   512
#define MAXWIN    8

#define IDM_OPEN      100
#define IDM_MOVE      101
#define IDM_COPY      102
#define IDM_DELETE    103
#define IDM_RENAME    104
#define IDM_MKDIR     105
#define IDM_RUN       106
#define IDM_EXIT      107
#define IDM_DRIVE     110
#define IDM_EXPONE    120
#define IDM_EXPBRANCH 121
#define IDM_EXPALL    122
#define IDM_COLLAPSE  123
#define IDM_TREEDIR   130
#define IDM_TREEONLY  131
#define IDM_DIRONLY   132
#define IDM_NAME      133
#define IDM_DETAILS   134
#define IDM_BYNAME    135
#define IDM_BYTYPE    136
#define IDM_BYSIZE    137
#define IDM_BYDATE    138
#define IDM_STATUS    140
#define IDM_NEWWIN    150
#define IDM_CASCADE   151
#define IDM_TILE      152
#define IDM_ARRANGE   153
#define IDM_REFRESH   154
#define IDM_ABOUT     160
#define IDM_FIRSTCHILD 200

#define ATTR_ALL  0x16              /* directories, hidden and system files */

typedef struct {
    char name[13];
    BYTE level;
    BYTE expanded;
} tnode;

typedef struct {
    HWND   hwnd, tree, files, focus;
    int    drive;                   /* 0 = A: */
    char   path[MAXPATH];           /* "C:\\", "C:\\WINN" */
    tnode *nodes;
    int    nnodes, tsel, ttop;
    WINFIND_N *ents;                /* ents[0] is ".." below the root */
    BYTE  *sel;
    int    nents, fcaret, fanchor, ftop;
    int    up;                      /* ents[0] is ".." */
} fmwin;

static HINSTANCE hinst;
static HWND      hframe, hclient;
static fmwin    *wins[MAXWIN];
static fmwin    *creating;
static int       line_h, char_w, drive_h;
static int       view_details, sort_by = IDM_BYNAME, show_status = 1, panes = IDM_TREEDIR;
static char      start_path[MAXPATH];
static BYTE      drive_types[26];   /* GetDriveType, read at start and on Refresh */

static void read_drives(void)
{
    for (int d = 0; d < 26; d++)
        drive_types[d] = (BYTE)GetDriveType(d);
}

static UINT drive_type(int d) { return d >= 0 && d < 26 ? drive_types[d] : 0; }

#define STATUS_H (line_h + 6)
#define ROW_H    (line_h + 2)

/* ---------------------------------------------------------------- strings and paths */

static int is_root(const char *p) { return p[0] && p[1] == ':' && p[2] == '\\' && !p[3]; }

static void join(char *out, const char *dir, const char *name)
{
    lstrcpyn(out, dir, MAXPATH);
    int n = lstrlen(out);
    if (n && out[n - 1] != '\\' && n < MAXPATH - 1) {
        out[n++] = '\\';
        out[n] = 0;
    }
    lstrcpyn(out + n, name, MAXPATH - n);
}

static void parent_of(char *p)
{
    if (is_root(p))
        return;
    int cut = -1;
    for (int i = 0; p[i]; i++)
        if (p[i] == '\\')
            cut = i;
    if (cut <= 2)
        p[3] = 0;
    else
        p[cut] = 0;
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++)
        if (*p == '\\' || *p == ':')
            b = p + 1;
    return b;
}

static const char *ext_of(const char *name)
{
    const char *dot = NULL;
    for (; *name; name++)
        if (*name == '.')
            dot = name;
    return dot ? dot + 1 : "";
}

/* "to" made absolute against dir, with "." and ".." resolved */
static void absolute(char *out, const char *dir, const char *to)
{
    char tmp[MAXPATH * 2];
    if (to[0] && to[1] == ':') {
        if (to[2] == '\\')
            lstrcpyn(tmp, to, sizeof(tmp));
        else
            wsprintf(tmp, "%c:\\%s", to[0], to + 2);
    } else if (to[0] == '\\') {
        wsprintf(tmp, "%c:%s", dir[0], to);
    } else {
        join(tmp, dir, to);
    }
    AnsiUpper(tmp);
    /* rebuild component by component */
    char res[MAXPATH];
    res[0] = tmp[0];
    res[1] = ':';
    res[2] = '\\';
    res[3] = 0;
    const char *p = tmp + 3;
    while (*p) {
        char comp[16];
        int n = 0;
        while (*p && *p != '\\') {
            if (n < 15)
                comp[n++] = *p;
            p++;
        }
        comp[n] = 0;
        if (*p == '\\')
            p++;
        if (!n || !lstrcmp(comp, "."))
            continue;
        if (!lstrcmp(comp, "..")) {
            parent_of(res);
            continue;
        }
        char next[MAXPATH];
        join(next, res, comp);
        lstrcpy(res, next);
    }
    lstrcpyn(out, res, MAXPATH);
}

static int get_entry(const char *path, WINFIND_N *f)
{
    if (is_root(path)) {
        memset(f, 0, sizeof(*f));
        f->attr = 0x10;
        return 1;
    }
    return WinFindFilesN(path, ATTR_ALL, f, 1) == 1;
}

static int is_dir(const char *path)
{
    WINFIND_N f;
    return get_entry(path, &f) && (f.attr & 0x10);
}

static void commas(char *out, DWORD v)
{
    char t[16];
    int n = 0, o = 0;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) {
        out[o++] = t[--n];
        if (n && n % 3 == 0)
            out[o++] = ',';
    }
    out[o] = 0;
}

static int is_program(const char *name)
{
    const char *e = ext_of(name);
    return !lstrcmpi(e, "EXE") || !lstrcmpi(e, "COM") || !lstrcmpi(e, "BAT") || !lstrcmpi(e, "PIF");
}

/* the command a document opens with, from WIN.INI [Extensions] */
static int association(const char *name, char *cmd, int size)
{
    const char *e = ext_of(name);
    if (!*e)
        return 0;
    char v[96];
    GetProfileString("Extensions", e, "", v, sizeof(v));
    if (!v[0]) {
        if (!lstrcmpi(e, "TXT") || !lstrcmpi(e, "INI"))
            lstrcpy(v, "notepad.exe ^.");
        else
            return 0;
    }
    int n = 0;
    while (v[n] && v[n] != '^')
        n++;
    while (n && v[n - 1] == ' ')
        n--;
    lstrcpyn(cmd, v, n + 1 < size ? n + 1 : size);
    return 1;
}

/* ---------------------------------------------------------------- windows */

/* zeroed memory; the handle is kept in front of the block */
static void *galloc(DWORD n)
{
    HGLOBAL g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, n + 8);
    BYTE *p = g ? (BYTE *)GlobalLock(g) : NULL;
    if (!p)
        return NULL;
    *(HGLOBAL *)p = g;
    return p + 8;
}

static void gfree(void *p)
{
    if (p) {
        HGLOBAL g = *(HGLOBAL *)((BYTE *)p - 8);
        GlobalUnlock(g);
        GlobalFree(g);
    }
}

static fmwin *find_win(HWND h)
{
    for (int i = 0; i < MAXWIN; i++)
        if (wins[i] && (wins[i]->hwnd == h || wins[i]->tree == h || wins[i]->files == h))
            return wins[i];
    return NULL;
}

static fmwin *active_win(void)
{
    HWND a = (HWND)LOWORD(SendMessage(hclient, WM_MDIGETACTIVE, 0, 0));
    return a ? find_win(a) : NULL;
}

static void update_status(void)
{
    if (show_status) {
        RECT rc;
        GetClientRect(hframe, &rc);
        rc.top = rc.bottom - STATUS_H;
        InvalidateRect(hframe, &rc, TRUE);
    }
}

static void set_title(fmwin *fw)
{
    char t[MAXPATH + 8];
    join(t, fw->path, "*.*");
    SetWindowText(fw->hwnd, t);
}

/* ---------------------------------------------------------------- the tree */

static void node_path(fmwin *fw, int i, char *out)
{
    /* walk up: the nearest node before i with a smaller level is the parent */
    int chain[32], n = 0;
    int lv = fw->nodes[i].level;
    chain[n++] = i;
    for (int k = i - 1; k >= 0 && lv > 0; k--)
        if (fw->nodes[k].level < lv) {
            lv = fw->nodes[k].level;
            if (n < 32)
                chain[n++] = k;
        }
    wsprintf(out, "%c:\\", 'A' + fw->drive);
    for (int k = n - 2; k >= 0; k--) {
        char t[MAXPATH];
        join(t, out, fw->nodes[chain[k]].name);
        lstrcpy(out, t);
    }
}

static void sort_names(WINFIND_N *e, int n, int how);

static int expand_node(fmwin *fw, int i)
{
    if (fw->nodes[i].expanded)
        return 0;
    char path[MAXPATH], spec[MAXPATH];
    node_path(fw, i, path);
    join(spec, path, "*.*");
    static WINFIND_N f[MAXNODES];
    int n = WinFindFilesN(spec, ATTR_ALL, f, MAXNODES), k = 0;
    for (int j = 0; j < n; j++)
        if (f[j].attr & 0x10)
            f[k++] = f[j];
    sort_names(f, k, IDM_BYNAME);
    if (fw->nnodes + k > MAXNODES)
        k = MAXNODES - fw->nnodes;
    for (int j = fw->nnodes - 1; j > i; j--)
        fw->nodes[j + k] = fw->nodes[j];
    for (int j = 0; j < k; j++) {
        tnode *t = &fw->nodes[i + 1 + j];
        lstrcpy(t->name, f[j].name);
        t->level = (BYTE)(fw->nodes[i].level + 1);
        t->expanded = 0;
    }
    fw->nnodes += k;
    if (fw->tsel > i)
        fw->tsel += k;
    fw->nodes[i].expanded = 1;
    return k;
}

static void collapse_node(fmwin *fw, int i)
{
    int e = i + 1;
    while (e < fw->nnodes && fw->nodes[e].level > fw->nodes[i].level)
        e++;
    int k = e - i - 1;
    for (int j = e; j < fw->nnodes; j++)
        fw->nodes[j - k] = fw->nodes[j];
    fw->nnodes -= k;
    if (fw->tsel > i && fw->tsel < e)
        fw->tsel = i;
    else if (fw->tsel >= e)
        fw->tsel -= k;
    fw->nodes[i].expanded = 0;
}

static void expand_branch(fmwin *fw, int i)
{
    int lv = fw->nodes[i].level;
    expand_node(fw, i);
    for (int j = i + 1; j < fw->nnodes && fw->nodes[j].level > lv; j++)
        expand_node(fw, j);
}

/* the tree of the drive, opened along path, its node selected */
static void build_tree(fmwin *fw, const char *path)
{
    fw->nnodes = 1;
    fw->nodes[0].name[0] = 0;
    fw->nodes[0].level = 0;
    fw->nodes[0].expanded = 0;
    fw->tsel = 0;
    fw->ttop = 0;
    expand_node(fw, 0);
    int cur = 0;
    const char *p = path + 3;
    while (*p) {
        char comp[16];
        int n = 0;
        while (*p && *p != '\\') {
            if (n < 15)
                comp[n++] = *p;
            p++;
        }
        comp[n] = 0;
        if (*p)
            p++;
        int found = -1;
        for (int j = cur + 1; j < fw->nnodes && fw->nodes[j].level > fw->nodes[cur].level; j++)
            if (fw->nodes[j].level == fw->nodes[cur].level + 1 && !lstrcmpi(fw->nodes[j].name, comp)) {
                found = j;
                break;
            }
        if (found < 0)
            break;
        cur = found;
        expand_node(fw, cur);
    }
    fw->tsel = cur;
}

/* ---------------------------------------------------------------- the contents */

static int ent_cmp(const WINFIND_N *a, const WINFIND_N *b, int how)
{
    int da = (a->attr & 0x10) != 0, db = (b->attr & 0x10) != 0;
    if (da != db)
        return db - da;             /* directories first */
    if (how == IDM_BYTYPE && !da) {
        int c = lstrcmpi(ext_of(a->name), ext_of(b->name));
        if (c)
            return c;
    } else if (how == IDM_BYSIZE && !da) {
        if (a->size != b->size)
            return a->size > b->size ? -1 : 1;
    } else if (how == IDM_BYDATE) {
        DWORD ta = ((DWORD)a->date << 16) | a->time, tb = ((DWORD)b->date << 16) | b->time;
        if (ta != tb)
            return ta > tb ? -1 : 1;
    }
    return lstrcmpi(a->name, b->name);
}

static void sort_names(WINFIND_N *e, int n, int how)
{
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++) {
            WINFIND_N t = e[i];
            int j = i;
            while (j >= gap && ent_cmp(&e[j - gap], &t, how) > 0) {
                e[j] = e[j - gap];
                j -= gap;
            }
            e[j] = t;
        }
}

static void scroll_to(HWND pane, int *top, int caret, int count);

static void read_files(fmwin *fw)
{
    char spec[MAXPATH];
    join(spec, fw->path, "*.*");
    fw->up = !is_root(fw->path);
    int base = fw->up ? 1 : 0;
    if (fw->up) {
        memset(&fw->ents[0], 0, sizeof(WINFIND_N));
        lstrcpy(fw->ents[0].name, "..");
        fw->ents[0].attr = 0x10;
    }
    int n = WinFindFilesN(spec, ATTR_ALL, fw->ents + base, MAXENTS - base);
    sort_names(fw->ents + base, n, sort_by);
    fw->nents = base + n;
    memset(fw->sel, 0, MAXENTS);
    fw->fcaret = fw->fanchor = 0;
    fw->ftop = 0;
    if (fw->nents > base) {
        fw->fcaret = fw->fanchor = base;
        fw->sel[base] = 1;
    }
    if (fw->files) {
        scroll_to(fw->files, &fw->ftop, fw->fcaret, fw->nents);
        InvalidateRect(fw->files, NULL, TRUE);
    }
}

/* the directory shown in the contents pane */
static void change_dir(fmwin *fw, const char *path, int rebuild_tree)
{
    lstrcpyn(fw->path, path, MAXPATH);
    fw->drive = (fw->path[0] | 0x20) - 'a';
    if (rebuild_tree)
        build_tree(fw, fw->path);
    read_files(fw);
    set_title(fw);
    if (fw->tree) {
        scroll_to(fw->tree, &fw->ttop, fw->tsel, fw->nnodes);
        InvalidateRect(fw->tree, NULL, TRUE);
    }
    RECT rc;
    GetClientRect(fw->hwnd, &rc);
    rc.bottom = drive_h;
    InvalidateRect(fw->hwnd, &rc, TRUE);
    update_status();
}

/* the tree selection moved: the contents follow */
static void tree_select(fmwin *fw, int i)
{
    if (i < 0) i = 0;
    if (i >= fw->nnodes) i = fw->nnodes - 1;
    fw->tsel = i;
    char p[MAXPATH];
    node_path(fw, i, p);
    change_dir(fw, p, 0);
}

/* everything read again (after a file operation), the open branches kept */
static void refresh(fmwin *fw)
{
    static char open_paths[16][MAXPATH];   /* off the task stack */
    int nopen = 0;
    for (int i = 1; i < fw->nnodes && nopen < 16; i++)
        if (fw->nodes[i].expanded)
            node_path(fw, i, open_paths[nopen++]);
    char keep[MAXPATH];
    lstrcpy(keep, fw->path);
    while (!is_dir(keep) && !is_root(keep))
        parent_of(keep);
    build_tree(fw, keep);
    int sel = fw->tsel;
    for (int k = 0; k < nopen; k++)
        for (int i = 1; i < fw->nnodes; i++) {
            char p[MAXPATH];
            node_path(fw, i, p);
            if (!lstrcmpi(p, open_paths[k])) {
                if (i <= sel) {
                    int before = fw->nnodes;
                    expand_node(fw, i);
                    if (i < sel)
                        sel += fw->nnodes - before;
                } else {
                    expand_node(fw, i);
                }
                break;
            }
        }
    fw->tsel = sel;
    change_dir(fw, keep, 0);
}

static void refresh_all(void)
{
    for (int i = 0; i < MAXWIN; i++)
        if (wins[i])
            refresh(wins[i]);
}

/* ---------------------------------------------------------------- drawing */

static void box(HDC dc, int l, int t, int r, int b, COLORREF fill_c)
{
    HBRUSH br = CreateSolidBrush(fill_c);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, GetStockObject(BLACK_PEN));
    Rectangle(dc, l, t, r, b);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(br);
}

static void line(HDC dc, int x0, int y0, int x1, int y1)
{
    MoveTo(dc, x0, y0);
    LineTo(dc, x1, y1);
}

enum { G_FOLDER, G_OPEN, G_UP, G_PROGRAM, G_DOC, G_FILE };

static void glyph(HDC dc, int x, int y, int g)
{
    switch (g) {
    case G_FOLDER:
    case G_OPEN:
        box(dc, x + 1, y + 2, x + 7, y + 5, RGB(255, 255, 0));
        box(dc, x + 1, y + 4, x + 16, y + 14, RGB(255, 255, 0));
        if (g == G_OPEN) {
            POINT p[4] = { { x + 3, y + 7 }, { x + 18, y + 7 }, { x + 15, y + 13 }, { x + 1, y + 13 } };
            HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
            HGDIOBJ ob = SelectObject(dc, br);
            Polygon(dc, p, 4);
            SelectObject(dc, ob);
            DeleteObject(br);
        }
        break;
    case G_UP: {
        POINT p[7] = { { x + 8, y + 1 }, { x + 15, y + 8 }, { x + 11, y + 8 }, { x + 11, y + 14 },
                       { x + 5, y + 14 }, { x + 5, y + 8 }, { x + 1, y + 8 } };
        HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
        HGDIOBJ ob = SelectObject(dc, br);
        Polygon(dc, p, 7);
        SelectObject(dc, ob);
        DeleteObject(br);
        break;
    }
    case G_PROGRAM:
        box(dc, x + 1, y + 2, x + 16, y + 14, RGB(255, 255, 255));
        box(dc, x + 1, y + 2, x + 16, y + 6, RGB(0, 0, 128));
        break;
    case G_DOC:
    case G_FILE:
        box(dc, x + 3, y + 1, x + 14, y + 15, RGB(255, 255, 255));
        if (g == G_DOC)
            for (int k = 0; k < 4; k++)
                line(dc, x + 5, y + 4 + k * 3, x + 12, y + 4 + k * 3);
        break;
    }
}

static int ent_glyph(const WINFIND_N *f, int up)
{
    if (up)
        return G_UP;
    if (f->attr & 0x10)
        return G_FOLDER;
    if (is_program(f->name))
        return G_PROGRAM;
    char cmd[64];
    return association(f->name, cmd, sizeof(cmd)) ? G_DOC : G_FILE;
}

/* the item's text: the name, and in "All File Details" the rest */
static void ent_text(const WINFIND_N *f, int up, char *out)
{
    if (up || !view_details) {
        lstrcpy(out, f->name);
        if (!up && !view_details)
            AnsiLower(out);
        return;
    }
    char size[16] = "", name[16], attrs[8];
    lstrcpy(name, f->name);
    AnsiLower(name);
    if (!(f->attr & 0x10))
        commas(size, f->size);
    int a = 0;
    if (f->attr & 0x01) attrs[a++] = 'r';
    if (f->attr & 0x02) attrs[a++] = 'h';
    if (f->attr & 0x04) attrs[a++] = 's';
    if (f->attr & 0x20) attrs[a++] = 'a';
    attrs[a] = 0;
    int mon = (f->date >> 5) & 15, day = f->date & 31, yr = (1980 + (f->date >> 9)) % 100;
    int hr = f->time >> 11, mi = (f->time >> 5) & 63, se = (f->time & 31) * 2;
    int h12 = hr % 12 ? hr % 12 : 12;
    wsprintf(out, "%-13s%11s  %2d/%02d/%02d  %2d:%02d:%02d%s  %s", name, size, mon, day, yr, h12, mi, se,
             hr < 12 ? "am" : "pm", attrs);
}

static void paint_tree(HWND h, fmwin *fw, HDC dc)
{
    RECT rc;
    GetClientRect(h, &rc);
    int focus = GetFocus() == h;
    int ind = 2 * char_w;
    SetBkMode(dc, TRANSPARENT);
    HPEN dots = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    HGDIOBJ op = SelectObject(dc, dots);
    for (int i = fw->ttop; i < fw->nnodes; i++) {
        int y = (i - fw->ttop) * ROW_H;
        if (y > rc.bottom)
            break;
        tnode *t = &fw->nodes[i];
        int x = 4 + t->level * ind;
        /* the lines to the parent and past it to the later siblings */
        SelectObject(dc, dots);
        for (int lv = 1; lv <= t->level; lv++) {
            /* is there a later node at this level before the branch ends? */
            int more = 0;
            for (int k = i + 1; k < fw->nnodes && fw->nodes[k].level >= lv; k++)
                if (fw->nodes[k].level == lv) {
                    more = 1;
                    break;
                }
            int lx = 4 + (lv - 1) * ind + 8;
            if (lv == t->level) {
                line(dc, lx, y, lx, more ? y + ROW_H : y + ROW_H / 2);
                line(dc, lx, y + ROW_H / 2, x, y + ROW_H / 2);
            } else if (more) {
                line(dc, lx, y, lx, y + ROW_H);
            }
        }
        SelectObject(dc, GetStockObject(BLACK_PEN));
        glyph(dc, x, y + (ROW_H - 16) / 2, i == fw->tsel ? G_OPEN : G_FOLDER);
        char label[16];
        if (i == 0)
            wsprintf(label, "%c:\\", 'a' + fw->drive);
        else {
            lstrcpy(label, t->name);
            AnsiLower(label);
        }
        int tx = x + 20;
        SIZE sz;
        GetTextExtentPoint(dc, label, lstrlen(label), &sz);
        RECT tr = { tx - 1, y, tx + sz.cx + 1, y + ROW_H };
        if (i == fw->tsel && focus) {
            FillRect(dc, &tr, (HBRUSH)(COLOR_HIGHLIGHT + 1));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            if (i == fw->tsel)
                FrameRect(dc, &tr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        TextOut(dc, tx, y + 1, label, lstrlen(label));
    }
    SelectObject(dc, op);
    DeleteObject(dots);
}

static void paint_files(HWND h, fmwin *fw, HDC dc)
{
    RECT rc;
    GetClientRect(h, &rc);
    int focus = GetFocus() == h;
    SetBkMode(dc, TRANSPARENT);
    if (!fw->nents) {
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TextOut(dc, 8, 4, "No files found.", 15);
        return;
    }
    for (int i = fw->ftop; i < fw->nents; i++) {
        int y = (i - fw->ftop) * ROW_H;
        if (y > rc.bottom)
            break;
        int up = fw->up && i == 0;
        glyph(dc, 4, y + (ROW_H - 16) / 2, ent_glyph(&fw->ents[i], up));
        char text[96];
        ent_text(&fw->ents[i], up, text);
        int tx = 24;
        SIZE sz;
        GetTextExtentPoint(dc, text, lstrlen(text), &sz);
        RECT tr = { tx - 1, y, tx + sz.cx + 1, y + ROW_H };
        if (fw->sel[i]) {
            if (focus) {
                FillRect(dc, &tr, (HBRUSH)(COLOR_HIGHLIGHT + 1));
                SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
            } else {
                SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
                FrameRect(dc, &tr, (HBRUSH)GetStockObject(BLACK_BRUSH));
            }
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }
        TextOut(dc, tx, y + 1, text, lstrlen(text));
        if (focus && i == fw->fcaret)
            DrawFocusRect(dc, &tr);
    }
}

static void paint_drives(HWND h, fmwin *fw, HDC dc)
{
    RECT rc;
    GetClientRect(h, &rc);
    RECT bar = { 0, 0, rc.right, drive_h };
    FillRect(dc, &bar, (HBRUSH)(COLOR_WINDOW + 1));
    SetBkMode(dc, TRANSPARENT);
    int x = 4;
    for (int d = 0; d < 26; d++) {
        UINT t = drive_type(d);
        if (!t)
            continue;
        int y = (drive_h - 12) / 2;
        COLORREF body = t == DRIVE_REMOVABLE ? RGB(192, 192, 192) : t == DRIVE_REMOTE ? RGB(0, 128, 128)
                                                                                       : RGB(128, 128, 128);
        box(dc, x, y + 2, x + 18, y + 11, body);
        line(dc, x + 3, y + 8, x + 8, y + 8);
        char l[2] = { (char)('a' + d), 0 };
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TextOut(dc, x + 21, (drive_h - line_h) / 2, l, 1);
        if (d == fw->drive) {
            RECT r = { x - 2, 1, x + 21 + char_w + 2, drive_h - 1 };
            FrameRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
            if (fw->focus == h)
                DrawFocusRect(dc, &r);
        }
        x += 21 + char_w + 10;
    }
    SelectObject(dc, GetStockObject(BLACK_PEN));
    line(dc, 0, drive_h - 1, rc.right, drive_h - 1);
}

/* which drive is at x in the drive bar, -1 none */
static int drive_at(int px)
{
    int x = 4;
    for (int d = 0; d < 26; d++) {
        if (!drive_type(d))
            continue;
        if (px >= x - 2 && px < x + 21 + char_w + 2)
            return d;
        x += 21 + char_w + 10;
    }
    return -1;
}

static void select_drive(fmwin *fw, int d)
{
    if (d < 0 || !drive_type(d))
        return;
    char p[MAXPATH];
    wsprintf(p, "%c:\\", 'A' + d);
    if (d == (start_path[0] | 0x20) - 'a')
        lstrcpy(p, start_path);         /* the drive's current directory */
    HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));
    change_dir(fw, p, 1);
    SetCursor(old);
}

/* ---------------------------------------------------------------- pane scrolling and keys */

static int visible_rows(HWND pane)
{
    RECT rc;
    GetClientRect(pane, &rc);
    int n = rc.bottom / ROW_H;
    return n > 0 ? n : 1;
}

static void scroll_to(HWND pane, int *top, int caret, int count)
{
    int vis = visible_rows(pane);
    if (caret < *top)
        *top = caret;
    if (caret >= *top + vis)
        *top = caret - vis + 1;
    int max = count - vis;
    if (max < 0) max = 0;
    if (*top > max) *top = max;
    if (*top < 0) *top = 0;
    SetScrollRange(pane, SB_VERT, 0, max, FALSE);
    SetScrollPos(pane, SB_VERT, *top, TRUE);
}

static void vscroll(HWND pane, int *top, int count, WPARAM code, LPARAM lp)
{
    int vis = visible_rows(pane), t = *top;
    switch (code) {
    case SB_LINEUP: t--; break;
    case SB_LINEDOWN: t++; break;
    case SB_PAGEUP: t -= vis; break;
    case SB_PAGEDOWN: t += vis; break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: t = (short)LOWORD(lp); break;
    case SB_TOP: t = 0; break;
    case SB_BOTTOM: t = count; break;
    default: return;
    }
    int max = count - vis;
    if (max < 0) max = 0;
    if (t > max) t = max;
    if (t < 0) t = 0;
    if (t != *top) {
        *top = t;
        SetScrollPos(pane, SB_VERT, t, TRUE);
        InvalidateRect(pane, NULL, TRUE);
    }
}

static void next_pane(fmwin *fw, int back)
{
    HWND order[3] = { fw->tree, fw->files, fw->hwnd };
    int n = 0, cur = 0;
    HWND list[3];
    for (int i = 0; i < 3; i++) {
        if ((order[i] == fw->tree && panes == IDM_DIRONLY) || (order[i] == fw->files && panes == IDM_TREEONLY))
            continue;
        if (order[i] == fw->focus)
            cur = n;
        list[n++] = order[i];
    }
    cur = (cur + (back ? n - 1 : 1)) % n;
    SetFocus(list[cur]);
}

static void open_selected(fmwin *fw);

static int count_rows(fmwin *fw, HWND h) { return h == fw->tree ? fw->nnodes : fw->nents; }

static void files_set_caret(fmwin *fw, int i, int extend, int toggle)
{
    if (!fw->nents)
        return;
    if (i < 0) i = 0;
    if (i >= fw->nents) i = fw->nents - 1;
    if (toggle) {
        fw->sel[i] = !fw->sel[i];
        fw->fanchor = i;
    } else if (extend) {
        memset(fw->sel, 0, MAXENTS);
        int a = fw->fanchor < i ? fw->fanchor : i, b = fw->fanchor < i ? i : fw->fanchor;
        for (int k = a; k <= b; k++)
            fw->sel[k] = 1;
    } else {
        memset(fw->sel, 0, MAXENTS);
        fw->sel[i] = 1;
        fw->fanchor = i;
    }
    fw->fcaret = i;
    scroll_to(fw->files, &fw->ftop, i, fw->nents);
    InvalidateRect(fw->files, NULL, TRUE);
    update_status();
}

static void tree_keys(fmwin *fw, WPARAM k)
{
    int vis = visible_rows(fw->tree), i = fw->tsel;
    switch (k) {
    case VK_UP: i--; break;
    case VK_DOWN: i++; break;
    case VK_PRIOR: i -= vis; break;
    case VK_NEXT: i += vis; break;
    case VK_HOME: i = 0; break;
    case VK_END: i = fw->nnodes - 1; break;
    case VK_LEFT:
        if (fw->nodes[i].expanded && i) {
            collapse_node(fw, i);
            InvalidateRect(fw->tree, NULL, TRUE);
            scroll_to(fw->tree, &fw->ttop, fw->tsel, fw->nnodes);
            return;
        }
        for (int j = i - 1; j >= 0; j--)
            if (fw->nodes[j].level < fw->nodes[i].level) {
                i = j;
                break;
            }
        break;
    case VK_RIGHT:
        if (!fw->nodes[i].expanded) {
            HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));
            expand_node(fw, i);
            SetCursor(old);
            scroll_to(fw->tree, &fw->ttop, fw->tsel, fw->nnodes);
            InvalidateRect(fw->tree, NULL, TRUE);
            return;
        }
        if (i + 1 < fw->nnodes && fw->nodes[i + 1].level > fw->nodes[i].level)
            i++;
        break;
    case VK_RETURN:
        SendMessage(hframe, WM_COMMAND, fw->nodes[i].expanded ? IDM_COLLAPSE : IDM_EXPONE, 0);
        return;
    default:
        return;
    }
    if (i < 0) i = 0;
    if (i >= fw->nnodes) i = fw->nnodes - 1;
    if (i != fw->tsel)
        tree_select(fw, i);
}

static void files_keys(fmwin *fw, WPARAM k)
{
    int vis = visible_rows(fw->files), i = fw->fcaret;
    int shift = GetKeyState(VK_SHIFT) < 0;
    switch (k) {
    case VK_UP: i--; break;
    case VK_DOWN: i++; break;
    case VK_PRIOR: i -= vis; break;
    case VK_NEXT: i += vis; break;
    case VK_HOME: i = 0; break;
    case VK_END: i = fw->nents - 1; break;
    case VK_RETURN: open_selected(fw); return;
    case VK_BACK:
        if (!is_root(fw->path)) {
            char p[MAXPATH];
            lstrcpy(p, fw->path);
            parent_of(p);
            change_dir(fw, p, 1);
        }
        return;
    case VK_SPACE:
        if (GetKeyState(VK_CONTROL) < 0)
            files_set_caret(fw, fw->fcaret, 0, 1);
        return;
    default:
        return;
    }
    files_set_caret(fw, i, shift, 0);
}

/* a letter: the next item starting with it */
static void jump_letter(fmwin *fw, HWND h, char c)
{
    int tree = h == fw->tree, n = count_rows(fw, h), cur = tree ? fw->tsel : fw->fcaret;
    c = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    for (int k = 1; k <= n; k++) {
        int i = (cur + k) % n;
        const char *name = tree ? fw->nodes[i].name : fw->ents[i].name;
        char f = name[0] >= 'a' && name[0] <= 'z' ? (char)(name[0] - 32) : name[0];
        if (f == c) {
            if (tree)
                tree_select(fw, i);
            else
                files_set_caret(fw, i, 0, 0);
            return;
        }
    }
}

static LRESULT CALLBACK PaneProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    fmwin *fw = find_win(GetParent(h));
    if (!fw)
        return DefWindowProc(h, msg, wp, lp);
    int tree = h == fw->tree;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (tree)
            paint_tree(h, fw, dc);
        else
            paint_files(h, fw, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE:
        if (tree)
            scroll_to(h, &fw->ttop, fw->tsel, fw->nnodes);
        else
            scroll_to(h, &fw->ftop, fw->fcaret, fw->nents);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (msg == WM_SETFOCUS)
            fw->focus = h;
        InvalidateRect(h, NULL, TRUE);
        update_status();
        return 0;
    case WM_VSCROLL:
        if (tree)
            vscroll(h, &fw->ttop, fw->nnodes, wp, lp);
        else
            vscroll(h, &fw->ftop, fw->nents, wp, lp);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        SetFocus(h);
        int i = (short)HIWORD(lp) / ROW_H + (tree ? fw->ttop : fw->ftop);
        if (i >= count_rows(fw, h))
            return 0;
        if (tree) {
            if (i != fw->tsel)
                tree_select(fw, i);
            if (msg == WM_LBUTTONDBLCLK)
                SendMessage(hframe, WM_COMMAND, fw->nodes[i].expanded ? IDM_COLLAPSE : IDM_EXPONE, 0);
        } else if (msg == WM_LBUTTONDBLCLK) {
            files_set_caret(fw, i, 0, 0);
            open_selected(fw);
        } else {
            files_set_caret(fw, i, (wp & MK_SHIFT) != 0, (wp & MK_CONTROL) != 0);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_TAB) {
            next_pane(fw, GetKeyState(VK_SHIFT) < 0);
            return 0;
        }
        if (tree)
            tree_keys(fw, wp);
        else
            files_keys(fw, wp);
        return 0;
    case WM_CHAR:
        if (tree && (wp == '+' || wp == '-' || wp == '*')) {
            SendMessage(hframe, WM_COMMAND,
                        wp == '+' ? IDM_EXPONE : wp == '-' ? IDM_COLLAPSE : IDM_EXPBRANCH, 0);
            return 0;
        }
        if ((wp >= 'a' && wp <= 'z') || (wp >= 'A' && wp <= 'Z') || (wp >= '0' && wp <= '9'))
            jump_letter(fw, h, (char)wp);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---------------------------------------------------------------- directory windows */

static void layout(fmwin *fw)
{
    RECT rc;
    GetClientRect(fw->hwnd, &rc);
    int top = drive_h, h = rc.bottom - top;
    if (h < 0) h = 0;
    int split = panes == IDM_TREEONLY ? rc.right : panes == IDM_DIRONLY ? 0 : rc.right * 2 / 5;
    ShowWindow(fw->tree, panes == IDM_DIRONLY ? SW_HIDE : SW_SHOW);
    ShowWindow(fw->files, panes == IDM_TREEONLY ? SW_HIDE : SW_SHOW);
    if (panes == IDM_TREEDIR) {
        MoveWindow(fw->tree, 0, top, split - 2, h, TRUE);
        MoveWindow(fw->files, split + 2, top, rc.right - split - 2, h, TRUE);
    } else {
        MoveWindow(fw->tree, 0, top, rc.right, h, TRUE);
        MoveWindow(fw->files, 0, top, rc.right, h, TRUE);
    }
    if (panes == IDM_TREEONLY && fw->focus == fw->files)
        fw->focus = fw->tree;
    if (panes == IDM_DIRONLY && fw->focus == fw->tree)
        fw->focus = fw->files;
}

static LRESULT CALLBACK DirProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    fmwin *fw = find_win(h);
    switch (msg) {
    case WM_CREATE: {
        fw = creating;
        if (!fw)
            return -1;
        fw->hwnd = h;
        DWORD ps = WS_CHILD | WS_VISIBLE | WS_VSCROLL;
        fw->tree = CreateWindow("WFPane", "", ps, 0, 0, 0, 0, h, (HMENU)1, hinst, NULL);
        fw->files = CreateWindow("WFPane", "", ps, 0, 0, 0, 0, h, (HMENU)2, hinst, NULL);
        fw->focus = panes == IDM_DIRONLY ? fw->files : fw->tree;
        set_title(fw);
        break;
    }
    case WM_SIZE:
        if (fw && wp != SIZE_MINIMIZED)
            layout(fw);
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (fw)
            paint_drives(h, fw, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        if (fw && fw->focus && fw->focus != h)
            SetFocus(fw->focus);
        else if (fw) {
            fw->focus = h;
            RECT r = { 0, 0, 0, drive_h };
            GetClientRect(h, &r);
            r.bottom = drive_h;
            InvalidateRect(h, &r, TRUE);
        }
        return 0;
    case WM_KILLFOCUS:
        if (fw && fw->focus == h) {
            RECT r;
            GetClientRect(h, &r);
            r.bottom = drive_h;
            InvalidateRect(h, &r, TRUE);
        }
        return 0;
    case WM_KEYDOWN:
        if (fw && fw->focus == h) {
            if (wp == VK_TAB) {
                next_pane(fw, GetKeyState(VK_SHIFT) < 0);
                return 0;
            }
            if (wp == VK_LEFT || wp == VK_RIGHT) {
                int d = fw->drive;
                for (int k = 0; k < 26; k++) {
                    d = (d + (wp == VK_RIGHT ? 1 : 25)) % 26;
                    if (drive_type(d))
                        break;
                }
                select_drive(fw, d);
                fw->focus = h;
                return 0;
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (fw && (short)HIWORD(lp) < drive_h) {
            int d = drive_at((short)LOWORD(lp));
            if (d >= 0 && d != fw->drive)
                select_drive(fw, d);
            return 0;
        }
        break;
    case WM_MDIACTIVATE:
        update_status();
        break;
    case WM_DESTROY:
        if (fw) {
            for (int i = 0; i < MAXWIN; i++)
                if (wins[i] == fw)
                    wins[i] = NULL;
            gfree(fw->nodes);
            gfree(fw->ents);
            gfree(fw->sel);
            gfree(fw);
        }
        update_status();
        break;
    }
    return DefMDIChildProc(h, msg, wp, lp);
}


static HWND new_window(const char *path)
{
    int slot = -1;
    for (int i = 0; i < MAXWIN; i++)
        if (!wins[i]) {
            slot = i;
            break;
        }
    if (slot < 0) {
        MessageBox(hframe, "Too many directory windows are open.", "File Manager", MB_OK | MB_ICONEXCLAMATION);
        return 0;
    }
    fmwin *fw = (fmwin *)galloc(sizeof(fmwin));
    if (fw) {
        fw->nodes = (tnode *)galloc(sizeof(tnode) * MAXNODES);
        fw->ents = (WINFIND_N *)galloc(sizeof(WINFIND_N) * MAXENTS);
        fw->sel = (BYTE *)galloc(MAXENTS);
    }
    if (!fw || !fw->nodes || !fw->ents || !fw->sel) {
        MessageBox(hframe, "Not enough memory.", "File Manager", MB_OK | MB_ICONSTOP);
        return 0;
    }
    lstrcpyn(fw->path, path, MAXPATH);
    fw->drive = (path[0] | 0x20) - 'a';
    wins[slot] = fw;
    HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));
    build_tree(fw, fw->path);
    MDICREATESTRUCT cs;
    RECT rc;
    GetClientRect(hclient, &rc);
    int n = 0;
    for (int i = 0; i < MAXWIN; i++)
        if (wins[i])
            n++;
    cs.szClass = "WFDir";
    cs.szTitle = "";
    cs.hOwner = hinst;
    cs.x = cs.y = (n - 1) * 20;
    cs.cx = rc.right - cs.x;
    cs.cy = rc.bottom - cs.y;
    cs.style = 0;
    cs.lParam = 0;
    creating = fw;
    HWND h = (HWND)SendMessage(hclient, WM_MDICREATE, 0, (LPARAM)(intptr_t)&cs);
    creating = NULL;
    if (!h) {
        wins[slot] = NULL;
        SetCursor(old);
        return 0;
    }
    change_dir(fw, fw->path, 0);
    SetCursor(old);
    return h;
}

/* ---------------------------------------------------------------- open, run */

static void report(const char *what, const char *name)
{
    char t[MAXPATH + 64];
    wsprintf(t, what, name);
    MessageBox(hframe, t, "File Manager", MB_OK | MB_ICONEXCLAMATION);
}

static void run_cmd(const char *cmd)
{
    UINT r = WinExec(cmd, SW_SHOWNORMAL);
    if (r < 32) {
        char t[MAXPATH + 64];
        wsprintf(t, "Cannot run '%s'.%s", cmd, r == 11 ? "\nIt is not a Windows application." : "");
        MessageBox(hframe, t, "File Manager", MB_OK | MB_ICONEXCLAMATION);
    }
}

static void open_selected(fmwin *fw)
{
    if (!fw->nents)
        return;
    int i = fw->fcaret;
    WINFIND_N *f = &fw->ents[i];
    char p[MAXPATH];
    if (fw->up && i == 0) {
        lstrcpy(p, fw->path);
        parent_of(p);
        change_dir(fw, p, 1);
        return;
    }
    join(p, fw->path, f->name);
    if (f->attr & 0x10) {
        /* into the directory, its branch opened */
        if (!fw->nodes[fw->tsel].expanded)
            expand_node(fw, fw->tsel);
        for (int j = fw->tsel + 1; j < fw->nnodes && fw->nodes[j].level > fw->nodes[fw->tsel].level; j++)
            if (fw->nodes[j].level == fw->nodes[fw->tsel].level + 1 && !lstrcmpi(fw->nodes[j].name, f->name)) {
                fw->tsel = j;
                break;
            }
        change_dir(fw, p, 0);
        return;
    }
    if (is_program(f->name)) {
        run_cmd(p);
        return;
    }
    char cmd[MAXPATH * 2];
    if (association(f->name, cmd, MAXPATH)) {
        lstrcat(cmd, " ");
        lstrcat(cmd, p);
        run_cmd(cmd);
        return;
    }
    MessageBox(hframe, "No association exists for the specified file.\n"
                       "Add one to WIN.INI [Extensions], as: txt=notepad.exe ^.txt",
               "File Manager", MB_OK | MB_ICONEXCLAMATION);
}

/* ---------------------------------------------------------------- copy, move, delete */

static char copy_buf[2048];

static int copy_file(const char *src, const char *dst)
{
    if (!lstrcmpi(src, dst))
        return 0;
    HFILE in = _lopen(src, OF_READ);
    if (in == HFILE_ERROR)
        return 0;
    HFILE out = _lcreat(dst, 0);
    if (out == HFILE_ERROR) {
        _lclose(in);
        return 0;
    }
    int ok = 1;
    for (;;) {
        UINT n = _lread(in, copy_buf, sizeof(copy_buf));
        if (n == (UINT)HFILE_ERROR) { ok = 0; break; }
        if (!n) break;
        if (_lwrite(out, copy_buf, n) != n) { ok = 0; break; }
    }
    _lclose(in);
    _lclose(out);
    if (!ok)
        DeleteFileN(dst);
    return ok;
}

/* the entries of a directory, in their own memory (the operations recurse) */
static WINFIND_N *list_dir(const char *dir, int *n)
{
    char spec[MAXPATH];
    join(spec, dir, "*.*");
    WINFIND_N *e = (WINFIND_N *)galloc(sizeof(WINFIND_N) * MAXENTS);
    *n = e ? WinFindFilesN(spec, ATTR_ALL, e, MAXENTS) : 0;
    return e;
}

static void free_list(WINFIND_N *e)
{
    gfree(e);
}

static int delete_tree(const char *path)
{
    WINFIND_N f;
    if (!get_entry(path, &f))
        return 0;
    if (!(f.attr & 0x10))
        return DeleteFileN(path);
    int n, ok = 1;
    WINFIND_N *e = list_dir(path, &n);
    for (int i = 0; i < n; i++) {
        char p[MAXPATH];
        join(p, path, e[i].name);
        if (!delete_tree(p))
            ok = 0;
    }
    free_list(e);
    return RemoveDirectoryN(path) && ok;
}

static int copy_tree(const char *src, const char *dst)
{
    WINFIND_N f;
    if (!get_entry(src, &f))
        return 0;
    if (!(f.attr & 0x10))
        return copy_file(src, dst);
    /* not into itself */
    int sl = lstrlen(src);
    char low[MAXPATH];
    lstrcpyn(low, dst, sl + 1);
    if (!lstrcmpi(low, src) && (dst[sl] == '\\' || !dst[sl]))
        return 0;
    if (!is_dir(dst) && !CreateDirectoryN(dst))
        return 0;
    int n, ok = 1;
    WINFIND_N *e = list_dir(src, &n);
    for (int i = 0; i < n; i++) {
        char a[MAXPATH], b[MAXPATH];
        join(a, src, e[i].name);
        join(b, dst, e[i].name);
        if (!copy_tree(a, b))
            ok = 0;
    }
    free_list(e);
    return ok;
}

static int move_tree(const char *src, const char *dst)
{
    if ((src[0] | 0x20) == (dst[0] | 0x20) && MoveFileN(src, dst))
        return 1;
    return copy_tree(src, dst) && delete_tree(src);
}

/* the names of the selection, space separated */
static void selection_text(fmwin *fw, char *out, int size)
{
    out[0] = 0;
    if (fw->focus == fw->tree) {
        if (fw->tsel > 0)
            lstrcpyn(out, fw->path, size);
        return;
    }
    int o = 0;
    for (int i = 0; i < fw->nents; i++) {
        if (!fw->sel[i] || (fw->up && i == 0))
            continue;
        int n = lstrlen(fw->ents[i].name);
        if (o + n + 2 >= size)
            break;
        if (o)
            out[o++] = ' ';
        lstrcpy(out + o, fw->ents[i].name);
        o += n;
    }
    out[o] = 0;
}

/* the next name of a space separated list, made absolute; 0 at the end */
static int next_name(const char **pp, const char *dir, char *out)
{
    const char *p = *pp;
    while (*p == ' ')
        p++;
    if (!*p)
        return 0;
    char t[MAXPATH];
    int n = 0;
    while (*p && *p != ' ') {
        if (n < MAXPATH - 1)
            t[n++] = *p;
        p++;
    }
    t[n] = 0;
    *pp = p;
    absolute(out, dir, t);
    return 1;
}

#define IDC_CURDIR 300
#define IDC_FROM   301
#define IDC_TO     302

static char dlg_from[256], dlg_to[MAXPATH], dlg_dir[MAXPATH];
static const char *dlg_caption;

static BOOL CALLBACK FromToProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        char t[MAXPATH + 24];
        wsprintf(t, "Current directory: %s", dlg_dir);
        SetDlgItemText(dlg, IDC_CURDIR, t);
        SetWindowText(dlg, dlg_caption);
        SetDlgItemText(dlg, IDC_FROM, dlg_from);
        SetDlgItemText(dlg, IDC_TO, dlg_to);
        if (GetDlgItem(dlg, IDC_TO)) {
            SetFocus(GetDlgItem(dlg, IDC_TO));
            SendDlgItemMessage(dlg, IDC_TO, EM_SETSEL, 0, MAKELPARAM(0, -1));
            return FALSE;
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK) {
            GetDlgItemText(dlg, IDC_FROM, dlg_from, sizeof(dlg_from));
            if (GetDlgItem(dlg, IDC_TO))
                GetDlgItemText(dlg, IDC_TO, dlg_to, sizeof(dlg_to));
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

#define EDITS (ES_LEFT | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE)

static const DLGTEMPLATE_N fromto_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 7, 30, 30, 240, 70,
    NULL, NULL, "Copy", 0, NULL
};
static const DLGITEM_N fromto_items[] = {
    { 6, 5, 228, 9, IDC_CURDIR, SS_LEFT | WS_VISIBLE, "STATIC", "" },
    { 6, 20, 30, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&From:" },
    { 38, 18, 196, 12, IDC_FROM, EDITS, "EDIT", "" },
    { 6, 36, 30, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&To:" },
    { 38, 34, 196, 12, IDC_TO, EDITS, "EDIT", "" },
    { 66, 52, 46, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 128, 52, 46, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

/* one field: Delete, Create Directory, Run */
static const DLGTEMPLATE_N one_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 5, 30, 30, 240, 54,
    NULL, NULL, "Delete", 0, NULL
};
static char one_label[24];
static const DLGITEM_N one_items[] = {
    { 6, 5, 228, 9, IDC_CURDIR, SS_LEFT | WS_VISIBLE, "STATIC", "" },
    { 6, 20, 30, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", one_label },
    { 38, 18, 196, 12, IDC_FROM, EDITS, "EDIT", "" },
    { 66, 36, 46, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 128, 36, 46, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

static int ask_one(const char *caption, const char *label, const char *init)
{
    dlg_caption = caption;
    lstrcpyn(one_label, label, sizeof(one_label));
    lstrcpyn(dlg_from, init, sizeof(dlg_from));
    return DialogBoxIndirectN(hinst, &one_tpl, one_items, hframe, FromToProc) == IDOK;
}

static void do_copy_move(fmwin *fw, int move)
{
    selection_text(fw, dlg_from, sizeof(dlg_from));
    lstrcpy(dlg_dir, fw->path);
    dlg_to[0] = 0;
    dlg_caption = move ? "Move" : "Copy";
    if (DialogBoxIndirectN(hinst, &fromto_tpl, fromto_items, hframe, FromToProc) != IDOK || !dlg_to[0])
        return;
    char to[MAXPATH];
    absolute(to, fw->path, dlg_to);
    int to_dir = is_dir(to), count = 0;
    for (const char *p = dlg_from; *p;) {
        if (*p == ' ') { p++; continue; }
        count++;
        while (*p && *p != ' ') p++;
    }
    if (count > 1 && !to_dir) {
        report("The destination '%s' is not a directory.", to);
        return;
    }
    HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));
    const char *p = dlg_from;
    char src[MAXPATH];
    while (next_name(&p, fw->path, src)) {
        char dst[MAXPATH];
        if (to_dir)
            join(dst, to, base_name(src));
        else
            lstrcpy(dst, to);
        if (!lstrcmpi(src, dst))
            continue;
        if (!(move ? move_tree(src, dst) : copy_tree(src, dst))) {
            SetCursor(old);
            report(move ? "Cannot move '%s'." : "Cannot copy '%s'.", src);
            break;
        }
    }
    SetCursor(old);
    refresh_all();
}

static void do_delete(fmwin *fw)
{
    char init[256];
    selection_text(fw, init, sizeof(init));
    lstrcpy(dlg_dir, fw->path);
    if (!init[0] || !ask_one("Delete", "&Delete:", init) || !dlg_from[0])
        return;
    const char *p = dlg_from;
    char path[MAXPATH];
    while (next_name(&p, fw->path, path)) {
        WINFIND_N f;
        if (!get_entry(path, &f) || is_root(path)) {
            report("Cannot find '%s'.", path);
            continue;
        }
        char q[MAXPATH + 64];
        wsprintf(q, (f.attr & 0x10) ? "Delete the directory %s\nand everything in it?" : "Delete file %s?", path);
        int r = MessageBox(hframe, q, "Confirm Delete", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL)
            break;
        if (r != IDYES)
            continue;
        HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));
        int ok = delete_tree(path);
        SetCursor(old);
        if (!ok)
            report("Cannot delete '%s'.", path);
    }
    refresh_all();
}

static void do_rename(fmwin *fw)
{
    selection_text(fw, dlg_from, sizeof(dlg_from));
    lstrcpy(dlg_dir, fw->path);
    dlg_to[0] = 0;
    dlg_caption = "Rename";
    if (DialogBoxIndirectN(hinst, &fromto_tpl, fromto_items, hframe, FromToProc) != IDOK || !dlg_to[0])
        return;
    const char *p = dlg_from;
    char src[MAXPATH], dst[MAXPATH];
    if (!next_name(&p, fw->path, src))
        return;
    /* a bare new name stays in the file's own directory */
    char dir[MAXPATH];
    lstrcpy(dir, src);
    parent_of(dir);
    absolute(dst, dir, dlg_to);
    if (!MoveFileN(src, dst))
        report("Cannot rename '%s'.", src);
    refresh_all();
}

static void do_mkdir(fmwin *fw)
{
    lstrcpy(dlg_dir, fw->path);
    if (!ask_one("Create Directory", "&Name:", "") || !dlg_from[0])
        return;
    char p[MAXPATH];
    absolute(p, fw->path, dlg_from);
    if (!CreateDirectoryN(p))
        report("Cannot create the directory '%s'.", p);
    refresh_all();
}

static void do_run(fmwin *fw)
{
    char init[MAXPATH] = "";
    if (fw && fw->focus == fw->files && fw->nents && !(fw->ents[fw->fcaret].attr & 0x10))
        join(init, fw->path, fw->ents[fw->fcaret].name);
    lstrcpy(dlg_dir, fw ? fw->path : start_path);
    if (ask_one("Run", "&Command:", init) && dlg_from[0])
        run_cmd(dlg_from);
}

/* ---------------------------------------------------------------- Select Drive */

#define IDC_DRIVES 310

static int drive_pick;

static BOOL CALLBACK DriveProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND lb = GetDlgItem(dlg, IDC_DRIVES);
        for (int d = 0; d < 26; d++) {
            UINT t = drive_type(d);
            if (!t)
                continue;
            char s[32];
            wsprintf(s, "%c:  %s", 'A' + d, t == DRIVE_REMOVABLE ? "removable" : t == DRIVE_REMOTE ? "network" : "fixed");
            int i = (int)SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(intptr_t)s);
            if (d == drive_pick)
                SendMessage(lb, LB_SETCURSEL, (WPARAM)i, 0);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDC_DRIVES && HIWORD(lp) == LBN_DBLCLK)
            wp = IDOK;
        if (wp == IDOK) {
            char s[32];
            HWND lb = GetDlgItem(dlg, IDC_DRIVES);
            int i = (int)SendMessage(lb, LB_GETCURSEL, 0, 0);
            if (i >= 0) {
                SendMessage(lb, LB_GETTEXT, (WPARAM)i, (LPARAM)(intptr_t)s);
                drive_pick = (s[0] | 0x20) - 'a';
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static const DLGTEMPLATE_N drive_tpl = {
    WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME, 4, 60, 30, 130, 92,
    NULL, NULL, "Select Drive", 0, NULL
};
static const DLGITEM_N drive_items[] = {
    { 6, 5, 80, 9, -1, SS_LEFT | WS_VISIBLE, "STATIC", "&Drives:" },
    { 6, 15, 76, 70, IDC_DRIVES, LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_VISIBLE, "LISTBOX", "" },
    { 88, 15, 36, 14, IDOK, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "OK" },
    { 88, 33, 36, 14, IDCANCEL, BS_PUSHBUTTON | WS_TABSTOP | WS_VISIBLE, "BUTTON", "Cancel" },
};

/* ---------------------------------------------------------------- the frame */

static void paint_status(HDC dc)
{
    RECT rc;
    GetClientRect(hframe, &rc);
    RECT sb = { 0, rc.bottom - STATUS_H, rc.right, rc.bottom };
    FillRect(dc, &sb, (HBRUSH)(COLOR_BTNFACE + 1));
    HPEN hi = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
    HPEN sh = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    HGDIOBJ op = SelectObject(dc, GetStockObject(BLACK_PEN));
    line(dc, 0, sb.top, rc.right, sb.top);
    char left[96] = "", right[96] = "";
    fmwin *fw = active_win();
    if (fw && fw->focus == fw->files) {
        DWORD sb_bytes = 0, tb = 0;
        int sn = 0, tn = 0;
        for (int i = 0; i < fw->nents; i++) {
            if (fw->up && i == 0)
                continue;
            tn++;
            tb += fw->ents[i].size;
            if (fw->sel[i]) {
                sn++;
                sb_bytes += fw->ents[i].size;
            }
        }
        char a[16], b[16];
        commas(a, sb_bytes);
        commas(b, tb);
        wsprintf(left, "Selected %d file(s) (%s bytes)", sn, a);
        wsprintf(right, "Total %d file(s) (%s bytes)", tn, b);
    } else if (fw) {
        DWORD fr, tot;
        if (GetDiskFreeSpaceN(fw->drive + 1, &fr, &tot)) {
            char a[16], b[16];
            commas(a, fr / 1024);
            commas(b, tot / 1024);
            wsprintf(left, "%c: %sKB free,  %sKB total", 'A' + fw->drive, a, b);
        }
        int dirs = 0;
        for (int i = 0; i < fw->nents; i++)
            if (!(fw->up && i == 0))
                dirs++;
        wsprintf(right, "Total %d file(s)", dirs);
    }
    int mid = rc.right / 2;
    RECT boxes[2] = { { 4, sb.top + 3, mid - 4, rc.bottom - 2 }, { mid + 4, sb.top + 3, rc.right - 4, rc.bottom - 2 } };
    for (int k = 0; k < 2; k++) {
        RECT *r = &boxes[k];
        SelectObject(dc, sh);
        line(dc, r->left, r->bottom - 1, r->left, r->top);
        line(dc, r->left, r->top, r->right, r->top);
        SelectObject(dc, hi);
        line(dc, r->right - 1, r->top + 1, r->right - 1, r->bottom - 1);
        line(dc, r->left + 1, r->bottom - 1, r->right, r->bottom - 1);
    }
    SelectObject(dc, op);
    DeleteObject(hi);
    DeleteObject(sh);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    TextOut(dc, boxes[0].left + 4, boxes[0].top + 1, left, lstrlen(left));
    TextOut(dc, boxes[1].left + 4, boxes[1].top + 1, right, lstrlen(right));
}

static void place_client(void)
{
    RECT rc;
    GetClientRect(hframe, &rc);
    int h = rc.bottom - (show_status ? STATUS_H : 0);
    MoveWindow(hclient, 0, 0, rc.right, h > 0 ? h : 0, TRUE);
}

static void save_settings(void)
{
    char v[8];
    wsprintf(v, "%d", view_details);
    WritePrivateProfileString("Settings", "Details", v, INI);
    wsprintf(v, "%d", sort_by - IDM_BYNAME);
    WritePrivateProfileString("Settings", "Sort", v, INI);
    wsprintf(v, "%d", show_status);
    WritePrivateProfileString("Settings", "StatusBar", v, INI);
    wsprintf(v, "%d", panes - IDM_TREEDIR);
    WritePrivateProfileString("Settings", "Panes", v, INI);
}

static void all_layout(void)
{
    for (int i = 0; i < MAXWIN; i++)
        if (wins[i]) {
            layout(wins[i]);
            InvalidateRect(wins[i]->files, NULL, TRUE);
        }
}

static LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        hframe = h;
        CLIENTCREATESTRUCT cc;
        cc.hWindowMenu = GetSubMenu(GetMenu(h), 5);
        cc.idFirstChild = IDM_FIRSTCHILD;
        hclient = CreateWindow("MDICLIENT", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                               0, 0, 0, 0, h, (HMENU)1, hinst, (LPVOID)&cc);
        return hclient ? 0 : -1;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            place_client();
            update_status();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (show_status)
            paint_status(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_INITMENUPOPUP: {
        HMENU m = (HMENU)wp;
        fmwin *fw = active_win();
        UINT on = fw ? MF_ENABLED : MF_GRAYED;
        for (UINT id = IDM_OPEN; id <= IDM_MKDIR; id++)
            EnableMenuItem(m, id, on);
        for (UINT id = IDM_EXPONE; id <= IDM_COLLAPSE; id++)
            EnableMenuItem(m, id, on);
        for (UINT id = IDM_TREEDIR; id <= IDM_DIRONLY; id++)
            CheckMenuItem(m, id, id == (UINT)panes ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(m, IDM_NAME, view_details ? MF_UNCHECKED : MF_CHECKED);
        CheckMenuItem(m, IDM_DETAILS, view_details ? MF_CHECKED : MF_UNCHECKED);
        for (UINT id = IDM_BYNAME; id <= IDM_BYDATE; id++)
            CheckMenuItem(m, id, id == (UINT)sort_by ? MF_CHECKED : MF_UNCHECKED);
        CheckMenuItem(m, IDM_STATUS, show_status ? MF_CHECKED : MF_UNCHECKED);
        return 0;
    }
    case WM_COMMAND: {
        fmwin *fw = active_win();
        switch (wp) {
        case IDM_OPEN:
            if (fw) {
                if (fw->focus == fw->tree)
                    SendMessage(h, WM_COMMAND, fw->nodes[fw->tsel].expanded ? IDM_COLLAPSE : IDM_EXPONE, 0);
                else
                    open_selected(fw);
            }
            return 0;
        case IDM_MOVE:   if (fw) do_copy_move(fw, 1); return 0;
        case IDM_COPY:   if (fw) do_copy_move(fw, 0); return 0;
        case IDM_DELETE: if (fw) do_delete(fw); return 0;
        case IDM_RENAME: if (fw) do_rename(fw); return 0;
        case IDM_MKDIR:  if (fw) do_mkdir(fw); return 0;
        case IDM_RUN:    do_run(fw); return 0;
        case IDM_EXIT:   DestroyWindow(h); return 0;
        case IDM_DRIVE:
            if (fw) {
                drive_pick = fw->drive;
                if (DialogBoxIndirectN(hinst, &drive_tpl, drive_items, h, DriveProc) == IDOK)
                    select_drive(fw, drive_pick);
            }
            return 0;
        case IDM_EXPONE:
        case IDM_EXPBRANCH:
        case IDM_EXPALL:
        case IDM_COLLAPSE:
            if (fw) {
                HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));
                if (wp == IDM_EXPONE)
                    expand_node(fw, fw->tsel);
                else if (wp == IDM_EXPBRANCH)
                    expand_branch(fw, fw->tsel);
                else if (wp == IDM_EXPALL)
                    expand_branch(fw, 0);
                else if (fw->tsel)
                    collapse_node(fw, fw->tsel);
                SetCursor(old);
                scroll_to(fw->tree, &fw->ttop, fw->tsel, fw->nnodes);
                InvalidateRect(fw->tree, NULL, TRUE);
            }
            return 0;
        case IDM_TREEDIR:
        case IDM_TREEONLY:
        case IDM_DIRONLY:
            panes = (int)wp;
            all_layout();
            save_settings();
            return 0;
        case IDM_NAME:
        case IDM_DETAILS:
            view_details = wp == IDM_DETAILS;
            all_layout();
            save_settings();
            return 0;
        case IDM_BYNAME:
        case IDM_BYTYPE:
        case IDM_BYSIZE:
        case IDM_BYDATE:
            sort_by = (int)wp;
            for (int i = 0; i < MAXWIN; i++)
                if (wins[i])
                    read_files(wins[i]);
            save_settings();
            update_status();
            return 0;
        case IDM_STATUS:
            show_status = !show_status;
            place_client();
            InvalidateRect(h, NULL, TRUE);
            save_settings();
            return 0;
        case IDM_NEWWIN:
            new_window(fw ? fw->path : start_path);
            return 0;
        case IDM_CASCADE: SendMessage(hclient, WM_MDICASCADE, 0, 0); return 0;
        case IDM_TILE:    SendMessage(hclient, WM_MDITILE, MDITILE_VERTICAL, 0); return 0;
        case IDM_ARRANGE: SendMessage(hclient, WM_MDIICONARRANGE, 0, 0); return 0;
        case IDM_REFRESH:
            read_drives();
            if (fw) {
                HCURSOR old = SetCursor(LoadCursor(0, IDC_WAIT));
                refresh(fw);
                SetCursor(old);
            }
            return 0;
        case IDM_ABOUT:
            MessageBox(h, "File Manager\nNative Windows 3.x API for murm386.", "About File Manager",
                       MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefFrameProc(h, hclient, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    hinst = inst;
    if (!prev) {
        WNDCLASS wc = { 0 };
        wc.lpfnWndProc = FrameProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
        wc.lpszClassName = "WFFrame";
        if (!RegisterClass(&wc))
            return 1;
        wc.lpfnWndProc = DirProc;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "WFDir";
        if (!RegisterClass(&wc))
            return 1;
        wc.style = CS_DBLCLKS;
        wc.hIcon = 0;
        wc.lpfnWndProc = PaneProc;
        wc.lpszClassName = "WFPane";
        if (!RegisterClass(&wc))
            return 1;
    }
    view_details = GetPrivateProfileInt("Settings", "Details", 0, INI) != 0;
    sort_by = IDM_BYNAME + (int)GetPrivateProfileInt("Settings", "Sort", 0, INI) % 4;
    show_status = GetPrivateProfileInt("Settings", "StatusBar", 1, INI) != 0;
    panes = IDM_TREEDIR + (int)GetPrivateProfileInt("Settings", "Panes", 0, INI) % 3;

    HDC dc = GetDC(0);
    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);
    ReleaseDC(0, dc);
    line_h = tm.tmHeight;
    char_w = tm.tmAveCharWidth;
    drive_h = line_h + 8;

    /* the start directory: the command line, or the current one */
    if (cmd && cmd[0]) {
        GetCurrentDirectoryN(start_path, MAXPATH);
        char t[MAXPATH];
        absolute(t, start_path, cmd);
        lstrcpy(start_path, t);
    } else {
        GetCurrentDirectoryN(start_path, MAXPATH);
    }
    AnsiUpper(start_path);
    read_drives();
    if (!is_dir(start_path))
        GetCurrentDirectoryN(start_path, MAXPATH);

    HMENU file = CreatePopupMenu();
    AppendMenu(file, MF_STRING, IDM_OPEN, "&Open\tEnter");
    AppendMenu(file, MF_STRING, IDM_MOVE, "&Move...\tF7");
    AppendMenu(file, MF_STRING, IDM_COPY, "&Copy...\tF8");
    AppendMenu(file, MF_STRING, IDM_DELETE, "&Delete...\tDel");
    AppendMenu(file, MF_STRING, IDM_RENAME, "Re&name...");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_MKDIR, "Cr&eate Directory...");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_RUN, "&Run...");
    AppendMenu(file, MF_SEPARATOR, 0, NULL);
    AppendMenu(file, MF_STRING, IDM_EXIT, "E&xit");
    HMENU disk = CreatePopupMenu();
    AppendMenu(disk, MF_STRING, IDM_DRIVE, "&Select Drive...");
    HMENU tree = CreatePopupMenu();
    AppendMenu(tree, MF_STRING, IDM_EXPONE, "E&xpand One Level\t+");
    AppendMenu(tree, MF_STRING, IDM_EXPBRANCH, "Expand &Branch\t*");
    AppendMenu(tree, MF_STRING, IDM_EXPALL, "Expand &All");
    AppendMenu(tree, MF_STRING, IDM_COLLAPSE, "&Collapse Branch\t-");
    HMENU view = CreatePopupMenu();
    AppendMenu(view, MF_STRING, IDM_TREEDIR, "T&ree and Directory");
    AppendMenu(view, MF_STRING, IDM_TREEONLY, "Tr&ee Only");
    AppendMenu(view, MF_STRING, IDM_DIRONLY, "Directory &Only");
    AppendMenu(view, MF_SEPARATOR, 0, NULL);
    AppendMenu(view, MF_STRING, IDM_NAME, "&Name");
    AppendMenu(view, MF_STRING, IDM_DETAILS, "&All File Details");
    AppendMenu(view, MF_SEPARATOR, 0, NULL);
    AppendMenu(view, MF_STRING, IDM_BYNAME, "&Sort by Name");
    AppendMenu(view, MF_STRING, IDM_BYTYPE, "Sort &by Type");
    AppendMenu(view, MF_STRING, IDM_BYSIZE, "Sort b&y Size");
    AppendMenu(view, MF_STRING, IDM_BYDATE, "Sort by &Date");
    HMENU opts = CreatePopupMenu();
    AppendMenu(opts, MF_STRING, IDM_STATUS, "&Status Bar");
    HMENU win = CreatePopupMenu();
    AppendMenu(win, MF_STRING, IDM_NEWWIN, "&New Window");
    AppendMenu(win, MF_STRING, IDM_CASCADE, "&Cascade\tShift+F5");
    AppendMenu(win, MF_STRING, IDM_TILE, "&Tile\tShift+F4");
    AppendMenu(win, MF_STRING, IDM_ARRANGE, "&Arrange Icons");
    AppendMenu(win, MF_STRING, IDM_REFRESH, "&Refresh\tF5");
    HMENU help = CreatePopupMenu();
    AppendMenu(help, MF_STRING, IDM_ABOUT, "&About File Manager...");
    HMENU bar = CreateMenu();
    AppendMenu(bar, MF_POPUP, (UINT)file, "&File");
    AppendMenu(bar, MF_POPUP, (UINT)disk, "&Disk");
    AppendMenu(bar, MF_POPUP, (UINT)tree, "&Tree");
    AppendMenu(bar, MF_POPUP, (UINT)view, "&View");
    AppendMenu(bar, MF_POPUP, (UINT)opts, "&Options");
    AppendMenu(bar, MF_POPUP, (UINT)win, "&Window");
    AppendMenu(bar, MF_POPUP, (UINT)help, "&Help");

    static const ACCEL_N keys[] = {
        { FVIRTKEY, VK_F7, IDM_MOVE },
        { FVIRTKEY, VK_F8, IDM_COPY },
        { FVIRTKEY, VK_DELETE, IDM_DELETE },
        { FVIRTKEY, VK_F5, IDM_REFRESH },
        { FVIRTKEY | FSHIFT, VK_F5, IDM_CASCADE },
        { FVIRTKEY | FSHIFT, VK_F4, IDM_TILE },
    };
    HACCEL acc = CreateAcceleratorTableN(keys, 6);

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindow("WFFrame", "File Manager", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          10, 10, sw - 20, sh - 40, 0, bar, inst, NULL);
    if (!h)
        return 1;
    ShowWindow(h, show);
    new_window(start_path);
    UpdateWindow(h);

    MSG m;
    while (GetMessage(&m, 0, 0, 0)) {
        if (m.message == WM_KEYDOWN && GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) >= 0 &&
            m.wParam >= 'A' && m.wParam <= 'Z') {
            fmwin *fw = active_win();
            if (fw && drive_type((int)(m.wParam - 'A'))) {
                select_drive(fw, (int)(m.wParam - 'A'));
                continue;
            }
        }
        if (TranslateMDISysAccel(hclient, &m) || TranslateAccelerator(h, acc, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    DestroyAcceleratorTableN(acc);
    return (int)m.wParam;
}
