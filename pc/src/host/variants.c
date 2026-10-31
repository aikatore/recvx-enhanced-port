/* Enemy variants (variants.h, docs/ENEMIES.md "Enemy variants"): the built-in table and the
   mods/enemies.ini reader. */
#include "variants.h"
#include "actors.h"
#include "fileio.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void host_log(const char* fmt, ...);

/* Mutated Steve: Monster Steve himself (en17.c: his walk, swings, charge, sounds, his arms) spawned as an enemy
   that can die. His code is written for the Antarctic chase: no hit takes his hit points, his Die mode is empty,
   his swing kills outright and his other heavy blows kill below 120 hit points, it drives room light 2 and shakes
   the camera. For this variant only (en17.c pc_variant_is): hits take his hit points and kill him (flg & 2, as
   every enemy), the killing blows are heavy ones (50, x damage), no light and no camera shake. He has no death
   motion: the Tyrant's (donor 19.0: its fall 18 and lying 2, en19.c TY_BR0_DIE) is played by the Tyrant's
   skeleton and retargeted onto his model, which then lies where he fell. */
static EnemyVariant table[VARIANT_MAX] = {
    { .id = 1, .name = "Mutated Steve", .type = 17, .mdlver = 0, .model = -1,
      .attach_n = 2, .attach = { { 3, 9 }, { 4, 13 } },   /* his claw and his axe arm on the posed model (en17.c bhEne17_SetLinkWork) */
      .hp = 2.0f, .armour = 1.0f, .damage = 1.25f, .speed = 1.0f, .scale = 1.0f,
      .donor_type = 19, .donor_mdlver = 0, .death_n = 2, .death = { 18, 2 },
      .wild = 1, .mild = 0, .heavy = 1, .big = 1, .stages = VARIANT_STAGES_ALL },   /* (hp x2: his 250 make 500, a randomized Tyrant's on Normal: en19.c En19HpTbl, type 0) */
};
static int builtin_n = 1, n = 1;

static void make_ui(EnemyVariant* v) { snprintf(v->ui, sizeof(v->ui), "[C] %s", v->name); }

__attribute__((constructor)) static void builtin_init(void)
{
    for (int i = 0; i < builtin_n; i++) make_ui(&table[i]);
}

int variant_count(void) { return n; }
const EnemyVariant* variant_at(int i) { return i >= 0 && i < n ? &table[i] : NULL; }
const EnemyVariant* variant_by_id(int id)
{
    for (int i = 0; i < n; i++) if (table[i].id == id) return &table[i];
    return NULL;
}
void variants_reset_user(void) { n = builtin_n; }

/* ---------------------------------------------------------------- mods/enemies.ini */
static char* trim(char* s)
{
    char* e;
    while (isspace((unsigned char)*s)) s++;
    for (e = s + strlen(s); e > s && isspace((unsigned char)e[-1]); e--);
    *e = 0;
    return s;
}

static int model_named(const char* s)   /* a catalogue entry by name or index, -1 */
{
    char* end;
    long k = strtol(s, &end, 10);
    if (*s && !*end) return k >= 0 && k < ACTOR_MODELS ? (int)k : -1;
    for (int i = 0; i < ACTOR_MODELS; i++)
        if (!strcasecmp(actor_models[i].name, s)) return i;
    return -1;
}

/* "a:b, c:d" into out[max][2] (each 0..lim); count or -1 */
static int pairs(const char* s, short (*out)[2], int max, int lim)
{
    int k = 0, a, b, m;
    while (*s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        if (sscanf(s, "%d : %d%n", &a, &b, &m) != 2 || k == max || a < 0 || b < 0 || a > lim || b > lim) return -1;
        out[k][0] = (short)a; out[k][1] = (short)b; k++;
        s += m;
    }
    return k;
}

/* "0-9" / "1,5,6" / "all" */
static int stage_bits(const char* s)
{
    int bits = 0, a, b, m;
    if (!strcasecmp(s, "all")) return VARIANT_STAGES_ALL;
    while (*s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        if (sscanf(s, "%d - %d%n", &a, &b, &m) == 2) {}
        else if (sscanf(s, "%d%n", &a, &m) == 1) b = a;
        else return -1;
        if (a < 0 || b > 9 || a > b) return -1;
        for (; a <= b; a++) bits |= 1 << a;
        s += m;
    }
    return bits;
}

/* the variant's one donor (another enemy whose motions it borrows): type t, model version ver */
static int donor(EnemyVariant* v, int t, int ver, const char** why)
{
    if (t < 1 || t > 99 || ver < 0 || ver > 63) return 0;
    if (v->donor_type && (v->donor_type != t || v->donor_mdlver != ver)) { *why = "one donor enemy per variant"; return 0; }
    v->donor_type = (unsigned char)t;
    v->donor_mdlver = (unsigned char)ver;
    return 1;
}

