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
static NOTIFYICONDATAA g_nid;

/* control handles we need to touch later */
static HWND h_tab, h_toggle, h_status, h_modedesc, h_track, h_interval;
static HWND h_chk_dry, h_chk_svc, h_chk_heur, h_chk_tray, h_chk_startup, h_chk_notify;
static HWND h_stats, h_log;
static HWND h_wl_list, h_wl_edit, h_bl_list, h_bl_edit;
static HWND h_proc_list, h_proc_info;
static HWND h_radio[MODE_COUNT];

/* ---- process table backing store (Processes tab) ------------------------ */
typedef struct { char name[260]; DWORD pid; unsigned long mem; int doomed; } ProcRow;
static ProcRow *g_rows;
static int g_rows_n, g_rows_cap;
static int g_sort_col = 2;     /* default: RAM */
static int g_sort_dir = -1;    /* descending  */

/* ---- page membership (for show/hide) ------------------------------------ */
static HWND g_page[4][28];
static int  g_page_n[4];

static void page_add(int page, HWND h) { g_page[page][g_page_n[page]++] = h; }

static void show_page(int idx)
{
    int p, i;
    for (p = 0; p < 4; ++p)
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
        char path[MAX_PATH], q[MAX_PATH + 4];
        GetModuleFileNameA(NULL, path, sizeof(path));
        wsprintfA(q, "\"%s\"", path);
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
    Shell_NotifyIconA(NIM_ADD, &g_nid);
    g_tray_added = 1;
    tray_update_tip();
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
        g_running_ui = 1;
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
    for_each_selected(send_one);
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
    for (i = n - 1; i >= 0; --i)
        if (SendMessageA(lb, LB_GETTEXT, (WPARAM)idx[i], (LPARAM)buf) != LB_ERR)
            strlist_remove(l, buf);
    free(idx);
    reload_listbox(lb, l);
    save_cfg();
}

