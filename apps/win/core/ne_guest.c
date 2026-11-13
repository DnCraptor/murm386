/*
 * ne_guest.c - stage relocatable Win16 NE segments in x86 guest memory.
 *
 * This layer does not touch CPU internals.  It creates 16-bit (D=0, G=0)
 * descriptors in a GDT image and emits guest x86 API thunks. The caller
 * remains the native WINN executor. Running the GDT is a separate step.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ne_guest.h"
#include "ne_boot.h"
#include "x86bridge.h"

#define GDT_ENTRY_BYTES 8u
#define GDT_THUNK_INDEX 1u
#define GDT_SEGMENT_INDEX(i) ((i) + 1u)
#define THUNK_BYTES WIN_NE_THUNK_BYTES

/* Never allow allocated API slots or the PM bootstrap to overlap. */
_Static_assert(WIN_NE_GUEST_ACTIVE_THUNKS * THUNK_BYTES <= WIN_NE_BOOT_OFFSET,
               "NE API thunks overlap the PM trampoline");
_Static_assert(WIN_NE_BOOT_OFFSET + WIN_NE_BOOT_CAPACITY <= WIN_NE_DOS_STUB_OFFSET,
               "NE PM trampoline overlaps the DOS stubs");
_Static_assert(WIN_NE_DOS_STUB_OFFSET + WIN_NE_DOS_STUB_BYTES <= WIN_NE_FAULT_IDT_OFFSET,
               "NE DOS stubs overlap the private IDT");
_Static_assert(WIN_NE_FAULT_IDT_OFFSET + WIN_NE_FAULT_IDT_BYTES <=
               WIN_NE_FAULT_CODE_OFFSET, "NE private IDT overlaps fault code");
_Static_assert(WIN_NE_FAULT_CODE_OFFSET + WIN_NE_FAULT_CODE_BYTES <= WIN_NE_FPMATH_OFFSET,
               "NE fault recovery overlaps __FPMATH");
_Static_assert(WIN_NE_FPMATH_OFFSET + WIN_NE_FPMATH_BYTES <= WIN_NE_RING0_OFFSET,
               "NE __FPMATH overlaps the ring 0 area");
_Static_assert(WIN_NE_RING0_OFFSET + WIN_NE_RING0_BYTES <= WIN_NE_THUNK_SEGMENT_BYTES,
               "NE ring 0 area exceeds the thunk segment");

/* the GDT after the globals: the ring 3 alias of the thunks, three call
 * gates, the TSS, the ring 0 area, the BIOS data (KERNEL's __0040H), the
 * first megabyte's segments KERNEL has selectors of (low_segments[]) */
#define GDT_EXTRA(g) ((uint32_t)(g)->info.segment_count + 3u + WIN_NE_GUEST_GLOBALS)
#define GDT_LOW 7u
#define GDT_EXTRAS (GDT_LOW + 9u)

/* KERNEL's __0000H, __A000H ...: 64K of the first megabyte each, read
 * only (a program writes to the screen through GDI) */
static const struct { uint16_t ordinal; const char *name; uint32_t base; } low_segments[9] = {
    { 183, "__0000H", 0x00000u }, { 174, "__A000H", 0xa0000u }, { 181, "__B000H", 0xb0000u },
    { 182, "__B800H", 0xb8000u }, { 195, "__C000H", 0xc0000u }, { 179, "__D000H", 0xd0000u },
    { 190, "__E000H", 0xe0000u }, { 194, "__F000H", 0xf0000u }, { 173, "__ROMBIOS", 0xf0000u },
};
/* then the DLLs' segments */
#define GDT_DLL(g) (GDT_EXTRA(g) + GDT_EXTRAS)

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* Access byte: present, the DPL, executable/readable or data/read-write
 * (or a system type). Flags: G=0 (byte granularity), D=0 (16-bit). */
static void seg_descriptor(uint8_t *p, uint32_t base, uint32_t size, uint8_t access)
{
    uint32_t limit = size - 1u;
    put16(p, (uint16_t)limit);
    put16(p + 2, (uint16_t)base);
    p[4] = (uint8_t)(base >> 16);
    p[5] = access;
    p[6] = (uint8_t)(limit >> 16) & 0x0fu;
    p[7] = (uint8_t)(base >> 24);
}

/* the program's own: DPL 3 */
static void descriptor(uint8_t *p, uint32_t base, uint32_t size, int code)
{
    seg_descriptor(p, base, size, code ? 0xfau : 0xf2u);
}

/* a 16-bit call gate, DPL 3, without parameters, to ring 0 code */
static void call_gate(uint8_t *p, uint16_t selector, uint16_t offset)
{
    put16(p, offset);
    put16(p + 2, selector);
    p[4] = 0;
    p[5] = 0xe4;
    put16(p + 6, 0);
}

static void lstrcpy_n(char *d, const char *s, size_t cap)
{
    size_t n = strlen(s);
    if (n >= cap)
        n = cap - 1;
    memcpy(d, s, n);
    d[n] = 0;
}

static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return 0;
    }
    return !*a && !*b;
}

void win_ne_guest_release(win_ne_guest_image *image)
{
    if (!image)
        return;
    if (image->io.free) {
        if (image->segments)
            for (uint16_t i = 1; i <= image->info.segment_count; i++)
                if (image->segments[i].linear &&
                    !(image->segments[i].flags & WIN_NE_GUEST_ALIAS))
                    image->io.free(image->opaque, image->segments[i].linear);
        if (image->thunk_linear)
            image->io.free(image->opaque, image->thunk_linear);
        if (image->gdt_linear)
            image->io.free(image->opaque, image->gdt_linear);
        if (image->psp.linear)
            image->io.free(image->opaque, image->psp.linear);
        for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++)
            if (image->globals[i].linear && !(image->globals[i].flags & WIN_NE_GUEST_ALIAS))
                image->io.free(image->opaque, image->globals[i].linear);
        for (unsigned i = 0; i < WIN_NE_GUEST_DLL_SEGMENTS; i++)
            if (image->dll_segments[i].linear)
                image->io.free(image->opaque, image->dll_segments[i].linear);
    }
    free(image->segments);
    free(image->gdt);
    memset(image, 0, sizeof(*image));
}

static int guest_alloc_in(win_ne_guest_image *g, uint32_t bytes, uint32_t *linear, int low)
{
    *linear = 0;
    int (*alloc)(void *, uint32_t, uint32_t *) =
        low && g->io.alloc_low ? g->io.alloc_low : g->io.alloc;
    if (!alloc(g->opaque, bytes, linear))
        return 0;
    if (low && *linear + bytes > 0x100000u) {
        g->io.free(g->opaque, *linear);
        *linear = 0;
        return 0;
    }
    if (!*linear || (*linear & 15u) || *linear > UINT32_MAX - bytes) {
        if (*linear)
            g->io.free(g->opaque, *linear);
        *linear = 0;
        return 0;
    }
    return 1;
}

/* the programs' segments: anywhere (the extended memory first) */
static int guest_alloc(win_ne_guest_image *g, uint32_t bytes, uint32_t *linear)
{
    return guest_alloc_in(g, bytes, linear, 0);
}

/* what loading a module (the program: dll -1; a DLL) has at hand */
typedef struct {
    win_ne_guest_image *g;
    int dll;
    int depth;                          /* DLLs loading DLLs */
} load_ctx;

static int assign_selector(void *opaque, uint16_t index, uint16_t flags,
                           uint32_t bytes, uint16_t *selector)
{
    load_ctx *lc = (load_ctx *)opaque;
    win_ne_guest_image *g = lc->g;
    if (lc->dll >= 0) {                 /* a DLL's: from the pool after the extras */
        win_ne_guest_dll *d = &g->dlls[lc->dll];
        if (!index || index > d->count || !bytes || bytes > 65536u)
            return 0;
        unsigned k = d->first + index - 1u;
        win_ne_guest_segment *s = &g->dll_segments[k];
        if (s->linear || !guest_alloc(g, bytes, &s->linear))
            return 0;
        s->size = bytes;
        s->flags = flags;
        s->selector = (uint16_t)((GDT_DLL(g) + k) * GDT_ENTRY_BYTES | 3u);
        *selector = s->selector;
        descriptor(g->gdt + (s->selector & ~7u), s->linear, s->size, !(flags & 1u));
        return 1;
    }
    if (!index || index > g->info.segment_count || bytes > 65536u)
        return 0;
    win_ne_guest_segment *s = &g->segments[index];
    if (!bytes) {                       /* its loader's: the selector, not present yet */
        if (!(g->info.module_flags & WIN_NE_SELF_LOADING) || index == 1)
            return 0;
        s->flags = flags & 0x0fffu;
        s->selector = (uint16_t)(GDT_SEGMENT_INDEX(index) * GDT_ENTRY_BYTES | 3u);
        *selector = s->selector;
        return 1;
    }
    if (s->linear || !guest_alloc(g, bytes, &s->linear))
        return 0;
    s->size = bytes;
    s->flags = flags;
    s->selector = (uint16_t)(GDT_SEGMENT_INDEX(index) * GDT_ENTRY_BYTES | 3u);
    *selector = s->selector;
    descriptor(g->gdt + (s->selector & ~7u), s->linear, s->size, !(flags & 1u));
    return 1;
}

/* The Win16 functions the guest can import: module, ordinal, name and the
 * bytes of PASCAL arguments its RETF releases (0 for the C ones: the caller
 * does). The service number, KERNEL ordinal n, USER 0x1000 + n, GDI
 * 0x3000 + n, lives entirely inside WINN; it is not a guest pointer or an
 * emulated IRQ. */
enum { M_KERNEL, M_USER, M_GDI, M_KEYBOARD, M_SHELL, M_WIN87EM, M_TOOLHELP, M_COUNT };
static const uint16_t module_base[] = { 0, 0x1000u, 0x3000u, WIN_NE_SERVICE_KEYBOARD,
                                        WIN_NE_SERVICE_SHELL, WIN_NE_SERVICE_WIN87EM,
                                        WIN_NE_SERVICE_TOOLHELP };
static const char *const module_names[M_COUNT] = { "KERNEL", "USER", "GDI", "KEYBOARD",
                                                   "SHELL", "WIN87EM", "TOOLHELP" };
