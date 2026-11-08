/* The voice-line randomizer's catalogue and picks (docs/ACTORS.md "Voice lines"; the game side: pc/glue/cast_glue.c
   pc_voice_on). Every cutscene line is a VoiceOn of a room script (op 25: kind, voice u16, mode, fade, record): a file of
   the voice archive of the room's disc (VOICE1.AFS for stages 0..5, VOICE2.AFS for 6..9). Who says it is the record
   whose mouth the next LipSet (op 61: bank, record, lip entry) moves, before another VoiceOn; without one, the VoiceOn's
   own record (kind 1; its position, mostly the speaker's). The catalogue holds the lines with a LipSet of a story
   character, with that lip entry's bytes (pairs phoneme, time; 0xFF ends) copied from the room: what another role says
   in its place and how its mouth moves. Read from the room files as the teleport list reads them (teleport.c, its
   thread); nothing of the disc is in the repository. No game state here: ctest "cast". */
#include "cast.h"
#include <stdlib.h>
#include <string.h>

enum { OP_END = 0, OP_IFEL = 1, OP_ELSE = 2, OP_ENDIF = 3, OP_VOICEON = 25, OP_MESSAGE = 31, OP_LIPSET = 61, OP_LIPSTART = 63, OP_EVTEND = 255 };

static unsigned rd32(const unsigned char* d, long n, long o) { return o >= 0 && o <= n - 4 ? d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (unsigned)d[o + 3] << 24 : 0; }

int voice_lipset(const unsigned char* p, int n, int (*insn)(const unsigned char*), int* rec)
{
    for (int k = 0, o = 0, len; k < 16 && o + 4 <= n; k++, o += len) {
        if (p[o] == OP_LIPSET) { *rec = p[o + 2]; return p[o + 3]; }
        if (p[o] == OP_VOICEON || p[o] == OP_END || p[o] == OP_EVTEND || (len = insn(p + o)) <= 0) return -1;
    }
    return -1;
}

int voice_lipset_before(const unsigned char* p, const unsigned char* von, int (*insn)(const unsigned char*))
{
    for (int k = 0, len; p < von && k < 4; k++, p += len)
        if ((*p != OP_LIPSTART && *p != OP_MESSAGE) || (len = insn(p)) <= 0) return 0;
    return p == von;
}

int voice_model_chr(int m)
{
    if (m >= 0 && m < ACTOR_MODELS) return cast_character(actor_models[m].id, actor_models[m].mdlver);
    if (m >= ACTOR_PART && m < ACTOR_PART + 5) return (m - ACTOR_PART) & 3;   /* the player files (4: Claire's second costume) */
    return -1;   /* imported models, the zombies' entry: no lines of their own */
}

/* the catalogue: built on the scan thread, published whole (voice_scan_done), read-only after that */
static VoiceLine *scan, *lines;
static int scan_n, scan_cap, lines_n, ready, first[CAST_CHARACTERS + 1];
int g_voice_mouths = 1;
/* the lines without a LipSet whose VoiceOn record is surely the speaker: in the same script, at least 9 in 10 of the lines
   with a LipSet that name that record are said by it; none naming it: every line with one names its speaker. Tried on the
   US disc's lines with both (the LipSet's record the speaker), each judged by the others: 294 of the 428 pass and 284 of
   those are right (97 %, against 81 % for all); 133 of the 191 lines without a LipSet pass. Also (k0) the lines placed at
   the player (kind 0) without a LipSet, which name no record: said by the record that every kind-0 line of the same
   script with a LipSet moves the mouth of (at least one; Claire waking up in RM_6000, Alfred in RM_1020) */
typedef struct { unsigned short room, vid; unsigned char rec, k0; } Sure;
static Sure* sure;
static int sure_n, sure_cap;

static void add(int room, int disc, int vid, int chr, const unsigned char* lip, int lip_n)
{
    VoiceLine* l;
    unsigned char* b;
    if (scan_n == scan_cap) {
        VoiceLine* q = realloc(scan, sizeof(VoiceLine) * (size_t)(scan_cap + 256));
        if (!q) return;
        scan = q; scan_cap += 256;
    }
    if (!(b = malloc((size_t)lip_n))) return;
    memcpy(b, lip, (size_t)lip_n);
    l = &scan[scan_n++];
    l->room = (unsigned short)room; l->vid = (unsigned short)vid; l->disc = (unsigned char)disc; l->chr = (unsigned char)chr;
    l->lip = b; l->lip_n = (unsigned short)lip_n;
}

