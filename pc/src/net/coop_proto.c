/* Co-op wire checks, join code, transfers and room script tables (coop_proto.h). Pure functions:
   no game state, so pc/tests/test_coop.c runs them against malformed input. */
#include "coop_proto.h"
#include "monocypher.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(Pose) == 112, "wire: Pose");
_Static_assert(sizeof(PoseExt) == 108, "wire: PoseExt");
_Static_assert(sizeof(Event) == 96, "wire: Event");
_Static_assert(sizeof(AtrRec) == 36, "wire: AtrRec (ATR_WORK)");
_Static_assert(sizeof(CoopHello) == sizeof(Pose), "wire: CoopHello (a build before protocol 28 must see a datagram of a Pose's size to report the version)");
_Static_assert(COOP_CODE_LEN == RDV_CODE_LEN,"join codes are made and read as room codes (rdv_code_from, rdv_code_norm)");

static int fin(float f) { return isfinite(f) && fabsf(f) < 1e7f; }
static int fins(const float* f, int n) { while (n--) if (!fin(*f++)) return 0; return 1; }

int coop_flag_bits(int set) { return set == 1 || set == 3 ? 1024 : set == 2 ? 512 : set == 8 ? 256 : 0; }
int coop_item_word_ok(uint32_t w) { int id = (w >> 16) & 0xFF; return id > 0 && id < 156 && (w & 0xFFFF) <= 999; }

int coop_check_pose(const Pose* p)
{
    if (!fin(p->px) || !fin(p->py) || !fin(p->pz)) return COOP_BAD_FLOAT;
    if (p->busy > BUSY_TRADE || p->mset < MSET_NONE || p->mset >= COOP_ENE_SLOTS) return COOP_BAD_FIELD;
    if (p->flr > 32) return COOP_BAD_FIELD;   /* flr_no + 3: rooms have floors -2..29 (rom->grand[2 + flr_no], 32 entries); 0 not sent */
    if (p->rcase > 10) return COOP_BAD_FIELD;   /* room case + 1: cases 0..9 (one digit of the file name); 0 not sent */
    if ((p->flags & ~PF_ALL) || (p->opts & ~OPT_ALL) || (p->opts2 & ~OPT2_ALL)) return COOP_BAD_FIELD;   /* only the flags and game rules this protocol has */
    return COOP_OK;
}

int coop_check_dgram(const void* buf, int n, int* nev, int* nene, int* ext)
{
    Pose p;
    int x;
    if (n < (int)sizeof(Pose)) return COOP_BAD_SIZE;
    memcpy(&p, buf, sizeof(p));
    if (p.magic != COOP_MAGIC) return COOP_BAD_HDR;
    if (p.version != COOP_VERSION) return COOP_BAD_VERSION;
    if (p.type < PKT_POSE || p.type > PKT_REJECT) return COOP_BAD_TYPE;
    if (p.type != PKT_POSE) { *nev = *nene = *ext = 0; return n == (int)sizeof(Pose) ? COOP_OK : COOP_BAD_SIZE; }
    if (p.nev > COOP_EV_FRAME) return COOP_BAD_COUNT;
    x = (p.flags & PF_EXT) != 0;
    {   /* the snapshot block's length is in its header (protocol 45) */
        int at = (int)(sizeof(Pose) + p.nev * sizeof(Event)), sz = at + x * (int)sizeof(PoseExt);
        if (p.nene) {
            const unsigned char* h = (const unsigned char*)buf + at;
            if (n < sz + COOP_SNAP_HDR) return COOP_BAD_SIZE;
            sz += COOP_SNAP_HDR + (h[0] | h[1] << 8);
        }
        if (n != sz) return COOP_BAD_SIZE;
    }
    if (x) {   /* the extension's enumerations */
        PoseExt e;
        memcpy(&e, (const unsigned char*)buf + n - sizeof(e), sizeof(e));
        if (e.items > ITEMS_CURRENT || e.rec > REC_OFF || e.br > 1 || e.br_pad[0] || e.br_pad[1] || e.br_pad[2]) return COOP_BAD_FIELD;
    }
    *nev = p.nev; *nene = p.nene; *ext = x;
    return coop_check_pose(&p);
}

int coop_check_snap(const EneSnap* s)
{
    if (s->idx >= COOP_ENE_SLOTS || s->mdl < -1 || s->mdl >= 16 || s->id >= 100 || (s->root & ~SNAP_ROOT_ALL)) return COOP_BAD_FIELD;   /* id: bhJumpEnemy[100] */
    if (s->mnw != MNW_ROOM && s->mnw != MNW_KEEP && (s->mnw < 0 || s->mnw >= COOP_MNW_MAX)) return COOP_BAD_FIELD;
    if (s->mtn_no < 0 || s->mtn_no >= COOP_MNW_MAX || s->frm_no < 0) return COOP_BAD_FIELD;
    if (!fin(s->px) || !fin(s->py) || !fin(s->pz) || !fins(s->watr, 7)) return COOP_BAD_FLOAT;
    return COOP_OK;
}

/* ---------------------------------------------------------------- snapshots, protocol 45 (coop_proto.h) */
static int32_t qpos(float f)   /* 1/COOP_SNAP_Q unit; fin() bounds every position the receiver takes */
{
    float v = f * COOP_SNAP_Q;
    return v == v ? (int32_t)lrintf(v > 1e9f ? 1e9f : v < -1e9f ? -1e9f : v) : 0;
}
void coop_snap_quant(const EneSnap* s, CoopSnapQ* q)
{
    int32_t* v = q->v;
    int k;
    v[SQ_PX] = qpos(s->px); v[SQ_PY] = qpos(s->py); v[SQ_PZ] = qpos(s->pz);
    for (k = 0; k < 6; k++) v[SQ_C1X + k] = (int32_t)((uint32_t)qpos(s->watr[k]) - (uint32_t)v[k % 3 == 0 ? SQ_PX : k % 3 == 1 ? SQ_PY : SQ_PZ]);   /* the capsule from the enemy */
    v[SQ_R] = qpos(s->watr[6]);
    v[SQ_AX] = s->ax; v[SQ_AY] = s->ay; v[SQ_AZ] = s->az;
    v[SQ_FRM] = s->frm_no >> 8; v[SQ_MFRM] = s->mtn_frm0 >> 8;   /* 1/256 frame: the whole frame stays exact */
    v[SQ_MTN] = s->mtn_no; v[SQ_HOKC] = s->hokan_count; v[SQ_HOKR] = s->hokan_rate; v[SQ_MD] = s->mtn_md;
    v[SQ_ADD] = s->mtn_add; v[SQ_MSEQ] = (int32_t)s->mtn_seq;
    v[SQ_MODE] = (int32_t)((uint32_t)s->mode[0] | (uint32_t)s->mode[1] << 8 | (uint32_t)s->mode[2] << 16 | (uint32_t)s->mode[3] << 24);
    v[SQ_FLG] = (int32_t)s->flg; v[SQ_FLG2] = (int32_t)s->flg2; v[SQ_STFLG] = (int32_t)s->stflg; v[SQ_MDFLG] = (int32_t)s->mdflg;
    v[SQ_HP] = s->hp;
    v[SQ_MDL] = (int32_t)((uint32_t)(uint8_t)s->mdl | (uint32_t)(uint16_t)s->mnw << 8);
    v[SQ_ID] = s->id | s->root << 8;
    v[SQ_NECK] = (int32_t)s->neck;
}
int coop_snap_dequant(const CoopSnapQ* q, int idx, EneSnap* s)
{
    const int32_t* v = q->v;
    int k;
    if ((uint32_t)v[SQ_MDL] >> 24 || (uint32_t)v[SQ_ID] >> 16 || v[SQ_HOKC] != (int16_t)v[SQ_HOKC] ||
        v[SQ_FRM] < 0 || v[SQ_FRM] >= 1 << 23 || v[SQ_MFRM] < 0 || v[SQ_MFRM] >= 1 << 23) return 0;
    memset(s, 0, sizeof(*s));
    s->idx = (uint8_t)idx;
    s->px = v[SQ_PX] / COOP_SNAP_Q; s->py = v[SQ_PY] / COOP_SNAP_Q; s->pz = v[SQ_PZ] / COOP_SNAP_Q;
    for (k = 0; k < 6; k++) s->watr[k] = (int32_t)((uint32_t)v[SQ_C1X + k] + (uint32_t)v[k % 3 == 0 ? SQ_PX : k % 3 == 1 ? SQ_PY : SQ_PZ]) / COOP_SNAP_Q;
    s->watr[6] = v[SQ_R] / COOP_SNAP_Q;
    s->ax = v[SQ_AX]; s->ay = v[SQ_AY]; s->az = v[SQ_AZ];
    s->frm_no = v[SQ_FRM] << 8; s->mtn_frm0 = v[SQ_MFRM] << 8;
    s->mtn_no = v[SQ_MTN]; s->hokan_count = (int16_t)v[SQ_HOKC]; s->hokan_rate = v[SQ_HOKR]; s->mtn_md = v[SQ_MD];
    s->mtn_add = v[SQ_ADD]; s->mtn_seq = (uint32_t)v[SQ_MSEQ];
    for (k = 0; k < 4; k++) s->mode[k] = (uint8_t)((uint32_t)v[SQ_MODE] >> 8 * k);
    s->flg = (uint32_t)v[SQ_FLG]; s->flg2 = (uint32_t)v[SQ_FLG2]; s->stflg = (uint32_t)v[SQ_STFLG]; s->mdflg = (uint32_t)v[SQ_MDFLG];
    s->hp = v[SQ_HP];
    s->mdl = (int8_t)(v[SQ_MDL] & 0xFF); s->mnw = (int16_t)(v[SQ_MDL] >> 8 & 0xFFFF);
    s->id = (uint8_t)v[SQ_ID]; s->root = (uint8_t)(v[SQ_ID] >> 8);
    s->neck = (uint32_t)v[SQ_NECK];
    return 1;
}

static int vput(uint8_t* p, uint32_t x)
{
    int n = 0;
    do { p[n++] = (uint8_t)(x & 0x7F) | (x > 0x7F ? 0x80 : 0); x >>= 7; } while (x);
    return n;
}
static int vget(const uint8_t* p, int n, uint32_t* x)   /* bytes read, 0 bad (past n, more than 32 bits) */
{
    int k;
    *x = 0;
    for (k = 0; k < 5 && k < n; k++) {
        if (k == 4 && p[k] > 0x0F) return 0;
        *x |= (uint32_t)(p[k] & 0x7F) << 7 * k;
        if (!(p[k] & 0x80)) return k + 1;
    }
    return 0;
}
/* one record: bytes read, 0 bad; d[] the deltas (0 where the mask has no bit) */
static int rec_get(const uint8_t* p, int n, int* idx, int* age, uint32_t d[SQ_N])
{
    uint32_t mask;
    int at = 2, k, r;
    if (n < 3 || p[0] >= COOP_ENE_SLOTS) return 0;
    *idx = p[0]; *age = p[1];
    if (!(r = vget(p + at, n - at, &mask)) || mask >> SQ_N) return 0;
    for (at += r, k = 0; k < SQ_N; k++) {
        uint32_t z = 0;
        if (mask >> k & 1) {
            if (!(r = vget(p + at, n - at, &z))) return 0;
            at += r;
        }
        d[k] = z >> 1 ^ (0u - (z & 1));   /* zigzag */
    }
    return at;
}
int coop_snap_check(const uint8_t* blk, int n, int nrec)
{
    uint32_t d[SQ_N];
    int at = COOP_SNAP_HDR, k, idx, age, r;
    if (n < COOP_SNAP_HDR || (blk[0] | blk[1] << 8) != n - COOP_SNAP_HDR || (blk[2] & 0x70)) return COOP_BAD_SIZE;
    for (k = 0; k < nrec; k++, at += r)
        if (!(r = rec_get(blk + at, n - at, &idx, &age, d))) return COOP_BAD_FIELD;
    return at == n ? COOP_OK : COOP_BAD_SIZE;
}

