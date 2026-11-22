/* Resident Evil: The Umbrella Chronicles: its voice lines for "Randomize Voice Lines" (docs/IMPORT_UMBRELLA.md "Voice lines";
   the game side: host/voices.c voice_import_load, glue/cast_glue.c). Read from the user's RVZ image at import time, written to
   <import dir>/umbrella/: voice/<n>.adx, voices.txt (one line per file: n, source id, speaker guess, confidence, ms, kind,
   scenario) and voices_text.txt (source id, its English subtitle: the user's own disc data, for the voice tool; never in the
   repository). Our own reading of the formats as the Wii modding community documents them (BRSAR, RWSD, BRSTM, DSP-ADPCM).

   The lines: the stream sounds of Data/sound/biosound.brsar named VO_... (the ones the game plays; the disc's other VO files
   are unused retakes), each Data/sound/stream/<name>.brstm, DSP-ADPCM mono 32 kHz (a few 48 kHz). The grunts: the RWSD of the
   brsar's groups CV1_Gr .. CV9_Gr (CV7 is CV6 again), one file per wave. Decoded, brought to Code: Veronica's loudness (about
   -20 dBFS, the peaks at most -1 dBFS; the disc's are about 7 dB louder) and written as standard CRI ADX (fixed
   coefficients, cutoff 500 Hz, 18-byte frames) at the file's rate.

   Who says a line is not on the disc. The guess (uc_voice_guess): the scenario's speakers by the id (VO_<story><sub-scenario>
   <chapter><0/1><nnn>), one of them alone ("pair"); of two, a line that calls the other by name or names the speaker ("sure"),
   else the line right before or after such a line in the same numbered dialogue, the other one ("guess"); the other lines of
   two speakers a suggestion for the voice tool ("weak", not used in the game until checked: the turn further along the dialogue,
   the pitch, weaker words, the first speaker); else none. */
#include "import.h"
#include "../host/fileio.h"
#include "../host/lang.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);

static uint32_t be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t be16(const unsigned char* p) { return (uint32_t)(p[0] << 8 | p[1]); }
static int in(uint32_t o, uint32_t len, uint32_t n) { return o <= n && len <= n - o; }

/* ---------------------------------------------------------------- DSP-ADPCM, BRSTM, RWSD
   DSP-ADPCM: frames of 8 bytes, a header byte (coefficient pair, scale exponent) and 14 nibbles; 8 coefficient pairs */
static int dsp_decode(const unsigned char* d, uint32_t nb, int ns, const short* coef, int* h1, int* h2, short* out)
{
    int o = 0;
    for (uint32_t i = 0; o < ns && i < nb; i += 8) {
        int ps = d[i], sc = 1 << (ps & 15), c1 = coef[(ps >> 4 & 7) * 2], c2 = coef[(ps >> 4 & 7) * 2 + 1];
        for (int k = 0; k < 14 && o < ns && i + 1 + k / 2 < nb; k++) {
            int nib = k & 1 ? d[i + 1 + k / 2] & 15 : d[i + 1 + k / 2] >> 4;
            int64_t s;
            if (nib >= 8) nib -= 16;
            s = ((int64_t)nib * sc * 2048 + 1024 + (int64_t)c1 * *h1 + (int64_t)c2 * *h2) >> 11;
            s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            *h2 = *h1; *h1 = (int)s; out[o++] = (short)s;
        }
    }
    return o;
}
static void coefs(const unsigned char* p, short* c) { for (int i = 0; i < 16; i++) c[i] = (short)be16(p + 2 * i); }
#define MAX_SAMPLES (48000 * 120)

