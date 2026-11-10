/*
 * windll.c - native DLLs (ddi/windll.h): loaded at a task's first use of
 * one (a Win16 program's import of it, LoadLibrary), one instance for all
 * tasks, unloaded when the last task that used it lets it go
 * (FreeLibrary, its end). LoadLibrary, FreeLibrary, GetProcAddress and
 * GetModuleHandle of native callers find them too.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <string.h>
#include "wincore.h"
#include "winmod.h"
#include "windll.h"

#define DLL_SLOTS WIN_DLL_SLOTS
#define DLL_REFS  48

typedef struct {
    char name[9];
    const win_dll_info *info;
    void *raw;                      /* the image (on the target) */
    HANDLE h;                       /* its module handle */
    int users;                      /* the uses of all tasks */
    char path[128];
} dslot;

static dslot slots[DLL_SLOTS];
/* which task uses which DLL how often (a task's uses go at its end) */
static struct { HTASK task; uint8_t slot; uint16_t count; } refs[DLL_REFS];

/* the Win16 programs' help (ne_guest_sys.c); none without them */
__attribute__((weak)) const win_dll_ne *win_dll_ne_glue(void) { return NULL; }

#if defined(__arm__) || defined(__thumb__)
#define DLL_FILES 1
#else
/* the host: the DLLs built in (host_dlls.c), their help set here */
const win_dll_ne *__win_dll_ne;
extern const win_dll_builtin win_dll_builtins[] __attribute__((weak));
#endif

