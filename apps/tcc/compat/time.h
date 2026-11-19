#ifndef TCC_FDOS_TIME_H
#define TCC_FDOS_TIME_H
#include <stdint.h>
typedef long clock_t;
typedef int64_t time_t;
#define CLOCKS_PER_SEC 1000L
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};
clock_t clock(void);
struct tm *localtime(const time_t *value);
#endif
