#include "windows.h"

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    MessageBox(0, "Built by native TinyCC", "WINN C", MB_OK);
    return 0;
}
