#include "monitor.h"

#define MAX_SYSTEM_PROCS 2048

typedef struct {
    unsigned int pid;
    unsigned int ppid;
    unsigned char in_tree;
} ProcNode;

static u64 ft_to_u64(const FILETIME *ft)
{
    return ((u64)ft->dwHighDateTime << 32) | (u64)ft->dwLowDateTime;
}

static u64 now_100ns(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ft_to_u64(&ft);
}

/* PROCESS_QUERY_LIMITED_INFORMATION is enough for both calls we make and is
   granted for our own processes without elevation. The wider right is only a
   fallback for pre-Vista, which we will never meet in practice. */
static HANDLE open_for_query(unsigned int pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
    return h;
}

/* Fills `s` from a live handle. Returns 0 if the process went away mid-read. */
static int sample_process(unsigned int pid, ProcSample *s)
{
    HANDLE h;
    FILETIME create, exit, kernel, user;
    IO_COUNTERS io;

    h = open_for_query(pid);
    if (!h) return 0;

    if (!GetProcessTimes(h, &create, &exit, &kernel, &user)) {
        CloseHandle(h);
        return 0;
    }

    s->pid = pid;
    s->create_100ns = ft_to_u64(&create);
    s->cpu_100ns = ft_to_u64(&kernel) + ft_to_u64(&user);

    /* Read/write is real file work; "other" is DeviceIoControl, sockets and
       the pipes Electron apps talk to themselves over. Kept apart because
       the rule treats them completely differently. */
    if (GetProcessIoCounters(h, &io)) {
        s->rw_bytes = io.ReadTransferCount + io.WriteTransferCount;
        s->other_bytes = io.OtherTransferCount;
    } else {
        s->rw_bytes = 0;
        s->other_bytes = 0;
    }

    CloseHandle(h);
    return 1;
}

/* Reads the whole process table into `nodes`. Returns the count, or 0. */
static int read_process_table(ProcNode *nodes, int max)
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
            nodes[n].pid = pe.th32ProcessID;
            nodes[n].ppid = pe.th32ParentProcessID;
            nodes[n].in_tree = 0;
            n++;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return n;
}

/* Marks the root and everything descended from it. Repeated sweeps rather
   than a real graph walk: at one sweep per second over a couple of thousand
   processes the cost is invisible, and it needs no allocation. */
static void mark_tree(ProcNode *nodes, int n, unsigned int root)
{
    int i, changed = 1, guard = 0;

    for (i = 0; i < n; i++)
        if (nodes[i].pid == root) nodes[i].in_tree = 1;

    while (changed && guard++ < 64) {
        changed = 0;
        for (i = 0; i < n; i++) {
            int j;
            if (nodes[i].in_tree) continue;
            if (nodes[i].ppid == 0) continue;
            for (j = 0; j < n; j++) {
                if (nodes[j].in_tree && nodes[j].pid == nodes[i].ppid) {
                    nodes[i].in_tree = 1;
                    changed = 1;
                    break;
                }
            }
        }
    }
}

int monitor_start(Monitor *m, unsigned int pid)
{
    ProcSample s;

    tracker_reset(&m->tracker);
    m->root_pid = pid;
    m->root_create_100ns = 0;
    m->alive = 0;

    if (!sample_process(pid, &s)) return 0;

    m->root_create_100ns = s.create_100ns;
    m->alive = 1;
    return 1;
}

int monitor_tick(Monitor *m, TrackerDelta *out)
{
    static ProcNode nodes[MAX_SYSTEM_PROCS];
    static ProcSample samples[CFG_MAX_PROCS];
    ProcSample root_sample;
    int n, i, count = 0;

    m->pid_count = 0;

    out->d_cpu_100ns = 0;
    out->d_rw_bytes = 0;
    out->d_other_bytes = 0;
    out->d_wall_100ns = 0;
    out->n_procs = 0;

    if (!m->alive) return 0;

    /* The root having exited is the signal to stop, and a recycled PID must
       not be mistaken for it still running. */
    if (!sample_process(m->root_pid, &root_sample) ||
        root_sample.create_100ns != m->root_create_100ns) {
        m->alive = 0;
        return 0;
    }

    n = read_process_table(nodes, MAX_SYSTEM_PROCS);
    if (n == 0) {
        /* The snapshot failed this tick; report no change rather than
           pretending the app went quiet. */
        out->n_procs = 1;
        return 1;
    }

    mark_tree(nodes, n, m->root_pid);

    m->pid_count = 0;
    samples[count] = root_sample;
    m->pids[m->pid_count++] = root_sample.pid;
    count++;

    for (i = 0; i < n && count < CFG_MAX_PROCS; i++) {
        ProcSample s;
        if (!nodes[i].in_tree) continue;
        if (nodes[i].pid == m->root_pid) continue;
        if (!sample_process(nodes[i].pid, &s)) continue;

        /* Windows reuses PIDs, so a process can name a parent it never had.
           A genuine descendant cannot predate its root. */
        if (s.create_100ns < m->root_create_100ns) continue;

        m->pids[m->pid_count++] = s.pid;
        samples[count++] = s;
    }

    *out = tracker_update(&m->tracker, samples, count, now_100ns());
    return 1;
}