/* a BRSTM's first channel (HEAD: the stream info, the channels' coefficients; DATA: blocks interleaved by channel) */
static short* brstm_pcm(const unsigned char* d, uint32_t n, int* rate, int* count)
{
    uint32_t ho, base, p1, p3, co, ci, dofs, pos, nblk, bs, spb, lsam, lpad, total;
    int ch, h1 = 0, h2 = 0, got = 0;
    short c[16], *out;
    if (n < 0x40 || memcmp(d, "RSTM", 4) || !in(ho = be32(d + 0x10), 0x20, n) || memcmp(d + ho, "HEAD", 4)) return NULL;
    base = ho + 8;
    p1 = base + be32(d + base + 4); p3 = base + be32(d + base + 0x14);
    if (!in(p1, 44, n) || !in(p3, 16, n) || d[p1] != 2 || !(ch = d[p1 + 2])) return NULL;   /* (codec 2: DSP-ADPCM) */
    *rate = (int)be16(d + p1 + 4); total = be32(d + p1 + 12); nblk = be32(d + p1 + 20); bs = be32(d + p1 + 24);
    spb = be32(d + p1 + 28); lsam = be32(d + p1 + 36); lpad = be32(d + p1 + 40);
    co = base + be32(d + p3 + 8);
    if (!in(co, 8, n) || !in(ci = base + be32(d + co + 4), 32, n) || !in(dofs = be32(d + 0x20), 12, n) || memcmp(d + dofs, "DATA", 4)) return NULL;
    coefs(d + ci, c);
    pos = dofs + 8 + be32(d + dofs + 8);
    if (*rate < 8000 || !total || total > MAX_SAMPLES || !(out = malloc(sizeof(short) * total))) return NULL;
    for (uint32_t b = 0; b < nblk && got < (int)total; b++) {
        uint32_t sz = b + 1 == nblk ? lpad : bs, ns = b + 1 == nblk ? lsam : spb;
        if (!in(pos, sz, n)) break;
        got += dsp_decode(d + pos, sz, (int)(ns < total - got ? ns : total - got), c, &h1, &h2, out + got);
        pos += sz * (uint32_t)ch;
    }
    if (got < (int)total / 2) { free(out); return NULL; }
    *count = got;
    return out;
}

/* wave k of the RWSD at rw (its WAVE list), the waves' data at wbase */
static short* rwsd_wave(const unsigned char* d, uint32_t n, uint32_t rw, uint32_t wbase, int k, int* rate, int* count)
{
    uint32_t w, wi, ci, a, nib, at;
    int ns, h1 = 0, h2 = 0;
    short c[16], *out;
    if (!in(rw, 0x20, n) || memcmp(d + rw, "RWSD", 4) || !in(w = rw + be32(d + rw + 0x18), 12, n) || k >= (int)be32(d + w + 8) ||
        !in(w + 12 + 4 * (uint32_t)k, 4, n) || !in(wi = w + be32(d + w + 12 + 4 * k), 24, n) || d[wi] != 2) return NULL;   /* (format 2: DSP-ADPCM) */
    *rate = d[wi + 3] << 16 | (int)be16(d + wi + 4);
    nib = be32(d + wi + 12) + 1;
    if (!in(ci = wi + be32(d + wi + 16), 8, n) || !in(a = wi + be32(d + ci + 4), 32, n)) return NULL;
    coefs(d + a, c);
    at = wbase + be32(d + wi + 20) + be32(d + ci);
    ns = (int)(nib / 16 * 14 + (nib % 16 > 2 ? nib % 16 - 2 : 0));
    if (*rate < 8000 || ns <= 0 || ns > MAX_SAMPLES || !in(at, (nib + 1) / 2, n) || !(out = malloc(sizeof(short) * (size_t)ns))) return NULL;
    *count = dsp_decode(d + at, (nib + 1) / 2, ns, c, &h1, &h2, out);
    return out;
}

/* ---------------------------------------------------------------- the standard CRI ADX (as audio.c and the disc's voice
   archives): a 0x24-byte header (version 3, no loop, "(c)CRI" before the data), 18-byte frames of 32 nibbles, sample = nibble
   * (scale + 1) + the prediction from the two before (fixed coefficients from the cutoff) */
