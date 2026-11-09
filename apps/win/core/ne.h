/*
 * ne.h - Win16 NE metadata reader and relocatable staged image loader.
 *
 * Allocates and relocates segment data using selectors and native API thunk
 * addresses supplied by the caller. It does not install CPU descriptors,
 * select a protected-mode context or execute the NE entry point.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_NE_H
#define WINN_NE_H

#include <stdint.h>

enum {
    WIN_NE_OK = 0,
    WIN_NE_NO_FILE = -1,
    WIN_NE_NOT_NE = -2,
    WIN_NE_BAD_IMAGE = -3,
    WIN_NE_IO_ERROR = -4,
    WIN_NE_CANCELLED = -5,
    WIN_NE_UNSUPPORTED = -6,
    WIN_NE_NO_MEMORY = -7,
    WIN_NE_UNRESOLVED = -8
};

/* WINN-specific WinExec error codes (WinExec treats values < 32 as errors). */
enum {
    WIN_EXEC_NE_REQUIRES_386 = 12,
    WIN_EXEC_NE_UNSUPPORTED = 13
};

typedef enum {
    WIN_NE_LAUNCH_OTHER = 0,       /* not a valid Windows NE image */
    WIN_NE_LAUNCH_READY,           /* eligible for the restricted 386 runner */
    WIN_NE_LAUNCH_REQUIRES_386,    /* valid Windows NE, but CPU < 386 */
    WIN_NE_LAUNCH_UNSUPPORTED      /* Windows NE layout not yet supported */
} win_ne_launch_status;

typedef struct {
    uint16_t cs_index, ip, ss_index, sp;
    uint16_t segment_count, module_count;
    uint16_t auto_data;            /* the DGROUP segment, 0: none */
    uint16_t heap_size, stack_size;     /* added to it when it is loaded */
    uint32_t fixup_records, import_records;
    uint8_t target_os;
} win_ne_info;

/* This describes one IMPORT fixup record, not each location in its source
 * chain. module_index is 1-based as in the NE file. ordinal is zero for a
 * name import. Source types and relocation flags are the on-disk NE values.
 */
typedef struct {
    uint16_t segment_index, source_offset, module_index, ordinal;
    uint8_t source_type, flags;
    char module[256];
    char procedure[256];
} win_ne_import;

typedef int (*win_ne_import_cb)(void *opaque, const win_ne_import *imp);

/* A staged NE image. Segment slots are 1-based, matching NE fixup indices.
 * data buffers are owned by the image and may later be copied into guest
 * memory at the linear base of each protected-mode selector. The selector
 * values are provided by the caller; this loader does not install the GDT.
 */
typedef struct {
    uint8_t *data;
    uint32_t size;             /* allocated, zero-initialised bytes (max 65536) */
    uint16_t selector;
    uint16_t flags;
} win_ne_segment_image;

typedef struct {
    win_ne_info info;
    win_ne_segment_image *segments; /* array [0..segment_count], slot 0 unused */
} win_ne_image;

/* Assign a selector for a loaded segment. Must return 1 on success.
 * The caller retains ownership of selectors / descriptor allocations.
 * A failure rolls back image buffers but not external selector allocations.
 */
typedef int (*win_ne_selector_cb)(void *opaque, uint16_t index,
                                  uint16_t flags, uint32_t size,
                                  uint16_t *selector);

/* Resolve a target DLL import to a guest-callable FAR address. The service
 * thunk allocator can use imp.module, imp.ordinal or imp.procedure. The
 * callback must NOT return an ARM function pointer as a guest address.
 * Return 1 with a selector:offset, or 0 if unresolved.
 */
typedef int (*win_ne_resolve_cb)(void *opaque, const win_ne_import *imp,
                                 uint16_t *selector, uint16_t *offset);

/* Reads, allocates, and relocates NE segments in host memory, including
 * 16-bit fixed internal and imported source chains. It does not yet launch
 * Win16 applications or allocate guest physical memory / descriptors.
 * Supports fixed and movable internal entry fixups; rejects OS fixups.
 * On failure frees all buffers, clears image, and reports a negative code.
 */
int win_ne_load_image(const char *path, win_ne_image *image,
                      win_ne_selector_cb assign, win_ne_resolve_cb resolve,
                      void *opaque);
void win_ne_free_image(win_ne_image *image);

/* On success fills info and invokes cb for each import relocation record.
 * Callback returning nonzero aborts with WIN_NE_CANCELLED. No guest state is
 * modified. Output is cleared on failure. */
int win_ne_scan(const char *path, win_ne_info *info,
                win_ne_import_cb cb, void *opaque);

/* Classify the file independently of CPU capability. A valid Windows NE
 * must not be diagnosed as an invalid native ARM/EZ application on a 286.
 * This only checks the current runner's segment layout, not its supported
 * x86 instructions/imports (those are validated at task execution). */
win_ne_launch_status win_ne_classify_launch(const char *path, unsigned cpu_gen);

/* A resource of the program: its type (RT_MENU 4, RT_DIALOG 5, RT_STRING 6,
 * RT_ACCELERATOR 9 ...), and its number id or, not NULL, its name ("#12"
 * is number 12). WIN_NE_OK with the bytes in *data (malloc'd: the caller
 * frees them), WIN_NE_UNRESOLVED when there is none. */
int win_ne_resource(const char *path, uint16_t type, uint16_t id, const char *name,
                    uint8_t **data, uint32_t *bytes);

#endif
