/* --dev object placer: the catalogue and the placements file (docs/DEV_PLACEMENTS.md). The models
   are loaded and drawn by pc/glue/placer_glue.c, edited in the F2 window (overlay.cpp). */
#include "placer.h"
#include "fileio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PlacerState g_placer = { .sel = -1 };

/* Sources (rooms without enemies, so their texture lists follow the objects directly). The item box's
   body and the typewriter are pieces of the room model (each room has its own copy); the box's lid
   is an object record that opens. Item ids: as in the inventory (the F2 item list shows the names). */
const PlacerDef placer_defs[] = {
    { "item_box",     "Item Box",         1080, PLK_PIECE, { 50, -1 }, 4, 0 },
    { "typewriter",   "Typewriter",       1080, PLK_PIECE, { 35, 36, 43, 45, 47, 48, -1 }, -1, 0 },
    { "green_herb",   "Green Herb",       1080, PLK_ITEM, { 6 }, -1, 21 },
    { "red_herb",     "Red Herb",         3190, PLK_ITEM, { 3 }, -1, 22 },
    { "blue_herb",    "Blue Herb",        3190, PLK_ITEM, { 5 }, -1, 23 },
    { "handgun_ammo", "Handgun Bullets",  1080, PLK_ITEM, { 5 }, -1, 12 },
    { "first_aid",    "First Aid Spray",  3040, PLK_ITEM, { 1 }, -1, 20 },
    { "ink_ribbon",   "Ink Ribbon",       1080, PLK_ITEM, { 3 }, -1, 31 },
};
#define NDEFS ((int)(sizeof(placer_defs) / sizeof(placer_defs[0])))

int placer_types(void) { return NDEFS; }
const char* placer_type_name(int t) { return t >= 0 && t < NDEFS ? placer_defs[t].name : "?"; }
const char* placer_type_key(int t) { return t >= 0 && t < NDEFS ? placer_defs[t].key : "?"; }
int placer_type_item(int t) { return t >= 0 && t < NDEFS && placer_defs[t].kind == PLK_ITEM ? placer_defs[t].item : 0; }
int placer_type_find(const char* key)
{
    for (int t = 0; t < NDEFS; t++) if (!strcmp(placer_defs[t].key, key)) return t;
    return -1;
}
const char* placer_type_source(int t, char* buf, int n)
{
    const PlacerDef* d;
    int k = 0;
    if (t < 0 || t >= NDEFS) return "";
    d = &placer_defs[t];
    if (d->kind != PLK_PIECE) k = snprintf(buf, n, "RM_%04d %s record %d", d->room, d->kind == PLK_OBJ ? "object" : "item", d->rec[0]);
    else {
        k = snprintf(buf, n, "RM_%04d room model node", d->room);
        for (int i = 0; i < 8 && d->rec[i] >= 0 && k < n; i++) k += snprintf(buf + k, n - k, "%s%d", i ? "," : " ", d->rec[i]);
    }
    if (d->lid >= 0 && k < n) snprintf(buf + k, n - k, " + object record %d", d->lid);
    return buf;
}

/* ---------------------------------------------------------------- the file: one line per entry, key=value */
static float deg(int a) { return (float)((a & 0xFFFF) > 32767 ? (a & 0xFFFF) - 65536 : (a & 0xFFFF)) * 360.0f / 65536.0f; }

static int format_entry(char* b, int n, const Placement* p)
{
    return snprintf(b, n, "type=%s stage=%d room=%d case=%d player=%d x=%.2f y=%.2f z=%.2f ax=%d ay=%d az=%d "
                    "ax_deg=%.1f ay_deg=%.1f az_deg=%.1f floor=%d file=RM_%d%02d%d.RDX\n",
                    placer_type_key(p->type), p->stg, p->rom, p->rcase, p->ply, p->pos[0], p->pos[1], p->pos[2],
                    p->ang[0], p->ang[1], p->ang[2], deg(p->ang[0]), deg(p->ang[1]), deg(p->ang[2]), p->flr, p->stg, p->rom, p->rcase);
}

