/* GDI drawing test: renders a sample sheet and saves gdi.ppm. */
#include <stdio.h>
#include "wincore.h"
#include "host_drivers.h"

void win_ddi_key_event(int vk, int sc, int down) { (void)vk; (void)sc; (void)down; }
void win_ddi_mouse_event(int x, int y, unsigned b) { cursor_move(x, y); (void)b; }
int user_init(void) { return 0; }   /* GDI only: no USER here */
int win_file_attr(LPCSTR path, int set, UINT attr) { return -1; }   /* no files: no SYSTEM */
int win_dll_find(const char *name) { return -1; }                    /* no DLLs */
HANDLE win_dll_handle(int slot) { return 0; }
wtask *task_current(void) { static wtask t; return &t; }   /* no tasks either */

int main(void)
{
    win_register_display_driver(&host_snap_display);
    win_register_input_driver(&host_script_input);
    if (win_init(NULL, NULL, 640, 400, 8)) {
        printf("init failed\n");
        return 1;
    }
    HDC dc = CreateDC("DISPLAY", NULL, NULL, NULL);
    RECT all = { 0, 0, 640, 400 };
    HBRUSH desk = CreateSolidBrush(RGB(0, 128, 128));
    FillRect(dc, &all, desk);

    /* static colours */
    for (int i = 0; i < 20; i++) {
        RECT r = { 4 + i * 20, 4, 4 + i * 20 + 18, 22 };
        HBRUSH b = CreateSolidBrush(PALETTEINDEX(i));
        FillRect(dc, &r, b);
        DeleteObject(b);
    }
    /* rectangles, pens */
    SelectObject(dc, GetStockObject(LTGRAY_BRUSH));
    Rectangle(dc, 10, 30, 110, 90);
    HPEN red3 = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
    SelectObject(dc, red3);
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    Rectangle(dc, 120, 30, 220, 90);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    RoundRect(dc, 230, 30, 330, 90, 20, 20);
    HBRUSH yel = CreateSolidBrush(RGB(255, 255, 0));
    SelectObject(dc, yel);
    Ellipse(dc, 340, 30, 440, 90);
    Ellipse(dc, 450, 30, 470, 50);
    Ellipse(dc, 480, 30, 483, 33);
    HBRUSH hat = CreateHatchBrush(HS_DIAGCROSS, RGB(0, 0, 255));
    SelectObject(dc, hat);
    Ellipse(dc, 500, 30, 630, 90);

    /* lines in all styles */
    for (int s = 0; s < 5; s++) {
        HPEN p = CreatePen(s, 1, RGB(0, 0, 0));
        HGDIOBJ o = SelectObject(dc, p);
        MoveTo(dc, 10, 100 + s * 6);
        LineTo(dc, 300, 100 + s * 6);
        SelectObject(dc, o);
        DeleteObject(p);
    }
    SelectObject(dc, red3);
    MoveTo(dc, 320, 100); LineTo(dc, 420, 130);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    for (int a = 0; a < 16; a++) {
        MoveTo(dc, 520, 130);
        static const int dx[16] = { 50, 46, 35, 19, 0, -19, -35, -46, -50, -46, -35, -19, 0, 19, 35, 46 };
        static const int dy[16] = { 0, 19, 35, 46, 50, 46, 35, 19, 0, -19, -35, -46, -50, -46, -35, -19 };
        LineTo(dc, 520 + dx[a] * 2 / 3, 130 + dy[a] * 2 / 3);
    }

    /* polygon: star, winding vs alternate */
    POINT star[5] = { { 60, 140 }, { 90, 230 }, { 15, 175 }, { 105, 175 }, { 30, 230 } };
    SelectObject(dc, GetStockObject(GRAY_BRUSH));
    Polygon(dc, star, 5);
    for (int i = 0; i < 5; i++) star[i].x += 110;
    SetPolyFillMode(dc, WINDING);
    Polygon(dc, star, 5);

    /* text */
    SetBkMode(dc, TRANSPARENT);
    TextOut(dc, 240, 150, "Hello, Windows! System font.", -1);
    SelectObject(dc, GetStockObject(SYSTEM_FIXED_FONT));
    SetTextColor(dc, RGB(128, 0, 0));
    TextOut(dc, 240, 168, "Fixedsys: 0123456789 iiiWWW", -1);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    SetTextColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(255, 255, 0));
    TextOut(dc, 240, 186, "\xcf\xf0\xe8\xe2\xe5\xf2, \xec\xe8\xf0! (cp1251)", -1);
    HFONT bold = CreateFont(16, 0, 0, 0, FW_BOLD, 0, 1, 0, 0, 0, 0, 0, 0, "System");
    SelectObject(dc, bold);
    SetBkMode(dc, TRANSPARENT);
    TextOut(dc, 240, 204, "Bold underlined text", -1);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    SetTextAlign(dc, TA_RIGHT);
    TextOut(dc, 630, 222, "right aligned", -1);
    SetTextAlign(dc, TA_LEFT);
    RECT clip = { 240, 240, 330, 256 };
    SetBkColor(dc, RGB(255, 255, 255));
    ExtTextOut(dc, 240, 240, ETO_OPAQUE | ETO_CLIPPED, &clip, "Clipped long text here", 22, NULL);

    /* memory DC + BitBlt + mono */
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, 64, 64);
    SelectObject(mem, bm);
    RECT mr = { 0, 0, 64, 64 };
    FillRect(mem, &mr, GetStockObject(WHITE_BRUSH));
    SelectObject(mem, yel);
    Ellipse(mem, 4, 4, 60, 60);
    BitBlt(dc, 10, 250, 64, 64, mem, 0, 0, SRCCOPY);
    StretchBlt(dc, 80, 250, 128, 64, mem, 0, 0, 64, 64, SRCCOPY);
    static const uint8_t check[] = { 0xAA, 0, 0x55, 0, 0xAA, 0, 0x55, 0, 0xAA, 0, 0x55, 0, 0xAA, 0, 0x55, 0 };
    HBITMAP mono = CreateBitmap(8, 8, 1, 1, check);
    HBRUSH pb = CreatePatternBrush(mono);
    SelectObject(dc, pb);
    SetTextColor(dc, RGB(0, 0, 128));
    SetBkColor(dc, RGB(255, 255, 255));
    PatBlt(dc, 220, 260, 60, 50, PATCOPY);
    SelectObject(dc, GetStockObject(BLACK_BRUSH));
    PatBlt(dc, 300, 260, 60, 50, DSTINVERT);

    /* regions + clipping */
    HRGN r1 = CreateRectRgn(400, 260, 500, 340), r2 = CreateRectRgn(450, 290, 550, 380);
    HRGN rx = CreateRectRgn(0, 0, 0, 0);
    CombineRgn(rx, r1, r2, RGN_XOR);
    HBRUSH grn = CreateSolidBrush(RGB(0, 255, 0));
    FillRgn(dc, rx, grn);
    SelectClipRgn(dc, r2);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    for (int i = 0; i < 200; i += 8) { MoveTo(dc, 380 + i, 250); LineTo(dc, 580 + i - 200, 400); }
    SelectClipRgn(dc, 0);

    /* self-overlapping blit (scroll) */
    BitBlt(dc, 570, 250, 60, 60, dc, 560, 240, SRCCOPY);
    RECT fr = { 10, 330, 200, 390 };
    DrawFocusRect(dc, &fr);
    FrameRect(dc, &(RECT){ 20, 340, 120, 380 }, GetStockObject(BLACK_BRUSH));
    InvertRect(dc, &(RECT){ 130, 340, 190, 380 });

    cursor_move(320, 200);
    host_snapshot("gdi.ppm");
    win_shutdown();
    printf("ok\n");
    return 0;
}