void coop_snaptx_reset(CoopSnapTx* t) { memset(t, 0, sizeof(*t)); }
void coop_snaptx_ack(CoopSnapTx* t, uint32_t ack)
{
    int i;
    if (!ack || ack == t->ack) return;
    t->ack = ack;
    for (i = 0; i < COOP_ENE_SLOTS; i++)   /* the slots that frame had (a frame never sent has no entry) */
        if (t->h[i][ack % COOP_SNAP_HIST].seq == ack && (!t->acked[i] || (int32_t)(ack - t->acked[i]) > 0)) t->acked[i] = ack;
}
int coop_snaptx_put(CoopSnapTx* t, uint32_t seq, const EneSnap* s, uint8_t* out, int cap, int* full)
{
    static const CoopSnapQ zero;
    uint8_t tmp[COOP_SNAP_REC_MAX];
    const CoopSnapQ* base = &zero;
    CoopSnapQ q;
    uint32_t a = t->acked[s->idx % COOP_ENE_SLOTS], mask = 0;
    int i = s->idx % COOP_ENE_SLOTS, age = 0, n, k;
    if (!seq) return 0;   /* (0 is "none" in acknowledgements) */
    coop_snap_quant(s, &q);
    if (a && seq - a < COOP_SNAP_HIST && seq != a && t->h[i][a % COOP_SNAP_HIST].seq == a) { base = &t->h[i][a % COOP_SNAP_HIST].q; age = (int)(seq - a); }
    for (k = 0; k < SQ_N; k++) mask |= (uint32_t)(q.v[k] != base->v[k]) << k;
    tmp[0] = (uint8_t)i; tmp[1] = (uint8_t)age;
    n = 2 + vput(tmp + 2, mask);
    for (k = 0; k < SQ_N; k++)
        if (mask >> k & 1) {
            uint32_t dd = (uint32_t)q.v[k] - (uint32_t)base->v[k];
            n += vput(tmp + n, dd << 1 ^ (0u - (dd >> 31)));
        }
    if (n > cap) return 0;
    memcpy(out, tmp, n);
    t->h[i][seq % COOP_SNAP_HIST].seq = seq;
    t->h[i][seq % COOP_SNAP_HIST].q = q;
    if (full) *full = !age;
    return n;
}

void coop_snaprx_reset(CoopSnapRx* r) { memset(r, 0, sizeof(*r)); }
void coop_snaptx_forget(CoopSnapTx* t, int idx)
{
    if (idx < 0 || idx >= COOP_ENE_SLOTS) return;
    memset(t->h[idx], 0, sizeof(t->h[idx]));
    t->acked[idx] = 0;
}
void coop_snaprx_forget(CoopSnapRx* r, int idx) { if (idx >= 0 && idx < COOP_ENE_SLOTS) memset(r->h[idx], 0, sizeof(r->h[idx])); }
int coop_snaprx_take(CoopSnapRx* r, uint32_t seq, const uint8_t* blk, int n, int nrec, EneSnap* out)
{
    uint32_t d[SQ_N];
    int at = COOP_SNAP_HDR, k, j, got = 0, idx, age, m;
    __typeof__(&r->f[0]) fr;
    if (!seq || coop_snap_check(blk, n, nrec)) return 0;
    if (r->seq && (int32_t)(seq - r->seq) <= -COOP_SNAP_HIST) return 0;   /* too late: its entries hold newer frames */
    if (!r->seq || (int32_t)(seq - r->seq) > 0) r->seq = seq;
    fr = &r->f[seq % COOP_SNAP_HIST];
    if (fr->seq != seq) { fr->seq = seq; fr->parts = 0; fr->last = -1; fr->bad = 0; }
    fr->parts |= (uint16_t)(1u << (blk[2] & 15));
    if (blk[2] & 0x80) fr->last = (int8_t)(blk[2] & 15);
    for (k = 0; k < nrec; k++, at += m) {
        CoopSnapBase* e;
        const CoopSnapQ* base;
        static const CoopSnapQ zero;
        uint32_t b;
        m = rec_get(blk + at, n - at, &idx, &age, d);
        b = seq - (uint32_t)age;
        e = &r->h[idx][seq % COOP_SNAP_HIST];
        if (!age) base = &zero;
        else if (b && age < COOP_SNAP_HIST && r->h[idx][b % COOP_SNAP_HIST].seq == b) base = &r->h[idx][b % COOP_SNAP_HIST].q;
        else { fr->bad = 1; r->n_bad++; continue; }   /* not the base the sender meant: nothing from it */
        for (j = 0; j < SQ_N; j++) e->q.v[j] = (int32_t)((uint32_t)base->v[j] + d[j]);
        e->seq = seq;   /* (taken even when it fails its checks below: it is the sender's state, a base later; the entry
                           held an older frame: a newer one in it would be COOP_SNAP_HIST past this, past the newest) */
        if (got < nrec && coop_snap_dequant(&e->q, idx, &out[got]) && coop_check_snap(&out[got]) == COOP_OK) got++;
    }
    if (!fr->bad && fr->last >= 0 && fr->parts == (2u << fr->last) - 1 && (!r->ack || (int32_t)(seq - r->ack) > 0)) r->ack = seq;
    return got;
}

static int xfer_fields_ok(const Event* e)
{
    int off = e->u.blob.off, len = e->u.blob.len, total = e->u.blob.total;
    return total > 0 && len > 0 && len <= 64 && off % 64 == 0 && off + len <= total &&
           (len == 64 || off + len == total);
}

static int fx_ref(int r) { return r < COOP_ENE_SLOTS || (r >= FX_RECEIVER && r <= FX_NONE); }   /* an enemy slot or FX_* */

/* The effects (decomp effect.c bhJumpEffect*: effsub*.c, en*.c) whose creators run in a sender window, from
   their creators and steps (a survey of every creation site; ids seen in play: 5 11 250 253 254 256 257 267
   274 275 297 305 353 354 356 364 370 397). Left out on purpose: effects made only by another effect's
   step or a room/event table (not in a window), an enemy's Init step (the window opens after it: 258 261
   262 270), and those that cannot go as fields: OR_WORK effects whose state overlays the fields (307
   309..312 349), raw pointers in their counters (304), exp0 an extra eff[] slot or a drawing function the
   creator sets (300 365 366), gunfire the receiver makes itself (3 9 14 132 229), 301/302 and 308 (they
   go as their call), 17 and 29 (lkwkp a barrel or a room record). */
