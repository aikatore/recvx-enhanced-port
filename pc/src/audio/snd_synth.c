/*
 * TSNDDRV sound effects: the "TQ" request layer of the driver plus a small model of
 * Sony's hardware synthesizer (modhsyn) on SPU2 voices.
 *
 * Driver layer (from the retail TSNDDRV.IRX: treq_TQ, Tsnd_tqreq/tqchg/tqcancel,
 * TsndTQ_ATick, tsnd_tq_chgreq). A request (port = SE bank, tq = program, ch = 0..15)
 * stores vol/pan/pitch in a per-channel slot; the 240 Hz tick then sends MIDI to the
 * synth: CC7 (master), program change tq, key-on of the sample's base note at velocity
 * 127, then CC7 = master*vol/127, CC10 = pan, pitch bend. The slot counts down the
 * sample length (len*16+0x70, minus pitch>>9 per tick) and then sends All Sound Off.
 * Its active bits are what the EE reads back as se_info[port].
 *
 * Synth: HD bank (Prog -> split -> sample set -> sample -> Vagi) to SPU2 ADPCM voices
 * with ADSR (PSX/PS2 SPU envelope rules), pitch = rate/48k * 2^(semitones/12) with
 * bend range and detune in 1/128 semitone (format/sound/hd.h).
 */
#include "../ps2_platform.h"
#include "snd.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SE_PORTS   5
#define VOICES     48           /* 2 SPU2 cores x 24 */
#define OUT_RATE   48000

static const int se_bd_port[SE_PORTS] = { 0, 4, 5, 6, 7 };  /* SE_BANK[] on the EE */

/* ---------------------------------------------------------------- HD access */

static uint32_t rd32(const SndBlob* b, uint32_t o) { return o + 4 <= b->size ? b->data[o] | (b->data[o + 1] << 8) | (b->data[o + 2] << 16) | ((uint32_t)b->data[o + 3] << 24) : 0xFFFFFFFFu; }
static uint16_t rd16(const SndBlob* b, uint32_t o) { return o + 2 <= b->size ? (uint16_t)(b->data[o] | (b->data[o + 1] << 8)) : 0xFFFF; }
static uint8_t  rd8(const SndBlob* b, uint32_t o)  { return o < b->size ? b->data[o] : 0; }
static int trace_snd(void) { static int on = -1; if (on < 0) on = SDL_getenv("RECVX_TRACE_SND") != NULL; return on; }   /* RECVX_TRACE_SND */

/* offset of entry idx of the chunk whose address is at Head+field, or 0 */
static uint32_t hd_entry(const SndBlob* hd, int field, uint32_t idx)
{
    uint32_t chunk = rd32(hd, 0x10 + field), max, ofs;
    if (chunk == 0xFFFFFFFFu) return 0;
    max = rd32(hd, chunk + 12);
    if (max == 0xFFFFFFFFu || idx > max) return 0;
    ofs = rd32(hd, chunk + 16 + idx * 4);
    return ofs == 0xFFFFFFFFu ? 0 : chunk + ofs;
}
enum { HD_PROG = 0x14, HD_SSET = 0x18, HD_SMPL = 0x1C, HD_VAGI = 0x20 };   /* Head chunk fields */

/* length of a VAG in bytes: up to the next entry's offset (or the body size) */
static uint32_t vag_size(const SndBlob* hd, uint32_t vag_idx)
{
    uint32_t e = hd_entry(hd, HD_VAGI, vag_idx), n = hd_entry(hd, HD_VAGI, vag_idx + 1);
    uint32_t start = rd32(hd, e), end = n ? rd32(hd, n) : rd32(hd, 0x20);   /* bodySize */
    return end > start ? end - start : 0;
}

/* ---------------------------------------------------------------- voices */

typedef struct { uint8_t prog, vol, pan; uint16_t bend; } MidiCh;   /* effective channel state */

typedef struct {
    int on, note;
    MidiCh* chan;
    const SndBlob* bd;
    uint32_t addr, loop_addr;         /* byte offsets of current / loop-start block */
    int16_t buf[28];
    int pos28;                        /* next sample index in buf */
    int32_t h1, h2;
    uint32_t frac;                    /* 12-bit pitch accumulator */
    double base_pitch;                /* rate/48k * note ratio, before bend */
    int bend_lo, bend_hi;             /* 1/128 semitone */
    float vol;                        /* prog*split*sample volume */
    int dry, wet, core;               /* sampleSpuAttr: direct / effect send, SPU2 core */
    int pan;                          /* prog+split+sample pan offset (centre 64) */
    int16_t s0, s1;                   /* interpolation pair */
    /* envelope */
    uint16_t adsr1, adsr2;
    int phase;                        /* 0 attack 1 decay 2 sustain 3 release */
    int32_t level, wait;
    uint32_t age;
    int heard;                        /* the mixer has played a sample of it (RECVX_TRACE_SND: released before it sounded) */
} Voice;

/* TQ slot (tqreq_tbl entry) */
typedef struct {
    uint32_t flags;          /* 1 active, 2 stop, 4 key-on, 0x20 vol, 0x40 pan, 0x80 pitch */
    uint32_t count;          /* remaining length (0xFFFF = looping) */
    uint8_t tq, vol, pan, mvol, mpan;
    uint8_t xb;              /* PC: 0 the port's bank, else extra bank xb (g_snd_banks.xhd/xbd[xb - 1]) */
    uint16_t pitch, mpitch;
} TqSlot;

static SDL_Mutex* lock;
static SDL_AudioStream* stream;
static Voice voice[VOICES];
static MidiCh midi[SE_PORTS][16];
static TqSlot slot[SE_PORTS][16];
static uint16_t active_bits[SE_PORTS];
static uint32_t voice_age;
static uint64_t ticks_done;
static int mono;
static float master = 1.0f, user_volume = 1.0f;

void snd_synth_set_volume(float v) { user_volume = v; }

void snd_synth_lock(void)   { if (lock) SDL_LockMutex(lock); }
void snd_synth_unlock(void) { if (lock) SDL_UnlockMutex(lock); }
uint16_t snd_synth_se_active(int port) { return port >= 0 && port < SE_PORTS ? active_bits[port] : 0; }

