/* engine.c - process/service detection and termination with hard safety guards.
 *
 * Detection pipeline (per process), in order:
 *   1. HARD-protected  -> always kept: self/tree, PID<=4, foreground app,
 *      PROTECTED_SYSTEM, ANTI_CHEAT, user whitelist.
 *   2. blacklist       -> always closed (unless hard-protected).
 *   3. mode rules      -> Smart/Aggressive use curated lists + heuristics;
 *      Risk/Nuclear use an allow-list (keep set), closing everything else.
 *   4. SOFT-system     -> anything under %WinDir% is shielded from heuristic
 *      and allow-list kills (but explicit lists/blacklist still apply), so the
 *      OS itself is never dismantled.
 *
 * Heuristics ("intelligent" detection) flag windowless background helpers whose
 * name matches known updater/telemetry/crash-handler patterns - catching junk
 * from vendors we never hard-coded, while sparing anything you are looking at. */
#include "engine.h"
#include "known_lists.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <winsvc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------- process snapshot */
typedef struct {
    DWORD  pid;
    DWORD  ppid;
    char   name[260];    /* original case */
    char   lname[260];   /* lower-case    */
    char   lpath[MAX_PATH]; /* lower-case full image path ("" if unknown) */
    int    self;         /* part of GameMode's own process tree */
    int    has_window;   /* owns a visible top-level window */
    SIZE_T mem;          /* working-set bytes */
} ProcEntry;

typedef struct {
    ProcEntry *items;
    int   count, cap;
    DWORD foreground_pid;
} ProcList;

static void proclist_free(ProcList *pl)
{
    free(pl->items); pl->items = NULL; pl->count = pl->cap = 0;
}

/* ---- visible-window collection (for the "background helper" heuristic) ---- */
typedef struct { DWORD *pids; int count, cap; } PidSet;

static void pidset_add(PidSet *s, DWORD pid)
{
    int i;
    for (i = 0; i < s->count; ++i) if (s->pids[i] == pid) return;
    if (s->count >= s->cap) {
        int ncap = s->cap ? s->cap * 2 : 64;
        DWORD *np = (DWORD *)realloc(s->pids, (size_t)ncap * sizeof(DWORD));
        if (!np) return;
        s->pids = np; s->cap = ncap;
    }
    s->pids[s->count++] = pid;
}

static int pidset_has(const PidSet *s, DWORD pid)
{
    int i;
    for (i = 0; i < s->count; ++i) if (s->pids[i] == pid) return 1;
    return 0;
}

static BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp)
{
    PidSet *set = (PidSet *)lp;
    DWORD pid = 0;
    LONG ex;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;     /* top-level only */
    ex = GetWindowLongA(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;                  /* tray/util windows */
    if (GetWindowTextLengthA(hwnd) == 0) return TRUE;        /* no title bar */
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid) pidset_add(set, pid);
    return TRUE;
}

/* ---- lower-cased Windows directory, cached ---- */
static const char *windir_lower(void)
{
    static char dir[MAX_PATH];
    static int ready = 0;
    if (!ready) {
        char raw[MAX_PATH];
        UINT n = GetWindowsDirectoryA(raw, sizeof(raw));
        if (n == 0 || n >= sizeof(raw)) raw[0] = '\0';
        str_lower_copy(dir, sizeof(dir), raw);
        ready = 1;
    }
    return dir;
}

/* Capture all processes plus per-process path, memory and window/foreground
 * state used by the decision logic. */
