#ifndef TINY_MURM386_NATIVE_H
#define TINY_MURM386_NATIVE_H

/* Turbo C text / timing adapters for the existing murm386 native DOS API. */
#include <stddef.h>
#include <stdint.h>
#include "dos.h"
#include "conio.h"
#include "io.h"
#include "stdlib.h"
#include "stdio.h"
#include "dos_yield.h"

typedef uint32_t clock_t;
#define CLOCKS_PER_SEC 1000000UL
#define clock tiny_clock
#define time(x) tiny_time(x)
#define rand tiny_rand
#define srand tiny_srand
#define int86 int386

/* Turbo C video-mode constants used by TINY. */
#define BW40 0
#define BW80 2
#define C80 3
#define MONO 7

struct text_info { unsigned char currmode; };
struct time { unsigned char ti_hour, ti_min, ti_sec, ti_hund; };

clock_t tiny_clock(void);
unsigned long tiny_time(void *unused);
int tiny_rand(void);
void tiny_srand(unsigned seed);
char *tiny_fgets(char *dst, size_t len);
int tiny_fgetc(FILE *fp);
#ifndef EOF
#define EOF (-1)
#endif
#define fgetc tiny_fgetc

void tiny_init_screen(void);
void tiny_sync_cursor(void);
void tiny_window(int x1, int y1, int x2, int y2);
void tiny_gotoxy(int x, int y);
int tiny_wherex(void);
int tiny_wherey(void);
void tiny_textattr(int attr);
void tiny_clrscr(void);
int tiny_putch(int ch);
int tiny_cputs(const char *s);
int tiny_cprintf(const char *fmt, ...);
int tiny_gettext(int x1, int y1, int x2, int y2, void *dst);
void tiny_gettextinfo(struct text_info *info);
void tiny_textmode(int mode);
void tiny_gettime(struct time *time);
void tiny_delay(unsigned ms);
void tiny_sound(unsigned hz);
void tiny_nosound(void);

#define window tiny_window
#define gotoxy tiny_gotoxy
#define wherex tiny_wherex
#define wherey tiny_wherey
#define textattr tiny_textattr
#define clrscr tiny_clrscr
#define putch tiny_putch
#define cputs tiny_cputs
#define cprintf tiny_cprintf
#define gettext tiny_gettext
#define gettextinfo tiny_gettextinfo
#define textmode tiny_textmode
#define gettime tiny_gettime
#define delay tiny_delay
#define sound tiny_sound
#define nosound tiny_nosound

#endif
