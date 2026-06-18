"""Allow ``python -m gamemode`` to launch the GUI."""

from __future__ import annotations

import sys

from .gui import main

if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
