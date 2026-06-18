"""Curated, built-in knowledge about processes and services.

Every name in this module is stored **lower-case, without path** so the
engine can compare against ``psutil`` process names cheaply and case
insensitively.

The single most important set here is :data:`PROTECTED_SYSTEM`.  Anything in
it is *never* terminated by GameMode, in any mode, even if the user puts it on
their blacklist.  Killing those processes would freeze or blue-screen Windows,
so they are treated as inviolable.
"""

from __future__ import annotations

# ---------------------------------------------------------------------------
# CRITICAL OPERATING-SYSTEM PROCESSES - NEVER TERMINATE THESE
# ---------------------------------------------------------------------------
# Terminating any of these will, at best, log the user out and, at worst,
# crash Windows.  They are protected unconditionally - the blacklist cannot
# override this set.  This is the program's primary safety net.
PROTECTED_SYSTEM = {
    # Kernel / session core
    "system",
    "system idle process",
    "registry",
    "memory compression",
    "secure system",
    "smss.exe",
    "csrss.exe",
    "wininit.exe",
    "winlogon.exe",
    "services.exe",
    "lsass.exe",
    "lsaiso.exe",
    "svchost.exe",
    "fontdrvhost.exe",
    "dwm.exe",            # Desktop Window Manager - killing it kills the UI
    "explorer.exe",       # Taskbar / desktop shell
    "sihost.exe",
    "taskhostw.exe",
    "ctfmon.exe",
    "conhost.exe",
    "dllhost.exe",
    "runtimebroker.exe",
    "wudfhost.exe",
    "audiodg.exe",        # Audio - killing it can wedge sound stack
    "spoolsv.exe",        # Print spooler
    "wmiprvse.exe",
    "wmiapsrv.exe",
    "searchindexer.exe",
    "searchhost.exe",
    "startmenuexperiencehost.exe",
    "shellexperiencehost.exe",
    "applicationframehost.exe",
    "textinputhost.exe",
    "lockapp.exe",
    "logonui.exe",
    "userinit.exe",
    "wlanext.exe",
    "spppsvc.exe",
    "smartscreen.exe",
    # Windows Defender / Security
    "msmpeng.exe",
    "nissrv.exe",
    "mpdefendercoreservice.exe",
    "securityhealthservice.exe",
    "securityhealthsystray.exe",
    "mpcmdrun.exe",
    # Python interpreters (so GameMode never kills its own runtime).  The
    # engine also protects its own PID explicitly, this is belt-and-braces.
    "python.exe",
    "pythonw.exe",
    "py.exe",
}

# ---------------------------------------------------------------------------
# ANTI-CHEAT ENGINES - protected in every mode (killing them bans accounts /
# crashes the protected game).
# ---------------------------------------------------------------------------
ANTI_CHEAT = {
    "easyanticheat.exe",
    "easyanticheat_eos.exe",
    "easyanticheat_setup.exe",
    "beservice.exe",            # BattlEye
    "beservice_x64.exe",
    "bedaisy.exe",
    "vgc.exe",                  # Riot Vanguard
    "vgtray.exe",
    "vanguard.exe",
    "pnkbstra.exe",             # PunkBuster
    "pnkbstrb.exe",
    "gamemon.exe",              # nProtect GameGuard
    "gamemon64.des",
    "xhunter1.sys",
    "faceitclient.exe",         # FACEIT AC
    "faceit.exe",
    "esea.exe",
    "eseaclient.exe",
    "mhyprot.exe",              # mihoyo / Genshin
    "ricochet.exe",
}

# ---------------------------------------------------------------------------
# GAME LAUNCHERS & STORES - kept in Risk / Nuclear modes so games keep working.
# Actual game executables are too numerous to enumerate; users add theirs to
# the whitelist, but the launchers below are recognised automatically.
# ---------------------------------------------------------------------------
GAME_PLATFORMS = {
    "steam.exe",
    "steamwebhelper.exe",
    "steamservice.exe",
    "epicgameslauncher.exe",
    "epicwebhelper.exe",
    "egslauncher.exe",
    "battle.net.exe",
    "battle.net helper.exe",
    "agent.exe",                # Blizzard update agent
    "riotclientservices.exe",
    "riotclientux.exe",
    "riotclientuxrender.exe",
    "leagueclient.exe",
    "leagueclientux.exe",
    "valorant.exe",
    "galaxyclient.exe",         # GOG Galaxy
    "galaxyclienthelper.exe",
    "origin.exe",
    "eadesktop.exe",
    "easteamservice.exe",
    "ubisoftconnect.exe",
    "upc.exe",
    "uplaywebcore.exe",
    "rockstargames launcher.exe",
    "rockstarservice.exe",
    "playgameservices.exe",
    "xboxapp.exe",
    "gamingservices.exe",
    "gamebar.exe",
    "itch.exe",
    "minecraftlauncher.exe",
    "javaw.exe",                # many games / Minecraft run on this
}

# ---------------------------------------------------------------------------
# COMMUNICATION / "KEEP WHILE GAMING" - Discord etc. are kept in Risk mode and
# (per the request) in Nuclear mode too.
# ---------------------------------------------------------------------------
KEEP_WHILE_GAMING = {
    "discord.exe",
    "discordptb.exe",
    "discordcanary.exe",
    "discorddevelopment.exe",
    "teamspeak3.exe",
    "ts3client_win64.exe",
    "mumble.exe",
    "overwolf.exe",
}