#define A FXC_ANY
#define ANYF -128, 127
static const FxRule fx_rules[] = {
    /*  id  type_max types                                      flg        refs                        mdlver   flr      lkono   ct0..ct3 */
    {   2,  10, 1u << 3,                                     0x100000, 0,                          FXV_ANY,  0, 0,   0,  0, { 0, 0, 0, 0 } },  /* en25 */
    {   5,   2, 0,                                           0,        FXF_LINK,                   3,       ANYF,    0, 63, { A, 0, 0, 0 } },  /* blood: mdlver its colour, ani_ct of texture 5 (4) */
    {  10,   1, 1u << 1,                                     0x100000, 0,                          FXV_ANY, ANYF,    0,  0, { A, 0, 0, 0 } },  /* gun splash */
    {  11,  11, 1u << 0 | 1u << 1 | 1u << 2 | 1u << 5 | 1u << 10 | 1u << 11, 0, 0,                1,        0, 1,   0,  2, { A, 0, 0, 0 } },  /* splashes (types by mdlver: coop_check_event) */
    {  13,   0, 0,                                           0x40000,  0,                          1,        0, 0,   0,  0, { 0, A, A, 100 } },/* bow gun bolt: mdlver its model (objP) */
    {  15,   2, 1u << 2,                                     0,        0,                          FXV_ANY,  0, 6,   0,  0, { A, 0, 0, 0 } },  /* water hit: flr_no its delay */
    {  16,   0, 0,                                           0,        0,                          FXV_ANY,  0, 0,  16, 16, { 0, 0, 0, 0 } },  /* blast: lkono its delay */
    {  23,   2, 0,                                           0,        0,                          FXV_ANY,  0, 1,   0,  0, { A, 0, 0, 0 } },  /* explosion: flr_no its ani_ct (texture 9: 2) */
    {  24,   0, 0,                                           0,        0,                          FXV_ANY,  0, 1,   0,  0, { 0, 0, 0, 0 } },  /* debris: ct0 indexes its extra work */
    { 108, 0xFFFF, 0,                                        0,        0,                          FXV_ANY, ANYF,   10, 20, { 0, 0, 0, 0 } },  /* water ripple: lkono 10 12 13 15 20 (coop_check_event) */
    { 130,   6, 1u << 0 | 1u << 1 | 1u << 2 | 1u << 3 | 1u << 4 | 1u << 6, 0x4100000, 0,         FXV_ANY,  0, 0,   0,  0, { A, A, A, A } },  /* grenade / rocket: type + 14 a weapon */
    { 250, 0xFFFF, 0,                                        0,        FXF_TXP,                    3,       ANYF,    0, 63, { A, A, A, A } },  /* mince: mdlver its colour */
    { 252, 0xFFFF, 0,                                        0,        FXF_LINK_REQ,               FXV_ANY, ANYF,    0, 63, { 120, A, A, A } },/* fire: ct0 its length */
    { 253,   2, 0,                                           0,        0,                          1,       ANYF,    0, 63, { A, A, A, A } },  /* sand dust: uvtble[type + 3 * mdlver] */
    { 254,   2, 0,                                           0,        0,                          FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* sand particles: col[type] */
    { 256,   6, 0,                                           0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* acid: param[type] */
    { 257, 0xFFFF, 0,                                        0,        0,                          FXV_ANY, ANYF,    0, 63, { A, 0, A, A } },  /* acid drop: ct1 a UV index its init keeps */
    { 259,   1, 0,                                           0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* blood */
    { 260, 0xFFFF, 0,                                        0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* moth powder (en06's texture 1: apply_fx) */
    { 267,   3, 0,                                           0,        0,                          FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* fluid */
    { 274, 0xFFFF, 0,                                        0,        FXF_LINK_REQ,               FXV_ANY, ANYF,    0, 63, { 300, 8, A, A } },/* fire: ct0 its life, ct1 its delay */
    { 275, 0xFFFF, 0,                                        0,        0,                          FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* fire */
    { 297,   6, 0,                                           0,        FXF_LINK_REQ,               3,       ANYF,    0, 63, { A, A, A, A } },  /* blood burst: TypeList[type], BloodCol[mdlver] */
    { 298,  40, 0,                                           0,        FXF_LINK,                   FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* acid generator: type its count */
    { 303,   7, 0,                                           0,        0,                          3,       ANYF,    0, 63, { A, A, A, A } },  /* PrmTbl[type], ani_ct mdlver */
    { 305,   3, 0,                                           0,        FXF_LINK,                   FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* Eff305Prm[type] */
    { 306,   7, 0,                                           0,        FXF_LINK_REQ,               FXV_ANY, ANYF,    0, 63, { A, A, A, A } },
    { 350, 0xFFFF, 0,                                        0,        FXF_LKWK | FXF_TXP,         FXV_ANY, ANYF,    0, 63, { A, A, 150, 64 } },/* vomit: its zombie (sounds), ct3 a delay */
    { 352, 0xFFFF, 0,                                        0,        FXF_LINK_REQ,               FXV_ANY, ANYF,    0, 63, { A, A, A, A } },
    { 353,   0, 0,                                           0,        FXF_LINK_REQ | FXF_TXP,     FXV_ANY, ANYF,    0, 63, { A, 0, A, 64 } },
    { 354,   3, 0,                                           0,        FXF_LINK | FXF_TXP,         FXV_ANY, ANYF,    0, 63, { A, 0, A, 64 } },  /* anim_tbl[type]; ct1 not reset */
    { 355,   0, 0,                                           0,        FXF_EXP0,                   FXV_ANY, ANYF,    0, 63, { A, A, A, 64 } },
    { 356, 0xFFFF, 0,                                        0,        FXF_LKWK,                   FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* head burst: an en01 (apply_fx) */
    { 362,   4, 0,                                           0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, A, A } },
    { 363, 0xFFFF, 0,                                        0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, 150, 64 } },
    { 364,   1, 0,                                           0,        FXF_TXP | FXF_MLW,          FXV_ANY,  0, 1,   0, 63, { A, A, A, A } },  /* a part of the owner's model: mdlver < its objects */
    { 369,   1, 0,                                           0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, A, 64 } },
    { 370,  10, 0,                                           0,        FXF_LINK | FXF_TXP,         FXV_ANY, ANYF,    0, 63, { 16, 16, A, A } },/* blood emitter: eff370_tbl[type]; types 3 5 6 7 follow (coop_check_event) */
    { 396,   0, 0,                                           0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, A, A } },
    { 397,   4, 0,                                           0,        FXF_LINK | FXF_TXP | FXF_EXP0, FXV_ANY, ANYF, 0, 63, { A, A, A, A } },  /* poison: eff_info[type], exp0 its en15 (apply_fx) */
    { 399,   3, 0,                                           0,        FXF_TXP,                    FXV_ANY, ANYF,    0, 63, { A, A, A, A } },  /* pat_tbl[type] */
};
#undef A
#undef ANYF

const FxRule* coop_fx_rule(int id)
{
    unsigned i;
    for (i = 0; i < sizeof(fx_rules) / sizeof(fx_rules[0]); i++)
        if (fx_rules[i].id == id) return &fx_rules[i];
    return NULL;
}

/* a generic EV_FX against its rule (coop_proto.h FxRule) */
static int fx_entry_ok(const Event* e)
{
    const FxRule* r = coop_fx_rule(e->u.fx.id);
    int link = coop_fx_get(e->idx, e->u.fx.refs, FXR_IDX), own = coop_fx_get(e->u.fx.owner, e->u.fx.refs, FXR_OWNER),
        ex = coop_fx_get(e->u.fx.exp0, e->u.fx.refs, FXR_EXP0), t = e->u.fx.type, k, lf;
    uint32_t allow;
    if (!r) return 0;
    if (t > r->type_max || (r->types && (t >= 32 || !(r->types >> t & 1)))) return 0;
    if (r->mdlver_max != FXV_ANY && e->u.fx.mdlver > r->mdlver_max) return 0;
    if (e->u.fx.flr_no < r->flr_min || e->u.fx.flr_no > r->flr_max || e->u.fx.lkono < r->lk_min || e->u.fx.lkono > r->lk_max) return 0;
    if (e->u.fx.mode[0] | e->u.fx.mode[1] | e->u.fx.mode[2] | e->u.fx.mode[3]) return 0;   /* a fresh entry: its own init runs */
    for (k = 0; k < 4; k++)
        if (r->ct_max[k] != FXC_ANY && (e->u.fx.ct[k] < 0 || e->u.fx.ct[k] > r->ct_max[k])) return 0;
    /* what it follows: flg 0x80 exactly when a link is there (FXF_LKWK: a link without it) */
    lf = (e->u.fx.flg & 0x80) != 0;
    if (r->refs & FXF_LKWK) { if (lf || link == FX_NONE) return 0; }
    else if (r->refs & (FXF_LINK | FXF_LINK_REQ)) { if (lf != (link != FX_NONE) || ((r->refs & FXF_LINK_REQ) && !lf)) return 0; }
    else if (lf || link != FX_NONE) return 0;
    if (e->u.fx.id == 370 && (t == 3 || t == 5 || t == 6 || t == 7) && !lf) return 0;   /* lmode 1: reads its link's nodes */
    allow = 0x1 | 0x80 | r->flg;
    if (!(e->u.fx.flg & 0x1) || (e->u.fx.flg & ~allow)) return 0;
    /* textures and models: the owner's (txp, mlw), or none */
    if (r->refs & FXF_TXP) { if (e->u.fx.stflg != 0x20 || e->u.fx.txp < 0 || e->u.fx.tex_id < 0) return 0; }
    else if (e->u.fx.stflg || e->u.fx.txp != -1 || e->u.fx.tex_id != -1) return 0;
    if ((r->refs & FXF_MLW) ? e->u.fx.mlw < 0 : e->u.fx.mlw != -1) return 0;
    if ((e->u.fx.txp >= 0 || e->u.fx.mlw >= 0) && own == FX_NONE) return 0;
    if ((r->refs & FXF_EXP0) ? ex == FX_NONE : ex != FX_NONE) return 0;
    if (e->u.fx.id == 11 && !(e->u.fx.mdlver ? (t == 1 || t == 2 || t == 5) : (t <= 2 || t == 10 || t == 11))) return 0;   /* the others leave its UV table unset */
    if (e->u.fx.id == 108 && e->u.fx.lkono != 10 && e->u.fx.lkono != 12 && e->u.fx.lkono != 13 && e->u.fx.lkono != 15 && e->u.fx.lkono != 20) return 0;
    return 1;
}
/* objects: slots 4..31 (sys->obwp, coop_glue.c OBJ_N); fins also keeps their angles within the int they become */
static int atr_ok(const AtrRec* r, int kind, int ix) { return fins(r->p, 6) && (kind != ATR_WAL || r->type < 8) && (kind != ATR_OBJ || (ix >= 4 && ix < 32)); }

/* EV_BRSEED: n bytes all 0 (unknown), or each one of `set` (the receiver decodes the seed string too) */
static int zero_or(const char* b, int n, const char* set)
{
    int k, z = 1;
    for (k = 0; k < n; k++) z &= !b[k];
    for (k = 0; k < n && !z; k++)
        if (!b[k] || !strchr(set, b[k])) return 0;
    return 1;
}

/* events only the host sends (it runs the session and the enemies), and those only a guest sends */
/* (protocol 46: EV_PLY_HIT, EV_GRAB and EV_UNGRAB go either way - with OPT2_OWNERSHIP the guest runs enemies too;
   coop_glue.c takes them from the guest only under that rule) */
static int host_only(int t) { return t == EV_BLOB || t == EV_BOXFIX || t == EV_EOPTS || t == EV_SKIP || t == EV_BRSEED || t == EV_CASTOFF; }
static int guest_only(int t) { return t == EV_GONE || t == EV_OWN; }

int coop_check_event(const Event* e, int from_host)
{
    int k;
    if (e->type == 0 || e->type >= EV_TYPES) return COOP_BAD_TYPE;
    if ((from_host && guest_only(e->type)) || (!from_host && host_only(e->type))) return COOP_BAD_DIR;
    switch (e->type) {
    case EV_ENE_HIT:
        if (e->idx >= COOP_ENE_SLOTS) return COOP_BAD_FIELD;
        for (k = 0; k < 6; k++) {
            if (e->u.eh.jnt[k] < -1 || e->u.eh.jnt[k] >= 64) return COOP_BAD_FIELD;
            if (e->u.eh.dam[k] < 0 || e->u.eh.dam[k] > 10000 || (e->u.eh.jnt[k] < 0 && e->u.eh.dam[k])) return COOP_BAD_FIELD;
        }
        if ((e->u.eh.flg & ~(4u | 0xE00u | COOP_HIT_AIM | COOP_HIT_PRECISE)) || (e->u.eh.flg2 & ~4u) || (e->u.eh.comb_flg & ~0x70u)) return COOP_BAD_FIELD;
        if (e->u.eh.wpnr_no < 0 || e->u.eh.wpnr_no >= 23 || e->u.eh.djnt_no < -1 || e->u.eh.djnt_no >= 64) return COOP_BAD_FIELD;
        return fins(e->u.eh.dp, 3) && fins(e->u.eh.dv, 3) ? COOP_OK : COOP_BAD_FLOAT;
    case EV_PLY_HIT:
        if (e->u.ph.hp_delta < 0 || e->u.ph.hp_delta > 1000) return COOP_BAD_FIELD;
        return fins(e->u.ph.dv, 3) ? COOP_OK : COOP_BAD_FLOAT;
    case EV_BLOB:
    case EV_ESTATE:
        return xfer_fields_ok(e) && e->u.blob.pad <= (e->type == EV_BLOB ? 2 : 0) ? COOP_OK : COOP_BAD_FIELD;   /* EV_BLOB: 1 saved items, 2 a shared continue */
    case EV_OWN:   /* the guest's own items, in chunks of 64 (only those that changed); pad 1 ends a change */
        return xfer_fields_ok(e) && e->u.blob.total == COOP_OWN_SIZE && e->u.blob.pad <= 1 ? COOP_OK : COOP_BAD_FIELD;
    case EV_FLAGS:
        if (e->u.fl.n < 1 || e->u.fl.n > 6) return COOP_BAD_COUNT;
        for (k = 0; k < e->u.fl.n; k++)
            if (e->u.fl.word[k] >= 120 + ENEMY_XDEAD_WORDS) return COOP_BAD_FIELD;   /* (120..: the extras' dead flags) */
        return COOP_OK;
    case EV_PICKUP:   /* undo: the host settles a spot both took (shared loot) */
        if (e->u.pk.undo && !from_host) return COOP_BAD_DIR;
        return e->u.pk.item < 156 && e->u.pk.bit >= -1 && e->u.pk.bit < 512 && e->u.pk.undo <= 1 && (!e->u.pk.undo || e->u.pk.bit >= 0) ? COOP_OK : COOP_BAD_FIELD;
    case EV_LOST:
        return e->u.lost.item > 0 && e->u.lost.item < 156 ? COOP_OK : COOP_BAD_FIELD;
    case EV_GRAB:
    case EV_UNGRAB:
    case EV_ENEOWN:   /* state: 1 with its state, 0 without (EV_ENEOWN: refused, run it on), 2 EV_UNGRAB: the grab is over and
                         the runner keeps it / EV_ENEOWN: taken back; EV_ENEOWN 3 the guest ran it alone and keeps running it
                         (the hand-in, guest -> host), 4 the host shows it as the guest's until the guest hands it in (host -> guest) */
        if (e->idx >= COOP_ENE_SLOTS || e->u.gr.state > (e->type == EV_GRAB ? 1 : e->type == EV_ENEOWN ? 4 : 2) || e->u.gr.pset > 1) return COOP_BAD_FIELD;
        if (e->type == EV_ENEOWN && (e->u.gr.state == 3 ? from_host : e->u.gr.state == 4 && !from_host)) return COOP_BAD_DIR;
        if (e->u.gr.mdl < -1 || e->u.gr.mdl >= 16 || e->u.gr.cmdl < -1 || e->u.gr.cmdl >= 16) return COOP_BAD_FIELD;
        for (k = 0; k < 2; k++) {
            int m = k ? e->u.gr.cmnw : e->u.gr.mnw;
            if (m != MNW_ROOM && m != MNW_KEEP && (m < 0 || m >= COOP_MNW_MAX)) return COOP_BAD_FIELD;
        }
        return COOP_OK;
    case EV_TRIG:
        if (!coop_flag_bits(e->u.tr.set) || e->u.tr.bit >= coop_flag_bits(e->u.tr.set) || e->u.tr.val > 1) return COOP_BAD_FIELD;
        if (e->u.tr.n < 1 || e->u.tr.n > COOP_TRIG_OPS || e->u.tr.first % 24 || e->u.tr.first >= e->u.tr.n) return COOP_BAD_COUNT;
        if (e->u.tr.last != (e->u.tr.first + 24 >= e->u.tr.n)) return COOP_BAD_FIELD;
        return COOP_OK;
    case EV_TRADE:
        if (e->u.td.op < TD_OFFER || e->u.td.op > TD_QUERY || e->u.td.yes > 1) return COOP_BAD_FIELD;
        return e->u.td.op != TD_OFFER || coop_item_word_ok(e->u.td.entry) ? COOP_OK : COOP_BAD_FIELD;
    case EV_TSCREEN:
        if (e->u.ts.op < TS_REQ || e->u.ts.op > TS_DECLINE || e->u.ts.ready > 1 || e->u.ts.yes > 1 || e->u.ts.why >= TS_WHY_MAX) return COOP_BAD_FIELD;
        if ((from_host && e->u.ts.op == TS_DONE) || (!from_host && (e->u.ts.op == TS_SWAP || e->u.ts.op == TS_QUERY))) return COOP_BAD_DIR;
        if ((e->u.ts.cell > 9 && e->u.ts.cell != TSC_NONE) || (e->u.ts.pcell > 9 && e->u.ts.pcell != TSC_NONE) ||
            (e->u.ts.eq > 9 && e->u.ts.eq != TSC_NONE)) return COOP_BAD_FIELD;
        if (e->u.ts.op == TS_STATE && e->u.ts.slots != 8 && e->u.ts.slots != 10) return COOP_BAD_FIELD;
        if (e->u.ts.op == TS_WANT && (e->u.ts.cell == TSC_NONE) != !e->u.ts.take) return COOP_BAD_FIELD;   /* a cell and its item, or neither */
        if (e->u.ts.op == TS_DECLINE && (e->u.ts.cell == TSC_NONE || !e->u.ts.take)) return COOP_BAD_FIELD;
        if ((e->u.ts.give && !coop_item_word_ok(e->u.ts.give)) || (e->u.ts.take && !coop_item_word_ok(e->u.ts.take))) return COOP_BAD_FIELD;
        for (k = 0; k < 10; k++)
            if (e->u.ts.inv[k] && !coop_item_word_ok(e->u.ts.inv[k])) return COOP_BAD_FIELD;
        return COOP_OK;
    case EV_BOX:
    case EV_BOXFIX:
        if (e->u.bx.nrm + e->u.bx.nadd < 1 || e->u.bx.nrm + e->u.bx.nadd > 16) return COOP_BAD_COUNT;
        for (k = 0; k < e->u.bx.nrm + e->u.bx.nadd; k++)
            if (!coop_item_word_ok(e->u.bx.w[k])) return COOP_BAD_FIELD;
        return COOP_OK;
    case EV_SPAWN:
        if (e->idx >= COOP_ENE_SLOTS || e->u.sp.room > 9999 || (e->u.sp.dead && (e->u.sp.dead < ENEMY_XDEAD_BASE || e->u.sp.dead >= ENEMY_XDEAD_BASE + ENEMY_XDEAD_WORDS * 32))) return COOP_BAD_FIELD;
        if (e->u.sp.pad == 1 || e->u.sp.pad > SPAWN_NOTE_XDONE || (e->u.sp.pad == SPAWN_NOTE_RAND && e->idx >= 128) ||
            (e->u.sp.pad == SPAWN_NOTE_DONE && e->u.sp.rec > 128) ||
            (e->u.sp.pad == SPAWN_NOTE_XDONE && e->u.sp.rec > ENEMY_EXTRA_MAX && e->u.sp.rec != SPAWN_XDONE_NONE)) return COOP_BAD_FIELD;   /* (1: before protocol 52,
                                                                                                      SPAWN_NOTE_KEPT) */
        if (from_host && e->u.sp.pad == SPAWN_NOTE_XDONE) return COOP_BAD_DIR;   /* (protocol 53: the guest's randomizer results and its own extras) */
        return fin(e->u.sp.px) && fin(e->u.sp.py) && fin(e->u.sp.pz) ? COOP_OK : COOP_BAD_FLOAT;
    case EV_EOPTS:
        for (k = 0; k < ENEMY_RATE_N; k++)
            if (e->u.eo.rate[k] > ENEMY_RATE_MAX) return COOP_BAD_FIELD;
        return e->u.eo.extra >= 0 && e->u.eo.extra <= ENEMY_EXTRA_MAX && (unsigned)e->u.eo.place <= 1 && (unsigned)e->u.eo.rand <= 1 && e->u.eo.seed >= 0 &&
               (unsigned)e->u.eo.variety <= 2 && (unsigned)e->u.eo.events <= 1 && (unsigned)e->u.eo.cast <= 1 && (unsigned)e->u.eo.cast_all <= 2 &&
               (unsigned)e->u.eo.cast_assign <= 1 && (unsigned)e->u.eo.cast_imports <= 1 && (unsigned)e->u.eo.voices <= 1 && (unsigned)e->u.eo.types <= ENEMY_TYPES_MAX && (unsigned)e->u.eo.cast_enemy <= 3 ? COOP_OK : COOP_BAD_FIELD;
    case EV_FX:   /* (a burst: Event.idx is not read) */
        if (e->u.fx.id == FX_BURST) {   /* counts and times bounded (the effect loops over them), floats finite */
            const uint32_t* w = e->u.fxr.w;
            if (e->u.fxr.nw != FX_RAW_WORDS || w[0] < 1 || w[0] > 32 || w[4] > 600 || w[7] > 600) return COOP_BAD_FIELD;
            return fins((const float*)&w[1], 3) && fins((const float*)&w[5], 2) && fins((const float*)&w[8], 7) && fins((const float*)&w[17], 1) ? COOP_OK : COOP_BAD_FLOAT;
        }
        if (e->u.fx.id == FX_ARM)
            return e->idx < COOP_ENE_SLOTS ? (fins(e->u.fxa.len, 4) && fabsf(e->u.fxa.len[3]) < 1000 ? COOP_OK : COOP_BAD_FLOAT) : COOP_BAD_FIELD;
        if (e->u.fx.id == FX_REACT)
            return e->idx < COOP_ENE_SLOTS && e->u.fxk.rg >= 1 && e->u.fxk.rg < FX_REACT_REGIONS && !e->u.fxk.pad ?
                   (fins(e->u.fxk.d, 3) && fabsf(e->u.fxk.d[0]) <= 1.01f && fabsf(e->u.fxk.d[1]) <= 1.01f && fabsf(e->u.fxk.d[2]) <= 1.01f ? COOP_OK : COOP_BAD_FLOAT) : COOP_BAD_FIELD;
        if (e->u.fx.id == FX_PARTICLE)
            return e->u.fxp.typ < FX_PTCL_TYPES && e->u.fxp.mk2 <= 1 && e->u.fxp.lnk < 64 && e->u.fxp.refs <= FXR_IDX &&
                   fx_ref(coop_fx_get(e->idx, e->u.fxp.refs, FXR_IDX)) ? (fins(e->u.fxp.off, 3) && fins(e->u.fxp.dir, 3) ? COOP_OK : COOP_BAD_FLOAT) : COOP_BAD_FIELD;
        if (e->u.fx.id >= 400 || e->u.fx.lkono >= 64 || (e->u.fx.refs & ~(FXR_IDX | FXR_OWNER | FXR_EXP0)) || !fx_ref(coop_fx_get(e->idx, e->u.fx.refs, FXR_IDX)) ||
            !fx_ref(coop_fx_get(e->u.fx.owner, e->u.fx.refs, FXR_OWNER)) || !fx_ref(coop_fx_get(e->u.fx.exp0, e->u.fx.refs, FXR_EXP0))) return COOP_BAD_FIELD;
        if (e->u.fx.txp < -1 || e->u.fx.txp >= 16 || e->u.fx.mlw < -1 || e->u.fx.mlw >= 16) return COOP_BAD_FIELD;
        if (!fx_entry_ok(e)) return COOP_BAD_FIELD;   /* only the effects co-op sends, each as its creators make it */
        if (e->u.fx.id == 108 && (fabsf(e->u.fx.p[0]) > 10000 || fabsf(e->u.fx.p[2]) > 10000)) return COOP_BAD_FIELD;   /* its ripple map index (apply_fx: near the water) */
        return fins(e->u.fx.p, 3) && fins(e->u.fx.o, 3) && fins(e->u.fx.n, 3) ? COOP_OK : COOP_BAD_FLOAT;
    case EV_GRANT:
        if (e->u.gt.item < 1 || e->u.gt.item >= 156 || e->u.gt.flr_no < 0 || e->u.gt.flr_no >= 64 || e->u.gt.count > 999 ||
            e->stg > 9 || e->rom > 99) return COOP_BAD_FIELD;
        if (!fins(e->u.gt.p, 3)) return COOP_BAD_FLOAT;
        for (k = 0; k < 3; k++)   /* kept in the save region as 1/100 units in 16 bits */
            if (fabsf(e->u.gt.p[k]) > 320) return COOP_BAD_FIELD;
        return COOP_OK;
    case EV_SKIP:   /* the host's decision, in a room that exists in a save */
        return e->u.sk.op == SKIP_GO && e->stg <= 9 && e->rom <= 99 ? COOP_OK : COOP_BAD_FIELD;
    case EV_BRSEED:   /* "R840-XXXX-..." in BioRand's alphabet (upper case), the digest in lower-case hex */
        if (!zero_or(e->u.br.str, COOP_BR_STR, "0123456789ABCEFGHJKLMNPQRTUVWXYZ-") || !zero_or(e->u.br.dig, COOP_BR_DIG, "0123456789abcdef")) return COOP_BAD_FIELD;
        return !e->u.br.str[0] || (e->u.br.str[0] == 'R' && e->u.br.str[4] == '-' && e->u.br.str[9] == '-') ? COOP_OK : COOP_BAD_FIELD;
    case EV_CASTOFF:   /* a chunk of the bit list, in place */
        return e->u.co.n >= 1 && e->u.co.n <= COOP_CASTOFF_CHUNK && !(e->u.co.at % COOP_CASTOFF_CHUNK) && e->u.co.at + e->u.co.n <= COOP_CASTOFF_MAX &&
               !e->u.co.pad ? COOP_OK : COOP_BAD_FIELD;
    case EV_GONE:
        if (e->u.gn.n < 1 || e->u.gn.n > COOP_GONE_MAX || e->idx > 9) return COOP_BAD_COUNT;
        for (k = 0; k < e->u.gn.n; k++)
            if (e->u.gn.slot[k] >= COOP_ENE_SLOTS) return COOP_BAD_FIELD;
        return COOP_OK;
    case EV_ATR:
        if (e->u.at.kind > ATR_OBJ || e->u.at.n < 1 || e->u.at.n > 2) return COOP_BAD_COUNT;
        for (k = 0; k < e->u.at.n; k++)
            if (!atr_ok(&e->u.at.r[k], e->u.at.kind, e->u.at.ix[k])) return COOP_BAD_FIELD;
        return COOP_OK;
    }
    return COOP_BAD_TYPE;
}

int coop_check_all(const void* buf, int n, int from_host, int* nev, int* nene, int* ext)
{
    const unsigned char* b = buf;
    Event e;
    int r = coop_check_dgram(buf, n, nev, nene, ext), k, at;
    if (r) return r;
    for (k = 0; k < *nev; k++) {   /* copies: the buffer need not be aligned */
        memcpy(&e, b + sizeof(Pose) + k * sizeof(Event), sizeof(e));
        if ((r = coop_check_event(&e, from_host))) return r;
    }
    at = (int)(sizeof(Pose) + *nev * sizeof(Event));   /* the snapshots' values are checked as they are decoded (coop_snaprx_take) */
    return *nene ? coop_snap_check(b + at, n - at - *ext * (int)sizeof(PoseExt), *nene) : COOP_OK;
}

/* ---------------------------------------------------------------- event stream */
void coop_ring_reset(CoopRing* r) { r->head = r->next = 1; r->overflow = 0; }

int coop_ring_push(CoopRing* r, Event* e)
{
    if (r->overflow || r->next - r->head >= COOP_RING) { r->overflow = 1; return 0; }
    e->id = r->next;
    r->ev[r->next++ % COOP_RING] = *e;
    return 1;
}

void coop_ring_ack(CoopRing* r, uint32_t ack)
{
    if ((int32_t)(ack - r->head) >= 0 && (int32_t)(ack - r->next) < 0) r->head = ack + 1;
}

const Event* coop_ring_get(const CoopRing* r, uint32_t id)
{
    return (int32_t)(id - r->head) >= 0 && (int32_t)(id - r->next) < 0 ? &r->ev[id % COOP_RING] : NULL;
}

/* ---------------------------------------------------------------- direct sessions (coop_proto.h) */
static int guard_find(const CoopGuard* g, uint32_t ip)
{
    int i;
    for (i = 0; i < COOP_GUARD_N; i++)
        if (g->e[i].fails && g->e[i].ip == ip) return i;
    return -1;
}

int coop_guard_wait(const CoopGuard* g, uint32_t ip, uint32_t now)
{
    int i = guard_find(g, ip);
    return i < 0 || (int32_t)(g->e[i].until - now) <= 0 ? 0 : (int)(g->e[i].until - now);
}

uint32_t coop_guard_fail(CoopGuard* g, uint32_t ip, uint32_t now)
{
    int i = guard_find(g, ip), k;
    uint32_t w = COOP_GUARD_MIN;
    if (i >= 0 && now - g->e[i].last > COOP_GUARD_FORGET) g->e[i].fails = 0;   /* long ago: a fresh start */
    if (i < 0) {   /* a free entry, else the one whose last failure is oldest */
        for (i = 0, k = 1; k < COOP_GUARD_N && g->e[i].fails; k++)
            if (!g->e[k].fails || now - g->e[k].last > now - g->e[i].last) i = k;
        memset(&g->e[i], 0, sizeof(g->e[i]));
        g->e[i].ip = ip;
    }
    if (g->e[i].fails < 0xFFFF) g->e[i].fails++;
    for (k = 1; k < g->e[i].fails && w < COOP_GUARD_MAX; k++) w *= 2;
    if (w > COOP_GUARD_MAX) w = COOP_GUARD_MAX;
    g->e[i].last = now;
    g->e[i].until = now + w;
    return w;
}

int coop_guard_take(CoopGuard* g, uint32_t now)
{
    uint32_t dt;
    if (!g->tok_init) { g->tok_init = 1; g->tok = COOP_HS_RATE * 1000; g->tok_ms = now; }
    dt = now - g->tok_ms;
    g->tok_ms = now;
    g->tok += (int32_t)(dt > 1000 ? 1000 : dt) * COOP_HS_RATE;   /* COOP_HS_RATE tokens a second, in thousandths */
    if (g->tok > COOP_HS_RATE * 1000) g->tok = COOP_HS_RATE * 1000;
    if (g->tok < 1000) return 0;
    g->tok -= 1000;
    return 1;
}

/* the code's generator: a point of the curve no one knows the logarithm of (Elligator 2 of its hash) */
static void hs_gen(const char* code, uint8_t g[32])
{
    uint8_t h[32];
    crypto_blake2b_ctx c;
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, (const uint8_t*)"recvx direct v1 G", 17);
    crypto_blake2b_update(&c, (const uint8_t*)code, strlen(code));
    crypto_blake2b_final(&c, h);
    crypto_elligator_map(g, h);
    crypto_wipe(h, sizeof(h));
}

/* the session's keys from our secret, the other side's value and the transcript (both values, the code) */
static int hs_keys(CoopHs* h, int host, const uint8_t sk[32], const uint8_t other[32], const char* code)
{
    static const uint8_t zero[32];
    uint8_t k[32], base[64];
    crypto_blake2b_ctx c;
    crypto_x25519(k, sk, other);
    if (!crypto_verify32(k, zero)) { crypto_wipe(k, sizeof(k)); return 0; }   /* a low-order value: no secret */
    crypto_blake2b_init(&c, 64);
    crypto_blake2b_update(&c, (const uint8_t*)"recvx direct v1 K", 17);
    crypto_blake2b_update(&c, k, 32);
    crypto_blake2b_update(&c, h->yj, 32);
    crypto_blake2b_update(&c, h->yh, 32);
    crypto_blake2b_update(&c, (const uint8_t*)code, strlen(code));
    crypto_blake2b_final(&c, base);
    memset(&h->ses, 0, sizeof(h->ses));
    memcpy(host ? h->ses.tx : h->ses.rx, base, 32);   /* host to joiner */
    memcpy(host ? h->ses.rx : h->ses.tx, base + 32, 32);
    crypto_wipe(k, sizeof(k));
    crypto_wipe(base, sizeof(base));
    return 1;
}

static int hs_is(const CoopHs* h, uint32_t ip, uint16_t port) { return h->state != HS_FREE && h->ip == ip && h->port == port; }
static void hs_drop(CoopHs* h) { crypto_wipe(h, sizeof(*h)); }

void coop_direct_init(CoopDirect* d, int host, const char* code)
{
    CoopGuard g = d->guard;   /* the failures stay (a host may stop and host again in one run) */
    crypto_wipe(d, sizeof(*d));
    d->guard = g;
    d->host = host;
    snprintf(d->code, sizeof(d->code), "%s", code);
    hs_gen(d->code, d->g);
}

void coop_direct_wipe(CoopDirect* d)
{
    CoopGuard g = d->guard;
    crypto_wipe(d, sizeof(*d));
    d->guard = g;
}

static void hello(CoopHello* m, int type, const uint8_t key[32])
{
    memset(m, 0, sizeof(*m));
    m->magic = COOP_MAGIC;
    m->version = COOP_VERSION;
    m->type = (uint8_t)type;
    memcpy(m->key, key, 32);
}

int coop_direct_hello(CoopDirect* d, uint32_t ip, uint16_t port, CoopHello* out)
{
    CoopHs* h = &d->hs[0];
    if (!hs_is(h, ip, port)) {
        hs_drop(h);
        if (!rdv_random(h->sk, 32)) return 0;
        crypto_x25519(h->yj, h->sk, d->g);
        h->ip = ip; h->port = port; h->state = HS_HELLO;
    }
    hello(out, PKT_HELLO, h->yj);
    return 1;
}

/* host: an entry for a new handshake - a free one, an unproven one past its time, else the oldest
   unproven one (a flood of handshakes pushes out only its own), else the oldest */
static CoopHs* hs_slot(CoopDirect* d, uint32_t now)
{
    CoopHs* best = NULL;
    int i;
    for (i = 0; i < COOP_HS_N; i++) {
        CoopHs* h = &d->hs[i];
        if (h->state == HS_FREE || (h->state != HS_PROVEN && now - h->t > COOP_HS_TTL)) { best = h; break; }
        if (!best || (best->state == HS_PROVEN && h->state != HS_PROVEN) ||
            ((best->state == HS_PROVEN) == (h->state == HS_PROVEN) && now - h->t > now - best->t)) best = h;
    }
    hs_drop(best);
    return best;
}

int coop_direct_in(CoopDirect* d, uint32_t ip, uint16_t port, uint32_t now, uint8_t* buf, int n, int cap, CoopHello* reply, int* reply_n)
{
    uint32_t magic;
    uint16_t version;
    CoopHello m;
    int i, k;
    *reply_n = 0;
    if (n < 8) return 0;
    memcpy(&magic, buf, 4);
    if (magic == RDV_E2E_MAGIC) {
        uint8_t tmp[RDV_DGRAM_MAX + 64];
        int unproven = 0;
        if (n - RDV_E2E_OVER > (int)sizeof(tmp)) return 0;
        for (i = 0; i < COOP_HS_N; i++) {
            CoopHs* h = &d->hs[i];
            if (h->state < HS_KEYED || !hs_is(h, ip, port)) continue;
            if ((k = rdv_open(&h->ses, tmp, sizeof(tmp), buf, n)) >= 0) {
                if (k > cap) return 0;
                if (h->state != HS_PROVEN) {   /* the code is proven: older keys of this address go */
                    int j;
                    h->state = HS_PROVEN;
                    for (j = 0; j < COOP_HS_N; j++)
                        if (j != i && hs_is(&d->hs[j], ip, port)) hs_drop(&d->hs[j]);
                }
                h->t = now;
                memcpy(buf, tmp, k);
                crypto_wipe(tmp, k);
                return k;
            }
            if (h->state == HS_KEYED && d->host) unproven = 1;
        }
        if (!unproven) return 0;   /* forged, replayed, or keys we do not have (any more) */
        for (i = 0; i < COOP_HS_N; i++)   /* the guess is spent: another one needs another handshake */
            if (d->hs[i].state == HS_KEYED && hs_is(&d->hs[i], ip, port)) hs_drop(&d->hs[i]);
        coop_guard_fail(&d->guard, ip, now);
        return COOP_DIN_WRONG;
    }
    if (magic != COOP_MAGIC) return 0;
    memcpy(&version, buf + 4, 2);
    if (version != COOP_VERSION || buf[6] == PKT_REJECT) return COOP_DIN_RAW;
    if (n != (int)sizeof(m)) return 0;
    memcpy(&m, buf, sizeof(m));
    for (i = k = 0; i < (int)sizeof(m.zero); i++) k |= m.zero[i];
    if (k || m.zero8) return 0;
    if (m.type == PKT_HELLO && d->host) {
        uint8_t sk[32];
        CoopHs* h;
        if (coop_guard_wait(&d->guard, ip, now)) return 0;   /* it failed: it waits (no answer at all) */
        for (i = 0; i < COOP_HS_N; i++)   /* a repeat (our answer was lost): the same answer */
            if (d->hs[i].state >= HS_KEYED && hs_is(&d->hs[i], ip, port) && !memcmp(d->hs[i].yj, m.key, 32)) {
                hello(reply, PKT_HELLO_ACK, d->hs[i].yh);
                *reply_n = sizeof(*reply);
                return 0;
            }
        if (!coop_guard_take(&d->guard, now) || !rdv_random(sk, 32)) return 0;
        for (i = 0; i < COOP_HS_N; i++)   /* one open guess per address: a new handshake replaces its unproven one (any port) */
            if (d->hs[i].state == HS_KEYED && d->hs[i].ip == ip) hs_drop(&d->hs[i]);
        h = hs_slot(d, now);
        h->ip = ip; h->port = port;
        memcpy(h->yj, m.key, 32);
        crypto_x25519(h->yh, sk, d->g);
        if (!hs_keys(h, 1, sk, h->yj, d->code)) { hs_drop(h); crypto_wipe(sk, sizeof(sk)); coop_guard_fail(&d->guard, ip, now); return 0; }
        crypto_wipe(sk, sizeof(sk));
        h->state = HS_KEYED;
        h->t = now;
        hello(reply, PKT_HELLO_ACK, h->yh);
        *reply_n = sizeof(*reply);
        return 0;
    }
    if (m.type == PKT_HELLO_ACK && !d->host) {
        CoopHs* h = &d->hs[0];
        if (h->state != HS_HELLO || !hs_is(h, ip, port)) return 0;
        memcpy(h->yh, m.key, 32);
        if (!hs_keys(h, 0, h->sk, h->yh, d->code)) return 0;   /* unusable: the real answer may still come */
        crypto_wipe(h->sk, sizeof(h->sk));
        h->state = HS_KEYED;
        h->t = now;
    }
    return 0;
}

static CoopHs* hs_out(CoopDirect* d, uint32_t ip, uint16_t port)   /* the keys we send with: the joiner's, the host's proven ones */
{
    int i;
    for (i = 0; i < COOP_HS_N; i++)
        if (hs_is(&d->hs[i], ip, port) && d->hs[i].state >= (d->host ? HS_PROVEN : HS_KEYED)) return &d->hs[i];
    return NULL;
}
int coop_direct_ready(const CoopDirect* d, uint32_t ip, uint16_t port) { return hs_out((CoopDirect*)d, ip, port) != NULL; }

int coop_direct_seal(CoopDirect* d, uint32_t ip, uint16_t port, const void* in, int n, uint8_t* out, int cap)
{
    CoopHs* h = hs_out(d, ip, port);
    return h ? rdv_seal(&h->ses, out, cap, in, n) : 0;
}

void coop_direct_forget(CoopDirect* d, uint32_t ip, uint16_t port)
{
    int i;
    for (i = 0; i < COOP_HS_N; i++)
        if (hs_is(&d->hs[i], ip, port)) hs_drop(&d->hs[i]);
}

int coop_parse_join(const char* s, char* host, int host_cap, int* port, char code[COOP_CODE_LEN + 1])
{
    char buf[160], *sep, *colon;
    code[0] = 0;
    snprintf(buf, sizeof(buf), "%s", s);
    sep = strpbrk(buf, "# ");
    if (sep) {
        *sep++ = 0;
        while (*sep == ' ' || *sep == '#') sep++;
        if (*sep && !rdv_code_norm(sep, code)) strcpy(code, "?");   /* given but malformed: matches no host */
    }
    colon = strrchr(buf, ':');
    if (colon) *colon++ = 0;
    *port = colon ? atoi(colon) : 7140;
    snprintf(host, host_cap, "%s", buf);
    return host[0] && *port >= 1 && *port <= 65535;
}

/* ---------------------------------------------------------------- transfers */
void coop_xfer_init(CoopXfer* x, void* buf, int cap) { memset(x, 0, sizeof(*x)); x->buf = buf; x->cap = cap; }
void coop_xfer_reset(CoopXfer* x) { x->tid = 0; x->total = 0; x->done = 0; memset(x->have, 0, sizeof(x->have)); }

int coop_xfer_chunk(CoopXfer* x, int tid, int total, int off, int len, const uint8_t* data)
{
    int k, chunks;
    if (!tid || total <= 0 || total > x->cap || total > 64 * (int)sizeof(x->have) || len <= 0 || len > 64 ||
        off < 0 || off % 64 || off + len > total || (len != 64 && off + len != total))
        return -1;
    if (x->tid == tid && x->total != total) return -1;   /* one transfer has one size */
    if (x->tid != tid) { coop_xfer_reset(x); x->tid = (uint16_t)tid; x->total = (uint16_t)total; }
    memcpy(x->buf + off, data, len);
    x->have[off / 64] = 1;
    chunks = (total + 63) / 64;
    for (k = 0; k < chunks && x->have[k]; k++) ;
    x->done = k == chunks;
    return x->done;
}

/* ---------------------------------------------------------------- room scripts */
static const unsigned char op_len[256] = {   /* pc/tools/rdx.py OP_LEN */
    2, 2, 2, 2, 6, 6, 4, 6, 4, 4, 4, 4, 4, 4, 6, 2, 2, 2, 2, 4, 4, 4, 2, 8, 2, 8, 2, 6, 6, 4, 6, 4,
    4, 6, 4, 6, 4, 10, 2, 2, 2, 2, 4, 2, 4, 2, 2, 4, 4, 2, 12, 8, 12, 4, 6, 2, 6, 4, 2, 6, 4, 4, 4, 4,
    8, 2, 4, 4, 6, 2, 8, 10, 6, 10, 8, 4, 6, 4, 14, 6, 2, 12, 12, 12, 12, 2, 6, 2, 14, 12, 4, 2, 2, 6, 4, 2,
    6, 2, 6, 2, 1, 4, 1, 1, 6, 1, 2, 2, 8, 22, 22, 12, 2, 2, 14, 22, 4, 4, 4, 6, 14, 6, 2, 10, 10, 4, 4, 6,
    4, 2, 4, 2, 8, 4, 16, 2, 10, 2, 2, 12, 6, 2, 4, 4, 2, 4, 2, 2, 4, 4, 2, 8, 12, 4, 4, 6, 2, 4, 2, 2,
    6, 2, 4, 12, 4, 4, 4, 4, 10, 2, 10, 6, 6, 4, 2, 4, 22, 6, 4, 8, 4, 4, 2, 2, 2, 10, 16, 2, 2, 2, 6, 4,
    28, 2, 6, 4, 8, 2, 2, 4, 8, 4, 2, 2, 4, 2, 2, 2, 4, 10, 10, 2, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 4, 3, 4, 2, 2, 1, 1, 2,
};
/* controller ops 100/102/103/105: sub-command -> length incl. the sub-command byte (rdx.py
   CTRL_LEN); an unknown sub-command is not consumed */
static const unsigned char ctrl100[][2] = { { 0, 1 }, { 7, 7 }, { 12, 5 }, { 13, 5 }, { 52, 1 }, { 128, 1 }, { 129, 11 }, { 130, 1 }, { 131, 3 }, { 132, 5 }, { 137, 3 }, { 139, 1 }, { 140, 7 }, { 141, 5 }, { 142, 1 }, { 143, 3 }, { 145, 5 }, { 150, 3 } };
static const unsigned char ctrl102[][2] = { { 1, 1 }, { 20, 3 } };
static const unsigned char ctrl103[][2] = { { 0, 1 }, { 128, 1 }, { 139, 1 }, { 143, 5 }, { 144, 1 }, { 146, 7 }, { 147, 7 }, { 148, 3 }, { 149, 3 } };
static const unsigned char ctrl105[][2] = { { 0, 1 }, { 2, 2 }, { 3, 2 }, { 4, 2 }, { 5, 5 }, { 6, 5 }, { 7, 7 }, { 8, 7 }, { 9, 5 }, { 10, 5 }, { 11, 5 }, { 12, 5 }, { 13, 5 }, { 14, 3 }, { 15, 5 }, { 16, 5 }, { 17, 2 }, { 18, 2 }, { 19, 2 }, { 21, 3 }, { 22, 5 }, { 23, 3 }, { 24, 7 }, { 25, 7 }, { 26, 9 }, { 27, 7 }, { 28, 2 }, { 29, 2 }, { 30, 2 }, { 31, 2 }, { 32, 2 }, { 33, 2 }, { 34, 9 }, { 35, 7 }, { 36, 1 }, { 38, 9 }, { 40, 3 }, { 41, 3 }, { 42, 3 }, { 43, 4 }, { 44, 4 }, { 45, 4 }, { 46, 3 }, { 47, 1 }, { 48, 3 }, { 49, 3 }, { 50, 3 }, { 51, 5 }, { 52, 3 }, { 53, 3 }, { 132, 5 }, { 140, 7 }, { 141, 5 } };

static int sub_len(const unsigned char (*t)[2], int n, int sub)
{
    int i;
    for (i = 0; i < n; i++)
        if (t[i][0] == sub) return t[i][1];
    return 0;
}

int coop_insn_len_raw(const uint8_t* p)
{
    switch (p[0]) {
    case 100: return 1 + sub_len(ctrl100, sizeof(ctrl100) / 2, p[1]);
    case 102: return 1 + sub_len(ctrl102, sizeof(ctrl102) / 2, p[1]);
    case 103: return 1 + sub_len(ctrl103, sizeof(ctrl103) / 2, p[1]);
    case 105: return 1 + sub_len(ctrl105, sizeof(ctrl105) / 2, p[1]);
    }
    return op_len[p[0]];
}

/* Ck CmpB CmpW UseItemCheck PlItemCheck AdxCk AdxTimeCk DieEventCk ArmsItemCheck FrameCheck
   PlItemBlockCk PadCheck TFrameCheck CamCheck AreaSearchObj SubMapBusyCk PlayerPoisonCk RandTest
   ZombieUpDieCk MovieCk MovieTimeCk ObjDposCk PlyDposCk */
int coop_op_cond(int op)
{
    switch (op) {
    case 4: case 6: case 7: case 16: case 17: case 27: case 30: case 33: case 38: case 56: case 77: case 93: case 96:
    case 98: case 114: case 129: case 138: case 140: case 142: case 155: case 172: case 177: case 190: return 1;
    }
    return 0;
}
int coop_op_flow(int op) { return op <= 3 || op >= 243; }

/* The actions retail world event blocks run (a census of the then-parts of all 394 blocks in the 205
   rooms), less those that act on the replaying player's own game: a door (SetDoorCall, RoomCaseNo),
   the retry point, its weapon or hand (ArmsItemChange, PlyHandChange), its poison, the player
   controller (PlCtr), a LoadWork of the player, and Sv other than the examined trigger (20). */
int coop_op_replay(const uint8_t* p)
{
    switch (p[0]) {
    case 5: case 10: case 11: case 12: case 18: case 19: case 20: case 22: case 24: case 31: case 32: case 36: case 37:
    case 42: case 49: case 54: case 57: case 66: case 67: case 68: case 76: case 87: case 92: case 99: case 102:
    case 103: case 105: case 106: case 107: case 123: case 124: case 134: case 147: case 158: case 171: case 182:
    case 188: case 203: case 206:
        return 1;
    case 8: return p[1] == 20;           /* Sv etc_idx */
    case 101: return p[1] != 0;          /* LoadWork of an enemy, object, item, effect: not the player */
    }
    return 0;
}

int coop_trig_ops_ok(const uint8_t* d, int size, int then0, int then1, const uint16_t* ops, int n)
{
    int o = then0, i = 0, len;
    if (then0 < 0 || then1 > size || then0 > then1) return 0;
    while (i < n && o < then1) {
        if (ops[i] < o) return 0;        /* not a start, or out of order */
        if (ops[i] == o) i++;
        if (o + 2 > size || (len = coop_insn_len_raw(d + o)) <= 0) return 0;
        o += len;
    }
    return i == n;
}

/* the actions in [o, lim): enemy works and their control, enemy records, room flags, records off, events doing the
   same (one level); 1 = only those, *en: an enemy was touched */
static int wake_ops(const uint8_t* d, int size, int o, int lim, const uint32_t* evs, const uint32_t* eve, int nev, int depth, int* en)
{
    int n, work = 0;
    for (; o < lim; o += n) {
        int op = d[o];
        if (o + 2 > size || (n = coop_insn_len_raw(d + o)) <= 0 || o + n > size) return 0;
        if (op == 101) { if (d[o + 1] != 1) return 0; work = *en = 1; }                 /* LoadWork of an enemy */
        else if (op == 103 || op == 105) { if (!work) return 0; }                       /* SubCtr, CommonCtr: on it */
        else if (op == 36) { if (d[o + 1] != 1) return 0; *en = 1; }                     /* InitModelSet of an enemy record */
        else if (op == 5) {                                                              /* Set: rm_flg, cb_flg, sp_flg 27 */
            if (d[o + 1] != 4 && d[o + 1] != 10 && !(d[o + 1] == 6 && ((d[o + 2] | d[o + 3] << 8) & 0x3FF) == 27)) return 0;
        } else if (op == 20) {                                                           /* EvtOn: an event doing the same */
            int e = d[o + 3];
            if (depth || e >= nev || !wake_ops(d, size, (int)evs[e], (int)eve[e], evs, eve, nev, 1, en)) return 0;
        } else if (depth && (op == 255 || op == 0)) return 1;                            /* EvtEnd, End */
        else if (op != 12 && !coop_op_cond(op) && !coop_op_flow(op)) return 0;           /* (12 FlrAtariSet) */
    }
    return 1;
}

int coop_wake_scan(const uint8_t* d, int size, int scd1, int scd1_end, const uint32_t* evs, const uint32_t* eve, int nev,
                   CoopWake* out, int max)
{
    struct { uint8_t etc, ix, bad; } s[32];
    int ns = 0, ne = 0, o, n, i, k, ends[32];
    if (scd1 < 0 || scd1_end > size) return 0;
    for (o = scd1; o < scd1_end; o += n) {
        if (o + 2 > size || (n = coop_insn_len_raw(d + o)) <= 0 || o + n > size) break;
        while (ne && ends[ne - 1] <= o) ne--;
        if (d[o] == 1) {   /* IfelCk n: the then-part ends at o + 2 + n (an ElseCk there opens the else part) */
            int then1 = o + 2 + d[o + 1], p = o + 2, en = 0, ok, m;
            if (then1 > size || then1 < o + 4) return 0;
            if (ne < 32) ends[ne++] = d[then1 - 2] == 2 ? then1 - 2 + d[then1 - 1] : then1;
            while (p < then1 && coop_op_cond(d[p]) && (m = coop_insn_len_raw(d + p)) > 0) p += m;
            ok = d[then1 - 2] != 2 && wake_ops(d, size, p, then1, evs, eve, nev, 0, &en) && en;
            for (k = o + 2; k < p; k += coop_insn_len_raw(d + k)) {
                int bit = (d[k + 2] | d[k + 3] << 8) & 0x3FF;
                if (d[k] != 4 || d[k + 1] != 10 || (bit != 22 && bit != 23)) continue;
                for (i = 0; i < ns && !(s[i].etc == (bit == 23) && s[i].ix == d[k + 4]); i++) {}
                if (i == ns) { if (ns == 32) continue; s[ns].etc = bit == 23; s[ns].ix = d[k + 4]; s[ns++].bad = 0; }
                if (!ok || d[k + 5]) s[i].bad = 1;   /* (a test of "not there" too) */
            }
        }
        if (!ne && d[o] == 0) break;   /* scd1's End */
    }
    for (i = k = 0; i < ns && k < max; i++)
        if (!s[i].bad) { out[k].etc = s[i].etc; out[k++].ix = s[i].ix; }
    return k;
}

int coop_atr_set(const uint8_t* p, AtrSet* out)
{
    if (p[0] != 37 && p[0] != 123 && p[0] != 124) return 0;   /* EtcAtariSet2 WalAtariSet2 FlrAtariSet2 (event.c) */
    out->kind = p[0] == 37 ? ATR_ETC : p[0] == 123 ? ATR_WAL : ATR_FLR;
    out->ix = p[1];
    out->attr = p[2] | p[3] << 8;
    memcpy(out->prm, p + 4, 4);
    out->type = p[8];
    return 1;
}

void coop_atr_merge(AtrRec* rec, const AtrRec* in, int kind, int ix, const AtrSet* sets, int n)
{
    int i;
    rec->flg = in->flg;
    memcpy(rec->p, in->p, sizeof(rec->p));
    for (i = 0; i < n; i++)
        if (sets[i].kind == kind && sets[i].ix == ix && sets[i].type == in->type && sets[i].attr == in->attr && !memcmp(sets[i].prm, in->prm, 4)) {
            rec->type = in->type;
            rec->attr = in->attr;
            memcpy(rec->prm, in->prm, 4);
            return;
        }
}

int coop_pick_target(int cur, const CoopTgtIn* in)
{
    int oth = !cur;
    cur = !!cur;
    if (in->commit == COOP_TGT_HOLD || in->busy[oth]) return cur;
    if (in->commit == COOP_TGT_ATTACK && in->d[cur] <= COOP_TGT_KEEP * COOP_TGT_KEEP) return cur;
    if (in->busy[cur]) return in->d[oth] < COOP_TGT_REACH * COOP_TGT_REACH ? oth : cur;
    if (in->front[cur] && in->d[oth] * 4.0f >= in->d[cur]) return cur;
    if (in->front[oth] && in->d[oth] < in->d[cur] * 2.25f) return oth;
    return in->d[oth] * 1.5f < in->d[cur] ? oth : cur;
}

int coop_ene_modes_ok(int id, int type, int m0, int m1, int m2, int upper)
{
    /* per mode0 1..4 (Move, Nage, Damage, Die) the size of the table mode2 indexes (255: none); Init (0) and the event
       (5) index none by mode2. brain: mode0 1 with mode1 1 runs the Brain first, its table by mode2 too */
    static const struct { uint8_t id, types, m2[4], brain; } t[] = {
        { 4, 3, { 13, 2, 3, 1 }, 12 },      /* en04: Move/Nage/Damage/DieType[3], BrainMode2[12], MoveMode2[13] ... */
        { 5, 8, { 18, 255, 14, 9 }, 0 },    /* en05: BrainType[8] (mode1 1, by type), MoveMode2[18], DamageMode2[14], DeadMode2[9] */
        { 9, 11, { 15, 2, 11, 1 }, 12 },    /* en09: MoveType[11], BrainMode2[12], MoveMode2[15] ... */
        { 26, 255, { 17, 1, 16, 1 }, 0 },   /* en26: no type table */
        /* protocol 54, the hand-in's types (docs/COOP.md "The hand-in") */
        { 3, 2, { 20, 2, 13, 6 }, 0 },      /* en03: BrainType[3] by type (2 for its cocoon flag), action_table[2][6] by type; Move[20] ... */
        { 6, 1, { 11, 1, 2, 5 }, 0 },       /* en06: BrainType[1] by type */
        { 7, 2, { 9, 2, 255, 7 }, 0 },      /* en07: BrainType[2] by type; its Damage has no table */
        { 8, 255, { 3, 255, 255, 255 }, 0 },   /* en08 */
        { 10, 255, { 3, 255, 255, 255 }, 0 },  /* en10 */
        { 11, 5, { 10, 255, 255, 255 }, 0 },   /* en11: its aim by type 0..4 */
        { 15, 255, { 255, 255, 255, 255 }, 0 },   /* en15: Move_func[5] by mode1 (coop_glue.c ene_modes_ok) */
        { 19, 3, { 255, 255, 255, 255 }, 0 },  /* en19: BrainType[3], CheckDmgLvl[3] ... by type; Mode0[4] is NULL (below) */
        { 20, 255, { 8, 255, 255, 255 }, 0 },  /* en20 */
        { 21, 255, { 9, 1, 255, 1 }, 0 },      /* en21: its Damage has no table */
        { 22, 1, { 7, 255, 3, 2 }, 6 },        /* en22: InitType[1] ... by type, BrainMode2[6] */
        { 23, 2, { 13, 1, 8, 4 }, 0 },         /* en23: BrainType[2] by type */
        { 24, 1, { 6, 255, 255, 255 }, 0 },    /* en24: BrainType[1] by type; mode0 6 (in its mother) is past the check's 5 */
        { 25, 255, { 4, 255, 255, 255 }, 0 },  /* en25 */
        { 71, 255, { 255, 255, 255, 255 }, 0 },   /* en71: all empty (its flipbook: coop_glue.c) */
    };
    unsigned int k;
    for (k = 0; k < sizeof(t) / sizeof(t[0]) && t[k].id != id; k++) {}
    if (id != 1 && k == sizeof(t) / sizeof(t[0])) return 1;
    if (m0 < 0 || m0 > 5 || m1 < 0 || m2 < 0 || type < 0 || (id == 19 && m0 == 4)) return 0;
    if (id == 1) {   /* en01: the *Type[16]; types 2 3 6 7 9 (MVType02, DGType02) add 10 to mode2 when moving or hit */
        static const uint8_t n[4] = { 17, 4, 17, 4 };
        static const uint32_t nul[4] = { 1u << 13, 1u << 3, 3u << 12, 0 };   /* MoveMode2W[13], NageMode2W[3], DamageMode2W[12..13] */
        int i = m2 + ((m0 == 1 || m0 == 3) && type < 16 && (0x2CC >> type & 1) ? 10 : 0);
        if (type >= 16) return 0;
        if (m0 < 1 || m0 > 4) return 1;
        return i < n[m0 - 1] && (upper || !(nul[m0 - 1] >> i & 1)) && (m0 != 4 || m2 || m1 < 3);   /* (DD00: mtn_tbl[9][2] by mode1 + 6) */
    }
    return (t[k].types == 255 || type < t[k].types) &&
           (m0 < 1 || m0 > 4 || ((t[k].m2[m0 - 1] == 255 || m2 < t[k].m2[m0 - 1]) && (m0 != 1 || m1 != 1 || !t[k].brain || m2 < t[k].brain)));
}

int coop_atk_note(CoopAtk* a, unsigned int now)
{
    if (!a->n || now - a->t0 > COOP_LOOP_WIN) { a->t0 = now; a->n = 0; }
    if (++a->n < COOP_LOOP_N) return 0;
    a->n = 0;
    a->until = now + COOP_LOOP_COOL;
    return 1;
}

int coop_atk_cooling(const CoopAtk* a, unsigned int now) { return (int)(a->until - now) > 0; }

/* ---------------------------------------------------------------- playout (coop_proto.h) */
static int cmpf(const void* a, const void* b) { float x = *(const float*)a, y = *(const float*)b; return (x > y) - (x < y); }

void coop_playout_reset(CoopPlayout* po) { memset(po, 0, sizeof(*po)); }

void coop_playout_arrive(CoopPlayout* po, uint32_t seq, uint64_t arr_us, int exact)
{
    double o = (double)arr_us - seq * COOP_TICK_US;
    int k = seq % COOP_PO_WIN;
    if (!seq) return;
    if (!po->have || (int32_t)(seq - po->newest) > 100000 || (int32_t)(po->newest - seq) > 100000) {   /* the first, or another counter */
        coop_playout_reset(po);
        po->have = 1; po->base = o; po->newest = seq;
    }
    o -= po->base;
    if ((int32_t)(po->newest - seq) >= COOP_PO_WIN) return;   /* too old to count */
    if ((int32_t)(seq - po->newest) > 0) {
        /* every new frame over two frames above the margin, 8 in a row: the sender's grid moved (it stalled and
           re-phased): measured again from these */
        if (exact) po->high = po->started && o > po->delay + 2 * COOP_TICK_US ? po->high + 1 : 0;   /* (a read-time stamp says nothing) */
        if (po->high >= 8) {
            int j;
            for (j = 0; j < COOP_PO_WIN; j++)
                if ((int32_t)(seq - po->f[j].seq) >= 8) po->f[j].seq = 0;
            po->high = 0;
            po->delay = o;
        }
        po->newest = seq;
    }
    if (po->f[k].seq != seq) { po->f[k].seq = seq; po->f[k].lo = (float)o; po->f[k].hi = -1e30f; }
    if (o < po->f[k].lo) po->f[k].lo = (float)o;
    if (exact && o > po->f[k].hi) po->f[k].hi = (float)o;
}

int coop_playout_frame(CoopPlayout* po, uint64_t now_us, uint32_t* P)
{
    float hi[COOP_PO_WIN];
    double fast = 1e30, want, pf, d;
    int j, n = 0;
    if (!po->have) return 0;
    for (j = 0; j < COOP_PO_WIN; j++) {
        if (!po->f[j].seq || (int32_t)(po->newest - po->f[j].seq) >= COOP_PO_WIN) continue;
        if (po->f[j].lo < fast) fast = po->f[j].lo;
        if (po->f[j].hi > -1e29f) hi[n++] = po->f[j].hi;
    }
    if (fast > 1e29) { *P = po->P; return po->started; }
    if (n) {
        qsort(hi, n, sizeof(hi[0]), cmpf);
        want = hi[(int)(0.98 * (n - 1))] + COOP_PO_PAD_US;
    } else want = fast + COOP_PO_PAD_US;
    if (want > fast + COOP_PO_MAX * COOP_TICK_US) want = fast + COOP_PO_MAX * COOP_TICK_US;
    if (want < fast) want = fast;
    po->fast = fast;
    if (!po->started || want > po->delay) po->delay = want;                                     /* up at once */
    else po->delay = want < po->delay - COOP_PO_FALL_US ? po->delay - COOP_PO_FALL_US : want;   /* down slowly */
    if (po->delay > fast + COOP_PO_MAX * COOP_TICK_US) po->delay = fast + COOP_PO_MAX * COOP_TICK_US;   /* the cap holds for a re-anchor too */
    pf = ((double)now_us - (po->base + po->delay)) / COOP_TICK_US;   /* frame k is due once now >= base + k * T + delay */
    if (!po->started) {
        if (pf < 1) return 0;
        po->P = (uint32_t)floor(pf + COOP_PO_EARLY);   /* (a frame due within the dead band's early side: now) */
        po->started = 1;
    } else {
        d = pf - (po->P + 1.0);
        if (d >= 4.0) po->P = (uint32_t)floor(pf + COOP_PO_EARLY);          /* far behind (we stalled): to the due frame */
        else if (d >= 1.0 + COOP_PO_LATE) po->P += 2;          /* a frame behind: catch up by one */
        else if (d >= -COOP_PO_EARLY) po->P += 1;              /* in step */
    }                                                          /* (else early: the same frame again) */
    if ((int32_t)(po->P - (po->newest + COOP_PO_EXTRA)) > 0) po->P = po->newest + COOP_PO_EXTRA;   /* not past the data */
    *P = po->P;
    return 1;
}

void coop_hist_put(CoopSnapHist* h, uint32_t seq, const EneSnap* s)
{
    int k = seq % COOP_HIST;
    if (!seq || (h->seq[k] && (int32_t)(h->seq[k] - seq) > 0)) return;   /* older than what that place holds */
    h->seq[k] = seq;
    h->s[k] = *s;
}

static int32_t ang_at(int32_t a0, int32_t a1, double t)   /* from a0 towards a1 (the short way round) by t (also beyond) */
{
    int32_t r = a0 + (int32_t)lround((int16_t)(uint16_t)(a1 - a0) * t);
    return a0 >= 0 && a0 < 0x10000 ? r & 0xFFFF : r;
}

/* frm_no at sender frame P from a's anchor (not wrapped: the caller knows the motion's length), 0: none */
static int anchor_at(const EneSnap* a, const EneSnap* s0, uint32_t P, int32_t* f)
{
    int64_t v;
    if (!a->mtn_seq || (a->mtn_md & 0x1) || a->mtn_no != s0->mtn_no || a->mnw != s0->mnw || (int32_t)(P - a->mtn_seq) < 0) return 0;
    v = (int64_t)a->mtn_frm0 + (int64_t)(int32_t)(P - a->mtn_seq) * a->mtn_add;
    if (v > INT32_MAX || v < INT32_MIN) return 0;
    *f = (int32_t)v;
    return 1;
}

int coop_hist_at(const CoopSnapHist* h, uint32_t P, uint32_t applied, EneSnap* out, uint32_t* from)
{
    int j, b = -1, o = -1, n = -1;   /* the newest at or before P, the one before that, the first after P */
    for (j = 0; j < COOP_HIST; j++) {
        if (!h->seq[j]) continue;
        if ((int32_t)(h->seq[j] - P) <= 0) { if (b < 0 || (int32_t)(h->seq[j] - h->seq[b]) > 0) b = j; }
        else if (n < 0 || (int32_t)(h->seq[j] - h->seq[n]) < 0) n = j;
    }
    if (b < 0) return COOP_AT_NONE;
    for (j = 0; j < COOP_HIST; j++)
        if (h->seq[j] && (int32_t)(h->seq[j] - h->seq[b]) < 0 && (o < 0 || (int32_t)(h->seq[j] - h->seq[o]) > 0)) o = j;
    *out = h->s[b];
    *from = h->seq[b];
    if (h->seq[b] == P) return COOP_AT_EXACT;
    if ((int32_t)(P - h->seq[b]) > COOP_PO_EXTRA) return (int32_t)(h->seq[b] - applied) > 0 ? COOP_AT_OLD : COOP_AT_NONE;
    {
        const EneSnap* s0 = &h->s[b];
        const EneSnap* s1 = NULL;
        const EneSnap* a;
        int interp = n >= 0 && (int32_t)(h->seq[n] - h->seq[b]) <= 2 * COOP_PO_EXTRA;
        int32_t f;
        double gap, t;
        if (interp || (o >= 0 && (int32_t)(h->seq[b] - h->seq[o]) <= COOP_PO_EXTRA)) {   /* (else one snapshot: its place held) */
            s1 = interp ? &h->s[n] : &h->s[o];
            gap = interp ? (double)(h->seq[n] - h->seq[b]) : (double)(h->seq[b] - h->seq[o]);
            t = (double)(P - h->seq[b]) / gap * (interp ? 1 : -1);   /* P's place from s0 towards s1 (the older one: backwards) */
            if (fabsf(s1->px - s0->px) + fabsf(s1->py - s0->py) + fabsf(s1->pz - s0->pz) < 20.0 * gap) {   /* (2 m a frame: a jump, not a move) */
                out->px = (float)(s0->px + (s1->px - s0->px) * t);
                out->py = (float)(s0->py + (s1->py - s0->py) * t);
                out->pz = (float)(s0->pz + (s1->pz - s0->pz) * t);
                out->ax = ang_at(s0->ax, s1->ax, t); out->ay = ang_at(s0->ay, s1->ay, t); out->az = ang_at(s0->az, s1->az, t);
            }
            if (s1->mtn_no == s0->mtn_no && s1->mnw == s0->mnw && s1->mdl == s0->mdl && (interp ? s1->frm_no >= s0->frm_no : s1->frm_no <= s0->frm_no))
                out->frm_no = s0->frm_no + (int32_t)lround((double)(s1->frm_no - s0->frm_no) * t);   /* one motion, going forward */
        }
        /* the motion's anchor (protocol 47) gives the frame exactly while the sender kept it: s0's, or from the next
           snapshot's anchor frame on (between) that one's, in the same motion */
        a = interp && s1->mtn_seq && (int32_t)(P - s1->mtn_seq) >= 0 ? s1 : s0;
        if (anchor_at(a, s0, P, &f)) out->frm_no = f;
        return interp ? COOP_AT_INTERP : COOP_AT_EXTRAP;
    }
}
