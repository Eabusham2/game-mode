/* config.c - settings persistence (INI) and the StrList helper type. */
#include "config.h"
#include "gamemode.h"
#include <stdlib.h>
#include <string.h>
#include <shlobj.h>

/* ------------------------------------------------------------ small utils */
void str_lower_copy(char *dst, size_t size, const char *src)
{
    size_t i = 0;
    if (!size) return;
    for (; src && src[i] && i + 1 < size; ++i) {
        char ch = src[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        dst[i] = ch;
    }
    dst[i] = '\0';
}

int str_ieq(const char *a, const char *b)
{
    return lstrcmpiA(a, b) == 0;
}

/* ----------------------------------------------------------------- StrList */
void strlist_init(StrList *l) { l->items = NULL; l->count = 0; l->cap = 0; }

void strlist_free(StrList *l)
{
    int i;
    for (i = 0; i < l->count; ++i) free(l->items[i]);
    free(l->items);
    strlist_init(l);
}

void strlist_clear(StrList *l)
{
    int i;
    for (i = 0; i < l->count; ++i) free(l->items[i]);
    l->count = 0;
}

int strlist_contains(const StrList *l, const char *name_lower)
{
    int i;
    for (i = 0; i < l->count; ++i)
        if (strcmp(l->items[i], name_lower) == 0) return 1;
    return 0;
}

int strlist_add(StrList *l, const char *name)
{
    char buf[260];
    char *copy;
    if (!name || !*name) return 0;
    str_lower_copy(buf, sizeof(buf), name);
    /* trim leading/trailing spaces */
    {
        char *s = buf, *e;
        while (*s == ' ') ++s;
        e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\r' || e[-1] == '\n')) *--e = '\0';
        if (!*s) return 0;
        if (strlist_contains(l, s)) return 0;
        if (l->count >= l->cap) {
            int ncap = l->cap ? l->cap * 2 : 8;
            char **ni = (char **)realloc(l->items, (size_t)ncap * sizeof(char *));
            if (!ni) return 0;
            l->items = ni; l->cap = ncap;
        }
        copy = (char *)malloc(strlen(s) + 1);
        if (!copy) return 0;
        strcpy(copy, s);
        l->items[l->count++] = copy;
    }
    return 1;
}

void strlist_remove(StrList *l, const char *name_lower)
{
    int i;
    for (i = 0; i < l->count; ++i) {
        if (strcmp(l->items[i], name_lower) == 0) {
            free(l->items[i]);
            memmove(&l->items[i], &l->items[i + 1],
                    (size_t)(l->count - i - 1) * sizeof(char *));
            --l->count;
            return;
        }
    }
}

/* Join with ';' into dst. */
static void strlist_join(const StrList *l, char *dst, size_t size)
{
    int i;
    dst[0] = '\0';
    for (i = 0; i < l->count; ++i) {
        if (i) strncat(dst, ";", size - strlen(dst) - 1);
        strncat(dst, l->items[i], size - strlen(dst) - 1);
    }
}

/* Split a ';'-separated string into a list. */
static void strlist_split(StrList *l, const char *src)
{
    char buf[4096];
    char *tok, *ctx = NULL;
    strlist_clear(l);
    if (!src || !*src) return;
    lstrcpynA(buf, src, sizeof(buf));
    for (tok = strtok_s(buf, ";", &ctx); tok; tok = strtok_s(NULL, ";", &ctx))
        strlist_add(l, tok);
}

/* ------------------------------------------------------------------ paths */
static void config_path(char *out, size_t size)
{
    char base[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, base))) {
        char dir[MAX_PATH];
        wsprintfA(dir, "%s\\.gamemode", base);
        CreateDirectoryA(dir, NULL);
        wsprintfA(out, "%s\\config.ini", dir);
    } else {
        lstrcpynA(out, ".\\gamemode.ini", (int)size);
    }
}

/* ------------------------------------------------------------------ config */
void config_defaults(Config *c)
{
    c->mode = MODE_SMART;
    c->scan_interval = 3.0;
    c->dry_run = 0;
    c->manage_services = 0;
    c->enabled_on_start = 0;
    c->heuristics = 1;           /* smart detection on by default */
    c->minimize_to_tray = 1;
    c->run_at_startup = 0;
    c->notifications = 1;
    strlist_init(&c->whitelist);
    strlist_init(&c->blacklist);
}

void config_free(Config *c)
{
    strlist_free(&c->whitelist);
    strlist_free(&c->blacklist);
}

void config_load(Config *c)
{
    char path[MAX_PATH];
    char buf[4096];
    config_defaults(c);
    config_path(path, sizeof(path));

    c->mode = GetPrivateProfileIntA("general", "mode", MODE_SMART, path);
    if (c->mode < 0 || c->mode >= MODE_COUNT) c->mode = MODE_SMART;
    c->scan_interval = GetPrivateProfileIntA("general", "scan_interval", 3, path);
    if (c->scan_interval < 1) c->scan_interval = 1;
    if (c->scan_interval > 60) c->scan_interval = 60;
    c->dry_run = GetPrivateProfileIntA("general", "dry_run", 0, path) ? 1 : 0;
    c->manage_services = GetPrivateProfileIntA("general", "manage_services", 0, path) ? 1 : 0;
    c->enabled_on_start = GetPrivateProfileIntA("general", "enabled_on_start", 0, path) ? 1 : 0;
    c->heuristics = GetPrivateProfileIntA("general", "heuristics", 1, path) ? 1 : 0;
    c->minimize_to_tray = GetPrivateProfileIntA("general", "minimize_to_tray", 1, path) ? 1 : 0;
    c->run_at_startup = GetPrivateProfileIntA("general", "run_at_startup", 0, path) ? 1 : 0;
    c->notifications = GetPrivateProfileIntA("general", "notifications", 1, path) ? 1 : 0;

    GetPrivateProfileStringA("lists", "whitelist", "", buf, sizeof(buf), path);
    strlist_split(&c->whitelist, buf);
    GetPrivateProfileStringA("lists", "blacklist", "", buf, sizeof(buf), path);
    strlist_split(&c->blacklist, buf);
}

void config_save(const Config *c)
{
    char path[MAX_PATH];
    char num[32];
    char buf[4096];
    config_path(path, sizeof(path));

    wsprintfA(num, "%d", c->mode);
    WritePrivateProfileStringA("general", "mode", num, path);
    wsprintfA(num, "%d", (int)(c->scan_interval + 0.5));
    WritePrivateProfileStringA("general", "scan_interval", num, path);
    WritePrivateProfileStringA("general", "dry_run", c->dry_run ? "1" : "0", path);
    WritePrivateProfileStringA("general", "manage_services", c->manage_services ? "1" : "0", path);
    WritePrivateProfileStringA("general", "enabled_on_start", c->enabled_on_start ? "1" : "0", path);
    WritePrivateProfileStringA("general", "heuristics", c->heuristics ? "1" : "0", path);
    WritePrivateProfileStringA("general", "minimize_to_tray", c->minimize_to_tray ? "1" : "0", path);
    WritePrivateProfileStringA("general", "run_at_startup", c->run_at_startup ? "1" : "0", path);
    WritePrivateProfileStringA("general", "notifications", c->notifications ? "1" : "0", path);

    strlist_join(&c->whitelist, buf, sizeof(buf));
    WritePrivateProfileStringA("lists", "whitelist", buf, path);
    strlist_join(&c->blacklist, buf, sizeof(buf));
    WritePrivateProfileStringA("lists", "blacklist", buf, path);
}