# ---------------------------------------------------------------------------
# CONFIRMED BLOATWARE / UPDATERS / BACKGROUND JUNK - killed in EVERY active
# mode (Smart and up).  These are well-known, safe-to-close helpers that serve
# no purpose during a gaming/work session and silently respawn anyway.
# ---------------------------------------------------------------------------
BLOATWARE = {
    # Adobe -----------------------------------------------------------------
    "adobeupdateservice.exe",
    "adobe desktop service.exe",
    "adobeipcbroker.exe",
    "adobe cef helper.exe",
    "adobenotificationclient.exe",
    "adobecollabsync.exe",
    "adobe crash processor.exe",
    "creative cloud.exe",
    "creative cloud helper.exe",
    "creative cloud ux helper.exe",
    "ccxprocess.exe",
    "cclibrary.exe",
    "agsservice.exe",
    "agmservice.exe",
    "armsvc.exe",
    "adobearm.exe",
    "acrotray.exe",
    "acrobat.exe",          # PDF reader - not needed while gaming
    "acrord32.exe",
    "node.exe",             # Adobe ships background node helpers (CCX)
    # Generic updaters ------------------------------------------------------
    "googleupdate.exe",
    "googlecrashhandler.exe",
    "googlecrashhandler64.exe",
    "googleupdaterservice.exe",
    "microsoftedgeupdate.exe",
    "edgeupdate.exe",
    "jusched.exe",          # Java updater
    "jucheck.exe",
    "officeclicktorun.exe",
    "msoia.exe",
    "setup.exe",
    # Microsoft consumer fluff ---------------------------------------------
    "onedrive.exe",
    "onedrivestandaloneupdater.exe",
    "yourphone.exe",
    "phoneexperiencehost.exe",
    "widgets.exe",
    "widgetservice.exe",
    "gamebarftserver.exe",
    "gamebarpresencewriter.exe",
    "cortana.exe",
    "skype.exe",
    "skypeapp.exe",
    "skypehost.exe",
    "ms-teams.exe",
    "teams.exe",
    "feedbackhub.exe",
    "gettingstarted.exe",
    "people.exe",
    # Vendor tray / helper junk --------------------------------------------
    "spotifywebhelper.exe",
    "ggvampire.exe",
    "asusupdate.exe",
    "armourycrate.exe",
    "armorycrate.service.exe",
    "lightingservice.exe",
    "icue.exe",
    "razer synapse.exe",
    "razer central.exe",
    "rzsdkservice.exe",
    "logioptionsplus_agent.exe",
    "lghub.exe",
    "lghub_agent.exe",
    "msiafterburner.exe",   # users may whitelist; tray helper otherwise
    "rivatuner statistics server.exe",
    "ccc.exe",
    "nvcontainer.exe",      # NVIDIA - usually wanted; left out of risk kill
    "wallpaper engine.exe",
    "wallpaper32.exe",
    "wallpaper64.exe",
}

# ---------------------------------------------------------------------------
# EXTRA "NOISE" - heavier background apps closed from Aggressive mode upward.
# These are real apps a user might be using, so they are NOT touched in Smart
# mode, only when the user opts into a more aggressive profile.
# ---------------------------------------------------------------------------
BACKGROUND_NOISE = {
    "slack.exe",
    "zoom.exe",
    "outlook.exe",
    "winword.exe",
    "excel.exe",
    "powerpnt.exe",
    "onenote.exe",
    "spotify.exe",
    "itunes.exe",
    "dropbox.exe",
    "googledrivefs.exe",
    "googledrivesync.exe",
    "evernote.exe",
    "notion.exe",
    "telegram.exe",
    "whatsapp.exe",
    "signal.exe",
    "obs64.exe",
    "obs32.exe",
    "vlc.exe",
    "calculator.exe",
    "notepad.exe",
    "stickynot.exe",
    "stickynotes.exe",
}

# ---------------------------------------------------------------------------
# COMMON, GENERALLY-USEFUL APPS - kept in Risk mode (so the desktop stays
# usable) but closed in Nuclear mode.  Browsers and dev tools live here.
# ---------------------------------------------------------------------------
COMMON_APPS = {
    "chrome.exe",
    "msedge.exe",
    "firefox.exe",
    "brave.exe",
    "opera.exe",
    "opera_gx.exe",
    "vivaldi.exe",
    "code.exe",
    "devenv.exe",
    "windowsterminal.exe",
    "cmd.exe",
    "powershell.exe",
    "pwsh.exe",
    "wt.exe",
}

# ---------------------------------------------------------------------------
# ESSENTIAL WINDOWS SERVICES - never stopped when "manage services" is on.
# Stored as lower-case service *names* (not display names).
# ---------------------------------------------------------------------------
ESSENTIAL_SERVICES = {
    "audiosrv", "audioendpointbuilder", "rpcss", "rpceptmapper", "dcomlaunch",
    "lsm", "samss", "schedule", "themes", "winmgmt", "eventlog", "eventsystem",
    "plugplay", "power", "profsvc", "usermanager", "dnscache", "nsi",
    "netprofm", "nlasvc", "dhcp", "wlansvc", "lanmanworkstation",
    "lanmanserver", "cryptsvc", "bfe", "mpssvc", "wscsvc", "windefend",
    "wuauserv", "trustedinstaller", "wsearch", "spooler", "bits", "gpsvc",
    "fontcache", "tabletinputservice", "textinputmanagementservice",
    "stisvc", "shellhwdetection", "systemeventsbroker", "timebrokersvc",
    "coremessagingregistrar", "camsvc", "appinfo", "seclogon", "browser",
    "key iso", "keyiso", "dps", "wdiservicehost", "wdisystemhost",
    "ngc", "ngccontainer", "vaultsvc", "sens", "uso", "usosvc",
}


def normalize(name: str) -> str:
    """Return a process/service name in the canonical comparison form."""
    return (name or "").strip().lower()
