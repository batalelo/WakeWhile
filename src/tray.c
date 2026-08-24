#include "tray.h"
#include "icon.h"

#define TRAY_ID 1

static HWND  g_owner;
static UINT  g_msg;
static HICON g_icon;
static int   g_added;
static TrayState g_state = TRAY_OFF;
static DWORD g_last_error;

static void wcopy(WCHAR *dst, const WCHAR *src, int cap)
{
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* The tray shows the same cup as the taskbar, tinted by what the rule
   currently thinks: green holding, amber in the grace window, grey released.
   Shape for recognition, colour for status. */
static HICON icon_make_tray(COLORREF c)
{
    int sz = GetSystemMetrics(SM_CXSMICON);
    if (sz < 16) sz = 16;
    return icon_make(c, sz);
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
    g_icon = icon_make_tray(state_colour(TRAY_OFF));
    g_added = 0;
}

int tray_add(void)
{
    NOTIFYICONDATAW nid;
    if (g_added) return 1;
    fill_nid(&nid, CFG_APP_NAME);
    SetLastError(0);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        g_added = 1;
        g_last_error = 0;
    } else {
        g_last_error = GetLastError();
    }
    return g_added;
}

unsigned int tray_last_error(void) { return (unsigned int)g_last_error; }
unsigned int tray_struct_size(void) { return (unsigned int)sizeof(NOTIFYICONDATAW); }

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
        HICON fresh = icon_make_tray(state_colour(state));
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
