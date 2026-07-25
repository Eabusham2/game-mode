/* engine.h - the background detector/terminator. */
#ifndef ENGINE_H
#define ENGINE_H

#include "gamemode.h"
#include "config.h"

typedef struct {
    long scans;
    long killed;
    long services_stopped;
    long last_targets;
    long ram_freed_mb;     /* cumulative working-set of closed processes */
} Stats;

typedef struct {
    Config        *cfg;        /* not owned */
    HWND           notify;     /* window that receives WM_APP_LOG / WM_APP_STATS */
    HANDLE         thread;
    HANDLE         stop_evt;
    volatile LONG  running;
    Stats          stats;
    CRITICAL_SECTION lock;
    /* session undo state - everything here is TEMPORARY and reversed on OFF */
    StrList        stopped_services; /* service names we stopped this session  */
    StrList        closed_paths;     /* image paths of windowed apps we closed  */
} Engine;

/* Callback used by enumeration helpers (preview / list).
 * mem_mb is the process working set in MB; doomed is 1 if the current mode
 * would close it. */
typedef void (*ProcCb)(const char *name, unsigned long pid,
                       unsigned long mem_mb, int doomed, void *user);

void engine_init(Engine *e, Config *cfg, HWND notify);
void engine_destroy(Engine *e);

void engine_start(Engine *e);
void engine_stop(Engine *e);
int  engine_running(const Engine *e);

/* Reverse this session's temporary changes (restart stopped services, and
 * optionally reopen closed apps). Invoked automatically by engine_stop. */
void engine_restore(Engine *e);

/* Run exactly one detection/termination pass (used by the worker thread). */
long engine_scan_once(Engine *e);

/* Read-only helpers for the GUI. */
void engine_preview(Engine *e, ProcCb cb, void *user);       /* would-close only */
void engine_list_running(Engine *e, ProcCb cb, void *user);  /* every process + fate */

/* Manually terminate one process (Processes tab). Refuses hard-protected
 * processes (critical OS, anti-cheat, GameMode itself). Returns 1 on success. */
int  engine_kill_pid(Engine *e, unsigned long pid, const char *name);

void engine_get_stats(Engine *e, Stats *out);

/* Guard shared Config state (whitelist/blacklist) that the GUI thread mutates
 * while the worker thread may be reading it. GUI mutators wrap edits to
 * cfg->whitelist / cfg->blacklist in engine_lock/engine_unlock. */
void engine_lock(Engine *e);
void engine_unlock(Engine *e);

#endif /* ENGINE_H */
