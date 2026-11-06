/*
 * IOP side of Capcom's TSNDDRV sound driver, high-level emulated.
 *
 * Protocol (EE side: ps2_snddrv.c / ps2_sg_sd.c):
 *   server 1 (TSNDGET): state queries; 18 -> address of the SND_STATUS block,
 *                       19 -> address of the 64-byte IOP buffer table.
 *   server 0 (TSNDDRV): byte-encoded command stream (see sending_req()).
 * Sound data arrives through SIF DMA into a staging buffer: HD (bank headers),
 * BD (ADPCM bodies, 48 KB double-buffered packets) and SQ (MIDI sequences).
 * The driver acknowledges header uploads with a byte checksum in SND_STATUS.
 * Data commands run here at once; the sound commands go in order to the mixer's queue, which
 * plays each at the time it was sent (snd_synth.c "command queue").
 *
 * Playback (TQ sound effects, SQ MIDI ports, HD/BD synthesis) is in snd_synth.c.
 */
#include "../ps2_platform.h"
#include "snd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void* iop_ptr(uint32_t iop_addr);

#define IOP_TABLE   0x1F0000u   /* 64-byte address table read with sceSifGetOtherData */
#define IOP_STATUS  0x1E0000u   /* SND_STATUS block polled by the EE */
#define IOP_HD_AREA 0x100000u
#define IOP_SQ_AREA 0x140000u
#define IOP_STAGING 0x160000u   /* iop_data_buff: HD/BD/SQ staging (2 x 200 KB) */
#define BD_PACKET2  204800u     /* offset of the second BD packet buffer */

typedef struct {
    uint16_t se_info[6];
    uint16_t midi_info;
    int16_t  port_info[8];
    int16_t  midi_sum[4];
    int16_t  se_sum[5];
    uint16_t dummy[9];
} __attribute__((packed)) SndStatus;

SndBanks g_snd_banks;
static int initialised;

static SndStatus* status(void) { return (SndStatus*)iop_ptr(IOP_STATUS); }

static void tsnd_init(void)
{
    uint32_t* t = iop_ptr(IOP_TABLE);
    memset(t, 0, 64);
    t[0] = IOP_HD_AREA; t[1] = IOP_SQ_AREA; t[2] = IOP_STAGING;
    memset(status(), 0, sizeof(SndStatus));
    initialised = 1;
}

static int16_t byte_sum(const uint8_t* p, uint32_t n)
{
    int16_t s = 0;
    for (uint32_t i = 0; i < n; i++) s = (int16_t)(s + (int8_t)p[i]);
    return s;
}

static void store(SndBlob* b, const void* src, uint32_t ofs, uint32_t size)
{
    if (ofs + size > b->cap) {
        uint32_t cap = (ofs + size + 0xFFFF) & ~0xFFFFu;
        b->data = realloc(b->data, cap);
        memset(b->data + b->cap, 0, cap - b->cap);
        b->cap = cap;
    }
    memcpy(b->data + ofs, src, size);
    if (ofs + size > b->size) b->size = ofs + size;
}

static int tsnd_get_state(u_int cmd, int data)
{
    (void)data;
    if (!initialised) tsnd_init();
    switch (cmd) {
    case 16: return 0;          /* set transfer buffer size (no reply used) */
    case 18: return IOP_STATUS;
    case 19: return IOP_TABLE;
    }
    { static int seen[64]; if (cmd < 64 && !seen[cmd]++) host_log("tsnddrv: GetState %u (unimplemented)\n", cmd); }
    return 0;
}

