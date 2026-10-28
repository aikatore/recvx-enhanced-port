/*
 * libgraph on the host: display/draw environment setup and image transfers go
 * straight to the GS emulation.
 */
#include "ps2_platform.h"
#include "../gs/gs.h"
#include "../host/biorand.h"
#include "../gs/stream.h"
#include "../host/prof.h"
#include "../host/cheat.h"
#include "../host/coop.h"
#include "../host/tps.h"
#include "../host/lobby.h"
#include "../host/lang.h"
#include <string.h>

static int field;

void sceGsResetPath(void) {}
void sceGsResetGraph(short mode, short inter, short omode, short ffmode)
{
    (void)mode; (void)inter; (void)omode; (void)ffmode;
    gs_reset();
}

int sceGsSyncV(int mode)
{
    (void)mode;
    ps2_wait_vblank();
    field ^= 1;
    return field;
}

int sceGsSyncPath(int mode, u_short timeout) { (void)mode; (void)timeout; return 0; }
int sceGsSyncVCallback(int (*func)(int)) { (void)func; return 0; }

#define AD(v, r) do { *p++ = (v); *p++ = (r); } while (0)

void sceGsSetDefDBuffDc(sceGsDBuffDc* db, short psm, short w, short h, short ztest, short zpsm, short clear)
{
    uint32_t fbw = (uint32_t)(w + 63) / 64;
    uint32_t pages = fbw * (uint32_t)((h + 31) / 32);
    memset(db, 0, sizeof(*db));
    for (int i = 0; i < 2; i++) {
        db->disp[i].pmode.EN1 = 1; db->disp[i].pmode.CRTMD = 1; db->disp[i].pmode.MMOD = 1; db->disp[i].pmode.ALP = 0xFF;
        db->disp[i].smode2.INT = 1; db->disp[i].smode2.FFMD = 0;
        db->disp[i].dispfb.FBP = i ? 0 : pages; db->disp[i].dispfb.FBW = fbw; db->disp[i].dispfb.PSM = psm;
        db->disp[i].display.DX = 636; db->disp[i].display.DY = 50; db->disp[i].display.MAGH = 3;
        db->disp[i].display.DW = (uint32_t)w * 4 - 1; db->disp[i].display.DH = (uint32_t)h - 1;
    }
#define DRAWENV(e, n, fbp_) \
    e.frame##n.FBP = fbp_; e.frame##n.FBW = fbw; e.frame##n.PSM = psm; e.frame##n##addr = SCE_GS_FRAME_##n; \
    e.zbuf##n.ZBP = pages * 2; e.zbuf##n.PSM = zpsm & 0xF; e.zbuf##n##addr = SCE_GS_ZBUF_##n; \
    e.xyoffset##n.OFX = (2048 - w / 2) << 4; e.xyoffset##n.OFY = (2048 - h / 2) << 4; e.xyoffset##n##addr = SCE_GS_XYOFFSET_##n; \
    e.scissor##n.SCAX1 = w - 1; e.scissor##n.SCAY1 = h - 1; e.scissor##n##addr = SCE_GS_SCISSOR_##n; \
    e.prmodecont.AC = 1; e.prmodecontaddr = SCE_GS_PRMODECONT; \
    e.colclamp.CLAMP = 1; e.colclampaddr = SCE_GS_COLCLAMP; \
    e.dthe.DTHE = 0; e.dtheaddr = SCE_GS_DTHE; \
    e.test##n.ZTE = 1; e.test##n.ZTST = ztest; e.test##n##addr = SCE_GS_TEST_##n;
    DRAWENV(db->draw01, 1, 0)
    DRAWENV(db->draw02, 2, 0)
    DRAWENV(db->draw11, 1, pages)
    DRAWENV(db->draw12, 2, pages)
#undef DRAWENV
    {
        sceGsClear* cl[2] = { &db->clear0, &db->clear1 };
        for (int i = 0; i < 2; i++) {
            cl[i]->testa = SCE_GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, 1); cl[i]->testaaddr = SCE_GS_TEST_1;
            cl[i]->prim = SCE_GS_SET_PRIM(6, 0, 0, 0, 0, 0, 0, 0, 0); cl[i]->primaddr = SCE_GS_PRIM;
            cl[i]->rgbaq = 0; cl[i]->rgbaqaddr = SCE_GS_RGBAQ;
            cl[i]->xyz2a = SCE_GS_SET_XYZ((2048 - w / 2) << 4, (2048 - h / 2) << 4, 0); cl[i]->xyz2aaddr = SCE_GS_XYZ2;
            cl[i]->xyz2b = SCE_GS_SET_XYZ((2048 + w / 2) << 4, (2048 + h / 2) << 4, 0); cl[i]->xyz2baddr = SCE_GS_XYZ2;
            cl[i]->testb = SCE_GS_SET_TEST(0, 0, 0, 0, 0, 0, 1, ztest); cl[i]->testbaddr = SCE_GS_TEST_1;
        }
    }
    (void)clear;
}