/* "c:\windows\system\lzexpand.dll" -> "LZEXPAND" */
static void module_name(const char *s, char *out)
{
    const char *base = s;
    for (const char *p = s; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    int n = 0;
    while (base[n] && base[n] != '.' && n < 8) {
        char c = base[n];
        out[n++] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    out[n] = 0;
}

int win_dll_find(const char *name)
{
    char m[9];
    module_name(name, m);
    for (int i = 0; m[0] && i < DLL_SLOTS; i++)
        if (slots[i].info && !strcmp(slots[i].name, m))
            return i;
    return -1;
}

#ifdef DLL_FILES
typedef const win_dll_info *(*dll_entry)(const win_dll_host *);

/* <NAME>.DLL in the Windows directory or the system one: a native DLL's
 * image, called; its info */
static const win_dll_info *load_file(const char *m, dslot *s)
{
    static const win_dll_host host = {
        WINDLL_MAGIC, WINDLL_VERSION, sizeof(win_dll_host), win_export_tables, NULL,
    };
    static win_dll_host with_ne;
    const win_dll_ne *ne = win_dll_ne_glue();
    if (!ne)
        return NULL;
    with_ne = host;
    with_ne.ne = ne;
    char file[16], dir[128];
    wsprintf(file, "%s.DLL", m);
    for (int pass = 0; pass < 2; pass++) {
        UINT n = pass ? GetSystemDirectory(dir, sizeof(dir)) : GetWindowsDirectory(dir, sizeof(dir));
        if (!n || n + 14 >= sizeof(dir))
            continue;
        wsprintf(s->path, dir[n - 1] == '\\' ? "%s%s" : "%s\\%s", dir, file);
        win_ez_image img;
        if (win_ez_load(s->path, &img))
            continue;               /* none, or Windows' own (an NE file) */
        uintptr_t e = (uintptr_t)img.entry & ~(uintptr_t)1;
        uint32_t sig = 0;
        if (!(e & 3) && e + 8 <= (uintptr_t)img.base + 0x100u + img.image_size)
            memcpy(&sig, (const void *)(e + 4), 4);
        const win_dll_info *info = NULL;
        if (sig == WINDLL_MAGIC)
            info = ((dll_entry)(uintptr_t)img.entry)(&with_ne);
        if (info && info->version == WINDLL_VERSION && info->size >= sizeof(win_dll_info) &&
            info->name) {
            char named[9];
            module_name(info->name, named);
            if (!strcmp(named, m)) {
                s->raw = img.raw;
                return info;
            }
        }
        win_free(img.raw);
    }
    return NULL;
}
#else
static const win_dll_info *load_file(const char *m, dslot *s)
{
    const win_dll_ne *ne = win_dll_ne_glue();
    if (!ne)
        return NULL;
    for (const win_dll_builtin *b = win_dll_builtins; b && b->name; b++)
        if (!lstrcmpi(b->name, m)) {
            __win_dll_ne = ne;
            const win_dll_info *info = b->init();
            if (!info || info->version != WINDLL_VERSION || info->size < sizeof(win_dll_info))
                return NULL;
            char dir[128];
            UINT n = GetWindowsDirectory(dir, sizeof(dir));
            wsprintf(s->path, n && dir[n - 1] == '\\' ? "%s%s.DLL" : "%s\\%s.DLL", dir, m);
            s->raw = NULL;
            return info;
        }
    return NULL;
}
#endif

/* a use of the current task's more */
static int ref(int slot)
{
    HTASK t = GetCurrentTask();
    int free_at = -1;
    for (int i = 0; i < DLL_REFS; i++) {
        if (refs[i].count && refs[i].task == t && refs[i].slot == slot) {
            refs[i].count++;
            slots[slot].users++;
            return 1;
        }
        if (!refs[i].count && free_at < 0)
            free_at = i;
    }
    if (free_at < 0)
        return 0;
    refs[free_at].task = t;
    refs[free_at].slot = (uint8_t)slot;
    refs[free_at].count = 1;
    slots[slot].users++;
    return 1;
}

static void unload(int slot)
{
    dslot *s = &slots[slot];
    if (s->info->term)
        s->info->term();
    if (s->h)
        win_handle_free(s->h);
    win_free(s->raw);
    memset(s, 0, sizeof(*s));
}

int win_dll_load(const char *name)
{
    int i = win_dll_find(name);
    if (i < 0) {
        char m[9];
        module_name(name, m);
        if (!m[0])
            return -1;
        for (i = 0; i < DLL_SLOTS && slots[i].info; i++)
            ;
        if (i == DLL_SLOTS)
            return -1;
        dslot *s = &slots[i];
        const win_dll_info *info = load_file(m, s);
        if (!info) {
            memset(s, 0, sizeof(*s));
            return -1;
        }
        lstrcpy(s->name, m);
        s->info = info;
        s->h = win_handle_alloc(WOBJ_MODULE, s);
    }
    if (!ref(i)) {
        if (!slots[i].users)
            unload(i);
        return -1;
    }
    return i;
}

void win_dll_unref(int slot)
{
    if (slot < 0 || slot >= DLL_SLOTS || !slots[slot].info)
        return;
    HTASK t = GetCurrentTask();
    for (int i = 0; i < DLL_REFS; i++)
        if (refs[i].count && refs[i].task == t && refs[i].slot == slot) {
            refs[i].count--;
            if (--slots[slot].users <= 0)
                unload(slot);
            return;
        }
}

void win_dll_task_end(HTASK task)
{
    for (int i = 0; i < DLL_SLOTS; i++)
        if (slots[i].info && slots[i].info->task_end)
            slots[i].info->task_end((uint16_t)task);
    for (int i = 0; i < DLL_REFS; i++)
        if (refs[i].count && refs[i].task == task) {
            int s = refs[i].slot;
            slots[s].users -= refs[i].count;
            refs[i].count = 0;
            if (slots[s].info && slots[s].users <= 0)
                unload(s);
        }
}

const win_dll_info *win_dll_get(int slot)
{
    return slot >= 0 && slot < DLL_SLOTS ? slots[slot].info : NULL;
}

int win_dll_of(HANDLE h)
{
    dslot *s = h ? (dslot *)win_handle_get(h, WOBJ_MODULE) : NULL;
    return s ? (int)(s - slots) : -1;
}

HANDLE win_dll_handle(int slot) { return win_dll_get(slot) ? slots[slot].h : 0; }

const char *win_dll_path(int slot) { return win_dll_get(slot) ? slots[slot].path : NULL; }

/* by its ordinal, or by its name when name is given */
const win_dll_export *win_dll_export_of(int slot, uint16_t ordinal, const char *name)
{
    const win_dll_info *d = win_dll_get(slot);
    for (uint32_t i = 0; d && i < d->export_count; i++) {
        const win_dll_export *e = &d->exports[i];
        if (name ? e->name && !lstrcmpi(e->name, name) : e->ordinal == ordinal)
            return e;
    }
    return NULL;
}

/* ---------------------------------------------------------------- KERNEL */

HINSTANCE WINAPI LoadLibrary(LPCSTR name)
{
    int s = name ? win_dll_load(name) : -1;
    return s >= 0 ? slots[s].h : 2;         /* file not found */
}

void WINAPI FreeLibrary(HINSTANCE h)
{
    win_dll_unref(win_dll_of(h));
}

/* a name, or an ordinal as MAKEINTRESOURCE */
FARPROC WINAPI GetProcAddress(HINSTANCE h, LPCSTR name)
{
    int s = win_dll_of(h);
    int by_ord = (uintptr_t)name < 0x10000u;
    const win_dll_export *e = s >= 0 ? win_dll_export_of(s, by_ord ? (uint16_t)(uintptr_t)name : 0,
                                                         by_ord ? NULL : name) : NULL;
    return e ? (FARPROC)(uintptr_t)e->native : (FARPROC)0;
}
