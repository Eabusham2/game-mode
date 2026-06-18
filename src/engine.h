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
} Stats;

typedef struct {
    Config        *cfg;        /* not owned */
    HWND           notify;     /* window that receives WM_APP_LOG / WM_APP_STATS */
    HANDLE         thread;
    HANDLE         stop_evt;
    volatile LONG  running;
    Stats          stats;
    CRITICAL_SECTION lock;
} Engine;

/* Callback used by enumeration helpers (preview / list). */
typedef void (*ProcCb)(const char *name, unsigned long pid, int doomed, void *user);

void engine_init(Engine *e, Config *cfg, HWND notify);
void engine_destroy(Engine *e);

void engine_start(Engine *e);
void engine_stop(Engine *e);
int  engine_running(const Engine *e);

/* Run exactly one detection/termination pass (used by the worker thread). */
long engine_scan_once(Engine *e);

/* Read-only helpers for the GUI. */
void engine_preview(Engine *e, ProcCb cb, void *user);       /* would-close only */
void engine_list_running(Engine *e, ProcCb cb, void *user);  /* every process + fate */

void engine_get_stats(Engine *e, Stats *out);

#endif /* ENGINE_H */
