/*
 * winmain_ez.c - entry point of a native Windows EZ application:
 * select the drivers, start the core and USER, run WinMain.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"

extern const win_display_driver win_disp_vbe8;
extern const win_input_driver win_input_dos;

int main(int argc, char **argv)
{
    static char cmdline[128];
    for (int i = 1; i < argc; i++) {
        if (strlen(cmdline) + strlen(argv[i]) + 2 >= sizeof(cmdline))
            break;
        if (i > 1)
            strcat(cmdline, " ");
        strcat(cmdline, argv[i]);
    }
    win_register_display_driver(&win_disp_vbe8);
    win_register_input_driver(&win_input_dos);
    int r = win_init(NULL, NULL, 0, 0, 8);
    if (r) {
        printf("Windows: display start failed (%d)\n", r);
        return 1;
    }
    user_init();
    r = WinMain(1, 0, cmdline, SW_SHOWNORMAL);
    win_shutdown();
    return r;
}
