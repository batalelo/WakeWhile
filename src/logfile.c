#include "logfile.h"
#include "wincompat.h"
#include "config.h"

static HANDLE g_file = INVALID_HANDLE_VALUE;
static WCHAR  g_path[MAX_PATH];
static int    g_failed;

static int alen(const char *s)
{
    int i = 0;
    while (s[i]) i++;
    return i;
}

static void raw(const char *s, int n)
{
    DWORD written;
    if (g_file == INVALID_HANDLE_VALUE) return;
    WriteFile(g_file, s, (DWORD)n, &written, 0);
}

/* Beside the executable, which is where the user will look for it. */
static void build_path(void)
{
    int i, cut = -1;

    g_path[0] = 0;
    if (!GetModuleFileNameW(0, g_path, MAX_PATH)) return;

    for (i = 0; g_path[i]; i++)
        if (g_path[i] == '\\' || g_path[i] == '/') cut = i;
    if (cut < 0) { g_path[0] = 0; return; }

    g_path[cut + 1] = 0;
    {
        const WCHAR *name = CFG_LOG_NAME;
        int j = cut + 1;
        while (*name && j < MAX_PATH - 1) g_path[j++] = *name++;
        g_path[j] = 0;
    }
}

static void stamp(void)
{
    SYSTEMTIME t;
    char buf[32];
    GetLocalTime(&t);
    wsprintfA(buf, "%04u-%02u-%02u %02u:%02u:%02u  ",
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    raw(buf, alen(buf));
}

void log_open(void)
{
    LARGE_INTEGER end;

    if (g_file != INVALID_HANDLE_VALUE || g_failed) return;

    build_path();
    if (!g_path[0]) { g_failed = 1; return; }

    g_file = CreateFileW(g_path, FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (g_file == INVALID_HANDLE_VALUE) { g_failed = 1; return; }

    /* One line a second adds up. Past the cap, start the file again rather
       than let it grow without limit. */
    end.QuadPart = 0;
    if (SetFilePointerEx(g_file, end, &end, FILE_END) &&
        end.QuadPart > CFG_LOG_MAX_BYTES) {
        CloseHandle(g_file);
        g_file = CreateFileW(g_path, GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        if (g_file == INVALID_HANDLE_VALUE) { g_failed = 1; return; }
        end.QuadPart = 0;
        raw("(log restarted: the previous one reached the size limit)\r\n", 57);
    }

    /* App titles go in here, and they are not all ASCII. Without the mark,
       Notepad and PowerShell read the file as the local codepage and every
       non-Latin title comes out as mojibake. */
    if (end.QuadPart == 0) raw("\xEF\xBB\xBF", 3);
}

void log_close(void)
{
    if (g_file == INVALID_HANDLE_VALUE) return;
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
}

const WCHAR *log_path(void)
{
    return g_failed ? L"" : g_path;
}

void log_line(const char *text)
{
    if (g_file == INVALID_HANDLE_VALUE) return;
    stamp();
    raw(text, alen(text));
    raw("\r\n", 2);
}

void log_linew(const char *prefix, const WCHAR *text)
{
    char utf8[512];
    int n;

    if (g_file == INVALID_HANDLE_VALUE) return;

    stamp();
    raw(prefix, alen(prefix));

    n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof utf8, 0, 0);
    if (n > 1) raw(utf8, n - 1);   /* n counts the terminator */

    raw("\r\n", 2);
}
