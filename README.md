# 🎮 GameMode — System Optimizer (native C / Win32)

A full-fledged **native Windows desktop app** — pure C on the Win32 API, **no
runtime, no dependencies, a single self-contained `.exe`** — that continuously
detects and disables useless background programs, bloatware, updaters and
(optionally) non-essential Windows services with one master switch.

Flip it **ON** and GameMode keeps watch *non-stop*, closing the targeted
programs every few seconds (they tend to silently respawn — GameMode just keeps
closing them). Flip it **OFF** and everything is left alone.

> **Platform:** Windows 7 → 11 (x64). Uses Toolhelp for process enumeration,
> the Service Control Manager for services, and native Win32 controls for the
> GUI. No .NET, no Python, no external libraries.

---

## ✨ Features

- **One master toggle** — a big ON/OFF switch that arms/disarms continuous
  enforcement on a background worker thread.
- **Four intelligent modes**, from gentle to nuclear (see below).
- **Custom whitelist & blacklist** for end-task, edited in the GUI and saved
  between runs (`%USERPROFILE%\.gamemode\config.ini`).
- **Built-in knowledge** of common bloatware (Adobe Creative Cloud helpers,
  updaters, telemetry, vendor tray apps), game launchers, **anti-cheats**,
  Discord and the essential Windows processes/services.
- **Riskiest "Nuclear" mode** — keeps *only* Windows essentials, anti-cheats,
  detected games & launchers, Discord, GameMode itself and your whitelist, and
  closes **everything else**.
- **Optional Windows service sweeping** in Risk/Nuclear (non-essential,
  stoppable services only; uses the Service Control Manager — needs admin).
- **Dry-run** and **Preview** — see exactly what a mode *would* close before you
  arm it. Nothing gets killed until you're sure.
- **Live activity log** and running counters in the GUI.

---

## 🛡️ Safety first

GameMode is built so it **cannot break your PC**. Every single termination
decision flows through one guard (`is_protected()` in `src/engine.c`) that
protects, *unconditionally and in every mode — even Nuclear, even if you
blacklist them*:

- Critical OS processes (`csrss`, `wininit`, `winlogon`, `services`, `lsass`,
  `svchost`, `dwm`, `explorer`, the kernel, Windows Defender, …) and PIDs ≤ 4.
- **Anti-cheat engines** (EasyAntiCheat, BattlEye, Vanguard, PunkBuster, …) — so
  you never get kicked or banned mid-match.
- **GameMode itself** — its own PID plus its ancestors and all descendants
  (computed from the process tree each scan), so it can never terminate itself
  or the shell that launched it.
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

## 🔨 Build

The app links only against standard Windows system libraries
(`user32`, `gdi32`, `comctl32`, `advapi32`, `shell32`, `ole32`).

### MSVC (Visual Studio)

Open a *Developer Command Prompt for VS* and run:

```bat
build.bat
```

→ produces `GameMode.exe`.

### MinGW-w64 (on Windows)

```sh
mingw32-make
```

### Cross-compile from Linux/macOS (MinGW-w64)

```sh
make CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres
```

All three produce a single self-contained **`GameMode.exe`**.

---

## ▶️ Run

Double-click **`GameMode.exe`**. Because it closes other programs and can stop
services, the embedded manifest requests **Administrator** elevation (you'll see
a UAC prompt). Running elevated is what lets it close protected/other-user
processes and stop services.

**Recommended first run**

1. Start on **Smart** mode.
2. Open the **Processes** tab → **Preview kills** (or tick **Dry run** on the
   Dashboard) to see what would close.
3. Add anything you want to keep to the **Whitelist**; add anything you always
   want gone to the **Blacklist**.
4. Flip the master switch **ON**.
5. Escalate to **Aggressive / Risk / Nuclear** only once you've previewed them.

---

## 🗂️ Project layout

```
src/
  gamemode.h      # shared decls, control IDs, app constants
  known_lists.h/.c# curated keep/kill knowledge (the safety net)
  modes.c         # mode labels & descriptions
  config.h/.c     # settings persistence (INI) + dynamic string list
  engine.h/.c     # process/service detection + termination + safety guards
  gui.c           # native Win32 GUI + WinMain entry point
app.manifest      # visual styles + admin elevation + DPI awareness
resource.rc       # embeds the manifest + version info
Makefile          # MinGW / cross build
build.bat         # MSVC build
```

Settings are saved to `%USERPROFILE%\.gamemode\config.ini`.

---

## ⚠️ Disclaimer

GameMode force-closes programs with `TerminateProcess` (no graceful save).
**Save your work first.** Closing apps can lose unsaved data; stopping services
may affect system features until the next reboot. Use **Preview** / **Dry run**
until you trust your configuration. You are responsible for what you put on your
blacklist and which mode you arm.
