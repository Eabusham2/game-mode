/* gui.c - native Win32 GUI + WinMain.
 *
 * Highlights:
 *   - Owner-drawn green/red master toggle.
 *   - Colour-coded RichEdit activity log.
 *   - ListView process viewer (sortable: Process / PID / RAM / Fate) with
 *     manual "kill selected" and send-to-whitelist/blacklist.
 *   - System-tray integration: minimise to tray, tray menu, coalesced balloon
 *     notifications when closing things in the background.
 *   - Global hotkey (Ctrl+Alt+G) to toggle, "run at startup", "minimise to tray".
 *
 * The engine runs on its own thread and reports back via WM_APP_LOG /
 * WM_APP_STATS so the UI never blocks. */
#include "gamemode.h"
#include "config.h"
#include "engine.h"
#include "known_lists.h"
#include <commctrl.h>
#include <shellapi.h>
#include <richedit.h>
#include <stdlib.h>
#include <string.h>

#define CLIENT_W 820
#define CLIENT_H 632
#define PX 25            /* page content left edge */

static Config   g_cfg;
static Engine   g_engine;
static HWND     g_hwnd;
static HINSTANCE g_hinst;
static HFONT    g_font, g_font_big, g_font_status, g_font_log;
static HICON    g_icon_big, g_icon_small;
static int      g_running_ui;        /* mirrors engine state for the UI */
static int      g_really_quit;       /* distinguishes "Exit" from "minimise"  */
static int      g_tray_added;
static int      g_warned_tray;       /* showed the "still running" hint once   */
static int      g_pending_notify;    /* kills queued for a coalesced balloon    */
static UINT     g_wm_taskbar;         /* "TaskbarCreated" - re-add tray on relaunch */
static NOTIFYICONDATAA g_nid;

/* control handles we need to touch later */
static HWND h_tab, h_toggle, h_status, h_modedesc, h_track, h_interval;
static HWND h_chk_dry, h_chk_svc, h_chk_heur, h_chk_tray, h_chk_startup, h_chk_notify;
static HWND h_chk_restore, h_chk_relaunch;
static HWND h_stats, h_log;
static HWND h_wl_list, h_wl_edit, h_bl_list, h_bl_edit;
static HWND h_proc_list, h_proc_info;
static HWND h_radio[MODE_COUNT];
static HWND h_preset[MAX_PRESETS];

/* task-picker popup state */
static HWND     g_pick_hwnd, g_pick_lv;
static StrList *g_pick_dst;
static HWND     g_pick_dst_list;

/* ---- process table backing store (Processes tab) ------------------------ */
typedef struct { char name[260]; DWORD pid; unsigned long mem; int doomed; } ProcRow;
static ProcRow *g_rows;
static int g_rows_n, g_rows_cap;
static int g_sort_col = 2;     /* default: RAM */
static int g_sort_dir = -1;    /* descending  */

/* ---- page membership (for show/hide) ------------------------------------ */
#define PAGE_COUNT      5
#define PAGE_DASHBOARD  0
#define PAGE_PRESETS    1
#define PAGE_WHITELIST  2
#define PAGE_BLACKLIST  3
#define PAGE_PROCESSES  4

static HWND g_page[PAGE_COUNT][40];
static int  g_page_n[PAGE_COUNT];

static void page_add(int page, HWND h) { g_page[page][g_page_n[page]++] = h; }

static void show_page(int idx)
{
    int p, i;
    for (p = 0; p < PAGE_COUNT; ++p)
        for (i = 0; i < g_page_n[p]; ++i)
            ShowWindow(g_page[p][i], p == idx ? SW_SHOW : SW_HIDE);
}

/* ---- small helpers ------------------------------------------------------- */
static HWND mk(const char *cls, const char *text, DWORD style,
               int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | style,
                             x, y, w, h, g_hwnd, (HMENU)(INT_PTR)id, g_hinst, NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void set_text(HWND h, const char *s) { SetWindowTextA(h, s); }

static COLORREF darken(COLORREF c, double f)
{
    return RGB((int)(GetRValue(c) * f), (int)(GetGValue(c) * f), (int)(GetBValue(c) * f));
}

/* ---- config <-> widgets -------------------------------------------------- */
static void reload_listbox(HWND lb, const StrList *l)
{
    int i;
    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < l->count; ++i)
        SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)l->items[i]);
}

static void save_cfg(void) { config_save(&g_cfg); }

static void update_mode_desc(void) { set_text(h_modedesc, MODE_DESCRIPTIONS[g_cfg.mode]); }

/* ---- run at Windows startup (HKCU ..\Run) -------------------------------- */
static void sync_startup(int enable)
{
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return;
    if (enable) {
        char path[MAX_PATH], q[MAX_PATH + 16];
        GetModuleFileNameA(NULL, path, sizeof(path));
        /* Logon launches start in the tray (when "minimise to tray" is on)
         * instead of popping the window up at every sign-in. */
        wsprintfA(q, "\"%s\" %s", path, ARG_START_IN_TRAY);
        RegSetValueExA(k, APP_NAME, 0, REG_SZ, (const BYTE *)q, (DWORD)strlen(q) + 1);
    } else {
        RegDeleteValueA(k, APP_NAME);
    }
    RegCloseKey(k);
}