static const struct {
    uint8_t module;                 /* M_* */
    uint16_t ordinal;
    const char *name;
    uint16_t args;
} services[] = {
    { M_KERNEL, 3, "GetVersion", 0 },
    /* DOS: its INT 21h in the thunk itself (ne_guest_int21.c) */
    { M_KERNEL, 102, "DOS3Call", 0 },
    { M_KERNEL, 101, "NoHookDosCall", 0 },
    { M_KERNEL, 74, "OpenFile", 10 },
    { M_KERNEL, 131, "GetDOSEnvironment", 0 },
    { M_KERNEL, 207, "IsDBCSLeadByte", 2 },
    { M_KEYBOARD, 1, "Inquire", 4 },
    { M_KEYBOARD, 2, "Enable", 8 },
    { M_KEYBOARD, 3, "Disable", 0 },
    { M_KEYBOARD, 4, "ToAscii", 14 },
    { M_KEYBOARD, 5, "AnsiToOem", 8 },
    { M_KEYBOARD, 6, "OemToAnsi", 8 },
    { M_KEYBOARD, 7, "SetSpeed", 2 },
    { M_KEYBOARD, 100, "ScreenSwitchEnable", 2 },
    { M_KEYBOARD, 126, "GetTableSeg", 0 },
    { M_KEYBOARD, 127, "NewTable", 0 },
    { M_KEYBOARD, 128, "OemKeyScan", 2 },
    { M_KEYBOARD, 129, "VkKeyScan", 2 },
    { M_KEYBOARD, 130, "GetKeyboardType", 2 },
    { M_KEYBOARD, 131, "MapVirtualKey", 4 },
    { M_KEYBOARD, 132, "GetKBCodePage", 0 },
    { M_KEYBOARD, 134, "AnsiToOemBuff", 10 },
    { M_KEYBOARD, 135, "OemToAnsiBuff", 10 },
    { M_KEYBOARD, 133, "GetKeyNameText", 10 },
    { M_KEYBOARD, 136, "EnableKBSysReq", 2 },
    { M_KEYBOARD, 137, "GetBIOSKeyProc", 0 },
    { M_KERNEL, 30, "WaitEvent", 2 },
    { M_KERNEL, 36, "GetCurrentTask", 0 },
    { M_KERNEL, 49, "GetModuleFileName", 8 },
    { M_KERNEL, 88, "lstrcpy", 8 },
    { M_KERNEL, 89, "lstrcat", 8 },
    { M_KERNEL, 90, "lstrlen", 4 },
    { M_KERNEL, 91, "InitTask", 0 },
    { M_KERNEL, 152, "GetNumTasks", 0 },
    { M_KERNEL, 51, "MakeProcInstance", 6 },
    { M_KERNEL, 52, "FreeProcInstance", 4 },
    { M_USER, 1, "MessageBox", 12 },
    { M_USER, 5, "InitApp", 2 },
    { M_USER, 13, "GetTickCount", 0 },
    { M_USER, 179, "GetSystemMetrics", 2 },
    { M_USER, 180, "GetSysColor", 2 },
    { M_USER, 6, "PostQuitMessage", 2 },
    { M_USER, 39, "BeginPaint", 6 },
    { M_USER, 40, "EndPaint", 6 },
    { M_USER, 41, "CreateWindow", 30 },
    { M_USER, 42, "ShowWindow", 4 },
    { M_USER, 53, "DestroyWindow", 2 },
    { M_USER, 57, "RegisterClass", 4 },
    { M_USER, 107, "DefWindowProc", 10 },
    { M_USER, 108, "GetMessage", 10 },
    { M_USER, 113, "TranslateMessage", 4 },
    { M_USER, 114, "DispatchMessage", 4 },
    { M_USER, 124, "UpdateWindow", 2 },
    { M_USER, 125, "InvalidateRect", 8 },
    { M_USER, 173, "LoadCursor", 6 },
    { M_USER, 174, "LoadIcon", 6 },
    { M_USER, 175, "LoadBitmap", 6 },
    { M_USER, 196, "TabbedTextOut", 20 },
    { M_USER, 197, "GetTabbedTextExtent", 14 },
    { M_USER, 404, "GetClassInfo", 10 },
    { M_USER, 458, "DestroyCursor", 2 },
    { M_KERNEL, 349, "_hread", 10 },
    { M_KERNEL, 350, "_hwrite", 10 },
    { M_GDI, 48, "CreateBitmap", 12 },
    { M_GDI, 49, "CreateBitmapIndirect", 4 },
    { M_GDI, 74, "GetBitmapBits", 10 },
    { M_GDI, 92, "GetTextFace", 8 },
    { M_GDI, 104, "RectVisibleOld", 6 },
    { M_GDI, 106, "SetBitmapBits", 10 },
    { M_GDI, 351, "ExtTextOut", 22 },
    { M_GDI, 471, "GetTextExtentPoint", 12 },
    /* KERNEL: modules, resources, Catch/Throw, the rest (ne_guest_kernel.c) */
    { M_KERNEL, 25, "GlobalCompact", 4 },
    { M_KERNEL, 37, "GetCurrentPDB", 0 },
    { M_KERNEL, 47, "GetModuleHandle", 4 },
    { M_KERNEL, 48, "GetModuleUsage", 2 },
    { M_KERNEL, 50, "GetProcAddress", 6 },
    { M_KERNEL, 54, "GetInstanceData", 6 },
    { M_KERNEL, 55, "Catch", 4 },
    { M_KERNEL, 56, "Throw", 6 },
    { M_KERNEL, 60, "FindResource", 10 },
    { M_KERNEL, 61, "LoadResource", 4 },
    { M_KERNEL, 62, "LockResource", 2 },
    { M_KERNEL, 63, "FreeResource", 2 },
    { M_KERNEL, 65, "SizeofResource", 4 },
    { M_KERNEL, 92, "GetTempDrive", 2 },
    { M_KERNEL, 95, "LoadLibrary", 4 },
    { M_KERNEL, 96, "FreeLibrary", 2 },
    { M_KERNEL, 97, "GetTempFileName", 10 },
    { M_KERNEL, 107, "SetErrorMode", 2 },
    { M_KERNEL, 132, "GetWinFlags", 0 },
    { M_KERNEL, 133, "GetExePtr", 2 },
    { M_KERNEL, 137, "FatalAppExit", 6 },
    { M_KERNEL, 155, "GetTaskDS", 0 },
    { M_KERNEL, 166, "WinExec", 6 },
    { M_KERNEL, 169, "GetFreeSpace", 2 },
    /* KERNEL: memory, selectors, pointers, strings, the rest (ne_guest_kernel.c,
     * the local and global heaps' in ne_guest_call.c) */
    { M_KERNEL, 1, "FatalExit", 2 },
    { M_KERNEL, 11, "LocalHandle", 2 },
    { M_KERNEL, 12, "LocalFlags", 2 },
    { M_KERNEL, 13, "LocalCompact", 2 },
    { M_KERNEL, 29, "Yield", 0 },
    { M_KERNEL, 105, "GetExeVersion", 0 },
    { M_KERNEL, 111, "GlobalWire", 2 },
    { M_KERNEL, 112, "GlobalUnWire", 2 },
    { M_KERNEL, 121, "LocalShrink", 4 },
    { M_KERNEL, 161, "LocalCountFree", 0 },
    { M_KERNEL, 310, "LocalHandleDelta", 2 },
    { M_KERNEL, 162, "LocalHeapSize", 0 },
    { M_KERNEL, 167, "GetExpWinVer", 2 },
    { M_KERNEL, 170, "AllocCStoDSAlias", 2 },
    { M_KERNEL, 171, "AllocDStoCSAlias", 2 },
    { M_KERNEL, 175, "AllocSelector", 2 },
    { M_KERNEL, 176, "FreeSelector", 2 },
    { M_KERNEL, 177, "PrestoChangoSelector", 4 },
    { M_KERNEL, 184, "GlobalDOSAlloc", 4 },
    { M_KERNEL, 185, "GlobalDOSFree", 2 },
    { M_KERNEL, 186, "GetSelectorBase", 2 },
    { M_KERNEL, 187, "SetSelectorBase", 6 },
    { M_KERNEL, 188, "GetSelectorLimit", 2 },
    { M_KERNEL, 189, "SetSelectorLimit", 6 },
    { M_KERNEL, 191, "GlobalPageLock", 2 },
    { M_KERNEL, 192, "GlobalPageUnlock", 2 },
    { M_KERNEL, 197, "GlobalFix", 2 },
    { M_KERNEL, 198, "GlobalUnfix", 2 },
    { M_KERNEL, 199, "SetHandleCount", 2 },
    { M_KERNEL, 203, "DebugBreak", 0 },
    { M_KERNEL, 334, "IsBadReadPtr", 6 },
    { M_KERNEL, 335, "IsBadWritePtr", 6 },
    { M_KERNEL, 336, "IsBadCodePtr", 4 },
    { M_KERNEL, 337, "IsBadStringPtr", 6 },
    { M_KERNEL, 346, "IsBadHugeReadPtr", 8 },
    { M_KERNEL, 347, "IsBadHugeWritePtr", 8 },
    { M_KERNEL, 348, "hmemcpy", 12 },
    { M_KERNEL, 353, "lstrcpyn", 10 },
    { M_KERNEL, 14, "LocalNotify", 4 },
    { M_KERNEL, 45, "LoadModule", 8 },
    { M_KERNEL, 46, "FreeModule", 2 },
    { M_KERNEL, 93, "GetCodeHandle", 4 },
    { M_KERNEL, 98, "GetLastDiskChange", 0 },
    { M_KERNEL, 138, "GetHeapSpaces", 2 },
    { M_KERNEL, 150, "DirectedYield", 2 },
    { M_KERNEL, 154, "GlobalNotify", 4 },
    { M_KERNEL, 159, "GlobalHandleNoRIP", 2 },
    { M_KERNEL, 196, "SelectorAccessRights", 6 },
    { M_KERNEL, 206, "AllocSelectorArray", 2 },
    { M_KERNEL, 316, "GetFreeMemInfo", 0 },
    { M_KERNEL, 352, "lstrcatn", 10 },
    /* USER: dialogs, classes, the keyboard, hooks and the rest (ne_guest_user.c,
     * ne_guest_call.c) */
    { M_USER, 123, "CallMsgFilter", 6 },
    { M_USER, 138, "CloseClipboard", 0 },
    { M_USER, 139, "EmptyClipboard", 0 },
    { M_USER, 141, "SetClipboardData", 4 },
    { M_USER, 142, "GetClipboardData", 2 },
    { M_USER, 121, "SetWindowsHook", 6 },
    { M_USER, 130, "SetClassWord", 6 },
    { M_USER, 132, "SetClassLong", 8 },
    { M_USER, 185, "GrayString", 22 },
    /* the serial and parallel ports: none (ne_guest_kernel.c) */
    { M_USER, 200, "OpenComm", 8 },
    { M_USER, 201, "SetCommState", 4 },
    { M_USER, 202, "GetCommState", 6 },
    { M_USER, 203, "GetCommError", 6 },
    { M_USER, 204, "ReadComm", 8 },
    { M_USER, 205, "WriteComm", 8 },
    { M_USER, 206, "TransmitCommChar", 4 },
    { M_USER, 207, "CloseComm", 2 },
    { M_USER, 208, "SetCommEventMask", 4 },
    { M_USER, 209, "GetCommEventMask", 4 },
    { M_USER, 210, "SetCommBreak", 2 },
    { M_USER, 211, "ClearCommBreak", 2 },
    { M_USER, 212, "UngetCommChar", 4 },
    { M_USER, 213, "BuildCommDCB", 8 },
    { M_USER, 214, "EscapeCommFunction", 4 },
    { M_USER, 215, "FlushComm", 4 },
    { M_USER, 245, "EnableCommNotification", 8 },
    /* metafiles: their bits in global blocks, their records to a procedure */
    { M_GDI, 159, "GetMetaFileBits", 2 },
    { M_GDI, 160, "SetMetaFileBits", 2 },
    { M_GDI, 196, "SetMetaFileBitsBetter", 2 },
    { M_GDI, 175, "EnumMetaFile", 12 },
    { M_GDI, 176, "PlayMetaFileRecord", 12 },
    /* Windows' own DLLs, WINN's (ne_guest_sys.c) */
    { M_TOOLHELP, 50, "GlobalHandleToSel", 2 },
    { M_TOOLHELP, 51, "GlobalFirst", 6 },
    { M_TOOLHELP, 52, "GlobalNext", 6 },
    { M_TOOLHELP, 53, "GlobalInfo", 4 },
    { M_TOOLHELP, 54, "GlobalEntryHandle", 6 },
    { M_TOOLHELP, 55, "GlobalEntryModule", 8 },
    { M_TOOLHELP, 56, "LocalInfo", 6 },
    { M_TOOLHELP, 57, "LocalFirst", 6 },
    { M_TOOLHELP, 58, "LocalNext", 4 },
    { M_TOOLHELP, 59, "ModuleFirst", 4 },
    { M_TOOLHELP, 60, "ModuleNext", 4 },
    { M_TOOLHELP, 61, "ModuleFindName", 8 },
    { M_TOOLHELP, 62, "ModuleFindHandle", 6 },
    { M_TOOLHELP, 63, "TaskFirst", 4 },
    { M_TOOLHELP, 64, "TaskNext", 4 },
    { M_TOOLHELP, 65, "TaskFindHandle", 6 },
    { M_TOOLHELP, 66, "StackTraceFirst", 6 },
    { M_TOOLHELP, 67, "StackTraceCSIPFirst", 12 },
    { M_TOOLHELP, 68, "StackTraceNext", 4 },
    { M_TOOLHELP, 69, "ClassFirst", 4 },
    { M_TOOLHELP, 70, "ClassNext", 4 },
    { M_TOOLHELP, 71, "SystemHeapInfo", 4 },
    { M_TOOLHELP, 72, "MemManInfo", 4 },
    { M_TOOLHELP, 73, "NotifyRegister", 8 },
    { M_TOOLHELP, 74, "NotifyUnregister", 2 },
    { M_TOOLHELP, 75, "InterruptRegister", 6 },
    { M_TOOLHELP, 76, "InterruptUnRegister", 2 },
    { M_TOOLHELP, 77, "TerminateApp", 4 },
    { M_TOOLHELP, 78, "MemoryRead", 14 },
    { M_TOOLHELP, 79, "MemoryWrite", 14 },
    { M_TOOLHELP, 80, "TimerCount", 4 },
    { M_TOOLHELP, 84, "Local32Info", 6 },
    { M_TOOLHELP, 85, "Local32First", 6 },
    { M_TOOLHELP, 86, "Local32Next", 4 },
    { M_USER, 218, "DialogBoxIndirect", 10 },
    { M_USER, 219, "CreateDialogIndirect", 12 },
    { M_USER, 222, "GetKeyboardState", 4 },
    { M_USER, 223, "SetKeyboardState", 4 },
    { M_USER, 225, "EnumTaskWindows", 10 },
    { M_USER, 234, "UnhookWindowsHook", 6 },
    { M_USER, 235, "DefHookProc", 12 },
    { M_USER, 239, "DialogBoxParam", 16 },
    { M_USER, 240, "DialogBoxIndirectParam", 14 },
    { M_USER, 241, "CreateDialogParam", 16 },
    { M_USER, 242, "CreateDialogIndirectParam", 16 },
    { M_USER, 258, "MapWindowPoints", 10 },
    { M_USER, 291, "SetWindowsHookEx", 10 },
    { M_USER, 292, "UnhookWindowsHookEx", 4 },
    { M_USER, 293, "CallNextHookEx", 12 },
    { M_USER, 437, "AnsiUpperBuff", 6 },
    { M_USER, 438, "AnsiLowerBuff", 6 },
    { M_USER, 452, "CreateWindowEx", 34 },
    { M_USER, 27, "EnumProps", 6 },
    { M_USER, 99, "DlgDirSelect", 8 },
    { M_USER, 100, "DlgDirList", 12 },
    { M_USER, 194, "DlgDirSelectComboBox", 8 },
    { M_USER, 195, "DlgDirListComboBox", 12 },
    { M_USER, 220, "LoadMenuIndirect", 4 },
    { M_USER, 370, "GetWindowPlacement", 6 },
    { M_USER, 371, "SetWindowPlacement", 6 },
    { M_USER, 406, "CreateCursor", 18 },
    { M_USER, 407, "CreateIcon", 18 },
    { M_USER, 181, "SetSysColors", 10 },
    { M_USER, 284, "GetFreeSystemResources", 2 },
    { M_USER, 451, "TranslateMDISysAccel", 6 },
    /* GDI: palettes, a DC, Escape, the points of LPtoDP and DPtoLP, the
     * arrays, callbacks and DIBs (ne_guest_kernel.c); no printers */
    { M_GDI, 38, "Escape", 12 },
    { M_GDI, 53, "CreateDC", 16 },
    { M_GDI, 67, "DPtoLP", 8 },
    { M_GDI, 70, "EnumFonts", 14 },
    { M_GDI, 71, "EnumObjects", 12 },
    { M_GDI, 307, "GetCharABCWidths", 10 },
    { M_GDI, 308, "GetOutlineTextMetrics", 8 },
    { M_GDI, 309, "GetGlyphOutline", 22 },
    { M_GDI, 311, "GetFontData", 18 },
    { M_GDI, 332, "GetKerningPairs", 8 },
    { M_GDI, 367, "AnimatePalette", 10 },
    { M_GDI, 376, "ResetDC", 6 },
    { M_USER, 11, "SetSystemTimer", 10 },
    { M_USER, 402, "GetPriorityClipboardFormat", 6 },
    { M_USER, 465, "DragDetect", 6 },
    { M_GDI, 99, "LPtoDP", 8 },
    { M_GDI, 100, "LineDDA", 16 },
    { M_GDI, 153, "CreateIC", 16 },
    { M_GDI, 155, "QueryAbort", 4 },
    { M_GDI, 330, "EnumFontFamilies", 14 },
    { M_GDI, 350, "GetCharWidth", 10 },
    { M_GDI, 377, "StartDoc", 6 },
    { M_GDI, 378, "EndDoc", 2 },
    { M_GDI, 379, "StartPage", 2 },
    { M_GDI, 380, "EndPage", 2 },
    { M_GDI, 381, "SetAbortProc", 6 },
    { M_GDI, 382, "AbortDoc", 2 },
    { M_GDI, 439, "StretchDIBits", 32 },
    { M_GDI, 440, "SetDIBits", 18 },
    { M_GDI, 441, "GetDIBits", 18 },
    { M_GDI, 442, "CreateDIBitmap", 20 },
    { M_GDI, 443, "SetDIBitsToDevice", 28 },
    { M_GDI, 445, "CreateDIBPatternBrush", 4 },
    { M_GDI, 450, "PolyPolygon", 12 },
    { M_GDI, 451, "CreatePolyPolygonRgn", 12 },
    { M_GDI, 360, "CreatePalette", 4 },
    { M_GDI, 363, "GetPaletteEntries", 10 },
    { M_GDI, 364, "SetPaletteEntries", 10 },
    { M_GDI, 375, "GetSystemPaletteEntries", 10 },
    { M_SHELL, 1, "RegOpenKey", 12 },
    { M_SHELL, 2, "RegCreateKey", 12 },
    { M_SHELL, 3, "RegCloseKey", 4 },
    { M_SHELL, 5, "RegSetValue", 20 },
    { M_SHELL, 6, "RegQueryValue", 16 },
    { M_SHELL, 9, "DragAcceptFiles", 4 },
    { M_SHELL, 11, "DragQueryFile", 10 },
    { M_SHELL, 12, "DragFinish", 2 },
    { M_SHELL, 20, "ShellExecute", 20 },
    { M_SHELL, 21, "FindExecutable", 12 },
    { M_SHELL, 22, "ShellAbout", 12 },
    { M_SHELL, 34, "ExtractIcon", 8 },
    { M_WIN87EM, 1, "__FPMATH", 0 },
    { M_WIN87EM, 3, "__WinEm87Info", 6 },
    { M_WIN87EM, 4, "__WinEm87Restore", 6 },
    { M_WIN87EM, 5, "__WinEm87Save", 6 },
    { M_USER, 420, "wsprintf", 0 },
    { M_USER, 87, "DialogBox", 12 },
    { M_USER, 88, "EndDialog", 4 },
    { M_USER, 91, "GetDlgItem", 4 },
    { M_USER, 92, "SetDlgItemText", 8 },
    { M_USER, 93, "GetDlgItemText", 10 },
    { M_USER, 94, "SetDlgItemInt", 8 },
    { M_USER, 95, "GetDlgItemInt", 10 },
    { M_USER, 150, "LoadMenu", 6 },
    { M_USER, 176, "LoadString", 10 },
    { M_USER, 177, "LoadAccelerators", 6 },
    { M_USER, 178, "TranslateAccelerator", 8 },
    { M_GDI, 33, "TextOut", 12 },
    { M_GDI, 87, "GetStockObject", 2 },
    /* ne_guest_call.c */
    { M_KERNEL, 4, "LocalInit", 6 },
    { M_KERNEL, 5, "LocalAlloc", 4 },
    { M_KERNEL, 6, "LocalReAlloc", 6 },
    { M_KERNEL, 7, "LocalFree", 2 },
    { M_KERNEL, 8, "LocalLock", 2 },
    { M_KERNEL, 9, "LocalUnlock", 2 },
    { M_KERNEL, 10, "LocalSize", 2 },
    { M_KERNEL, 15, "GlobalAlloc", 6 },
    { M_KERNEL, 16, "GlobalReAlloc", 8 },
    { M_KERNEL, 17, "GlobalFree", 2 },
    { M_KERNEL, 18, "GlobalLock", 2 },
    { M_KERNEL, 19, "GlobalUnlock", 2 },
    { M_KERNEL, 20, "GlobalSize", 2 },
    { M_KERNEL, 21, "GlobalHandle", 2 },
    { M_KERNEL, 22, "GlobalFlags", 2 },
    { M_KERNEL, 23, "LockSegment", 2 },
    { M_KERNEL, 24, "UnlockSegment", 2 },
    { M_USER, 10, "SetTimer", 10 },
    { M_USER, 12, "KillTimer", 4 },
    { M_USER, 30, "WindowFromPoint", 4 },
    { M_USER, 54, "EnumWindows", 8 },
    { M_USER, 55, "EnumChildWindows", 10 },
    { M_USER, 76, "PtInRect", 8 },
    { M_USER, 89, "CreateDialog", 12 },
    { M_USER, 90, "IsDialogMessage", 6 },
    { M_USER, 101, "SendDlgItemMessage", 12 },
    { M_USER, 109, "PeekMessage", 12 },
    { M_USER, 110, "PostMessage", 10 },
    { M_USER, 111, "SendMessage", 10 },
    { M_USER, 122, "CallWindowProc", 14 },
    { M_USER, 129, "GetClassWord", 4 },
    { M_USER, 131, "GetClassLong", 4 },
    { M_USER, 133, "GetWindowWord", 4 },
    { M_USER, 134, "SetWindowWord", 6 },
    { M_USER, 135, "GetWindowLong", 4 },
    { M_USER, 136, "SetWindowLong", 8 },
    { M_USER, 191, "ChildWindowFromPoint", 6 },
    { M_USER, 308, "DefDlgProc", 10 },
    { M_USER, 421, "wvsprintf", 12 },
    { M_USER, 431, "AnsiUpper", 4 },
    { M_USER, 432, "AnsiLower", 4 },
    { M_USER, 472, "AnsiNext", 4 },
    { M_USER, 473, "AnsiPrev", 8 },
    { M_GDI, 50, "CreateBrushIndirect", 4 },
    { M_GDI, 57, "CreateFontIndirect", 4 },
    { M_GDI, 62, "CreatePenIndirect", 4 },
    { M_GDI, 82, "GetObject", 8 },
    { M_GDI, 93, "GetTextMetrics", 6 },
    /* the rest of Windows 3.1's (ne_guest_more.c; AllocResource, DirectResAlloc,
     * AccessResource, SetResourceHandler, AllocAlias: ne_guest_kernel.c) */
    { M_KERNEL, 26, "GlobalFreeAll", 2 },
    { M_KERNEL, 27, "GetModuleName", 8 },
    { M_KERNEL, 28, "GlobalMasterHandle", 0 },
    { M_KERNEL, 38, "SetTaskSignalProc", 6 },
    { M_KERNEL, 64, "AccessResource", 4 },
    { M_KERNEL, 66, "AllocResource", 8 },
    { M_KERNEL, 67, "SetResourceHandler", 10 },
    { M_KERNEL, 99, "GetLPErrMode", 0 },
    { M_KERNEL, 103, "NetBIOSCall", 0 },
    { M_KERNEL, 104, "GetCodeInfo", 8 },
    { M_KERNEL, 110, "PatchCodeHandle", 2 },
    { M_KERNEL, 118, "GetTaskQueueDS", 0 },
    { M_KERNEL, 119, "GetTaskQueueES", 0 },
    { M_KERNEL, 130, "FileCDR", 4 },
    { M_KERNEL, 140, "SetSigHandler", 16 },
    { M_KERNEL, 157, "GetCurPID", 4 },
    { M_KERNEL, 168, "DirectResAlloc", 6 },
    { M_KERNEL, 172, "AllocAlias", 2 },
    { M_KERNEL, 180, "LongPtrAdd", 8 },
    { M_KERNEL, 216, "RegEnumKey", 16 },
    { M_KERNEL, 217, "RegOpenKey", 12 },
    { M_KERNEL, 218, "RegCreateKey", 12 },
    { M_KERNEL, 219, "RegDeleteKey", 8 },
    { M_KERNEL, 220, "RegCloseKey", 4 },
    { M_KERNEL, 221, "RegSetValue", 20 },
    { M_KERNEL, 224, "RegQueryValue", 16 },
    { M_KERNEL, 227, "RegFlushKey", 4 },
    { M_KERNEL, 311, "GetSetKernelDOSProc", 4 },
    { M_KERNEL, 318, "FatalExitHook", 4 },
    { M_KERNEL, 324, "LogError", 6 },
    { M_KERNEL, 325, "LogParamError", 10 },
    { M_KERNEL, 338, "HasGPHandler", 4 },
    { M_KERNEL, 341, "ToolHelpHook", 4 },
    { M_KERNEL, 355, "GetWinDebugInfo", 6 },
    { M_KERNEL, 356, "SetWinDebugInfo", 4 },
    { M_USER, 184, "QuerySendMessage", 10 },
    { M_USER, 216, "UserSeeUserDo", 8 },
    { M_USER, 251, "SendDriverMessage", 12 },
    { M_USER, 252, "OpenDriver", 12 },
    { M_USER, 253, "CloseDriver", 10 },
    { M_USER, 254, "GetDriverModuleHandle", 2 },
    { M_USER, 255, "DefDriverProc", 16 },
    { M_USER, 256, "GetDriverInfo", 6 },
    { M_USER, 257, "GetNextDriver", 6 },
    { M_USER, 273, "ControlPanelInfo", 8 },
    { M_USER, 289, "keybd_event", 0 },
    { M_USER, 299, "mouse_event", 0 },
    { M_USER, 314, "SignalProc", 10 },
    { M_USER, 321, "SetEventHook", 4 },
    { M_USER, 337, "GetMouseEventProc", 0 },
    { M_USER, 483, "SystemParametersInfo", 10 },
    { M_USER, 285, "SetDeskWallPaper", 4 },
    /* Windows 95's for Win16 programs, the stack switched, GDI's with
     * pointers (ne_guest_api.c, ne_guest_more.c) */
    { M_KERNEL, 108, "SwitchStackTo", 6 },
    { M_KERNEL, 109, "SwitchStackBack", 0 },
    { M_KERNEL, 144, "CreateDirectory", 8 },
    { M_KERNEL, 145, "RemoveDirectory", 4 },
    { M_KERNEL, 146, "DeleteFile", 4 },
    { M_KERNEL, 149, "GetVersionEx", 4 },
    { M_KERNEL, 120, "UndefDynLink", 0 },
    /* a self-loading program's loader's (ne_guest_kernel.c) */
    { M_KERNEL, 403, "FarSetOwner", 4 },
    { M_KERNEL, 404, "FarGetOwner", 2 },
    { M_KERNEL, 667, "EntryAddrProc", 4 },
    { M_KERNEL, 668, "MyAlloc", 6 },
    { M_KERNEL, 222, "RegDeleteValue", 8 },
    { M_KERNEL, 223, "RegEnumValue", 32 },
    { M_KERNEL, 225, "RegQueryValueEx", 24 },
    { M_KERNEL, 226, "RegSetValueEx", 24 },
    { M_USER, 375, "DrawTextEx", 18 },
    { M_USER, 389, "LoadImage", 12 },
    { M_USER, 395, "GetIconInfo", 6 },
    { M_USER, 397, "RegisterClassEx", 4 },
    { M_USER, 398, "GetClassInfoEx", 10 },
    { M_USER, 399, "ChildWindowFromPointEx", 8 },
    { M_GDI, 188, "GetTextExtentEx", 22 },
    { M_GDI, 191, "GetDCHook", 6 },
    { M_USER, 501, "WNetOpenJob", 14 },
    { M_USER, 502, "WNetCloseJob", 10 },
    { M_USER, 503, "WNetAbortJob", 6 },
    { M_USER, 504, "WNetHoldJob", 6 },
    { M_USER, 505, "WNetReleaseJob", 6 },
    { M_USER, 506, "WNetCancelJob", 6 },
    { M_USER, 507, "WNetSetJobCopies", 8 },
    { M_USER, 508, "WNetWatchQueue", 12 },
    { M_USER, 509, "WNetUnwatchQueue", 4 },
    { M_USER, 510, "WNetLockQueueData", 12 },
    { M_USER, 511, "WNetUnlockQueueData", 4 },
    { M_USER, 512, "WNetGetConnection", 12 },
    { M_USER, 513, "WNetGetCaps", 2 },
    { M_USER, 514, "WNetDeviceMode", 2 },
    { M_USER, 515, "WNetBrowseDialog", 8 },
    { M_USER, 516, "WNetGetUser", 8 },
    { M_USER, 517, "WNetAddConnection", 12 },
    { M_USER, 518, "WNetCancelConnection", 6 },
    { M_USER, 519, "WNetGetError", 4 },
    { M_USER, 520, "WNetGetErrorText", 10 },
    { M_USER, 523, "WNetRestoreConnection", 6 },
    { M_USER, 524, "WNetWriteJob", 10 },
    { M_USER, 525, "WNetConnectDialog", 4 },
    { M_USER, 526, "WNetDisconnectDialog", 4 },
    { M_USER, 527, "WNetConnectionDialog", 4 },
    { M_USER, 528, "WNetViewQueueDialog", 6 },
    { M_USER, 529, "WNetPropertyDialog", 12 },
    { M_USER, 530, "WNetGetDirectoryType", 8 },
    { M_USER, 531, "WNetDirectoryNotify", 8 },
    { M_USER, 532, "WNetGetPropertyText", 16 },
    { M_SHELL, 4, "RegDeleteKey", 8 },
    { M_SHELL, 7, "RegEnumKey", 16 },
    { M_SHELL, 13, "DragQueryPoint", 6 },
    { M_SHELL, 36, "ExtractAssociatedIcon", 10 },
    { M_SHELL, 37, "DoEnvironmentSubst", 6 },
    { M_SHELL, 38, "FindEnvironmentString", 4 },
    { M_SHELL, 102, "RegisterShellHook", 4 },
    { M_SHELL, 262, "DriveType", 4 },
    { M_TOOLHELP, 81, "TaskSetCSIP", 6 },
    { M_TOOLHELP, 82, "TaskGetCSIP", 2 },
    { M_TOOLHELP, 83, "TaskSwitch", 6 },
};