static int adsr_tick(Voice* v)
{
    int rate, shift, step, inc, expo, target = 0;
    switch (v->phase) {
    case 0: rate = (v->adsr1 >> 8) & 0x7F; inc = 1; expo = v->adsr1 >> 15; break;
    case 1: rate = ((v->adsr1 >> 4) & 0xF) * 4; inc = 0; expo = 1; target = ((v->adsr1 & 0xF) + 1) * 0x800; break;
    case 2: rate = (v->adsr2 >> 6) & 0x7F; inc = !((v->adsr2 >> 14) & 1); expo = v->adsr2 >> 15; break;
    default: rate = (v->adsr2 & 0x1F) * 4; inc = 0; expo = (v->adsr2 >> 5) & 1; break;
    }
    if (v->wait > 0) { v->wait--; return 1; }
    shift = rate >> 2;
    step = inc ? 7 - (rate & 3) : -8 + (rate & 3);
    {
        int32_t cycles = 1 << (shift > 11 ? shift - 11 : 0);
        int32_t s = step << (shift < 11 ? 11 - shift : 0);
        if (expo && inc && v->level > 0x6000) cycles *= 4;
        if (expo && !inc) s = (int32_t)(((int64_t)s * v->level) >> 15);   /* rounds down as the SPU: at least -1, so a release reaches 0 (a division stalled it above 0: a looping voice sounded on after its key-off) */
        v->wait = cycles - 1;
        v->level += s;
    }
    if (v->level > 0x7FFF) v->level = 0x7FFF;
    if (v->level < 0) v->level = 0;
    if (v->phase == 0 && v->level >= 0x7FFF) v->phase = 1, v->wait = 0;
    else if (v->phase == 1 && v->level <= target) v->phase = 2, v->wait = 0;
    else if (v->phase == 3 && v->level == 0) return 0;
    return 1;
}

/* decode the next 16-byte SPU ADPCM block; 0 at the end of a non-looping sample */
static int decode_block(Voice* v)
{
    static const int f0[5] = { 0, 60, 115, 98, 122 }, f1[5] = { 0, 0, -52, -55, -60 };
    const uint8_t* b;
    int shift, filt, flags;
    if (v->addr + 16 > v->bd->size) return 0;
    b = v->bd->data + v->addr;
    shift = b[0] & 15; filt = (b[0] >> 4) % 5; flags = b[1];
    if (shift > 12) shift = 9;
    if (flags & 4) v->loop_addr = v->addr;
    for (int i = 0; i < 28; i++) {
        int32_t s = (int16_t)(((b[2 + i / 2] >> ((i & 1) * 4)) & 15) << 12) >> shift;
        s += (v->h1 * f0[filt] + v->h2 * f1[filt] + 32) >> 6;
        if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
        v->h2 = v->h1; v->h1 = s; v->buf[i] = (int16_t)s;
    }
    v->pos28 = 0;
    if (flags & 1) {
        if (!(flags & 2)) { v->addr = 0xFFFFFFFFu; return 1; }   /* play this block, then end */
        v->addr = v->loop_addr;
    } else v->addr += 16;
    return 1;
}

static int next_sample(Voice* v, int16_t* out)
{
    if (v->pos28 >= 28 && (v->addr == 0xFFFFFFFFu || !decode_block(v))) return 0;
    *out = v->buf[v->pos28++];
    return 1;
}

static void key_on(const SndBlob* hd, const SndBlob* bd, MidiCh* m, int note, int vel)
{
    uint32_t prog = hd_entry(hd, HD_PROG, m->prog);
    if (!prog || !bd->size) {
        if (trace_snd()) printf("tsnd: key-on prog %d note %d dropped (%s)\n", m->prog, note, prog ? "no samples loaded" : "no such program");
        return;
    }
    for (int s = 0; s < rd8(hd, prog + 4); s++) {
        uint32_t split = prog + rd32(hd, prog) + s * rd8(hd, prog + 5);
        uint32_t sset;
        if (note < rd8(hd, split + 2) || note > rd8(hd, split + 4)) continue;
        sset = hd_entry(hd, HD_SSET, rd16(hd, split));
        if (!sset || vel < rd8(hd, sset + 1) || vel > rd8(hd, sset + 2)) continue;
        for (int k = 0; k < rd8(hd, sset + 3); k++) {
            uint32_t smp = hd_entry(hd, HD_SMPL, rd16(hd, sset + 4 + k * 2)), vag;
            Voice* v = NULL;
            if (!smp || vel < rd8(hd, smp + 2) || vel > rd8(hd, smp + 4)) continue;
            vag = hd_entry(hd, HD_VAGI, rd16(hd, smp));
            if (!vag) continue;
            for (int i = 0; i < VOICES; i++) if (!voice[i].on) { v = &voice[i]; break; }
            if (!v) {   /* steal the oldest */
                v = &voice[0];
                for (int i = 1; i < VOICES; i++) if (voice[i].age < v->age) v = &voice[i];
                if (trace_snd()) printf("tsnd: all %d voices busy, the oldest (prog %d) stopped\n", VOICES, v->chan ? v->chan->prog : -1);
            }
            memset(v, 0, sizeof(*v));
            v->on = 1; v->chan = m; v->note = note; v->bd = bd;
            v->addr = v->loop_addr = rd32(hd, vag);
            v->pos28 = 28;
            {
                double semis = note - rd8(hd, smp + 11) + (int8_t)rd8(hd, prog + 8) + (int8_t)rd8(hd, split + 18)
                             + ((int8_t)rd8(hd, prog + 9) + (int8_t)rd8(hd, split + 19) + (int8_t)rd8(hd, smp + 12)) / 128.0;
                v->base_pitch = rd16(hd, vag + 4) / (double)OUT_RATE * pow(2.0, semis / 12.0);
            }
            v->bend_lo = rd16(hd, split + 6); v->bend_hi = rd16(hd, split + 8);
            v->vol = rd8(hd, prog + 6) / 128.0f * rd8(hd, split + 16) / 128.0f * rd8(hd, smp + 16) / 128.0f * vel / 127.0f;
            v->pan = (int8_t)rd8(hd, prog + 7) - 64 + (int8_t)rd8(hd, split + 17) - 64 + (int8_t)rd8(hd, smp + 13) - 64;
            v->adsr1 = rd16(hd, smp + 18); v->adsr2 = rd16(hd, smp + 20);
            {
                int attr = rd8(hd, smp + 41);
                v->dry = (attr & 3) != 0; v->wet = (attr & 0xC) != 0; v->core = (attr & 0x20) ? 1 : 0;
            }
            v->age = ++voice_age;
            if (!next_sample(v, &v->s1)) v->on = 0;
            if (trace_snd())
                printf("tsnd: key-on prog %d note %d vel %d vag @%x %u Hz pitch %.3f adsr %04x/%04x vol %.2f send %s%s core %d%s\n", m->prog,
                       note, vel, v->addr, rd16(hd, vag + 4), v->base_pitch * OUT_RATE / rd16(hd, vag + 4), v->adsr1, v->adsr2, v->vol, v->dry ? "dry" : "", v->wet ? "+fx" : "", v->core, v->on ? "" : " (empty)");
        }
    }
}

