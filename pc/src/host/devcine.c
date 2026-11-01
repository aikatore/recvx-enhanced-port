/* --dev cutscene log: the list and its text (devcine.h, docs/DEV_TOOLS.md). Pure; ctest "devcine". */
#include "devcine.h"
#include "fileio.h"
#include "../net/coop_proto.h"   /* coop_insn_len_raw, coop_op_cond (the scene list) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_devcine_show = 1;

static struct {
    volatile int lock;
    DevCine e[DC_MAX];                  /* ring: e[(head + i) % DC_MAX], oldest first */
    int head, n, open;
    unsigned no;
} lg;

static void lock(void) { while (__atomic_exchange_n(&lg.lock, 1, __ATOMIC_ACQUIRE)) {} }
static void unlock(void) { __atomic_store_n(&lg.lock, 0, __ATOMIC_RELEASE); }
static DevCine* last(void) { return lg.n ? &lg.e[(lg.head + lg.n - 1) % DC_MAX] : NULL; }

void devcine_begin(const DevCine* in)
{
    DevCine* l;
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);   /* (game thread only) */
    lock();
    l = last();
    if (l && l->kind == in->kind && l->room == in->room && !strcmp(l->key, in->key)) l->repeat++;   /* the same again: counted, its first start kept */
    else {
        if (lg.n == DC_MAX) { lg.head = (lg.head + 1) % DC_MAX; lg.n--; }
        l = &lg.e[(lg.head + lg.n++) % DC_MAX];
        *l = *in;
        l->no = ++lg.no;
        l->repeat = 1;
        if (tm) strftime(l->when, sizeof l->when, "%H:%M:%S", tm);
        else snprintf(l->when, sizeof l->when, "?");
    }
    lg.open = 1;
    unlock();
}

void devcine_update(int frames, const char* what, const char* flags, const char* cast)
{
    DevCine* l;
    lock();
    if (lg.open && (l = last())) {
        l->frames = frames;
        if (what) snprintf(l->what, sizeof l->what, "%s", what);
        if (flags) snprintf(l->flags, sizeof l->flags, "%s", flags);
        if (cast) snprintf(l->cast, sizeof l->cast, "%s", cast);
    }
    unlock();
}

void devcine_end(void) { lock(); lg.open = 0; unlock(); }

int devcine_snapshot(DevCine* out, int* open)
{
    int i, n;
    lock();
    n = lg.n;
    for (i = 0; i < n; i++) out[i] = lg.e[(lg.head + i) % DC_MAX];
    if (open) *open = lg.open && n;
    unlock();
    return n;
}

void devcine_clear(void)   /* (the scene playing now stays) */
{
    lock();
    if (lg.open && lg.n) { lg.e[0] = *last(); lg.n = 1; }
    else lg.n = 0;
    lg.head = 0;
    unlock();
}

const char* devcine_who(int ply)
{
    static const char* const who[4] = { "Claire", "Chris", "Steve", "Wesker" };
    return ply >= 0 && ply < 4 ? who[ply] : "?";
}

int devcine_format(const DevCine* e, int full, char* buf, int n)
{
    char rep[16] = "";
    int r;
    if (e->repeat > 1) snprintf(rep, sizeof rep, " x%d", e->repeat);
    if (!full)
        r = snprintf(buf, (size_t)n, "#%u RM_%04d %s  %.1f s%s  %s", e->no, e->room, e->what, e->frames / 30.0, rep, devcine_who(e->ply));
    else
        r = snprintf(buf, (size_t)n, "#%u %s RM_%04d (stage %d room %02d case %d) | %s %s | %s | %.1f s%s | player %s | %s", e->no, e->when, e->room,
                     e->room / 1000, e->room / 10 % 100, e->room % 10, e->kind == DC_FMV ? "movie" : "event", e->what, e->flags[0] ? e->flags : "-",
                     e->frames / 30.0, rep, devcine_who(e->ply), e->cast[0] ? e->cast : "no cast");
    return r < 0 ? 0 : r >= n ? n - 1 : r;
}