/* "a:b" (its own motion b where the base asks for a) or "a:T[.v]:b" (enemy T's motion b), comma separated */
static int motions(EnemyVariant* v, char* val, const char** why)
{
    char* tok;
    v->motion_n = v->dmotion_n = 0;
    for (tok = strtok(val, ","); tok; tok = strtok(NULL, ",")) {
        int a, t, ver = 0, b, m = -1, len = (int)strlen(tok);
        if ((sscanf(tok, " %d : %d . %d : %d %n", &a, &t, &ver, &b, &m) == 4 && m == len) ||
            (m = -1, ver = 0, sscanf(tok, " %d : %d : %d %n", &a, &t, &b, &m) == 3 && m == len)) {
            if (a < 0 || a > 511 || b < 0 || b > 511 || v->dmotion_n == 8 || !donor(v, t, ver, why)) return 0;
            v->dmotion[v->dmotion_n][0] = (short)a; v->dmotion[v->dmotion_n++][1] = (short)b;
        } else if (m = -1, sscanf(tok, " %d : %d %n", &a, &b, &m) == 2 && m == len) {
            if (a < 0 || a > 511 || b < 0 || b > 511 || v->motion_n == 8) return 0;
            v->motion[v->motion_n][0] = (short)a; v->motion[v->motion_n++][1] = (short)b;
        } else return 0;
    }
    return 1;
}

/* "T[.v]: m1, m2, ...": enemy T's motions played in turn when it dies, the last one's end held */
static int death(EnemyVariant* v, char* val, const char** why)
{
    int t, ver = 0, m = -1, b;
    char* tok;
    if (!(sscanf(val, " %d . %d : %n", &t, &ver, &m) == 2 && m > 0) && (m = -1, ver = 0, !(sscanf(val, " %d : %n", &t, &m) == 1 && m > 0)))
        return 0;
    if (!donor(v, t, ver, why)) return 0;
    v->death_n = 0;
    for (tok = strtok(val + m, ","); tok; tok = strtok(NULL, ",")) {
        int n2 = -1;
        if (sscanf(tok, " %d %n", &b, &n2) != 1 || n2 != (int)strlen(tok) || b < 0 || b > 511 || v->death_n == 4) return 0;
        v->death[v->death_n++] = (short)b;
    }
    return v->death_n > 0;
}

static int number(const char* s, float* out, float lo, float hi)
{
    char* end;
    float f = strtof(s, &end);
    if (end == s || *trim(end) || f < lo || f > hi) return 0;
    *out = f;
    return 1;
}

/* one key of section v; 0 with *why set: not understood */
static int key(EnemyVariant* v, const char* k, char* val, const char** why, int* have_base)
{
    float f;
    short pr[16][2];
    int c, i;
    *why = "bad value";
    if (!strcmp(k, "id")) { if (!number(val, &f, VARIANT_USER_MIN, VARIANT_USER_MAX) || f != (int)f) { *why = "id must be 100..999"; return 0; } v->id = (unsigned short)f; return 1; }
    if (!strcmp(k, "base")) {
        int t, ver = 0, m;
        if (sscanf(val, "%d%n", &t, &m) == 1 && (!val[m] || sscanf(val + m, ".%d", &ver) == 1)) {
            if (t < 1 || t > 99 || ver < 0 || ver > 63) return 0;
            v->type = (unsigned char)t; v->mdlver = (unsigned char)ver;
        } else {
            if ((m = model_named(val)) < 0 || actor_models[m].id > 30) { *why = "no enemy of that name in the spawner list"; return 0; }
            v->type = actor_models[m].id; v->mdlver = actor_models[m].mdlver;
        }
        *have_base = 1;
        return 1;
    }
    if (!strcmp(k, "model")) {
        if (!strcasecmp(val, "none")) { v->model = -1; return 1; }
        if ((v->model = (short)model_named(val)) < 0) { *why = "no \"You appear as\" model of that name"; return 0; }
        return 1;
    }
    if (!strcmp(k, "attach")) {
        if ((c = pairs(val, pr, 4, 63)) < 0) return 0;
        for (i = 0; i < c; i++) { v->attach[i][0] = (unsigned char)pr[i][0]; v->attach[i][1] = (unsigned char)pr[i][1]; }
        v->attach_n = (unsigned char)c;
        return 1;
    }
    if (!strcmp(k, "hp")) return number(val, &v->hp, 0.05f, 50.0f);
    if (!strcmp(k, "armour") || !strcmp(k, "armor")) return number(val, &v->armour, 0.0f, 20.0f);
    if (!strcmp(k, "damage")) return number(val, &v->damage, 0.0f, 20.0f);
    if (!strcmp(k, "speed")) return number(val, &v->speed, 0.5f, 2.0f);
    if (!strcmp(k, "scale")) return number(val, &v->scale, 0.25f, 4.0f);
    if (!strcmp(k, "motions")) return motions(v, val, why);
    if (!strcmp(k, "death")) return death(v, val, why);
    if (!strcmp(k, "sounds")) { if (!number(val, &f, 0, 9999) || f != (int)f) return 0; v->sound_room = (unsigned short)f; return 1; }
    if (!strcmp(k, "sound_map")) {
        if ((c = pairs(val, pr, 16, 255)) < 0) return 0;
        for (i = 0; i < c; i++) { v->sound[i][0] = (unsigned char)pr[i][0]; v->sound[i][1] = (unsigned char)pr[i][1]; }
        v->sound_n = (unsigned char)c;
        return 1;
    }
    if (!strcmp(k, "stages")) { if ((c = stage_bits(val)) < 0) return 0; v->stages = (unsigned short)c; return 1; }
    {   /* small whole numbers */
        static const struct { const char* k; size_t off; int max; } u8[] = {
            { "wild", offsetof(EnemyVariant, wild), 20 }, { "mild", offsetof(EnemyVariant, mild), 20 },
            { "slots", offsetof(EnemyVariant, slots), 64 }, { "big", offsetof(EnemyVariant, big), 1 },
            { "heavy", offsetof(EnemyVariant, heavy), 1 }, { "hurt_sound", offsetof(EnemyVariant, hurt), 255 },
        };
        for (i = 0; i < (int)(sizeof(u8) / sizeof(u8[0])); i++)
            if (!strcmp(k, u8[i].k)) {
                if (!number(val, &f, 0, (float)u8[i].max) || f != (int)f) return 0;
                *((unsigned char*)v + u8[i].off) = (unsigned char)f;
                return 1;
            }
    }
    *why = "unknown key";
    return 0;
}

