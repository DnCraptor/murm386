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

bool gfx_scratch_acquire(size_t offset, size_t bytes, const char *backup_path,
                         uint8_t **scratch)
{
    if (!backup_path || !scratch || !bytes)
        return false;

    FIL *backup = open_backup(backup_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (!backup)
        return false;

    UINT bw = 0;
    FRESULT fr = f_write(backup, gfx_buffer + offset, (UINT)bytes, &bw);
    FRESULT close_fr = f_close(backup);
    free(backup);
    if (fr != FR_OK || close_fr != FR_OK || bw != bytes) {
        f_unlink(backup_path);
        return false;
    }

    *scratch = gfx_buffer + offset;
    return true;
}

bool gfx_scratch_release(size_t offset, size_t bytes, const char *backup_path)
{
    if (!backup_path || !bytes)
        return false;

    FIL *backup = open_backup(backup_path, FA_READ);
    if (!backup)
        return false;

    UINT br = 0;
    FRESULT fr = f_read(backup, gfx_buffer + offset, (UINT)bytes, &br);
    FRESULT close_fr = f_close(backup);
    free(backup);
    if (fr != FR_OK || close_fr != FR_OK || br != bytes)
        return false;

    f_unlink(backup_path);
    return true;
}