int devcine_text(char* buf, int n)
{
    DevCine* s = malloc(sizeof(DevCine) * DC_MAX);
    int k, cnt = s ? devcine_snapshot(s, NULL) : 0, len = 0;
    if (n > 0) buf[0] = 0;
    for (k = 0; k < cnt && len < n - 1; k++) {
        len += devcine_format(&s[k], 1, buf + len, n - len);
        if (len < n - 1) { buf[len++] = '\n'; buf[len] = 0; }
    }
    free(s);
    return len;
}

int devcine_export(const char* path)
{
    char* text = malloc(DEVCINE_TEXT), stamp[32] = "?";
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    int len = text ? devcine_text(text, DEVCINE_TEXT) : 0, lines = 0, i, ok;
    FILE* f = text ? pc_fopen(path, "a") : NULL;
    if (!f) { free(text); return -1; }
    if (tm) strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tm);
    for (i = 0; i < len; i++) lines += text[i] == '\n';
    fprintf(f, "# recvx cutscene log, exported %s: %d %s\n%s", stamp, lines, lines == 1 ? "scene" : "scenes", text);
    ok = !ferror(f);
    free(text);
    return fclose(f) == 0 && ok ? lines : -1;
}

/* ---------------------------------------------------------------- the scene list (devcine.h DevScene) */
enum { SC_DEPTH = 24, SC_GROUPS = 512, SC_EVENTS = 256, SC_RECS = 128 };
typedef struct { unsigned int end, hdr, els; int kind; } Frame;   /* kind 1 a then-part, 2 an else part; hdr: its IfelCk, els: its ElseCk */
typedef struct { int k, nfr, nev; unsigned int key; Frame fr[SC_DEPTH]; short ev[DS_EV]; } Group;   /* the EvtOn of one block */
typedef struct {
    unsigned char scene, nmv, mv[DS_MV], starts[SC_EVENTS / 8], recs[SC_RECS / 8];
    unsigned short shots, voices, lines;
    int text;                           /* its first message with letters, -1 */
} EvInfo;
typedef struct { const unsigned char* d; long n, base, rom; Group* g; int ng; EvInfo* ev; int nev; } Scan;

static unsigned rd32(const unsigned char* d, long n, long o) { return o >= 0 && o <= n - 4 ? d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (unsigned)d[o + 3] << 24 : 0; }
static int insn(const unsigned char* d, long n, long o)
{
    int len = o >= 0 && o + 2 <= n ? coop_insn_len_raw(d + o) : 0;
    return len > 0 && o + len <= n ? len : 0;
}
#define BIT(a, i) ((a)[(i) >> 3] >> ((i) & 7) & 1)
#define SETBIT(a, i) ((a)[(i) >> 3] |= (unsigned char)(1 << ((i) & 7)))