/* lip entry k of the room's list (hed02: a u32 per enemy record, the room motions, the mouths, then the lips: size, bytes;
   -1 ends); its length up to and with the 0xFF, 0 none */
static int lip_entry(const unsigned char* d, long n, int k, const unsigned char** out)
{
    long o = rd32(d, n, rd32(d, n, 0x18) + 4 * ((long)rd32(d, n, rd32(d, n, 0x10) + 0x80 + 2 * 4) + 2));
    for (int i = 0; o > 0 && o + 4 <= n && i <= k; i++) {
        unsigned sz = rd32(d, n, o);
        if (sz == 0xFFFFFFFFu || sz > (unsigned)(n - o - 4)) return 0;
        if (i == k) {
            for (unsigned j = 0; j < sz && j < 4096; j += 2)
                if (d[o + 4 + j] == 0xFF) { *out = d + o + 4; return (int)j + 1; }
            return 0;
        }
        o += 4 + sz;
    }
    return 0;
}

void voice_scan_room(int room, const unsigned char* d, int n, int (*insn)(const unsigned char*))
{
    long rom = rd32(d, n, 0x10), base = rd32(d, n, 0x1C), ep = rd32(d, n, rom + 2 * 4);
    int nr = (int)rd32(d, n, rom + 0x80 + 2 * 4), disc = room / 1000 < 6 ? 1 : 2;
    unsigned first_ofs, offs[260];
    int k = 0;
    if (!base || base >= n) return;
    first_ofs = rd32(d, n, base);   /* hed03: offsets of scd0, scd1, the events, up to the first script */
    for (unsigned i = 0; i * 4 < first_ofs && k < 260 && base + (long)i * 4 + 4 <= n; i++) {
        unsigned v = rd32(d, n, base + (long)i * 4);
        offs[k++] = v;
        if (v && v < first_ofs) first_ofs = v;
    }
    for (int i = 0; i < k; i++) {   /* each script to its End outside any if-block (an event's also at op 255), as teleport.c sweep */
        long o = base + offs[i], ends[64], next = 0;
        int ne = 0, nq = 0, bad = 0;
        Sure q[64];   /* this script's lines without a LipSet that name a record (kind 1), or are placed at the player (kind 0) */
        unsigned short named[256] = { 0 }, right[256] = { 0 };   /* its lines with one naming each record, those said by it */
        int k0rec = -1, k0bad = 0;   /* the record its kind-0 lines with a LipSet move the mouth of; more than one */
        long pre = -1;               /* the end of the last LipSet no line before claims (pre_rec's mouth, entry pre_lip) */
        int pre_rec = 0, pre_lip = 0, claimed = 0;
        for (int j = 0; j < k; j++) if (offs[j] > offs[i] && (!next || offs[j] < (unsigned long)next)) next = offs[j];
        while (o >= 0 && o + 8 <= n && (!next || o < base + next)) {
            int op = d[o], len = insn(d + o), rec, lip;
            const unsigned char* b;
            while (ne && ends[ne - 1] <= o) ne--;
            if (len <= 0) break;
            lip = op == OP_VOICEON ? voice_lipset(d + o + len, (int)(n - o - len), insn, &rec) : -1;
            if (op == OP_VOICEON) {
                if (lip >= 0) claimed = 1;   /* (the next LipSet is this line's) */
                else if (pre >= 0 && voice_lipset_before(d + pre, d + o, insn)) { lip = pre_lip; rec = pre_rec; }   /* (the LipSet right before) */
                pre = -1;
            }
            if (op == OP_LIPSET) {
                if (claimed) claimed = 0;
                else { pre = o + len; pre_rec = d[o + 2]; pre_lip = d[o + 3]; }
            }
            if (op == OP_VOICEON && d[o + 1] == 1) {   /* (kind 1: the line names a record) */
                if (lip >= 0) { named[d[o + 6]]++; right[d[o + 6]] += rec == d[o + 6]; bad |= rec != d[o + 6]; }
                else if (nq < 64) q[nq++] = (Sure){ (unsigned short)room, (unsigned short)(d[o + 2] | d[o + 3] << 8), d[o + 6], 0 };
            } else if (op == OP_VOICEON && !d[o + 1]) {   /* (kind 0: at the player) */
                if (lip >= 0) { k0bad |= k0rec >= 0 && rec != k0rec; k0rec = rec; }
                else if (nq < 64) q[nq++] = (Sure){ (unsigned short)room, (unsigned short)(d[o + 2] | d[o + 3] << 8), 0, 1 };
            }
            if (lip >= 0 && rec < nr && ep) {
                long r = ep + rec * 0x24;   /* ETTY_WORK: id at 4, mdlver at 9 */
                int chr = r + 0x24 <= n ? cast_character(d[r + 4] | d[r + 5] << 8, d[r + 9]) : -1, bn;
                if (chr >= 0 && (bn = lip_entry(d, n, lip, &b)) > 0) add(room, disc, d[o + 2] | d[o + 3] << 8, chr, b, bn);
            }
            if (op == OP_IFEL && ne < 64) ends[ne++] = o + d[o + 1] + 1;
            else if (op == OP_ELSE) { if (ne) ne--; if (ne < 64) ends[ne++] = o + d[o + 1]; }
            else if (op == OP_ENDIF && ne) ne--;
            o += len;
            if (!ne && (op == OP_END || (i >= 2 && op == OP_EVTEND))) break;
        }
        for (int j = 0; j < nq; j++) {
            int r = q[j].rec;
            if (q[j].k0) { if (k0rec < 0 || k0bad) continue; q[j].rec = (unsigned char)k0rec; }
            else if (named[r] ? right[r] * 10 < named[r] * 9 : bad) continue;
            if (sure_n == sure_cap) {
                Sure* g = realloc(sure, sizeof(Sure) * (size_t)(sure_cap + 64));
                if (!g) break;
                sure = g; sure_cap += 64;
            }
            sure[sure_n++] = q[j];
        }
    }
}