unsigned char* uc_adx_encode(const short* x, int n, int rate, size_t* size)
{
    int frames = (n + 31) / 32, h1 = 0, h2 = 0;
    double a = M_SQRT2 - cos(2.0 * M_PI * 500.0 / rate), b = M_SQRT2 - 1.0, cc = (a - sqrt((a + b) * (a - b))) / b;
    int c1 = (int)(cc * 8192.0), c2 = (int)(cc * cc * -4096.0);
    unsigned char* o = calloc(1, 0x24 + (size_t)frames * 18);
    if (!o || rate <= 0) { free(o); return NULL; }
    o[0] = 0x80; o[3] = 0x20; o[4] = 3; o[5] = 18; o[6] = 4; o[7] = 1;
    for (int i = 0; i < 4; i++) { o[8 + i] = (unsigned char)(rate >> (24 - 8 * i)); o[12 + i] = (unsigned char)(n >> (24 - 8 * i)); }
    o[16] = 500 >> 8; o[17] = 500 & 255; o[18] = 3;
    memcpy(o + 0x1E, "(c)CRI", 6);
    for (int f = 0; f < frames; f++) {
        unsigned char* fr = o + 0x24 + 18 * f;
        int best_sc = 1, best_h1 = 0, best_h2 = 0, e1 = h1, e2 = h2, peak = 0;
        double best = -1;
        unsigned char nb[16] = { 0 };
        for (int i = 0; i < 32; i++) {   /* (the open-loop residual's peak: the first scale tried) */
            int s = f * 32 + i < n ? x[f * 32 + i] : 0, r = s - ((c1 * e1) >> 12) - ((c2 * e2) >> 12);
            if (abs(r) > peak) peak = abs(r);
            e2 = e1; e1 = s;
        }
        for (int t = 0; t < 4; t++) {   /* (closed loop as the decoder runs: the scale of least error of a few) */
            int sc = (int)((peak + 6) / 7 * (1.0 + 0.25 * t)), q1 = h1, q2 = h2;
            double err = 0;
            unsigned char nt[16] = { 0 };
            if (sc < 1) sc = 1;
            if (sc > 32768) sc = 32768;
            for (int i = 0; i < 32; i++) {
                int s = f * 32 + i < n ? x[f * 32 + i] : 0, p = ((c1 * q1) >> 12) + ((c2 * q2) >> 12), r = s - p;
                int q = (int)floor((double)r / sc + 0.5), y;
                q = q > 7 ? 7 : q < -8 ? -8 : q;
                y = q * sc + p;
                y = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
                err += (double)(s - y) * (s - y);
                nt[i / 2] |= (unsigned char)((q & 15) << (i & 1 ? 0 : 4));
                q2 = q1; q1 = y;
            }
            if (best < 0 || err < best) { best = err; best_sc = sc; best_h1 = q1; best_h2 = q2; memcpy(nb, nt, 16); }
        }
        fr[0] = (unsigned char)((best_sc - 1) >> 8); fr[1] = (unsigned char)(best_sc - 1);
        memcpy(fr + 2, nb, 16);
        h1 = best_h1; h2 = best_h2;
    }
    *size = 0x24 + (size_t)frames * 18;
    return o;
}

