#ifndef WAKEWHILE_WINCOMPAT_H
#define WAKEWHILE_WINCOMPAT_H

/* TCC ships a trimmed set of Windows headers: no tlhelp32.h, no shellapi.h,
   no dwmapi.h, and a few constants that postdate them. Everything we need
   from those is declared here, matching the SDK layout exactly.

   The matching imports that TCC's .def files also lack are supplied by the
   small .def files under build/. */

#include <windows.h>

/* ------------------------------------------------------ tlhelp32.h subset */

#define TH32CS_SNAPPROCESS 0x00000002

typedef struct tagPROCESSENTRY32W {
    DWORD     dwSize;
    DWORD     cntUsage;
    DWORD     th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD     th32ModuleID;
    DWORD     cntThreads;
    DWORD     th32ParentProcessID;
    LONG      pcPriClassBase;
    DWORD     dwFlags;
    WCHAR     szExeFile[MAX_PATH];
} PROCESSENTRY32W;

WINBASEAPI HANDLE WINAPI CreateToolhelp32Snapshot(DWORD, DWORD);
WINBASEAPI BOOL   WINAPI Process32FirstW(HANDLE, PROCESSENTRY32W *);
WINBASEAPI BOOL   WINAPI Process32NextW(HANDLE, PROCESSENTRY32W *);

/* ------------------------------------------------------ shellapi.h subset */

#define NIM_ADD    0x00000000
#define NIM_MODIFY 0x00000001
#define NIM_DELETE 0x00000002

#define NIF_MESSAGE 0x00000001
#define NIF_ICON    0x00000002
#define NIF_TIP     0x00000004

typedef struct _NOTIFYICONDATAW {
    DWORD cbSize;
    HWND  hWnd;
    UINT  uID;
    UINT  uFlags;
    UINT  uCallbackMessage;
    HICON hIcon;
    WCHAR szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    WCHAR szInfo[256];
    union { UINT uTimeout; UINT uVersion; } DUMMYUNIONNAME;
    WCHAR szInfoTitle[64];
    DWORD dwInfoFlags;
} NOTIFYICONDATAW;

WINUSERAPI BOOL WINAPI Shell_NotifyIconW(DWORD, NOTIFYICONDATAW *);

/* ------------------------------------------------ constants TCC predates */

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif

#ifndef GA_ROOTOWNER
#define GA_ROOTOWNER 3
#endif

#ifndef LWA_ALPHA
#define LWA_ALPHA 0x00000002
#endif

WINBASEAPI BOOL WINAPI QueryFullProcessImageNameW(HANDLE, DWORD, LPWSTR, PDWORD);
WINUSERAPI BOOL WINAPI SetProcessDPIAware(void);

/* Windows 10 1809 and later; resolved at run time so older builds simply do
   not get a dark title bar. */
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#define DWMWA_CLOAKED                 14


/* windowsx.h is not shipped either. */
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#ifndef FW_SEMIBOLD
#define FW_SEMIBOLD 600
#endif


/* winnls.h is not shipped; only these two pieces of it are needed. */
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif
WINBASEAPI int WINAPI WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR,
                                          int, LPCSTR, LPBOOL);


/* ------------------------------------------------------ psapi.h subset */

typedef struct _PROCESS_MEMORY_COUNTERS {
    DWORD  cb;
    DWORD  PageFaultCount;
    SIZE_T PeakWorkingSetSize;
    SIZE_T WorkingSetSize;
    SIZE_T QuotaPeakPagedPoolUsage;
    SIZE_T QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage;
    SIZE_T QuotaNonPagedPoolUsage;
    SIZE_T PagefileUsage;
    SIZE_T PeakPagefileUsage;
} PROCESS_MEMORY_COUNTERS;

WINBASEAPI BOOL WINAPI GetProcessMemoryInfo(HANDLE, PROCESS_MEMORY_COUNTERS *,
                                            DWORD);

#ifndef PROCESS_VM_READ
#define PROCESS_VM_READ 0x0010
#endif

#endif