/* the generic ones (sdk/gen_ne_sigs.py) */
static const struct {
    uint8_t module;
    uint16_t ordinal;
    const char *name;
    const char *sig;
} generic[] = {
#include "ne_sigs.h"
};

/* the bytes of PASCAL arguments of a signature's codes */
static uint16_t sig_args(const char *sig)
{
    uint16_t n = 0;
    for (const char *p = strchr(sig, ':') + 1; *p; p++)
        n = (uint16_t)(n + (strchr("wiI", *p) ? 2 : 4));
    return n;
}

static int generic_index(int module, const win_ne_import *imp)
{
    for (int i = 0; i < (int)(sizeof(generic) / sizeof(generic[0])); i++)
        if (generic[i].module == module &&
            (imp->ordinal ? imp->ordinal == generic[i].ordinal && !imp->procedure[0]
                          : name_eq(imp->procedure, generic[i].name)))
            return i;
    return -1;
}

const char *win_ne_service_name(uint16_t service)
{
    for (unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++)
        if (module_base[services[i].module] + services[i].ordinal == service)
            return services[i].name;
    for (unsigned i = 0; i < sizeof(generic) / sizeof(generic[0]); i++)
        if (module_base[generic[i].module] + generic[i].ordinal == service)
            return generic[i].name;
    return service == WIN_NE_SERVICE_INT21 ? "INT 21h" : NULL;
}

