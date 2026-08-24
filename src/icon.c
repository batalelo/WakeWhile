#include "icon.h"

/* A cup on a saucer.

   The first attempt was a crescent moon with a bar through it, which is the
   more literal reading of "no sleep". It fell apart at 16 px: the clear space
   the bar needs cuts a crescent -- thin by definition -- into two floating
   slivers. A cup is solid, has no thin parts, and is what this whole category
   of program has looked like since Caffeine.

   Geometry is in percent of the icon box, so one set of numbers serves every
   size Windows asks for. */

#define CUP_TOP     22
#define CUP_BOTTOM  64
#define CUP_TL      18   /* top edge, left and right */
#define CUP_TR      58
#define CUP_BL      26   /* bottom edge, tapered in  */
#define CUP_BR      50

#define HANDLE_CX   60
#define HANDLE_CY   36
#define HANDLE_OUT  17
#define HANDLE_IN    9

#define SAUCER_TOP     71
#define SAUCER_BOTTOM  81
#define SAUCER_L       10
#define SAUCER_R       72

typedef long long i64;

/* The cup tapers, so its sides are lines rather than constants: this gives
   the left or right edge at a given height. */
static i64 lerp(i64 a, i64 b, i64 num, i64 den)
{
    if (den == 0) return a;
    return a + ((b - a) * num) / den;
}

void icon_render(unsigned char *px, int size, COLORREF colour)
{
    int x, y;
    int r = GetRValue(colour), g = GetGValue(colour), b = GetBValue(colour);

    /* Eighths of a pixel: the resolution the 4x4 sampling grid needs, and
       small enough that every product stays well inside 64 bits. */
    i64 s8 = (i64)size * 8;

    i64 ctop = s8 * CUP_TOP / 100,    cbot = s8 * CUP_BOTTOM / 100;
    i64 ctl  = s8 * CUP_TL / 100,     ctr  = s8 * CUP_TR / 100;
    i64 cbl  = s8 * CUP_BL / 100,     cbr  = s8 * CUP_BR / 100;

    i64 hcx  = s8 * HANDLE_CX / 100,  hcy  = s8 * HANDLE_CY / 100;
    i64 hout = s8 * HANDLE_OUT / 100, hin  = s8 * HANDLE_IN / 100;
    i64 hout2 = hout * hout, hin2 = hin * hin;

    i64 stop = s8 * SAUCER_TOP / 100, sbot = s8 * SAUCER_BOTTOM / 100;
    i64 sl   = s8 * SAUCER_L / 100,   sr   = s8 * SAUCER_R / 100;
    i64 srad = (sbot - stop) / 2;

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            int sx, sy, cov = 0, a;
            unsigned char *p = px + ((size_t)y * size + x) * 4;

            for (sy = 0; sy < 4; sy++) {
                for (sx = 0; sx < 4; sx++) {
                    i64 X = (i64)x * 8 + sx * 2 + 1;
                    i64 Y = (i64)y * 8 + sy * 2 + 1;
                    int lit = 0;

                    /* the cup */
                    if (Y >= ctop && Y <= cbot) {
                        i64 l  = lerp(ctl, cbl, Y - ctop, cbot - ctop);
                        i64 rr = lerp(ctr, cbr, Y - ctop, cbot - ctop);
                        if (X >= l && X <= rr) lit = 1;
                    }

                    /* the handle: a ring, clipped to the side of the cup, so
                       it reads as a loop rather than a blob stuck on */
                    if (!lit && X >= ctr - s8 / 50) {
                        i64 dx = X - hcx, dy = Y - hcy;
                        i64 d2 = dx * dx + dy * dy;
                        if (d2 <= hout2 && d2 >= hin2) lit = 1;
                    }

                    /* the saucer, with rounded ends */
                    if (!lit && Y >= stop && Y <= sbot) {
                        i64 l = sl + srad, rr = sr - srad;
                        if (X >= l && X <= rr) lit = 1;
                        else {
                            i64 cx = (X < l) ? l : rr;
                            i64 cy = (stop + sbot) / 2;
                            i64 dx = X - cx, dy = Y - cy;
                            if (dx * dx + dy * dy <= srad * srad) lit = 1;
                        }
                    }

                    if (lit) cov++;
                }
            }

            a = (cov * 255) / 16;
            /* Premultiplied, which is what the shell expects of a 32-bit
               icon with an alpha channel. */
            p[0] = (unsigned char)((b * a) / 255);
            p[1] = (unsigned char)((g * a) / 255);
            p[2] = (unsigned char)((r * a) / 255);
            p[3] = (unsigned char)a;
        }
    }
}

HICON icon_make(COLORREF colour, int size)
{
    static unsigned char zeros[256 * 256 / 8];
    BITMAPINFO bi;
    ICONINFO ii;
    HDC dc;
    HBITMAP color, mask;
    HICON icon;
    unsigned char *px = 0;

    if (size < 8) size = 8;
    if (size > 256) size = 256;

    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;        /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biSizeImage = 0;
    bi.bmiHeader.biXPelsPerMeter = 0;
    bi.bmiHeader.biYPelsPerMeter = 0;
    bi.bmiHeader.biClrUsed = 0;
    bi.bmiHeader.biClrImportant = 0;

    dc = GetDC(0);
    color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&px, 0, 0);
    ReleaseDC(0, dc);
    if (!color || !px) { if (color) DeleteObject(color); return 0; }

    icon_render(px, size, colour);

    /* An all-zero mask means "take the colour bitmap as it is". The buffer
       must be supplied explicitly; CreateBitmap leaves it undefined. */
    mask = CreateBitmap(size, size, 1, 1, zeros);

    ii.fIcon = TRUE;
    ii.xHotspot = 0;
    ii.yHotspot = 0;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    icon = CreateIconIndirect(&ii);

    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}
