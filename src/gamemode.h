/* gamemode.h - shared declarations, control IDs and app-wide constants.
 *
 * GameMode is a native Win32 (C) desktop application that continuously detects
 * and disables useless background programs, bloatware, updaters and (optionally)
 * non-essential Windows services via a single master toggle.
 *
 * Build is ANSI (the project intentionally does NOT define UNICODE) so process
 * and service names can be handled as plain char strings throughout.
 */
#ifndef GAMEMODE_H
#define GAMEMODE_H

#ifndef WINVER
#define WINVER 0x0601        /* Windows 7+ */
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include <windows.h>
#include <stddef.h>

#define APP_NAME      "GameMode"
#define APP_TITLE     "GameMode - System Optimizer"
#define APP_VERSION   "3.0.0"

/* ------------------------------------------------------------------ modes */
enum {
    MODE_SMART = 0,
    MODE_AGGRESSIVE = 1,
    MODE_RISK = 2,
    MODE_NUCLEAR = 3,
    MODE_COUNT = 4
};

extern const char *MODE_KEYS[MODE_COUNT];
extern const char *MODE_LABELS[MODE_COUNT];
extern const char *MODE_DESCRIPTIONS[MODE_COUNT];

/* log levels used for colour / prefixing in the GUI log */
enum {
    LOG_INFO = 0,
    LOG_SCAN = 1,
    LOG_KILL = 2,
    LOG_DRY  = 3,
    LOG_WARN = 4,
    LOG_ERROR = 5
};

/* Custom window messages posted from the engine worker thread to the GUI. */
#define WM_APP_LOG    (WM_APP + 1)   /* wParam = level, lParam = char* (heap)  */
#define WM_APP_STATS  (WM_APP + 2)   /* signal the GUI to refresh its counters */
#define WM_APP_TRAY   (WM_APP + 3)   /* notify-icon callback                    */

/* --------------------------------------------------------------- control IDs */
#define IDC_TAB            1000

/* Dashboard */
#define IDC_TOGGLE         1100
#define IDC_STATUS         1101
#define IDC_RADIO_SMART    1110   /* MODE_SMART..MODE_NUCLEAR are +offset */
#define IDC_MODE_DESC      1120
#define IDC_CHK_DRYRUN     1121
#define IDC_CHK_SERVICES   1122
#define IDC_TRACK          1123
#define IDC_INTERVAL_LBL   1124
#define IDC_STATS          1125
#define IDC_LOG            1126
#define IDC_CLEARLOG       1127
#define IDC_MODE_GROUP     1128
#define IDC_OPT_GROUP      1129
#define IDC_CHK_HEUR       1130
#define IDC_CHK_TRAY       1131
#define IDC_CHK_STARTUP    1132
#define IDC_CHK_NOTIFY     1133
#define IDC_CHK_RESTORE    1134
#define IDC_CHK_RELAUNCH   1135

/* Presets tab: one checkbox per PRESETS[] entry, base + index */
#define IDC_PRESET_BASE    1600

/* Task picker dialog */
#define IDC_PICK_LV        1700
#define IDC_PICK_ADD       1701
#define IDC_PICK_CANCEL    1702

/* Whitelist page */
#define IDC_WL_LIST        1200
#define IDC_WL_EDIT        1201
#define IDC_WL_ADD         1202
#define IDC_WL_REMOVE      1203
#define IDC_WL_INTRO       1204
#define IDC_WL_PICK        1205

/* Blacklist page */
#define IDC_BL_LIST        1300
#define IDC_BL_EDIT        1301
#define IDC_BL_ADD         1302
#define IDC_BL_REMOVE      1303
#define IDC_BL_INTRO       1304
#define IDC_BL_PICK        1305

/* Processes page */
#define IDC_PROC_LIST      1400
#define IDC_PROC_REFRESH   1401
#define IDC_PROC_PREVIEW   1402
#define IDC_PROC_TOWL      1403
#define IDC_PROC_TOBL      1404
#define IDC_PROC_INFO      1405
#define IDC_PROC_KILLSEL   1406

/* Tray menu commands */
#define IDM_TRAY_SHOW      1500
#define IDM_TRAY_TOGGLE    1501
#define IDM_TRAY_MODE      1510   /* +mode index */
#define IDM_TRAY_EXIT      1520

#define TIMER_STATS        1
#define TIMER_NOTIFY       2
#define ID_TRAY            0xA000
#define HOTKEY_TOGGLE      1
#define IDI_APPICON        101

/* ------------------------------------------------------------ small string utils */
/* Lower-case copy of src into dst (size bytes, always NUL-terminated). */
void str_lower_copy(char *dst, size_t size, const char *src);
/* Case-insensitive equality. */
int  str_ieq(const char *a, const char *b);

#endif /* GAMEMODE_H */