/* to Code: Veronica's voice loudness: -20 dBFS RMS, the peaks at most -1 dBFS (ponytail: one gain per file, no limiter) */
static void normalize(short* x, int n)
{
    double sum = 0, g;
    int peak = 1;
    for (int i = 0; i < n; i++) { sum += (double)x[i] * x[i]; if (abs(x[i]) > peak) peak = abs(x[i]); }
    if (!n || sum <= 0) return;
    g = 0.1 * 32768.0 / sqrt(sum / n);
    if (g * peak > 0.891 * 32767.0) g = 0.891 * 32767.0 / peak;
    for (int i = 0; i < n; i++) { long v = lrint(x[i] * g); x[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
}

/* ---------------------------------------------------------------- who says it (cf. the file comment) */
static const struct { char sc[3]; const char *a, *b; } pairs[] = {
    { "00", "Rebecca", "Billy" }, { "01", "Wesker", NULL }, { "10", "Jill", "Chris" }, { "11", "Rebecca", "Richard" },
    { "12", "Wesker", NULL }, { "20", "Jill", "Carlos" }, { "21", NULL, NULL }, { "22", "Ada", NULL }, { "23", "Wesker", "other" },
    { "30", "Jill", "Chris" }, { "31", "Wesker", "Sergei" },
};
static int alpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\''; }
static int word_at(const char* t, const char* p, const char* nm)   /* nm at p, a whole word */
{
    size_t k = strlen(nm);
    return !strncmp(p, nm, k) && !alpha((unsigned char)p[k]) && (p == t || !alpha((unsigned char)p[-1]));
}
/* t calls nm: it starts with the name, has ", nm", or ends with it (then only punctuation) */
static int calls(const char* t, const char* nm)
{
    const char* s = t;
    while (*s == ' ') s++;
    if (word_at(t, s, nm)) return 1;
    for (const char* p = t; (p = strstr(p, nm)); p++) {
        const char* q = p + strlen(nm);
        if (!word_at(t, p, nm)) continue;
        if (p >= t + 2 && p[-1] == ' ' && p[-2] == ',') return 1;
        while (*q && (strchr(" !?.,\"", *q) || (unsigned char)*q >= 0x80)) q++;
        if (!*q) return 1;
    }
    return 0;
}
static int names_self(const char* t, const char* nm)   /* "I'm nm", "I am nm", "This is nm", "nm here" */
{
    static const char* const pre[] = { "I'm ", "I am ", "This is ", "my name is ", "My name is " };
    char b[64];
    for (int i = 0; i < 5; i++) { snprintf(b, sizeof b, "%s%s", pre[i], nm); for (const char* p = t; (p = strstr(p, b)); p++) if (word_at(t, p + strlen(pre[i]), nm)) return 1; }
    snprintf(b, sizeof b, "%s here", nm);
    return strstr(t, b) != NULL;
}
static int mentions(const char* t, const char* nm) { for (const char* p = t; (p = strstr(p, nm)); p++) if (word_at(t, p, nm)) return 1; return 0; }
static int has_letter(const char* t) { for (; *t; t++) if (alpha((unsigned char)*t) && *t != '\'') return 1; return 0; }

static int by_float(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return (x > y) - (x < y); }
/* the turn of line i in its numbered dialogue (VO_ + 7 digits, the same first four): from the nearest sure line by number, the
   other speaker an odd number of turns away, the same one an even; *d the turns; NULL none, or two nearest that disagree */
static const char* chain(const UcVoiceLine* l, int n, int i, int* d)
{
    const char *w = NULL, *a, *b;
    int best = 1 << 30, num = atoi(l[i].id + 7);
    if (strlen(l[i].id) != 10 || l[i].pair < 0 || !(b = pairs[l[i].pair].b)) return NULL;
    a = pairs[l[i].pair].a;
    for (int dir = -1; dir <= 1; dir += 2)
        for (int j = i + dir; j >= 0 && j < n && strlen(l[j].id) == 10 && !strncmp(l[j].id, l[i].id, 7); j += dir) {
            int t = abs(atoi(l[j].id + 7) - num);
            const char* o;
            if (strcmp(l[j].conf, "sure") || l[j].pair != l[i].pair) continue;
            o = (t & 1) == !strcmp(l[j].who, a) ? b : a;
            if (t < best) { best = t; w = o; }
            else if (t == best && w && strcmp(w, o)) w = NULL;
            break;
        }
    *d = best;
    return w;
}
static void weak(UcVoiceLine* l, int n);

void uc_voice_guess(UcVoiceLine* l, int n)
{
    for (int i = 0; i < n; i++) {
        UcVoiceLine* v = &l[i];
        const char* id = v->id;
        int k;
        v->who = "-"; v->conf = "none"; v->kind = "line"; v->pair = -1; snprintf(v->scen, sizeof v->scen, "-");
        if (SDL_strcasestr(id, "civ")) { v->kind = "npc"; continue; }   /* (the civilians of Raccoon City) */
        for (k = 3; k < 7 && id[k] >= '0' && id[k] <= '9'; k++);
        if (strncmp(id, "VO_", 3) || k < 7) continue;
        v->scen[0] = id[3]; v->scen[1] = id[4]; v->scen[2] = 0;
        if (!has_letter(v->text)) v->kind = "grunt";   /* (no subtitle, or "!!": a shout, a gasp) */
        if (!strncmp(id + 3, "2311", 4)) { v->kind = "radio"; continue; }   /* (Raccoon City's radio and TV, the police's calls) */
        for (k = 0; k < (int)(sizeof pairs / sizeof pairs[0]) && strncmp(pairs[k].sc, v->scen, 2); k++);
        if (k == (int)(sizeof pairs / sizeof pairs[0]) || !pairs[k].a) continue;
        v->pair = k;
        if (!pairs[k].b) { v->who = pairs[k].a; v->conf = "pair"; continue; }
        {
            const char *a = pairs[k].a, *b = pairs[k].b;
            int sa = names_self(v->text, a), sb = names_self(v->text, b), ca = calls(v->text, a), cb = calls(v->text, b);
            const char* w = sa != sb ? (sa ? a : b) : ca != cb && !(mentions(v->text, a) && mentions(v->text, b)) ? (cb ? a : b) : NULL;   /* (both
                named, or neither: no cue) */
            if (w) { v->who = w; v->conf = "sure"; }
        }
    }
    for (int i = 0; i < n; i++) {   /* turn-taking: next to a sure line of the same numbered dialogue, the other one */
        UcVoiceLine* v = &l[i];
        const char* w;
        int d;
        if (strcmp(v->conf, "none") || strcmp(v->kind, "line") || !(w = chain(l, n, i, &d)) || d != 1) continue;
        v->who = w; v->conf = "guess";
    }
    weak(l, n);
}

/* ---------------------------------------------------------------- the weak suggestions (not used in the game until checked) */
static float median(float* f, int n)
{
    if (!n) return 0;
    qsort(f, (size_t)n, sizeof *f, by_float);
    return n & 1 ? f[n / 2] : (f[n / 2 - 1] + f[n / 2]) / 2;
}
/* the median pitch of a line (Hz, 0 unknown): per frame of 32 ms at about 8 kHz, of at least half the line's loudness, the
   first peak of the normalized autocorrelation within 0.85 of the best (70..500 Hz: the period, not its multiples) if the best
   is over 0.6 (voiced); the voiced frames' median. ponytail: no YIN, octave errors left to the median */
float uc_voice_f0(const short* x, int n, int rate)
{
    enum { W = 256 };
    int r = rate >= 16000 ? rate / 8000 : 1, m = x && n > 0 && rate > 0 ? n / r : 0, nf = 0, lo, hi;
    double sr = (double)rate / r, all = 0;
    float *d = m ? malloc(sizeof *d * (size_t)m) : NULL, *f = m ? malloc(sizeof *f * (size_t)(m / (W / 2) + 1)) : NULL, res;
    if (!d || !f) { free(d); free(f); return 0; }
    lo = (int)(sr / 500); hi = (int)(sr / 70) + 1;
    for (int i = 0; i < m; i++) {
        float a = 0;
        for (int k = 0; k < r; k++) a += x[i * r + k];
        d[i] = a / r; all += (double)d[i] * d[i];
    }
    all /= m;
    for (int s = 0; hi + 1 < 600 && s + W + hi + 1 <= m; s += W / 2) {
        double e = 0, c[600], best = 0;
        int L = 0;
        for (int i = 0; i < W; i++) e += (double)d[s + i] * d[s + i];
        if (e <= 0 || e / W < all * 0.25) continue;
        for (int k = lo - 1; k <= hi + 1; k++) {
            double xy = 0, yy = 0;
            for (int i = 0; i < W; i++) { xy += (double)d[s + i] * d[s + i + k]; yy += (double)d[s + i + k] * d[s + i + k]; }
            c[k] = yy > 0 ? xy / sqrt(e * yy) : 0;
            if (k >= lo && k <= hi && c[k] > best) best = c[k];
        }
        if (best < 0.6) continue;
        for (int k = lo; k <= hi && !L; k++) if (c[k] >= best * 0.85 && c[k] >= c[k - 1] && c[k] >= c[k + 1]) L = k;
        if (L) f[nf++] = (float)(sr / L);
    }
    res = nf >= 3 ? median(f, nf) : 0;
    free(d); free(f);
    return res;
}

static int female(const char* nm) { return !strcmp(nm, "Rebecca") || !strcmp(nm, "Jill") || !strcmp(nm, "Ada"); }
/* weaker words than the calls: the other's name not as a call ("Sorry Richard. I'm on it!"), a title of one ("Colonel": not
   Sergei), a word for a woman or a man said to the other ("Watch your step, princess!": the man) or of the speaker ("Don't
   call me princess!", "I'm a ...": the woman) */
static const char* text_cue(const char* t, const char* a, const char* b)
{
    static const struct { const char *word, *name; } titles[] = { { "Colonel", "Sergei" }, { "Captain", "Wesker" } };
    static const struct { const char* word; int f; } words[] = { { "princess", 1 }, { "girl", 1 }, { "lady", 1 }, { "sweetheart", 1 },
        { "ma'am", 1 }, { "sir", 0 }, { "pal", 0 }, { "buddy", 0 }, { "man", 0 } };
    int ma = mentions(t, a) && !names_self(t, a), mb = mentions(t, b) && !names_self(t, b);
    if (ma != mb) return ma ? b : a;
    for (int i = 0; i < (int)(sizeof titles / sizeof titles[0]); i++)
        if (mentions(t, titles[i].word) && (!strcmp(titles[i].name, a) || !strcmp(titles[i].name, b))) return strcmp(titles[i].name, a) ? a : b;
    if (female(a) == female(b)) return NULL;
    for (int i = 0; i < (int)(sizeof words / sizeof words[0]); i++) {
        char me[48], im[48];
        snprintf(me, sizeof me, "me %s", words[i].word); snprintf(im, sizeof im, "I'm a %s", words[i].word);
        if (strstr(t, me) || strstr(t, im)) return female(a) == words[i].f ? a : b;
        if (calls(t, words[i].word)) return female(a) == words[i].f ? b : a;
    }
    return NULL;
}

/* the median pitch of each scenario's speakers: of their sure and scenario lines on this disc */
static void speaker_f0(const UcVoiceLine* l, int n, float fm[][2])
{
    float* buf = malloc(sizeof *buf * (size_t)(n + 1));
    for (int k = 0; buf && k < (int)(sizeof pairs / sizeof pairs[0]); k++)
        for (int s = 0; s < 2; s++) {
            const char* nm = s ? pairs[k].b : pairs[k].a;
            int c = 0;
            for (int i = 0; nm && i < n; i++)
                if (l[i].f0 > 0 && !strcmp(l[i].who, nm) && (!strcmp(l[i].conf, "sure") || !strcmp(l[i].conf, "pair")) && !strcmp(l[i].kind, "line")) buf[c++] = l[i].f0;
            fm[k][s] = median(buf, c);
        }
    free(buf);
}
/* the speaker whose median pitch is nearer the line's; none if the two are less than 20 % apart (Jill and Chris: their sure lines
   are shouted calls, both about 360 Hz) */
static const char* by_pitch(const UcVoiceLine* v, float fm[][2])
{
    const float* f = fm[v->pair];
    if (v->f0 <= 0 || f[0] <= 0 || f[1] <= 0 || fabsf(logf(f[0] / f[1])) < logf(1.2f)) return NULL;
    return fabsf(logf(v->f0 / f[0])) <= fabsf(logf(v->f0 / f[1])) ? pairs[v->pair].a : pairs[v->pair].b;
}

/* lines of two speakers that no rule above took, lines and shouts: their turn further along the numbered dialogue (chain), else
   the pitch nearer to one speaker's, else text_cue, else the scenario's first speaker; all "weak" (cf. docs) */
static void weak(UcVoiceLine* l, int n)
{
    float fm[sizeof pairs / sizeof pairs[0]][2] = { { 0 } };
    speaker_f0(l, n, fm);
    for (int i = 0; i < n; i++) {
        UcVoiceLine* v = &l[i];
        const char* w;
        int d;
        if (strcmp(v->conf, "none") || (strcmp(v->kind, "line") && strcmp(v->kind, "grunt")) || v->pair < 0 || !pairs[v->pair].b) continue;
        if (!(w = chain(l, n, i, &d)) && !(w = by_pitch(v, fm)) && !(w = text_cue(v->text, pairs[v->pair].a, pairs[v->pair].b))) w = pairs[v->pair].a;
        v->who = w; v->conf = "weak";
    }
}

/* ---------------------------------------------------------------- the extraction */
static const char* const cv_who[10] = { NULL, "Rebecca", "Billy", "Chris", "Jill", "Carlos", "Wesker", NULL, "Ada", "HUNK" };   /* (main.dol's
    table of the banks; CV7: CV6's again) */

typedef struct { const unsigned char* d; uint32_t n, symb, ib; } Sar;
static uint32_t ref(const Sar* s, uint32_t o) { return in(o, 8, s->n) ? s->ib + be32(s->d + o + 4) : 0; }
static const char* sar_name(const Sar* s, uint32_t id)
{
    uint32_t st = s->symb + 8 + be32(s->d + s->symb + 8), o;
    if (!in(st, 4, s->n) || id >= be32(s->d + st) || !in(st + 4 + 4 * id, 4, s->n) || !in(o = s->symb + 8 + be32(s->d + st + 4 + 4 * id), 1, s->n) ||
        !memchr(s->d + o, 0, s->n - o)) return "";
    return (const char*)s->d + o;
}

typedef struct { char** id; int n; } Names;
static int by_str(const void* a, const void* b) { return strcmp(*(char* const*)a, *(char* const*)b); }

typedef struct { const unsigned char* d; size_t n; } Buf;
static int put(FILE* f, void* ud) { const Buf* b = ud; return fwrite(b->d, 1, b->n, f) == b->n ? 0 : -1; }
typedef struct { const UcVoiceLine* l; int n, text; } List;
static int put_list(FILE* f, void* ud)
{
    const List* L = ud;
    if (!L->text) fprintf(f, "# recvx Umbrella Chronicles voice lines 1: n | source id | speaker guess | sure/pair/guess/weak/none | ms | line/grunt/radio/npc | scenario\n");
    for (int i = 0; i < L->n; i++) {
        const UcVoiceLine* v = &L->l[i];
        if (L->text) fprintf(f, "%s | %s\n", v->id, v->text);
        else fprintf(f, "%d | %s | %s | %s | %d | %s | %s\n", i, v->id, v->who, v->conf, v->ms, v->kind, v->scen);
    }
    return ferror(f) ? -1 : 0;
}

/* the subtitle of key in mes_us.txtres (a table of records: key, line count, the lines), its lines joined; "" none */
static void subtitle(const unsigned char* d, uint32_t n, const char* key, char* out, int on)
{
    uint32_t cnt = n >= 16 ? be32(d + 8) : 0, to = n >= 16 ? be32(d + 12) : 0;
    out[0] = 0;
    for (uint32_t i = 0; i < cnt && in(to + 4 * i, 4, n); i++) {
        uint32_t r = be32(d + to + 4 * i), k, nl, t;
        if (!in(r, 16, n) || !in(k = be32(d + r + 4), 1, n) || !memchr(d + k, 0, n - k) || SDL_strcasecmp((const char*)d + k, key)) continue;
        nl = be32(d + r + 8); t = be32(d + r + 12);
        for (uint32_t j = 0; j < nl && j < 16 && in(t, 1, n) && memchr(d + t, 0, n - t); j++) {
            size_t len = strlen(out);
            snprintf(out + len, (size_t)on - len, "%s%s", len ? " " : "", (const char*)d + t);
            t += (uint32_t)strlen((const char*)d + t) + 1;
        }
        break;
    }
    for (char* p = out; *p; p++) if (*p == '|' || *p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
}

int uc_voice_extract(RvzDisc* disc, const char* outdir)
{
    ImportProgress* pg = &g_import_progress;
    uint32_t sn, mn, ns, ng;
    unsigned char *sar = rvz_file(disc, "Data/sound/biosound.brsar", &sn), *mes = rvz_file(disc, "Data/message/mes_us.txtres", &mn);
    Sar s;
    char **ids = NULL, dir[1200], path[1300];
    int nid = 0, nl = 0, fails = 0, cnt[5] = { 0 };
    UcVoiceLine* l = NULL;
    if (!sar || sn < 0x20 || memcmp(sar, "RSAR", 4)) { host_log("import: umbrella voices: no Data/sound/biosound.brsar\n"); free(sar); free(mes); return -1; }
    s = (Sar){ sar, sn, be32(sar + 0x10), be32(sar + 0x18) + 8 };
    {   /* the stream sounds VO_... the game plays */
        uint32_t snd = ref(&s, s.ib);
        ns = in(snd, 4, sn) ? be32(sar + snd) : 0;
        ids = calloc(ns + 1, sizeof *ids);
        for (uint32_t i = 0; ids && i < ns; i++) {
            uint32_t e = ref(&s, snd + 4 + 8 * i);
            const char* nm;
            if (!in(e, 24, sn) || sar[e + 22] != 2 || strncmp(nm = sar_name(&s, be32(sar + e)), "VO_", 3) || strlen(nm) > 30) continue;
            ids[nid++] = SDL_strdup(nm);
        }
        if (nid) qsort(ids, (size_t)nid, sizeof *ids, by_str);
    }
    l = calloc((size_t)nid + 128, sizeof *l);
    snprintf(dir, sizeof dir, "%s/voice", outdir);
    SDL_CreateDirectory(dir);
    pg->total += nid + 80;
    for (int pass = 0; l && pass < 2; pass++) {   /* the lines, then the grunt banks' waves */
        uint32_t groups = ref(&s, s.ib + 32);
        ng = pass && in(groups, 4, sn) ? be32(sar + groups) : 0;
        for (int i = 0; i < (pass ? (int)ng * 16 : nid) && nl < nid + 128; i++) {
            short* pcm = NULL;
            int rate = 0, n = 0, bank = 0, wave = 0;
            char id[48];
            if (!pass) {
                uint32_t fn;
                unsigned char* f;
                snprintf(path, sizeof path, "Data/sound/stream/%s.brstm", ids[i]);
                if (!(f = rvz_file(disc, path, &fn))) continue;   /* (a few the brsar names are not on the disc: VO_civwomzonbie for VO_CIVWOMZOMBIES) */
                pcm = brstm_pcm(f, fn, &rate, &n);
                free(f);
                snprintf(id, sizeof id, "%s", ids[i]);
            } else {
                uint32_t e = ref(&s, groups + 4 + 8 * (uint32_t)(i / 16)), it, q;
                wave = i % 16;
                if (!in(e, 0x28, sn) || sscanf(sar_name(&s, be32(sar + e)), "CV%d_Gr", &bank) != 1 || bank < 1 || bank > 9 || !cv_who[bank] ||
                    !in(it = ref(&s, e + 0x20), 12, sn) || !be32(sar + it) || !in(q = ref(&s, it + 4), 20, sn)) continue;
                if (!(pcm = rwsd_wave(sar, sn, be32(sar + e + 0x10) + be32(sar + q + 4), be32(sar + e + 0x18) + be32(sar + q + 12), wave, &rate, &n))) continue;
                snprintf(id, sizeof id, "CV%d_Gr_%d", bank, wave);
            }
            pg->done++;
            snprintf(pg->phase, sizeof pg->phase, "%s (%s)", tr("Voice Lines"), id);
            if (!pcm) { host_log("import: umbrella voices: %s cannot be decoded\n", id); fails++; continue; }
            {
                UcVoiceLine* v = &l[nl];
                size_t size;
                unsigned char* adx;
                Buf b;
                v->f0 = pass ? 0 : uc_voice_f0(pcm, n, rate);
                normalize(pcm, n);
                adx = uc_adx_encode(pcm, n, rate, &size);
                free(pcm);
                snprintf(path, sizeof path, "%s/%d.adx", dir, nl);
                b = (Buf){ adx, size };
                if (!adx || pc_save_file(path, "wb", put, &b)) { host_log("import: umbrella voices: cannot write %s\n", path); free(adx); fails++; continue; }
                free(adx);
                snprintf(v->id, sizeof v->id, "%s", id);
                v->ms = (int)((int64_t)n * 1000 / rate);
                v->bank = bank;
                if (!pass && mes) subtitle(mes, mn, id, v->text, sizeof v->text);
                nl++;
            }
        }
    }
    int vo = 0;
    while (l && vo < nl && !l[vo].bank) vo++;
    if (l) {
        uc_voice_guess(l, vo);
        for (int i = vo; i < nl; i++) { l[i].who = cv_who[l[i].bank]; l[i].conf = "sure"; l[i].kind = "grunt"; snprintf(l[i].scen, sizeof l[i].scen, "CV%d", l[i].bank); }
        for (int i = 0; i < nl; i++) cnt[!strcmp(l[i].conf, "sure") ? 0 : !strcmp(l[i].conf, "pair") ? 1 : !strcmp(l[i].conf, "guess") ? 2 : !strcmp(l[i].conf, "weak") ? 3 : 4]++;
        List L = { l, nl, 0 };
        snprintf(path, sizeof path, "%s/voices.txt", outdir);
        if (pc_save_file(path, "w", put_list, &L)) { host_log("import: cannot write %s\n", path); fails++; }
        L.text = 1;
        snprintf(path, sizeof path, "%s/voices_text.txt", outdir);
        if (pc_save_file(path, "w", put_list, &L)) { host_log("import: cannot write %s\n", path); fails++; }
    }
    host_log("import: umbrella voices: %d files (%d of %d lines the game plays, %d grunts), speakers %d sure, %d by scenario, %d guessed, %d weak, %d unknown; %d failed\n",
             nl, vo, nid, nl - vo, cnt[0], cnt[1], cnt[2], cnt[3], cnt[4], fails);
    for (int i = 0; i < nid; i++) SDL_free(ids[i]);
    free(ids); free(l); free(sar); free(mes);
    return fails;
}