const char *win_ne_generic_sig(uint16_t service)
{
    for (unsigned i = 0; i < sizeof(generic) / sizeof(generic[0]); i++)
        if (module_base[generic[i].module] + generic[i].ordinal == service)
            return generic[i].sig;
    return NULL;
}

static void module_stem(const char *module, char *out, size_t cap);

static int module_of(const win_ne_import *imp)
{
    if (name_eq(imp->module, "KERNEL") || name_eq(imp->module, "KERNEL.EXE"))
        return M_KERNEL;
    if (name_eq(imp->module, "USER") || name_eq(imp->module, "USER.EXE"))
        return M_USER;
    if (name_eq(imp->module, "GDI") || name_eq(imp->module, "GDI.EXE"))
        return M_GDI;
    if (name_eq(imp->module, "KEYBOARD") || name_eq(imp->module, "KEYBOARD.DRV"))
        return M_KEYBOARD;
    if (name_eq(imp->module, "SHELL") || name_eq(imp->module, "SHELL.DLL"))
        return M_SHELL;
    if (name_eq(imp->module, "WIN87EM") || name_eq(imp->module, "WIN87EM.DLL"))
        return M_WIN87EM;
    for (int m = M_TOOLHELP; m < M_COUNT; m++) {    /* Windows' own DLLs, WINN's now */
        char stem[16];
        module_stem(imp->module, stem, sizeof(stem));
        if (name_eq(stem, module_names[m]))
            return m;
    }
    return -1;
}


/* Windows' own modules WINN has nothing of, or as native DLLs (there may
 * be none, or one without the function): a program importing them loads
 * (WIN_NE_SERVICE_UNDEFINED); their indices after WINN's (M_COUNT on) */
static const char *const system_modules[] = { "DDEML", "OLECLI", "OLESVR", "SOUND", "PENWIN",
                                              "LZEXPAND", "VER", "MMSYSTEM", "COMMDLG",
                                              "COMMCTRL", "NDDEAPI" };
_Static_assert(M_COUNT + sizeof(system_modules) / sizeof(system_modules[0]) <= 32,
               "an undefined call's module takes 5 bits");

/* the module of an import as an undefined call's: WINN's, or Windows' own */
static int trap_module(const win_ne_import *imp)
{
    int m = module_of(imp);
    if (m >= 0)
        return m;
    char stem[16];
    module_stem(imp->module, stem, sizeof(stem));
    for (unsigned i = 0; i < sizeof(system_modules) / sizeof(system_modules[0]); i++)
        if (name_eq(stem, system_modules[i]))
            return M_COUNT + (int)i;
    return -1;
}

/* the service of a function WINN has not, of a module it knows; 0 if none */
static uint16_t undefined_id(win_ne_guest_image *g, const win_ne_import *imp)
{
    int m = trap_module(imp);
    if (m < 0)
        return 0;
    unsigned ord = imp->ordinal;
    if (imp->procedure[0] || !ord || ord >= 504) {
        char num[8];                    /* a large ordinal: its number as the name */
        const char *name = imp->procedure;
        if (!name[0]) {
            int d = (int)sizeof(num) - 1;
            num[d] = 0;
            do {
                num[--d] = (char)('0' + ord % 10u);
                ord /= 10u;
            } while (ord && d > 0);
            name = num + d;
        }
        unsigned k = 0;                 /* by name: one of eight remembered */
        while (k < 7 && g && g->undefined[k][0] && !name_eq(g->undefined[k], name))
            k++;
        if (g && !g->undefined[k][0])
            lstrcpy_n(g->undefined[k], name[0] ? name : "?", sizeof(g->undefined[k]));
        ord = 504 + k;
    }
    return (uint16_t)(WIN_NE_SERVICE_UNDEFINED | (unsigned)m << 9 | ord);
}

void win_ne_guest_undefined_text(const win_ne_guest_image *g, uint16_t service, char *out,
                                 size_t cap)
{
    unsigned m = (service >> 9) & 31u, ord = service & 511u;
    const char *mod = m < M_COUNT ? module_names[m] :
                      m - M_COUNT < sizeof(system_modules) / sizeof(system_modules[0]) ?
                      system_modules[m - M_COUNT] : "?";
    char buf[64], num[8];
    size_t n = strlen(mod);
    memcpy(buf, mod, n);
    buf[n++] = '.';
    const char *fn = g ? g->undefined[ord - 504 < 8 ? ord - 504 : 0] : "?";
    if (ord < 504) {                   /* the ordinal in decimal */
        int k = (int)sizeof(num) - 1;
        num[k] = 0;
        do {
            num[--k] = (char)('0' + ord % 10u);
            ord /= 10u;
        } while (ord);
        fn = num + k;
    }
    lstrcpy_n(buf + n, fn, sizeof(buf) - n);
    lstrcpy_n(out, buf, cap);
}

/* a function WINN has its own code for: its index in services[], -1 */
static int service_index(const win_ne_import *imp)
{
    int module = module_of(imp);
    for (int i = 0; module >= 0 && i < (int)(sizeof(services) / sizeof(services[0])); i++)
        if (services[i].module == module &&
            (imp->ordinal ? imp->ordinal == services[i].ordinal && !imp->procedure[0]
                          : name_eq(imp->procedure, services[i].name)))
            return i;
    return -1;
}

/* the service number of an import, and the bytes of its arguments */
static int service_id_args(const win_ne_import *imp, uint16_t *id, uint16_t *args)
{
    /* KERNEL's Reserved1 to 4: Windows 2.x's AnsiNext, AnsiPrev, AnsiUpper,
     * AnsiLower there, USER's */
    static const uint16_t ansi[4] = { 472, 473, 431, 432 };
    if (module_of(imp) == M_KERNEL && !imp->procedure[0] && imp->ordinal >= 77 &&
        imp->ordinal <= 80) {
        win_ne_import u = *imp;
        lstrcpy_n(u.module, "USER", sizeof(u.module));
        u.ordinal = ansi[imp->ordinal - 77];
        return service_id_args(&u, id, args);
    }
    int i = service_index(imp), module = module_of(imp);
    if (i >= 0) {
        *id = (uint16_t)(module_base[services[i].module] + services[i].ordinal);
        *args = services[i].args;
        return 1;
    }
    if (module >= 0 && (i = generic_index(module, imp)) >= 0) {
        *id = (uint16_t)(module_base[module] + generic[i].ordinal);
        *args = sig_args(generic[i].sig);
        return 1;
    }
    return 0;
}

static int service_id(const win_ne_import *imp, uint16_t *id)
{
    uint16_t args;
    return service_id_args(imp, id, &args);
}

/* without core/windll.c (the tests of the loader alone): no native DLLs */
__attribute__((weak)) int win_ne_native_import(const char *module, uint16_t ordinal,
                                               const char *name, uint16_t *id)
{
    (void)module; (void)ordinal; (void)name; (void)id;
    return 0;
}

__attribute__((weak)) int win_ne_native_has(const char *module, uint16_t ordinal,
                                            const char *name)
{
    (void)module; (void)ordinal; (void)name;
    return 0;
}

__attribute__((weak)) uint16_t win_ne_native_args(uint16_t id)
{
    (void)id;
    return 0;
}

/* Win16 PASCAL return releases the argument words after popping CS:IP. */
static uint16_t service_args(uint16_t id)
{
    if (id >= WIN_NE_SERVICE_DLL && id < WIN_NE_SERVICE_UNDEFINED)
        return win_ne_native_args(id);
    for (unsigned i = 0; i < sizeof(services) / sizeof(services[0]); i++)
        if (module_base[services[i].module] + services[i].ordinal == id)
            return services[i].args;
    const char *sig = win_ne_generic_sig(id);
    return sig ? sig_args(sig) : 0;
}

/* KERNEL's constants a program imports as values, not as procedures:
 * __AHSHIFT, __AHINCR (a huge pointer's next 64K: no further selectors
 * are tiled), __WINFLAGS, __0040H (the BIOS data, a selector of its own) */