static int proclist_capture(ProcList *pl)
{
    HANDLE snap;
    PROCESSENTRY32 pe;
    PidSet windows = { NULL, 0, 0 };

    pl->items = NULL; pl->count = pl->cap = 0;
    pl->foreground_pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pl->foreground_pid);
    EnumWindows(enum_windows_cb, (LPARAM)&windows);

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) { free(windows.pids); return 0; }
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            ProcEntry *en;
            HANDLE h;
            if (pl->count >= pl->cap) {
                int ncap = pl->cap ? pl->cap * 2 : 256;
                ProcEntry *ni = (ProcEntry *)realloc(pl->items, (size_t)ncap * sizeof(ProcEntry));
                if (!ni) break;
                pl->items = ni; pl->cap = ncap;
            }
            en = &pl->items[pl->count++];
            en->pid = pe.th32ProcessID;
            en->ppid = pe.th32ParentProcessID;
            en->self = 0;
            en->mem = 0;
            en->lpath[0] = '\0';
            lstrcpynA(en->name, pe.szExeFile, sizeof(en->name));
            str_lower_copy(en->lname, sizeof(en->lname), en->name);
            en->has_window = pidset_has(&windows, en->pid);

            /* best-effort path + memory (needs an open handle) */
            h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                            FALSE, en->pid);
            if (!h)
                h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, en->pid);
            if (h) {
                char path[MAX_PATH];
                DWORD sz = sizeof(path);
                PROCESS_MEMORY_COUNTERS pmc;
                if (QueryFullProcessImageNameA(h, 0, path, &sz))
                    str_lower_copy(en->lpath, sizeof(en->lpath), path);
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                    en->mem = pmc.WorkingSetSize;
                CloseHandle(h);
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    free(windows.pids);
    return pl->count;
}

/* Mark GameMode's own pid, its ancestors and all its descendants. */
static void proclist_mark_self(ProcList *pl)
{
    DWORD self = GetCurrentProcessId();
    int i, changed, idx_self = -1;

    for (i = 0; i < pl->count; ++i)
        if (pl->items[i].pid == self) { pl->items[i].self = 1; idx_self = i; }

    if (idx_self >= 0) {
        DWORD cur = pl->items[idx_self].ppid;
        int guard = 0;
        while (cur != 0 && guard++ < 64) {
            int found = -1, j;
            for (j = 0; j < pl->count; ++j)
                if (pl->items[j].pid == cur) { found = j; break; }
            if (found < 0) break;
            pl->items[found].self = 1;
            cur = pl->items[found].ppid;
        }
    }
    do {
        changed = 0;
        for (i = 0; i < pl->count; ++i) {
            if (pl->items[i].self) continue;
            {
                DWORD pp = pl->items[i].ppid;
                int j;
                for (j = 0; j < pl->count; ++j)
                    if (pl->items[j].pid == pp && pl->items[j].self) {
                        pl->items[i].self = 1; changed = 1; break;
                    }
            }
        }
    } while (changed);
}

/* ----------------------------------------------------- decision logic */

/* Never-kill tier: nothing here is ever terminated, in any mode. */
static int is_hard_protected(const Engine *e, const ProcEntry *p, DWORD foreground)
{
    if (p->self) return 1;
    if (p->pid <= 4) return 1;                          /* System / Idle */
    if (p->pid == foreground && foreground != 0) return 1; /* app you're using */
    if (namelist_contains(&PROTECTED_SYSTEM, p->lname)) return 1;
    if (namelist_contains(&ANTI_CHEAT, p->lname)) return 1;
    if (strlist_contains(&e->cfg->whitelist, p->lname)) return 1;
    return 0;
}

/* Soft tier: OS files under %WinDir%. Shielded from heuristic and allow-list
 * kills so the operating system is never taken apart, but explicit curated
 * lists / the user blacklist can still target them. */
static int is_soft_system(const ProcEntry *p)
{
    const char *wd = windir_lower();
    if (wd[0] == '\0' || p->lpath[0] == '\0') return 0;
    return strncmp(p->lpath, wd, strlen(wd)) == 0;
}

/* Apps we never want heuristics to touch (real, useful software). */
static int is_known_good(const char *lname)
{
    return namelist_contains(&GAME_PLATFORMS, lname) ||
           namelist_contains(&KEEP_WHILE_GAMING, lname) ||
           namelist_contains(&COMMON_APPS, lname);
}