/* ponytail: a linear search; a few hundred entries, looked up once per line */
int voice_sure(int room, int vid, int rec)
{
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) return 0;
    for (int i = 0; i < sure_n; i++) if (sure[i].room == room && sure[i].vid == vid && sure[i].rec == rec) return 1;
    return 0;
}
int voice_said_by(int room, int vid)
{
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) return -1;
    for (int i = 0; i < sure_n; i++) if (sure[i].k0 && sure[i].room == room && sure[i].vid == vid) return sure[i].rec;
    return -1;
}
int voice_sure_count(void) { return __atomic_load_n(&ready, __ATOMIC_ACQUIRE) ? sure_n : -1; }

static int by_line(const void* a, const void* b)
{
    const VoiceLine *x = a, *y = b;
    if (x->chr != y->chr) return x->chr - y->chr;
    if (x->disc != y->disc) return x->disc - y->disc;
    if (x->vid != y->vid) return x->vid - y->vid;
    return x->room - y->room;
}

void voice_scan_done(void)
{
    int i, j, n = 0;
    unsigned char* two = calloc((size_t)scan_n + 1, 1);
    if (scan_n) qsort(scan, (size_t)scan_n, sizeof(VoiceLine), by_line);
    for (i = 0; two && i < scan_n; i++)   /* a file said by two characters in the scripts is nobody's */
        for (j = 0; j < scan_n && !two[i]; j++) two[i] = scan[j].disc == scan[i].disc && scan[j].vid == scan[i].vid && scan[j].chr != scan[i].chr;
    for (i = 0; two && i < scan_n; i++) {   /* each file once: its first room's lips */
        if (two[i] || (n && scan[n - 1].disc == scan[i].disc && scan[n - 1].vid == scan[i].vid)) { free((void*)scan[i].lip); continue; }
        scan[n++] = scan[i];
    }
    free(two);
    for (int c = 0, at = 0; c <= CAST_CHARACTERS; c++) {
        while (at < n && scan[at].chr < c) at++;
        first[c] = at;
    }
    lines = scan;
    lines_n = n;
    __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
}

int voice_count(int chr)
{
    if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) return -1;
    return chr >= 0 && chr < CAST_CHARACTERS ? first[chr + 1] - first[chr] : 0;
}

const VoiceLine* voice_line(int chr, int i) { return voice_count(chr) > i && i >= 0 ? &lines[first[chr] + i] : NULL; }

static unsigned mix(unsigned seed, int room, int disc, int vid, unsigned salt)
{
    unsigned h = seed * 2654435761u ^ ((unsigned)room * 40503u + (unsigned)vid * 0x9E3779B9u + (unsigned)disc * 0x85EBCA6Bu + salt * 0xC2B2AE35u);
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}

/* how much a line of len ms fills an original of ms: 1000 + 5000 r^2 (r the part of the original's time it fills): one that
   fills it 6 times as likely as a one-word shout, one filling half 2.25 times (a soft preference: the pool stays wide) */