static int constant_of(const win_ne_guest_image *g, const win_ne_import *imp, uint16_t *value)
{
    static const struct { uint16_t ordinal; const char *name; uint16_t value; } k[] = {
        { 113, "__AHSHIFT", 3 }, { 114, "__AHINCR", 8 },
        { 178, "__WINFLAGS", WIN_NE_WINFLAGS }, { 193, "__0040H", 0 },
    };
    if (module_of(imp) != M_KERNEL)
        return 0;
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (imp->ordinal ? imp->ordinal == k[i].ordinal && !imp->procedure[0]
                         : name_eq(imp->procedure, k[i].name)) {
            *value = k[i].value ? k[i].value : g ? g->bios_selector : 1;
            return 1;
        }
    for (unsigned i = 0; i < sizeof(low_segments) / sizeof(low_segments[0]); i++)
        if (imp->ordinal ? imp->ordinal == low_segments[i].ordinal && !imp->procedure[0]
                         : name_eq(imp->procedure, low_segments[i].name)) {
            *value = g ? (uint16_t)((GDT_EXTRA(g) + GDT_LOW + i) * GDT_ENTRY_BYTES | 3u) : 1;
            return 1;
        }
    return 0;
}

static int locate_dll(const char *program, const char *module, char *out, size_t cap);
static int dll_has(const char *dll, uint16_t ordinal, const char *name);
static void module_stem(const char *module, char *out, size_t cap);

typedef struct {
    win_ne_unresolved *u;
    const char *path;
    int depth;
    char checked[16];                   /* the DLL last looked into */
} scan_ctx;

static int note_unresolved(void *opaque, const win_ne_import *imp)
{
    uint16_t id;
    scan_ctx *sc = (scan_ctx *)opaque;
    char dll[160];
    if (service_id(imp, &id) || constant_of(NULL, imp, &id))
        return 0;
    int native = module_of(imp) < 0 ? win_ne_native_has(imp->module, imp->ordinal,
                                                        imp->procedure[0] ? imp->procedure : NULL)
                                    : 0;
    if (native > 0)
        return 0;                       /* a native DLL's */
    if (!native && module_of(imp) < 0 && locate_dll(sc->path, imp->module, dll, sizeof(dll)) &&
        dll_has(dll, imp->ordinal, imp->procedure[0] ? imp->procedure : NULL)) {
        /* its DLL has it; and what the DLL needs? */
        char base[16];
        module_stem(imp->module, base, sizeof(base));
        if (sc->depth >= 3 || !strcmp(sc->checked, base))
            return 0;
        lstrcpy_n(sc->checked, base, sizeof(sc->checked));
        win_ne_info info;
        scan_ctx inner = { sc->u, dll, sc->depth + 1, "" };
        return win_ne_scan(dll, &info, note_unresolved, &inner) == WIN_NE_CANCELLED;
    }
    if (trap_module(imp) >= 0)
        return 0;                       /* its call of it ends it */
    win_ne_unresolved *u = sc->u;
    size_t n = strlen(imp->module);
    if (n >= sizeof(u->module))
        n = sizeof(u->module) - 1;
    memcpy(u->module, imp->module, n);
    u->module[n] = 0;
    n = strlen(imp->procedure);
    if (n >= sizeof(u->name))
        n = sizeof(u->name) - 1;
    memcpy(u->name, imp->procedure, n);
    u->name[n] = 0;
    u->ordinal = imp->ordinal;
    return 1;                           /* the first one is enough */
}

int win_ne_guest_unresolved(const char *path, win_ne_unresolved *u)
{
    memset(u, 0, sizeof(*u));
    win_ne_info info;
    scan_ctx sc = { u, path, 0, "" };
    return win_ne_scan(path, &info, note_unresolved, &sc) == WIN_NE_CANCELLED;
}

static int load_dll(win_ne_guest_image *g, const char *module, int depth);
static int dll_export(win_ne_guest_image *g, int dll, uint16_t ordinal, const char *name,
                      uint16_t *selector, uint16_t *offset);

static int thunk_slot(win_ne_guest_image *g, uint16_t id, uint16_t *selector, uint16_t *offset);

static int resolve_import(void *opaque, const win_ne_import *imp,
                          uint16_t *selector, uint16_t *offset)
{
    load_ctx *lc = (load_ctx *)opaque;
    win_ne_guest_image *g = lc->g;
    uint16_t id;
    if (constant_of(g, imp, &id)) {
        *selector = *offset = id;
        return 1;
    }
    if (module_of(imp) < 0) {           /* a native DLL's, a DLL's (one of Windows' own, there
                                         * may be none) */
        int native = win_ne_native_import(imp->module, imp->ordinal,
                                          imp->procedure[0] ? imp->procedure : NULL, &id);
        if (native < 0 && !(id = undefined_id(g, imp)))
            return 0;                   /* a native DLL without it, of no module known */
        if (!native) {
            int d = load_dll(g, imp->module, lc->depth);
            if (d >= 0)
                return dll_export(g, d, imp->ordinal, imp->procedure[0] ? imp->procedure : NULL,
                                  selector, offset);
            if (trap_module(imp) < 0)
                return 0;
        }
        if (!native && !service_id(imp, &id) && !(id = undefined_id(g, imp)))
            return 0;
    } else if (!service_id(imp, &id) && !(id = undefined_id(g, imp))) {
        return 0;
    }
    return thunk_slot(g, id, selector, offset);
}

/* the program's thunk of a service: the one it has, or a new one (to be
 * written) */
static int thunk_slot(win_ne_guest_image *g, uint16_t id, uint16_t *selector, uint16_t *offset)
{
    for (uint16_t i = 0; i < g->thunk_count; i++) {
        if (g->thunk_services[i] == id) {
            *selector = g->thunk3_selector;
            *offset = (uint16_t)(i * THUNK_BYTES);
            return 1;
        }
    }
    if (g->thunk_count >= WIN_NE_GUEST_ACTIVE_THUNKS)
        return 0;
    uint16_t slot = g->thunk_count++;
    g->thunk_services[slot] = id;
    *selector = g->thunk3_selector;
    *offset = (uint16_t)(slot * THUNK_BYTES);
    return 1;
}

/* WIN87EM's __FPMATH: its function in BX, its operands in DX:AX and the
 * coprocessor's stack; the emulator's coprocessor does the work. 0, 1:
 * install and initialize; 4: the control word from AX; 5: the control
 * word into AX; 7: pop as a long into DX:AX; 8: the status word into AX,
 * then cleared; 9: cleared; 11: a coprocessor there (DX:AX 1); the rest
 * (2 to remove it, 3 its error handler, 6, 10, 12): AX 0. */
static const uint8_t fpmath_code[] = {
    0x55, 0x89, 0xe5, 0x50, 0x50,       /* push bp; mov bp,sp; push ax x2 */
    0x83, 0xfb, 0x01, 0x77, 0x04,       /* cmp bx,1; ja n1 */
    0xdb, 0xe3, 0xeb, 0x4b,             /* fninit; jmp zero */
    0x83, 0xfb, 0x04, 0x75, 0x05,       /* n1: cmp bx,4; jne n4 */
    0xd9, 0x6e, 0xfe, 0xeb, 0x43,       /* fldcw [bp-2]; jmp out */
    0x83, 0xfb, 0x05, 0x75, 0x05,       /* n4: cmp bx,5; jne n5 */
    0xd9, 0x7e, 0xfe, 0xeb, 0x32,       /* fnstcw [bp-2]; jmp ax2 */
    0x83, 0xfb, 0x07, 0x75, 0x08,       /* n5: cmp bx,7; jne n7 */
    0xdb, 0x5e, 0xfc,                   /* fistp dword [bp-4] */
    0x8b, 0x56, 0xfe, 0xeb, 0x20,       /* mov dx,[bp-2]; jmp ax4 */
    0x83, 0xfb, 0x08, 0x75, 0x06,       /* n7: cmp bx,8; jne n8 */
    0xdf, 0xe0, 0xdb, 0xe2, 0xeb, 0x21, /* fnstsw ax; fnclex; jmp out */
    0x83, 0xfb, 0x09, 0x75, 0x04,       /* n8: cmp bx,9; jne n9 */
    0xdb, 0xe2, 0xeb, 0x16,             /* fnclex; jmp zero */
    0x83, 0xfb, 0x0b, 0x75, 0x11,       /* n9: cmp bx,11; jne zero */
    0x31, 0xd2, 0xb8, 0x01, 0x00,       /* xor dx,dx; mov ax,1 */
    0xeb, 0x0c,                         /* jmp out */
    0x8b, 0x46, 0xfc, 0xeb, 0x07,       /* ax4: mov ax,[bp-4]; jmp out */
    0x8b, 0x46, 0xfe, 0xeb, 0x02,       /* ax2: mov ax,[bp-2]; jmp out */
    0x31, 0xc0,                         /* zero: xor ax,ax */
    0x89, 0xec, 0x5d, 0xcb,             /* out: mov sp,bp; pop bp; retf */
};
_Static_assert(sizeof(fpmath_code) <= WIN_NE_FPMATH_BYTES, "__FPMATH too big");

/* A thunk at its offset: the service's number, CALL FAR the API gate
 * (ring 0), RETF releasing its PASCAL arguments; DOS3Call's an INT 21h of
 * its own, __FPMATH's a jump to its code */
static void thunk_bytes(const win_ne_guest_image *g, uint16_t id, uint16_t at,
                        uint8_t p[THUNK_BYTES])
{
    uint16_t args = service_args(id);
    memset(p, 0xcc, THUNK_BYTES);
    if (id == 0xfffeu) {                /* Win16's initial system HOOKPROC */
        p[0] = 0x31; p[1] = 0xc0;       /* xor ax,ax */
        p[2] = 0x31; p[3] = 0xd2;       /* xor dx,dx */
        p[4] = 0xca;                    /* retf 8: HOOKPROC(code,wp,lp) */
        put16(p + 5, 8);
        return;
    }
    if (id == WIN_NE_SERVICE_DOS3CALL || id == 101) {   /* and NoHookDosCall */
        p[0] = 0xcd; p[1] = 0x21;          /* int 21h, with its registers */
        p[2] = 0xcb;                       /* retf */
        return;
    }
    if (id == WIN_NE_SERVICE_FPMATH) {
        p[0] = 0xe9;                       /* jmp near */
        put16(p + 1, (uint16_t)(WIN_NE_FPMATH_OFFSET - (at + 3u)));
        return;
    }
    if (id == WIN_NE_SERVICE_KEYBD_EVENT || id == WIN_NE_SERVICE_MOUSE_EVENT) {
        /* the registers are the arguments: AX too, on the stack */
        p[0] = 0x50;                       /* push ax */
        p[1] = 0xb8; put16(p + 2, id);     /* mov ax,service */
        p[4] = 0x9a;                       /* call far api gate */
        put16(p + 5, 0);
        put16(p + 7, g->api_gate);
        p[9] = 0x83; p[10] = 0xc4; p[11] = 0x02;  /* add sp,2 */
        p[12] = 0xcb;                      /* retf */
        return;
    }
    /* MOV AX,service in its long form (C7 C0): a thunk does not look like
     * a Windows module's exported prologue (MOV AX,DGROUP, B8), which
     * programs take that DGROUP from (Word for Windows 1.0 does, for
     * USER's and GDI's heaps) */
    p[0] = 0xc7; p[1] = 0xc0; put16(p + 2, id);   /* mov ax,service */
    p[4] = 0x9a;                           /* call far api gate */
    put16(p + 5, 0);
    put16(p + 7, g->api_gate);
    if (args) {
        p[9] = 0xca; put16(p + 10, args);
    } else {
        p[9] = 0xcb;
    }
}

static uint32_t module_proc(win_ne_guest_image *g, unsigned module, uint16_t ordinal,
                            const char *name, int undefined)
{
    if (!g || module >= M_COUNT)
        return 0;
    win_ne_import imp;
    memset(&imp, 0, sizeof(imp));
    lstrcpy_n(imp.module, module_names[module], sizeof(imp.module));
    if (name)
        lstrcpy_n(imp.procedure, name, sizeof(imp.procedure));
    else
        imp.ordinal = ordinal;
    uint16_t id;
    if (constant_of(g, &imp, &id))      /* a constant: FFFF:value, as Windows has it */
        return 0xffff0000u | id;
    if (!service_id(&imp, &id) && (!undefined || !(id = undefined_id(g, &imp))))
        return 0;
    return win_ne_guest_thunk(g, id);
}

uint32_t win_ne_guest_proc(win_ne_guest_image *g, unsigned module, uint16_t ordinal,
                           const char *name)
{
    return module_proc(g, module, ordinal, name, 0);
}

uint32_t win_ne_guest_import_proc(win_ne_guest_image *g, unsigned module, uint16_t ordinal,
                                  const char *name)
{
    return module_proc(g, module, ordinal, name, 1);
}

uint32_t win_ne_guest_thunk(win_ne_guest_image *g, uint16_t id)
{
    if (!g || !id)
        return 0;
    uint16_t sel, off;
    uint16_t before = g->thunk_count;
    if (!thunk_slot(g, id, &sel, &off))
        return 0;
    if (g->thunk_count != before) {         /* a new one: written now */
        uint8_t p[THUNK_BYTES];
        thunk_bytes(g, id, off, p);
        if (!g->io.write(g->opaque, g->thunk_linear + off, p, sizeof(p))) {
            g->thunk_count = before;
            return 0;
        }
    }
    return (uint32_t)sel << 16 | off;
}