/* message i of the room as text (0xFExx starts it over: the typed-out captions; "~" an item's name); 0: none with letters */
static int mes_text(const Scan* c, int i, char* out, int n)
{
    long t = (long)rd32(c->d, c->n, c->rom + 14 * 4), o;
    int len = 0, letters = 0;
    out[0] = 0;
    if (!t || i < 0 || (unsigned)i >= rd32(c->d, c->n, c->rom + 0x80 + 12 * 4)) return 0;
    o = t + (long)rd32(c->d, c->n, t + 4 * (i + 1));
    for (int k = 0; k < 1024 && o + 2 <= c->n && len < n - 1; k++, o += 2) {
        unsigned v = c->d[o] | c->d[o + 1] << 8;
        if (v == 0xFFFF) break;
        if ((v >> 8) == 0xFE) { len = letters = 0; continue; }
        if (v == 0xFF03) { o += 2; out[len++] = '~'; continue; }   /* (an item's name, a wait: an operand) */
        if (v == 0xFF02) o += 2;
        if (v >= 0xFF00) { if (len && out[len - 1] != ' ') out[len++] = ' '; continue; }
        if (v < 95) { char ch = (char)(v + 32); out[len++] = ch; letters += (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z'); }
    }
    while (len && out[len - 1] == ' ') len--;
    out[len] = 0;
    return letters >= 2;
}

static void add_ev(Scan* c, int k, const Frame* st, int ns, int v)
{
    unsigned key = ns ? (st[ns - 1].kind == 2 ? st[ns - 1].els : st[ns - 1].hdr) + 1 : 0;
    Group* g;
    int i;
    for (i = c->ng - 1; i >= 0 && !(c->g[i].k == k && c->g[i].key == key); i--) {}
    if (i < 0) {
        if (c->ng == SC_GROUPS) return;
        g = &c->g[c->ng++];
        memset(g, 0, sizeof *g);
        g->k = k; g->key = key; g->nfr = ns;
        memcpy(g->fr, st, sizeof(Frame) * (size_t)ns);
    } else g = &c->g[i];
    for (i = 0; i < g->nev && g->ev[i] != v; i++) {}
    if (i == g->nev && g->nev < DS_EV) g->ev[g->nev++] = (short)v;
}

/* script k (0 scd0, 1 scd1, 2 + e event e), swept as teleport.c and pc/tools/rdx.py disasm_block */
static void sweep(Scan* c, int k, long o, long hint)
{
    const unsigned char* d = c->d;
    Frame st[SC_DEPTH];
    int ns = 0, ev = k >= 2;
    EvInfo* e = ev && k - 2 < c->nev ? &c->ev[k - 2] : NULL;
    while (o >= 0 && o < c->n) {
        int op = d[o], len;
        while (ns && st[ns - 1].end <= (unsigned)o) ns--;
        if (!(len = insn(d, c->n, o))) return;
        if (e && len >= 4) {
            if ((op == 18 && (d[o + 1] == 0 || d[o + 1] == 3)) || (op == 19 && d[o + 1] == 0) || op == 94 || (op == 36 && d[o + 1] == 0 && d[o + 3] == 0)) e->scene = 1;
            if (op == 19 && d[o + 1] == 0) e->shots++;
            if (op == 25) e->voices++;
            if (op == 94 && e->nmv < DS_MV) e->mv[e->nmv++] = d[o + 1];
            if (op == 31 && d[o + 1] == 0) {
                char t[8];
                e->lines++;
                if (e->text < 0 && mes_text(c, d[o + 2], t, sizeof t)) e->text = d[o + 2];
            }
            if (((op == 101 && d[o + 1] == 1) || (op == 36 && d[o + 1] == 1 && d[o + 3])) && d[o + 2] < SC_RECS) SETBIT(e->recs, d[o + 2]);   /* LoadWork, InitModelSet shown: a record */
        } else if (e && op == 18 && (d[o + 1] == 0 || d[o + 1] == 3)) e->scene = 1;   /* (CineSet: 2 bytes) */
        if (op == 20) {   /* EvtOn: level, event */
            int v = d[o + 2] | d[o + 3] << 8;
            add_ev(c, k, st, ns, (v & 0xFF) << 8 | v >> 8);
            if (e) SETBIT(e->starts, v >> 8);
        }
        if (op == 1 && ns < SC_DEPTH) { st[ns].end = (unsigned)(o + d[o + 1] + 1); st[ns].hdr = (unsigned)o; st[ns].els = 0; st[ns++].kind = 1; }
        else if (op == 2) {
            unsigned h = ns ? st[--ns].hdr : 0;
            if (ns < SC_DEPTH) { st[ns].end = (unsigned)(o + d[o + 1]); st[ns].hdr = h; st[ns].els = (unsigned)o; st[ns++].kind = 2; }
        } else if (op == 3 && ns) ns--;
        o += len;
        if (!ns && (op == 0 || (ev && op == 255))) return;
        if (hint && o > hint) return;
    }
}

/* the tests of the IfelCk at h (their offsets, up to max); *then0: where its then-part starts */
static int tests(const Scan* c, unsigned h, long* at, int max, long* then0)
{
    long o = (long)h + 2;
    int k = 0, len;
    while (h && o < c->n && coop_op_cond(c->d[o]) && (len = insn(c->d, c->n, o))) { if (k < max) at[k] = o; k++; o += len; }
    if (then0) *then0 = o;
    return k;
}

static void closure(const Scan* c, int e, unsigned char* seen, short* order, int* n)
{
    if (e < 0 || e >= c->nev || BIT(seen, e)) return;
    SETBIT(seen, e);
    order[(*n)++] = (short)e;
    for (int x = 0; x < SC_EVENTS; x++) if (BIT(c->ev[e].starts, x)) closure(c, x, seen, order, n);
}

/* scene s's tests and the part that starts it, from group g; 0: the skip's ending, not a scene */
static int guard(const Scan* c, const Group* g, DevScene* s)
{
    const unsigned char* d = c->d;
    for (int f = 0; f < g->nfr; f++) {   /* the blocks around it: their tests (an else part's when it has only one) */
        long at[16];
        int nt = tests(c, g->fr[f].hdr, at, 16, NULL), inv = g->fr[f].kind == 2;
        if (inv && nt != 1) continue;
        for (int t = 0; t < nt && t < 16; t++) {
            const unsigned char* p = d + at[t];
            if (p[0] == 4) {   /* Ck set bit: continues if (bit set) != p[5] */
                int set = p[1], bit = (p[2] | p[3] << 8) & 0x3FF, on = (p[5] == 0) ^ inv, m;
                if (set == 10 && bit == 3 && on) return 0;   /* cb_flg 0x10000000: the skip button */
                if (set != 1 && set != 2 && set != 3 && set != 7 && set != 8) continue;
                for (m = 0; m < s->nflag && !(s->flag[m].set == set && s->flag[m].bit == bit); m++) {}
                if (m == s->nflag && m < DS_FLAGS) { s->flag[m].set = (unsigned char)set; s->flag[m].bit = (unsigned short)bit; s->flag[m].on = (unsigned char)on; s->nflag++; }
            } else if (p[0] == 6 && p[1] == 24 && p[2] == 0 && !inv) s->ply = p[3];   /* CmpB ply_id == */
        }
    }
    if (g->nfr && g->k < 2) {   /* the part of the room's script that starts it (not an event's: Sleep, While are a task's) */
        const Frame* f = &g->fr[g->nfr - 1];
        long b, e;
        if (f->kind == 1) { tests(c, f->hdr, NULL, 0, &b); e = (long)f->hdr + d[f->hdr + 1]; }
        else { b = (long)f->els + 2; e = (long)f->els + d[f->els + 1]; }
        if (f->hdr && b >= c->base && b <= e && e < c->n && (d[e] == 2 || d[e] == 3)) { s->block = (unsigned)(b - c->base); s->blen = (unsigned)(e - b); }
    }
    return 1;
}

int devcat_room(int room, const unsigned char* d, int n, DevWho who, DevScene* out, int max, int* unused)
{
    Scan c;
    unsigned char part[SC_EVENTS / 8] = { 0 }, started[SC_EVENTS / 8] = { 0 };
    unsigned offs[2 + SC_EVENTS], first;
    int k = 0, cnt = 0;
    memset(&c, 0, sizeof c);
    c.d = d; c.n = n; c.rom = rd32(d, n, 0x10); c.base = rd32(d, n, 0x1C);
    if (unused) *unused = 0;
    if (!c.base || c.base >= n) return 0;
    c.g = malloc(sizeof(Group) * SC_GROUPS);
    c.ev = calloc(SC_EVENTS, sizeof(EvInfo));
    if (!c.g || !c.ev) { free(c.g); free(c.ev); return 0; }
    first = rd32(d, n, c.base);   /* hed03: offsets of scd0, scd1, the events, up to the first script */
    for (unsigned i = 0; i * 4 < first && k < 2 + SC_EVENTS && c.base + (long)i * 4 + 4 <= n; i++) {
        unsigned v = rd32(d, n, c.base + (long)i * 4);
        offs[k++] = v;
        if (v && v < first) first = v;
    }
    c.nev = k > 2 ? k - 2 : 0;
    for (int i = 0; i < SC_EVENTS; i++) c.ev[i].text = -1;
    for (int i = 0; i < k; i++) {
        unsigned next = 0;
        for (int j = 0; j < k; j++) if (offs[j] > offs[i] && (!next || offs[j] < next)) next = offs[j];
        sweep(&c, i, c.base + offs[i], next ? c.base + next : 0);
    }
    for (int pass = 0; pass < 2; pass++)   /* the room's scripts' blocks, then the events' (those not part of a scene) */
        for (int i = 0; i < c.ng && cnt < max; i++) {
            const Group* g = &c.g[i];
            DevScene* s = &out[cnt];
            short order[SC_EVENTS];
            unsigned char seen[SC_EVENTS / 8] = { 0 }, recs[SC_RECS / 8] = { 0 };
            int no = 0, j;
            if ((g->k >= 2) != pass || (g->k >= 2 && BIT(part, g->k - 2))) continue;
            for (j = 0; j < g->nev && !((g->ev[j] & 0xFF) < c.nev && c.ev[g->ev[j] & 0xFF].scene); j++) {}
            if (j == g->nev) continue;   /* none of its events is a cutscene */
            memset(s, 0, sizeof *s);
            s->room = (short)room; s->from = (short)g->k; s->ply = 0xFF;
            s->nev = (unsigned char)g->nev;
            memcpy(s->ev, g->ev, sizeof(short) * (size_t)g->nev);
            for (j = 0; j < cnt && !(out[j].nev == s->nev && !memcmp(out[j].ev, s->ev, sizeof(short) * s->nev)); j++) {}
            if (j < cnt || !guard(&c, g, s)) continue;   /* the same events from another block; the skip's ending */
            for (j = 0; j < g->nev; j++) closure(&c, g->ev[j] & 0xFF, seen, order, &no);
            for (j = 0; j < no; j++) {
                const EvInfo* e = &c.ev[order[j]];
                SETBIT(part, order[j]);
                s->shots += e->shots; s->voices += e->voices; s->lines += e->lines;
                for (int m = 0; m < e->nmv && s->nmv < DS_MV; m++) s->mv[s->nmv++] = e->mv[m];
                if (!s->text[0] && e->text >= 0) mes_text(&c, e->text, s->text, sizeof s->text);
                for (int r = 0; r < SC_RECS / 8; r++) recs[r] |= e->recs[r];
            }
            {   /* the records' names (ETTY_WORK 0x24 bytes: id at 4, mdlver at 9) */
                long ep = rd32(d, n, c.rom + 2 * 4);
                int nr = (int)rd32(d, n, c.rom + 0x80 + 2 * 4), len = 0;
                for (int r = 0; r < SC_RECS && r < nr && who && ep; r++) {
                    long o = ep + r * 0x24;
                    const char* nm;
                    if (!BIT(recs, r) || o + 0x24 > n || !(nm = who(d[o + 4] | d[o + 5] << 8, d[o + 9])) || strstr(s->who, nm)) continue;
                    len += snprintf(s->who + len, sizeof s->who - (size_t)len, "%s%s", len ? ", " : "", nm);
                    if (len >= (int)sizeof s->who - 1) break;
                }
            }
            cnt++;
        }
    for (int i = 0; i < c.ng; i++)
        for (int j = 0; j < c.g[i].nev; j++) SETBIT(started, c.g[i].ev[j] & 0xFF);
    for (int e = 0; unused && e < c.nev; e++) *unused += c.ev[e].scene && !BIT(part, e) && !BIT(started, e);
    free(c.g); free(c.ev);
    return cnt;
}

/* ours, for the main scenes (by room file and first event); the others are described by devcat_line */
static const struct { short room, ev; const char* name; } titles[] = {
    { 0, 0 << 8 | 29, "The opening movie" },
    { 0, 0 << 8 | 0, "Rodrigo releases Claire" },
    { 20, 0 << 8 | 0, "The graveyard" },
    { 30, 0 << 8 | 0, "Claire meets Steve" },
    { 40, 0 << 8 | 0, "Steve and Claire at the computer" },
    { 9280, 0 << 8 | 0, "Steve's transformation" },
    { 9350, 0 << 8 | 3, "Chris finds Claire; Wesker" },
    { 1020, 1 << 8 | 1, "Alfred's first appearance" },
    { 5001, 0 << 8 | 5, "The end of Claire's part" },
    { 7240, 0 << 8 | 0, "Chris arrives on Rockfort Island (Chris's part starts)" },
    { 9360, 10 << 8 | 21, "The ending movie and the credits" },
};

const char* devcat_title(const DevScene* s)
{
    for (size_t i = 0; i < sizeof titles / sizeof titles[0]; i++)
        if (titles[i].room == s->room && s->nev && titles[i].ev == s->ev[0]) return titles[i].name;
    return NULL;
}

int devcat_short(const DevScene* s) { return s->shots <= 2 && !s->voices && !s->nmv && !s->who[0]; }

int devcat_line(const DevScene* s, const char* room_name, char* buf, int n)
{
    const char* t = devcat_title(s);
    char name[112];
    int len, i;
    if (t) snprintf(name, sizeof name, "%s", t);
    else if (s->nmv) snprintf(name, sizeof name, "Movie MV_%03u", s->mv[0]);
    else if (s->who[0]) snprintf(name, sizeof name, "Scene with %s", s->who);
    else if (s->voices) snprintf(name, sizeof name, "Voiced scene");
    else if (s->text[0]) snprintf(name, sizeof name, "\"%.60s\"", s->text);
    else if (s->shots > 2) snprintf(name, sizeof name, "%u-shot scene", s->shots);
    else snprintf(name, sizeof name, "Short shot");
    len = snprintf(buf, (size_t)n, "RM_%04d %s | %s |", s->room, room_name ? room_name : "", name);
    for (i = 0; i < s->nev && len < n - 1; i++) len += snprintf(buf + len, (size_t)(n - len), "%s%d:%d", i ? ", " : " ev ", s->ev[i] >> 8, s->ev[i] & 0xFF);
    for (i = 0; i < s->nmv && len < n - 1; i++) len += snprintf(buf + len, (size_t)(n - len), "%sMV_%03u", i ? ", " : " | ", s->mv[i]);
    if (s->who[0] && len < n - 1) len += snprintf(buf + len, (size_t)(n - len), " | %s", s->who);
    if (len < n - 1) len += snprintf(buf + len, (size_t)(n - len), " | %u shots, %u lines, %u voice%s%s", s->shots, s->lines, s->voices,
                                     s->from == 0 ? ", on entry" : "", s->ply < 4 ? (s->ply == 0 ? ", Claire" : s->ply == 1 ? ", Chris" : s->ply == 2 ? ", Steve" : ", Wesker") : "");
    if (s->text[0] && (t || s->nmv || s->who[0] || s->voices) && len < n - 1) len += snprintf(buf + len, (size_t)(n - len), " | \"%.60s\"", s->text);
    return len < 0 ? 0 : len >= n ? n - 1 : len;
}

static struct { DevScene* list; int n, unused; volatile int ready, req, chr; char say[200]; } cat;

void devcat_publish(DevScene* list, int n, int unused)
{
    cat.list = list; cat.n = n; cat.unused = unused;
    __atomic_store_n(&cat.ready, 1, __ATOMIC_RELEASE);
}
int devcat_count(void) { return __atomic_load_n(&cat.ready, __ATOMIC_ACQUIRE) ? cat.n : 0; }
const DevScene* devcat_get(int i) { return i >= 0 && i < devcat_count() ? &cat.list[i] : NULL; }
int devcat_unused(void) { return devcat_count() ? cat.unused : 0; }

void devcat_play(int i, int chr)
{
    __atomic_store_n(&cat.chr, chr, __ATOMIC_RELAXED);
    __atomic_store_n(&cat.req, i + 1, __ATOMIC_RELEASE);
}
int devcat_take(int* chr)
{
    int r = __atomic_exchange_n(&cat.req, 0, __ATOMIC_ACQ_REL);
    if (chr) *chr = __atomic_load_n(&cat.chr, __ATOMIC_RELAXED);
    return r - 1;
}

void devcat_say(const char* text)
{
    lock();
    snprintf(cat.say, sizeof cat.say, "%s", text);
    unlock();
}
void devcat_status(char* buf, int n)
{
    lock();
    snprintf(buf, (size_t)n, "%s", cat.say);
    unlock();
}