/* For Risk/Nuclear: is this name on the allow-list we keep? */
static int in_keep_set(const Engine *e, const char *lname)
{
    if (namelist_contains(&PROTECTED_SYSTEM, lname)) return 1;
    if (namelist_contains(&ANTI_CHEAT, lname)) return 1;
    if (namelist_contains(&GAME_PLATFORMS, lname)) return 1;
    if (namelist_contains(&KEEP_WHILE_GAMING, lname)) return 1;
    if (strlist_contains(&e->cfg->whitelist, lname)) return 1;
    if (e->cfg->mode == MODE_RISK && namelist_contains(&COMMON_APPS, lname)) return 1;
    return 0;
}

/* Heuristic junk detection. Conservative: requires a windowless background
 * process whose name matches a junk pattern and is not known-good/soft-system. */
static int heuristic_junk(const Engine *e, const ProcEntry *p)
{
    if (!e->cfg->heuristics) return 0;
    if (p->has_window) return 0;          /* you can see it -> leave it */
    if (is_known_good(p->lname)) return 0;
    if (is_soft_system(p)) return 0;      /* OS component -> leave it */

    if (namelist_substr(&JUNK_PATTERNS_STRONG, p->lname)) return 1; /* Smart+ */
    if (e->cfg->mode >= MODE_AGGRESSIVE &&
        namelist_substr(&JUNK_PATTERNS_WEAK, p->lname)) return 1;   /* Aggr+  */
    return 0;
}

/* Decide whether a (non hard-protected) process should be terminated. */
static int should_kill(const Engine *e, const ProcEntry *p)
{
    const char *lname = p->lname;

    if (strlist_contains(&e->cfg->blacklist, lname)) return 1;  /* user wins */

    switch (e->cfg->mode) {
    case MODE_SMART:
        if (namelist_contains(&BLOATWARE, lname)) return 1;
        return heuristic_junk(e, p);
    case MODE_AGGRESSIVE:
        if (namelist_contains(&BLOATWARE, lname)) return 1;
        if (namelist_contains(&BACKGROUND_NOISE, lname)) return 1;
        return heuristic_junk(e, p);
    case MODE_RISK:
    case MODE_NUCLEAR:
        if (in_keep_set(e, lname)) return 0;
        if (is_soft_system(p)) return 0;   /* don't dismantle the OS */
        return 1;
    }
    return 0;
}

/* ----------------------------------------------------- logging */
static void engine_log(Engine *e, int level, const char *fmt, ...)
{
    char *buf;
    va_list ap;
    if (!e->notify) return;
    buf = (char *)malloc(512);
    if (!buf) return;
    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);   /* note: supports %s %d %u %lu %x, NOT %f */
    va_end(ap);
    PostMessageA(e->notify, WM_APP_LOG, (WPARAM)level, (LPARAM)buf);
}

/* ----------------------------------------------------- termination */
static int terminate_pid(Engine *e, const ProcEntry *p)
{
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p->pid);
    if (!h) {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            engine_log(e, LOG_WARN,
                "Access denied closing %s (pid %lu). Run GameMode as Administrator.",
                p->name, p->pid);
        return 0;
    }
    if (TerminateProcess(h, 1)) {
        CloseHandle(h);
        engine_log(e, LOG_KILL, "Closed %s (pid %lu, %lu MB)",
                   p->name, p->pid, (unsigned long)(p->mem / (1024 * 1024)));
        return 1;
    }
    CloseHandle(h);
    engine_log(e, LOG_ERROR, "Failed to close %s (pid %lu)", p->name, p->pid);
    return 0;
}

/* ----------------------------------------------------- services */
static int service_accepts_stop(SC_HANDLE svc)
{
    SERVICE_STATUS_PROCESS ssp;
    DWORD needed = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                             (LPBYTE)&ssp, sizeof(ssp), &needed)) {
        if (ssp.dwCurrentState != SERVICE_RUNNING) return 0;
        return (ssp.dwControlsAccepted & SERVICE_ACCEPT_STOP) ? 1 : 0;
    }
    return 0;
}

