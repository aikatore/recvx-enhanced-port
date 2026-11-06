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

enum { OP_END = 0, OP_IFEL = 1, OP_ELSE = 2, OP_ENDIF = 3, OP_VOICEON = 25, OP_LIPSET = 61, OP_EVTEND = 255 };

static unsigned rd32(const unsigned char* d, long n, long o) { return o >= 0 && o <= n - 4 ? d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (unsigned)d[o + 3] << 24 : 0; }

int voice_lipset(const unsigned char* p, int n, int (*insn)(const unsigned char*), int* rec)
{
    for (int k = 0, o = 0, len; k < 16 && o + 4 <= n; k++, o += len) {
        if (p[o] == OP_LIPSET) { *rec = p[o + 2]; return p[o + 3]; }
        if (p[o] == OP_VOICEON || p[o] == OP_END || p[o] == OP_EVTEND || (len = insn(p + o)) <= 0) return -1;
    }
    return -1;
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
        int ne = 0;
        for (int j = 0; j < k; j++) if (offs[j] > offs[i] && (!next || offs[j] < (unsigned long)next)) next = offs[j];
        while (o >= 0 && o + 8 <= n && (!next || o < base + next)) {
            int op = d[o], len = insn(d + o), rec, lip;
            const unsigned char* b;
            while (ne && ends[ne - 1] <= o) ne--;
            if (len <= 0) break;
            if (op == OP_VOICEON && (lip = voice_lipset(d + o + len, (int)(n - o - len), insn, &rec)) >= 0 && rec < nr && ep) {
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
    }
}

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

const VoiceLine* voice_pick(unsigned seed, int room, int disc, int vid, int chr)
{
    unsigned h = seed * 2654435761u ^ ((unsigned)room * 40503u + (unsigned)vid * 0x9E3779B9u + (unsigned)disc * 0x85EBCA6Bu + (unsigned)chr * 0xC2B2AE35u);
    int n = voice_count(chr);
    if (n <= 0) return NULL;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return &lines[first[chr] + (int)(h % (unsigned)n)];
}
