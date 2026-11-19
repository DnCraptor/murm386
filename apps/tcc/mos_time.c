/* Compiler clock and SOURCE_DATE_EPOCH policy for native FDOS. */
#include <stdint.h>
#include <stdlib.h>
#include "mos_time.h"
#if defined(__arm__)
#include "fdos_compat.h"
#endif

unsigned mos_clock_ms(void)
{
    return (unsigned)((uint64_t)clock() * 1000u / CLOCKS_PER_SEC);
}

int mos_tcc_timestamp(time_t *value)
{
    const char *p = getenv("SOURCE_DATE_EPOCH");
    uint64_t seconds = 0;
    if (!p)
        return -1;
    if (!*p)
        return -2;
    for (; *p; ++p) {
        unsigned digit = (unsigned)(*p - '0');
        if (digit > 9 || seconds > (UINT64_C(253402300799) - digit) / 10)
            return -2;
        seconds = seconds * 10 + digit;
    }
    *value = (time_t)seconds;
    if (*value < 0 || (uint64_t)*value != seconds)
        return -2;
    return 0;
}
