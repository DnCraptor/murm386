/*
 * host_main.c - run native Windows applications on Linux under a script:
 *   winhost script.txt [PROGRAM]      (default DEMO.EXE)
 * The Windows directory is HOSTWIN/ (the Makefile puts stand-ins for the
 * application files there, PROGMAN.INI lands there too).
 * Script lines (executed one by one whenever the system is idle):
 *   wait MS | move X Y | down X Y | up X Y | click X Y | dblclick X Y |
 *   rdown X Y | rup X Y | key VK | keydown VK | keyup VK | type TEXT |
 *   snap FILE | exit
 * VK is a number (decimal or 0x..) or a letter/digit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wincore.h"
#include "host_drivers.h"

/* the applications, built in (WinMain renamed by the Makefile) */
int PASCAL demo_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show);
int PASCAL progman_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show);
int PASCAL notepad_WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show);

static int demo_entry(const win_app_host *h)
{
    return demo_WinMain(h->hinstance, 0, (LPSTR)h->cmdline, h->show);
}

static int progman_entry(const win_app_host *h)
{
    return progman_WinMain(h->hinstance, 0, (LPSTR)h->cmdline, h->show);
}

static int notepad_entry(const win_app_host *h)
{
    return notepad_WinMain(h->hinstance, 0, (LPSTR)h->cmdline, h->show);
}

