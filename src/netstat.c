#include "netstat.h"

#define TCP_TABLE_OWNER_PID_ALL 5
#define MIB_TCP_STATE_ESTAB     5
#define AF_INET_                2
#define AF_INET6_              23

typedef struct {
    DWORD dwState;
    DWORD dwLocalAddr;
    DWORD dwLocalPort;
    DWORD dwRemoteAddr;
    DWORD dwRemotePort;
    DWORD dwOwningPid;
} TCPROW4;

typedef struct {
    DWORD dwNumEntries;
    TCPROW4 table[1];
} TCPTABLE4;

typedef struct {
    UCHAR ucLocalAddr[16];
    DWORD dwLocalScopeId;
    DWORD dwLocalPort;
    UCHAR ucRemoteAddr[16];
    DWORD dwRemoteScopeId;
    DWORD dwRemotePort;
    DWORD dwState;
    DWORD dwOwningPid;
} TCPROW6;

typedef struct {
    DWORD dwNumEntries;
    TCPROW6 table[1];
} TCPTABLE6;

typedef DWORD (WINAPI *PFN_GetExtendedTcpTable)(PVOID, PDWORD, BOOL, ULONG,
                                                int, ULONG);

static HMODULE g_iphlp;
static PFN_GetExtendedTcpTable g_get_table;
static void *g_buf;
static DWORD g_buf_size;

void netstat_init(void)
{
    g_iphlp = LoadLibraryW(L"iphlpapi.dll");
    if (g_iphlp)
        g_get_table = (PFN_GetExtendedTcpTable)
            GetProcAddress(g_iphlp, "GetExtendedTcpTable");
}

void netstat_shutdown(void)
{
    if (g_buf) { HeapFree(GetProcessHeap(), 0, g_buf); g_buf = 0; g_buf_size = 0; }
    if (g_iphlp) { FreeLibrary(g_iphlp); g_iphlp = 0; g_get_table = 0; }
}

static int owned(const unsigned int *pids, int n, DWORD pid)
{
    int i;
    for (i = 0; i < n; i++)
        if (pids[i] == (unsigned int)pid) return 1;
    return 0;
}

/* 127.x.x.x, or a remote address that was never filled in. The field is in
   network byte order, so the first octet is the low byte. */
static int remote4_is_local(DWORD addr)
{
    return addr == 0 || (addr & 0xFF) == 127;
}

static int remote6_is_local(const UCHAR *a)
{
    int i, zero = 1;
    for (i = 0; i < 16; i++) if (a[i]) { zero = 0; break; }
    if (zero) return 1;                       /* :: */
    for (i = 0; i < 15; i++) if (a[i]) return 0;
    return a[15] == 1;                        /* ::1 */
}

/* Grows the shared buffer until the table fits, then hands it back. */
static void *fetch(ULONG family, DWORD *out_size)
{
    DWORD size, rc;
    int attempt;

    for (attempt = 0; attempt < 4; attempt++) {
        size = g_buf_size;
        rc = g_get_table(g_buf, &size, FALSE, family,
                         TCP_TABLE_OWNER_PID_ALL, 0);
        if (rc == NO_ERROR) { *out_size = size; return g_buf; }
        if (rc != ERROR_INSUFFICIENT_BUFFER) return 0;

        if (g_buf) HeapFree(GetProcessHeap(), 0, g_buf);
        /* Headroom, because the table can grow between the sizing call and
           the real one. */
        g_buf_size = size + 8192;
        g_buf = HeapAlloc(GetProcessHeap(), 0, g_buf_size);
        if (!g_buf) { g_buf_size = 0; return 0; }
    }
    return 0;
}

int netstat_established(const unsigned int *pids, int n)
{
    DWORD size;
    int found = 0;
    unsigned int i;

    if (!g_get_table || n <= 0) return 0;

    {
        TCPTABLE4 *t = (TCPTABLE4 *)fetch(AF_INET_, &size);
        if (t) {
            for (i = 0; i < t->dwNumEntries; i++) {
                const TCPROW4 *r = &t->table[i];
                if (r->dwState != MIB_TCP_STATE_ESTAB) continue;
                if (remote4_is_local(r->dwRemoteAddr)) continue;
                if (owned(pids, n, r->dwOwningPid)) found++;
            }
        }
    }

    {
        TCPTABLE6 *t = (TCPTABLE6 *)fetch(AF_INET6_, &size);
        if (t) {
            for (i = 0; i < t->dwNumEntries; i++) {
                const TCPROW6 *r = &t->table[i];
                if (r->dwState != MIB_TCP_STATE_ESTAB) continue;
                if (remote6_is_local(r->ucRemoteAddr)) continue;
                if (owned(pids, n, r->dwOwningPid)) found++;
            }
        }
    }

    return found;
}
