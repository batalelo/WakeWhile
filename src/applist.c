#include "applist.h"

#define MAX_SYSTEM_PROCS 2048

typedef HRESULT (WINAPI *PFN_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);

typedef struct {
    unsigned int pid;
    WCHAR name[64];
} ExeName;

typedef struct {
    AppEntry *out;
    int max;
    int count;
    unsigned int self_pid;
    const ExeName *names;
    int name_count;
    PFN_DwmGetWindowAttribute dwm_get;
} EnumCtx;

static void wcopy(WCHAR *dst, const WCHAR *src, int cap)
{
    int i = 0;
    if (cap <= 0) return;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* One snapshot up front is cheaper and more reliable than opening every
   window's process just to read its image name. */
static int read_exe_names(ExeName *out, int max)
{
    HANDLE snap;
    PROCESSENTRY32W pe;
    int n = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    pe.dwSize = sizeof pe;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (n >= max) break;
            out[n].pid = pe.th32ProcessID;
            wcopy(out[n].name, pe.szExeFile, 64);
            n++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return n;
}

static const WCHAR *lookup_exe(const EnumCtx *c, unsigned int pid)
{
    int i;
    for (i = 0; i < c->name_count; i++)
        if (c->names[i].pid == pid) return c->names[i].name;
    return L"";
}

/* Windows 10 keeps suspended UWP apps around as visible, titled, top-level
   windows that are not actually on screen. Without this they flood the list. */
static int is_cloaked(const EnumCtx *c, HWND h)
{
    DWORD cloaked = 0;
    if (!c->dwm_get) return 0;
    if (c->dwm_get(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked) != S_OK) return 0;
    return cloaked != 0;
}

static BOOL CALLBACK enum_proc(HWND h, LPARAM lp)
{
    EnumCtx *c = (EnumCtx *)lp;
    DWORD pid = 0;
    LONG_PTR ex;
    int i;
    AppEntry *e;

    if (c->count >= c->max) return FALSE;

    if (!IsWindowVisible(h)) return TRUE;
    /* Owned windows are dialogs and popups belonging to a window we have
       already listed, or will. */
    if (GetAncestor(h, GA_ROOTOWNER) != h) return TRUE;

    ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (GetWindowTextLengthW(h) == 0) return TRUE;
    if (is_cloaked(c, h)) return TRUE;

    GetWindowThreadProcessId(h, &pid);
    if (pid == 0 || pid == c->self_pid) return TRUE;

    /* One row per app, not one per window: z-order puts the window the user
       last touched first, so that is the one we keep. */
    for (i = 0; i < c->count; i++)
        if (c->out[i].pid == pid) return TRUE;

    e = &c->out[c->count];
    e->pid = pid;
    e->hwnd = h;
    e->title[0] = 0;
    GetWindowTextW(h, e->title, 128);
    wcopy(e->exe, lookup_exe(c, pid), 64);
    c->count++;

    return TRUE;
}

int applist_collect(AppEntry *out, int max)
{
    static ExeName names[MAX_SYSTEM_PROCS];
    EnumCtx c;
    HMODULE dwm;

    c.out = out;
    c.max = max;
    c.count = 0;
    c.self_pid = GetCurrentProcessId();
    c.names = names;
    c.name_count = read_exe_names(names, MAX_SYSTEM_PROCS);
    c.dwm_get = 0;

    dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm)
        c.dwm_get = (PFN_DwmGetWindowAttribute)
            GetProcAddress(dwm, "DwmGetWindowAttribute");

    EnumWindows(enum_proc, (LPARAM)&c);

    if (dwm) FreeLibrary(dwm);
    return c.count;
}
