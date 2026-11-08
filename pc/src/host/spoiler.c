/* BioRand's spoiler log, read (spoiler.h). The lines (BioRand 3.3, ItemRandomiser.cs, DoorRandomiser.cs,
   EnemyRandomiser.cs; indented with spaces):
     BioRand 3.3.2 (7955765)                     Seed: R840-...   Player: 0 Claire   Scenario: A
     Creating segment from 1000 to 6000:         (door randomizer: a segment; its rooms are the Connected lines')
       Connected 40C0:1 [EmblemCard] to 5020:2 (locked #129)    ([...]: the keys that door asks for)
     4011:1 -> null                              (the dead ends after the segments: no segment of their own)
     Placing key item (EmblemCard x1) in A160:6  (the original is not in the log: BioRand's rdt.json has it)
     ------------ checkpoint ------------        (the key items' next part)
     Replaced 8170:7 [BlueHerb x1] with 8170:7 [M1P x1]
     Swapped 8170:7 [BlueHerb x1] with 3020:4 [M1P x1]    (shuffled: the spot is the first)
     8170:7 [M1P x1] placed at 8171:7            (a linked spot: the same item)
     Adding CombatKnife x1                       (the start inventory, under "Randomizing Inventory")
     1010 shall have Zombie / Randomizing room 1010 with enemy: Zombie
   Anything else is skipped, so a newer BioRand's extra lines do no harm. */
#include "spoiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spoiler_data.inc"

int spoiler_item_id(const char* name)
{
    for (int i = 0; i < (int)(sizeof(br_names) / sizeof(br_names[0])); i++)
        if (br_names[i][0] && !strcmp(br_names[i], name)) return i;
    return -1;
}

const char* spoiler_item_name(int id) { return id >= 0 && id < (int)(sizeof(br_names) / sizeof(br_names[0])) ? br_names[id] : ""; }

int spoiler_room_stage(unsigned short c) { return (c >> 12) - 1; }
int spoiler_room_room(unsigned short c) { return c >> 4 & 0xFF; }
int spoiler_room_variant(unsigned short c) { return c & 0xF; }

static int hexd(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

/* "A1A0" at s (4 hex digits, not followed by one): the code, -1 */
static int code_at(const char* s)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        int d = hexd(s[i]);
        if (d < 0) return -1;
        v = v << 4 | d;
    }
    return hexd(s[4]) >= 0 || v < 0x1000 ? -1 : v;
}

/* "A160:6" at *p: room and spot, *p past it; 0 not one */
static int spot_at(const char** p, int* room, int* spot)
{
    const char* s = *p;
    char* e;
    long n;
    if ((*room = code_at(s)) < 0 || s[4] != ':' || s[5] < '0' || s[5] > '9') return 0;
    n = strtol(s + 5, &e, 10);
    if (n > 255) return 0;
    *spot = (int)n;
    *p = e;
    return 1;
}

/* "Name x12" up to the closing c: name, count; *p past c; 0 not one */
static int item_at(const char** p, char c, char* name, int* n)
{
    const char *s = *p, *end = strchr(s, c), *x;
    if (!end) return 0;
    for (x = end; x > s && x[-1] != 'x'; x--) {}
    if (x <= s + 1 || x[-2] != ' ') return 0;
    snprintf(name, SP_NAME, "%.*s", (int)(x - 2 - s), s);
    *n = atoi(x);
    *p = end + 1;
    return 1;
}

static int find_item(Spoiler* sp, int room, int spot)
{
    for (int i = 0; i < sp->nitem; i++)
        if (sp->item[i].room == room && sp->item[i].spot == spot) return i;
    return -1;
}

static int orig(int room, int spot, int* n)
{
    for (int i = 0; i < (int)(sizeof(br_orig) / sizeof(br_orig[0])); i++)
        if (br_orig[i].room == room && br_orig[i].id == spot) { *n = br_orig[i].amount; return br_orig[i].type; }
    return -1;
}

