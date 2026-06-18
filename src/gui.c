/* gui.c - native Win32 GUI: master toggle, mode picker, options, live log,
 * whitelist/blacklist editors and a process viewer with kill-preview.
 *
 * Controls are siblings of the tab control; pages are shown/hidden on tab
 * change. The engine runs on its own thread and reports back via WM_APP_LOG /
 * WM_APP_STATS so the UI never blocks. */
#include "gamemode.h"
#include "config.h"
#include "engine.h"
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLIENT_W 820
#define CLIENT_H 610
#define PX 25            /* page content left edge */

static Config  g_cfg;
static Engine  g_engine;
static HWND    g_hwnd;
static HFONT   g_font, g_font_big, g_font_status;
static int     g_running_ui;     /* mirrors engine state for UI */

/* control handles we need to touch later */
static HWND h_tab, h_toggle, h_status, h_modedesc, h_track, h_interval;
static HWND h_chk_dry, h_chk_svc, h_stats, h_log;
static HWND h_wl_list, h_wl_edit, h_bl_list, h_bl_edit;
static HWND h_proc_list, h_proc_info;
static HWND h_radio[MODE_COUNT];

/* names shown in the Processes listbox, aligned with item indices */
static StrList g_proc_names;

/* ---- page membership (for show/hide) ------------------------------------ */
static HWND g_page[4][24];
static int  g_page_n[4];
static int  g_cur_page = 0;

static void page_add(int page, HWND h) { g_page[page][g_page_n[page]++] = h; }

static void show_page(int idx)
{
    int p, i;
    g_cur_page = idx;
    for (p = 0; p < 4; ++p)
        for (i = 0; i < g_page_n[p]; ++i)
            ShowWindow(g_page[p][i], p == idx ? SW_SHOW : SW_HIDE);
}

