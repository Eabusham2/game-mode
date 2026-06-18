/* modes.c - human-readable text for each mode. */
#include "gamemode.h"

const char *MODE_KEYS[MODE_COUNT] = {
    "smart", "aggressive", "risk", "nuclear"
};

const char *MODE_LABELS[MODE_COUNT] = {
    "Smart  -  safe clean-up",
    "Aggressive  -  close background apps",
    "Risk  -  whitelist-leaning",
    "Nuclear  -  riskiest, whitelist only"
};

const char *MODE_DESCRIPTIONS[MODE_COUNT] = {
    "Closes only confirmed junk: Adobe helpers, updaters, telemetry and vendor "
    "tray bloat, plus anything on your blacklist. Safe for daily use.",

    "Everything Smart does, plus heavier background apps (Office, Spotify, Slack, "
    "Zoom, cloud sync, chat apps). Browsers and your games stay open.",

    "Whitelist-leaning. Keeps Windows essentials, anti-cheats, game launchers, "
    "Discord, browsers/dev tools and your whitelist - kills everything else.",

    "RISKIEST. Keeps ONLY Windows essentials, anti-cheats, detected games & "
    "launchers, Discord, GameMode itself and your whitelist. Closes everything "
    "else, including browsers. Use with care."
};
