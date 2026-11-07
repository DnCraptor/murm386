#include "gfx_scratch.h"

#include "ff.h"

#include <stdlib.h>
#include <string.h>

extern uint8_t gfx_buffer[];

static FIL *open_backup(const char *path, BYTE mode)
{
    FIL *file = (FIL *)calloc(1, sizeof(*file));
    if (!file)
        return NULL;
    if (f_open(file, path, mode) != FR_OK) {
        free(file);
        return NULL;
    }
    return file;
}

static bool xfer_all(FIL *file, bool write, void *ptr, size_t bytes)
{
    UINT done = 0;
    FRESULT fr = write ? f_write(file, ptr, (UINT)bytes, &done)
                       : f_read(file, ptr, (UINT)bytes, &done);
    return fr == FR_OK && done == bytes;
}

bool gfx_scratch_acquire_ex(size_t offset, size_t bytes, const char *backup_path,
                            const gfx_scratch_seg_t *extra, int extra_count,
                            uint8_t **scratch)
{
    if (!backup_path || !scratch || !bytes)
        return false;

    FIL *backup = open_backup(backup_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (!backup)
        return false;

    bool ok = xfer_all(backup, true, gfx_buffer + offset, bytes);
    for (int i = 0; ok && i < extra_count; i++)
        ok = xfer_all(backup, true, extra[i].ptr, extra[i].bytes);
    FRESULT close_fr = f_close(backup);
    free(backup);
    if (!ok || close_fr != FR_OK) {
        f_unlink(backup_path);
        return false;
    }

    *scratch = gfx_buffer + offset;
    return true;
}

bool gfx_scratch_release_ex(size_t offset, size_t bytes, const char *backup_path,
                            const gfx_scratch_seg_t *extra, int extra_count)
{
    if (!backup_path || !bytes)
        return false;

    FIL *backup = open_backup(backup_path, FA_READ);
    if (!backup)
        return false;

    bool ok = xfer_all(backup, false, gfx_buffer + offset, bytes);
    for (int i = 0; ok && i < extra_count; i++)
        ok = xfer_all(backup, false, extra[i].ptr, extra[i].bytes);
    FRESULT close_fr = f_close(backup);
    free(backup);
    if (!ok || close_fr != FR_OK)
        return false;

    f_unlink(backup_path);
    return true;
}

bool gfx_scratch_acquire(size_t offset, size_t bytes, const char *backup_path,
                         uint8_t **scratch)
{
    return gfx_scratch_acquire_ex(offset, bytes, backup_path, NULL, 0, scratch);
}

bool gfx_scratch_release(size_t offset, size_t bytes, const char *backup_path)
{
    return gfx_scratch_release_ex(offset, bytes, backup_path, NULL, 0);
}
