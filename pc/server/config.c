/* recvx-server settings: the config file (INI-style "key = value", # or ; comments) and the
   command line (--key value, the same names with '-'), one table for both. recvx-server.conf.example
   and docs/SERVER.md "Configuration" describe the keys. Portable C (the Windows test build
   links it too). */
#include "server.h"
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { T_INT, T_BOOL, T_LEVEL, T_LISTEN, T_PATH };
static const struct Key {
    const char* key;
    int type, off, lo, hi;
    const char* alias;      /* the older flag name (before the config file) */
    const char* help;
} keys[] = {
#define F(x) (int)offsetof(SrvConfig, x)
    { "port", T_INT, F(port), 1, 65535, NULL, "UDP port" },
    { "listen", T_LISTEN, 0, 0, 0, "bind", "local IPv4 address(es) to listen on, comma-separated; 0.0.0.0 = all" },
    { "max_rooms", T_INT, F(max_rooms), 1, 65535, NULL, "open rooms at once (restart to change)" },
    { "rooms_per_ip", T_INT, F(rooms_per_ip), 1, 65535, NULL, "open rooms one IP address may host" },
    { "requests_per_sec", T_INT, F(req_per_sec), 1, 1000000, NULL, "requests per IP address and second (bursts of 2 s)" },
    { "failed_joins_per_min", T_INT, F(joins_per_min), 1, 1000000, "joins_per_min", "failed joins per IP address and minute" },
    { "rooms_per_ip_per_min", T_INT, F(creates_per_min), 1, 1000000, NULL, "rooms one IP address may open per minute" },
    { "requests_total_per_sec", T_INT, F(req_total_per_sec), 1, 100000000, NULL, "requests from all addresses together per second" },
    { "failed_joins_total_per_min", T_INT, F(joins_total_per_min), 1, 100000000, NULL, "failed joins from all addresses together per minute" },
    { "relay_kb_per_sec", T_INT, F(relay_kbps), 1, 10000000, "relay_kbps", "relay bandwidth per player, KB/s (1024 bytes)" },
    { "relay_datagrams_per_sec", T_INT, F(relay_pps), 1, 10000000, "relay_pps", "relayed datagrams per player and second" },
    { "relay_address_kb_per_sec", T_INT, F(ip_kbps), 1, 10000000, NULL, "relay bandwidth per IP address (all its players), KB/s" },
    { "relay_address_datagrams_per_sec", T_INT, F(ip_pps), 1, 10000000, NULL, "relayed datagrams per IP address and second" },
    { "relay_total_kb_per_sec", T_INT, F(total_kbps), 0, 100000000, "total_kbps", "all relayed traffic, KB/s (0 = no limit)" },
    { "relay_total_datagrams_per_sec", T_INT, F(total_pps), 0, 100000000, NULL, "all relayed datagrams per second (0 = no limit)" },
    { "room_timeout", T_INT, F(room_timeout_s), 5, 86400, "timeout", "seconds without the host's keepalive before a room closes" },
    { "member_timeout", T_INT, F(member_timeout_s), 6, 86400, NULL, "seconds without the joiner's keepalive before its place in the room is free" },
    { "code_grace", T_INT, F(grace_s), 0, 86400, "grace", "seconds a closed room's code is not handed out again" },
    { "log_level", T_LEVEL, F(verbose), 0, 1, NULL, "normal or verbose" },
    { "verbose", T_BOOL, F(verbose), 0, 1, "debug", "yes = log_level verbose" },
    { "stats_interval", T_INT, F(stats_s), 0, 86400, NULL, "seconds between metrics lines in the log (0 = off)" },
    { "control", T_PATH, F(control), 0, 0, NULL, "unix socket for --take-over and --drain, an absolute path (restart to change)" },
#undef F
};
#define NKEYS (int)(sizeof(keys) / sizeof(keys[0]))

static int same_key(const char* a, const char* b)   /* '-' and '_' alike, case ignored */
{
    for (; *a && *b; a++, b++)
        if ((*a == '-' ? '_' : tolower((unsigned char)*a)) != (*b == '-' ? '_' : tolower((unsigned char)*b))) return 0;
    return !*a && !*b;
}

