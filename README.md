# 🎮 GameMode — System Optimizer (native C / Win32)

[![Build GameMode.exe](https://github.com/Eabusham2/game-mode-/actions/workflows/build.yml/badge.svg)](https://github.com/Eabusham2/game-mode-/actions/workflows/build.yml)

A full-fledged **native Windows desktop app** — pure C on the Win32 API, **no
runtime, no dependencies, a single self-contained `.exe`** — that continuously
detects and disables useless background programs, bloatware, updaters,
telemetry tasks and (optionally) non-essential Windows services with one master
switch.

Flip it **ON** and GameMode keeps watch *non-stop*, closing the targeted
programs every few seconds (they tend to silently respawn — GameMode just keeps
closing them). Flip it **OFF** and everything is left alone.

> **Platform:** Windows 7 → 11 (x64). Uses Toolhelp for process enumeration,
> PSAPI for per-process memory, the Service Control Manager for services, and
> native Win32 controls for the GUI. No .NET, no Python, no external libraries.

---

## ✨ Features

- **One master toggle** — a big owner-drawn green/red ON/OFF switch driving a
  background worker thread. Also bound to a **global hotkey: `Ctrl+Alt+G`**.
- **Four intelligent modes**, from gentle to nuclear (see below).
- **Smart heuristic detection** — beyond hardcoded names, GameMode recognises
  *windowless background helpers* whose names match updater / crash-handler /
  telemetry patterns (`*update*`, `*updater*`, `crashhandler`, `telemetry`,
  `compattel`, `census`, …), so it closes junk from vendors that were never
  hard-coded — while sparing anything you have a window open for.
- **Preset toggles** — a Presets tab of opinionated "common targets" you can
  tick to **close** or untick to **keep** (Game Bar, Cortana, Widgets, Teams,
  Skype, Adobe CC, Office tasks, OneDrive, Phone Link, Spotify, Edge). A preset
  overrides the built-in lists either way, so you decide what some people like
  and others don't.
- **Custom whitelist & blacklist** for end-task, edited in the GUI and saved
  between runs (`%USERPROFILE%\.gamemode\config.ini`) — with a **"Pick from
  running tasks" picker** (a checkbox list of live processes) so you never have
  to type exe names. **Precedence** below the hard-protected tier is
  **blacklist → presets → mode rules**, so a name you blacklist is always closed
  even if a preset would keep it.
- **Temporary by design / restore on OFF** — GameMode only *stops* and *closes*
  things while ON; it **never** disables anything permanently (no startup or
  service-start-type changes). When you turn it **OFF** it can **restart the
  services it stopped** and optionally **reopen the apps it closed**.
- **Built-in knowledge** of common bloatware (Adobe Creative Cloud helpers,
  updaters, vendor tray apps), Windows telemetry tasks (CompatTelRunner,
  DeviceCensus, …), game launchers, **anti-cheats**, Discord and the essential
  Windows processes/services.
- **Riskiest "Nuclear" mode** — keeps *only* Windows essentials, anti-cheats,
  detected games & launchers, Discord, GameMode itself and your whitelist, and
  closes **everything else**.
- **Optional Windows service sweeping** in Risk/Nuclear via the Service Control
  Manager (non-essential, stoppable services only — covers telemetry services
  like DiagTrack, DoSvc, SysMain, WerSvc, PcaSvc, CDPSvc, MapsBroker; needs
  admin). Services are only **stopped** (temporary) and are restarted on OFF —
  their start type is never changed to disabled.
- **Process viewer** — sortable ListView (Process / PID / RAM / Fate) with a
  live "would-be-closed" preview, plus **manual "Kill selected"** and one-click
  **send-to-whitelist / send-to-blacklist**.
- **System tray integration** — minimise to tray and keep running, tray menu
  (show, toggle, switch mode, exit), and **coalesced balloon notifications**
  when things are closed in the background.
- **Run at Windows startup** (one checkbox; writes the `HKCU…\Run` value).
- **Dry-run** + **Preview** so you can see exactly what a mode would close
  before arming it.
- **Colour-coded live activity log** (RichEdit) and running counters, including
  **total RAM freed**.

---

## 🛡️ Safety first

GameMode is built so it **cannot break your PC**. Kill decisions flow through a
two-tier guard in `src/engine.c`:

**Hard-protected — never closed, in any mode, even if you blacklist them:**
- Critical OS processes (`csrss`, `wininit`, `winlogon`, `services`, `lsass`,
  `svchost`, `dwm`, `explorer`, the kernel, Windows Defender, …) and PIDs ≤ 4.
- **Anti-cheat engines** (EasyAntiCheat, BattlEye, Vanguard, PunkBuster, …) — so
  you never get kicked or banned mid-match.
- **GameMode itself** — its own PID plus its ancestors and all descendants.
- The **foreground application** — the app you are actively using is never
  closed out from under you.
- Anything on **your whitelist**.

**Soft-system — shielded from heuristics and from the Risk/Nuclear allow-list,
so the OS is never dismantled:**
- Anything whose executable lives under `%WinDir%`. (Explicit curated lists and
  your blacklist can still target these, e.g. CompatTelRunner.)

The blacklist can never override the hard-protected tier.

---

## 🎚️ The four modes

| Mode | What it closes |
|------|----------------|
| **Smart** *(safe)* | Confirmed junk (Adobe helpers, updaters, telemetry, vendor bloat) **+ smart heuristic** windowless updaters/crash-handlers **+** your blacklist. Safe for everyday use. |
| **Aggressive** | Smart **plus** heavier background apps (Office, Spotify, Slack, Zoom, cloud sync, chat) and a broader heuristic (helper/agent/tray/sync). Browsers and games stay open. |
| **Risk** | Whitelist-leaning. Keeps Windows essentials, anti-cheats, game launchers, Discord, browsers/dev tools and your whitelist — closes everything else (except OS files under `%WinDir%`). |
| **Nuclear** *(riskiest)* | Keeps **only** Windows essentials, anti-cheats, detected games & launchers, Discord, GameMode and your whitelist. Closes absolutely everything else, including your browser. |

---

## 🔨 Build

Links only against standard Windows system libraries (`user32`, `gdi32`,
`comctl32`, `advapi32`, `shell32`, `ole32`, `psapi`).

```bat
build.bat                 :: MSVC (from a Developer Command Prompt)
```
```sh
mingw32-make              # MinGW-w64 on Windows
# cross-compile from Linux/macOS:
make CC=x86_64-w64-mingw32-gcc WINDRES=x86_64-w64-mingw32-windres
```

All produce a single self-contained **`GameMode.exe`**.

### Don't want to build it yourself?

**Download the ready-to-run exe — no login, no unzip:**

➡️ **[`GameMode.exe` (latest build)](https://github.com/Eabusham2/game-mode-/releases/download/latest/GameMode.exe)**

GitHub Actions (`.github/workflows/build.yml`) rebuilds the exe on every push to
`main` and republishes it to the **["Latest build (rolling)"
release](https://github.com/Eabusham2/game-mode-/releases/tag/latest)**, so the
link above always points at the newest binary.

Prefer a pinned version? On a version tag (`vX.Y.Z`) the same workflow publishes
a matching [versioned Release](https://github.com/Eabusham2/game-mode-/releases).
You can also grab the raw `GameMode-windows-x64` build artifact from any green
run on the
[Actions tab](https://github.com/Eabusham2/game-mode-/actions/workflows/build.yml).

The app icon is generated (no binary committed dependency needed to regenerate):
```sh
python3 scripts/make_icon.py   # rewrites assets/icon.ico
```

---

## ▶️ Run

Double-click **`GameMode.exe`**. Because it closes other programs and can stop
services, the embedded manifest requests **Administrator** elevation (UAC).

**Recommended first run**

1. Start on **Smart** mode.
2. Open the **Processes** tab → **Preview kills** (or tick **Dry run**) to see
   what would close.
3. Add anything you want to keep to the **Whitelist**; add anything you always
   want gone to the **Blacklist** (or select rows in the Processes tab and use
   the send buttons).
4. Flip the master switch **ON** (or press `Ctrl+Alt+G`).
5. Escalate to **Aggressive / Risk / Nuclear** only once you've previewed them.

---

## 🗂️ Project layout

```
src/
  gamemode.h      # shared decls, control IDs, app constants
  known_lists.h/.c# curated keep/kill knowledge + heuristic patterns
  modes.c         # mode labels & descriptions
  config.h/.c     # settings persistence (INI) + dynamic string list
  engine.h/.c     # detection + termination + safety guards + worker thread
  gui.c           # native Win32 GUI (tray, hotkey, RichEdit, ListView) + WinMain
app.manifest      # visual styles + admin elevation + DPI awareness
resource.rc       # embeds the manifest, icon and version info
assets/icon.ico   # app icon (generated by scripts/make_icon.py)
Makefile          # MinGW / cross build
build.bat         # MSVC build
```

Settings are saved to `%USERPROFILE%\.gamemode\config.ini`.

---

## 🙏 Inspiration

The curated bloatware/telemetry targets are informed by well-known open-source
Windows debloat/optimisation projects (process & service names cross-checked
against them):

- [Win11Debloat](https://github.com/Raphire/Win11Debloat)
- [LeDragoX/Win-Debloat-Tools](https://github.com/LeDragoX/Win-Debloat-Tools)
- [ckdvs99/windows-debloat](https://github.com/ckdvs99/windows-debloat)

GameMode differs from those in that it runs **continuously and reversibly** (a
live toggle that closes processes each scan) rather than making permanent
registry/service changes — and it is laser-focused on safety for gaming
(foreground-app, anti-cheat and `%WinDir%` protection).

Sources consulted: [borncity overview](https://borncity.com/win/2025/03/23/windows-10-11-open-source-debloat-tool/),
[Win11Debloat](https://github.com/Raphire/Win11Debloat),
[Win-Debloat-Tools](https://github.com/LeDragoX/Win-Debloat-Tools),
[windows-debloat](https://github.com/ckdvs99/windows-debloat).

---

## ⚠️ Disclaimer

GameMode force-closes programs with `TerminateProcess` (no graceful save).
**Save your work first.** Closing apps can lose unsaved data; stopping services
may affect system features until the next reboot. Use **Preview** / **Dry run**
until you trust your configuration. You are responsible for what you put on your
blacklist and which mode you arm.