win_app_entry win_host_app(const char *prog)
{
    const char *b = prog;
    for (const char *p = prog; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    if (!lstrcmpi(b, "DEMO.EXE"))
        return demo_entry;
    if (!lstrcmpi(b, "PROGMAN.EXE"))
        return progman_entry;
    if (!lstrcmpi(b, "NOTEPAD.EXE"))
        return notepad_entry;
    return NULL;
}

#ifdef HOST_VGA800
/* the VGA800 module's driver, built in for the test */
extern const win_display_driver win_disp_vga800;
int vga800_host_snapshot(const char *path);
#define SNAPSHOT vga800_host_snapshot
#else
#define SNAPSHOT host_snapshot
#endif

static char **lines;
static int nlines, cur;
static uint32_t wait_until;
static int mx, my;
static unsigned btn;

static int parse_vk(const char *s)
{
    if (s[0] && !s[1])
        return (s[0] >= 'a' && s[0] <= 'z') ? s[0] - 32 : s[0];
    if (!strcmp(s, "RETURN")) return VK_RETURN;
    if (!strcmp(s, "TAB")) return VK_TAB;
    if (!strcmp(s, "ESC")) return VK_ESCAPE;
    if (!strcmp(s, "SHIFT")) return VK_SHIFT;
    if (!strcmp(s, "CTRL")) return VK_CONTROL;
    if (!strcmp(s, "ALT")) return VK_MENU;
    if (!strcmp(s, "LEFT")) return VK_LEFT;
    if (!strcmp(s, "RIGHT")) return VK_RIGHT;
    if (!strcmp(s, "UP")) return VK_UP;
    if (!strcmp(s, "DOWN")) return VK_DOWN;
    if (!strcmp(s, "BACK")) return VK_BACK;
    if (!strcmp(s, "SPACE")) return VK_SPACE;
    if (!strcmp(s, "HOME")) return VK_HOME;
    if (!strcmp(s, "END")) return VK_END;
    if (!strcmp(s, "DELETE")) return VK_DELETE;
    if (!strcmp(s, "PGUP")) return VK_PRIOR;
    if (!strcmp(s, "PGDN")) return VK_NEXT;
    if (s[0] == 'F' && s[1] >= '1' && s[1] <= '9') return VK_F1 + atoi(s + 1) - 1;
    return (int)strtol(s, NULL, 0);
}

static void key(int vk, int down)
{
    static const struct { int vk, sc; } map[] = {
        { VK_RETURN, 0x1C }, { VK_TAB, 0x0F }, { VK_ESCAPE, 0x01 }, { VK_SHIFT, 0x2A },
        { VK_CONTROL, 0x1D }, { VK_MENU, 0x38 }, { VK_SPACE, 0x39 }, { VK_BACK, 0x0E },
        { VK_LEFT, 0x14B }, { VK_RIGHT, 0x14D }, { VK_UP, 0x148 }, { VK_DOWN, 0x150 },
        { VK_HOME, 0x147 }, { VK_END, 0x14F }, { VK_DELETE, 0x153 }, { VK_PRIOR, 0x149 },
        { VK_NEXT, 0x151 },
    };
    int sc = 0;
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (map[i].vk == vk)
            sc = map[i].sc;
    host_push_key(vk, sc, down);
}

static void type_char(char c)
{
    int shift = 0, vk;
    if (c >= 'a' && c <= 'z') vk = c - 32;
    else if (c >= 'A' && c <= 'Z') { vk = c; shift = 1; }
    else if (c >= '0' && c <= '9') vk = c;
    else if (c == ' ') vk = VK_SPACE;
    else if (c == '.') vk = 0xBE;
    else if (c == ',') vk = 0xBC;
    else if (c == '!') { vk = '1'; shift = 1; }
    else if (c == '-') vk = 0xBD;
    else return;
    if (shift) key(VK_SHIFT, 1);
    key(vk, 1);
    key(vk, 0);
    if (shift) key(VK_SHIFT, 0);
}

/* called by the host input driver when the system is idle */
void host_script_idle(uint32_t now)
{
    if (now < wait_until || host_pending())
        return;
    while (cur < nlines) {
        char *l = lines[cur++];
        char cmd[16], a[256] = "";
        int x = 0, y = 0;
        if (sscanf(l, "%15s", cmd) != 1 || cmd[0] == '#')
            continue;
        const char *rest = l + strlen(cmd);
        while (*rest == ' ')
            rest++;
        sscanf(rest, "%d %d", &x, &y);
        if (!strcmp(cmd, "wait")) {
            wait_until = now + (uint32_t)x;
            return;
        } else if (!strcmp(cmd, "move")) {
            mx = x; my = y;
            host_push_mouse(mx, my, btn);
        } else if (!strcmp(cmd, "down") || !strcmp(cmd, "up") ||
                   !strcmp(cmd, "rdown") || !strcmp(cmd, "rup")) {
            unsigned b = cmd[0] == 'r' ? 2 : 1;
            mx = x; my = y;
            host_push_mouse(mx, my, btn);
            btn = (cmd[0] == 'd' || cmd[1] == 'd') ? (btn | b) : (btn & ~b);
            host_push_mouse(mx, my, btn);
        } else if (!strcmp(cmd, "click") || !strcmp(cmd, "dblclick")) {
            mx = x; my = y;
            host_push_mouse(mx, my, btn);
            for (int k = 0; k < (cmd[0] == 'd' ? 2 : 1); k++) {
                host_push_mouse(mx, my, btn | 1);
                host_push_mouse(mx, my, btn);
            }
        } else if (!strcmp(cmd, "key")) {
            sscanf(rest, "%255s", a);
            key(parse_vk(a), 1);
            key(parse_vk(a), 0);
        } else if (!strcmp(cmd, "keydown") || !strcmp(cmd, "keyup")) {
            sscanf(rest, "%255s", a);
            key(parse_vk(a), cmd[3] == 'd');
        } else if (!strcmp(cmd, "type")) {
            for (const char *p = rest; *p && *p != '\n'; p++)
                type_char(*p);
        } else if (!strcmp(cmd, "snap")) {
            sscanf(rest, "%255s", a);
            SNAPSHOT(a);
            printf("snap %s\n", a);
        } else if (!strcmp(cmd, "exit")) {
            printf("script done\n");
            exit(0);
        }
        return;    /* one input step per idle */
    }
    printf("script ended without exit\n");
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s script [program]\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\r\n")] = 0;
        lines = realloc(lines, sizeof(char *) * (size_t)(nlines + 1));
        lines[nlines++] = strdup(buf);
    }
    fclose(f);
    task_init();
#ifdef HOST_VGA800
    if (win_boot("HOSTWIN/WINN.EXE", &win_disp_vga800, &host_script_input))
        return 1;
#else
    if (win_boot("HOSTWIN/WINN.EXE", &host_snap_display, &host_script_input))
        return 1;
#endif
    UINT r = WinExec(argc > 2 ? argv[2] : "DEMO.EXE", SW_SHOWNORMAL);
    if (r < 32) {
        printf("WinExec failed: %u\n", r);
        return 1;
    }
    task_kernel_loop();
    printf("Windows ended\n");
    win_shutdown();
    return 0;
}