/* ---- small creation helpers --------------------------------------------- */
static HWND mk(const char *cls, const char *text, DWORD style,
               int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | style,
                             x, y, w, h, g_hwnd, (HMENU)(INT_PTR)id,
                             GetModuleHandle(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void set_text(HWND h, const char *s) { SetWindowTextA(h, s); }

/* ---- config <-> widgets -------------------------------------------------- */
static void reload_listbox(HWND lb, const StrList *l)
{
    int i;
    SendMessageA(lb, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < l->count; ++i)
        SendMessageA(lb, LB_ADDSTRING, 0, (LPARAM)l->items[i]);
}

static void save_cfg(void) { config_save(&g_cfg); }

static void update_mode_desc(void)
{
    set_text(h_modedesc, MODE_DESCRIPTIONS[g_cfg.mode]);
}

static void update_status_ui(void)
{
    if (g_running_ui) {
        set_text(h_status, "ON");
        set_text(h_toggle, "TURN OFF");
    } else {
        set_text(h_status, "OFF");
        set_text(h_toggle, "TURN ON");
    }
    InvalidateRect(h_status, NULL, TRUE);
}

/* ---- toggle -------------------------------------------------------------- */
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

/* ---- logging ------------------------------------------------------------- */
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

static void log_append(int level, const char *msg)
{
    char line[640];
    SYSTEMTIME st;
    int len;
    GetLocalTime(&st);
    wsprintfA(line, "[%02d:%02d:%02d] %s  %s\r\n",
              st.wHour, st.wMinute, st.wSecond, level_tag(level), msg);

    /* keep the edit from growing without bound */
    len = GetWindowTextLengthA(h_log);
    if (len > 28000) {
        SendMessageA(h_log, EM_SETSEL, 0, 8000);
        SendMessageA(h_log, EM_REPLACESEL, FALSE, (LPARAM)"");
        len = GetWindowTextLengthA(h_log);
    }
    SendMessageA(h_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(h_log, EM_REPLACESEL, FALSE, (LPARAM)line);
    SendMessageA(h_log, EM_SCROLLCARET, 0, 0);
}

/* ---- process list population -------------------------------------------- */
static void proc_fill_cb(const char *name, unsigned long pid, int doomed, void *user)
{
    int *killcount = (int *)user;
    char line[320];
    wsprintfA(line, "%-32s  pid %-6lu  %s",
              name, pid, doomed ? "<< will be CLOSED" : "kept");
    SendMessageA(h_proc_list, LB_ADDSTRING, 0, (LPARAM)line);
    strlist_add(&g_proc_names, name);
    if (doomed && killcount) (*killcount)++;
}

static void refresh_processes(int preview_only)
{
    char info[160];
    int killcount = 0;
    SendMessageA(h_proc_list, LB_RESETCONTENT, 0, 0);
    strlist_clear(&g_proc_names);
    if (preview_only)
        engine_preview(&g_engine, proc_fill_cb, &killcount);
    else
        engine_list_running(&g_engine, proc_fill_cb, &killcount);

    if (preview_only)
        wsprintfA(info, "Preview: %d process(es) would be closed in '%s' mode.",
                  killcount, MODE_KEYS[g_cfg.mode]);
    else
        wsprintfA(info, "%d processes - %d would be closed in '%s' mode.",
                  (int)SendMessageA(h_proc_list, LB_GETCOUNT, 0, 0),
                  killcount, MODE_KEYS[g_cfg.mode]);
    set_text(h_proc_info, info);
}

/* send selected processes to the whitelist or blacklist */
static void send_selected(StrList *dst, HWND dst_list)
{
    int n = (int)SendMessageA(h_proc_list, LB_GETSELCOUNT, 0, 0);
    int *idx, i, added = 0;
    if (n <= 0) return;
    idx = (int *)malloc((size_t)n * sizeof(int));
    if (!idx) return;
    SendMessageA(h_proc_list, LB_GETSELITEMS, (WPARAM)n, (LPARAM)idx);
    for (i = 0; i < n; ++i) {
        if (idx[i] >= 0 && idx[i] < g_proc_names.count)
            added += strlist_add(dst, g_proc_names.items[idx[i]]);
    }
    free(idx);
    if (added) {
        reload_listbox(dst_list, dst);
        save_cfg();
        log_append(LOG_INFO, "Added selected process(es) to list.");
    }
}

/* remove selected entries from a list listbox */
static void remove_selected(StrList *l, HWND lb)
{
    int n = (int)SendMessageA(lb, LB_GETSELCOUNT, 0, 0);
    int *idx, i;
    char buf[260];
    if (n <= 0) return;
    idx = (int *)malloc((size_t)n * sizeof(int));
    if (!idx) return;
    SendMessageA(lb, LB_GETSELITEMS, (WPARAM)n, (LPARAM)idx);
    /* remove from the end so indices stay valid */
    for (i = n - 1; i >= 0; --i) {
        if (SendMessageA(lb, LB_GETTEXT, (WPARAM)idx[i], (LPARAM)buf) != LB_ERR)
            strlist_remove(l, buf);
    }
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
static void build_ui(void)
{
    int i;
    TCITEMA tie;
    const char *tabs[4] = { "Dashboard", "Whitelist", "Blacklist", "Processes" };

    /* header (always visible, not part of any page) */
    {
        HWND title = mk("STATIC", "\xF0\x9F\x8E\xAE  GameMode", SS_LEFT, 15, 12, 320, 26, -1);
        SendMessageA(title, WM_SETFONT, (WPARAM)g_font_big, TRUE);
        h_status = mk("STATIC", "OFF", SS_RIGHT, CLIENT_W - 180, 14, 160, 24, IDC_STATUS);
        SendMessageA(h_status, WM_SETFONT, (WPARAM)g_font_status, TRUE);
    }

    /* tab control */
    h_tab = mk(WC_TABCONTROLA, "", 0, 12, 46, CLIENT_W - 24, CLIENT_H - 56, IDC_TAB);
    ShowWindow(h_tab, SW_SHOW);
    tie.mask = TCIF_TEXT;
    for (i = 0; i < 4; ++i) {
        tie.pszText = (char *)tabs[i];
        SendMessageA(h_tab, TCM_INSERTITEMA, (WPARAM)i, (LPARAM)&tie);
    }

    /* =================== DASHBOARD =================== */
    h_toggle = mk("BUTTON", "TURN ON", BS_PUSHBUTTON, PX, 85, 170, 56, IDC_TOGGLE);
    SendMessageA(h_toggle, WM_SETFONT, (WPARAM)g_font_big, TRUE);
    page_add(0, h_toggle);
    page_add(0, mk("STATIC",
        "Master switch - while ON, GameMode continuously closes the targeted "
        "programs every scan.", SS_LEFT, PX + 185, 92, 560, 44, -1));

    page_add(0, mk("BUTTON", "Mode", BS_GROUPBOX, PX, 150, 770, 150, IDC_MODE_GROUP));
    for (i = 0; i < MODE_COUNT; ++i) {
        DWORD st = BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0);
        h_radio[i] = mk("BUTTON", MODE_LABELS[i], st, PX + 15, 175 + i * 27, 320, 24,
                        IDC_RADIO_SMART + i);
        page_add(0, h_radio[i]);
    }
    h_modedesc = mk("STATIC", "", SS_LEFT, PX + 345, 178, 410, 112, IDC_MODE_DESC);
    page_add(0, h_modedesc);

    page_add(0, mk("BUTTON", "Options", BS_GROUPBOX, PX, 308, 770, 112, IDC_OPT_GROUP));
    h_chk_dry = mk("BUTTON", "Dry run (report only - close nothing)",
                   BS_AUTOCHECKBOX, PX + 15, 330, 360, 22, IDC_CHK_DRYRUN);
    page_add(0, h_chk_dry);
    h_chk_svc = mk("BUTTON", "Also stop non-essential Windows services (Risk/Nuclear, needs admin)",
                   BS_AUTOCHECKBOX, PX + 15, 356, 500, 22, IDC_CHK_SERVICES);
    page_add(0, h_chk_svc);
    page_add(0, mk("STATIC", "Scan interval:", SS_LEFT, PX + 15, 388, 90, 20, -1));
    h_track = mk(TRACKBAR_CLASSA, "", TBS_HORZ | TBS_AUTOTICKS, PX + 110, 384, 300, 28, IDC_TRACK);
    page_add(0, h_track);
    SendMessageA(h_track, TBM_SETRANGE, TRUE, MAKELONG(1, 30));
    h_interval = mk("STATIC", "every 3s", SS_LEFT, PX + 420, 388, 120, 20, IDC_INTERVAL_LBL);
    page_add(0, h_interval);

    h_stats = mk("STATIC", "", SS_LEFT, PX, 430, 600, 20, IDC_STATS);
    page_add(0, h_stats);
    page_add(0, mk("BUTTON", "Clear log", BS_PUSHBUTTON, PX + 685, 426, 85, 26, IDC_CLEARLOG));
    h_log = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        PX, 456, 770, 118, g_hwnd, (HMENU)(INT_PTR)IDC_LOG, GetModuleHandle(NULL), NULL);
    SendMessageA(h_log, WM_SETFONT, (WPARAM)g_font, TRUE);
    page_add(0, h_log);

    /* =================== WHITELIST =================== */
    page_add(1, mk("STATIC",
        "Processes here are NEVER closed, in any mode (in addition to the "
        "built-in OS and anti-cheat protections). Use exe names, e.g. mygame.exe",
        SS_LEFT, PX, 85, 770, 40, IDC_WL_INTRO));
    h_wl_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL,
        PX, 130, 770, 350, g_hwnd, (HMENU)(INT_PTR)IDC_WL_LIST, GetModuleHandle(NULL), NULL);
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
        PX, 130, 770, 350, g_hwnd, (HMENU)(INT_PTR)IDC_BL_LIST, GetModuleHandle(NULL), NULL);
    SendMessageA(h_bl_list, WM_SETFONT, (WPARAM)g_font, TRUE);
    page_add(2, h_bl_list);
    h_bl_edit = mk("EDIT", "", ES_AUTOHSCROLL | WS_BORDER, PX, 490, 500, 26, IDC_BL_EDIT);
    page_add(2, h_bl_edit);
    page_add(2, mk("BUTTON", "Add", BS_PUSHBUTTON, PX + 510, 489, 110, 28, IDC_BL_ADD));
    page_add(2, mk("BUTTON", "Remove selected", BS_PUSHBUTTON, PX + 630, 489, 140, 28, IDC_BL_REMOVE));

    /* =================== PROCESSES =================== */
    page_add(3, mk("BUTTON", "Refresh running", BS_PUSHBUTTON, PX, 85, 130, 28, IDC_PROC_REFRESH));
    page_add(3, mk("BUTTON", "Preview kills", BS_PUSHBUTTON, PX + 140, 85, 130, 28, IDC_PROC_PREVIEW));
    page_add(3, mk("BUTTON", "-> Whitelist sel.", BS_PUSHBUTTON, PX + 280, 85, 140, 28, IDC_PROC_TOWL));
    page_add(3, mk("BUTTON", "-> Blacklist sel.", BS_PUSHBUTTON, PX + 430, 85, 140, 28, IDC_PROC_TOBL));
    h_proc_info = mk("STATIC", "", SS_LEFT, PX, 120, 770, 20, IDC_PROC_INFO);
    page_add(3, h_proc_info);
    h_proc_list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_EXTENDEDSEL | LBS_USETABSTOPS,
        PX, 145, 770, 425, g_hwnd, (HMENU)(INT_PTR)IDC_PROC_LIST, GetModuleHandle(NULL), NULL);
    SendMessageA(h_proc_list, WM_SETFONT, (WPARAM)g_font, TRUE);
    page_add(3, h_proc_list);
}