static unsigned fill_weight(int len, int ms)
{
    unsigned r = (unsigned)((len < ms ? len : ms) * 1000LL / ms);
    return 1000u + 5u * r * r / 1000u;
}

/* ... a line's: a gasp, grunt or shout (under VOICE_SHORT_MS) in place of a spoken line ten times less likely again */
static unsigned line_weight(int len, int ms)
{
    unsigned w = fill_weight(len, ms);
    return ms >= VOICE_SHORT_MS && len < VOICE_SHORT_MS ? w / 10u : w;
}

const VoiceLine* voice_pick(unsigned seed, int room, int disc, int vid, int chr, int ms, int max_ms, int (*ms_of)(const VoiceLine*),
                            int (*used)(const VoiceLine*))
{
    const VoiceLine* l = chr >= 0 && chr < CAST_CHARACTERS ? &lines[first[chr]] : NULL;
    int n = voice_count(chr), i, len, best = -1, best_ms = 0, pass;
    unsigned h, sum = 0;
    if (n <= 0) return NULL;
    h = mix(seed, room, disc, vid, (unsigned)chr);
    if (ms <= 0 || max_ms <= 0 || !ms_of) max_ms = 0;   /* (any, alike) */
    for (pass = used ? 0 : 1; pass < 2 && !sum; pass++)   /* the ones not said yet in the room, else all */
        for (i = 0; i < n; i++) {
            if (!pass && used(&l[i])) continue;
            if (!max_ms) sum += 1000u;
            else if ((len = ms_of(&l[i])) > 0 && len <= max_ms) sum += line_weight(len, ms);   /* (the ones that end in time, weighted) */
            else if (len > 0 && (best < 0 || len < best_ms)) { best = i; best_ms = len; }
        }
    if (!sum) return best >= 0 ? &l[best] : &l[h % (unsigned)n];   /* (none ends in time: the shortest) */
    h %= sum;
    for (pass--, i = 0; i < n; i++) {
        unsigned w;
        if (!pass && used(&l[i])) continue;
        if (!max_ms) w = 1000u;
        else if ((len = ms_of(&l[i])) > 0 && len <= max_ms) w = line_weight(len, ms);
        else continue;
        if (h < w) break;
        h -= w;
    }
    return &l[i];
}

/* ---------------------------------------------------------------- an enemy model's sounds in place of a line
   Every enemy sound is a program of SE bank 3, which a room's sound pack fills with its own types' sounds (docs/ENEMIES.md
   "Sounds of added types"). The programs here are each type's cries as its code requests them (enNN.c): those on the
   enemy voice channel (SeNo bit 24: the zombies' moans, the dog's barks, Nosferatu's screams), else its hurt and death
   cries, else what its moves sound like (the Tyrant has no voice: its blows and stomps). Each room's bank holds that
   type's sounds and no other type's under those numbers (the numbers collide between types). Program numbers only:
   the sounds are read from the disc. */
static const struct { unsigned char id; unsigned short room; unsigned char n, prog[8]; } creature[] = {
    {  3, 7080, 5, { 3, 4, 14, 16, 17 } },          /* Black Widow: bhEne03_CallSE's, bhEne03_DamageInit's 16, 17 */
    {  4, 3020, 8, { 2, 3, 4, 6, 7, 9, 13, 14 } },  /* Cerberus: the voice channel's barks, growls, howls */
    {  5, 8010, 2, { 17, 18 } },                    /* Hunter (both): bhEne05_InitDamage's hurt and death cries */
    {  6, 6030, 3, { 0, 3, 4 } },                   /* Moth: death, grab, hurt */
    {  7,  130, 3, { 5, 6, 7 } },                   /* Bat: the voice channel's 5, the death squeals */
    {  9, 2000, 4, { 12, 14, 15, 16 } },            /* Bandersnatch (both): the voice channel's (RM_3130 has no bank 3) */
    { 11, 7081, 2, { 1, 2 } },                      /* Spotter: its own two (the Hunters' rooms' bank) */
    { 12, 5820, 3, { 7, 8, 9 } },                   /* Alexia (First Form): the voice channel's */
    { 15, 5800, 3, { 10, 11, 12 } },                /* Nosferatu: the voice channel's (13 is in no bank) */
    { 17, 9280, 3, { 2, 8, 9 } },                   /* Monster Steve: the voice channel's */
    { 19, 5010, 5, { 5, 6, 7, 9, 13 } },            /* Tyrant (both): its voice channel's 2..4 are in no bank */
    { 20, 9170, 4, { 0, 1, 2, 3 } },                /* Ant: its room's four */
    { 21, 3140, 3, { 0, 1, 2 } },                   /* Albinoid (Infant): its room's three (RM_3120's are zombies') */
    { 22, 7100, 7, { 1, 2, 4, 5, 6, 10, 11 } },     /* Albinoid (Adult): the voice channel's */
    { 23, 9050, 5, { 3, 14, 16, 18, 20 } },         /* Giant Black Widow: bhEne23_CallSE's, bhEne23_DG04's 20 */
    { 26,  110, 4, { 20, 21, 22, 23 } },            /* Anatomist Zombie: the voice channel's (bhEne26_SePlay) */
};
/* a zombie's moans: programs 20..23 + add_seno[the type of its model version] (en01.c bhEne01_SePlay, ene01_typ_tbl) */
static const unsigned char zom_typ[][2] = { { 0, 0 }, { 1, 1 }, { 31, 2 }, { 18, 0 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 32, 6 },
    { 33, 7 }, { 34, 8 }, { 21, 9 }, { 19, 3 }, { 5, 10 }, { 6, 11 }, { 35, 12 }, { 36, 13 }, { 22, 14 }, { 20, 10 }, { 7, 15 },
    { 8, 16 }, { 37, 17 }, { 38, 18 }, { 24, 15 }, { 9, 19 }, { 10, 20 }, { 25, 21 }, { 39, 22 }, { 40, 23 }, { 41, 24 },
    { 42, 25 }, { 43, 26 } };
