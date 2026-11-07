#ifndef GFX_SCRATCH_H
#define GFX_SCRATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool gfx_scratch_acquire(size_t offset, size_t bytes, const char *backup_path,
                         uint8_t **scratch);
bool gfx_scratch_release(size_t offset, size_t bytes, const char *backup_path);

/*
 * Same, with extra memory segments stored in the backup file after the
 * gfx_buffer bytes, in the given order, and read back into the same
 * places on release. Lets a caller park state it must restore later on
 * the SD card instead of keeping a copy in RAM.
 */
typedef struct {
    void *ptr;
    size_t bytes;
} gfx_scratch_seg_t;

bool gfx_scratch_acquire_ex(size_t offset, size_t bytes, const char *backup_path,
                            const gfx_scratch_seg_t *extra, int extra_count,
                            uint8_t **scratch);
bool gfx_scratch_release_ex(size_t offset, size_t bytes, const char *backup_path,
                            const gfx_scratch_seg_t *extra, int extra_count);

#endif
