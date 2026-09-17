#ifndef GFX_SCRATCH_H
#define GFX_SCRATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool gfx_scratch_acquire(size_t offset, size_t bytes, const char *backup_path,
                         uint8_t **scratch);
bool gfx_scratch_release(size_t offset, size_t bytes, const char *backup_path);

#endif
