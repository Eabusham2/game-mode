/* known_lists.c - curated keep/kill data. See known_lists.h. */
#include "known_lists.h"
#include "gamemode.h"
#include <string.h>

#define LIST(name, ...) \
    static const char *const name##_items[] = { __VA_ARGS__ }; \
    const NameList name = { name##_items, (int)(sizeof(name##_items)/sizeof(char*)) }

/* CRITICAL OS PROCESSES - terminating any of these freezes or crashes Windows.
 * Protected unconditionally; the blacklist cannot override this set. */
LIST(PROTECTED_SYSTEM,
    "system", "system idle process", "registry", "memory compression",
    "secure system", "smss.exe", "csrss.exe", "wininit.exe", "winlogon.exe",
    "services.exe", "lsass.exe", "lsaiso.exe", "svchost.exe", "fontdrvhost.exe",
    "dwm.exe", "explorer.exe", "sihost.exe", "taskhostw.exe", "ctfmon.exe",
    "conhost.exe", "dllhost.exe", "runtimebroker.exe", "wudfhost.exe",
    "audiodg.exe", "spoolsv.exe", "wmiprvse.exe", "wmiapsrv.exe",
    "searchindexer.exe", "searchhost.exe", "startmenuexperiencehost.exe",
    "shellexperiencehost.exe", "applicationframehost.exe", "textinputhost.exe",
    "lockapp.exe", "logonui.exe", "userinit.exe", "wlanext.exe", "spppsvc.exe",
    "smartscreen.exe", "msmpeng.exe", "nissrv.exe", "mpdefendercoreservice.exe",
    "securityhealthservice.exe", "securityhealthsystray.exe", "mpcmdrun.exe");

/* ANTI-CHEAT - killing these bans accounts / crashes the protected game. */
LIST(ANTI_CHEAT,
    "easyanticheat.exe", "easyanticheat_eos.exe", "easyanticheat_setup.exe",
    "beservice.exe", "beservice_x64.exe", "bedaisy.exe", "vgc.exe",
    "vgtray.exe", "vanguard.exe", "pnkbstra.exe", "pnkbstrb.exe",
    "gamemon.exe", "faceitclient.exe", "faceit.exe", "esea.exe",
    "eseaclient.exe", "mhyprot.exe", "ricochet.exe");

/* GAME LAUNCHERS & STORES - kept in Risk / Nuclear so games keep working. */
LIST(GAME_PLATFORMS,
    "steam.exe", "steamwebhelper.exe", "steamservice.exe",
    "epicgameslauncher.exe", "epicwebhelper.exe", "egslauncher.exe",
    "battle.net.exe", "battle.net helper.exe", "agent.exe",
    "riotclientservices.exe", "riotclientux.exe", "riotclientuxrender.exe",
    "leagueclient.exe", "leagueclientux.exe", "valorant.exe",
    "galaxyclient.exe", "galaxyclienthelper.exe", "origin.exe",
    "eadesktop.exe", "easteamservice.exe", "ubisoftconnect.exe", "upc.exe",
    "uplaywebcore.exe", "rockstargames launcher.exe", "rockstarservice.exe",
    "xboxapp.exe", "gamingservices.exe", "itch.exe",
    "minecraftlauncher.exe", "javaw.exe");

/* Communication apps kept while gaming (Risk + Nuclear). */
LIST(KEEP_WHILE_GAMING,
    "discord.exe", "discordptb.exe", "discordcanary.exe",
    "discorddevelopment.exe", "teamspeak3.exe", "ts3client_win64.exe",
    "mumble.exe", "overwolf.exe");

/* CONFIRMED BLOATWARE / UPDATERS - killed in every active mode (Smart up). */
LIST(BLOATWARE,
    /* Adobe */
    "adobeupdateservice.exe", "adobe desktop service.exe", "adobeipcbroker.exe",
    "adobe cef helper.exe", "adobenotificationclient.exe", "adobecollabsync.exe",
    "adobe crash processor.exe", "creative cloud.exe", "creative cloud helper.exe",
    "creative cloud ux helper.exe", "ccxprocess.exe", "cclibrary.exe",
    "agsservice.exe", "agmservice.exe", "armsvc.exe", "adobearm.exe",
    "acrotray.exe", "acrobat.exe", "acrord32.exe",
    /* updaters */
    "googleupdate.exe", "googlecrashhandler.exe", "googlecrashhandler64.exe",
    "googleupdaterservice.exe", "microsoftedgeupdate.exe", "edgeupdate.exe",
    "jusched.exe", "jucheck.exe", "officeclicktorun.exe", "msoia.exe",
    "onedrivestandaloneupdater.exe",
    /* Microsoft consumer fluff */
    "onedrive.exe", "yourphone.exe", "phoneexperiencehost.exe", "widgets.exe",
    "widgetservice.exe", "gamebarftserver.exe", "gamebarpresencewriter.exe",
    "cortana.exe", "skype.exe", "skypeapp.exe", "skypehost.exe", "ms-teams.exe",
    "teams.exe", "feedbackhub.exe", "people.exe",
    /* vendor tray / helper junk */
    "spotifywebhelper.exe", "asusupdate.exe", "armourycrate.exe",
    "armorycrate.service.exe", "lightingservice.exe", "icue.exe",
    "razer synapse.exe", "razer central.exe", "rzsdkservice.exe",
    "logioptionsplus_agent.exe", "lghub.exe", "lghub_agent.exe",
    "ccc.exe", "wallpaper engine.exe", "wallpaper32.exe", "wallpaper64.exe",
    "wallpaperservice32_c.exe", "steelseriesgg.exe", "steelseriesengine3.exe",
    "nahimicsvc64.exe", "nahimicsvc32.exe", "nahimicservice.exe",
    "asusoptimization.exe", "asussoftwaremanager.exe", "myasus.exe",
    "asus_framework.exe", "rgbfusion.exe", "killerservice.exe",
    "killernetworkservice.exe", "wmiregistrationservice.exe",
    /* background webview / electron / office helpers */
    "msedgewebview2.exe", "webviewhost.exe", "officebackgroundtaskhandler.exe",
    "officehubtaskhost.exe", "skypebackgroundhost.exe", "yourphoneserver.exe",
    "gamebar.exe", "ngciconservice.exe", "cefsharp.browsersubprocess.exe",
    "dataexchangehost.exe",
    /* Windows telemetry tasks & extra updaters (inspired by open-source
       debloaters: Win11Debloat, LeDragoX/Win-Debloat-Tools, windows-debloat).
       Most pure updaters are already caught by the "update" heuristic pattern;
       these telemetry helpers carry no obvious keyword, so we name them. */
    "compattelrunner.exe", "devicecensus.exe", "wsqmcons.exe",
    "officec2rclient.exe", "adobegcclient.exe", "acrobatnotificationclient.exe",
    "dropboxupdate.exe", "operaautoupdate.exe", "vivaldi_update_notifier.exe");

/* Heavier background apps - closed from Aggressive up. */
LIST(BACKGROUND_NOISE,
    "slack.exe", "zoom.exe", "outlook.exe", "winword.exe", "excel.exe",
    "powerpnt.exe", "onenote.exe", "spotify.exe", "itunes.exe", "dropbox.exe",
    "googledrivefs.exe", "evernote.exe", "notion.exe", "telegram.exe",
    "whatsapp.exe", "signal.exe", "obs64.exe", "obs32.exe", "vlc.exe",
    "notepad.exe", "stickynotes.exe", "thunderbird.exe", "trello.exe",
    "figma.exe", "figma_agent.exe", "messenger.exe", "viber.exe",
    "wechat.exe", "qqmusic.exe", "amazon music.exe", "foxit reader.exe",
    "calculator.exe", "calc.exe", "snippingtool.exe", "screensketch.exe",
    "mspaint.exe");

/* Generally-useful apps - kept in Risk, closed in Nuclear. */
LIST(COMMON_APPS,
    "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", "opera.exe",
    "opera_gx.exe", "vivaldi.exe", "librewolf.exe", "tor.exe", "code.exe",
    "code - insiders.exe", "devenv.exe", "rider64.exe", "idea64.exe",
    "pycharm64.exe", "clion64.exe", "sublime_text.exe", "windowsterminal.exe",
    "cmd.exe", "powershell.exe", "pwsh.exe", "wt.exe", "git-bash.exe",
    "explorer.exe", "taskmgr.exe");

/* ESSENTIAL WINDOWS SERVICES - never stopped during a service sweep. */
LIST(ESSENTIAL_SERVICES,
    "audiosrv", "audioendpointbuilder", "rpcss", "rpceptmapper", "dcomlaunch",
    "lsm", "samss", "schedule", "themes", "winmgmt", "eventlog", "eventsystem",
    "plugplay", "power", "profsvc", "usermanager", "dnscache", "nsi",
    "netprofm", "nlasvc", "dhcp", "wlansvc", "lanmanworkstation",
    "lanmanserver", "cryptsvc", "bfe", "mpssvc", "wscsvc", "windefend",
    "wuauserv", "trustedinstaller", "wsearch", "spooler", "bits", "gpsvc",
    "fontcache", "tabletinputservice", "textinputmanagementservice", "stisvc",
    "shellhwdetection", "systemeventsbroker", "timebrokersvc",
    "coremessagingregistrar", "camsvc", "appinfo", "seclogon", "keyiso",
    "dps", "wdiservicehost", "vaultsvc", "sens", "usosvc");

/* HEURISTIC PATTERNS - substrings that strongly suggest a useless helper.
 * These let GameMode close updaters/telemetry from vendors we never hard-coded.
 * Anything under %WinDir%, on the keep lists, or with a visible window is
 * excluded by the engine before these are consulted (see engine.c). */
LIST(JUNK_PATTERNS_STRONG,
    "update", "updater", "autoupdate", "crashhandler", "crashpad",
    "crashreport", "crashreporter", "crashservice", "telemetry", "squirrel",
    "errorreport", "watchdog", "heartbeat", "pingsender", "metrics",
    "compattel", "census", "wsqm", "ceip");

LIST(JUNK_PATTERNS_WEAK,
    "helper", "agent", "daemon", "tray", "notify", "notification", "sync",
    "analytics", "diagnostic", "reporter", "background", "assistant",
    "webhelper", "autostart", "scheduler");

int namelist_contains(const NameList *list, const char *name_lower)
{
    int i;
    if (!list || !name_lower) return 0;
    for (i = 0; i < list->count; ++i)
        if (strcmp(list->items[i], name_lower) == 0)
            return 1;
    return 0;
}

int namelist_substr(const NameList *list, const char *name_lower)
{
    int i;
    if (!list || !name_lower) return 0;
    for (i = 0; i < list->count; ++i)
        if (strstr(name_lower, list->items[i]) != NULL)
            return 1;
    return 0;
}