static const struct Key* find(const char* k)
{
    int i;
    for (i = 0; i < NKEYS; i++)
        if (same_key(k, keys[i].key) || (keys[i].alias && same_key(k, keys[i].alias))) return &keys[i];
    return NULL;
}

int srv_config_is_bool(const char* key) { const struct Key* k = find(key); return k && k->type == T_BOOL; }

static int ipv4(const char* s, uint8_t out[4])
{
    int i;
    for (i = 0; i < 4; i++) {
        char* e;
        long v;
        if (!isdigit((unsigned char)*s)) return 0;
        v = strtol(s, &e, 10);
        if (v > 255 || e - s > 3 || *e != (i < 3 ? '.' : 0)) return 0;
        out[i] = (uint8_t)v;
        s = e + (i < 3);
    }
    return 1;
}

int srv_config_set(SrvConfig* c, const char* key, const char* val, char* msg, int cap)
{
    const struct Key* k = find(key);
    int* p;
    if (!k) { snprintf(msg, cap, "unknown key '%s'", key); return -1; }
    p = (int*)((char*)c + k->off);
    if (k->type == T_BOOL) {
        if (!val || !strcmp(val, "1") || !strcmp(val, "yes") || !strcmp(val, "true") || !strcmp(val, "on")) *p = 1;
        else if (!strcmp(val, "0") || !strcmp(val, "no") || !strcmp(val, "false") || !strcmp(val, "off")) *p = 0;
        else { snprintf(msg, cap, "%s: '%s' is not yes or no", k->key, val); return 0; }
        return 1;
    }
    if (k->type == T_PATH) {   /* empty: none */
        char* d = (char*)c + k->off;
        if (val && *val && *val != '/') { snprintf(msg, cap, "%s: '%s' is not an absolute path", k->key, val); return 0; }
        if (val && strlen(val) >= sizeof(c->control)) { snprintf(msg, cap, "%s: longer than %d bytes", k->key, (int)sizeof(c->control) - 1); return 0; }
        memset(d, 0, sizeof(c->control));
        if (val) memcpy(d, val, strlen(val));
        return 1;
    }
    if (!val || !*val) { snprintf(msg, cap, "%s: a value is missing", k->key); return 0; }
    if (k->type == T_LEVEL) {
        if (!strcmp(val, "normal")) *p = 0;
        else if (!strcmp(val, "verbose") || !strcmp(val, "debug")) *p = 1;
        else { snprintf(msg, cap, "%s: '%s' is not normal or verbose", k->key, val); return 0; }
        return 1;
    }
    if (k->type == T_LISTEN) {   /* "a.b.c.d[, a.b.c.d ...]": all checked before any is taken */
        SrvConfig t = *c;
        const char* s = val;
        t.nlisten = 0;
        while (*s) {
            char one[32];
            int n = (int)strcspn(s, ", \t");
            if (n == 0) { s++; continue; }
            snprintf(one, sizeof(one), "%.*s", n < 31 ? n : 31, s);
            if (t.nlisten == SRV_LISTEN_MAX) { snprintf(msg, cap, "%s: at most %d addresses", k->key, SRV_LISTEN_MAX); return 0; }
            if (strchr(one, ':')) { snprintf(msg, cap, "%s: '%s': IPv6 is not supported yet (IPv4 only)", k->key, one); return 0; }
            if (n > 15 || !ipv4(one, t.listen[t.nlisten])) { snprintf(msg, cap, "%s: '%s' is not an IPv4 address", k->key, one); return 0; }
            t.nlisten++;
            s += n;
        }
        if (!t.nlisten) { snprintf(msg, cap, "%s: no address", k->key); return 0; }
        {   /* 0.0.0.0 among them: all */
            int i;
            for (i = 0; i < t.nlisten; i++)
                if (!memcmp(t.listen[i], "\0\0\0\0", 4)) t.nlisten = 0;
        }
        c->nlisten = t.nlisten;
        memcpy(c->listen, t.listen, sizeof(c->listen));
        return 1;
    }
    {
        char* e;
        long v;
        errno = 0;
        v = strtol(val, &e, 10);
        if (e == val || *e || errno) { snprintf(msg, cap, "%s: '%s' is not a whole number", k->key, val); return 0; }
        if (v < k->lo || v > k->hi) { snprintf(msg, cap, "%s: %ld is out of range (%d..%d)", k->key, v, k->lo, k->hi); return 0; }
        *p = (int)v;
        return 1;
    }
}