/* ---- load persisted settings into the widgets --------------------------- */
static void apply_cfg_to_ui(void)
{
    int iv;
    SendMessageA(h_radio[g_cfg.mode], BM_SETCHECK, BST_CHECKED, 0);
    update_mode_desc();
    SendMessageA(h_chk_dry, BM_SETCHECK, g_cfg.dry_run ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(h_chk_svc, BM_SETCHECK, g_cfg.manage_services ? BST_CHECKED : BST_UNCHECKED, 0);
    iv = (int)(g_cfg.scan_interval + 0.5);
    SendMessageA(h_track, TBM_SETPOS, TRUE, iv);
    {
        char b[32]; wsprintfA(b, "every %ds", iv); set_text(h_interval, b);
    }
    reload_listbox(h_wl_list, &g_cfg.whitelist);
    reload_listbox(h_bl_list, &g_cfg.blacklist);
    update_status_ui();
}

static void update_stats_label(void)
{
    Stats s; char buf[256];
    engine_get_stats(&g_engine, &s);
    wsprintfA(buf,
        "Engine %s   scans: %ld   closed: %ld   services stopped: %ld   last targets: %ld",
        g_engine.running ? "RUNNING" : "stopped",
        s.scans, s.killed, s.services_stopped, s.last_targets);
    set_text(h_stats, buf);
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
        SetTimer(hwnd, TIMER_STATS, 1000, NULL);
        if (g_cfg.enabled_on_start) set_running(1);
        update_stats_label();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == IDC_TOGGLE && code == BN_CLICKED) {
            set_running(!g_engine.running);
        } else if (id >= IDC_RADIO_SMART && id < IDC_RADIO_SMART + MODE_COUNT
                   && code == BN_CLICKED) {
            g_cfg.mode = id - IDC_RADIO_SMART;
            update_mode_desc();
            save_cfg();
            log_append(LOG_INFO, "Mode changed.");
        } else if (id == IDC_CHK_DRYRUN && code == BN_CLICKED) {
            g_cfg.dry_run = (int)SendMessageA(h_chk_dry, BM_GETCHECK, 0, 0) == BST_CHECKED;
            save_cfg();
        } else if (id == IDC_CHK_SERVICES && code == BN_CLICKED) {
            g_cfg.manage_services = (int)SendMessageA(h_chk_svc, BM_GETCHECK, 0, 0) == BST_CHECKED;
            save_cfg();
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
        } else if (id == IDC_PROC_TOWL && code == BN_CLICKED) {
            send_selected(&g_cfg.whitelist, h_wl_list);
        } else if (id == IDC_PROC_TOBL && code == BN_CLICKED) {
            send_selected(&g_cfg.blacklist, h_bl_list);
        }
        return 0;
    }

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
        if (s) { log_append((int)wp, s); free(s); }
        return 0;
    }

    case WM_APP_STATS:
        update_stats_label();
        return 0;

    case WM_TIMER:
        if (wp == TIMER_STATS) update_stats_label();
        return 0;

    case WM_CLOSE:
        engine_stop(&g_engine);
        save_cfg();
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_STATS);
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

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    config_load(&g_cfg);
    strlist_init(&g_proc_names);

    g_font        = CreateFontA(-15, 0,0,0, FW_NORMAL, 0,0,0, DEFAULT_CHARSET,
                                0,0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_big    = CreateFontA(-22, 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,
                                0,0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    g_font_status = CreateFontA(-20, 0,0,0, FW_BOLD, 0,0,0, DEFAULT_CHARSET,
                                0,0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "GameModeWndClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassExA(&wc)) return 1;

    r.left = 0; r.top = 0; r.right = CLIENT_W; r.bottom = CLIENT_H;
    AdjustWindowRect(&r, style, FALSE);

    hwnd = CreateWindowExA(0, wc.lpszClassName, APP_TITLE, style,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top,
                           NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageA(g_hwnd, &m)) continue; /* tab/arrow navigation */
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }

    engine_destroy(&g_engine);
    config_free(&g_cfg);
    strlist_free(&g_proc_names);
    DeleteObject(g_font); DeleteObject(g_font_big); DeleteObject(g_font_status);
    return (int)m.wParam;
}