static long sweep_services(Engine *e, int dry)
{
    SC_HANDLE scm;
    DWORD bytes = 0, returned = 0, resume = 0;
    ENUM_SERVICE_STATUS_PROCESSA *svc;
    BYTE *buf;
    long stopped = 0;
    DWORD i;

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE | SC_MANAGER_CONNECT);
    if (!scm) { engine_log(e, LOG_WARN, "Cannot open service manager (needs admin)."); return 0; }

    EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                          SERVICE_ACTIVE, NULL, 0, &bytes, &returned, &resume, NULL);
    if (GetLastError() != ERROR_MORE_DATA || bytes == 0) { CloseServiceHandle(scm); return 0; }

    buf = (BYTE *)malloc(bytes);
    if (!buf) { CloseServiceHandle(scm); return 0; }
    resume = 0;
    if (!EnumServicesStatusExA(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                               SERVICE_ACTIVE, buf, bytes, &bytes, &returned, &resume, NULL)) {
        free(buf); CloseServiceHandle(scm); return 0;
    }

    svc = (ENUM_SERVICE_STATUS_PROCESSA *)buf;
    for (i = 0; i < returned; ++i) {
        char lname[260];
        SC_HANDLE h;
        str_lower_copy(lname, sizeof(lname), svc[i].lpServiceName);
        if (namelist_contains(&ESSENTIAL_SERVICES, lname)) continue;
        if (strlist_contains(&e->cfg->whitelist, lname)) continue;

        h = OpenServiceA(scm, svc[i].lpServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
        if (!h) continue;
        if (service_accepts_stop(h)) {
            if (dry) {
                engine_log(e, LOG_DRY, "[dry-run] would stop service '%s'", lname);
                ++stopped;
            } else {
                SERVICE_STATUS st;
                if (ControlService(h, SERVICE_CONTROL_STOP, &st)) {
                    engine_log(e, LOG_KILL, "Stopped service '%s'", lname);
                    ++stopped;
                } else {
                    engine_log(e, LOG_WARN, "Could not stop service '%s'", lname);
                }
            }
        }
        CloseServiceHandle(h);
    }
    free(buf);
    CloseServiceHandle(scm);
    return stopped;
}

/* ----------------------------------------------------- one scan pass */
long engine_scan_once(Engine *e)
{
    ProcList pl;
    long killed = 0, targets = 0, svc_stopped = 0;
    SIZE_T freed = 0;
    int dry = e->cfg->dry_run;
    int i;

    if (!proclist_capture(&pl)) {
        engine_log(e, LOG_ERROR, "Could not enumerate processes.");
        return 0;
    }
    proclist_mark_self(&pl);

    for (i = 0; i < pl.count; ++i) {
        ProcEntry *p = &pl.items[i];
        if (is_hard_protected(e, p, pl.foreground_pid)) continue;
        if (!should_kill(e, p)) continue;
        ++targets;
        if (dry) {
            engine_log(e, LOG_DRY, "[dry-run] would close %s (pid %lu, %lu MB)",
                       p->name, p->pid, (unsigned long)(p->mem / (1024 * 1024)));
            continue;
        }
        if (terminate_pid(e, p)) { ++killed; freed += p->mem; }
    }
    proclist_free(&pl);

    if (e->cfg->manage_services &&
        (e->cfg->mode == MODE_RISK || e->cfg->mode == MODE_NUCLEAR))
        svc_stopped = sweep_services(e, dry);

    EnterCriticalSection(&e->lock);
    e->stats.scans += 1;
    e->stats.killed += killed;
    e->stats.services_stopped += svc_stopped;
    e->stats.last_targets = targets;
    e->stats.ram_freed_mb += (long)(freed / (1024 * 1024));
    LeaveCriticalSection(&e->lock);

    if (targets == 0 && svc_stopped == 0)
        engine_log(e, LOG_SCAN, "Scan complete - nothing to close.");

    if (e->notify) PostMessageA(e->notify, WM_APP_STATS, 0, 0);
    return killed;
}

