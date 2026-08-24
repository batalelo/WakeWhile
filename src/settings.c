#include "settings.h"
#include "config.h"

/* A four-line text file. Deliberately not the registry: the whole program is
   one file you can delete, and its settings should be too. */

static void build_path(WCHAR *out)
{
    int i, cut = -1;

    out[0] = 0;
    if (!GetModuleFileNameW(0, out, MAX_PATH)) return;

    for (i = 0; out[i]; i++)
        if (out[i] == '\\' || out[i] == '/') cut = i;
    if (cut < 0) { out[0] = 0; return; }

    out[cut + 1] = 0;
    {
        const WCHAR *name = CFG_INI_NAME;
        int j = cut + 1;
        while (*name && j < MAX_PATH - 1) out[j++] = *name++;
        out[j] = 0;
    }
}

static int alen(const char *s)
{
    int i = 0;
    while (s[i]) i++;
    return i;
}

static int starts_with(const char *s, const char *prefix)
{
    int i = 0;
    while (prefix[i]) { if (s[i] != prefix[i]) return 0; i++; }
    return 1;
}

static unsigned int parse_uint(const char *s)
{
    unsigned int v = 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') v = v * 10u + (unsigned)(*s++ - '0');
    return v;
}

/* Clamped on the way in: a hand-edited file must not be able to put a
   threshold somewhere the slider could never express. */
static void set_clamped(ChannelRule *r, unsigned int v)
{
    if (v < r->lo) v = r->lo;
    if (v > r->hi) v = r->hi;
    r->threshold = v;
}

void settings_load(ActivityConfig *cfg, int *keep_display)
{
    WCHAR path[MAX_PATH];
    HANDLE f;
    char buf[1024];
    DWORD got = 0;
    int i, line_start;

    build_path(path);
    if (!path[0]) return;

    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) return;

    if (!ReadFile(f, buf, sizeof buf - 1, &got, 0)) got = 0;
    CloseHandle(f);
    buf[got] = 0;

    line_start = 0;
    for (i = 0; i <= (int)got; i++) {
        if (buf[i] != '\n' && buf[i] != '\r' && buf[i] != 0) continue;
        buf[i] = 0;
        {
            const char *L = &buf[line_start];
            if      (starts_with(L, "cpu="))     set_clamped(&cfg->ch[CH_CPU],  parse_uint(L + 4));
            else if (starts_with(L, "disk="))    set_clamped(&cfg->ch[CH_DISK], parse_uint(L + 5));
            else if (starts_with(L, "net="))     set_clamped(&cfg->ch[CH_NET],  parse_uint(L + 4));
            else if (starts_with(L, "mem="))     set_clamped(&cfg->ch[CH_MEM],  parse_uint(L + 4));
            else if (starts_with(L, "wait="))    set_clamped(&cfg->wait,      parse_uint(L + 5));
            else if (starts_with(L, "display=")) *keep_display = parse_uint(L + 8) != 0;
        }
        line_start = i + 1;
    }
}

void settings_save(const ActivityConfig *cfg, int keep_display)
{
    WCHAR path[MAX_PATH];
    HANDLE f;
    char buf[512];
    DWORD written;

    build_path(path);
    if (!path[0]) return;

    f = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) return;

    wsprintfA(buf,
        "# nosleep settings. Delete this file to go back to the defaults.\r\n"
        "# cpu is permille of one core, disk and net are bytes per second,\r\n"
        "# mem is page faults per second, wait is milliseconds.\r\n"
        "cpu=%u\r\n"
        "disk=%u\r\n"
        "net=%u\r\n"
        "mem=%u\r\n"
        "wait=%u\r\n"
        "display=%u\r\n",
        cfg->ch[CH_CPU].threshold,
        cfg->ch[CH_DISK].threshold,
        cfg->ch[CH_NET].threshold,
        cfg->ch[CH_MEM].threshold,
        cfg->wait.threshold,
        keep_display ? 1u : 0u);

    WriteFile(f, buf, (DWORD)alen(buf), &written, 0);
    CloseHandle(f);
}