/* length of one encoded command (mirrors sending_req() on the EE) */
static int cmd_length(int cd)
{
    switch (cd & 0xF0) {
    case 0x00: return 4 + ((cd & 1) ? 1 : 0) + ((cd & 2) ? 1 : 0) + ((cd & 4) ? 2 : 0);
    case 0x10: return cd == 17 ? 3 : 1;
    case 0x20:
        if (cd >= 34 && cd <= 37) return 3;
        if (cd == 38) return 4;
        if (cd == 32) return 5;
        if (cd == 39 || cd == 40 || cd == 41 || cd == 44 || cd == 45) return 8;
        return 2;
    case 0x40:
        if (cd == 71 || cd == 72 || cd == 73 || cd == 74 || cd == 65 || cd == 66) return 2;
        if (cd == 75) return 3;
        if (cd == 69 || cd == 76) return 4;
        if (cd == 68) return 6;
        if (cd == 77 || cd == 78) return 3;
        if (cd == 79) return 6;
        return 1;
    case 0x50: case 0x60:
        if (cd >= 81 && cd <= 84) return 8;
        return 2;
    }
    return -1;
}

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void trace_cmd(const uint8_t* c, int len)
{
    static int on = -1;
    if (on < 0) on = getenv("RECVX_TRACE_SND") != NULL;
    if (!on) return;
    printf("tsnd:");
    for (int i = 0; i < len; i++) printf(" %02x", c[i]);
    printf("\n");
}

static void exec_cmd(const uint8_t* c, int len)
{
    int cd = c[0];
    trace_cmd(c, len);
    SndStatus* st = status();
    const uint8_t* staging = iop_ptr(IOP_STAGING);
    /* bank data is taken from the staging buffer now (the EE reuses it and polls the checksum),
       after the commands queued before it */
    if (cd >= 0x28 && cd <= 0x2D) snd_synth_flush();
    switch (cd) {
    case 0x28: { /* HDDataSet: MIDI program bank header */
        int bank = c[1] & 3; uint32_t size = le32(c + 4);
        store(&g_snd_banks.midi_hd[bank], staging, 0, size);
        st->midi_sum[bank] = byte_sum(staging, size);
        snd_synth_bank_changed(SND_BANK_MIDI, bank);
        break;
    }
    case 0x29: { /* HDDataSet2: SE bank header */
        int bank = c[1] % 5; uint32_t size = le32(c + 4);
        store(&g_snd_banks.se_hd[bank], staging, 0, size);
        st->se_sum[bank] = byte_sum(staging, size);
        snd_synth_bank_changed(SND_BANK_SE, bank);
        break;
    }
    case 0x2C: { /* BDDataTrans: body packet -> sound RAM of a bank */
        int port = c[1] & 0x7F, flag = c[1] >> 7;
        uint32_t adrs = c[2] | (c[3] << 8) | ((uint32_t)c[4] << 16);
        uint32_t size = c[5] | (c[6] << 8) | ((uint32_t)c[7] << 16);
        store(&g_snd_banks.bd[port & 15], staging + (flag ? BD_PACKET2 : 0), adrs, size);
        break;
    }
    case 0x2A: case 0x2B: /* BDDataSet: last body packet already moved; bank ready */
        snd_synth_bank_changed(SND_BANK_BODY, c[1] & 15);
        break;
    case 0x2D: { /* SQDataSet: MIDI sequence */
        int port = c[1] & 7; uint32_t size = le32(c + 4);
        store(&g_snd_banks.sq[port], staging, 0, size);
        snd_synth_bank_changed(SND_BANK_SQ, port);
        break;
    }
    default:   /* played by the mixer at the time it was sent (snd_synth.c "command queue") */
        snd_synth_post(c, len);
        break;
    }
}

/* the EE reads the status block (sceSifGetOtherData): the driver's state as it is now */
void snd_iop_status(uint32_t iop_addr)
{
    if (iop_addr != IOP_STATUS || !initialised) return;
    snd_synth_lock();
    for (int port = 0; port < 5; port++) status()->se_info[port] = snd_synth_se_active(port);
    status()->midi_info = snd_synth_midi_active();
    snd_synth_unlock();
}

