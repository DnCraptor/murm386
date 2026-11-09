/* Host stand-ins for what the emulator's i386.c takes from the Pico SDK,
 * so that test_ne_run steps NE guests on the very CPU core of the firmware
 * (built for the host; the source is not changed). Included first
 * (-include) in that one compilation. */
#ifndef X86RUN_PICO_H
#define X86RUN_PICO_H
#include <stdint.h>
#include <stdbool.h>
#define __not_in_flash(x)
#define __not_in_flash_func(f) f
#define __time_critical_func(f) f
#define __force_inline inline __attribute__((always_inline))
#define IRAM_ATTR
#endif
