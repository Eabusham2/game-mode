"""The GameMode desktop GUI (tkinter).

A single-window, tabbed application that drives :class:`gamemode.engine.Engine`:

* **Dashboard** - the master ON/OFF toggle, mode selection, options and a live
  activity log.
* **Whitelist** - processes that must never be closed.
* **Blacklist** - processes that should always be closed.
* **Processes** - a live viewer plus a "Preview kills" tool so you can see what
  the active mode would close before arming it.

The GUI never blocks on the engine: the engine logs through a thread-safe
queue that the Tk main loop drains on a timer.
"""

from __future__ import annotations

import queue
import sys
from typing import List

import tkinter as tk
from tkinter import messagebox, ttk

from .config import (
    Config,
    ALL_MODES,
    MODE_LABELS,
    MODE_DESCRIPTIONS,
    MODE_NUCLEAR,
    MODE_RISK,
)
from .engine import Engine

# Colour palette - a dark, "gamer" theme.
BG = "#15171c"
BG_PANEL = "#1d2027"
BG_INPUT = "#262a33"
FG = "#e6e6e6"
FG_DIM = "#9aa0aa"
ACCENT = "#37d67a"      # on / safe
ACCENT_OFF = "#5a5f6a"  # off
DANGER = "#ff5d5d"
WARN = "#ffb454"

LEVEL_COLORS = {
    "kill": DANGER,
    "dry": WARN,
    "warn": WARN,
    "error": DANGER,
    "info": ACCENT,
    "scan": FG_DIM,
}


class GameModeApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.config_data = Config.load()
        self.log_queue: "queue.Queue[tuple[str, str]]" = queue.Queue()
        self.engine = Engine(self.config_data, log=self._enqueue_log)

        self.title(f"GameMode — System Optimizer")
        self.geometry("860x640")
        self.minsize(760, 560)
        self.configure(bg=BG)

        self._build_style()
        self._build_layout()
        self._refresh_mode_widgets()

        # Drain the log queue periodically and keep the dashboard fresh.
        self.after(150, self._drain_log)
        self.after(1000, self._tick_stats)

        self.protocol("WM_DELETE_WINDOW", self._on_close)

        if self.config_data.enabled_on_start:
            self._set_running(True)

    # ------------------------------------------------------------------ #
    # Styling
    # ------------------------------------------------------------------ #
    def _build_style(self) -> None:
        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TFrame", background=BG)
        style.configure("Panel.TFrame", background=BG_PANEL)
        style.configure("TLabel", background=BG, foreground=FG)
        style.configure("Panel.TLabel", background=BG_PANEL, foreground=FG)
        style.configure("Dim.TLabel", background=BG_PANEL, foreground=FG_DIM)
        style.configure("Header.TLabel", background=BG, foreground=FG,
                        font=("Segoe UI", 15, "bold"))
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", background=BG_PANEL, foreground=FG_DIM,
                        padding=(16, 8), font=("Segoe UI", 10))
        style.map("TNotebook.Tab",
                  background=[("selected", BG_INPUT)],
                  foreground=[("selected", FG)])
        style.configure("TButton", background=BG_INPUT, foreground=FG,
                        borderwidth=0, padding=8, font=("Segoe UI", 10))
        style.map("TButton", background=[("active", "#333845")])
        style.configure("TCheckbutton", background=BG_PANEL, foreground=FG)
        style.map("TCheckbutton", background=[("active", BG_PANEL)])
        style.configure("TRadiobutton", background=BG_PANEL, foreground=FG)
        style.map("TRadiobutton", background=[("active", BG_PANEL)])
        style.configure("Treeview", background=BG_INPUT, fieldbackground=BG_INPUT,
                        foreground=FG, borderwidth=0, rowheight=24)
        style.configure("Treeview.Heading", background=BG_PANEL, foreground=FG_DIM)
        style.configure("Horizontal.TScale", background=BG_PANEL)

    # ------------------------------------------------------------------ #
    # Layout
    # ------------------------------------------------------------------ #
    def _build_layout(self) -> None:
        header = ttk.Frame(self)
        header.pack(fill="x", padx=16, pady=(14, 6))
        ttk.Label(header, text="\U0001F3AE  GameMode", style="Header.TLabel").pack(side="left")
        self.status_lbl = tk.Label(header, text="OFF", bg=BG, fg=ACCENT_OFF,
                                   font=("Segoe UI", 12, "bold"))
        self.status_lbl.pack(side="right")

        notebook = ttk.Notebook(self)
        notebook.pack(fill="both", expand=True, padx=16, pady=(4, 14))
        self._build_dashboard(notebook)
        self._build_listtab(notebook, "whitelist")
        self._build_listtab(notebook, "blacklist")
        self._build_processes(notebook)

    # ---- Dashboard -------------------------------------------------------
    def _build_dashboard(self, nb: ttk.Notebook) -> None:
        tab = ttk.Frame(nb)
        nb.add(tab, text="Dashboard")

        # Master toggle ----------------------------------------------------
        toggle_panel = ttk.Frame(tab, style="Panel.TFrame")
        toggle_panel.pack(fill="x", pady=(8, 8))
        inner = ttk.Frame(toggle_panel, style="Panel.TFrame")
        inner.pack(fill="x", padx=16, pady=14)

        self.toggle_btn = tk.Button(
            inner, text="TURN ON", command=self._on_toggle,
            bg=ACCENT, fg="#0c0d10", activebackground="#2fb868",
            font=("Segoe UI", 13, "bold"), bd=0, relief="flat",
            padx=24, pady=12, cursor="hand2",
        )
        self.toggle_btn.pack(side="left")

        desc = ttk.Frame(inner, style="Panel.TFrame")
        desc.pack(side="left", fill="x", expand=True, padx=(16, 0))
        ttk.Label(desc, text="Master switch", style="Panel.TLabel",
                  font=("Segoe UI", 11, "bold")).pack(anchor="w")
        ttk.Label(desc, text="Continuously detects and closes the targeted "
                  "programs while ON.", style="Dim.TLabel",
                  wraplength=520, justify="left").pack(anchor="w")

        # Mode selection ---------------------------------------------------
        mode_panel = ttk.Frame(tab, style="Panel.TFrame")
        mode_panel.pack(fill="x", pady=8)
        ttk.Label(mode_panel, text="Mode", style="Panel.TLabel",
                  font=("Segoe UI", 11, "bold")).pack(anchor="w", padx=16, pady=(12, 4))

        self.mode_var = tk.StringVar(value=self.config_data.mode)
        for mode in ALL_MODES:
            row = ttk.Frame(mode_panel, style="Panel.TFrame")
            row.pack(fill="x", padx=16, pady=2)
            rb = ttk.Radiobutton(
                row, text=MODE_LABELS[mode], value=mode, variable=self.mode_var,
                command=self._on_mode_change, style="TRadiobutton",
            )
            rb.pack(anchor="w")
            ttk.Label(row, text=MODE_DESCRIPTIONS[mode], style="Dim.TLabel",
                      wraplength=720, justify="left").pack(anchor="w", padx=(24, 0))
        self.nuclear_warn = ttk.Label(
            mode_panel, text="", style="Panel.TLabel", foreground=DANGER,
            wraplength=720, justify="left")
        self.nuclear_warn.pack(anchor="w", padx=16, pady=(2, 12))

        # Options ----------------------------------------------------------
        opt = ttk.Frame(tab, style="Panel.TFrame")
        opt.pack(fill="x", pady=8)
        ttk.Label(opt, text="Options", style="Panel.TLabel",
                  font=("Segoe UI", 11, "bold")).pack(anchor="w", padx=16, pady=(12, 6))

        self.dry_var = tk.BooleanVar(value=self.config_data.dry_run)
        ttk.Checkbutton(opt, text="Dry run (only report what would be closed - kill nothing)",
                        variable=self.dry_var, command=self._on_options_change,
                        style="TCheckbutton").pack(anchor="w", padx=16)

        self.svc_var = tk.BooleanVar(value=self.config_data.manage_services)
        ttk.Checkbutton(opt, text="Also stop non-essential Windows services (Risk/Nuclear only - needs admin)",
                        variable=self.svc_var, command=self._on_options_change,
                        style="TCheckbutton").pack(anchor="w", padx=16, pady=(2, 2))

        interval_row = ttk.Frame(opt, style="Panel.TFrame")
        interval_row.pack(fill="x", padx=16, pady=(6, 12))
        ttk.Label(interval_row, text="Scan interval:", style="Panel.TLabel").pack(side="left")
        self.interval_var = tk.DoubleVar(value=self.config_data.scan_interval)
        self.interval_lbl = ttk.Label(interval_row, text="", style="Dim.TLabel")
        self.interval_lbl.pack(side="right")
        scale = ttk.Scale(interval_row, from_=1, to=30, variable=self.interval_var,
                          orient="horizontal", command=self._on_interval_change)
        scale.pack(side="left", fill="x", expand=True, padx=10)
        self._on_interval_change()

        # Stats + log ------------------------------------------------------
        stats_row = ttk.Frame(tab)
        stats_row.pack(fill="x", pady=(6, 4))
        self.stats_lbl = ttk.Label(stats_row, text="", style="TLabel",
                                   foreground=FG_DIM)
        self.stats_lbl.pack(side="left")
        ttk.Button(stats_row, text="Clear log", command=self._clear_log).pack(side="right")

        log_frame = ttk.Frame(tab)
        log_frame.pack(fill="both", expand=True)
        self.log = tk.Text(log_frame, bg="#0e1014", fg=FG, bd=0, height=8,
                           font=("Consolas", 9), wrap="word", state="disabled",
                           insertbackground=FG)
        self.log.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(log_frame, command=self.log.yview)
        sb.pack(side="right", fill="y")
        self.log.configure(yscrollcommand=sb.set)
        for level, colour in LEVEL_COLORS.items():
            self.log.tag_configure(level, foreground=colour)

    # ---- Whitelist / Blacklist tabs -------------------------------------
    def _build_listtab(self, nb: ttk.Notebook, which: str) -> None:
        tab = ttk.Frame(nb)
        nb.add(tab, text=which.capitalize())
        is_white = which == "whitelist"

        intro = ("Processes here are NEVER closed, in any mode (in addition to the "
                 "built-in OS/anti-cheat protections)."
                 if is_white else
                 "Processes here are ALWAYS closed while GameMode is ON, in every mode "
                 "(critical OS processes are still protected for safety).")
        ttk.Label(tab, text=intro, style="TLabel", foreground=FG_DIM,
                  wraplength=780, justify="left").pack(anchor="w", pady=(10, 8))

        body = ttk.Frame(tab)
        body.pack(fill="both", expand=True)

        listbox = tk.Listbox(body, bg=BG_INPUT, fg=FG, bd=0, selectmode="extended",
                             font=("Consolas", 10), activestyle="none",
                             selectbackground=ACCENT, selectforeground="#0c0d10")
        listbox.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(body, command=listbox.yview)
        sb.pack(side="left", fill="y")
        listbox.configure(yscrollcommand=sb.set)

        controls = ttk.Frame(tab)
        controls.pack(fill="x", pady=10)
        entry = tk.Entry(controls, bg=BG_INPUT, fg=FG, bd=0, insertbackground=FG,
                         font=("Consolas", 10))
        entry.pack(side="left", fill="x", expand=True, ipady=5, padx=(0, 8))
        entry.bind("<Return>", lambda e: self._list_add(which))

        ttk.Button(controls, text="Add",
                   command=lambda: self._list_add(which)).pack(side="left", padx=2)
        ttk.Button(controls, text="Remove selected",
                   command=lambda: self._list_remove(which)).pack(side="left", padx=2)

        if is_white:
            self.white_listbox, self.white_entry = listbox, entry
        else:
            self.black_listbox, self.black_entry = listbox, entry
        self._reload_list(which)

    # ---- Processes tab ---------------------------------------------------
    def _build_processes(self, nb: ttk.Notebook) -> None:
        tab = ttk.Frame(nb)
        nb.add(tab, text="Processes")

        bar = ttk.Frame(tab)
        bar.pack(fill="x", pady=(10, 6))
        ttk.Button(bar, text="Refresh running",
                   command=self._refresh_processes).pack(side="left", padx=2)
        ttk.Button(bar, text="Preview kills for current mode",
                   command=self._preview_kills).pack(side="left", padx=2)
        ttk.Button(bar, text="→ Whitelist selected",
                   command=lambda: self._send_selected("whitelist")).pack(side="left", padx=2)
        ttk.Button(bar, text="→ Blacklist selected",
                   command=lambda: self._send_selected("blacklist")).pack(side="left", padx=2)
        self.proc_info = ttk.Label(tab, text="", style="TLabel", foreground=FG_DIM)
        self.proc_info.pack(anchor="w", pady=(0, 6))

        cols = ("name", "pid", "fate")
        self.proc_tree = ttk.Treeview(tab, columns=cols, show="headings")
        self.proc_tree.heading("name", text="Process")
        self.proc_tree.heading("pid", text="PID")
        self.proc_tree.heading("fate", text="Under current mode")
        self.proc_tree.column("name", width=320)
        self.proc_tree.column("pid", width=80, anchor="center")
        self.proc_tree.column("fate", width=200)
        self.proc_tree.tag_configure("kill", foreground=DANGER)
        self.proc_tree.tag_configure("keep", foreground=ACCENT)
        self.proc_tree.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(tab, command=self.proc_tree.yview)
        sb.pack(side="left", fill="y")
        self.proc_tree.configure(yscrollcommand=sb.set)
        self._refresh_processes()

    # ------------------------------------------------------------------ #
    # Event handlers
    # ------------------------------------------------------------------ #
    def _on_toggle(self) -> None:
        self._set_running(not self.engine.running)

    def _set_running(self, run: bool) -> None:
        if run:
            if self.config_data.mode == MODE_NUCLEAR and not self.config_data.dry_run:
                if not messagebox.askokcancel(
                    "Arm Nuclear mode?",
                    "Nuclear mode closes EVERYTHING except Windows essentials, "
                    "anti-cheats, detected games, Discord, GameMode and your "
                    "whitelist - including your browser and open documents.\n\n"
                    "Tip: use 'Preview kills' or Dry run first.\n\nArm it now?",
                ):
                    return
            self.engine.start()
            self.toggle_btn.configure(text="TURN OFF", bg=DANGER,
                                      activebackground="#d94545")
            self.status_lbl.configure(text="ON ●", fg=ACCENT)
        else:
            self.engine.stop()
            self.toggle_btn.configure(text="TURN ON", bg=ACCENT,
                                      activebackground="#2fb868")
            self.status_lbl.configure(text="OFF", fg=ACCENT_OFF)
        self.config_data.enabled_on_start = run
        self._save()

    def _on_mode_change(self) -> None:
        self.config_data.mode = self.mode_var.get()
        self._refresh_mode_widgets()
        self._save()
        self._log_line("info", f"Mode changed to '{self.config_data.mode}'.")

    def _refresh_mode_widgets(self) -> None:
        if self.config_data.mode == MODE_NUCLEAR:
            self.nuclear_warn.configure(
                text="⚠ Nuclear is the riskiest profile - preview it before arming.")
        elif self.config_data.mode == MODE_RISK:
            self.nuclear_warn.configure(
                text="⚠ Risk closes most non-whitelisted apps. Browsers stay open.")
        else:
            self.nuclear_warn.configure(text="")

    def _on_options_change(self) -> None:
        self.config_data.dry_run = self.dry_var.get()
        self.config_data.manage_services = self.svc_var.get()
        self._save()

    def _on_interval_change(self, *_args) -> None:
        val = round(float(self.interval_var.get()))
        self.config_data.scan_interval = float(val)
        self.interval_lbl.configure(text=f"every {val}s")
        self._save()

    # ---- list editing ----------------------------------------------------
    def _list_widgets(self, which: str):
        if which == "whitelist":
            return self.white_listbox, self.white_entry, self.config_data.whitelist
        return self.black_listbox, self.black_entry, self.config_data.blacklist

    def _reload_list(self, which: str) -> None:
        listbox, _, data = self._list_widgets(which)
        listbox.delete(0, "end")
        for item in sorted(data):
            listbox.insert("end", item)

    def _list_add(self, which: str) -> None:
        listbox, entry, data = self._list_widgets(which)
        name = entry.get().strip().lower()
        if not name:
            return
        if name not in data:
            data.append(name)
            self._save()
            self._reload_list(which)
        entry.delete(0, "end")

    def _list_remove(self, which: str) -> None:
        listbox, _, data = self._list_widgets(which)
        for idx in reversed(listbox.curselection()):
            name = listbox.get(idx)
            if name in data:
                data.remove(name)
        self._save()
        self._reload_list(which)

    def _send_selected(self, which: str) -> None:
        _, _, data = self._list_widgets(which)
        added = 0
        for iid in self.proc_tree.selection():
            name = str(self.proc_tree.item(iid, "values")[0]).strip().lower()
            if name and name not in data:
                data.append(name)
                added += 1
        if added:
            self._save()
            self._reload_list(which)
            self._log_line("info", f"Added {added} process(es) to {which}.")

    # ---- processes -------------------------------------------------------
    def _refresh_processes(self) -> None:
        rows = self.engine.list_running()
        kill_names = {(r["name"], r["pid"]) for r in self.engine.preview()}
        self.proc_tree.delete(*self.proc_tree.get_children())
        for r in rows:
            doomed = (r["name"], r["pid"]) in kill_names
            self.proc_tree.insert(
                "", "end",
                values=(r["name"], r["pid"], "will be CLOSED" if doomed else "kept"),
                tags=("kill" if doomed else "keep",),
            )
        self.proc_info.configure(
            text=f"{len(rows)} processes running · {len(kill_names)} would be closed "
                 f"in '{self.config_data.mode}' mode.")

    def _preview_kills(self) -> None:
        rows = self.engine.preview()
        self.proc_tree.delete(*self.proc_tree.get_children())
        for r in rows:
            self.proc_tree.insert("", "end",
                                  values=(r["name"], r["pid"], "will be CLOSED"),
                                  tags=("kill",))
        self.proc_info.configure(
            text=f"Preview: {len(rows)} process(es) would be closed in "
                 f"'{self.config_data.mode}' mode.")
        if not rows:
            self.proc_info.configure(text="Preview: nothing would be closed right now.")

    # ------------------------------------------------------------------ #
    # Logging / stats plumbing
    # ------------------------------------------------------------------ #
    def _enqueue_log(self, level: str, message: str) -> None:
        # Called from the engine thread - must stay thread-safe.
        self.log_queue.put((level, message))

    def _drain_log(self) -> None:
        try:
            while True:
                level, message = self.log_queue.get_nowait()
                self._log_line(level, message)
        except queue.Empty:
            pass
        self.after(150, self._drain_log)

    def _log_line(self, level: str, message: str) -> None:
        import time as _t
        ts = _t.strftime("%H:%M:%S")
        self.log.configure(state="normal")
        self.log.insert("end", f"[{ts}] ", "scan")
        self.log.insert("end", message + "\n", level if level in LEVEL_COLORS else "info")
        # Cap log length to avoid unbounded growth.
        if int(self.log.index("end-1c").split(".")[0]) > 800:
            self.log.delete("1.0", "200.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self) -> None:
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _tick_stats(self) -> None:
        s = self.engine.stats
        state = "RUNNING" if self.engine.running else "stopped"
        self.stats_lbl.configure(
            text=f"Engine {state}  ·  scans: {s.scans}  ·  "
                 f"closed: {s.killed}  ·  services stopped: {s.services_stopped}  "
                 f"·  last scan targets: {s.last_targets}")
        self.after(1000, self._tick_stats)

    # ------------------------------------------------------------------ #
    # Persistence / shutdown
    # ------------------------------------------------------------------ #
    def _save(self) -> None:
        try:
            self.config_data.save()
        except Exception as exc:
            self._log_line("error", f"Could not save settings: {exc}")

    def _on_close(self) -> None:
        try:
            self.engine.stop()
        finally:
            self._save()
            self.destroy()


def main(argv: List[str] | None = None) -> int:
    try:
        app = GameModeApp()
    except tk.TclError as exc:
        print("GameMode needs a graphical display to run its GUI.\n"
              f"Tkinter error: {exc}", file=sys.stderr)
        return 1
    app.mainloop()
    return 0
