"""The background engine that detects and disables programs/services.

The engine runs on its own daemon thread.  Every ``scan_interval`` seconds it
enumerates running processes (and, optionally, Windows services), decides what
should be terminated for the active mode, and kills it - unless the run is a
"dry run", in which case it only reports.

Design goals:

* **Safety first.** A multi-layered guard makes it impossible to terminate
  critical OS processes, the current process tree (GameMode itself) or anything
  the user whitelisted.  These guards win over every blacklist/mode rule.
* **Non-blocking GUI.** All work happens off the UI thread; the engine reports
  back through a thread-safe callback so the GUI can render a live log.
* **Cross-platform import.** Windows-only features (services) are guarded so the
  module still imports and partially runs on Linux/macOS for development.
"""

from __future__ import annotations

import os
import threading
import time
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional, Set

try:
    import psutil
except ImportError as exc:  # pragma: no cover - dependency missing
    raise SystemExit(
        "GameMode requires the 'psutil' package. Install it with:\n"
        "    pip install -r requirements.txt"
    ) from exc

from . import known_lists as KL
from .config import (
    Config,
    MODE_SMART,
    MODE_AGGRESSIVE,
    MODE_RISK,
    MODE_NUCLEAR,
)

LogFn = Callable[[str, str], None]  # (level, message)


@dataclass
class Stats:
    """Live counters surfaced to the GUI."""

    scans: int = 0
    killed: int = 0
    services_stopped: int = 0
    last_scan_ts: float = 0.0
    last_targets: int = 0