int srv_config_text(SrvConfig* c, const char* text, const char* src, int* warnings, FILE* log)
{
    int line = 0, errs = 0;
    const char* s = text;
    if (!log) log = stderr;
    if (!strncmp(s, "\xEF\xBB\xBF", 3)) s += 3;   /* a UTF-8 BOM (Windows editors) */
    while (*s) {
        char buf[512], msg[160], *key, *val, *eq, *end;
        int n = (int)strcspn(s, "\n"), r;
        line++;
        if (n >= (int)sizeof(buf)) { fprintf(log, "%s:%d: error: line too long\n", src, line); errs++; s += n + (s[n] != 0); continue; }
        memcpy(buf, s, n);
        buf[n] = 0;
        s += n + (s[n] != 0);
        buf[strcspn(buf, "#;")] = 0;   /* comments (no value contains # or ;) */
        end = buf + strlen(buf);
        for (key = buf; isspace((unsigned char)*key); key++) ;
        while (end > key && isspace((unsigned char)end[-1])) *--end = 0;
        if (!*key) continue;
        if (*key == '[') continue;     /* a section header: there are no sections, ignored */
        if (!(eq = strchr(key, '='))) { fprintf(log, "%s:%d: error: expected key = value: %s\n", src, line, key); errs++; continue; }
        for (val = eq + 1; isspace((unsigned char)*val); val++) ;
        while (eq > key && isspace((unsigned char)eq[-1])) eq--;
        *eq = 0;
        r = srv_config_set(c, key, val, msg, sizeof(msg));
        if (r < 0) { fprintf(log, "%s:%d: warning: %s, ignored\n", src, line, msg); if (warnings) ++*warnings; }
        else if (!r) { fprintf(log, "%s:%d: error: %s\n", src, line, msg); errs++; }
    }
    return errs;
}

int srv_config_file(SrvConfig* c, const char* path, int* warnings, FILE* log)
{
    FILE* f = fopen(path, "rb");
    char* text;
    long n;
    int errs;
    if (!log) log = stderr;
    if (!f) { fprintf(log, "%s: cannot read: %s\n", path, strerror(errno)); return -1; }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    if (n < 0 || n > 1 << 20) { fclose(f); fprintf(log, "%s: not a config file (size)\n", path); return -1; }
    rewind(f);
    text = malloc(n + 1);
    if (!text || fread(text, 1, n, f) != (size_t)n) { free(text); fclose(f); fprintf(log, "%s: cannot read\n", path); return -1; }
    fclose(f);
    text[n] = 0;
    if ((long)strlen(text) != n) { free(text); fprintf(log, "%s: not a text file\n", path); return -1; }
    errs = srv_config_text(c, text, path, warnings, log);
    free(text);
    return errs;
}

void srv_config_help(FILE* f)
{
    SrvConfig d;
    int i;
    srv_defaults(&d);
    for (i = 0; i < NKEYS; i++) {
        char flag[40], def[24];
        int k;
        snprintf(flag, sizeof(flag), "--%s", keys[i].key);
        for (k = 0; flag[k]; k++) if (flag[k] == '_') flag[k] = '-';
        if (keys[i].type == T_LISTEN) snprintf(def, sizeof(def), "0.0.0.0");
        else if (keys[i].type == T_LEVEL) snprintf(def, sizeof(def), "normal");
        else if (keys[i].type == T_BOOL) snprintf(def, sizeof(def), "no");
        else if (keys[i].type == T_PATH) snprintf(def, sizeof(def), "none");
        else snprintf(def, sizeof(def), "%d", *(int*)((char*)&d + keys[i].off));
        fprintf(f, "  %-26s %-24s %s (default %s)\n", flag, keys[i].key, keys[i].help, def);
    }
}
