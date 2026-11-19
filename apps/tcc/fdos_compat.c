#include "fdos_compat.h"
#include "dos.h"
#include "dos_guest_ptr.h"
#include "ez.h"
#include "dos_api_version.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int errno;

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    do {
        if ((unsigned char)*s == (unsigned char)c)
            last = s;
    } while (*s++);
    return (char *)last;
}

char *strstr(const char *s, const char *needle)
{
    const size_t n = strlen(needle);
    if (!n)
        return (char *)s;
    for (; *s; ++s)
        if (*s == *needle && strncmp(s, needle, n) == 0)
            return (char *)s;
    return NULL;
}

/* The DOS PSP points to an environment stored in a conventional MCB. */
char *getenv(const char *name)
{
    union REGS regs = {0};
    regs.h.ah = 0x62;
    int386(0x21, &regs, &regs);
    const uint8_t *psp = dos_guest_far_ptr(regs.w.bx, 0);
    const uint16_t seg = psp[0x2c] | ((uint16_t)psp[0x2d] << 8);
    if (!seg)
        return NULL;
    const uint8_t *mcb = dos_guest_far_ptr(seg - 1, 0);
    if (mcb[0] != 'M' && mcb[0] != 'Z')
        return NULL;
    size_t size = (mcb[3] | ((unsigned)mcb[4] << 8)) * 16u;
    char *p = dos_guest_far_ptr(seg, 0);
    const size_t n = strlen(name);
    while (size && *p) {
        char *end = memchr(p, 0, size);
        if (!end)
            return NULL;
        if ((size_t)(end - p) > n && p[n] == '=' && !strncasecmp(p, name, n))
            return p + n + 1;
        size -= (size_t)(end + 1 - p);
        p = end + 1;
    }
    return NULL;
}

char *getcwd(char *buffer, size_t size)
{
    if (size < 4)
        return NULL;
    union REGS regs = {0};
    regs.h.ah = 0x48;
    regs.w.bx = 4;
    int386(0x21, &regs, &regs);
    if (regs.x.cflag)
        return NULL;
    const uint16_t scratch = regs.w.ax;
    struct SREGS sregs;
    segread(&sregs);
    sregs.ds = scratch;
    memset(&regs, 0, sizeof(regs));
    regs.h.ah = 0x47;
    int386x(0x21, &regs, &regs, &sregs);
    const char *dir = dos_guest_far_ptr(scratch, 0);
    const char *end = regs.x.cflag ? NULL : memchr(dir, 0, 64);
    char *result = NULL;
    if (end && (size_t)(end - dir) + 4 <= size) {
        regs.h.ah = 0x19;
        int386(0x21, &regs, &regs);
        buffer[0] = 'A' + regs.h.al;
        buffer[1] = ':';
        buffer[2] = '\\';
        memcpy(buffer + 3, dir, (size_t)(end - dir) + 1);
        result = buffer;
    }
    segread(&sregs);
    sregs.es = scratch;
    regs.h.ah = 0x49;
    int386x(0x21, &regs, &regs, &sregs);
    return result;
}

static void swap(uint8_t *a, uint8_t *b, size_t size)
{
    while (size--) {
        uint8_t tmp = *a;
        *a++ = *b;
        *b++ = tmp;
    }
}

static void sift(uint8_t *base, size_t root, size_t count, size_t size,
                 int (*compare)(const void *, const void *))
{
    while (root < count / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < count &&
            compare(base + child * size, base + (child + 1) * size) < 0)
            ++child;
        if (compare(base + root * size, base + child * size) >= 0)
            return;
        swap(base + root * size, base + child * size, size);
        root = child;
    }
}

void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *))
{
    for (size_t i = count / 2; i != 0;)
        sift(base, --i, count, size, compare);
    while (count > 1) {
        swap(base, (uint8_t *)base + --count * size, size);
        sift(base, 0, count, size, compare);
    }
}

/* ARM aggregate helpers emitted by the TinyCC backend. */
void __aeabi_memcpy(void *dst, const void *src, size_t sz) {
    (void)memcpy(dst, src, sz);
}
void __aeabi_memcpy4(void *dst, const void *src, size_t sz) {
    (void)memcpy(dst, src, sz);
}
void __aeabi_memcpy8(void *dst, const void *src, size_t sz) {
    (void)memcpy(dst, src, sz);
}
void __aeabi_memmove(void *dst, const void *src, size_t sz) {
    (void)memmove(dst, src, sz);
}
void __aeabi_memmove4(void *dst, const void *src, size_t sz) {
    (void)memmove(dst, src, sz);
}
void __aeabi_memmove8(void *dst, const void *src, size_t sz) {
    (void)memmove(dst, src, sz);
}
void __aeabi_memset(void *dst, size_t sz, int value) {
    (void)memset(dst, value, sz);
}
void __aeabi_memset4(void *dst, size_t sz, int value) {
    (void)memset(dst, value, sz);
}
void __aeabi_memset8(void *dst, size_t sz, int value) {
    (void)memset(dst, value, sz);
}
void __aeabi_memclr(void *dst, size_t sz) {
    (void)memset(dst, 0, sz);
}
void __aeabi_memclr4(void *dst, size_t sz) {
    (void)memset(dst, 0, sz);
}
void __aeabi_memclr8(void *dst, size_t sz) {
    (void)memset(dst, 0, sz);
}