uint32_t win_ne_guest_hook_terminator(const win_ne_guest_image *g)
{
    /* The thunk table is mutable even when reached through a running context. */
    return win_ne_guest_thunk((win_ne_guest_image *)g, 0xfffeu);
}

const char *win_ne_guest_module_name(int m)
{
    return m >= 0 && m < M_COUNT ? module_names[m] : NULL;
}

int win_ne_guest_module(const char *name)
{
    char base[16];
    unsigned n = 0;
    for (const char *p = name; *p && *p != '.' && n + 1 < sizeof(base); p++)
        base[n++] = *p;
    base[n] = 0;
    for (int i = 0; i < M_COUNT; i++)
        if (name_eq(base, module_names[i]))
            return i;
    return -1;
}

int win_ne_guest_load(const char *path, win_ne_guest_image *out,
                      const win_ne_guest_io *io, void *opaque)
{
    if (!out || !io || !io->alloc || !io->free || !io->write)
        return WIN_NE_BAD_IMAGE;
    memset(out, 0, sizeof(*out));
    win_ne_info info;
    int rc = win_ne_scan(path, &info, NULL, NULL);
    if (rc)
        return rc;
    if (!info.cs_index || info.segment_count > WIN_NE_GUEST_MAX_SEGMENTS ||
        info.target_os != 2u)
        return WIN_NE_UNSUPPORTED;

    out->io = *io;
    out->opaque = opaque;
    out->info = info;
    out->segments = (win_ne_guest_segment *)calloc((size_t)info.segment_count + 1u,
                                                    sizeof(*out->segments));
    /* null, thunks, the segments, the PSP, the global blocks, the extras */
    uint32_t gdtsize = ((uint32_t)info.segment_count + 3u + WIN_NE_GUEST_GLOBALS + GDT_EXTRAS +
                        WIN_NE_GUEST_DLL_SEGMENTS) * GDT_ENTRY_BYTES;
    out->gdt = (uint8_t *)calloc(gdtsize, 1);
    rc = WIN_NE_NO_MEMORY;
    if (!out->segments || !out->gdt)
        goto fail;
    out->gdt_limit = (uint16_t)(gdtsize - 1u);
    out->thunk_selector = GDT_THUNK_INDEX * GDT_ENTRY_BYTES;
    out->psp.selector = (uint16_t)(GDT_SEGMENT_INDEX(info.segment_count + 1u) * GDT_ENTRY_BYTES | 3u);
    uint32_t x = GDT_EXTRA(out);
    out->thunk3_selector = (uint16_t)(x * GDT_ENTRY_BYTES | 3u);
    out->api_gate = (uint16_t)((x + 1u) * GDT_ENTRY_BYTES | 3u);
    out->exit_gate = (uint16_t)((x + 2u) * GDT_ENTRY_BYTES | 3u);
    out->cbret_gate = (uint16_t)((x + 3u) * GDT_ENTRY_BYTES | 3u);
    out->tss_selector = (uint16_t)((x + 4u) * GDT_ENTRY_BYTES);
    out->ring0_selector = (uint16_t)((x + 5u) * GDT_ENTRY_BYTES);
    out->bios_selector = (uint16_t)((x + 6u) * GDT_ENTRY_BYTES | 3u);
    if (!guest_alloc_in(out, gdtsize, &out->gdt_linear, 1) ||
        !guest_alloc_in(out, WIN_NE_THUNK_SEGMENT_BYTES, &out->thunk_linear, 1) ||
        !guest_alloc_in(out, WIN_NE_GUEST_PSP_BYTES, &out->psp.linear, 1))
        goto fail;
    seg_descriptor(out->gdt + out->thunk_selector, out->thunk_linear,
                   WIN_NE_THUNK_SEGMENT_BYTES, 0x9a);
    seg_descriptor(out->gdt + (out->thunk3_selector & ~7u), out->thunk_linear,
                   WIN_NE_THUNK_SEGMENT_BYTES, 0xfa);
    call_gate(out->gdt + (out->api_gate & ~7u), out->thunk_selector, WIN_NE_BOOT_GATEWAY_OFFSET);
    call_gate(out->gdt + (out->exit_gate & ~7u), out->thunk_selector, WIN_NE_BOOT_EXIT_OFFSET);
    call_gate(out->gdt + (out->cbret_gate & ~7u), out->thunk_selector, WIN_NE_CBRET_OFFSET);
    seg_descriptor(out->gdt + out->tss_selector, out->thunk_linear + WIN_NE_RING0_OFFSET,
                   WIN_NE_TSS_BYTES, 0x81);         /* 16-bit TSS, available */
    seg_descriptor(out->gdt + out->ring0_selector, out->thunk_linear + WIN_NE_RING0_OFFSET,
                   WIN_NE_RING0_BYTES, 0x92);
    seg_descriptor(out->gdt + (out->bios_selector & ~7u), 0x400u, 0x300u, 0xf0);  /* read-only */
    for (uint32_t i = 0; i < sizeof(low_segments) / sizeof(low_segments[0]); i++)
        seg_descriptor(out->gdt + (x + GDT_LOW + i) * GDT_ENTRY_BYTES, low_segments[i].base,
                       0x10000u, 0xf0);
    out->psp.size = WIN_NE_GUEST_PSP_BYTES;
    out->psp.flags = 1;                 /* data */
    descriptor(out->gdt + (out->psp.selector & ~7u), out->psp.linear, WIN_NE_GUEST_PSP_BYTES, 0);

    lstrcpy_n(out->path, path, sizeof(out->path));
    win_ne_image image;
    load_ctx lc = { out, -1, 0 };
    rc = win_ne_load_image(path, &image, assign_selector, resolve_import, &lc);
    if (rc)
        goto fail;

    /* Code segments, stack, and thunks must be visible to x86 before
     * loading GDTR. A failed guest write leaves no partially-owned blocks. */
    rc = WIN_NE_IO_ERROR;
    for (uint16_t i = 1; i <= info.segment_count; i++) {
        const win_ne_guest_segment *s = &out->segments[i];
        if (s->linear && !io->write(opaque, s->linear, image.segments[i].data, s->size))
            goto loaded_fail;
    }
    /* Every unused byte of the thunk segment INT3, so an unallocated
     * offset never executes stale DOS heap contents (ne_exec writes the
     * trampoline, the stubs, the IDT and the TSS over it). */
    for (uint32_t i = 0; i < WIN_NE_THUNK_SEGMENT_BYTES / THUNK_BYTES; i++) {
        uint8_t p[THUNK_BYTES];
        memset(p, 0xcc, sizeof(p));
        if (i < out->thunk_count) {
            uint16_t id = out->thunk_services[i];
            /* CALL FAR the shared PM->RM->PM bridge through the API call
             * gate (to ring 0). The OUT is executed in real mode, allowing
             * its native handler to use DOS/BIOS. RETF imm16 restores the
             * Win16 PASCAL caller's SP. */
            thunk_bytes(out, id, (uint16_t)(i * THUNK_BYTES), p);
        }
        if (!io->write(opaque, out->thunk_linear + i * THUNK_BYTES, p, sizeof(p)))
            goto loaded_fail;
    }
    if (!io->write(opaque, out->thunk_linear + WIN_NE_FPMATH_OFFSET, fpmath_code,
                   sizeof(fpmath_code)))
        goto loaded_fail;
    if (!io->write(opaque, out->gdt_linear, out->gdt, gdtsize))
        goto loaded_fail;

    out->cs = out->segments[info.cs_index].selector;
    out->ip = info.ip;
    if (info.ss_index) {
        /* SP 0: the top of the segment, after DGROUP's data, heap, stack */
        const win_ne_guest_segment *ss = &out->segments[info.ss_index];
        out->ss = ss->selector;
        out->sp = info.sp ? info.sp : (uint16_t)(ss->size < 65536u ? ss->size : 0xfffeu);
    }
    rc = WIN_NE_OK;
loaded_fail:
    win_ne_free_image(&image);
    if (!rc)
        return WIN_NE_OK;
fail:
    win_ne_guest_release(out);
    return rc;
}

/* ---------------------------------------------------------------- global memory */

/* Publish descriptor changes only after the guest GDT write succeeds. */
static int set_descriptor(win_ne_guest_image *g, uint16_t sel, uint32_t linear, uint32_t size,
                          int code)
{
    uint8_t desc[GDT_ENTRY_BYTES];
    descriptor(desc, linear, size, code);
    sel &= ~7u;
    if (!g->io.write(g->opaque, g->gdt_linear + sel, desc, sizeof(desc)))
        return 0;
    memcpy(g->gdt + sel, desc, sizeof(desc));
    return 1;
}

static uint16_t global_selector(const win_ne_guest_image *g, unsigned i)
{
    return (uint16_t)(GDT_SEGMENT_INDEX(g->info.segment_count + 2u + i) * GDT_ENTRY_BYTES | 3u);
}

/* a block MyAlloc gave its loader (a segment of a self-loading module) */
static int myalloc_code(const win_ne_guest_segment *s)
{
    return (s->flags & WIN_NE_GUEST_MYALLOC) && !(s->flags & 1u);
}

win_ne_guest_segment *win_ne_guest_global(win_ne_guest_image *g, uint16_t sel)
{
    for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++)
        if (g->globals[i].selector && (g->globals[i].selector & ~7u) == (sel & ~7u))
            return &g->globals[i];
    if (g->info.module_flags & WIN_NE_SELF_LOADING)
        for (uint16_t i = 2; i <= g->info.segment_count; i++)
            if ((g->segments[i].flags & WIN_NE_GUEST_MYALLOC) &&
                (g->segments[i].selector & ~7u) == (sel & ~7u))
                return &g->segments[i];
    return NULL;
}

static uint16_t global_alloc(win_ne_guest_image *g, uint32_t bytes, int low)
{
    if (!bytes || bytes > 65536u)
        return 0;
    for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++) {
        win_ne_guest_segment *s = &g->globals[i];
        if (s->selector)
            continue;
        if (!guest_alloc_in(g, bytes, &s->linear, low))
            return 0;
        uint8_t zero[256];
        memset(zero, 0, sizeof(zero));
        for (uint32_t at = 0; at < bytes; at += sizeof(zero)) {
            uint32_t n = bytes - at < sizeof(zero) ? bytes - at : (uint32_t)sizeof(zero);
            if (!g->io.write(g->opaque, s->linear + at, zero, n))
                goto alloc_fail;
        }
        uint16_t selector = global_selector(g, i);
        if (!set_descriptor(g, selector, s->linear, bytes, 0))
            goto alloc_fail;
        s->size = bytes;
        s->flags = 1;
        s->selector = selector;
        return selector;
alloc_fail:
        g->io.free(g->opaque, s->linear);
        memset(s, 0, sizeof(*s));
        return 0;
    }
    return 0;
}

uint16_t win_ne_guest_global_alloc(win_ne_guest_image *g, uint32_t bytes)
{
    return global_alloc(g, bytes, 0);
}

uint16_t win_ne_guest_global_alloc_low(win_ne_guest_image *g, uint32_t bytes)
{
    return global_alloc(g, bytes, 1);
}

/* a data segment moved to a block of the new size, its selector kept: the
 * old contents copied, the rest zeroed; untouched if any of it fails */
static int resize(win_ne_guest_image *g, win_ne_guest_segment *s, uint32_t bytes,
                  int (*read)(void *, uint32_t, void *, uint32_t))
{
    uint32_t linear;
    if (!s || !read || !bytes || bytes > 65536u)
        return 0;
    /* where it is, when what follows is free: no second copy of it while
     * it moves (DOS's memory is all the programs have); a block of none
     * yet (MyAlloc's of size 0) gets its first */
    if (s->linear && g->io.resize && g->io.resize(g->opaque, s->linear, bytes)) {
        uint8_t zero[256];
        memset(zero, 0, sizeof(zero));
        int ok = 1;
        for (uint32_t at = s->size; ok && at < bytes; at += sizeof(zero))
            ok = g->io.write(g->opaque, s->linear + at, zero,
                             bytes - at < sizeof(zero) ? bytes - at : (uint32_t)sizeof(zero));
        if (ok && set_descriptor(g, s->selector, s->linear, bytes, myalloc_code(s))) {
            s->size = bytes;
            return 1;
        }
        g->io.resize(g->opaque, s->linear, s->size);    /* as it was */
        return 0;
    }
    if (!guest_alloc(g, bytes, &linear))
        return 0;
    uint8_t buf[256];
    for (uint32_t at = 0; at < bytes; at += sizeof(buf)) {
        uint32_t n = bytes - at < sizeof(buf) ? bytes - at : (uint32_t)sizeof(buf);
        memset(buf, 0, sizeof(buf));
        if (at < s->size && !read(NULL, s->linear + at, buf,
                                 s->size - at < n ? s->size - at : n))
            goto realloc_fail;
        if (!g->io.write(g->opaque, linear + at, buf, n))
            goto realloc_fail;
    }
    if (!set_descriptor(g, s->selector, linear, bytes, myalloc_code(s)))
        goto realloc_fail;
    if (s->linear)
        g->io.free(g->opaque, s->linear);
    s->linear = linear;
    s->size = bytes;
    return 1;
realloc_fail:
    g->io.free(g->opaque, linear);
    return 0;
}

