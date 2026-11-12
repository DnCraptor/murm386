/*
 * winmain_ez.c - WINN.EXE: the Windows loader.
 *
 *   WINN [:] [program [arguments]]
 *
 * Starts the drivers named in SYSTEM.INI (or the built-in ones), GDI and
 * USER, then runs the shell given by [boot] shell= (PROGMAN.EXE when the
 * key is missing) and the program from the command line, each as its own
 * task. Windows ends when the first task
 * started (the shell, or the program if there is no shell) ends. The
 * start-up logo (logo.c) is up until then; ":" leaves it out.
 *
 * Copyright (c) 2026 murm386 contributors. MIT License.
 */
#include <stdio.h>
#include <string.h>
#include "wincore.h"
#include "ne.h"
#include "ne_guest.h"

/* the built-in display drivers: VGA 640x480x16 as Windows' own, VBE
 * 640x400x256 when it cannot */
extern const win_display_driver win_disp_vga, win_disp_vbe8;
extern const win_input_driver win_input_dos;
extern const win_sound_driver win_sound_dos;

static const char *exec_error(UINT r)
{
    switch (r) {
    case 2: return "file not found";
    case 8: return "out of memory";
    case 11: return "not a Native Windows application";
    case WIN_EXEC_NE_REQUIRES_386: return "Win16 NE requires a firmware build with a 386+ CPU core";
    case WIN_EXEC_NE_UNSUPPORTED: return "Win16 NE format not yet supported";
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
    win_logo_end();                 /* the message must be seen */
    MessageBox(0, msg, "Windows", MB_OK | MB_ICONSTOP);
    return 0;
}

int main(int argc, char **argv)
{
    static char cmdline[128];
    int first = 1;
    if (argc > 1 && !strcmp(argv[1], ":")) {
        win_logo_skip();
        first = 2;
    }
    for (int i = first; i < argc; i++) {
        if (strlen(cmdline) + strlen(argv[i]) + 2 >= sizeof(cmdline))
            break;
        if (i > first)
            strcat(cmdline, " ");
        strcat(cmdline, argv[i]);
    }
    task_init();
    static const win_display_driver *const displays[] = { &win_disp_vga, &win_disp_vbe8, NULL };
    win_register_sound_driver(&win_sound_dos);
    int r = win_boot(argc > 0 ? argv[0] : NULL, displays, &win_input_dos);
    if (r) {
        printf("Windows: display start failed (%d)\n", r);
        return 1;
    }
    win_logo_show();                /* up until the shell is first idle */
    /* no shell= at all: PROGMAN.EXE, as in Windows 3.x, if it is there;
     * "shell=" with nothing: no shell */
    char shell[80];
    GetPrivateProfileString("boot", "shell", "*", shell, sizeof(shell), "SYSTEM.INI");
    if (!lstrcmp(shell, "*")) {
        char path[128];
        win_path("PROGMAN.EXE", path, sizeof(path));
        lstrcpy(shell, IsWinAppN(path) ? "PROGMAN.EXE" : "");
    }
    int started = 0;
    if (shell[0])
        started += run(shell);
    if (cmdline[0])
        started += run(cmdline);
    if (!started)
        win_logo_end();
    if (!started && !shell[0] && !cmdline[0])
        MessageBox(0, "Nothing to run: give a program on the command line\n"
                      "(WINN DEMO) or set shell= in [boot] of SYSTEM.INI.",
                   "Windows", MB_OK | MB_ICONINFORMATION);
    if (started)
        task_kernel_loop();
    win_shutdown();
    win_ne_guest_mem_end();         /* the programs' extended memory back */
    return 0;
}
