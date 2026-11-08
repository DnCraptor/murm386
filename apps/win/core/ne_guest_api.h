/* Win16 x86 service dispatch, called in the original native WINN ARM thread. */
#ifndef WINN_NE_GUEST_API_H
#define WINN_NE_GUEST_API_H

#include "x86bridge.h"
#include "ne_guest.h"

typedef struct {
    const win_ne_guest_image *image;
    int (*read)(void *opaque, uint32_t linear, void *dest, uint32_t bytes);
    void *opaque;
    int invalid_args;  /* latched; reported only after the CPU leaves PE */
} win_ne_guest_api_context;

/* Only the declared zero/one-word/far-pointer Win16 PASCAL signatures.
 * Never calls DOS or switches native tasks with CR0.PE active. */
int win_ne_guest_api(void *opaque, CPU *cpu, uint16_t service);

#endif /* WINN_NE_GUEST_API_H */
