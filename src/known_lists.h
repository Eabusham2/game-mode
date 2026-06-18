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

/* Case-insensitive membership test. */
int namelist_contains(const NameList *list, const char *name_lower);

#endif /* KNOWN_LISTS_H */
