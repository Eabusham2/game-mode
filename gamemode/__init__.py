"""GameMode - a full-fledged system resource optimizer / task killer.

GameMode continuously detects and disables useless background programs,
bloatware, updaters and (optionally) Windows services while you game or work.
It offers several escalating modes, from a gentle "Smart" clean-up to a
"Nuclear" whitelist-only mode that keeps only the essentials.

The package is intentionally split into small modules:

* ``known_lists`` - the curated, built-in knowledge about what is safe to
  kill, what must never be touched, games, anti-cheats, bloatware, etc.
* ``config``      - persistent user settings (mode, interval, whitelist,
  blacklist, options) stored as JSON.
* ``engine``      - the background worker that scans processes/services and
  terminates them according to the active mode and the safety rules.
* ``gui``         - the tkinter desktop interface that drives the engine.
"""

__version__ = "1.0.0"
__all__ = ["__version__"]
