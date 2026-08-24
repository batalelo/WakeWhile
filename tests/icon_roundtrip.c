/* Checks the real HICON path, not just the rasteriser: builds the three
   icons the way the tray does -- open and green, open and amber, closed and
   grey -- then asks Windows to draw them back into a bitmap. A wrong mask or
   unpremultiplied alpha shows up here as a black square.

   Writes build/icon_roundtrip.bmp. Not part of the shipped executable. */

#include <stdio.h>
#include "../src/icon.c"

#define W 260
#define H 120

static unsigned char sheet[H][W][3];

int main(void)
{
    HDC screen, mem;
    HBITMAP bmp, old;
    BITMAPINFO bi;
    unsigned char *px = 0;
    HICON icons[3];
    COLORREF colours[3] = {
        RGB(0x2E, 0xA0, 0x43), RGB(0xE0, 0x9B, 0x00), RGB(0x8A, 0x8A, 0x8A)
    };
    int sz = GetSystemMetrics(SM_CXSMICON);
    int i, x, y;
    FILE *f;
    int stride = (W * 3 + 3) & ~3;
    unsigned int v;
    unsigned char pad[3] = {0, 0, 0};

    if (sz < 16) sz = 16;
    printf("SM_CXSMICON = %d\n", sz);

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

    /* Light on the top half, dark on the bottom, same as the preview. */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned char c = (y < H / 2) ? 0xF3 : 0x20;
            unsigned char *p = px + ((size_t)y * W + x) * 4;
            p[0] = p[1] = p[2] = c;
            p[3] = 255;
        }

    for (i = 0; i < 3; i++) {
        icons[i] = icon_make(colours[i], sz, i < 2);
        if (!icons[i]) { printf("icon_make returned null\n"); return 1; }
        /* Once at native size, once blown up, so both the shape and the
           pixel-level alpha are visible. */
        DrawIconEx(mem, 16 + i * 80, 20, icons[i], sz, sz, 0, 0, DI_NORMAL);
        DrawIconEx(mem, 16 + i * 80, 74, icons[i], 40, 40, 0, 0, DI_NORMAL);
    }

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            unsigned char *p = px + ((size_t)y * W + x) * 4;
            sheet[y][x][0] = p[0];
            sheet[y][x][1] = p[1];
            sheet[y][x][2] = p[2];
        }

    for (i = 0; i < 3; i++) DestroyIcon(icons[i]);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);

    f = fopen("build/icon_roundtrip.bmp", "wb");
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

    printf("wrote build/icon_roundtrip.bmp\n");
    return 0;
}
