/* Renders the tray-state figure used in the README, at a size worth looking
   at. Writes build/icon_sheet.bmp.

   The icons themselves are built the way the tray builds them and drawn by
   Windows through DrawIconEx, so what appears here is what the shell shows --
   just larger, with each state labelled.

   Not part of the shipped executable. */

#include <stdio.h>
#include "../src/icon.c"

#define W 900
#define H 300

static unsigned char sheet[H][W][3];

int main(void)
{
    HDC screen, mem;
    HBITMAP bmp, old;
    BITMAPINFO bi;
    unsigned char *px = 0;
    HFONT font, small;
    HICON icons[3];
    const COLORREF colours[3] = {
        RGB(0x2E, 0xA0, 0x43), RGB(0xE0, 0x9B, 0x00), RGB(0x8A, 0x8A, 0x8A)
    };
    const int open[3] = { 1, 1, 0 };
    const WCHAR *title[3] = { L"Working", L"Quiet, still holding", L"Released" };
    const WCHAR *note[3] = {
        L"over a limit right now",
        L"under every limit, waiting out the clock",
        L"the machine is free to sleep"
    };
    int i, x, y;
    FILE *f;
    int stride = (W * 3 + 3) & ~3;
    unsigned int v;
    unsigned char pad[3] = {0, 0, 0};

    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biSizeImage = 0;
    bi.bmiHeader.biXPelsPerMeter = 0;
    bi.bmiHeader.biYPelsPerMeter = 0;
    bi.bmiHeader.biClrUsed = 0;
    bi.bmiHeader.biClrImportant = 0;

    screen = GetDC(0);
    mem = CreateCompatibleDC(screen);
    bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, (void **)&px, 0, 0);
    ReleaseDC(0, screen);
    old = (HBITMAP)SelectObject(mem, bmp);

    /* The dark of a Windows 11 taskbar, so it reads in context. */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned char *p = px + ((size_t)y * W + x) * 4;
            p[0] = 0x1F; p[1] = 0x1F; p[2] = 0x20; p[3] = 255;
        }

    font  = CreateFontW(30, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        VARIABLE_PITCH, L"Segoe UI");
    small = CreateFontW(22, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        VARIABLE_PITCH, L"Segoe UI");
    SetBkMode(mem, TRANSPARENT);

    for (i = 0; i < 3; i++) {
        RECT r;
        int cx = 40 + i * 290;

        /* Built at 128 and drawn at 96: the rasteriser works at any size, so
           this is real detail rather than an upscale of the 16 px version. */
        icons[i] = icon_make(colours[i], 128, open[i]);
        if (!icons[i]) { printf("icon_make failed\n"); return 1; }
        DrawIconEx(mem, cx, 46, icons[i], 96, 96, 0, 0, DI_NORMAL);

        r.left = cx - 20; r.right = cx + 250; r.top = 170; r.bottom = 205;
        SelectObject(mem, font);
        SetTextColor(mem, RGB(0xF0, 0xF0, 0xF0));
        DrawTextW(mem, title[i], -1, &r, DT_CENTER | DT_SINGLELINE);

        r.top = 208; r.bottom = 280;
        SelectObject(mem, small);
        SetTextColor(mem, RGB(0x9B, 0x9B, 0x9B));
        DrawTextW(mem, note[i], -1, &r, DT_CENTER | DT_WORDBREAK);
    }

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned char *p = px + ((size_t)y * W + x) * 4;
            sheet[y][x][0] = p[0];
            sheet[y][x][1] = p[1];
            sheet[y][x][2] = p[2];
        }

    for (i = 0; i < 3; i++) DestroyIcon(icons[i]);
    DeleteObject(font);
    DeleteObject(small);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);

    f = fopen("build/icon_sheet.bmp", "wb");
    if (!f) { printf("cannot write\n"); return 1; }
    fwrite("BM", 1, 2, f);
    v = 14 + 40 + stride * H;    fwrite(&v, 4, 1, f);
    v = 0;                       fwrite(&v, 4, 1, f);
    v = 14 + 40;                 fwrite(&v, 4, 1, f);
    v = 40;                      fwrite(&v, 4, 1, f);
    v = W;                       fwrite(&v, 4, 1, f);
    v = H;                       fwrite(&v, 4, 1, f);
    v = 1 | (24 << 16);          fwrite(&v, 4, 1, f);
    v = 0;                       fwrite(&v, 4, 1, f);
    v = stride * H;              fwrite(&v, 4, 1, f);
    v = 2835; fwrite(&v, 4, 1, f); fwrite(&v, 4, 1, f);
    v = 0; fwrite(&v, 4, 1, f);  fwrite(&v, 4, 1, f);
    for (y = H - 1; y >= 0; y--) {
        for (x = 0; x < W; x++) fwrite(sheet[y][x], 1, 3, f);
        fwrite(pad, 1, stride - W * 3, f);
    }
    fclose(f);

    printf("wrote build/icon_sheet.bmp (%d x %d)\n", W, H);
    return 0;
}