int win_ne_guest_global_realloc(win_ne_guest_image *g, uint16_t sel, uint32_t bytes,
                                int (*read)(void *, uint32_t, void *, uint32_t))
{
    win_ne_guest_segment *s = win_ne_guest_global(g, sel);
    return s && !(s->flags & WIN_NE_GUEST_ALIAS) && resize(g, s, bytes, read);
}

int win_ne_guest_global_free(win_ne_guest_image *g, uint16_t sel)
{
    win_ne_guest_segment *s = win_ne_guest_global(g, sel);
    if (!s)
        return 0;
    uint8_t absent[GDT_ENTRY_BYTES] = {0};
    /* A failed descriptor write must not release still-addressable memory. */
    if (!g->io.write(g->opaque, g->gdt_linear + (s->selector & ~7u),
                     absent, sizeof(absent)))
        return 0;
    memcpy(g->gdt + (s->selector & ~7u), absent, sizeof(absent));
    if (!(s->flags & WIN_NE_GUEST_ALIAS) && s->linear)
        g->io.free(g->opaque, s->linear);
    uint16_t slot = s->flags & WIN_NE_GUEST_MYALLOC ? s->selector : 0;
    memset(s, 0, sizeof(*s));
    s->selector = slot;                 /* a segment's slot keeps its selector */
    return 1;
}

/* ---------------------------------------------------------------- selectors */

/* a selector's descriptor (the host's copy of the GDT): its base, limit,
 * whether code; 0 if it is none of the program's */
int win_ne_guest_selector_live(const win_ne_guest_image *g, uint16_t sel)
{
    uint32_t base, limit;
    int code;
    return !g || win_ne_guest_selector_get(g, sel, &base, &limit, &code);
}

int win_ne_guest_selector_get(const win_ne_guest_image *g, uint16_t sel, uint32_t *base,
                              uint32_t *limit, int *code)
{
    uint32_t at = sel & ~7u;
    if (!at || (sel & 4u) || at + GDT_ENTRY_BYTES > (uint32_t)g->gdt_limit + 1u)
        return 0;
    const uint8_t *p = g->gdt + at;
    if (!(p[5] & 0x80u) || !(p[5] & 0x10u) || (p[5] & 0x60u) != 0x60u)
        return 0;                       /* not present, a system one, not ring 3 */
    *base = p[2] | (uint32_t)p[3] << 8 | (uint32_t)p[4] << 16 | (uint32_t)p[7] << 24;
    *limit = p[0] | (uint32_t)p[1] << 8 | (uint32_t)(p[6] & 0x0fu) << 16;
    *code = !!(p[5] & 0x08u);
    return 1;
}

static int alias_write(win_ne_guest_image *g, win_ne_guest_segment *s, uint32_t base,
                       uint32_t size, int code)
{
    uint8_t desc[GDT_ENTRY_BYTES];
    descriptor(desc, base, size, code);
    if (!g->io.write(g->opaque, g->gdt_linear + (s->selector & ~7u), desc, sizeof(desc)))
        return 0;
    memcpy(g->gdt + (s->selector & ~7u), desc, sizeof(desc));
    s->linear = base;
    s->size = size;
    s->flags = (uint16_t)(WIN_NE_GUEST_ALIAS | (code ? 0u : 1u));
    return 1;
}

uint16_t win_ne_guest_selector_alloc(win_ne_guest_image *g, uint16_t like, int code)
{
    uint32_t base = 0, limit = 0;
    int was_code = 0;
    if (like && !win_ne_guest_selector_get(g, like, &base, &limit, &was_code))
        return 0;
    if (code < 0)
        code = was_code;
    for (unsigned i = 0; i < WIN_NE_GUEST_GLOBALS; i++) {
        win_ne_guest_segment *s = &g->globals[i];
        if (s->selector)
            continue;
        s->selector = global_selector(g, i);
        if (!alias_write(g, s, base, limit + 1u, code)) {
            memset(s, 0, sizeof(*s));
            return 0;
        }
        return s->selector;
    }
    return 0;
}

uint16_t win_ne_guest_selector_array(win_ne_guest_image *g, unsigned n)
{
    for (unsigned i = 0; n && i + n <= WIN_NE_GUEST_GLOBALS; i++) {
        unsigned k = 0;
        while (k < n && !g->globals[i + k].selector)
            k++;
        if (k < n)
            continue;
        for (k = 0; k < n; k++) {
            win_ne_guest_segment *s = &g->globals[i + k];
            s->selector = global_selector(g, i + k);
            if (!alias_write(g, s, 0, 1, 0)) {
                while (k--)
                    win_ne_guest_global_free(g, g->globals[i + k].selector);
                memset(s, 0, sizeof(*s));
                return 0;
            }
        }
        return g->globals[i].selector;
    }
    return 0;
}

int win_ne_guest_selector_set(win_ne_guest_image *g, uint16_t sel, uint32_t base,
                              uint32_t limit, int code)
{
    win_ne_guest_segment *s = win_ne_guest_global(g, sel);
    if (!s || !(s->flags & WIN_NE_GUEST_ALIAS) || limit > 0xffffu)
        return 0;
    return alias_write(g, s, base, limit + 1u, code);
}

/* ---------------------------------------------------------------- DLLs */

/* where a DLL's file is found: the program's directory, then those WINN
 * names (the Windows directory, the system one) */
__attribute__((weak)) const char *win_ne_dll_dir(void)
{
    return NULL;
}

__attribute__((weak)) const char *win_ne_dll_system_dir(void)
{
    return NULL;
}