/* the spot's entry (made: its original from the log's [..] or else rdt.json's) and its new item */
static SpItem* put(Spoiler* sp, int room, int spot, const char* old_name, int old_n, const char* new_name, int new_n)
{
    int i = find_item(sp, room, spot);
    SpItem* it;
    if (i < 0) {
        if (sp->nitem >= SP_ITEMS) { sp->truncated = 1; return NULL; }
        it = &sp->item[i = sp->nitem++];
        memset(it, 0, sizeof(*it));
        it->room = (unsigned short)room;
        it->spot = (unsigned char)spot;
        it->part = -1;
        if (old_name) {
            snprintf(it->old_name, SP_NAME, "%s", old_name);
            it->old_id = (short)spoiler_item_id(old_name);
            it->old_n = (unsigned short)old_n;
        } else {
            int n = 0, id = orig(room, spot, &n);
            it->old_id = (short)id;
            it->old_n = (unsigned short)n;
            snprintf(it->old_name, SP_NAME, "%s", spoiler_item_name(id));
        }
    }
    it = &sp->item[i];
    snprintf(it->new_name, SP_NAME, "%s", new_name);
    it->new_id = (short)spoiler_item_id(new_name);
    it->new_n = (unsigned short)new_n;
    return it;
}

static SpRoom* room_of(Spoiler* sp, int code)
{
    for (int i = 0; i < sp->nroom; i++)
        if (sp->room[i].code == code) return &sp->room[i];
    if (sp->nroom >= SP_ROOMS) { sp->truncated = 1; return NULL; }
    memset(&sp->room[sp->nroom], 0, sizeof(sp->room[0]));
    sp->room[sp->nroom].code = (unsigned short)code;
    sp->room[sp->nroom].seg = -1;
    return &sp->room[sp->nroom++];
}

/* a room of segment s (the Connected lines): its first segment, but a segment's start room is that segment's
   (it ends the one before, as its last door's target) */
static void seg_room(Spoiler* sp, int code, int s)
{
    SpRoom* r = room_of(sp, code);
    if (r && (r->seg < 0 || sp->seg[s].from == code)) r->seg = (signed char)s;
}

static void need(SpSeg* g, const char* list, const char* end)
{
    char nm[SP_NAME];
    while (list < end) {
        const char* c = memchr(list, ',', (size_t)(end - list));
        int id, k;
        if (!c) c = end;
        while (*list == ' ') list++;
        snprintf(nm, sizeof(nm), "%.*s", (int)(c - list), list);
        id = spoiler_item_id(nm);
        for (k = 0; k < g->nneed && g->need[k] != id; k++) {}
        if (id >= 0 && k == g->nneed && g->nneed < SP_NEED) g->need[g->nneed++] = (short)id;
        list = c + 1;
    }
}

static int by_spot(const void* a, const void* b)
{
    const SpItem *x = a, *y = b;
    return x->room != y->room ? x->room - y->room : x->spot - y->spot;
}
static int by_code(const void* a, const void* b) { return ((const SpRoom*)a)->code - ((const SpRoom*)b)->code; }