/* ----------------------------------------------------- worker thread */
static DWORD WINAPI worker(LPVOID arg)
{
    Engine *e = (Engine *)arg;
    while (WaitForSingleObject(e->stop_evt, 0) != WAIT_OBJECT_0) {
        DWORD ms;
        engine_scan_once(e);
        ms = (DWORD)(e->cfg->scan_interval * 1000.0);
        if (ms < 1000) ms = 1000;
        if (WaitForSingleObject(e->stop_evt, ms) == WAIT_OBJECT_0) break;
    }
    return 0;
}

/* ----------------------------------------------------- lifecycle */
void engine_init(Engine *e, Config *cfg, HWND notify)
{
    memset(e, 0, sizeof(*e));
    e->cfg = cfg;
    e->notify = notify;
    e->stop_evt = CreateEventA(NULL, TRUE, FALSE, NULL); /* manual reset */
    InitializeCriticalSection(&e->lock);
}

void engine_destroy(Engine *e)
{
    engine_stop(e);
    if (e->stop_evt) CloseHandle(e->stop_evt);
    DeleteCriticalSection(&e->lock);
}

int engine_running(const Engine *e) { return e->running != 0; }

void engine_start(Engine *e)
{
    if (e->running) return;
    ResetEvent(e->stop_evt);
    e->running = 1;
    e->thread = CreateThread(NULL, 0, worker, e, 0, NULL);
    if (!e->thread) { e->running = 0; return; }
    engine_log(e, LOG_INFO, "Engine started in '%s' mode.", MODE_KEYS[e->cfg->mode]);
}

void engine_stop(Engine *e)
{
    if (!e->running) return;
    SetEvent(e->stop_evt);
    if (e->thread) {
        WaitForSingleObject(e->thread, 5000);
        CloseHandle(e->thread);
        e->thread = NULL;
    }
    e->running = 0;
    engine_log(e, LOG_INFO, "Engine stopped.");
}

void engine_get_stats(Engine *e, Stats *out)
{
    EnterCriticalSection(&e->lock);
    *out = e->stats;
    LeaveCriticalSection(&e->lock);
}

/* ----------------------------------------------------- GUI read helpers */
void engine_preview(Engine *e, ProcCb cb, void *user)
{
    ProcList pl;
    int i;
    if (!proclist_capture(&pl)) return;
    proclist_mark_self(&pl);
    for (i = 0; i < pl.count; ++i) {
        ProcEntry *p = &pl.items[i];
        if (is_hard_protected(e, p, pl.foreground_pid)) continue;
        if (should_kill(e, p))
            cb(p->name, p->pid, (unsigned long)(p->mem / (1024 * 1024)), 1, user);
    }
    proclist_free(&pl);
}

void engine_list_running(Engine *e, ProcCb cb, void *user)
{
    ProcList pl;
    int i;
    if (!proclist_capture(&pl)) return;
    proclist_mark_self(&pl);
    for (i = 0; i < pl.count; ++i) {
        ProcEntry *p = &pl.items[i];
        int doomed = (!is_hard_protected(e, p, pl.foreground_pid)) && should_kill(e, p);
        cb(p->name, p->pid, (unsigned long)(p->mem / (1024 * 1024)), doomed, user);
    }
    proclist_free(&pl);
}

int engine_kill_pid(Engine *e, unsigned long pid, const char *name)
{
    char lname[260];
    HANDLE h;
    str_lower_copy(lname, sizeof(lname), name);

    if (pid <= 4 || pid == GetCurrentProcessId() ||
        namelist_contains(&PROTECTED_SYSTEM, lname) ||
        namelist_contains(&ANTI_CHEAT, lname)) {
        engine_log(e, LOG_WARN, "Refusing to close protected process %s.", name);
        return 0;
    }
    h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!h) {
        engine_log(e, LOG_WARN, "Access denied closing %s (pid %lu).", name, pid);
        return 0;
    }
    if (TerminateProcess(h, 1)) {
        CloseHandle(h);
        engine_log(e, LOG_KILL, "Closed %s (pid %lu) [manual]", name, pid);
        return 1;
    }
    CloseHandle(h);
    engine_log(e, LOG_ERROR, "Failed to close %s (pid %lu)", name, pid);
    return 0;
}