static const unsigned char zom_add[27] = { 28, 32, 16, 0, 4, 8, 32, 36, 0, 8, 20, 24, 8, 12, 8, 12, 16, 4, 40, 36, 40, 0, 8, 24, 20, 28, 8 };
#define ZOM_TYPES (int)(sizeof zom_typ / sizeof zom_typ[0])

int voice_creature(int m, int alt, int* room, unsigned char* prog, int max)
{
    const ActorModel* am = m >= 0 && m < ACTOR_MODELS ? &actor_models[m] : NULL;
    int i, n = 0, add = 0;
    if (!am || max < 44) return 0;
    if (am->id == 1 || am->id == 51 || am->id == 52) {   /* a zombie: its look's moans in its room, any there, RM_0030's */
        for (i = 0; i < ZOM_TYPES && zom_typ[i][0] != am->mdlver; i++);
        if (am->id == 1 && i < ZOM_TYPES) add = zom_add[zom_typ[i][1]];
        else if (!alt) alt = 1;   /* (the scripted zombies have no code of their own: any moans) */
        if (alt > 2) return 0;
        *room = alt == 2 || am->room == 9050 ? 30 : am->room;   /* (RM_9050's bank 3 is the spiders': Bandaged 2) */
        for (i = 20; i < 64; i++) if (alt || (i >= 20 + add && i < 24 + add)) prog[n++] = (unsigned char)i;
        return n;
    }
    for (i = 0; !alt && i < (int)(sizeof creature / sizeof creature[0]); i++)
        if (creature[i].id == am->id) {
            *room = creature[i].room;
            memcpy(prog, creature[i].prog, creature[i].n);
            return creature[i].n;
        }
    return 0;   /* the fish (no sounds), the hazmat man, the story cast (lines of their own) */
}

int voice_creature_pick(unsigned seed, int room, int disc, int vid, const int* len, int n, int ms, int max_ms, int last)
{
    unsigned h = mix(seed, room, disc, vid, 0x100u), sum = 0;
    int i, best = 0, pass;
    if (n <= 0) return -1;
    if (ms <= 0 || max_ms <= 0) max_ms = 0x7FFFFFFF, ms = 1;   /* (any, alike) */
    for (i = 1; i < n; i++) if (len[i] < len[best]) best = i;
    for (pass = n > 1 && last >= 0 && last < n ? 0 : 1; pass < 2 && !sum; pass++)   /* not the one before again, unless none else fits */
        for (i = 0; i < n; i++)
            if ((pass || i != last) && len[i] > 0 && len[i] <= max_ms) sum += fill_weight(len[i], ms);   /* (weighted as voice_pick's) */
    if (!sum) return best;   /* (none ends in time: the shortest) */
    h %= sum;
    for (pass--, i = 0; i < n; i++)
        if ((pass || i != last) && len[i] > 0 && len[i] <= max_ms) {
            unsigned w = fill_weight(len[i], ms);
            if (h < w) return i;
            h -= w;
        }
    return best;
}
