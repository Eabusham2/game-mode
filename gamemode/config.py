"""Persistent user configuration for GameMode.

Settings are stored as a small JSON file under the user's home directory
(``~/.gamemode/config.json``) so they survive restarts.  The :class:`Config`
object is a thin, validated wrapper around that JSON document.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field, asdict
from typing import List

# The four operating modes, in ascending order of aggressiveness.
MODE_SMART = "smart"
MODE_AGGRESSIVE = "aggressive"
MODE_RISK = "risk"
MODE_NUCLEAR = "nuclear"

ALL_MODES = (MODE_SMART, MODE_AGGRESSIVE, MODE_RISK, MODE_NUCLEAR)

MODE_LABELS = {
    MODE_SMART: "Smart (safe clean-up)",
    MODE_AGGRESSIVE: "Aggressive (close background apps)",
    MODE_RISK: "Risk (whitelist-leaning)",
    MODE_NUCLEAR: "Nuclear (riskiest - whitelist only)",
}

MODE_DESCRIPTIONS = {
    MODE_SMART: (
        "Closes only confirmed junk: Adobe helpers, updaters, telemetry and "
        "vendor tray bloat, plus anything on your blacklist. Safe for daily use."
    ),
    MODE_AGGRESSIVE: (
        "Everything Smart does, plus heavier background apps (Office, Spotify, "
        "Slack, Zoom, cloud sync, chat apps). Browsers and your games stay open."
    ),
    MODE_RISK: (
        "Whitelist-leaning. Keeps Windows essentials, anti-cheats, game "
        "launchers, Discord, browsers/dev tools and your whitelist - kills "
        "everything else it finds."
    ),
    MODE_NUCLEAR: (
        "RISKIEST. Keeps ONLY Windows essentials, anti-cheats, detected games "
        "& launchers, Discord, GameMode itself and your whitelist. Closes "
        "absolutely everything else, including browsers. Use with care."
    ),
}

CONFIG_DIR = os.path.join(os.path.expanduser("~"), ".gamemode")
CONFIG_PATH = os.path.join(CONFIG_DIR, "config.json")


@dataclass
class Config:
    """All persisted user preferences."""

    mode: str = MODE_SMART
    # Seconds between scans when the engine is running.
    scan_interval: float = 3.0
    # User-defined process names that must never be killed (lower-case).
    whitelist: List[str] = field(default_factory=list)
    # User-defined process names that should always be killed (lower-case).
    blacklist: List[str] = field(default_factory=list)
    # When true the engine logs what it *would* kill but does not kill it.
    dry_run: bool = False
    # When true, Risk/Nuclear also stop non-essential Windows services.
    manage_services: bool = False
    # Remember the toggle state so the engine can auto-resume on launch.
    enabled_on_start: bool = False

    # -- (de)serialisation --------------------------------------------------
    def normalized(self) -> "Config":
        """Return a copy with lists lower-cased and de-duplicated."""
        self.whitelist = sorted({p.strip().lower() for p in self.whitelist if p.strip()})
        self.blacklist = sorted({p.strip().lower() for p in self.blacklist if p.strip()})
        if self.mode not in ALL_MODES:
            self.mode = MODE_SMART
        self.scan_interval = max(1.0, min(60.0, float(self.scan_interval)))
        return self

    def save(self, path: str = CONFIG_PATH) -> None:
        self.normalized()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(asdict(self), fh, indent=2, sort_keys=True)
        os.replace(tmp, path)  # atomic write

    @classmethod
    def load(cls, path: str = CONFIG_PATH) -> "Config":
        if not os.path.exists(path):
            return cls()
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except (json.JSONDecodeError, OSError):
            # Corrupt config should never stop the app from launching.
            return cls()
        # Only accept keys we know about.
        known = {f for f in cls().__dataclass_fields__}  # type: ignore[attr-defined]
        clean = {k: v for k, v in data.items() if k in known}
        return cls(**clean).normalized()
