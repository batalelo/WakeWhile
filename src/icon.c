#include "icon.h"

/* An eye.

   The outline is a lens: the overlap of two equal circles, one centred above
   the middle and one below. That is what gives the almond its two sharp
   corners for free, and inseting both circles by the stroke width hollows it
   out with an even line all the way round.

   For a lens A wide and B tall (half-measures), the circles need radius
   (A*A + B*B) / (2*B), centred that radius minus B either side of the middle.
   With A = 40 and B = 24 that comes to 45, centres at 50 +/- 21.

   The closed eye is the upper arc of the same construction, dropped so it
   sits on the middle line. It has to bulge upward: a lash line follows the
   curve of the eyeball and so falls away at the outer corners. Curved the
   other way it reads as a bowl.

   Geometry is in percent of the icon box, so one set of numbers serves every
   size Windows asks for. */

#define LENS_A      40   /* half width  */
#define LENS_R      45
#define LENS_OFF    21   /* circle centres, either side of the middle */
#define STROKE       8

#define PUPIL_R     12

/* How deep the closed lid dips. It has to be much shallower than the open
   eye: reusing that radius gives a dome that reads as an arch, not a lid.
   From the half width and this sagitta the arc radius follows -- for a chord
   2A wide and S deep, R = (A*A + S*S) / (2*S). */
#define CLOSED_SAG   15
#define CLOSED_EXTRA  2  /* a closed eye is one line, so give it more weight */

typedef long long i64;

static i64 dist2(i64 x, i64 y, i64 cx, i64 cy)
{
    i64 dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy;
}

void icon_render(unsigned char *px, int size, COLORREF colour, int open)
{
    int x, y;
    int r = GetRValue(colour), g = GetGValue(colour), b = GetBValue(colour);

    /* Eighths of a pixel: the resolution the 4x4 sampling grid needs, and
       small enough that every product stays well inside 64 bits. */
    i64 s8 = (i64)size * 8;

    i64 mid  = s8 / 2;
    i64 off  = s8 * LENS_OFF / 100;
    i64 rad  = s8 * LENS_R / 100;
    i64 half = s8 * LENS_A / 100;

    /* The stroke has to stay at least one whole pixel or the outline breaks
       up into dashes at 16 px, which is where it matters most. */
    i64 t = s8 * STROKE / 100;
    if (t < 8) t = 8;

    i64 top_cy = mid - off, bot_cy = mid + off;
    i64 out2 = rad * rad;
    i64 in2  = (rad - t) * (rad - t);

    i64 pr2 = (s8 * PUPIL_R / 100) * (s8 * PUPIL_R / 100);

    /* the closed lid: a shallow arc of its own, centred on the middle line */
    i64 sag    = s8 * CLOSED_SAG / 100;
    i64 crad   = (half * half + sag * sag) / (2 * sag);
    i64 lid_cy = (mid - sag / 2) + crad;
    i64 ct     = t + s8 * CLOSED_EXTRA / 100;
    i64 cout2  = crad * crad;
    i64 cin2   = (crad - ct) * (crad - ct);

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            int sx, sy, cov = 0, a;
            unsigned char *p = px + ((size_t)y * size + x) * 4;

            for (sy = 0; sy < 4; sy++) {
                for (sx = 0; sx < 4; sx++) {
                    i64 X = (i64)x * 8 + sx * 2 + 1;
                    i64 Y = (i64)y * 8 + sy * 2 + 1;
                    int lit = 0;

                    if (open) {
                        i64 dt = dist2(X, Y, mid, top_cy);
                        i64 db = dist2(X, Y, mid, bot_cy);
                        int inside = dt <= out2 && db <= out2;
                        int hollow = dt <= in2  && db <= in2;

                        if (inside && !hollow) lit = 1;              /* lid  */
                        if (dist2(X, Y, mid, mid) <= pr2) lit = 1;   /* pupil */
                    } else {
                        i64 d = dist2(X, Y, mid, lid_cy);
                        /* the upper half of the ring only, and no wider than
                           the open eye so the two read as the same object */
                        if (d <= cout2 && d >= cin2 &&
                            Y <= lid_cy && X >= mid - half && X <= mid + half)
                            lit = 1;
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

HICON icon_make(COLORREF colour, int size, int open)
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

    icon_render(px, size, colour, open);

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