int iop_rpc_dispatch(u_int server, u_int fno, void* send, int ssize, void* receive, int rsize)
{
    if (!initialised) tsnd_init();
    if (server == 1) {
        int v = tsnd_get_state(fno, ((int*)send)[0]);
        if (rsize >= 4) ((int*)receive)[0] = v;
        return 0;
    }
    if (server == 0) {
        const uint8_t* p = send;
        int i = 0;
        snd_synth_lock();
        snd_synth_update();
        while (i < ssize && p[i] != 0xFF) {
            int len = cmd_length(p[i]);
            if (len <= 0 || i + len > ssize) { host_log("tsnddrv: bad command byte %02x\n", p[i]); break; }
            exec_cmd(p + i, len);
            i += len;
        }
        snd_synth_unlock();
        return 0;
    }
    host_log("sif: RPC to unknown server %u fno %u\n", server, fno);
    return 0;
}

/* ---------------------------------------------------------------- PC: extra enemy banks */
/* Enemies play their sound ids in SE bank 3, which a room's sound pack (RM_SRRC.SPQ) fills with the
   sounds of the room's own types only; ids collide between types (docs/ENEMIES.md "Sounds of added types"). For a type
   spawned into a room that has none of it, the spawner (spawn_glue.c) loads the bank 3 of the type's
   source room here, straight from the disc (MULTSPQ1/2: either half of the game), and its requests
   name the bank in the port byte. On the PS2 the room's banks fill the SPU2's 2 MB; here banks are
   heap blobs, so the limit is the 5 bits of the port byte: 31 banks, released with the room.
   A key 0xFFF0 + n is the player voice bank CORE_00n.SPQ (bank 4, RequestPlayerVoiceSoundBank's
   key): the voice of a character the player appears as (coop_glue.c pc_player_voice). */
static int xb_key[31], xb_n;

static uint8_t* disc_part(const char* name, uint32_t ofs, uint32_t size, uint32_t* fsize)
{
    uint32_t lsn, n;
    uint8_t* b;
    if (!cdvd_find_file(name, &lsn, fsize)) return NULL;
    if (!size) size = *fsize;
    if (ofs > *fsize || size > *fsize - ofs) return NULL;
    n = (ofs % 2048 + size + 2047) / 2048;
    if (!(b = malloc(n * 2048))) return NULL;
    if (!cdvd_read_sectors(lsn + ofs / 2048, n, b)) { free(b); return NULL; }
    memmove(b, b + ofs % 2048, size);
    return b;
}

/* the sound pack of room key `key` (stage*1000 + room*10 + case), or NULL */
static uint8_t* spq_read(int key, uint32_t* size)
{
    for (int disc = 1; disc <= 2; disc++) {
        char name[32];
        uint32_t fsize, i, ofs, sz;
        uint8_t *idx, *toc;
        snprintf(name, sizeof(name), "\\MULTSPQ%d.IDX;1", disc);
        if (!(idx = disc_part(name, 0, 0, &fsize))) continue;
        for (i = 0; i < fsize / 2 && (idx[i * 2] | idx[i * 2 + 1] << 8) != 0xFFFF && (idx[i * 2] | idx[i * 2 + 1] << 8) != key; i++);
        sz = i < fsize / 2 && (idx[i * 2] | idx[i * 2 + 1] << 8) == key;   /* (as sdfunc.c SearchAfsInsideFileId) */
        free(idx);
        if (!sz) continue;
        snprintf(name, sizeof(name), "\\MULTSPQ%d.AFS;1", disc);
        if (!(toc = disc_part(name, 0, 16 + i * 8, &fsize))) return NULL;
        ofs = le32(toc + 8 + i * 8); sz = le32(toc + 12 + i * 8);
        free(toc);
        *size = sz;
        return disc_part(name, ofs, sz, &fsize);
    }
    return NULL;
}