int spoiler_parse(const char* text, size_t n, Spoiler* sp)
{
    const char *p = text, *end = text + n;
    int seg = -1, inv = 0, part = 0, first = 1;
    char line[512], a[SP_NAME], b[SP_NAME];
    memset(sp, 0, sizeof(*sp));
    for (; p < end; first = 0) {
        const char *e = memchr(p, '\n', (size_t)(end - p)), *l;
        int room, spot, room2, spot2, na, nb;
        if (!e) e = end;
        snprintf(line, sizeof(line), "%.*s", (int)(e - p), p);   /* (a longer line is cut: none of ours is) */
        p = e + 1;
        line[strcspn(line, "\r")] = 0;
        l = line + strspn(line, " \t");
        if (first) {
            if (!strncmp(l, "\xEF\xBB\xBF", 3)) l += 3;   /* (a BOM) */
            if (strncmp(l, "BioRand ", 8)) return -1;
            snprintf(sp->version, sizeof(sp->version), "%s", l + 8);
            continue;
        }
        if (!strncmp(l, "Seed: ", 6)) snprintf(sp->seed, sizeof(sp->seed), "%s", l + 6);
        else if (!strncmp(l, "Player: ", 8)) snprintf(sp->player, sizeof(sp->player), "%s", l + 8 + strspn(l + 8, "0123456789 "));
        else if (!strncmp(l, "Scenario: ", 10)) snprintf(sp->scenario, sizeof(sp->scenario), "%s", l + 10);
        else if (!strncmp(l, "Creating segment from ", 22)) {
            int f = code_at(l + 22), t = strlen(l) > 30 ? code_at(l + 30) : -1;
            if (sp->nseg >= SP_SEGS || f < 0) { seg = -1; sp->truncated |= sp->nseg >= SP_SEGS; continue; }
            seg = sp->nseg++;
            sp->seg[seg].from = (unsigned short)f;
            sp->seg[seg].to = (unsigned short)(t < 0 ? 0 : t);
        } else if (!strncmp(l, "Connected ", 10)) {
            const char *q = l + 10, *k, *ke;
            if (seg < 0 || !spot_at(&q, &room, &spot)) continue;
            if ((k = strchr(q, '[')) && (ke = strchr(k, ']')) && (!strstr(q, " to ") || k < strstr(q, " to "))) need(&sp->seg[seg], k + 1, ke);
            if (!(q = strstr(q, " to "))) continue;
            q += 4;
            if (!spot_at(&q, &room2, &spot2)) continue;
            seg_room(sp, room, seg);
            seg_room(sp, room2, seg);
        } else if (strstr(l, " -> null")) seg = -1;
        else if (!strncmp(l, "Randomizing Inventory", 21)) inv = 1;
        else if (!strncmp(l, "Randomizing enemies", 19) || !strncmp(l, "Randomizing Items", 17)) inv = 0;
        else if (inv && !strncmp(l, "Adding ", 7)) {
            const char* q = l + 7;
            char nm[SP_NAME];
            int c;
            if (sp->ninv >= SP_INV) { sp->truncated = 1; continue; }
            if (sscanf(q, "%27s x%d", nm, &c) != 2) continue;
            snprintf(sp->inv[sp->ninv].name, SP_NAME, "%s", nm);
            sp->inv[sp->ninv].id = (short)spoiler_item_id(nm);
            sp->inv[sp->ninv].n = (unsigned short)c;
            sp->ninv++;
        } else if (!strncmp(l, "Placing key item (", 18)) {
            const char* q = l + 18;
            SpItem* it;
            if (!item_at(&q, ')', b, &nb) || strncmp(q, " in ", 4)) continue;
            q += 4;
            if (!spot_at(&q, &room, &spot) || !(it = put(sp, room, spot, NULL, 0, b, nb))) continue;
            it->key = 1;
            it->part = (signed char)(part < 127 ? part : 127);
            if (sp->parts < part + 1) sp->parts = part + 1;
        } else if (strstr(l, "--- checkpoint ---")) part++;
        else if (!strncmp(l, "Replaced ", 9) || !strncmp(l, "Swapped ", 8)) {
            const char* q = l + (l[0] == 'R' ? 9 : 8);
            if (!spot_at(&q, &room, &spot) || strncmp(q, " [", 2)) continue;
            q += 2;
            if (!item_at(&q, ']', a, &na) || strncmp(q, " with ", 6)) continue;
            q += 6;
            if (!spot_at(&q, &room2, &spot2) || strncmp(q, " [", 2)) continue;
            q += 2;
            if (!item_at(&q, ']', b, &nb)) continue;
            put(sp, room, spot, a, na, b, nb);
        } else if (spot_at(&l, &room, &spot) && !strncmp(l, " [", 2)) {   /* linked: "8170:7 [M1P x1] placed at 8171:7" */
            const char* q = l + 2;
            SpItem *it, *src;
            int si;
            if (!item_at(&q, ']', b, &nb) || strncmp(q, " placed at ", 11)) continue;
            q += 11;
            if (!spot_at(&q, &room2, &spot2) || !(it = put(sp, room2, spot2, NULL, 0, b, nb))) continue;
            if ((si = find_item(sp, room, spot)) >= 0 && (src = &sp->item[si]) != it) { it->key = src->key; it->part = src->part; }
        } else {
            int c = code_at(l);
            SpRoom* r;
            const char* q;
            if (c >= 0 && !strncmp(l + 4, " shall have ", 12)) q = l + 16;
            else if (!strncmp(l, "Randomizing room ", 17) && (c = code_at(l + 17)) >= 0 && !strncmp(l + 21, " with enemy: ", 13)) q = l + 34;
            else continue;
            if ((r = room_of(sp, c))) snprintf(r->enemy, SP_NAME, "%s", q);
        }
    }
    if (first) return -1;   /* (empty) */
    for (int i = 0; i < sp->nitem; i++) room_of(sp, sp->item[i].room);
    qsort(sp->item, (size_t)sp->nitem, sizeof(sp->item[0]), by_spot);
    qsort(sp->room, (size_t)sp->nroom, sizeof(sp->room[0]), by_code);
    return 0;
}

const SpRoom* spoiler_room(const Spoiler* sp, unsigned short code)
{
    int lo = 0, hi = sp->nroom - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (sp->room[m].code == code) return &sp->room[m];
        if (sp->room[m].code < code) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

int spoiler_has_doors(const Spoiler* sp) { return sp->nseg > 0; }