/* key off (ADSR release) the voices of a channel; note < 0 = all of them */
static void key_off(const MidiCh* m, int note)
{
    for (int i = 0; i < VOICES; i++)
        if (voice[i].on && voice[i].chan == m && (note < 0 || voice[i].note == note) && voice[i].phase != 3) {
            if (!voice[i].heard && trace_snd()) printf("tsnd: key-off prog %d note %d before it sounded\n", m->prog, voice[i].note);
            voice[i].phase = 3, voice[i].wait = 0;
        }
}

/* ---------------------------------------------------------------- SPU2 reverb */
/* The SPU reverb (psx-spx "SPU Reverb Formula") at half rate. Registers in preset order:
   dAPF1 dAPF2 vIIR vCOMB1-4 vWALL vAPF1 vAPF2 mLSAME mRSAME mLCOMB1 mRCOMB1 mLCOMB2 mRCOMB2
   dLSAME dRSAME mLDIFF mRDIFF mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4 dLDIFF dRDIFF mLAPF1 mRAPF1
   mLAPF2 mRAPF2 vLIN vRIN; addresses are in 8-byte units. Presets come from the disc's
   LIBSD.IRX (sceSdSetEffectAttr's table, modes 1-9). */
#define REV_RING 0x20000
typedef struct {
    int mode;
    int16_t reg[32];
    float depth;              /* EVOL */
    int16_t ring[REV_RING];
    uint32_t pos;
    float in[2], out[2];
    int phase;
} Reverb;
static Reverb rev[2];
static int16_t rev_preset[10][32];
static int rev_presets_ok;

static void load_reverb_presets(void)
{
    static const uint8_t room[8] = { 0x7D, 0x00, 0x5B, 0x00, 0x80, 0x6D, 0xB8, 0x54 };
    uint32_t lsn, size;
    uint8_t* d;
    if (!cdvd_find_file("\\PS2_DATA\\MODULES\\LIBSD.IRX;1", &lsn, &size) || size > (1u << 20)) return;
    d = malloc((size + 2047) & ~2047u);
    if (d && cdvd_read_sectors(lsn, (size + 2047) / 2048, d)) {
        for (uint32_t i = 0; i + 9 * 68 <= size; i += 2) {
            if (memcmp(d + i, room, 8)) continue;
            for (int m = 1; m <= 9; m++)
                for (int r = 0; r < 32; r++) rev_preset[m][r] = (int16_t)(d[i + (m - 1) * 68 + r * 2] | (d[i + (m - 1) * 68 + r * 2 + 1] << 8));
            rev_presets_ok = 1;
            break;
        }
    }
    free(d);
    if (!rev_presets_ok) host_log("tsnddrv: reverb presets not found in LIBSD.IRX, reverb disabled\n");
}

static void reverb_step(Reverb* r)
{
#define RG(i) (r->reg[i] / 32768.0f)
#define AD(i) ((uint32_t)(uint16_t)r->reg[i] * 4u)
#define RD(a) ((float)r->ring[(r->pos + (a)) & (REV_RING - 1)])
#define WR(a, v) do { float t_ = (v); r->ring[(r->pos + (a)) & (REV_RING - 1)] = (int16_t)(t_ > 32767 ? 32767 : t_ < -32768 ? -32768 : t_); } while (0)
    float lin = RG(30) * r->in[0], rin = RG(31) * r->in[1], l, rr;
    float iir = RG(2), wall = RG(7);
    WR(AD(10), (lin + RD(AD(16)) * wall - RD(AD(10) - 1)) * iir + RD(AD(10) - 1));
    WR(AD(11), (rin + RD(AD(17)) * wall - RD(AD(11) - 1)) * iir + RD(AD(11) - 1));
    WR(AD(18), (lin + RD(AD(25)) * wall - RD(AD(18) - 1)) * iir + RD(AD(18) - 1));
    WR(AD(19), (rin + RD(AD(24)) * wall - RD(AD(19) - 1)) * iir + RD(AD(19) - 1));
    l  = RG(3) * RD(AD(12)) + RG(4) * RD(AD(14)) + RG(5) * RD(AD(20)) + RG(6) * RD(AD(22));
    rr = RG(3) * RD(AD(13)) + RG(4) * RD(AD(15)) + RG(5) * RD(AD(21)) + RG(6) * RD(AD(23));
    l  -= RG(8) * RD(AD(26) - AD(0)); WR(AD(26), l);  l  = l  * RG(8) + RD(AD(26) - AD(0));
    rr -= RG(8) * RD(AD(27) - AD(0)); WR(AD(27), rr); rr = rr * RG(8) + RD(AD(27) - AD(0));
    l  -= RG(9) * RD(AD(28) - AD(1)); WR(AD(28), l);  l  = l  * RG(9) + RD(AD(28) - AD(1));
    rr -= RG(9) * RD(AD(29) - AD(1)); WR(AD(29), rr); rr = rr * RG(9) + RD(AD(29) - AD(1));
    r->out[0] = l * r->depth; r->out[1] = rr * r->depth;
    r->pos++;
#undef RG
#undef AD
#undef RD
#undef WR
}

