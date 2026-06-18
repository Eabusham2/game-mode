/* engine.c - process/service detection and termination with hard safety guards.
 *
 * Every kill decision flows through is_protected(): critical OS processes,
 * anti-cheats, GameMode's own process tree and the user whitelist can NEVER be
 * terminated - not by the blacklist, not by Nuclear mode. */
#include "engine.h"
#include "known_lists.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------- process snapshot */
typedef struct {
    DWORD pid;
    DWORD ppid;
    char  name[260];   /* original case */
    char  lname[260];  /* lower-case    */
    int   self;        /* part of GameMode's own process tree */
} ProcEntry;

typedef struct {
    ProcEntry *items;
    int count, cap;
} ProcList;

static void proclist_free(ProcList *pl) { free(pl->items); pl->items = NULL; pl->count = pl->cap = 0; }

static int proclist_capture(ProcList *pl)
{
    HANDLE snap;
    PROCESSENTRY32 pe;
    pl->items = NULL; pl->count = pl->cap = 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            if (pl->count >= pl->cap) {
                int ncap = pl->cap ? pl->cap * 2 : 256;
                ProcEntry *ni = (ProcEntry *)realloc(pl->items, (size_t)ncap * sizeof(ProcEntry));
                if (!ni) break;
                pl->items = ni; pl->cap = ncap;
            }
            {
                ProcEntry *en = &pl->items[pl->count++];
                en->pid = pe.th32ProcessID;
                en->ppid = pe.th32ParentProcessID;
                en->self = 0;
                lstrcpynA(en->name, pe.szExeFile, sizeof(en->name));
                str_lower_copy(en->lname, sizeof(en->lname), en->name);
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return pl->count;
}

/* Mark GameMode's own pid, its ancestors and all its descendants as protected
 * so the tool can never terminate itself or the shell that launched it. */
static void proclist_mark_self(ProcList *pl)
{
    DWORD self = GetCurrentProcessId();
    int i, changed, idx_self = -1;

    for (i = 0; i < pl->count; ++i)
        if (pl->items[i].pid == self) { pl->items[i].self = 1; idx_self = i; }

    /* Ancestors: walk parent chain upward from ourselves. */
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
    /* Descendants: any process whose parent is already marked, repeated until
     * the set stops growing. */
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

/* True when a process must NEVER be terminated. Single chokepoint. */
static int is_protected(const Engine *e, const ProcEntry *p)
{
    if (p->self) return 1;
    if (p->pid <= 4) return 1;                       /* System / Idle */
    if (namelist_contains(&PROTECTED_SYSTEM, p->lname)) return 1;
    if (namelist_contains(&ANTI_CHEAT, p->lname)) return 1;
    if (strlist_contains(&e->cfg->whitelist, p->lname)) return 1;
    return 0;
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

/* Decide whether a (already non-protected) process should be terminated. */
static int should_kill(const Engine *e, const char *lname)
{
    if (strlist_contains(&e->cfg->blacklist, lname)) return 1; /* blacklist wins */

    switch (e->cfg->mode) {
    case MODE_SMART:
        return namelist_contains(&BLOATWARE, lname);
    case MODE_AGGRESSIVE:
        return namelist_contains(&BLOATWARE, lname) ||
               namelist_contains(&BACKGROUND_NOISE, lname);
    case MODE_RISK:
    case MODE_NUCLEAR:
        return !in_keep_set(e, lname);
    }
    return 0;
}

/* ----------------------------------------------------- logging */
static void engine_log(Engine *e, int level, const char *fmt, ...)
{
    char *buf;
    va_list ap;
    int n;
    if (!e->notify) return;
    buf = (char *)malloc(512);
    if (!buf) return;
    va_start(ap, fmt);
    n = wvsprintfA(buf, fmt, ap);   /* wvsprintf: no %f, but we don't need it */
    va_end(ap);
    (void)n;
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
        engine_log(e, LOG_KILL, "Closed %s (pid %lu)", p->name, p->pid);
        return 1;
    }
    CloseHandle(h);
    engine_log(e, LOG_ERROR, "Failed to close %s (pid %lu)", p->name, p->pid);
    return 0;
}

/* ----------------------------------------------------- services */
#include <winsvc.h>

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
    int dry = e->cfg->dry_run;
    int i;

    if (!proclist_capture(&pl)) {
        engine_log(e, LOG_ERROR, "Could not enumerate processes.");
        return 0;
    }
    proclist_mark_self(&pl);

    for (i = 0; i < pl.count; ++i) {
        ProcEntry *p = &pl.items[i];
        if (is_protected(e, p)) continue;
        if (!should_kill(e, p->lname)) continue;
        ++targets;
        if (dry) {
            engine_log(e, LOG_DRY, "[dry-run] would close %s (pid %lu)", p->name, p->pid);
            continue;
        }
        if (terminate_pid(e, p)) ++killed;
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
        if (is_protected(e, p)) continue;
        if (should_kill(e, p->lname)) cb(p->name, p->pid, 1, user);
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
        int doomed = (!is_protected(e, p)) && should_kill(e, p->lname);
        cb(p->name, p->pid, doomed, user);
    }
    proclist_free(&pl);
}