/* ---- system tray --------------------------------------------------------- */
static void tray_update_tip(void)
{
    if (!g_tray_added) return;
    wsprintfA(g_nid.szTip, "GameMode - %s [%s]",
              g_running_ui ? "ON" : "off", MODE_KEYS[g_cfg.mode]);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static void tray_add(void)
{
    if (g_tray_added) return;
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = ID_TRAY;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_icon_small ? g_icon_small : g_icon_big;
    lstrcpynA(g_nid.szTip, "GameMode", sizeof(g_nid.szTip));
    g_tray_added = Shell_NotifyIconA(NIM_ADD, &g_nid) ? 1 : 0;
    if (g_tray_added) tray_update_tip();
}

static void tray_remove(void)
{
    if (!g_tray_added) return;
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    g_tray_added = 0;
}

static void tray_balloon(const char *title, const char *msg)
{
    if (!g_tray_added) return;
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_INFO;
    lstrcpynA(g_nid.szInfoTitle, title, sizeof(g_nid.szInfoTitle));
    lstrcpynA(g_nid.szInfo, msg, sizeof(g_nid.szInfo));
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static void show_main_window(void)
{
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

/* ---- master toggle ------------------------------------------------------- */
static void update_status_ui(void)
{
    set_text(h_status, g_running_ui ? "ON" : "OFF");
    set_text(h_toggle, g_running_ui ? "TURN OFF" : "TURN ON");
    InvalidateRect(h_status, NULL, TRUE);
    InvalidateRect(h_toggle, NULL, TRUE);
    tray_update_tip();
}

static void log_append(int level, const char *msg);   /* defined below */

/* When switching live into Nuclear (its arm-time confirmation would otherwise
 * be skipped), warn first. Returns 1 to proceed. */
static int confirm_mode_change(int new_mode)
{
    if (g_engine.running && new_mode == MODE_NUCLEAR && !g_cfg.dry_run) {
        int r = MessageBoxA(g_hwnd,
            "Switching to Nuclear while running closes EVERYTHING except Windows "
            "essentials, anti-cheats, detected games, Discord, GameMode and your "
            "whitelist - including your browser and open documents.\n\n"
            "Switch now?", "Switch to Nuclear mode?", MB_OKCANCEL | MB_ICONWARNING);
        return r == IDOK;
    }
    return 1;
}

static void set_running(int run)
{
    if (run && !g_engine.running) {
        if (g_cfg.mode == MODE_NUCLEAR && !g_cfg.dry_run) {
            int r = MessageBoxA(g_hwnd,
                "Nuclear mode closes EVERYTHING except Windows essentials, "
                "anti-cheats, detected games, Discord, GameMode and your "
                "whitelist - including your browser and open documents.\n\n"
                "Tip: use 'Preview kills' or Dry run first.\n\nArm it now?",
                "Arm Nuclear mode?", MB_OKCANCEL | MB_ICONWARNING);
            if (r != IDOK) return;
        }
        engine_start(&g_engine);
        g_running_ui = g_engine.running ? 1 : 0;   /* thread may fail to start */
        if (!g_running_ui)
            log_append(LOG_ERROR, "Could not start the engine (thread creation failed).");
    } else if (!run && g_engine.running) {
        engine_stop(&g_engine);
        g_running_ui = 0;
    } else {
        g_running_ui = g_engine.running ? 1 : 0;
    }
    g_cfg.enabled_on_start = g_running_ui;
    save_cfg();
    update_status_ui();
}

/* ---- logging (RichEdit, colour-coded) ------------------------------------ */
static const char *level_tag(int level)
{
    switch (level) {
    case LOG_KILL:  return "KILL ";
    case LOG_DRY:   return "DRY  ";
    case LOG_WARN:  return "WARN ";
    case LOG_ERROR: return "ERROR";
    case LOG_SCAN:  return "SCAN ";
    default:        return "INFO ";
    }
}

static COLORREF level_color(int level)
{
    switch (level) {
    case LOG_KILL:  return RGB(0xff, 0x6b, 0x6b);
    case LOG_ERROR: return RGB(0xff, 0x6b, 0x6b);
    case LOG_DRY:   return RGB(0xff, 0xb4, 0x54);
    case LOG_WARN:  return RGB(0xff, 0xb4, 0x54);
    case LOG_SCAN:  return RGB(0x8a, 0x90, 0x9a);
    default:        return RGB(0x4f, 0xd6, 0x8a);
    }
}

static void log_append(int level, const char *msg)
{
    char line[700];
    SYSTEMTIME st;
    CHARRANGE cr;
    CHARFORMAT2A cf;
    int len;

    GetLocalTime(&st);
    wsprintfA(line, "[%02d:%02d:%02d] %s  %s\r\n",
              st.wHour, st.wMinute, st.wSecond, level_tag(level), msg);

    len = GetWindowTextLengthA(h_log);
    if (len > 60000) {                          /* keep the buffer bounded */
        cr.cpMin = 0; cr.cpMax = 20000;
        SendMessageA(h_log, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageA(h_log, EM_REPLACESEL, FALSE, (LPARAM)"");
        len = GetWindowTextLengthA(h_log);
    }
    cr.cpMin = len; cr.cpMax = len;
    SendMessageA(h_log, EM_EXSETSEL, 0, (LPARAM)&cr);

    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = level_color(level);
    SendMessageA(h_log, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SendMessageA(h_log, EM_REPLACESEL, FALSE, (LPARAM)line);
    SendMessageA(h_log, WM_VSCROLL, SB_BOTTOM, 0);
}

/* ---- process table (Processes tab) -------------------------------------- */
static void rows_clear(void) { g_rows_n = 0; }

static void rows_add(const char *name, DWORD pid, unsigned long mem, int doomed)
{
    if (g_rows_n >= g_rows_cap) {
        int ncap = g_rows_cap ? g_rows_cap * 2 : 256;
        ProcRow *nr = (ProcRow *)realloc(g_rows, (size_t)ncap * sizeof(ProcRow));
        if (!nr) return;
        g_rows = nr; g_rows_cap = ncap;
    }
    lstrcpynA(g_rows[g_rows_n].name, name, sizeof(g_rows[0].name));
    g_rows[g_rows_n].pid = pid;
    g_rows[g_rows_n].mem = mem;
    g_rows[g_rows_n].doomed = doomed;
    ++g_rows_n;
}

static int __cdecl cmp_rows(const void *a, const void *b)
{
    const ProcRow *x = (const ProcRow *)a, *y = (const ProcRow *)b;
    int r = 0;
    switch (g_sort_col) {
    case 0: r = lstrcmpiA(x->name, y->name); break;
    case 1: r = (x->pid > y->pid) - (x->pid < y->pid); break;
    case 2: r = (x->mem > y->mem) - (x->mem < y->mem); break;
    case 3: r = x->doomed - y->doomed; break;
    }
    return r * g_sort_dir;
}

static void proc_fill_cb(const char *name, unsigned long pid,
                         unsigned long mem_mb, int doomed, void *user)
{
    (void)user;
    rows_add(name, (DWORD)pid, mem_mb, doomed);
}

static void populate_proc_list(void)
{
    int i, doomed_n = 0;
    char buf[64];
    LVITEMA it;

    qsort(g_rows, (size_t)g_rows_n, sizeof(ProcRow), cmp_rows);
    SendMessageA(h_proc_list, LVM_DELETEALLITEMS, 0, 0);

    for (i = 0; i < g_rows_n; ++i) {
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = i;
        it.pszText = g_rows[i].name;
        it.lParam = i;
        SendMessageA(h_proc_list, LVM_INSERTITEMA, 0, (LPARAM)&it);

        wsprintfA(buf, "%lu", g_rows[i].pid);
        ListView_SetItemText(h_proc_list, i, 1, buf);
        wsprintfA(buf, "%lu", g_rows[i].mem);
        ListView_SetItemText(h_proc_list, i, 2, buf);
        ListView_SetItemText(h_proc_list, i, 3,
                             g_rows[i].doomed ? (char *)"will be CLOSED" : (char *)"kept");
        if (g_rows[i].doomed) ++doomed_n;
    }
    wsprintfA(buf, "%d processes  -  %d would be closed in '%s' mode",
              g_rows_n, doomed_n, MODE_KEYS[g_cfg.mode]);
    set_text(h_proc_info, buf);
}

static void refresh_processes(int preview_only)
{
    rows_clear();
    if (preview_only)
        engine_preview(&g_engine, proc_fill_cb, NULL);
    else
        engine_list_running(&g_engine, proc_fill_cb, NULL);
    populate_proc_list();
}

/* map selected ListView rows back to g_rows and apply an action */
static void for_each_selected(void (*fn)(const ProcRow *))
{
    int item = -1;
    while ((item = (int)SendMessageA(h_proc_list, LVM_GETNEXTITEM, (WPARAM)item,
                                     MAKELPARAM(LVNI_SELECTED, 0))) != -1) {
        if (item >= 0 && item < g_rows_n) fn(&g_rows[item]);
    }
}

static StrList *g_send_dst;
static HWND g_send_list;
static int g_send_added;
static void send_one(const ProcRow *r)
{
    char low[260];
    str_lower_copy(low, sizeof(low), r->name);
    g_send_added += strlist_add(g_send_dst, low);
}
static void send_selected(StrList *dst, HWND dst_list)
{
    g_send_dst = dst; g_send_list = dst_list; g_send_added = 0;
    engine_lock(&g_engine);
    for_each_selected(send_one);
    engine_unlock(&g_engine);
    if (g_send_added) {
        reload_listbox(dst_list, dst);
        save_cfg();
        log_append(LOG_INFO, "Added selected process(es) to list.");
    }
}

static void kill_one(const ProcRow *r) { engine_kill_pid(&g_engine, r->pid, r->name); }
static void kill_selected(void)
{
    for_each_selected(kill_one);
    refresh_processes(0);
}

/* ---- list editing -------------------------------------------------------- */
static void remove_selected(StrList *l, HWND lb)
{
    int n = (int)SendMessageA(lb, LB_GETSELCOUNT, 0, 0);
    int *idx, i;
    char buf[260];
    if (n <= 0) return;
    idx = (int *)malloc((size_t)n * sizeof(int));
    if (!idx) return;
    SendMessageA(lb, LB_GETSELITEMS, (WPARAM)n, (LPARAM)idx);
    engine_lock(&g_engine);
    for (i = n - 1; i >= 0; --i)
        if (SendMessageA(lb, LB_GETTEXT, (WPARAM)idx[i], (LPARAM)buf) != LB_ERR)
            strlist_remove(l, buf);
    engine_unlock(&g_engine);
    free(idx);
    reload_listbox(lb, l);
    save_cfg();
}

static void add_from_edit(StrList *l, HWND edit, HWND lb)
{
    char buf[260];
    int added;
    GetWindowTextA(edit, buf, sizeof(buf));
    engine_lock(&g_engine);
    added = buf[0] && strlist_add(l, buf);
    engine_unlock(&g_engine);
    if (added) {
        reload_listbox(lb, l);
        save_cfg();
    }
    SetWindowTextA(edit, "");
}

/* ---- build the UI -------------------------------------------------------- */
static HWND mk_check(const char *text, int x, int y, int w, int id)
{
    return mk("BUTTON", text, BS_AUTOCHECKBOX, x, y, w, 22, id);
}

static void add_lv_column(int i, const char *title, int w, int fmt)
{
    LVCOLUMNA col;
    ZeroMemory(&col, sizeof(col));
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = fmt;
    col.cx = w;
    col.pszText = (char *)title;
    SendMessageA(h_proc_list, LVM_INSERTCOLUMNA, (WPARAM)i, (LPARAM)&col);
}

static void build_ui(void)
{
    int i;
    TCITEMA tie;
    const char *tabs[PAGE_COUNT] = { "Dashboard", "Presets", "Whitelist",
                                     "Blacklist", "Processes" };

    /* header (always visible) */
    {
        HWND title = mk("STATIC", "\xF0\x9F\x8E\xAE  GameMode", SS_LEFT, 15, 12, 320, 26, -1);
        SendMessageA(title, WM_SETFONT, (WPARAM)g_font_big, TRUE);
        h_status = mk("STATIC", "OFF", SS_RIGHT, CLIENT_W - 180, 14, 160, 24, IDC_STATUS);
        SendMessageA(h_status, WM_SETFONT, (WPARAM)g_font_status, TRUE);
    }

    h_tab = mk(WC_TABCONTROLA, "", 0, 12, 46, CLIENT_W - 24, CLIENT_H - 56, IDC_TAB);
    ShowWindow(h_tab, SW_SHOW);
    tie.mask = TCIF_TEXT;
    for (i = 0; i < PAGE_COUNT; ++i) {
        tie.pszText = (char *)tabs[i];
        SendMessageA(h_tab, TCM_INSERTITEMA, (WPARAM)i, (LPARAM)&tie);
    }

    /* =================== DASHBOARD =================== */
    h_toggle = mk("BUTTON", "TURN ON", BS_OWNERDRAW, PX, 85, 170, 56, IDC_TOGGLE);
    page_add(0, h_toggle);
    page_add(0, mk("STATIC",
        "Master switch - while ON, GameMode continuously closes the targeted "
        "programs every scan. Hotkey: Ctrl+Alt+G.", SS_LEFT, PX + 185, 90, 560, 50, -1));

    page_add(0, mk("BUTTON", "Mode", BS_GROUPBOX, PX, 150, 770, 150, IDC_MODE_GROUP));
    for (i = 0; i < MODE_COUNT; ++i) {
        DWORD st = BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0);
        h_radio[i] = mk("BUTTON", MODE_LABELS[i], st, PX + 15, 175 + i * 27, 330, 24,
                        IDC_RADIO_SMART + i);
        page_add(0, h_radio[i]);
    }
    h_modedesc = mk("STATIC", "", SS_LEFT, PX + 360, 178, 395, 112, IDC_MODE_DESC);
    page_add(0, h_modedesc);

    page_add(0, mk("BUTTON", "Options", BS_GROUPBOX, PX, 306, 770, 150, IDC_OPT_GROUP));
    h_chk_dry     = mk_check("Dry run (report only - close nothing)", PX + 15, 328, 320, IDC_CHK_DRYRUN);
    h_chk_heur    = mk_check("Smart heuristic detection (updaters/telemetry)", PX + 360, 328, 380, IDC_CHK_HEUR);
    h_chk_svc     = mk_check("Stop non-essential services (Risk/Nuclear)", PX + 15, 352, 320, IDC_CHK_SERVICES);
    h_chk_notify  = mk_check("Show tray notifications", PX + 360, 352, 380, IDC_CHK_NOTIFY);
    h_chk_tray    = mk_check("Minimise to tray (keep running in background)", PX + 15, 376, 320, IDC_CHK_TRAY);
    h_chk_startup = mk_check("Run at Windows startup", PX + 360, 376, 380, IDC_CHK_STARTUP);
    page_add(0, h_chk_dry); page_add(0, h_chk_heur); page_add(0, h_chk_svc);
    page_add(0, h_chk_notify); page_add(0, h_chk_tray); page_add(0, h_chk_startup);

    page_add(0, mk("STATIC", "Scan interval:", SS_LEFT, PX + 15, 408, 90, 20, -1));
    h_track = mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_AUTOTICKS, PX + 110, 404, 300, 28, IDC_TRACK);
    page_add(0, h_track);
    SendMessageA(h_track, TBM_SETRANGE, TRUE, MAKELONG(1, 60));
    h_interval = mk("STATIC", "every 3s", SS_LEFT, PX + 420, 408, 120, 20, IDC_INTERVAL_LBL);
    page_add(0, h_interval);

    h_stats = mk("STATIC", "", SS_LEFT, PX, 464, 600, 20, IDC_STATS);
    page_add(0, h_stats);
    page_add(0, mk("BUTTON", "Clear log", BS_PUSHBUTTON, PX + 685, 460, 85, 26, IDC_CLEARLOG));

    h_log = CreateWindowExA(WS_EX_CLIENTEDGE, "RICHEDIT50W", "",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        PX, 488, 770, 116, g_hwnd, (HMENU)(INT_PTR)IDC_LOG, g_hinst, NULL);
    SendMessageA(h_log, WM_SETFONT, (WPARAM)g_font_log, TRUE);
    SendMessageA(h_log, EM_SETBKGNDCOLOR, 0, (LPARAM)RGB(0x12, 0x14, 0x18));
    SendMessageA(h_log, EM_EXLIMITTEXT, 0, (LPARAM)0x100000);
    page_add(0, h_log);

    /* =================== PRESETS =================== */
    page_add(PAGE_PRESETS, mk("BUTTON", "When you turn GameMode OFF",
        BS_GROUPBOX, PX, 82, 770, 92, -1));
    page_add(PAGE_PRESETS, mk("STATIC",
        "GameMode is temporary by design - it only stops/closes things while ON "
        "and never disables anything permanently (no startup changes).",
        SS_LEFT, PX + 15, 104, 740, 32, -1));
    h_chk_restore = mk_check("Restart services I stopped",
                             PX + 15, 142, 360, IDC_CHK_RESTORE);
    h_chk_relaunch = mk_check("Reopen apps I closed (best effort)",
                              PX + 390, 142, 360, IDC_CHK_RELAUNCH);
    page_add(PAGE_PRESETS, h_chk_restore);
    page_add(PAGE_PRESETS, h_chk_relaunch);

    page_add(PAGE_PRESETS, mk("STATIC",
        "Common targets - tick to CLOSE, untick to KEEP (overrides the lists):",
        SS_LEFT, PX, 186, 740, 20, -1));
    for (i = 0; i < PRESET_COUNT && i < MAX_PRESETS; ++i) {
        int col = i % 2, row = i / 2;
        int x = PX + 15 + col * 380;
        int y = 212 + row * 28;
        h_preset[i] = mk_check(PRESETS[i].label, x, y, 365, IDC_PRESET_BASE + i);
        page_add(PAGE_PRESETS, h_preset[i]);
    }

    /* =================== WHITELIST =================== */
    page_add(PAGE_WHITELIST, mk("STATIC",
        "Processes here are NEVER closed, in any mode (in addition to built-in "
        "OS and anti-cheat protections). Use exe names, e.g. mygame.exe",
        SS_LEFT, PX, 85, 770, 40, IDC_WL_INTRO));
    h_wl_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
        PX, 130, 770, 350, g_hwnd, (HMENU)(INT_PTR)IDC_WL_LIST, g_hinst, NULL);
    SendMessageA(h_wl_list, WM_SETFONT, (WPARAM)g_font, TRUE);
    page_add(PAGE_WHITELIST, h_wl_list);
    h_wl_edit = mk("EDIT", "", ES_AUTOHSCROLL | WS_BORDER, PX, 490, 350, 26, IDC_WL_EDIT);
    page_add(PAGE_WHITELIST, h_wl_edit);
    page_add(PAGE_WHITELIST, mk("BUTTON", "Add", BS_PUSHBUTTON, PX + 360, 489, 80, 28, IDC_WL_ADD));
    page_add(PAGE_WHITELIST, mk("BUTTON", "Pick from running tasks", BS_PUSHBUTTON, PX + 450, 489, 180, 28, IDC_WL_PICK));
    page_add(PAGE_WHITELIST, mk("BUTTON", "Remove", BS_PUSHBUTTON, PX + 640, 489, 130, 28, IDC_WL_REMOVE));

    /* =================== BLACKLIST =================== */
    page_add(PAGE_BLACKLIST, mk("STATIC",
        "Processes here are ALWAYS closed while GameMode is ON, in every mode "
        "(critical OS processes stay protected for safety).",
        SS_LEFT, PX, 85, 770, 40, IDC_BL_INTRO));
    h_bl_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
        PX, 130, 770, 350, g_hwnd, (HMENU)(INT_PTR)IDC_BL_LIST, g_hinst, NULL);
    SendMessageA(h_bl_list, WM_SETFONT, (WPARAM)g_font, TRUE);
    page_add(PAGE_BLACKLIST, h_bl_list);
    h_bl_edit = mk("EDIT", "", ES_AUTOHSCROLL | WS_BORDER, PX, 490, 350, 26, IDC_BL_EDIT);
    page_add(PAGE_BLACKLIST, h_bl_edit);
    page_add(PAGE_BLACKLIST, mk("BUTTON", "Add", BS_PUSHBUTTON, PX + 360, 489, 80, 28, IDC_BL_ADD));
    page_add(PAGE_BLACKLIST, mk("BUTTON", "Pick from running tasks", BS_PUSHBUTTON, PX + 450, 489, 180, 28, IDC_BL_PICK));
    page_add(PAGE_BLACKLIST, mk("BUTTON", "Remove", BS_PUSHBUTTON, PX + 640, 489, 130, 28, IDC_BL_REMOVE));

    /* =================== PROCESSES =================== */
    page_add(PAGE_PROCESSES, mk("BUTTON", "Refresh", BS_PUSHBUTTON, PX, 85, 110, 28, IDC_PROC_REFRESH));
    page_add(PAGE_PROCESSES, mk("BUTTON", "Preview kills", BS_PUSHBUTTON, PX + 120, 85, 120, 28, IDC_PROC_PREVIEW));
    page_add(PAGE_PROCESSES, mk("BUTTON", "Kill selected", BS_PUSHBUTTON, PX + 250, 85, 120, 28, IDC_PROC_KILLSEL));
    page_add(PAGE_PROCESSES, mk("BUTTON", "-> Whitelist", BS_PUSHBUTTON, PX + 380, 85, 120, 28, IDC_PROC_TOWL));
    page_add(PAGE_PROCESSES, mk("BUTTON", "-> Blacklist", BS_PUSHBUTTON, PX + 510, 85, 120, 28, IDC_PROC_TOBL));
    h_proc_info = mk("STATIC", "", SS_LEFT, PX, 120, 770, 20, IDC_PROC_INFO);
    page_add(PAGE_PROCESSES, h_proc_info);

    h_proc_list = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
        WS_CHILD | LVS_REPORT | LVS_SHOWSELALWAYS,
        PX, 145, 770, 460, g_hwnd, (HMENU)(INT_PTR)IDC_PROC_LIST, g_hinst, NULL);
    SendMessageA(h_proc_list, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageA(h_proc_list, LVM_SETEXTENDEDLISTVIEWSTYLE,
                 0, (LPARAM)(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES));
    add_lv_column(0, "Process", 300, LVCFMT_LEFT);
    add_lv_column(1, "PID", 70, LVCFMT_RIGHT);
    add_lv_column(2, "RAM (MB)", 90, LVCFMT_RIGHT);
    add_lv_column(3, "Under current mode", 200, LVCFMT_LEFT);
    page_add(PAGE_PROCESSES, h_proc_list);
}

/* ---- apply persisted settings to widgets -------------------------------- */
static void apply_cfg_to_ui(void)
{
    int iv;
    char b[32];
    SendMessageA(h_radio[g_cfg.mode], BM_SETCHECK, BST_CHECKED, 0);
    update_mode_desc();
    SendMessageA(h_chk_dry, BM_SETCHECK, g_cfg.dry_run ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_svc, BM_SETCHECK, g_cfg.manage_services ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_heur, BM_SETCHECK, g_cfg.heuristics ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_tray, BM_SETCHECK, g_cfg.minimize_to_tray ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_startup, BM_SETCHECK, g_cfg.run_at_startup ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_notify, BM_SETCHECK, g_cfg.notifications ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_restore, BM_SETCHECK, g_cfg.restore_services ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_relaunch, BM_SETCHECK, g_cfg.relaunch_apps ? BST_CHECKED : BST_UNCHECKED, 0);
    {
        int i;
        for (i = 0; i < PRESET_COUNT && i < MAX_PRESETS; ++i)
            SendMessageA(h_preset[i], BM_SETCHECK,
                         g_cfg.presets[i] ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    iv = (int)(g_cfg.scan_interval + 0.5);
    SendMessageA(h_track, TBM_SETPOS, TRUE, iv);
    wsprintfA(b, "every %ds", iv);
    set_text(h_interval, b);
    reload_listbox(h_wl_list, &g_cfg.whitelist);
    reload_listbox(h_bl_list, &g_cfg.blacklist);
    update_status_ui();
}

static void update_stats_label(void)
{
    Stats s; char buf[256];
    engine_get_stats(&g_engine, &s);
    wsprintfA(buf,
        "Engine %s   scans: %ld   closed: %ld   RAM freed: %ld MB   "
        "services: %ld   last targets: %ld",
        g_engine.running ? "RUNNING" : "stopped",
        s.scans, s.killed, s.ram_freed_mb, s.services_stopped, s.last_targets);
    set_text(h_stats, buf);
}

/* ---- tray context menu --------------------------------------------------- */
static void show_tray_menu(void)
{
    HMENU menu = CreatePopupMenu();
    HMENU modes = CreatePopupMenu();
    POINT pt;
    int i;
    for (i = 0; i < MODE_COUNT; ++i)
        AppendMenuA(modes, MF_STRING | (i == g_cfg.mode ? MF_CHECKED : 0),
                    IDM_TRAY_MODE + i, MODE_LABELS[i]);
    AppendMenuA(menu, MF_STRING, IDM_TRAY_SHOW, "Show GameMode");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_TRAY_TOGGLE,
                g_running_ui ? "Turn OFF" : "Turn ON");
    AppendMenuA(menu, MF_POPUP, (UINT_PTR)modes, "Mode");
    AppendMenuA(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(menu, MF_STRING, IDM_TRAY_EXIT, "Exit");

    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageA(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

/* ---- task picker popup --------------------------------------------------- */
static void pick_fill_cb(const char *name, unsigned long pid,
                         unsigned long mem_mb, int doomed, void *user)
{
    (void)doomed; (void)user;
    rows_add(name, (DWORD)pid, mem_mb, 0);
}

static void picker_populate(void)
{
    int i, sc = g_sort_col, sd = g_sort_dir;
    char buf[64];
    LVITEMA it;

    rows_clear();
    engine_list_running(&g_engine, pick_fill_cb, NULL);
    g_sort_col = 0; g_sort_dir = 1;                 /* sort by name for picking */
    qsort(g_rows, (size_t)g_rows_n, sizeof(ProcRow), cmp_rows);
    g_sort_col = sc; g_sort_dir = sd;

    SendMessageA(g_pick_lv, LVM_DELETEALLITEMS, 0, 0);
    for (i = 0; i < g_rows_n; ++i) {
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_TEXT; it.iItem = i; it.pszText = g_rows[i].name;
        SendMessageA(g_pick_lv, LVM_INSERTITEMA, 0, (LPARAM)&it);
        wsprintfA(buf, "%lu", g_rows[i].pid);
        ListView_SetItemText(g_pick_lv, i, 1, buf);
        wsprintfA(buf, "%lu", g_rows[i].mem);
        ListView_SetItemText(g_pick_lv, i, 2, buf);
    }
}

static void close_picker(void)
{
    EnableWindow(g_hwnd, TRUE);
    if (g_pick_hwnd) {
        HWND h = g_pick_hwnd;
        g_pick_hwnd = NULL; g_pick_lv = NULL;
        DestroyWindow(h);
    }
    SetForegroundWindow(g_hwnd);
}

static LRESULT CALLBACK PickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDCANCEL) {           /* Esc, via IsDialogMessage */
            close_picker();
            return 0;
        }
        if (id == IDC_PICK_ADD) {
            int n = (int)SendMessageA(g_pick_lv, LVM_GETITEMCOUNT, 0, 0);
            int i, added = 0;
            char nm[260];
            engine_lock(&g_engine);
            for (i = 0; i < n; ++i) {
                if (ListView_GetCheckState(g_pick_lv, i)) {
                    ListView_GetItemText(g_pick_lv, i, 0, nm, (int)sizeof(nm));
                    added += strlist_add(g_pick_dst, nm);
                }
            }
            engine_unlock(&g_engine);
            if (added) {
                reload_listbox(g_pick_dst_list, g_pick_dst);
                save_cfg();
                log_append(LOG_INFO, "Added picked task(s) to list.");
            }
            close_picker();
        } else if (id == IDC_PICK_CANCEL) {
            close_picker();
        }
        return 0;
    }
    case WM_CLOSE:
        close_picker();
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void open_picker(StrList *dst, HWND dst_list, const char *title)
{
    RECT rc;
    int w = 560, h = 470, x, y;
    LVCOLUMNA c;
    HWND s, b1, b2;

    if (g_pick_hwnd) return;
    g_pick_dst = dst;
    g_pick_dst_list = dst_list;

    GetWindowRect(g_hwnd, &rc);
    x = rc.left + ((rc.right - rc.left) - w) / 2;
    y = rc.top + ((rc.bottom - rc.top) - h) / 2;
    g_pick_hwnd = CreateWindowExA(WS_EX_DLGMODALFRAME, "GameModePicker", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h, g_hwnd, NULL, g_hinst, NULL);
    if (!g_pick_hwnd) return;

    s = CreateWindowExA(0, "STATIC",
        "Tick the running tasks you want, then click Add checked:",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 12, 10, 520, 20, g_pick_hwnd, NULL, g_hinst, NULL);
    SendMessageA(s, WM_SETFONT, (WPARAM)g_font, TRUE);

    g_pick_lv = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        12, 36, 520, 350, g_pick_hwnd, (HMENU)(INT_PTR)IDC_PICK_LV, g_hinst, NULL);
    SendMessageA(g_pick_lv, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageA(g_pick_lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 (LPARAM)(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES));
    ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    c.fmt = LVCFMT_LEFT;  c.cx = 300; c.pszText = (char *)"Process";
    SendMessageA(g_pick_lv, LVM_INSERTCOLUMNA, 0, (LPARAM)&c);
    c.fmt = LVCFMT_RIGHT; c.cx = 80;  c.pszText = (char *)"PID";
    SendMessageA(g_pick_lv, LVM_INSERTCOLUMNA, 1, (LPARAM)&c);
    c.cx = 90; c.pszText = (char *)"RAM (MB)";
    SendMessageA(g_pick_lv, LVM_INSERTCOLUMNA, 2, (LPARAM)&c);

    b1 = CreateWindowExA(0, "BUTTON", "Add checked",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
        300, 398, 110, 30, g_pick_hwnd, (HMENU)(INT_PTR)IDC_PICK_ADD, g_hinst, NULL);
    SendMessageA(b1, WM_SETFONT, (WPARAM)g_font, TRUE);
    b2 = CreateWindowExA(0, "BUTTON", "Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        420, 398, 110, 30, g_pick_hwnd, (HMENU)(INT_PTR)IDC_PICK_CANCEL, g_hinst, NULL);
    SendMessageA(b2, WM_SETFONT, (WPARAM)g_font, TRUE);

    picker_populate();
    EnableWindow(g_hwnd, FALSE);
    ShowWindow(g_pick_hwnd, SW_SHOW);
    SetForegroundWindow(g_pick_hwnd);
}

/* ---- window procedure ---------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    /* Explorer restarted (or wasn't ready at launch): rebuild the tray icon. */
    if (msg == g_wm_taskbar && g_wm_taskbar) {
        g_tray_added = 0;
        tray_add();
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        build_ui();
        engine_init(&g_engine, &g_cfg, hwnd);
        apply_cfg_to_ui();
        show_page(0);
        g_wm_taskbar = RegisterWindowMessageA("TaskbarCreated");
        tray_add();
        if (!RegisterHotKey(hwnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT, 'G'))
            log_append(LOG_WARN,
                "Ctrl+Alt+G hotkey unavailable (another app owns it).");
        SetTimer(hwnd, TIMER_STATS, 1000, NULL);
        if (g_cfg.run_at_startup) sync_startup(1);
        if (g_cfg.enabled_on_start) set_running(1);
        update_stats_label();
        return 0;

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
        if (dis->CtlID == IDC_TOGGLE) {
            COLORREF base = g_running_ui ? RGB(0xd9, 0x45, 0x45) : RGB(0x2f, 0xb8, 0x68);
            HBRUSH br, ob; HPEN pen, op; HFONT of; char txt[32];
            if (dis->itemState & ODS_SELECTED) base = darken(base, 0.85);
            br = CreateSolidBrush(base);
            pen = CreatePen(PS_SOLID, 1, darken(base, 0.7));
            ob = (HBRUSH)SelectObject(dis->hDC, br);
            op = (HPEN)SelectObject(dis->hDC, pen);
            RoundRect(dis->hDC, dis->rcItem.left, dis->rcItem.top,
                      dis->rcItem.right, dis->rcItem.bottom, 16, 16);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            of = (HFONT)SelectObject(dis->hDC, g_font_big);
            GetWindowTextA(dis->hwndItem, txt, sizeof(txt));
            DrawTextA(dis->hDC, txt, -1, &dis->rcItem,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dis->hDC, of);
            SelectObject(dis->hDC, op); SelectObject(dis->hDC, ob);
            DeleteObject(br); DeleteObject(pen);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDC_TOGGLE && code == BN_CLICKED) {
            set_running(!g_engine.running);
        } else if (id >= IDC_RADIO_SMART && id < IDC_RADIO_SMART + MODE_COUNT && code == BN_CLICKED) {
            int nm = id - IDC_RADIO_SMART;
            if (nm != g_cfg.mode) {
                if (!confirm_mode_change(nm)) {
                    /* user cancelled: revert the radio to the active mode */
                    CheckRadioButton(hwnd, IDC_RADIO_SMART,
                                     IDC_RADIO_SMART + MODE_COUNT - 1,
                                     IDC_RADIO_SMART + g_cfg.mode);
                    return 0;
                }
                g_cfg.mode = nm;
                update_mode_desc(); save_cfg(); tray_update_tip();
                log_append(LOG_INFO, "Mode changed.");
            }
        } else if (id == IDC_CHK_DRYRUN && code == BN_CLICKED) {
            g_cfg.dry_run = (int)SendMessageA(h_chk_dry, BM_GETCHECK, 0, 0) == BST_CHECKED; save_cfg();
        } else if (id == IDC_CHK_SERVICES && code == BN_CLICKED) {
            g_cfg.manage_services = (int)SendMessageA(h_chk_svc, BM_GETCHECK, 0, 0) == BST_CHECKED; save_cfg();
        } else if (id == IDC_CHK_HEUR && code == BN_CLICKED) {
            g_cfg.heuristics = (int)SendMessageA(h_chk_heur, BM_GETCHECK, 0, 0) == BST_CHECKED; save_cfg();
        } else if (id == IDC_CHK_TRAY && code == BN_CLICKED) {
            g_cfg.minimize_to_tray = (int)SendMessageA(h_chk_tray, BM_GETCHECK, 0, 0) == BST_CHECKED; save_cfg();
        } else if (id == IDC_CHK_NOTIFY && code == BN_CLICKED) {
            g_cfg.notifications = (int)SendMessageA(h_chk_notify, BM_GETCHECK, 0, 0) == BST_CHECKED; save_cfg();
        } else if (id == IDC_CHK_STARTUP && code == BN_CLICKED) {
            g_cfg.run_at_startup = (int)SendMessageA(h_chk_startup, BM_GETCHECK, 0, 0) == BST_CHECKED;
            sync_startup(g_cfg.run_at_startup); save_cfg();
        } else if (id == IDC_CHK_RESTORE && code == BN_CLICKED) {
            g_cfg.restore_services = (int)SendMessageA(h_chk_restore, BM_GETCHECK, 0, 0) == BST_CHECKED; save_cfg();
        } else if (id == IDC_CHK_RELAUNCH && code == BN_CLICKED) {
            g_cfg.relaunch_apps = (int)SendMessageA(h_chk_relaunch, BM_GETCHECK, 0, 0) == BST_CHECKED; save_cfg();
        } else if (id >= IDC_PRESET_BASE && id < IDC_PRESET_BASE + PRESET_COUNT && code == BN_CLICKED) {
            int pi = id - IDC_PRESET_BASE;
            g_cfg.presets[pi] = (int)SendMessageA(h_preset[pi], BM_GETCHECK, 0, 0) == BST_CHECKED;
            save_cfg();
        } else if (id == IDC_CLEARLOG && code == BN_CLICKED) {
            SetWindowTextA(h_log, "");
        } else if (id == IDC_WL_ADD && code == BN_CLICKED) {
            add_from_edit(&g_cfg.whitelist, h_wl_edit, h_wl_list);
        } else if (id == IDC_WL_REMOVE && code == BN_CLICKED) {
            remove_selected(&g_cfg.whitelist, h_wl_list);
        } else if (id == IDC_WL_PICK && code == BN_CLICKED) {
            open_picker(&g_cfg.whitelist, h_wl_list, "Pick tasks to WHITELIST (never close)");
        } else if (id == IDC_BL_ADD && code == BN_CLICKED) {
            add_from_edit(&g_cfg.blacklist, h_bl_edit, h_bl_list);
        } else if (id == IDC_BL_REMOVE && code == BN_CLICKED) {
            remove_selected(&g_cfg.blacklist, h_bl_list);
        } else if (id == IDC_BL_PICK && code == BN_CLICKED) {
            open_picker(&g_cfg.blacklist, h_bl_list, "Pick tasks to BLACKLIST (always close)");
        } else if (id == IDC_PROC_REFRESH && code == BN_CLICKED) {
            refresh_processes(0);
        } else if (id == IDC_PROC_PREVIEW && code == BN_CLICKED) {
            refresh_processes(1);
        } else if (id == IDC_PROC_KILLSEL && code == BN_CLICKED) {
            kill_selected();
        } else if (id == IDC_PROC_TOWL && code == BN_CLICKED) {
            send_selected(&g_cfg.whitelist, h_wl_list);
        } else if (id == IDC_PROC_TOBL && code == BN_CLICKED) {
            send_selected(&g_cfg.blacklist, h_bl_list);
        } else if (id == IDM_TRAY_SHOW) {
            show_main_window();
        } else if (id == IDM_TRAY_TOGGLE) {
            set_running(!g_engine.running);
        } else if (id == IDM_TRAY_EXIT) {
            /* route through WM_CLOSE so engine_stop + save_cfg run first */
            g_really_quit = 1; SendMessageA(hwnd, WM_CLOSE, 0, 0);
        } else if (id >= IDM_TRAY_MODE && id < IDM_TRAY_MODE + MODE_COUNT) {
            int nm = id - IDM_TRAY_MODE;
            if (nm != g_cfg.mode && confirm_mode_change(nm)) {
                g_cfg.mode = nm;
                CheckRadioButton(hwnd, IDC_RADIO_SMART,
                                 IDC_RADIO_SMART + MODE_COUNT - 1,
                                 IDC_RADIO_SMART + g_cfg.mode);
                update_mode_desc(); save_cfg(); tray_update_tip();
            }
        }
        return 0;
    }

    case WM_HOTKEY:
        /* ignore while the modal picker is up (it disables the main window) */
        if (wp == HOTKEY_TOGGLE && !g_pick_hwnd) set_running(!g_engine.running);
        return 0;

    case WM_HSCROLL:
        if ((HWND)lp == h_track) {
            int pos = (int)SendMessageA(h_track, TBM_GETPOS, 0, 0);
            char b[32];
            if (pos < 1) pos = 1;
            g_cfg.scan_interval = pos;
            wsprintfA(b, "every %ds", pos);
            set_text(h_interval, b);
            save_cfg();
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lp;
        if (nm->idFrom == IDC_TAB && nm->code == (UINT)TCN_SELCHANGE) {
            int sel = (int)SendMessageA(h_tab, TCM_GETCURSEL, 0, 0);
            show_page(sel);
            if (sel == PAGE_PROCESSES) refresh_processes(0);
        } else if (nm->idFrom == IDC_PROC_LIST && nm->code == (UINT)LVN_COLUMNCLICK) {
            LPNMLISTVIEW lv = (LPNMLISTVIEW)lp;
            if (lv->iSubItem == g_sort_col) g_sort_dir = -g_sort_dir;
            else { g_sort_col = lv->iSubItem; g_sort_dir = (g_sort_col == 2) ? -1 : 1; }
            populate_proc_list();
        } else if (nm->idFrom == IDC_PROC_LIST && nm->code == (UINT)NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW cd = (LPNMLVCUSTOMDRAW)lp;
            switch (cd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                return CDRF_NOTIFYITEMDRAW;
            case CDDS_ITEMPREPAINT: {
                int idx = (int)cd->nmcd.dwItemSpec;
                if (idx >= 0 && idx < g_rows_n && g_rows[idx].doomed)
                    cd->clrText = RGB(0xc0, 0x30, 0x30);
                return CDRF_DODEFAULT;
            }
            }
            return CDRF_DODEFAULT;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == h_status) {
            HDC dc = (HDC)wp;
            SetTextColor(dc, g_running_ui ? RGB(0x1a, 0xa3, 0x4a) : RGB(0x80, 0x80, 0x80));
            SetBkMode(dc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;

    case WM_APP_LOG: {
        char *s = (char *)lp;
        int level = (int)wp;
        if (s) {
            log_append(level, s);
            if (level == LOG_KILL && g_cfg.notifications && !IsWindowVisible(hwnd)) {
                ++g_pending_notify;
                SetTimer(hwnd, TIMER_NOTIFY, 1500, NULL);
            }
            free(s);
        }
        return 0;
    }

    case WM_APP_STATS:
        update_stats_label();
        return 0;

    case WM_APP_TRAY:
        if (g_pick_hwnd) {
            /* the modal picker owns the UI: bring it forward instead of
             * popping a menu over a disabled main window */
            SetForegroundWindow(g_pick_hwnd);
            return 0;
        }
        if (lp == WM_LBUTTONDBLCLK) show_main_window();
        else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) show_tray_menu();
        return 0;

    case WM_TIMER:
        if (wp == TIMER_STATS) {
            update_stats_label();
        } else if (wp == TIMER_NOTIFY) {
            KillTimer(hwnd, TIMER_NOTIFY);
            if (g_pending_notify > 0) {
                char b[96];
                wsprintfA(b, "Closed %d background item(s).", g_pending_notify);
                tray_balloon("GameMode", b);
                g_pending_notify = 0;
            }
        }
        return 0;

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED && g_cfg.minimize_to_tray) {
            ShowWindow(hwnd, SW_HIDE);
            if (!g_warned_tray) {
                tray_balloon("GameMode", "Still running in the tray. "
                             "Right-click the tray icon for options.");
                g_warned_tray = 1;
            }
        }
        return 0;

    case WM_CLOSE:
        if (g_cfg.minimize_to_tray && !g_really_quit) {
            ShowWindow(hwnd, SW_HIDE);
            if (!g_warned_tray) {
                tray_balloon("GameMode", "Still running in the tray. "
                             "Use the tray menu to exit.");
                g_warned_tray = 1;
            }
            return 0;
        }
        engine_stop(&g_engine);
        save_cfg();
        DestroyWindow(hwnd);
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        /* logoff/shutdown: undo temporary changes (restart services, etc.)
         * before the process is torn down. */
        if (wp) {
            engine_stop(&g_engine);
            save_cfg();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_STATS);
        UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
        tray_remove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ---- entry point --------------------------------------------------------- */
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show)
{
    WNDCLASSEXA wc;
    INITCOMMONCONTROLSEX icc;
    HWND hwnd;
    MSG m;
    RECT r;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    HANDLE instance_mutex;
    int start_in_tray;

    (void)hPrev;
    g_hinst = hInst;

    /* One GameMode at a time: with "run at startup" on, a manual launch
     * would otherwise start a second engine scanning alongside the first.
     * Hand the existing instance the focus instead. */
    instance_mutex = CreateMutexA(NULL, FALSE, APP_MUTEX_NAME);
    if (instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowA(APP_WNDCLASS, NULL);
        if (prev) {
            ShowWindow(prev, SW_SHOW);
            ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        CloseHandle(instance_mutex);
        return 0;
    }
    start_in_tray = (cmd && strstr(cmd, ARG_START_IN_TRAY) != NULL);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);
    LoadLibraryA("Msftedit.dll");   /* registers the RichEdit window class */

    config_load(&g_cfg);

    g_icon_big   = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(IDI_APPICON),
                                     IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    g_icon_small = (HICON)LoadImageA(hInst, MAKEINTRESOURCEA(IDI_APPICON),
                                     IMAGE_ICON, 16, 16, LR_SHARED);

    g_font        = CreateFontA(-15, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,
                                0,0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_big    = CreateFontA(-22, 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,
                                0,0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_status = CreateFontA(-20, 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,
                                0,0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_log    = CreateFontA(-13, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,
                                0,0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = APP_WNDCLASS;
    wc.hIcon = g_icon_big ? g_icon_big : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = g_icon_small ? g_icon_small : wc.hIcon;
    if (!RegisterClassExA(&wc)) return 1;

    /* task-picker popup class (shares fonts/icon, own window proc) */
    wc.lpfnWndProc = PickerProc;
    wc.lpszClassName = "GameModePicker";
    RegisterClassExA(&wc);

    r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
    AdjustWindowRect(&r, style, FALSE);

    hwnd = CreateWindowExA(0, wc.lpszClassName, APP_TITLE, style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top,
                           NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    if (g_icon_big)   SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_icon_big);
    if (g_icon_small) SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon_small);
    if (start_in_tray && g_cfg.minimize_to_tray) {
        /* launched at logon: live in the tray until the user asks for us */
        ShowWindow(hwnd, SW_HIDE);
        tray_balloon("GameMode", "Started in the tray. Double-click the icon "
                     "to open, right-click for options.");
        g_warned_tray = 1;
    } else {
        ShowWindow(hwnd, show);
        UpdateWindow(hwnd);
    }

    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (g_pick_hwnd && IsDialogMessageA(g_pick_hwnd, &m)) continue;
        if (IsDialogMessageA(g_hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }

    /* free any WM_APP_LOG heap strings still queued when the window closed */
    {
        MSG dm;
        while (PeekMessageA(&dm, NULL, WM_APP_LOG, WM_APP_LOG, PM_REMOVE))
            free((char *)dm.lParam);
    }

    engine_destroy(&g_engine);
    config_free(&g_cfg);
    free(g_rows);
    DeleteObject(g_font); DeleteObject(g_font_big);
    DeleteObject(g_font_status); DeleteObject(g_font_log);
    if (instance_mutex) CloseHandle(instance_mutex);
    return (int)m.wParam;
}