/* ponytail: half-rate by pair averaging / sample hold; SPU2 uses FIR resampling filters */
static void reverb_run(Reverb* r, const float* in, float* out, int n)
{
    for (int k = 0; k < n; k++) {
        r->in[0] += in[k * 2] * 0.5f; r->in[1] += in[k * 2 + 1] * 0.5f;
        if (++r->phase == 2) { reverb_step(r); r->phase = 0; r->in[0] = r->in[1] = 0; }
        out[k * 2] += r->out[0]; out[k * 2 + 1] += r->out[1];
    }
}

static void set_reverb(int core, int mode, int depth)
{
    Reverb* r = &rev[core];
    if (!rev_presets_ok || mode < 0 || mode > 9) mode = 0;
    if (mode != r->mode) {
        memset(r->ring, 0, sizeof(r->ring));
        r->pos = 0; r->phase = 0; r->out[0] = r->out[1] = 0;
        if (mode) memcpy(r->reg, rev_preset[mode], sizeof(r->reg));
        r->mode = mode;
    }
    r->depth = (int16_t)depth / 32768.0f;
}

/* n samples of every voice into acc (dry) and fx[core] (reverb send), interleaved stereo */
/* how loud the player's voice (SE port 4, sdfunc.c CallPlayerVoice) has been since it was last asked:
   the mean |sample| of its voices, 0..1 (an imported model's jaw follows it, actor_glue.c imp_face) */
static float voice_sum;
static int voice_n;
float snd_synth_voice_level(void)
{
    float l;
    snd_synth_lock();
    l = voice_n ? voice_sum / voice_n / 32768.0f : 0.0f;
    voice_sum = 0; voice_n = 0;
    snd_synth_unlock();
    return l;
}

static void render(float* acc, float* fx0, float* fx1, int n)
{
    voice_n += n;
    for (int i = 0; i < VOICES; i++) {
        Voice* v = &voice[i];
        MidiCh* m;
        float gl, gr, g, *fx;
        uint32_t step;
        int pan;
        if (!v->on) continue;
        m = v->chan;
        fx = v->core ? fx1 : fx0;
        {
            int b = m->bend - 0x2000;
            double semis = (b >= 0 ? b * v->bend_hi : b * v->bend_lo) / (8192.0 * 128.0);
            double p = v->base_pitch * pow(2.0, semis / 12.0) * 4096.0;
            step = p > 0x3FFF ? 0x3FFF : (uint32_t)p;
        }
        pan = mono ? 64 : m->pan + v->pan;
        if (pan < 0) pan = 0; else if (pan > 127) pan = 127;
        g = master * user_volume * v->vol * m->vol / 127.0f;
        gl = g * (pan <= 64 ? 1.0f : (127 - pan) / 63.0f);
        gr = g * (pan >= 64 ? 1.0f : pan / 64.0f);
        for (int k = 0; k < n; k++) {
            float smp = v->s0 + (v->s1 - v->s0) * (v->frac / 4096.0f), env;
            if (!adsr_tick(v)) { v->on = 0; break; }
            v->heard = 1;
            env = v->level / 32768.0f;
            if (v->dry) { acc[k * 2] += smp * env * gl; acc[k * 2 + 1] += smp * env * gr; }
            if (v->wet) { fx[k * 2] += smp * env * gl; fx[k * 2 + 1] += smp * env * gr; }
            if (m >= midi[4] && m < midi[4] + 16) voice_sum += fabsf(smp * env) * v->vol;
            for (v->frac += step; v->frac >= 4096; v->frac -= 4096) {
                v->s0 = v->s1;
                if (!next_sample(v, &v->s1)) { v->on = 0; break; }
            }
            if (!v->on) break;
        }
    }
}

/* ---------------------------------------------------------------- command queue (game thread -> mixer) */
/*
 * On the PS2 the driver runs on the IOP beside the game: commands arrive whenever the EE sends
 * them and a 240 Hz timer ticks the TQ slots and the MIDI ports in between. Here the tick runs in
 * the mixer, every 200 output samples, and the game thread queues the driver commands (in order,
 * stamped with the time they were sent). A mixed buffer stands for the time since the previous
 * one: each queued command runs at its share of the buffer, so commands keep their spacing and a
 * sequence started in one frame plays its first ticks before the next frame's commands stop it.
 * Without an audio device the commands run at once and the ticks follow the vblank count
 * (snd_synth_update).
 */
#define QLEN 1024
static struct { uint64_t t; uint8_t len, c[8]; } q[QLEN];
static unsigned q_r, q_w;            /* (lock) */
static uint64_t mix_prev_ns;         /* when the previous buffer was mixed */
static int tick_wait;                /* samples to the next driver tick */
static void driver_tick(void);

void snd_synth_flush(void)
{
    for (; q_r != q_w; q_r++) snd_synth_command(q[q_r % QLEN].c, q[q_r % QLEN].len);
}

void snd_synth_post(const uint8_t* c, int len)
{
    if (!stream) { snd_synth_command(c, len); return; }
    if (q_w - q_r == QLEN) snd_synth_flush();   /* the mixer is not pulling: run them now */
    q[q_w % QLEN].t = SDL_GetTicksNS();
    q[q_w % QLEN].len = (uint8_t)len;
    memcpy(q[q_w % QLEN].c, c, (size_t)len);
    q_w++;
}

