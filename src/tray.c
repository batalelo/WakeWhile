#include "tray.h"

#define TRAY_ID 1

static HWND  g_owner;
static UINT  g_msg;
static HICON g_icon;
static int   g_added;
static TrayState g_state = TRAY_OFF;

static void wcopy(WCHAR *dst, const WCHAR *src, int cap)
{
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* A filled circle with 4x4 coverage sampling, written straight into a
   premultiplied BGRA DIB. All integer: no CRT, no float helpers. */
static HICON make_dot(COLORREF c)
{
    static unsigned char zeros[128 * 128 / 8];
    BITMAPINFO bi;
    ICONINFO ii;
    HDC dc;
    HBITMAP color, mask;
    HICON icon;
    unsigned char *px = 0;
    int sz, x, y, cx8, r8, rr;
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);

    sz = GetSystemMetrics(SM_CXSMICON);
    if (sz < 16) sz = 16;
    if (sz > 128) sz = 128;

    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;          /* top-down */
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

    cx8 = sz * 4;                 /* centre, in eighths of a pixel */
    r8  = (sz * 8 * 42) / 100;    /* radius: 42% of the icon box   */
    rr  = r8 * r8;

    for (y = 0; y < sz; y++) {
        for (x = 0; x < sz; x++) {
            int sx, sy, cov = 0, a;
            unsigned char *p = px + (y * sz + x) * 4;
            for (sy = 0; sy < 4; sy++) {
                for (sx = 0; sx < 4; sx++) {
                    int dx = (x * 8 + sx * 2 + 1) - cx8;
                    int dy = (y * 8 + sy * 2 + 1) - cx8;
                    if (dx * dx + dy * dy <= rr) cov++;
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

    /* An all-zero mask means "take the colour bitmap as it is". The buffer
       must be supplied explicitly; CreateBitmap leaves it undefined. */
    mask = CreateBitmap(sz, sz, 1, 1, zeros);

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

static COLORREF state_colour(TrayState s)
{
    if (s == TRAY_HOLDING) return RGB(0x2E, 0xA0, 0x43);
    if (s == TRAY_GRACE)   return RGB(0xE0, 0x9B, 0x00);
    return RGB(0x8A, 0x8A, 0x8A);
}

static void fill_nid(NOTIFYICONDATAW *nid, const WCHAR *tip)
{
    int i;
    unsigned char *p = (unsigned char *)nid;
    for (i = 0; i < (int)sizeof *nid; i++) p[i] = 0;

    nid->cbSize = sizeof *nid;
    nid->hWnd = g_owner;
    nid->uID = TRAY_ID;
    nid->uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid->uCallbackMessage = g_msg;
    nid->hIcon = g_icon;
    if (tip) wcopy(nid->szTip, tip, 128);
}

void tray_init(HWND owner, UINT callback_msg)
{
    g_owner = owner;
    g_msg = callback_msg;
    g_icon = make_dot(state_colour(TRAY_OFF));
    g_added = 0;
}

void tray_add(void)
{
    NOTIFYICONDATAW nid;
    if (g_added) return;
    fill_nid(&nid, CFG_APP_NAME);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) g_added = 1;
}

void tray_remove(void)
{
    NOTIFYICONDATAW nid;
    if (!g_added) return;
    fill_nid(&nid, 0);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_added = 0;
}

void tray_set(TrayState state, const WCHAR *tip)
{
    NOTIFYICONDATAW nid;

    if (state != g_state || !g_icon) {
        HICON fresh = make_dot(state_colour(state));
        if (fresh) {
            HICON old = g_icon;
            g_icon = fresh;
            g_state = state;
            if (old) DestroyIcon(old);
        }
    }

    if (!g_added) {
        tray_add();
        return;
    }

    fill_nid(&nid, tip);
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        /* The shell dropped us, most likely because Explorer restarted
           between the notification and now. Re-add on the next attempt. */
        g_added = 0;
    }
}

void tray_shutdown(void)
{
    tray_remove();
    if (g_icon) { DestroyIcon(g_icon); g_icon = 0; }
}
