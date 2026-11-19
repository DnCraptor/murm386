/*
 * winreg.h - WINN's registry (core/winreg.c): keys and values kept in
 * WINNREG.INI next to WINN.EXE, a section a key. HKEY_CLASSES_ROOT is
 * Windows 3.1's registration database (SHELL's RegOpenKey ...); Win32
 * programs' other roots (HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE ...) are
 * keys of their own names beside it. A handle belongs to the task that
 * opened it.
 *
 * The functions return Win32's error codes (ERROR_SUCCESS 0).
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#ifndef WINN_WINREG_H
#define WINN_WINREG_H

#include <stdint.h>
#include "windows.h"

#define WINREG_SUCCESS          0ul
#define WINREG_FILE_NOT_FOUND   2ul
#define WINREG_ACCESS_DENIED    5ul
#define WINREG_INVALID_HANDLE   6ul
#define WINREG_NOT_ENOUGH_MEMORY 8ul
#define WINREG_INVALID_PARAMETER 87ul
#define WINREG_MORE_DATA        234ul
#define WINREG_NO_MORE_ITEMS    259ul

#define WINREG_SZ 1ul
#define WINREG_DATA_BYTES 504u      /* a value's bytes at most */
#define WINREG_NAME_BYTES 128u      /* a key's path, a value's name */

/* a key (an open one, or a root) and a subkey of it (NULL, "": itself):
 * opened (created, with the keys on the way, with create); *out its
 * handle (the key itself for a root and no subkey) */
uint32_t winreg_open(HTASK task, uint32_t key, const char *sub, int create, uint32_t *out);
uint32_t winreg_close(HTASK task, uint32_t key);
int winreg_key_exists(HTASK task, uint32_t key, const char *sub);
/* a subkey and the keys under it */
uint32_t winreg_delete_key(HTASK task, uint32_t key, const char *sub);
/* the index-th subkey's name (cap bytes with its NUL) */
uint32_t winreg_enum_key(HTASK task, uint32_t key, uint32_t index, char *name, uint32_t cap);
/* a value of the key's (name "" or NULL: its default one); a subkey's
 * default (sub; RegSetValue makes the subkey) */
uint32_t winreg_set(HTASK task, uint32_t key, const char *sub, const char *name, uint32_t type,
                    const void *data, uint32_t n);
/* its type, its bytes (a string's with its NUL) in WINN's buffer, until
 * the next call */
uint32_t winreg_get(HTASK task, uint32_t key, const char *sub, const char *name, uint32_t *type,
                    const uint8_t **data, uint32_t *n);
uint32_t winreg_delete_value(HTASK task, uint32_t key, const char *name);
/* the index-th value: its name ("" the default one), type, bytes */
uint32_t winreg_enum_value(HTASK task, uint32_t key, uint32_t index, const char **name,
                           uint32_t *type, const uint8_t **data, uint32_t *n);
/* RegQueryInfoKey's counts: subkeys, the longest subkey name, values, the
 * longest value name, the largest value (any NULL) */
uint32_t winreg_info(HTASK task, uint32_t key, uint32_t *keys, uint32_t *key_len,
                     uint32_t *values, uint32_t *value_len, uint32_t *data_len);
/* the task's handles closed */
void winreg_task_end(HTASK task);

#endif /* WINN_WINREG_H */
