/* Renders the application mark at the sizes Windows actually asks for, on
   both a light and a dark background, so the shape can be judged by eye
   before it ships. Writes build/icon_preview.bmp.

   Not part of the shipped executable. */

#include <stdio.h>
#include "../src/icon.c"

#define SHEET_W 460
#define SHEET_H 200

static unsigned char sheet[SHEET_H][SHEET_W][3];

static void fill(int x0, int y0, int w, int h, int r, int g, int b)
{
    int x, y;
    for (y = y0; y < y0 + h && y < SHEET_H; y++)
        for (x = x0; x < x0 + w && x < SHEET_W; x++) {
            sheet[y][x][0] = (unsigned char)b;
            sheet[y][x][1] = (unsigned char)g;
            sheet[y][x][2] = (unsigned char)r;
        }
}

/* Composites the premultiplied mark over whatever is already there. */
static void blit(int x0, int y0, int size, COLORREF colour)
{
    static unsigned char px[256 * 256 * 4];
    int x, y;

    icon_render(px, size, colour);

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            unsigned char *p = px + ((size_t)y * size + x) * 4;
            int a = p[3];
            int dx = x0 + x, dy = y0 + y;
            int c;
            if (dx < 0 || dx >= SHEET_W || dy < 0 || dy >= SHEET_H) continue;
            for (c = 0; c < 3; c++)
                sheet[dy][dx][c] =
                    (unsigned char)(p[c] + (sheet[dy][dx][c] * (255 - a)) / 255);
        }
    }
}

/* A plain 24-bit BMP: bottom-up rows, each padded to four bytes. */
static int write_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    int stride = (SHEET_W * 3 + 3) & ~3;
    int data = stride * SHEET_H;
    int y, x;
    unsigned char pad[3] = {0, 0, 0};
    unsigned int v;

    if (!f) return 0;

    fwrite("BM", 1, 2, f);
    v = 14 + 40 + data;          fwrite(&v, 4, 1, f);
    v = 0;                       fwrite(&v, 4, 1, f);
    v = 14 + 40;                 fwrite(&v, 4, 1, f);
    v = 40;                      fwrite(&v, 4, 1, f);
    v = SHEET_W;                 fwrite(&v, 4, 1, f);
    v = SHEET_H;                 fwrite(&v, 4, 1, f);
    v = 1 | (24 << 16);          fwrite(&v, 4, 1, f);
    v = 0;                       fwrite(&v, 4, 1, f);
    v = data;                    fwrite(&v, 4, 1, f);
    v = 2835; fwrite(&v, 4, 1, f); fwrite(&v, 4, 1, f);
    v = 0; fwrite(&v, 4, 1, f);  fwrite(&v, 4, 1, f);

    for (y = SHEET_H - 1; y >= 0; y--) {
        for (x = 0; x < SHEET_W; x++) fwrite(sheet[y][x], 1, 3, f);
        fwrite(pad, 1, stride - SHEET_W * 3, f);
    }

    fclose(f);
    return 1;
}

int main(void)
{
    int sizes[5] = {16, 20, 24, 32, 48};
    int i, x;

    /* Top half light, bottom half dark: the two taskbars it has to survive. */
    fill(0, 0, SHEET_W, SHEET_H / 2, 0xF3, 0xF3, 0xF3);
    fill(0, SHEET_H / 2, SHEET_W, SHEET_H / 2, 0x20, 0x20, 0x20);

    x = 16;
    for (i = 0; i < 5; i++) {
        blit(x, 30 - sizes[i] / 2, sizes[i], ICON_BRAND);
        blit(x, 130 - sizes[i] / 2, sizes[i], ICON_BRAND);
        x += sizes[i] + 18;
    }

    /* And the three tray states, at the size the tray actually uses. */
    x += 20;
    blit(x, 22, 16, RGB(0x2E, 0xA0, 0x43));  blit(x, 122, 16, RGB(0x2E, 0xA0, 0x43));
    x += 34;
    blit(x, 22, 16, RGB(0xE0, 0x9B, 0x00));  blit(x, 122, 16, RGB(0xE0, 0x9B, 0x00));
    x += 34;
    blit(x, 22, 16, RGB(0x8A, 0x8A, 0x8A));  blit(x, 122, 16, RGB(0x8A, 0x8A, 0x8A));

    /* One big one, to check the shape itself rather than its legibility. */
    blit(SHEET_W - 100, 26, 80, ICON_BRAND);
    blit(SHEET_W - 100, 116, 80, ICON_BRAND);

    if (!write_bmp("build/icon_preview.bmp")) {
        printf("could not write build/icon_preview.bmp\n");
        return 1;
    }
    printf("wrote build/icon_preview.bmp\n");
    return 0;
}
