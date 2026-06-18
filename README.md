# 🎮 GameMode — System Optimizer

A full-fledged desktop app that **continuously detects and disables useless
programs, bloatware, updaters and background apps** with a single master
switch — so your CPU, RAM and disk go to your game (or your work) instead of to
Adobe updaters and vendor tray junk.

Flip it **ON** and GameMode keeps watch *non-stop*, closing the targeted
programs every few seconds (they tend to silently respawn — GameMode just keeps
closing them). Flip it **OFF** and everything is left alone.

> **Platform:** Built for **Windows** (process names, services and anti-cheat
> detection are Windows-oriented). It imports and partially runs on Linux/macOS
> for development, but the curated kill/keep lists target Windows.

---

## ✨ Features

- **One master toggle** — a big ON/OFF switch that arms/disarms continuous
  enforcement.
- **Four intelligent modes**, from gentle to nuclear (see below).
- **Custom whitelist & blacklist** for end-task, edited in the GUI and saved
  between runs.
- **Built-in knowledge** of common bloatware (Adobe Creative Cloud helpers,
  updaters, telemetry, vendor tray apps), game launchers, **anti-cheats**,
  Discord and the essential Windows processes/services.
- **Riskiest "Nuclear" mode** — keeps *only* Windows essentials, anti-cheats,
  detected games & launchers, Discord, GameMode itself and your whitelist, and
  closes **everything else**.
- **Optional Windows service sweeping** in Risk/Nuclear (non-essential
  auto/manual services only; needs Administrator).
- **Dry-run** and **Preview** — see exactly what a mode *would* close before you
  arm it. Nothing gets killed until you're sure.
- **Live activity log** and running counters in the GUI.
- **Headless CLI** for servers/scripting/testing.

---

## 🛡️ Safety first

GameMode is built so it **cannot break your PC**. Every single termination
decision passes through one guard that protects, *unconditionally and in every
mode (even Nuclear, even if you blacklist them)*:

- Critical OS processes (`csrss`, `wininit`, `winlogon`, `services`, `lsass`,
  `svchost`, `dwm`, `explorer`, the kernel, Windows Defender, …)
- **Anti-cheat engines** (EasyAntiCheat, BattlEye, Vanguard, PunkBuster, …) — so
  you never get kicked or banned mid-match.
- **GameMode itself** and its whole process tree (and its launching shell).
- Anything on **your whitelist**.

The blacklist can never override these protections.

---

## 🎚️ The four modes

| Mode | What it closes |
|------|----------------|
| **Smart** *(safe)* | Only confirmed junk: Adobe helpers, updaters, telemetry, vendor tray bloat + your blacklist. Safe for everyday use. |
| **Aggressive** | Smart **plus** heavier background apps — Office, Spotify, Slack, Zoom, cloud sync, chat apps. Browsers and games stay open. |
| **Risk** | Whitelist-leaning. Keeps Windows essentials, anti-cheats, game launchers, Discord, browsers/dev tools and your whitelist — closes everything else. |
| **Nuclear** *(riskiest)* | Keeps **only** Windows essentials, anti-cheats, detected games & launchers, Discord, GameMode and your whitelist. Closes absolutely everything else, including your browser. |

---

## 🚀 Install & run

Requires **Python 3.8+**. The GUI uses `tkinter` (bundled with the standard
Python installer on Windows; on Linux install `python3-tk`).

```bash
pip install -r requirements.txt

# Launch the desktop app:
python main.py
#   or
python -m gamemode
```

For full power (closing protected apps / stopping services) run your terminal
**as Administrator** on Windows.

### Headless CLI

```bash
# Preview what a mode would close — kills nothing:
python -m gamemode.cli preview --mode nuclear

# Run continuously until Ctrl+C:
python -m gamemode.cli run --mode smart --interval 5

# Safe rehearsal — report only:
python -m gamemode.cli run --mode aggressive --dry-run

# List running processes:
python -m gamemode.cli list
```

---

## 🧭 Recommended first run

1. Start on **Smart** mode.
2. Open the **Processes** tab and click **Preview kills for current mode** (or
   tick **Dry run** on the Dashboard) to see what would close.
3. Add anything you want to keep to the **Whitelist**; add anything you always
   want gone to the **Blacklist**.
4. Flip the master switch **ON**.
5. Escalate to **Aggressive / Risk / Nuclear** only once you've previewed them.

---

## 🗂️ Project layout

```
main.py                 # launcher → GUI
gamemode/
  __init__.py
  __main__.py           # python -m gamemode
  known_lists.py        # curated keep/kill knowledge (the safety net)
  config.py             # persistent settings (~/.gamemode/config.json)
  engine.py             # background detector/terminator + safety guards
  gui.py                # tkinter desktop interface
  cli.py                # headless command-line interface
requirements.txt
```

Settings are saved to `~/.gamemode/config.json`.

---

## ⚠️ Disclaimer

GameMode force-closes programs. **Save your work first.** Closing apps can lose
unsaved data; stopping services may affect system features until the next
reboot. Use **Preview** / **Dry run** until you trust your configuration. You
are responsible for what you put on your blacklist and which mode you arm.