class Engine:
    """Continuously enforces the selected GameMode profile."""

    def __init__(self, config: Config, log: Optional[LogFn] = None) -> None:
        self.config = config
        self._log: LogFn = log or (lambda level, msg: None)
        self._thread: Optional[threading.Thread] = None
        self._stop = threading.Event()
        self._lock = threading.Lock()
        self.stats = Stats()

        # The whole process tree of GameMode (this interpreter + children) is
        # protected so the tool can never terminate itself or its host shell.
        self._self_pids: Set[int] = self._compute_self_pids()

    # ------------------------------------------------------------------ #
    # Lifecycle
    # ------------------------------------------------------------------ #
    @property
    def running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def start(self) -> None:
        if self.running:
            return
        self._stop.clear()
        self._self_pids = self._compute_self_pids()
        self._thread = threading.Thread(
            target=self._run_loop, name="gamemode-engine", daemon=True
        )
        self._thread.start()
        self._log("info", f"Engine started in '{self.config.mode}' mode.")

    def stop(self) -> None:
        if not self.running:
            return
        self._stop.set()
        self._thread.join(timeout=5)
        self._thread = None
        self._log("info", "Engine stopped.")

    # ------------------------------------------------------------------ #
    # Main loop
    # ------------------------------------------------------------------ #
    def _run_loop(self) -> None:
        while not self._stop.is_set():
            try:
                self.scan_once()
            except Exception as exc:  # never let the loop die silently
                self._log("error", f"Scan failed: {exc}")
            # Sleep in small slices so Stop is responsive.
            interval = max(1.0, float(self.config.scan_interval))
            waited = 0.0
            while waited < interval and not self._stop.is_set():
                time.sleep(0.2)
                waited += 0.2

    # ------------------------------------------------------------------ #
    # Safety guards
    # ------------------------------------------------------------------ #
    def _compute_self_pids(self) -> Set[int]:
        """Collect our own PID, parent shell and all descendant PIDs."""
        pids: Set[int] = {os.getpid()}
        try:
            me = psutil.Process(os.getpid())
            # Protect ancestors (the launching shell / IDE) too.
            for parent in me.parents():
                pids.add(parent.pid)
            for child in me.children(recursive=True):
                pids.add(child.pid)
        except Exception:
            pass
        return pids

    def _is_protected(self, name: str, pid: int, whitelist: Set[str]) -> bool:
        """Return True if a process must NEVER be terminated.

        This is the single chokepoint every kill decision passes through.
        Nothing - not the blacklist, not Nuclear mode - can bypass it.
        """
        if pid in self._self_pids or pid <= 4:
            return True  # ourselves / PID 0,4 (System/Idle)
        if name in KL.PROTECTED_SYSTEM:
            return True
        if name in KL.ANTI_CHEAT:
            return True
        if name in whitelist:
            return True
        return False

    # ------------------------------------------------------------------ #
    # Decision logic
    # ------------------------------------------------------------------ #
    def _keep_set_for_mode(self) -> Set[str]:
        """Names kept ('allow-list') for whitelist-style modes."""
        keep = set()
        keep |= KL.PROTECTED_SYSTEM
        keep |= KL.ANTI_CHEAT
        keep |= KL.GAME_PLATFORMS
        keep |= KL.KEEP_WHILE_GAMING
        keep |= set(self.config.whitelist)
        if self.config.mode == MODE_RISK:
            # Risk mode is lenient: keep ordinary, useful apps too.
            keep |= KL.COMMON_APPS
        return keep

    def _should_kill(self, name: str, whitelist: Set[str], blacklist: Set[str]) -> bool:
        """Decide whether a (non-protected) process should be terminated."""
        mode = self.config.mode

        # The user's blacklist always kills (protection is checked separately).
        if name in blacklist:
            return True

        if mode == MODE_SMART:
            return name in KL.BLOATWARE

        if mode == MODE_AGGRESSIVE:
            return name in KL.BLOATWARE or name in KL.BACKGROUND_NOISE

        # Risk & Nuclear are allow-list driven: kill anything not kept.
        if mode in (MODE_RISK, MODE_NUCLEAR):
            return name not in self._keep_set_for_mode()

        return False

    # ------------------------------------------------------------------ #
    # One scan pass
    # ------------------------------------------------------------------ #
    def scan_once(self) -> int:
        """Run a single detection/termination pass. Returns processes killed."""
        whitelist = set(self.config.whitelist)
        blacklist = set(self.config.blacklist)
        dry = self.config.dry_run

        killed = 0
        targets = 0

        for proc in psutil.process_iter(["pid", "name"]):
            try:
                raw_name = proc.info.get("name") or ""
                name = KL.normalize(raw_name)
                pid = proc.info.get("pid")
                if not name or pid is None:
                    continue

                if self._is_protected(name, pid, whitelist):
                    continue

                if not self._should_kill(name, whitelist, blacklist):
                    continue

                targets += 1
                if dry:
                    self._log("dry", f"[dry-run] would close {raw_name} (pid {pid})")
                    continue

                if self._terminate(proc, raw_name, pid):
                    killed += 1
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
            except Exception as exc:
                self._log("error", f"Error handling pid: {exc}")
                continue

        services_stopped = 0
        if self.config.manage_services and self.config.mode in (MODE_RISK, MODE_NUCLEAR):
            services_stopped = self._sweep_services(dry)

        with self._lock:
            self.stats.scans += 1
            self.stats.killed += killed
            self.stats.services_stopped += services_stopped
            self.stats.last_scan_ts = time.time()
            self.stats.last_targets = targets

        if targets == 0 and services_stopped == 0:
            self._log("scan", "Scan complete - nothing to close.")
        return killed

    def _terminate(self, proc: "psutil.Process", raw_name: str, pid: int) -> bool:
        """Politely terminate, then force-kill a process. Returns success."""
        try:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except psutil.TimeoutExpired:
                proc.kill()  # graceful close ignored -> force it
                proc.wait(timeout=2)
            self._log("kill", f"Closed {raw_name} (pid {pid})")
            return True
        except psutil.NoSuchProcess:
            return True  # already gone - counts as success
        except psutil.AccessDenied:
            self._log(
                "warn",
                f"Access denied closing {raw_name} (pid {pid}). "
                "Run GameMode as Administrator to close protected apps.",
            )
            return False
        except Exception as exc:
            self._log("error", f"Failed to close {raw_name} (pid {pid}): {exc}")
            return False

    # ------------------------------------------------------------------ #
    # Windows services
    # ------------------------------------------------------------------ #
    def _sweep_services(self, dry: bool) -> int:
        """Stop non-essential, currently-running Windows services."""
        if not hasattr(psutil, "win_service_iter"):
            return 0  # not on Windows
        whitelist = set(self.config.whitelist)
        stopped = 0
        try:
            services = list(psutil.win_service_iter())
        except Exception as exc:
            self._log("error", f"Could not enumerate services: {exc}")
            return 0

        for svc in services:
            try:
                sname = KL.normalize(svc.name())
                if sname in KL.ESSENTIAL_SERVICES or sname in whitelist:
                    continue
                if svc.status() != "running":
                    continue
                # Only touch services that auto-start themselves; leave
                # on-demand/driver services to the OS.
                if svc.start_type() not in ("automatic", "manual"):
                    continue
                if dry:
                    self._log("dry", f"[dry-run] would stop service '{sname}'")
                    stopped += 1
                    continue
                if self._stop_service(sname):
                    stopped += 1
            except Exception:
                continue
        return stopped

    def _stop_service(self, sname: str) -> bool:
        """Stop a single Windows service via the SCM (best effort)."""
        try:
            import subprocess

            result = subprocess.run(
                ["sc", "stop", sname],
                capture_output=True,
                text=True,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            if result.returncode == 0:
                self._log("kill", f"Stopped service '{sname}'")
                return True
            self._log("warn", f"Could not stop service '{sname}' (needs admin?)")
            return False
        except Exception as exc:
            self._log("error", f"Service stop failed for '{sname}': {exc}")
            return False

    # ------------------------------------------------------------------ #
    # Read-only helpers for the GUI
    # ------------------------------------------------------------------ #
    def preview(self) -> List[Dict[str, object]]:
        """Return what the current mode WOULD close, without killing anything.

        Used by the GUI's "Preview" button so the user can sanity-check a mode
        (especially Nuclear) before arming the toggle.
        """
        whitelist = set(self.config.whitelist)
        blacklist = set(self.config.blacklist)
        rows: List[Dict[str, object]] = []
        for proc in psutil.process_iter(["pid", "name"]):
            try:
                raw_name = proc.info.get("name") or ""
                name = KL.normalize(raw_name)
                pid = proc.info.get("pid")
                if not name or pid is None:
                    continue
                if self._is_protected(name, pid, whitelist):
                    continue
                if self._should_kill(name, whitelist, blacklist):
                    rows.append({"name": raw_name, "pid": pid})
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        rows.sort(key=lambda r: str(r["name"]).lower())
        return rows

    def list_running(self) -> List[Dict[str, object]]:
        """Return a snapshot of running processes for the GUI viewer."""
        rows: List[Dict[str, object]] = []
        for proc in psutil.process_iter(["pid", "name"]):
            try:
                raw_name = proc.info.get("name") or ""
                pid = proc.info.get("pid")
                if not raw_name or pid is None:
                    continue
                rows.append({"name": raw_name, "pid": pid})
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        rows.sort(key=lambda r: str(r["name"]).lower())
        return rows