static void SDLCALL mix(void* ud, SDL_AudioStream* s, int additional, int total)
{
    float acc[512 * 2], fx[2][512 * 2];
    int16_t out[512 * 2];
    int frames = additional / 4 > 0 ? additional / 4 : 1;
    uint64_t now = SDL_GetTicksNS(), from = now - (uint64_t)frames * 1000000000u / OUT_RATE;
    (void)ud; (void)total;
    if (mix_prev_ns && mix_prev_ns < now && now - mix_prev_ns < 1000000000u) from = mix_prev_ns;
    mix_prev_ns = now;
    for (int done = 0; done < frames; ) {
        int n = frames - done < 512 ? frames - done : 512;
        memset(acc, 0, sizeof(float) * n * 2);
        memset(fx, 0, sizeof(fx));
        SDL_LockMutex(lock);
        for (int k = 0; k < n; ) {
            int seg = n - k;
            for (; q_r != q_w && q[q_r % QLEN].t < now; q_r++) {   /* commands due at this sample */
                uint64_t t = q[q_r % QLEN].t;
                int at = t > from ? (int)((t - from) * (uint64_t)frames / (now - from)) - done - k : 0;
                if (at > 0) { if (at < seg) seg = at; break; }
                snd_synth_command(q[q_r % QLEN].c, q[q_r % QLEN].len);
            }
            if (tick_wait <= 0) { driver_tick(); tick_wait = OUT_RATE / 240; }
            if (seg > tick_wait) seg = tick_wait;
            render(acc + k * 2, fx[0] + k * 2, fx[1] + k * 2, seg);
            k += seg;
            tick_wait -= seg;
        }
        for (int c = 0; c < 2; c++) if (rev[c].mode) reverb_run(&rev[c], fx[c], acc, n);
        SDL_UnlockMutex(lock);
        done += n;
        for (int k = 0; k < n * 2; k++) {
            float f = acc[k];
            out[k] = (int16_t)(f > 32767 ? 32767 : f < -32768 ? -32768 : f);
        }
        SDL_PutAudioStreamData(s, out, n * 4);
    }
}

void snd_synth_init(SDL_AudioDeviceID dev)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, OUT_RATE };
    lock = SDL_CreateMutex();
    load_reverb_presets();
    for (int p = 0; p < SE_PORTS; p++)
        for (int c = 0; c < 16; c++) midi[p][c] = (MidiCh){ 0, 127, 64, 0x2000 };
    if (!dev) return;
    stream = SDL_CreateAudioStream(&spec, &spec);
    if (!stream) return;
    SDL_SetAudioStreamGetCallback(stream, mix, NULL);
    SDL_BindAudioStream(dev, stream);
}

/* ---------------------------------------------------------------- TQ layer */

/* a slot's bank: the port's, extra enemy bank xb (1..31), or XB_PARTNER - co-op: arms programs
   SND_PARTNER_TQ.. of port 1 play program tq - SND_PARTNER_TQ of the partner's weapon bank */
#define XB_PARTNER 32
static const SndBlob* slot_hd(int port, int xb) { return xb == XB_PARTNER ? &g_snd_banks.arms2_hd : xb ? &g_snd_banks.xhd[xb - 1] : &g_snd_banks.se_hd[port]; }
static const SndBlob* slot_bd(int port, int xb) { return xb == XB_PARTNER ? &g_snd_banks.arms2_bd : xb ? &g_snd_banks.xbd[xb - 1] : &g_snd_banks.bd[se_bd_port[port]]; }
static int slot_tq(const TqSlot* t) { return t->xb == XB_PARTNER ? t->tq - SND_PARTNER_TQ : t->tq; }

static uint32_t tq_length(const SndBlob* hd, int tq)
{
    /* se_info[tq] as built by the driver on HDDataSet2: VAG of the program's first split */
    uint32_t prog = hd_entry(hd, HD_PROG, tq), vag, rate, idx;
    if (!prog) return 0;
    idx = rd16(hd, prog + rd32(hd, prog));              /* sampleSetIndex, used as VAG index */
    vag = hd_entry(hd, HD_VAGI, idx);
    if (!vag) return 0;
    if (rd8(hd, vag + 6)) return 0xFFFF;                 /* looping VAG */
    rate = rd16(hd, vag + 4);
    return rate ? (uint32_t)(vag_size(hd, idx) * 420u / rate) * 16 + 0x70 : 0;
}

static void tq_chgreq(int port, int ch)
{
    TqSlot* t = &slot[port][ch];
    MidiCh* m = &midi[port][ch];
    if (t->flags & 0x20) {
        int v = t->mvol * t->vol / 127;
        t->flags &= ~0x20u;
        m->vol = (uint8_t)(v > 127 ? 127 : v);
    }
    if (t->flags & 0x40) {
        int p = (int8_t)t->pan - 0x40 + t->mpan;
        t->flags &= ~0x40u;
        m->pan = (uint8_t)(p < 0 ? 0 : p > 127 ? 127 : p);
    }
    if (t->flags & 0x80) {
        int p = t->pitch + t->mpitch - 0x2000;
        t->flags &= ~0x80u;
        m->bend = (uint16_t)(p < 0 ? 0 : p > 0x3FFF ? 0x3FFF : p);
    }
}

static void tq_tick(void)
{
    for (int port = 0; port < SE_PORTS; port++) {
        for (int ch = 0; ch < 16; ch++) {
            TqSlot* t = &slot[port][ch];
            if (!((active_bits[port] >> ch) & 1)) continue;
            if (t->flags & 2) {
                key_off(&midi[port][ch], -1);
                t->flags &= ~2u;
                if (!(t->flags & 4)) { t->flags &= ~1u; active_bits[port] &= ~(1u << ch); t->count = 0; continue; }
            }
            if (t->flags & 4) {
                const SndBlob* hd = slot_hd(port, t->xb);
                uint32_t prog = hd_entry(hd, HD_PROG, slot_tq(t)), idx, smp;
                midi[port][ch].vol = t->mvol & 0x7F;
                midi[port][ch].prog = slot_tq(t) & 0x7F;
                idx = prog ? rd16(hd, prog + rd32(hd, prog)) : 0;
                smp = hd_entry(hd, HD_SMPL, idx);
                key_on(hd, slot_bd(port, t->xb), &midi[port][ch], smp ? rd8(hd, smp + 11) : 60, 127);
                t->flags &= ~4u;
            }
            if (t->count != 0xFFFF) t->count = (t->pitch >> 9) < t->count ? t->count - (t->pitch >> 9) : 0;
            if (t->count == 0) {
                key_off(&midi[port][ch], -1);
                t->flags &= ~1u;
                active_bits[port] &= ~(1u << ch);
                continue;
            }
            tq_chgreq(port, ch);
        }
    }
}

