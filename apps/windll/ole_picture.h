/* Copyright (c) 2026 murm386 contributors. MIT License. */
#ifndef WINN_OLE_PICTURE_H
#define WINN_OLE_PICTURE_H

#include "windows.h"

static HGLOBAL bitmap_to_dib(HBITMAP bitmap, BOOL true_color)
{
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    if (!GetDIBits(0, bitmap, 0, 0, NULL, &bi, DIB_RGB_COLORS))
        return 0;
    LONG w = bi.bmiHeader.biWidth, h = bi.bmiHeader.biHeight;
    if (w < 1 || w > 4096 || h < 1 || h > 4096)
        return 0;
    WORD depth = true_color ? 24 : bi.bmiHeader.biBitCount;
    if (depth != 1 && depth != 4 && depth != 8 && depth != 24)
        return 0;
    DWORD header = sizeof(BITMAPINFOHEADER) + (depth == 24 ? 0 : (1u << depth) * sizeof(RGBQUAD));
    DWORD bytes = header + (((DWORD)w * depth + 31) / 32 * 4) * (DWORD)h;
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes);
    BITMAPINFO *packed = dib ? GlobalLock(dib) : NULL;
    if (packed) {
        packed->bmiHeader = bi.bmiHeader;
        packed->bmiHeader.biBitCount = depth;
        int n = GetDIBits(0, bitmap, 0, (UINT)h, (uint8_t *)packed + header,
                          packed, DIB_RGB_COLORS);
        GlobalUnlock(dib);
        if (n == h)
            return dib;
    }
    if (dib)
        GlobalFree(dib);
    return 0;
}

#endif