static void send_ad(const uint64_t* pairs, int n)
{
    for (int i = 0; i < n; i++) gs_write_reg((int)(pairs[i * 2 + 1] & 0xFF), pairs[i * 2]);
}

int sceGsSwapDBuffDc(sceGsDBuffDc* db, int id)
{
    const sceGsDispEnv* d = &db->disp[id & 1];
    /* the display circuit shows DISPFB of the selected environment: the frame is complete
       and goes to the render thread */
    stream_disp(d->dispfb.FBP, d->dispfb.FBW, d->dispfb.PSM);
    prof_frame_end();
    cheat_frame();
    lang_frame();
    coop_frame();
    lobby_frame();
    biorand_frame();
    tps_frame();
    stream_submit();
    /* then the draw environment for the next frame (+ optional clear) */
    if (id & 1) { send_ad((const uint64_t*)&db->draw11, 8); send_ad((const uint64_t*)&db->draw12, 8); send_ad((const uint64_t*)&db->clear1, 6); }
    else        { send_ad((const uint64_t*)&db->draw01, 8); send_ad((const uint64_t*)&db->draw02, 8); send_ad((const uint64_t*)&db->clear0, 6); }
    return field;
}

void sceGsSetDefLoadImage(sceGsLoadImage* lp, short dbp, short dbw, short dpsm, short x, short y, short w, short h)
{
    memset(lp, 0, sizeof(*lp));
    lp->bitbltbuf.DBP = dbp; lp->bitbltbuf.DBW = dbw; lp->bitbltbuf.DPSM = dpsm;
    lp->trxpos.DSAX = x; lp->trxpos.DSAY = y;
    lp->trxreg.RRW = w; lp->trxreg.RRH = h;
}

static uint32_t psm_bytes(int psm, uint32_t px)
{
    switch (psm) {
    case PSMCT32: case PSMZ32: return px * 4;
    case PSMCT24: case PSMZ24: return px * 3;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return px * 2;
    case PSMT8: case PSMT8H: return px;
    default: return (px + 1) / 2;
    }
}

int sceGsExecLoadImage(sceGsLoadImage* lp, u_long128* src)
{
    gs_write_reg(0x50, *(uint64_t*)&lp->bitbltbuf);
    gs_write_reg(0x51, *(uint64_t*)&lp->trxpos);
    gs_write_reg(0x52, *(uint64_t*)&lp->trxreg);
    gs_write_reg(0x53, 0);
    {
        uint32_t n = psm_bytes(lp->bitbltbuf.DPSM, (uint32_t)lp->trxreg.RRW * lp->trxreg.RRH);
        gs_upload_image((const uint8_t*)src, n);
    }
    return 0;
}

void sceGsSetDefStoreImage(sceGsStoreImage* sp, short sbp, short sbw, short spsm, short x, short y, short w, short h)
{
    memset(sp, 0, sizeof(*sp));
    sp->bitbltbuf.SBP = sbp; sp->bitbltbuf.SBW = sbw; sp->bitbltbuf.SPSM = spsm;
    sp->trxpos.SSAX = x; sp->trxpos.SSAY = y;
    sp->trxreg.RRW = w; sp->trxreg.RRH = h;
}

int sceGsExecStoreImage(sceGsStoreImage* sp, u_long128* dst)
{
    gs_write_reg(0x50, *(uint64_t*)&sp->bitbltbuf);
    gs_write_reg(0x51, *(uint64_t*)&sp->trxpos);
    gs_write_reg(0x52, *(uint64_t*)&sp->trxreg);
    gs_write_reg(0x53, 1);
    stream_download(dst, psm_bytes(sp->bitbltbuf.SPSM, (uint32_t)sp->trxreg.RRW * sp->trxreg.RRH));
    return 0;
}