int snd_xbank_load(int key)
{
    uint8_t *spq, *d;
    uint32_t size, o, bank_ofs = 0, bank_size = 0, hd_ofs, hd_size, bd_ofs, bd_size;
    int k, bank = key >= 0xFFF0 ? 4 : 3;
    for (k = 0; k < xb_n; k++) if (xb_key[k] == key) return k + 1;
    if (xb_n >= 31) { host_log("snd: no extra bank for key %d (all 31 in use)\n", key); return 0; }
    if (!(spq = spq_read(key, &size))) return 0;
    for (o = 0; o + 16 <= size && le32(spq + o); o += 16)   /* {Offset, Size, Type, BankNo}...; Type 2 = SE bank */
        if (le32(spq + o + 8) == 2 && (int)le32(spq + o + 12) == bank) { bank_ofs = le32(spq + o); bank_size = le32(spq + o + 4); }
    if (!bank_ofs || bank_ofs > size || bank_size > size - bank_ofs || bank_size < 16) { free(spq); return 0; }
    d = spq + bank_ofs;   /* {header offset, size, body offset, size} (ps2_sg_sd.c sdBankDownload) */
    hd_ofs = le32(d); hd_size = le32(d + 4); bd_ofs = le32(d + 8); bd_size = le32(d + 12);
    if (hd_ofs > bank_size || hd_size > bank_size - hd_ofs || bd_ofs > bank_size || bd_size > bank_size - bd_ofs) { free(spq); return 0; }
    snd_synth_lock();
    g_snd_banks.xhd[xb_n].size = g_snd_banks.xbd[xb_n].size = 0;
    store(&g_snd_banks.xhd[xb_n], d + hd_ofs, 0, hd_size);
    store(&g_snd_banks.xbd[xb_n], d + bd_ofs, 0, bd_size);
    snd_synth_unlock();
    free(spq);
    xb_key[xb_n++] = key;
    if (bank == 4) host_log("snd: extra bank %d = CORE_%03d.SPQ bank 4 (header %u, samples %u bytes)\n", xb_n, key & 15, hd_size, bd_size);
    else host_log("snd: extra bank %d = RM_%04d.SPQ bank 3 (header %u, samples %u bytes)\n", xb_n, key, hd_size, bd_size);
    return xb_n;
}

/* MIDI bank 2 holds the system sounds (CallSystemSe with bit 31: item box lid 0x240, typewriter keys and bell 0x242..0x244),
   but each room's sound pack fills it with the programs that room uses: the lid's and the keys' only in save rooms. The
   extras' item boxes and typewriters and the F2 Save Now stand in any room, so the programs a room's bank 2 lacks are
   taken from RM_1080's (synth key_on). */
void snd_sysbank_load(void)
{
    static int done;
    uint8_t* spq;
    uint32_t size, o, d = 0, hd_ofs, hd_size, bd_ofs, bd_size;
    if (done++) return;
    if (!(spq = spq_read(1080, &size))) { host_log("snd: RM_1080.SPQ not found: no system sounds outside save rooms\n"); return; }
    for (o = 0; o + 16 <= size && le32(spq + o); o += 16)   /* {Offset, Size, Type, BankNo}; Type 1 = MIDI program bank */
        if (le32(spq + o + 8) == 1 && le32(spq + o + 12) == 2 && le32(spq + o) <= size - 16) d = le32(spq + o);
    if (d) {
        hd_ofs = d + le32(spq + d); hd_size = le32(spq + d + 4); bd_ofs = d + le32(spq + d + 8); bd_size = le32(spq + d + 12);
        if (hd_ofs <= size && hd_size <= size - hd_ofs && bd_ofs <= size && bd_size <= size - bd_ofs) {
            snd_synth_lock();
            store(&g_snd_banks.sys_hd, spq + hd_ofs, 0, hd_size);
            store(&g_snd_banks.sys_bd, spq + bd_ofs, 0, bd_size);
            snd_synth_unlock();
            host_log("snd: system sounds bank = RM_1080.SPQ MIDI bank 2 (header %u, samples %u bytes)\n", hd_size, bd_size);
        } else d = 0;
    }
    if (!d) host_log("snd: RM_1080.SPQ has no MIDI bank 2\n");
    free(spq);
}

void snd_xbank_release(void)
{
    if (!xb_n) return;
    snd_synth_lock();
    snd_synth_xbanks_stop();
    for (int k = 0; k < xb_n; k++) g_snd_banks.xhd[k].size = g_snd_banks.xbd[k].size = 0;   /* (buffers kept for reuse) */
    snd_synth_unlock();
    host_log("snd: released %d extra banks\n", xb_n);
    xb_n = 0;
}
