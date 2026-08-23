#include "theme.h"

typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

static int system_prefers_dark(void)
{
    HKEY key;
    DWORD value = 1, size = sizeof value, type = 0;
    int dark = 0;

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;

    if (RegQueryValueExW(key, L"AppsUseLightTheme", 0, &type,
                         (LPBYTE)&value, &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        dark = (value == 0);

    RegCloseKey(key);
    return dark;
}

void theme_load(Theme *t)
{
    t->dark = system_prefers_dark();

    /* Status colours read the same either way. */
    t->ok   = RGB(0x2E, 0xA0, 0x43);
    t->warn = RGB(0xE0, 0x9B, 0x00);
    t->off  = RGB(0x8A, 0x8A, 0x8A);

    if (t->dark) {
        t->bg          = RGB(0x20, 0x20, 0x20);
        t->panel       = RGB(0x2B, 0x2B, 0x2B);
        t->row_hot     = RGB(0x35, 0x35, 0x35);
        t->row_sel     = RGB(0x2F, 0x44, 0x5C);
        t->text        = RGB(0xF0, 0xF0, 0xF0);
        t->text_dim    = RGB(0x9B, 0x9B, 0x9B);
        t->text_sel    = RGB(0xFF, 0xFF, 0xFF);
        t->border      = RGB(0x3A, 0x3A, 0x3A);
        t->accent      = RGB(0x4C, 0xC2, 0xFF);
        t->accent_hot  = RGB(0x66, 0xCD, 0xFF);
        t->accent_down = RGB(0x3D, 0xA6, 0xDE);
        t->accent_text = RGB(0x00, 0x1B, 0x2C);
        t->muted       = RGB(0x33, 0x33, 0x33);
        t->muted_text  = RGB(0x77, 0x77, 0x77);
        t->scroll      = RGB(0x5A, 0x5A, 0x5A);
    } else {
        t->bg          = RGB(0xF3, 0xF3, 0xF3);
        t->panel       = RGB(0xFF, 0xFF, 0xFF);
        t->row_hot     = RGB(0xF0, 0xF0, 0xF0);
        t->row_sel     = RGB(0xDF, 0xEB, 0xFA);
        t->text        = RGB(0x1B, 0x1B, 0x1B);
        t->text_dim    = RGB(0x6A, 0x6A, 0x6A);
        t->text_sel    = RGB(0x10, 0x2A, 0x43);
        t->border      = RGB(0xDC, 0xDC, 0xDC);
        t->accent      = RGB(0x00, 0x67, 0xC0);
        t->accent_hot  = RGB(0x1A, 0x7C, 0xD4);
        t->accent_down = RGB(0x00, 0x52, 0x9B);
        t->accent_text = RGB(0xFF, 0xFF, 0xFF);
        t->muted       = RGB(0xE2, 0xE2, 0xE2);
        t->muted_text  = RGB(0x90, 0x90, 0x90);
        t->scroll      = RGB(0xBB, 0xBB, 0xBB);
    }
}

void theme_apply_titlebar(HWND hwnd, int dark)
{
    HMODULE dwm;
    PFN_DwmSetWindowAttribute set;
    BOOL on = dark ? TRUE : FALSE;

    /* Resolved at run time: on Windows before 1809 the attribute does not
       exist and the title bar simply stays light. */
    dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;

    set = (PFN_DwmSetWindowAttribute)
        GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set)
        set(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof on);

    FreeLibrary(dwm);
}