static void module_stem(const char *module, char *out, size_t cap)
{
    const char *b = module;
    for (const char *p = module; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    size_t n = 0;
    while (b[n] && b[n] != '.' && n + 1 < cap) {
        char c = b[n];
        out[n++] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    out[n] = 0;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

static int locate_dll(const char *program, const char *module, char *out, size_t cap)
{
    char base[16], name[24];
    module_stem(module, base, sizeof(base));
    if (!base[0])
        return 0;
    const char *dot = NULL;
    for (const char *p = module; *p; p++)
        if (*p == '.')
            dot = p;
    const char *ext = dot ? dot : ".DLL";
    if (strlen(base) + strlen(ext) >= sizeof(name))
        return 0;
    strcpy(name, base);
    strcat(name, ext);
    const char *dirs[3] = { program, win_ne_dll_dir(), win_ne_dll_system_dir() };
    for (int i = 0; i < 3; i++) {
        if (!dirs[i])
            continue;
        size_t n = strlen(dirs[i]);
        if (i == 0)                     /* the program's: its path up to the name */
            while (n && dirs[i][n - 1] != '\\' && dirs[i][n - 1] != '/' && dirs[i][n - 1] != ':')
                n--;
        if (n + strlen(name) + 2 > cap)
            continue;
        memcpy(out, dirs[i], n);
        if (i && n && out[n - 1] != '\\' && out[n - 1] != '/')
            out[n++] = dirs[i][0] && strchr(dirs[i], '\\') ? '\\' : '/';
        strcpy(out + n, name);
        if (file_exists(out))
            return 1;
    }
    return 0;
}

static int dll_has(const char *dll, uint16_t ordinal, const char *name)
{
    uint8_t flags;
    uint16_t seg, off;
    if (name)
        ordinal = win_ne_ordinal(dll, name);
    return ordinal && win_ne_entry(dll, ordinal, &flags, &seg, &off) == WIN_NE_OK;
}

static int dll_export(win_ne_guest_image *g, int dll, uint16_t ordinal, const char *name,
                      uint16_t *selector, uint16_t *offset)
{
    const win_ne_guest_dll *d = &g->dlls[dll];
    uint8_t flags;
    uint16_t seg, off;
    if (name)
        ordinal = win_ne_ordinal(d->path, name);
    if (!ordinal || win_ne_entry(d->path, ordinal, &flags, &seg, &off) != WIN_NE_OK ||
        !seg || seg > d->count)
        return 0;
    *selector = g->dll_segments[d->first + seg - 1u].selector;
    *offset = off;
    return 1;
}

/* An exported procedure of a DLL starts by taking DS for its own (PUSH DS;
 * POP AX or MOV AX,DS; NOP); its caller's is not: as Windows does, its
 * DGROUP goes there (MOV AX,DGROUP) */
static void patch_exports(win_ne_guest_image *g, int dll, win_ne_image *image)
{
    const win_ne_guest_dll *d = &g->dlls[dll];
    if (!d->auto_data)
        return;
    uint16_t dgroup = g->dll_segments[d->first + d->auto_data - 1u].selector;
    for (uint16_t o = 1; o < 1024; o++) {
        uint8_t flags;
        uint16_t seg, off;
        int rc = win_ne_entry(d->path, o, &flags, &seg, &off);
        if (rc == WIN_NE_CANCELLED)
            break;
        if (rc != WIN_NE_OK || !(flags & 1) || !seg || seg > d->count ||
            (image->segments[seg].flags & 1u) || off + 3u > image->segments[seg].size)
            continue;
        uint8_t *p = image->segments[seg].data + off;
        if ((p[0] == 0x1e && p[1] == 0x58 && p[2] == 0x90) ||
            (p[0] == 0x8c && p[1] == 0xd8 && p[2] == 0x90)) {
            p[0] = 0xb8;
            put16(p + 1, dgroup);
        }
    }
}

static int load_dll(win_ne_guest_image *g, const char *module, int depth)
{
    char base[16], path[160];
    module_stem(module, base, sizeof(base));
    for (int i = 0; i < g->dll_count; i++)
        if (!strcmp(g->dlls[i].name, base))
            return g->dlls[i].count ? i : -1;
    win_ne_info info;
    if (depth > 4 || g->dll_count >= WIN_NE_GUEST_DLLS || strlen(base) > 8 ||
        !locate_dll(g->path, module, path, sizeof(path)) ||
        win_ne_scan(path, &info, NULL, NULL) || !(info.module_flags & 0x8000u) ||
        (info.module_flags & WIN_NE_SELF_LOADING) ||   /* a self-loading DLL: not WINN's */
        info.target_os != 2u ||
        info.segment_count > WIN_NE_GUEST_DLL_SEGMENTS - g->dll_segment_count)
        return -1;
    int i = g->dll_count++;
    win_ne_guest_dll *d = &g->dlls[i];
    memset(d, 0, sizeof(*d));
    strcpy(d->name, base);
    lstrcpy_n(d->path, path, sizeof(d->path));
    d->first = g->dll_segment_count;
    d->count = info.segment_count;
    d->auto_data = info.auto_data;
    d->heap_size = info.heap_size;
    g->dll_segment_count = (uint16_t)(g->dll_segment_count + info.segment_count);
    win_ne_image image;
    load_ctx lc = { g, i, depth + 1 };
    int rc = win_ne_load_image(path, &image, assign_selector, resolve_import, &lc);
    if (!rc) {
        patch_exports(g, i, &image);
        for (uint16_t k = 1; !rc && k <= d->count; k++) {
            const win_ne_guest_segment *s = &g->dll_segments[d->first + k - 1u];
            if (!g->io.write(g->opaque, s->linear, image.segments[k].data, s->size))
                rc = WIN_NE_IO_ERROR;
        }
        win_ne_free_image(&image);
    }
    if (rc) {                           /* its slots stay, unused */
        for (uint16_t k = 0; k < d->count; k++) {
            win_ne_guest_segment *s = &g->dll_segments[d->first + k];
            if (s->linear)
                g->io.free(g->opaque, s->linear);
            memset(s, 0, sizeof(*s));
        }
        d->count = 0;
        return -1;
    }
    if (info.cs_index) {
        d->cs = g->dll_segments[d->first + info.cs_index - 1u].selector;
        d->ip = info.ip;
    }
    d->order = ++g->dll_order;
    return i;
}

int win_ne_guest_load_library(win_ne_guest_image *g, const char *name)
{
    uint16_t before = g->dll_count;
    int d = load_dll(g, name, 0);
    /* new descriptors: into the guest's GDT (and new thunks, made as the
     * DLLs were resolved) */
    if (d >= 0 && g->dll_count != before) {
        if (!g->io.write(g->opaque, g->gdt_linear, g->gdt, (uint32_t)g->gdt_limit + 1u))
            return -1;
        for (uint16_t t = 0; t < g->thunk_count; t++) {
            uint8_t p[THUNK_BYTES];
            thunk_bytes(g, g->thunk_services[t], (uint16_t)(t * THUNK_BYTES), p);
            if (!g->io.write(g->opaque, g->thunk_linear + t * THUNK_BYTES, p, sizeof(p)))
                return -1;
        }
    }
    return d;
}

uint32_t win_ne_guest_dll_proc(win_ne_guest_image *g, int dll, uint16_t ordinal,
                               const char *name)
{
    uint16_t sel, off;
    if (dll < 0 || dll >= g->dll_count || !g->dlls[dll].count ||
        !dll_export(g, dll, ordinal, name, &sel, &off))
        return 0;
    return (uint32_t)sel << 16 | off;
}

uint16_t win_ne_guest_dll_instance(const win_ne_guest_image *g, int dll)
{
    const win_ne_guest_dll *d = &g->dlls[dll];
    if (!d->count)
        return 0;
    return g->dll_segments[d->first + (d->auto_data ? d->auto_data : 1u) - 1u].selector;
}

int win_ne_guest_dll_of(const win_ne_guest_image *g, uint16_t instance)
{
    for (int i = 0; i < g->dll_count; i++) {
        const win_ne_guest_dll *d = &g->dlls[i];
        for (uint16_t k = 0; instance && k < d->count; k++)
            if ((g->dll_segments[d->first + k].selector & ~3u) == (instance & ~3u))
                return i;
    }
    return -1;
}

const char *win_ne_guest_path_of(const win_ne_guest_image *g, uint16_t instance,
                                 const char *program)
{
    int d = g ? win_ne_guest_dll_of(g, instance) : -1;
    return d >= 0 ? g->dlls[d].path : program;
}

win_ne_guest_segment *win_ne_guest_segment_of(win_ne_guest_image *g, uint16_t sel)
{
    sel &= (uint16_t)~3u;
    for (uint16_t i = 1; i <= g->info.segment_count; i++)
        if ((g->segments[i].selector & ~3u) == sel && g->segments[i].linear &&
            !(g->segments[i].flags & WIN_NE_GUEST_ALIAS))
            return &g->segments[i];
    for (unsigned i = 0; i < g->dll_segment_count; i++)
        if (g->dll_segments[i].linear && (g->dll_segments[i].selector & ~3u) == sel)
            return &g->dll_segments[i];
    return win_ne_guest_global(g, sel);
}

int win_ne_guest_grow(win_ne_guest_image *g, uint16_t sel, uint32_t bytes,
                      int (*read)(void *, uint32_t, void *, uint32_t))
{
    win_ne_guest_segment *s = win_ne_guest_segment_of(g, sel);
    if (!s || !(s->flags & 1u) || bytes < s->size)
        return 0;
    return bytes == s->size || resize(g, s, bytes, read);
}

/* ---------------------------------------------------------------- self-loading modules */

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* The module database, as Windows 3.x keeps one (its loader reads the
 * segment table, the module references, the imported names through
 * hModule): the NE header, its offsets near pointers into the database;
 * the segment table of 10-byte entries (sector, length, flags, minimum
 * size, handle); the resource, resident-name, module-reference (the
 * modules' handles), imported-name and entry tables as on disk; the file's
 * OFSTRUCT (fileinfo). */
#define DB_SEGMENT_BYTES 10u
#define DB_OFSTRUCT_BYTES 136u

uint16_t win_ne_guest_module_db(win_ne_guest_image *g,
                                uint16_t (*modref)(void *opaque, const char *module),
                                void *opaque)
{
    if (!g || g->module_db)
        return g ? g->module_db : 0;
    FILE *f = fopen(g->path, "rb");
    if (!f)
        return 0;
    uint8_t mz[64], h[64], *db = NULL;
    uint16_t sel = 0;
    long base;
    if (fread(mz, 1, sizeof(mz), f) != sizeof(mz) || get16(mz) != 0x5a4du)
        goto out;
    base = (long)(get16(mz + 0x3c) | (uint32_t)get16(mz + 0x3e) << 16);
    if (fseek(f, base, SEEK_SET) || fread(h, 1, sizeof(h), f) != sizeof(h) ||
        get16(h) != 0x454eu)
        goto out;
    uint16_t nseg = get16(h + 0x1c), nmod = get16(h + 0x1e);
    uint16_t segtab = get16(h + 0x22), rsrc = get16(h + 0x24), restab = get16(h + 0x26);
    uint16_t modtab = get16(h + 0x28), imptab = get16(h + 0x2a);
    uint16_t enttab = get16(h + 0x04), entlen = get16(h + 0x06);
    /* the tables in their usual order after the segment table */
    if (nseg != g->info.segment_count || segtab < 64u ||
        (uint32_t)segtab + nseg * 8u > rsrc || rsrc > restab || restab > modtab ||
        modtab + nmod * 2u > imptab || imptab > enttab)
        goto out;
    uint32_t tables = (uint32_t)enttab + entlen - rsrc;
    uint32_t seg_at = 64u, rest_at = seg_at + nseg * DB_SEGMENT_BYTES;
    uint32_t file_at = (rest_at + tables + 1u) & ~1u;
    uint32_t bytes = file_at + DB_OFSTRUCT_BYTES;
    if (bytes > 65536u || !(db = (uint8_t *)calloc(bytes, 1)))
        goto out;
    uint16_t delta = (uint16_t)(rest_at - rsrc);
    memcpy(db, h, sizeof(h));
    put16(db + 0x02, 1);                            /* used once */
    put16(db + 0x04, (uint16_t)(enttab + delta));
    put16(db + 0x06, 0);                            /* the next module: none */
    put16(db + 0x08, g->info.auto_data ?
          (uint16_t)(seg_at + (g->info.auto_data - 1u) * DB_SEGMENT_BYTES) : 0);
    put16(db + 0x0a, (uint16_t)file_at);
    put16(db + 0x22, (uint16_t)seg_at);
    put16(db + 0x24, (uint16_t)(rsrc + delta));
    put16(db + 0x26, (uint16_t)(restab + delta));
    put16(db + 0x28, (uint16_t)(modtab + delta));
    put16(db + 0x2a, (uint16_t)(imptab + delta));
    put16(db + 0x38, 0);
    put16(db + 0x3a, 0);
    for (uint16_t i = 0; i < nseg; i++) {
        uint8_t e[8];
        if (fseek(f, base + segtab + i * 8L, SEEK_SET) || fread(e, 1, sizeof(e), f) != sizeof(e))
            goto out;
        uint8_t *d = db + seg_at + i * DB_SEGMENT_BYTES;
        memcpy(d, e, sizeof(e));
        if (i == 0) {                                   /* the loader's: loaded */
            put16(d + 4, (uint16_t)(get16(e + 4) | 0x0006u));
            put16(d + 8, g->segments[1].selector);
        }
    }
    if (fseek(f, base + rsrc, SEEK_SET) ||
        fread(db + rest_at, 1, tables, f) != tables)
        goto out;
    for (uint16_t i = 0; i < nmod; i++) {               /* the names: their handles */
        uint8_t *m = db + rest_at + (modtab - rsrc) + i * 2u;
        uint32_t at = rest_at + (imptab - rsrc) + get16(m);
        char name[16] = "";
        if (at < rest_at + tables && db[at] < sizeof(name) &&
            at + 1u + db[at] <= rest_at + tables) {
            memcpy(name, db + at + 1u, db[at]);
            name[db[at]] = 0;
        }
        put16(m, modref && name[0] ? modref(opaque, name) : 0);
    }
    uint8_t *of = db + file_at;                         /* OFSTRUCT */
    size_t n = strlen(g->path);
    if (n > 127)
        n = 127;
    of[0] = (uint8_t)(8u + n + 1u);
    of[1] = 1;                                          /* a fixed disk */
    memcpy(of + 8, g->path, n);
    sel = win_ne_guest_global_alloc(g, bytes);
    win_ne_guest_segment *s = sel ? win_ne_guest_global(g, sel) : NULL;
    if (s && !g->io.write(g->opaque, s->linear, db, bytes)) {
        win_ne_guest_global_free(g, sel);
        sel = 0;
    }
    g->module_db = sel;
out:
    free(db);
    fclose(f);
    return sel;
}

uint16_t win_ne_guest_myalloc(win_ne_guest_image *g, uint16_t flags, uint32_t bytes)
{
    if (!g || bytes > 65536u || !(g->info.module_flags & WIN_NE_SELF_LOADING))
        return 0;
    /* code but for DATA (and DATA, ALLOCATED, LOADED), as Windows has it */
    int code = (flags & 7u) != 1u && (flags & 7u) != 7u;
    const uint16_t taken = WIN_NE_GUEST_MYALLOC | WIN_NE_GUEST_ALIAS;
    win_ne_guest_segment *s = NULL;
    if (g->loading >= 2 && g->loading <= g->info.segment_count &&
        !g->segments[g->loading].linear && !(g->segments[g->loading].flags & taken))
        s = &g->segments[g->loading];
    for (uint16_t i = 2; !s && i <= g->info.segment_count; i++)
        if (!g->segments[i].linear && g->segments[i].selector &&
            !(g->segments[i].flags & taken))
            s = &g->segments[i];
    if (!bytes) {                       /* a handle, no memory yet (GlobalReAlloc gives it) */
        if (!s)
            return 0;
        s->size = 0;
        s->flags = (uint16_t)((flags & 0x0ffeu) | (code ? 0u : 1u) | WIN_NE_GUEST_MYALLOC);
        return s->selector;
    }
    if (!s) {                                           /* a global block */
        uint16_t sel = win_ne_guest_global_alloc(g, bytes);
        win_ne_guest_segment *b = sel ? win_ne_guest_global(g, sel) : NULL;
        if (b && code && !set_descriptor(g, sel, b->linear, b->size, 1)) {
            win_ne_guest_global_free(g, sel);
            return 0;
        }
        return sel;
    }
    uint32_t linear;
    if (!guest_alloc(g, bytes, &linear))
        return 0;
    uint8_t zero[256];
    memset(zero, 0, sizeof(zero));
    for (uint32_t at = 0; at < bytes; at += sizeof(zero))
        if (!g->io.write(g->opaque, linear + at, zero,
                         bytes - at < sizeof(zero) ? bytes - at : (uint32_t)sizeof(zero)))
            goto fail;
    if (!set_descriptor(g, s->selector, linear, bytes, code))
        goto fail;
    s->linear = linear;
    s->size = bytes;
    s->flags = (uint16_t)((flags & 0x0ffeu) | (code ? 0u : 1u) | WIN_NE_GUEST_MYALLOC);
    return s->selector;
fail:
    g->io.free(g->opaque, linear);
    return 0;
}

int win_ne_guest_selfloaded(win_ne_guest_image *g, uint16_t k, uint16_t handle,
                            int (*read)(void *, uint32_t, void *, uint32_t))
{
    if (!g || k < 2 || k > g->info.segment_count || !handle)
        return 0;
    uint16_t j = 0;
    for (uint16_t i = 2; i <= g->info.segment_count && !j; i++)
        if ((g->segments[i].linear || (g->segments[i].flags & WIN_NE_GUEST_MYALLOC)) &&
            !(g->segments[i].flags & WIN_NE_GUEST_ALIAS) &&
            (g->segments[i].selector & ~7u) == (handle & ~7u))
            j = i;
    if (j && j != k) {                  /* allocated in another's slot: the two swapped */
        win_ne_guest_segment t = g->segments[j];
        g->segments[j] = g->segments[k];
        g->segments[k] = t;
    } else if (!j) {                    /* a global block: segment k is it */
        const win_ne_guest_segment *b = win_ne_guest_global(g, handle);
        if (!b || (g->segments[k].linear && !(g->segments[k].flags & WIN_NE_GUEST_ALIAS)))
            return 0;
        g->segments[k] = *b;
        g->segments[k].flags |= WIN_NE_GUEST_ALIAS;
    }
    win_ne_guest_segment *s = &g->segments[k];
    const win_ne_guest_segment *db = g->module_db ? win_ne_guest_global(g, g->module_db) : NULL;
    uint8_t e[DB_SEGMENT_BYTES], h[64];
    uint32_t at = 0;
    if (db && read && read(NULL, db->linear, h, sizeof(h))) {
        at = db->linear + get16(h + 0x22) + (k - 1u) * DB_SEGMENT_BYTES;
        if ((uint32_t)get16(h + 0x22) + k * DB_SEGMENT_BYTES > db->size ||
            !read(NULL, at, e, sizeof(e)))
            at = 0;
    }
    if (at) {                           /* the database's entry: loaded, its handle */
        put16(e + 4, (uint16_t)(get16(e + 4) | 0x0006u));
        put16(e + 8, s->selector);
        g->io.write(g->opaque, at, e, sizeof(e));
    }
    if (k == g->info.auto_data && (s->flags & 1u) && !(s->flags & WIN_NE_GUEST_ALIAS)) {
        /* DGROUP: its local heap and its stack after what the loader has */
        uint32_t need = at && get16(e + 6) ? get16(e + 6) : (at ? 65536u : s->size);
        need += (uint32_t)g->info.heap_size + g->info.stack_size;
        if (need > 65536u)
            need = 65536u;
        if (need > s->size && !win_ne_guest_grow(g, s->selector, need, read))
            return 0;
    }
    return 1;
}
