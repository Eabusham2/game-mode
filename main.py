#!/usr/bin/env python3
"""GameMode launcher.

Run the desktop app with:

    python main.py

or, equivalently:

    python -m gamemode
"""

from __future__ import annotations

import sys

from gamemode.gui import main

if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
