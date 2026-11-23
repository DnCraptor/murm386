/*
 * WIN.COM loads the Windows core into the native process heap.
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dos_api_version.h"
#include "ez.h"
#include "ez_image.h"
#include "win_exit.h"

/* Keep the directory from argv[0], including a possible DOS drive prefix. */
static int core_path(const char *loader, char *path, size_t size)
{
    static const char name[] = "WINN.EXE";
    const char *base = loader;
    for (const char *p = loader; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    size_t prefix = (size_t)(base - loader);
    if (prefix + sizeof(name) > size)
        return 0;
    memcpy(path, loader, prefix);
    memcpy(path + prefix, name, sizeof(name));
    return 1;
}

int main(int argc, char **argv)
{
    char path[128];
    if (argc < 1 || !core_path(argv[0], path, sizeof(path))) {
        puts("WIN: cannot determine the path to WINN.EXE");
        return 1;
    }

    /* WINN again as it asks (Windows restarted: ExitWindowsExec's
     * program run, EW_RESTARTWINDOWS): its shell alone */
    int result = 1;
    for (int pass = 0;; pass++) {
        win_ez_image image;
        int rc = win_ez_load(path, &image);
        if (rc) {
            printf("WIN: cannot load %s (%s)\n", path,
                   rc == -1 ? "file not found" :
                   rc == -3 ? "out of memory" : "invalid EZ image");
            return 1;
        }
        const native_ez_process_info *process = native_ez_get_process_info();
        result = 1;
        if (image.dos_api_version > DOS_API_VERSION) {
            printf("WIN: %s requires native DOS API %lu\n",
                   path, (unsigned long)image.dos_api_version);
        } else if (process && image.stack_size > process->native_stack_size) {
            printf("WIN: %s requires a larger native stack\n", path);
        } else {
            int (*start)(int, char **) = (int (*)(int, char **))image.entry;
            char *loader = argv[0];
            argv[0] = path;
            result = start(pass ? 1 : argc, argv);
            argv[0] = loader;
        }
        free(image.raw);
        if (result != WIN_EXIT_RESTART)
            break;
    }
    return result;
}