static void add_from_edit(StrList *l, HWND edit, HWND lb)
{
    char buf[260];
    GetWindowTextA(edit, buf, sizeof(buf));
    if (buf[0] && strlist_add(l, buf)) {
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
    const char *tabs[4] = { "Dashboard", "Whitelist", "Blacklist", "Processes" };

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
    for (i = 0; i < 4; ++i) {
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
    SendMessageA(h_track, TBM_SETRANGE, TRUE, MAKELONG(1, 30));
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

    /* =================== WHITELIST =================== */
    page_add(1, mk("STATIC",
        "Processes here are NEVER closed, in any mode (in addition to built-in "
        "OS and anti-cheat protections). Use exe names, e.g. mygame.exe",
        SS_LEFT, PX, 85, 770, 40, IDC_WL_INTRO));
    h_wl_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
        PX, 130, 770, 350, g_hwnd, (HMENU)(INT_PTR)IDC_WL_LIST, g_hinst, NULL);
    SendMessageA(h_wl_list, WM_SETFONT, (WPARAM)g_font, TRUE);
    page_add(1, h_wl_list);
    h_wl_edit = mk("EDIT", "", ES_AUTOHSCROLL | WS_BORDER, PX, 490, 500, 26, IDC_WL_EDIT);
    page_add(1, h_wl_edit);
    page_add(1, mk("BUTTON", "Add", BS_PUSHBUTTON, PX + 510, 489, 110, 28, IDC_WL_ADD));
    page_add(1, mk("BUTTON", "Remove selected", BS_PUSHBUTTON, PX + 630, 489, 140, 28, IDC_WL_REMOVE));

    /* =================== BLACKLIST =================== */
    page_add(2, mk("STATIC",
        "Processes here are ALWAYS closed while GameMode is ON, in every mode "
        "(critical OS processes stay protected for safety).",
        SS_LEFT, PX, 85, 770, 40, IDC_BL_INTRO));
    h_bl_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
        PX, 130, 770, 350, g_hwnd, (HMENU)(INT_PTR)IDC_BL_LIST, g_hinst, NULL);
    SendMessageA(h_bl_list, WM_SETFONT, (WPARAM)g_font, TRUE);
    page_add(2, h_bl_list);
    h_bl_edit = mk("EDIT", "", ES_AUTOHSCROLL | WS_BORDER, PX, 490, 500, 26, IDC_BL_EDIT);
    page_add(2, h_bl_edit);
    page_add(2, mk("BUTTON", "Add", BS_PUSHBUTTON, PX + 510, 489, 110, 28, IDC_BL_ADD));
    page_add(2, mk("BUTTON", "Remove selected", BS_PUSHBUTTON, PX + 630, 489, 140, 28, IDC_BL_REMOVE));

    /* =================== PROCESSES =================== */
    page_add(3, mk("BUTTON", "Refresh", BS_PUSHBUTTON, PX, 85, 110, 28, IDC_PROC_REFRESH));
    page_add(3, mk("BUTTON", "Preview kills", BS_PUSHBUTTON, PX + 120, 85, 120, 28, IDC_PROC_PREVIEW));
    page_add(3, mk("BUTTON", "Kill selected", BS_PUSHBUTTON, PX + 250, 85, 120, 28, IDC_PROC_KILLSEL));
    page_add(3, mk("BUTTON", "-> Whitelist", BS_PUSHBUTTON, PX + 380, 85, 120, 28, IDC_PROC_TOWL));
    page_add(3, mk("BUTTON", "-> Blacklist", BS_PUSHBUTTON, PX + 510, 85, 120, 28, IDC_PROC_TOBL));
    h_proc_info = mk("STATIC", "", SS_LEFT, PX, 120, 770, 20, IDC_PROC_INFO);
    page_add(3, h_proc_info);

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
    page_add(3, h_proc_list);
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

/* ---- window procedure ---------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        build_ui();
        engine_init(&g_engine, &g_cfg, hwnd);
        apply_cfg_to_ui();
        show_page(0);
        tray_add();
        RegisterHotKey(hwnd, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT, 'G');
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
            g_cfg.mode = id - IDC_RADIO_SMART;
            update_mode_desc(); save_cfg(); tray_update_tip();
            log_append(LOG_INFO, "Mode changed.");
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
        } else if (id == IDC_CLEARLOG && code == BN_CLICKED) {
            SetWindowTextA(h_log, "");
        } else if (id == IDC_WL_ADD && code == BN_CLICKED) {
            add_from_edit(&g_cfg.whitelist, h_wl_edit, h_wl_list);
        } else if (id == IDC_WL_REMOVE && code == BN_CLICKED) {
            remove_selected(&g_cfg.whitelist, h_wl_list);
        } else if (id == IDC_BL_ADD && code == BN_CLICKED) {
            add_from_edit(&g_cfg.blacklist, h_bl_edit, h_bl_list);
        } else if (id == IDC_BL_REMOVE && code == BN_CLICKED) {
            remove_selected(&g_cfg.blacklist, h_bl_list);
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
            g_really_quit = 1; DestroyWindow(hwnd);
        } else if (id >= IDM_TRAY_MODE && id < IDM_TRAY_MODE + MODE_COUNT) {
            g_cfg.mode = id - IDM_TRAY_MODE;
            SendMessageA(h_radio[g_cfg.mode], BM_SETCHECK, BST_CHECKED, 0);
            update_mode_desc(); save_cfg(); tray_update_tip();
        }
        return 0;
    }

    case WM_HOTKEY:
        if (wp == HOTKEY_TOGGLE) set_running(!g_engine.running);
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
            if (sel == 3) refresh_processes(0);
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

    (void)hPrev; (void)cmd;
    g_hinst = hInst;

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
    wc.lpszClassName = "GameModeWndClass";
    wc.hIcon = g_icon_big ? g_icon_big : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = g_icon_small ? g_icon_small : wc.hIcon;
    if (!RegisterClassExA(&wc)) return 1;

    r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
    AdjustWindowRect(&r, style, FALSE);

    hwnd = CreateWindowExA(0, wc.lpszClassName, APP_TITLE, style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top,
                           NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    if (g_icon_big)   SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_icon_big);
    if (g_icon_small) SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon_small);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageA(g_hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }

    engine_destroy(&g_engine);
    config_free(&g_cfg);
    free(g_rows);
    DeleteObject(g_font); DeleteObject(g_font_big);
    DeleteObject(g_font_status); DeleteObject(g_font_log);
    return (int)m.wParam;
}
