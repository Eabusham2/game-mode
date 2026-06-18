"""Headless command-line interface for GameMode.

Useful when there is no display (or for scripting/testing). It shares the same
engine, config and safety guards as the GUI.

Examples
--------
    python -m gamemode.cli preview --mode nuclear
    python -m gamemode.cli run --mode smart --interval 5
    python -m gamemode.cli run --mode aggressive --dry-run
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import List

from .config import Config, ALL_MODES, MODE_LABELS
from .engine import Engine


def _print_log(level: str, message: str) -> None:
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] {level.upper():5} {message}")


def _build_config(args: argparse.Namespace) -> Config:
    cfg = Config.load()
    if args.mode:
        cfg.mode = args.mode
    if getattr(args, "interval", None):
        cfg.scan_interval = args.interval
    if getattr(args, "dry_run", False):
        cfg.dry_run = True
    if getattr(args, "manage_services", False):
        cfg.manage_services = True
    return cfg.normalized()


def cmd_preview(args: argparse.Namespace) -> int:
    cfg = _build_config(args)
    engine = Engine(cfg)
    rows = engine.preview()
    print(f"Mode: {MODE_LABELS[cfg.mode]}")
    print(f"{len(rows)} process(es) would be closed:\n")
    for r in rows:
        print(f"  - {r['name']} (pid {r['pid']})")
    if not rows:
        print("  (nothing)")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    cfg = _build_config(args)
    engine = Engine(cfg, log=_print_log)
    print(f"Starting GameMode engine — mode={cfg.mode} "
          f"interval={cfg.scan_interval}s dry_run={cfg.dry_run}")
    print("Press Ctrl+C to stop.\n")
    engine.start()
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\nStopping…")
    finally:
        engine.stop()
    return 0


def cmd_list(args: argparse.Namespace) -> int:
    engine = Engine(Config.load())
    for r in engine.list_running():
        print(f"{r['pid']:>7}  {r['name']}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="gamemode.cli",
        description="GameMode — detect and disable useless programs/services.",
    )
    sub = p.add_subparsers(dest="command", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--mode", choices=ALL_MODES,
                        help="Override the configured mode.")

    pv = sub.add_parser("preview", parents=[common],
                        help="Show what the mode would close (kills nothing).")
    pv.set_defaults(func=cmd_preview)

    rn = sub.add_parser("run", parents=[common],
                        help="Run the engine continuously until Ctrl+C.")
    rn.add_argument("--interval", type=float, help="Seconds between scans.")
    rn.add_argument("--dry-run", action="store_true",
                    help="Report only; do not actually close anything.")
    rn.add_argument("--manage-services", action="store_true",
                    help="Also stop non-essential services (Risk/Nuclear).")
    rn.set_defaults(func=cmd_run)

    ls = sub.add_parser("list", help="List running processes.")
    ls.set_defaults(func=cmd_list)

    return p


def main(argv: List[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except BrokenPipeError:
        # Output was piped into something that closed early (e.g. `head`).
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