int placer_format(char* buf, int cap)
{
    int k = 0, t, i;
    char src[96];
#define PUT(...) do { if (k < cap) k += snprintf(buf + k, cap - k, __VA_ARGS__); } while (0)
    PUT("; recvx-enhanced-port dev placements, format 1 (docs/DEV_PLACEMENTS.md)\n");
    PUT("; one line per object: position in game units (y up), angles in game units (65536 = 360 degrees,\n");
    PUT("; njRotateXYZ order; ay 0 faces -z) and in degrees, floor = bhCheckFloorNum(y)\n");
    for (t = 0; t < NDEFS; t++) {
        PUT("; %-13s %s", placer_type_key(t), placer_type_source(t, src, sizeof(src)));
        if (placer_type_item(t)) PUT(", item %d", placer_type_item(t));
        if (g_placer.type_state[t] == 1)
            PUT(", extent x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f", g_placer.type_min[t][0], g_placer.type_max[t][0],
                g_placer.type_min[t][1], g_placer.type_max[t][1], g_placer.type_min[t][2], g_placer.type_max[t][2]);
        PUT("\n");
    }
    for (i = 0; i < PL_MAX; i++)
        if (g_placer.p[i].used && k < cap) k += format_entry(buf + k, cap - k, &g_placer.p[i]);
#undef PUT
    return k < cap ? k : cap - 1;
}

int placer_parse_line(const char* line, Placement* out)
{
    char key[16], val[48];
    const char* s = line;
    int n, have = 0;
    memset(out, 0, sizeof(*out));
    out->type = -1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == ';' || *s == '#' || !*s) return 0;
    while (sscanf(s, " %15[^= \t\r\n]=%47s%n", key, val, &n) == 2) {
        s += n;
        if (!strcmp(key, "type")) out->type = placer_type_find(val);
        else if (!strcmp(key, "stage")) out->stg = atoi(val), have |= 1;
        else if (!strcmp(key, "room")) out->rom = atoi(val), have |= 2;
        else if (!strcmp(key, "case")) out->rcase = atoi(val);
        else if (!strcmp(key, "player")) out->ply = atoi(val);
        else if (!strcmp(key, "x")) out->pos[0] = (float)atof(val), have |= 4;
        else if (!strcmp(key, "y")) out->pos[1] = (float)atof(val);
        else if (!strcmp(key, "z")) out->pos[2] = (float)atof(val), have |= 8;
        else if (!strcmp(key, "ax")) out->ang[0] = atoi(val);
        else if (!strcmp(key, "ay")) out->ang[1] = atoi(val);
        else if (!strcmp(key, "az")) out->ang[2] = atoi(val);
        else if (!strcmp(key, "floor")) out->flr = atoi(val);
    }
    if (out->type < 0 || have != 15) return 0;
    out->used = 1;
    return 1;
}

int placer_load(const char* path)
{
    FILE* f = pc_fopen(path, "r");
    char line[512];
    int n = 0, i = 0;
    Placement p;
    if (!f) return -1;
    memset(g_placer.p, 0, sizeof(g_placer.p));
    while (fgets(line, sizeof(line), f) && i < PL_MAX)
        if (placer_parse_line(line, &p)) { g_placer.p[i++] = p; n++; }
    fclose(f);
    g_placer.sel = -1;
    return n;
}

static int fill(FILE* f, void* ud)
{
    char* buf = (char*)ud;
    return fputs(buf, f) < 0 ? -1 : 0;
}

int placer_save(const char* path)
{
    int cap = PL_MAX * 256 + 4096, r;
    char* buf = (char*)malloc(cap);
    if (!buf) return -1;
    placer_format(buf, cap);
    r = pc_save_file(path, "w", fill, buf);
    free(buf);
    return r;
}