static void say(void (*log)(int, const char*), int line, const char* fmt, const char* a)
{
    char m[160];
    if (!log) return;
    snprintf(m, sizeof(m), fmt, a);
    log(line, m);
}

/* the section in progress: kept if it has an id and a base */
static int finish(EnemyVariant* v, int have_base, int line, void (*log)(int, const char*))
{
    if (!v->name[0]) return 0;
    if (!v->id || !have_base) { say(log, line, "[%s] left out: it needs an id and a base", v->name); return 0; }
    if (variant_by_id(v->id)) { say(log, line, "[%s] left out: its id is taken", v->name); return 0; }
    if (n == VARIANT_MAX) { say(log, line, "[%s] left out: too many enemies", v->name); return 0; }
    v->user = 1;
    make_ui(v);
    table[n++] = *v;
    return 1;
}

int variants_parse(const char* text, void (*log)(int line, const char* msg))
{
    EnemyVariant v;
    int added = 0, line = 0, have_base = 0, start = 0;
    const char* p = text;
    memset(&v, 0, sizeof(v));
    while (p && *p) {
        char buf[512], *s, *eq;
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, p, len);
        buf[len] = 0;
        p = nl ? nl + 1 : NULL;
        line++;
        if ((s = strpbrk(buf, ";#"))) *s = 0;   /* comments */
        s = trim(buf);
        if (!*s) continue;
        if (*s == '[') {
            char* e = strchr(s, ']');
            added += finish(&v, have_base, start, log);
            memset(&v, 0, sizeof(v));
            have_base = 0;
            start = line;
            if (!e || e == s + 1) { say(log, line, "bad section name %s", s); continue; }
            *e = 0;
            snprintf(v.name, sizeof(v.name), "%s", trim(s + 1));
            v.model = -1;
            v.hp = v.armour = v.damage = v.speed = v.scale = 1.0f;
            v.stages = VARIANT_STAGES_ALL;
            continue;
        }
        if (!(eq = strchr(s, '=')) || !v.name[0]) { say(log, line, "not a \"key = value\" line in a [section]: %s", s); continue; }
        *eq = 0;
        {
            const char* why;
            char* k = trim(s);
            for (char* c = k; *c; c++) *c = (char)tolower((unsigned char)*c);
            if (!key(&v, k, trim(eq + 1), &why, &have_base)) {
                char m[160];
                snprintf(m, sizeof(m), "[%s] %s: %s", v.name, k, why);
                if (log) log(line, m);
            }
        }
    }
    added += finish(&v, have_base, start, log);
    return added;
}

static void ini_log(int line, const char* msg) { host_log("enemies.ini line %d: %s\n", line, msg); }

void variants_load(void)
{
    const char* dir = getenv("RECVX_MODS") ? getenv("RECVX_MODS") : "mods";
    char path[1100], *text;
    FILE* f;
    long size;
    snprintf(path, sizeof(path), "%s/enemies.ini", dir);
    if (!(f = pc_fopen(path, "rb"))) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0 || size > (1 << 20) || !(text = calloc(1, (size_t)size + 1))) { fclose(f); return; }
    if (fread(text, 1, (size_t)size, f) == (size_t)size) {
        int k = variants_parse(text, ini_log);
        host_log("enemies: %s: %d custom enemies\n", path, k);
    }
    free(text);
    fclose(f);
}