/* ---------------------------------------------------------------- MIDI ports (SQ sequencer) */
/*
 * SQ files (format/sound/sq.h): Midi chunk -> data blocks {POFST sequence, division, optional
 * compression block}. The event stream is SMF-like (VLQ delta, running status, FF meta) with
 * two space savers observed in the game's data: note-off carries only the note number, and
 * bit 7 set on an event's last data byte means the following delta time is 0 and omitted.
 * Loops as in Sony's modmidi (MODMIDI.IRX): CC99 = 0 + CC6 = id marks a loop start; CC99 = 1,
 * CC6 = id, CC38 = count marks its end (count 0 = forever; TSNDDRV's repeat callback always
 * allows repeating). Up to 8 loop ids per port.
 */
#define MIDI_PORTS 10   /* the EE's 10 MIDI handles (ps2_sg_sd.c __midi_value); its system slot rotates over ports 2, 8 and 9 */
typedef struct {
    int playing, bank;
    const SndBlob* sq;
    uint32_t pos, comp_table, comp_size;   /* comp_size != 0: A0 events are compressed key-ons */
    int nrpn, data_msb, zero_delta;
    struct { int id, count; uint32_t pos; uint8_t status; int zero_delta; } loop[8];
    uint8_t status;
    double ticks;                    /* MIDI ticks available to consume */
    uint32_t wait;                   /* ticks until the next event */
    uint32_t tempo;                  /* us per quarter note */
    uint16_t division;
    uint8_t vol, pan;                /* port controls (BgmReq / BgmChg) */
    uint16_t pitch;
    struct { uint8_t vol, pan; uint16_t bend; } raw[16];
    MidiCh ch[16];
} MidiPort;
static MidiPort mport[MIDI_PORTS];
static uint16_t midi_bits;

uint16_t snd_synth_midi_active(void) { return midi_bits; }

static void port_apply(MidiPort* p, int c)
{
    int v = p->raw[c].vol * p->vol / 127, pan = p->raw[c].pan + p->pan - 64, bend = p->raw[c].bend + p->pitch - 0x2000;
    p->ch[c].vol = (uint8_t)(v > 127 ? 127 : v);
    p->ch[c].pan = (uint8_t)(pan < 0 ? 0 : pan > 127 ? 127 : pan);
    p->ch[c].bend = (uint16_t)(bend < 0 ? 0 : bend > 0x3FFF ? 0x3FFF : bend);
}

static void port_stop(int port)
{
    MidiPort* p = &mport[port];
    for (int c = 0; c < 16; c++) key_off(&p->ch[c], -1);
    p->playing = 0;
    midi_bits &= ~(1u << port);
}

static uint8_t sq8(const MidiPort* p) { return p->pos < p->sq->size ? p->sq->data[p->pos] : 0; }
static uint8_t sq_next(MidiPort* p) { uint8_t b = sq8(p); p->pos++; return b; }

