/*
 * winmain_ez.c - WINN.EXE: the Windows loader.
 *
 *   WINN [program [arguments]]
 *
 * Starts the drivers named in SYSTEM.INI (or the built-in ones), GDI and
 * USER, then runs the shell given by [boot] shell= and the program from the
 * command line, each as its own task. Windows ends when the first task
 * started (the shell, or the program if there is no shell) ends.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"

extern const win_display_driver win_disp_vbe8;
extern const win_input_driver win_input_dos;

static const char *exec_error(UINT r)
{
    switch (r) {
    case 2: return "file not found";
    case 8: return "out of memory";
    case 11: return "not a Windows program";
    default: return "cannot start";
    }
}

static int run(const char *cmd)
{
    UINT r = WinExec(cmd, SW_SHOWNORMAL);
    if (r >= 32)
        return 1;
    char msg[160];
    wsprintf(msg, "Cannot run %s: %s.", cmd, exec_error(r));
    MessageBox(0, msg, "Windows", MB_OK | MB_ICONSTOP);
    return 0;
}

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
    task_init();
    int r = win_boot(argc > 0 ? argv[0] : NULL, &win_disp_vbe8, &win_input_dos);
    if (r) {
        printf("Windows: display start failed (%d)\n", r);
        return 1;
    }
    char shell[80];
    GetPrivateProfileString("boot", "shell", "", shell, sizeof(shell), "SYSTEM.INI");
    int started = 0;
    if (shell[0])
        started += run(shell);
    if (cmdline[0])
        started += run(cmdline);
    if (!started && !shell[0] && !cmdline[0])
        MessageBox(0, "Nothing to run: give a program on the command line\n"
                      "(WINN DEMO) or set shell= in [boot] of SYSTEM.INI.",
                   "Windows", MB_OK | MB_ICONINFORMATION);
    if (started)
        task_kernel_loop();
    win_shutdown();
    return 0;
}
