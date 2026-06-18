/* config.h - persistent user settings and a small dynamic string list. */
#ifndef CONFIG_H
#define CONFIG_H

/* A growable, owned list of lower-cased name strings. */
typedef struct {
    char **items;
    int count;
    int cap;
} StrList;

void strlist_init(StrList *l);
void strlist_free(StrList *l);
int  strlist_contains(const StrList *l, const char *name_lower); /* 1/0 */
int  strlist_add(StrList *l, const char *name);   /* lower-cases; 1 if added */
void strlist_remove(StrList *l, const char *name_lower);
void strlist_clear(StrList *l);

typedef struct {
    int     mode;             /* MODE_SMART..MODE_NUCLEAR */
    double  scan_interval;    /* seconds, clamped 1..60   */
    int     dry_run;          /* report only, kill nothing */
    int     manage_services;  /* sweep services in Risk/Nuclear */
    int     enabled_on_start; /* auto-resume the toggle on launch */
    StrList whitelist;        /* never kill these */
    StrList blacklist;        /* always kill these */
} Config;

void config_defaults(Config *c);
void config_load(Config *c);             /* loads from %USERPROFILE%\.gamemode */
void config_save(const Config *c);
void config_free(Config *c);

#endif /* CONFIG_H */
