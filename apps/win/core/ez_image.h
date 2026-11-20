#ifndef WIN_EZ_IMAGE_H
#define WIN_EZ_IMAGE_H

#include <stdint.h>

typedef struct {
    void     *raw;              /* allocation to free */
    uint8_t  *base;
    void     *entry;
    uint32_t  image_size;       /* image memory size, for entry bounds */
    uint32_t  stack_size;       /* from the EZ header, 0 = default */
    uint32_t  dos_api_version;
} win_ez_image;
int    win_ez_load(const char *path, win_ez_image *img);

#endif
