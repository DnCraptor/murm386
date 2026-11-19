#ifndef TCC_FDOS_COMPAT_H
#define TCC_FDOS_COMPAT_H
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#define EOF (-1)
#define STDIN_FILENO 0
#define F_OK 0
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
char *getenv(const char *);
char *getcwd(char *, size_t);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
float strtof(const char *, char **);
double strtod(const char *, char **);
long double strtold(const char *, char **);
double ldexp(double, int);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
#endif