static uint32_t read_delta(MidiPort* p)
{
    uint32_t v = 0;
    if (p->zero_delta) { p->zero_delta = 0; return 0; }
    for (int i = 0; i < 4; i++) { uint8_t b = sq_next(p); v = (v << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
    return v;
}

static void port_event(int port)
{
    MidiPort* p = &mport[port];
    uint8_t st = sq8(p), d[2] = { 0, 0 };
    int n, c;
    if (st & 0x80) p->pos++; else st = p->status;
    if (st == 0xFF) {
        uint8_t type = sq_next(p), len = sq_next(p);
        if (type == 0x51 && len == 3) p->tempo = ((uint32_t)sq8(p) << 16) | ((uint32_t)p->sq->data[p->pos + 1] << 8) | p->sq->data[p->pos + 2];
        p->pos += len;
        if (type == 0x2F) port_stop(port);
        return;
    }
    if (st == 0xF0 || st == 0xF7) { uint32_t len = read_delta(p); p->pos += len; return; }
    p->status = st;
    c = st & 15;
    n = ((st & 0xF0) == 0x80 || (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 || ((st & 0xF0) == 0xA0 && p->comp_size)) ? 1 : 2;
    for (int i = 0; i < n; i++) {
        uint8_t b = sq_next(p);
        d[i] = b & 0x7F;
        if (b & 0x80) { p->zero_delta = 1; break; }
    }
    switch (st & 0xF0) {
    case 0x80: key_off(&p->ch[c], d[0]); break;
    case 0x90:
        if (!d[1]) key_off(&p->ch[c], d[0]);
        else key_on(&g_snd_banks.midi_hd[p->bank], &g_snd_banks.bd[p->bank], &p->ch[c], d[0], d[1]);
        break;
    case 0xB0:
        if (d[0] == 7) p->raw[c].vol = d[1];
        else if (d[0] == 10) p->raw[c].pan = d[1];
        else if (d[0] == 99) { p->nrpn = d[1]; p->data_msb = -1; }
        else if (d[0] == 6) {
            p->data_msb = d[1];
            if (p->nrpn == 0) {   /* loop start: remember the position after this event */
                int k, slot = -1;
                for (k = 0; k < 8; k++) if (p->loop[k].id == d[1]) { slot = k; break; }
                if (slot < 0) for (k = 0; k < 8; k++) if (p->loop[k].id < 0) { slot = k; break; }
                if (slot >= 0) {
                    p->loop[slot].id = d[1]; p->loop[slot].count = -1;
                    p->loop[slot].pos = p->pos; p->loop[slot].status = p->status; p->loop[slot].zero_delta = p->zero_delta;
                }
            }
        } else if (d[0] == 38 && p->nrpn == 1 && p->data_msb >= 0) {   /* loop end */
            for (int k = 0; k < 8; k++) {
                if (p->loop[k].id != p->data_msb) continue;
                if (p->loop[k].count < 0) p->loop[k].count = d[1];
                if (d[1] != 0 && p->loop[k].count == 0) { p->loop[k].count = -1; break; }   /* done */
                p->pos = p->loop[k].pos; p->status = p->loop[k].status; p->zero_delta = p->loop[k].zero_delta;
                if (d[1] != 0) p->loop[k].count--;
                break;
            }
        } else if (d[0] == 120 || d[0] == 123) key_off(&p->ch[c], -1);
        port_apply(p, c);
        break;
    case 0xC0: p->ch[c].prog = d[0]; break;
    case 0xE0: p->raw[c].bend = (uint16_t)(d[0] | (d[1] << 7)); port_apply(p, c); break;
    case 0xA0:
        if (p->comp_size) {   /* SCESQ_COMP_KEYON2POLY: index into (note, velocity) pairs */
            uint32_t e = p->comp_table + (uint32_t)d[0] * 2;
            static int logged;
            if (!logged++) host_log("tsnddrv: compressed SQ key-ons in use (table %u bytes)\n", p->comp_size);
            if ((uint32_t)d[0] * 2 + 1 < p->comp_size) {
                int note = rd8(p->sq, e) & 0x7F, vel = rd8(p->sq, e + 1) & 0x7F;
                if (!vel) key_off(&p->ch[c], note);
                else key_on(&g_snd_banks.midi_hd[p->bank], &g_snd_banks.bd[p->bank], &p->ch[c], note, vel);
            }
            break;
        }
        /* fall through */
    default: {   /* 0xD0, uncompressed 0xA0: not in this game's data */
        static int seen;
        if (!seen++) host_log("tsnddrv: SQ event %02x not supported\n", st);
    }
    }
}

static void port_tick(int port)
{
    MidiPort* p = &mport[port];
    if (!p->playing) return;
    p->ticks += 1e6 / 240.0 * p->division / (p->tempo ? p->tempo : 500000);
    for (int guard = 0; p->playing && p->ticks >= p->wait && guard < 4096; guard++) {
        p->ticks -= p->wait;
        port_event(port);
        if (p->playing) p->wait = read_delta(p);
        if (p->pos >= p->sq->size) port_stop(port);
    }
}

static void port_play(int port, int bank, int vol, int block)
{
    MidiPort* p;
    const SndBlob* sq;
    uint32_t chunk, max, ofs, blk;
    if (port >= MIDI_PORTS || bank >= 4) goto drop;
    sq = &g_snd_banks.sq[bank];
    chunk = rd32(sq, 0x24);                                  /* Sequ header: midiChunkAddr */
    max = rd32(sq, chunk + 12);
    if (chunk == 0xFFFFFFFFu || max == 0xFFFFFFFFu || (uint32_t)block > max || !g_snd_banks.midi_hd[bank].size) goto drop;
    ofs = rd32(sq, chunk + 16 + block * 4);
    if (ofs == 0xFFFFFFFFu) goto drop;
    port_stop(port);
    p = &mport[port];
    blk = chunk + ofs;
    memset(p, 0, sizeof(*p));
    p->sq = sq; p->bank = bank;
    p->pos = blk + rd32(sq, blk);
    if (rd32(sq, blk) > 6 && (rd16(sq, blk + 6) & 1)) {   /* compression block: key-ons as poly key pressure */
        p->comp_table = blk + 10;
        p->comp_size = rd16(sq, blk + 8);
    }
    p->division = rd16(sq, blk + 4);
    p->tempo = 500000;
    p->vol = (uint8_t)vol; p->pan = 64; p->pitch = 0x2000;
    for (int c = 0; c < 16; c++) { p->raw[c].vol = 127; p->raw[c].pan = 64; p->raw[c].bend = 0x2000; port_apply(p, c); }
    for (int k = 0; k < 8; k++) p->loop[k].id = -1;
    p->data_msb = -1;
    p->playing = 1;
    p->wait = read_delta(p);
    midi_bits |= 1u << port;
    return;
drop:
    if (trace_snd()) printf("tsnd: MIDI port %d bank %d block %d dropped (no such port, bank or block)\n", port, bank, block);
}

/* the driver's 240 Hz tick */
static void driver_tick(void)
{
    tq_tick();
    for (int i = 0; i < MIDI_PORTS; i++) port_tick(i);
}

/* no audio device: run the ticks up to the current vblank (59.94 Hz -> 4.004 ticks) */
void snd_synth_update(void)
{
    uint64_t due = ps2_vblank_count() * 4004 / 1000;
    if (stream) return;   /* the mixer ticks (mix) */
    if (due - ticks_done > 240) ticks_done = due - 240;
    for (; ticks_done < due; ticks_done++) driver_tick();
}

static void tq_request(int port, int tq, int ch, int vol, int pan, int pitch, int xb)
{
    TqSlot* t;
    if (port == 1 && !xb && tq >= SND_PARTNER_TQ && g_snd_banks.arms2_hd.size) xb = XB_PARTNER;
    if (port >= SE_PORTS || ch >= 16 || !slot_hd(port, xb)->size) {
        if (trace_snd()) printf("tsnd: SE port %d ch %d prog %d dropped (bank not loaded)\n", port, ch, tq);
        return;
    }
    t = &slot[port][ch];
    t->xb = (uint8_t)xb;
    t->count = tq_length(slot_hd(port, xb), xb == XB_PARTNER ? tq - SND_PARTNER_TQ : tq);
    if (xb && trace_snd()) printf("tsnd: SE port %d ch %d prog %d from extra bank %d\n", port, ch, tq, xb);
    t->flags |= 0x20 | 0x40 | 0x80;
    t->tq = (uint8_t)tq; t->vol = (uint8_t)vol; t->pan = (uint8_t)pan; t->pitch = (uint16_t)pitch;
    t->mvol = 0x7F; t->mpan = 0x40; t->mpitch = 0x2000;
    active_bits[port] |= 1u << ch;
    t->flags = (t->flags | 4 | 1) & ~2u;
}

static void tq_change(int port, int tq, int ch, int vol, int pan, int pitch)
{
    TqSlot* t;
    if (port >= SE_PORTS || ch >= 16) return;
    t = &slot[port][ch];
    if (!(t->flags & 1) || t->tq != tq) return;
    if (vol >= 0) { t->vol = (uint8_t)vol; t->flags |= 0x20; }
    if (pan >= 0) {   /* the driver drops a change back to centre (retail behaviour) */
        t->pan = (uint8_t)pan;
        if (pan == 0x40) t->flags &= ~0x40u; else t->flags |= 0x40;
    }
    if (pitch >= 0) {
        t->pitch = (uint16_t)pitch;
        if (pitch == 0x2000) t->flags &= ~0x80u; else t->flags |= 0x80;
    }
}

void snd_synth_bank_changed(int kind, int index) { (void)kind; (void)index; }

static int blob_set(SndBlob* b, const void* src, uint32_t n)
{
    if (n > b->cap) {
        uint8_t* d = realloc(b->data, n);
        if (!d) return 0;
        b->data = d;
        b->cap = n;
    }
    if (n) memcpy(b->data, src, n);
    b->size = n;
    return 1;
}

void snd_partner_arms(const void* hd, uint32_t hd_size, const void* bd, uint32_t bd_size)
{
    SndBlob *h = &g_snd_banks.arms2_hd, *b = &g_snd_banks.arms2_bd;
    snd_synth_lock();
    snd_synth_flush();   /* queued requests play from the bank they were sent for */
    for (int i = 0; i < VOICES; i++) if (voice[i].bd == b) voice[i].on = 0;   /* playing the old bank */
    for (int ch = 0; ch < 16; ch++) if (slot[1][ch].xb == XB_PARTNER) { slot[1][ch].flags = 0; slot[1][ch].xb = 0; active_bits[1] &= ~(1u << ch); }
    if (!hd || !blob_set(h, hd, hd_size) || !blob_set(b, bd, bd_size)) h->size = b->size = 0;
    snd_synth_unlock();
}

int snd_xbank_prog_ms(int xb, int tq)
{
    uint32_t n = 0;
    if (xb < 1 || xb > 31 || tq < 0 || tq > 127) return 0;
    snd_synth_lock();
    if (g_snd_banks.xbd[xb - 1].size) n = tq_length(&g_snd_banks.xhd[xb - 1], tq);
    snd_synth_unlock();
    return n && n != 0xFFFF ? (int)(n * 1000u / (16 * 240)) : 0;   /* (the slot counts 16 a tick at 240 Hz, unbent) */
}

void snd_synth_xbanks_stop(void)
{
    snd_synth_flush();
    for (int i = 0; i < VOICES; i++)
        if (voice[i].on && voice[i].bd >= g_snd_banks.xbd && voice[i].bd < g_snd_banks.xbd + 31) voice[i].on = 0;
    for (int p = 0; p < SE_PORTS; p++)
        for (int ch = 0; ch < 16; ch++)
            if (slot[p][ch].xb && slot[p][ch].xb != XB_PARTNER) { slot[p][ch].flags = 0; slot[p][ch].xb = 0; active_bits[p] &= ~(1u << ch); }
}

void snd_synth_command(const uint8_t* c, int len)
{
    int cd = c[0];
    (void)len;
    if (cd < 0x10) {
        /* port byte: SE port, PC extra bank << 3 (ps2_sg_sd.c sdSysServer; 0 on the PS2) */
        int port = c[1] & 7, xb = c[1] >> 3, tq = c[2] & 0x7F, ch = c[3] & 0x7F, i = 4;
        int vol, pan, pitch;
        if (cd == 8) {   /* cancel */
            if (port < SE_PORTS && ch < 16 && (slot[port][ch].flags & 1)) slot[port][ch].flags |= 2;
            return;
        }
        vol = (cd & 1) ? c[i++] : (cd & 8) ? -1 : 0x7F;
        pan = (cd & 2) ? c[i++] : (cd & 8) ? -1 : 0x40;
        if (cd & 4) { pitch = (c[i] << 8) | c[i + 1]; i += 2; } else pitch = (cd & 8) ? -1 : 0x2000;
        if (cd & 8) tq_change(port, tq, ch, (int8_t)vol < 0 ? -1 : vol, (int8_t)pan < 0 ? -1 : pan, (int16_t)pitch < 0 ? -1 : pitch);
        else tq_request(port, tq, ch, vol, pan, pitch, xb);
        return;
    }
    switch (cd) {
    case 0x10:   /* all stop */
        for (int p = 0; p < SE_PORTS; p++)
            for (int ch = 0; ch < 16; ch++) if (slot[p][ch].flags & 1) slot[p][ch].flags |= 2;
        return;
    case 0x42: mono = c[1] != 0; return;
    case 0x44: {   /* SdrSetRev: mode | (core+1) << 6, depth; delay/feedback (ECHO/DELAY only) ignored */
        int core = (c[1] >> 6) - 1, mode = c[1] & 0x3F, depth = (c[2] << 8) | c[3];
        if (core == 0 || core == 2) set_reverb(0, mode, depth);
        if (core == 1 || core == 2) set_reverb(1, mode, depth);
        return;
    }
    case 0x4B: master = ((c[1] << 8) | c[2]) / 16383.0f; return;
    case 0x2F: return;   /* status put: se_info/midi_info are written when the EE reads them (snd_iop_status) */
    case 0x20: port_play(c[1], c[2], c[3], c[4]); return;   /* SdrBgmReq: port, bank, vol, block */
    case 0x21: if (c[1] < MIDI_PORTS) port_stop(c[1]); return;
    case 0x27: {   /* SdrBgmChg: port, vol, pan, pitch bend (LSB, MSB) */
        MidiPort* p;
        if (c[1] >= MIDI_PORTS) return;
        p = &mport[c[1]];
        p->vol = c[4] & 0x7F; p->pan = c[5] & 0x7F; p->pitch = (uint16_t)((c[6] & 0x7F) | ((c[7] & 0x7F) << 7));
        for (int ch = 0; ch < 16; ch++) port_apply(p, ch);
        return;
    }
    }
    {
        static int seen[256];
        if (!seen[cd]++) host_log("tsnddrv: command %02x not emulated yet\n", cd);
    }
}
