/* known_lists.h - the curated, built-in knowledge about what may be killed and
 * what must never be touched. Every entry is stored lower-case, name-only.
 *
 * PROTECTED_SYSTEM is the program's primary safety net: nothing in it is ever
 * terminated, in any mode, even if the user blacklists it. */
#ifndef KNOWN_LISTS_H
#define KNOWN_LISTS_H

typedef struct { const char *const *items; int count; } NameList;

extern const NameList PROTECTED_SYSTEM;   /* critical OS processes - never kill */
extern const NameList ANTI_CHEAT;         /* anti-cheat engines - never kill    */
extern const NameList GAME_PLATFORMS;     /* launchers/stores - kept Risk/Nuclear */
extern const NameList KEEP_WHILE_GAMING;  /* Discord etc. - kept Risk/Nuclear    */
extern const NameList BLOATWARE;          /* killed from Smart up                */
extern const NameList BACKGROUND_NOISE;   /* killed from Aggressive up           */
extern const NameList COMMON_APPS;        /* kept in Risk, killed in Nuclear     */
extern const NameList ESSENTIAL_SERVICES; /* never stopped (service names)       */

/* Heuristic substring patterns used to recognise junk we did not hard-code.
 * STRONG = high confidence (updaters/crash handlers/telemetry) -> Smart mode.
 * WEAK   = softer signals (helper/agent/tray/sync) -> Aggressive mode only.   */
extern const NameList JUNK_PATTERNS_STRONG;
extern const NameList JUNK_PATTERNS_WEAK;

/* Case-insensitive exact membership test. */
int namelist_contains(const NameList *list, const char *name_lower);
/* True if any list entry occurs as a substring of name_lower. */
int namelist_substr(const NameList *list, const char *name_lower);

/* ---------------------------------------------------------------- presets --
 * Opinionated "common targets" the user can toggle as a preset. When a preset
 * is ON its programs are force-closed (like the blacklist); when OFF its
 * programs are force-kept (like the whitelist), overriding the curated lists.
 * This lets people keep things some users like (OneDrive, Spotify, Edge) and
 * remove things others dislike (Game Bar, Cortana, Widgets). */
typedef struct {
    const char *key;       /* stable id / INI key */
    const char *label;     /* GUI checkbox label   */
    int         default_on;/* 1 = close by default */
    NameList    names;     /* affected process names (lower-case) */
} Preset;

extern const Preset PRESETS[];
extern const int    PRESET_COUNT;

#endif /* KNOWN_LISTS_H */
