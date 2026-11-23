/* Co-op: the partner's stand-in (docs/COOP.md "Architecture"): its model, weapons and look loaded into the player-2
 * work, our own mesh when "You appear as" another character, its sounds, voice and lighter, driving it from its pose
 * (coop_control_remote) and drawing it (coop_draw_remote); and the host's look and enemy options sent to the guest
 * (cast choices, Enemy Models, enemy options). Moved out of coop_glue.c; the shared state is in coop_int.h. */
#include "coop_int.h"


static int read_part(int file)   /* into p2file (actor_glue.c afs_read) */
{
    int ok = afs_read(sys->sys_partid, file, p2file, sizeof(p2file));
    if (!ok) host_log("coop: cannot read part file %d\n", file);
    return ok;
}

void release_textures(void)
{
    int i;
    for (i = 0; i < ply2.mdl_n || i < p2_mdl_kept; i++)
        if (ply2.mdl[i].texP) njReleaseTexture(ply2.mdl[i].texP);
    for (i = 0; i < 2; i++)
        if (p2wpn[i].mlwP && p2wpn[i].mlwP != &no_model && p2wpn[i].mlwP->texP) njReleaseTexture(p2wpn[i].mlwP->texP);
    bhGarbageTexture(NULL, 0);
    actor_set(1, -1);
}

void forget(void)
{
    int i, k;
    /* effects that follow the partner's work or use its model or textures (EV_FX: an enemy's effect on the partner,
       FX_SENDER) end with it: the next step or drawing read ply2's model, zeroed here (a model change: "You appear
       as", the character switch) */
    for (i = 0; i < 512; i++) {
        O_WRK* op = &eff[i];
        int ours = (op->flg & 1) && (op->lkwkp == (unsigned char*)&ply2 || op->exp0 == (unsigned char*)&ply2 ||
                                     ((char*)op->mlwP >= (char*)ply2.mdl && (char*)op->mlwP < (char*)(ply2.mdl + 16)));
        for (k = 0; (op->flg & 1) && !ours && k < ply2.mdl_n && k < 16; k++) ours = ply2.mdl[k].texP && op->txp[0] == ply2.mdl[k].texP;
        if (ours) { op->flg = 0; host_log("coop: effect %d on the partner ended with its model\n", op->id); }
    }
    memset(&ply2, 0, sizeof(ply2));
    memset(p2wpn, 0, sizeof(p2wpn));
    memset(p2obj, 0, sizeof(p2obj));
    p2_chr = p2_mfile = p2_wpn = -1;
    p2_mdl_kept = 0;
}

/* A player model from p2file (actor_glue.c player_model_read) */
static void read_player(BH_PWORK* who, unsigned char* mdl, unsigned char* bmt, unsigned char* mth, unsigned char* zmt)
{
    player_model_read(who, p2file, mdl, bmt, mth, zmt);
}

/* Claire's hair (id 1211, simulated by bhObjClpn) or Wesker's piece (1212, rigid) for a model of
   character `chr` (actor_glue.c player_hair); the partner's hair simulates in work of its own */
static void setup_hair(O_WRK* objs, BH_PWORK* who, int chr)
{
    player_hair(objs, who, chr, who != plp ? p2hair_exp0 : NULL, p2hair_exp3);
}

/* Mesh of part file `chr + costume*4` with the motions of part file `mfile` (the sender's own
   character: its motion numbers index that set). A different mesh is loaded over the first
   file's model, its motions into a sink (docs/ACTORS.md "Option A"). chr 4..:
   catalogue model chr - 4 (pc/src/host/actors.h), drawn over part file `mfile`'s model. */
static int load_player(int chr, int costume, int mfile)
{
    int i, ok = 1, mesh = chr >= 4 ? mfile : chr + costume * 4;

    if (p2_chr >= 0) release_textures();
    forget();
    if (!read_part(mfile + 10)) return 0;
    memset(&p2, 0, sizeof(p2));
    read_player(&ply2, p2.plmdl, p2.plbmt, p2.plmth, p2.plzmt);
    if (ply2.mdl_n && mesh != mfile) {
        /* the mesh goes behind the motion file's model, so the sub-models only that file has stay, with their
           textures, as self_load keeps them for us: Claire's 5..7, the moth egg (en27 takes plp->mdl[6..7] and
           plp->skp[7] of the player it is laid on; over the first model they were another file's bytes) */
        BH_PWORK own = ply2;
        int end = 0, keep;
        for (i = 0; i < own.mdl_n; i++)
            if (own.mdl[i].owP && (unsigned char*)(own.mdl[i].owP + own.mdl[i].obj_num) - p2.plmdl > end)
                end = (unsigned char*)(own.mdl[i].owP + own.mdl[i].obj_num) - p2.plmdl;
        end = (end + 63) & ~63;
        memset(ply2.skp, 0, sizeof(ply2.skp));   /* the loader leaves stale skin pointers */
        if (!read_part(mesh + 10)) { release_textures(); forget(); return 0; }
        keep = end + *(unsigned int*)p2file + 8192 <= sizeof(p2.plmdl);   /* (its model block and O_WORKs; the disc's files fit) */
        if (!keep) { release_textures(); end = 0; }
        read_player(&ply2, p2.plmdl + end, p2sink.bmt, p2sink.mth, p2sink.zmt);
        for (i = 0; keep && i < own.mdl_n; i++)
            if (i < ply2.mdl_n) { if (own.mdl[i].texP) njReleaseTexture(own.mdl[i].texP); }
            else { ply2.mdl[i] = own.mdl[i]; ply2.skp[i] = own.skp[i]; ply2.mbp[i] = own.mbp[i]; ply2.txp[i] = own.txp[i]; }
        if (keep) { bhGarbageTexture(NULL, 0); p2_mdl_kept = own.mdl_n; }
        ply2.mnwP = ply2.mnwPb = (MN_WORK*)p2.plmth;
    }
    p2_other_mesh = ply2.mdl_n && mesh != mfile;
    if (p2_other_mesh) save_bones(&ply2.mdl[0], p2_bone);

    for (i = 0; i < ply2.mdl_n; i++) ok &= ply2.mdl[i].owP != NULL;
    if (!ply2.mdl_n || !ok) { host_log("coop: player model %d/%d did not load\n", chr, costume); release_textures(); forget(); return 0; }

    /* the parts of bhSetPlayer the drawing path depends on */
    ply2.flg = 0x119;
    ply2.mdflg = 0x20;
    ply2.exp0 = p2.plexw; ply2.exp1 = p2.plhdw; ply2.exp3 = p2.pletc;
    ply2.mtx = (float(*)[16])ply2.mtxbuf;
    ply2.sx = ply2.sy = ply2.sz = ply2.sxb = ply2.syb = ply2.szb = 1.0f;
    ply2.mtn_tp = (unsigned char*)PlyFlip;
    ply2.mlwP->owP[7].flg |= 0x8;
    ply2.mlwP->owP[11].flg |= 0x8;
    setup_hair(p2obj, &ply2, chr);
    if (chr >= 4) actor_set(1, chr - 4);   /* on failure the partner shows its own character */
    p2_chr = chr;
    p2_costume = costume;
    p2_mfile = mfile;
    host_log("coop: loaded player-2 model %d costume %d (%d parts) with the motions of part file %d\n", chr, costume, ply2.mdl_n, mfile + 10);
    return 1;
}

/* Weapon models and the weapon motion set (motions 100+), also present for "no weapon". */
static int load_weapon(int wpn)
{
    int i;

    p2_wpn = wpn;   /* also on failure: no retry every frame */
    if (wpn < 0 || wpn > 20 || !read_part(wpn + (p2_mfile & 3) * 30 + 20)) return 0;   /* the sender's character's weapon file */
    for (i = 0; i < 2; i++)
        if (!p2wpn[i].mlwP) p2wpn[i].mlwP = &no_model;   /* the loader releases the old weapon's textures */
    for (i = 100; i < MAX_MTN; i++) ply2.mnwPb[i].datP = NULL;

    ply2.mnwP = ply2.mnwPb;   /* the weapon motions go to entries 100+ of the base set */
    player_weapon_read(&ply2, p2wpn, wpn, p2file, p2.wrmdl, p2.wlmdl, p2.plwmt);   /* (actor_glue.c) */
    memset(&no_model, 0, sizeof(no_model));
    return 1;
}

/* ---------------------------------------------------------------- our own model (M7) */
/* "You appear as" also changes our own player's mesh while co-op is on: the chosen part file's
   model is loaded into a buffer of its own (the motions into the sink), so the original model
   data in sys->plmdlp stays intact and is put back by copying the saved ML_WORKs. Our motions,
   weapon files, PlyInfo and voice stay the character's. The original's textures stay loaded
   while the mesh is shown. Sub-models the new file lacks (Claire's 5..7: the moth egg, en27,
   takes mdl[6], skp[7]) keep the original's. When the game reloads the player (game start, load,
   continue, story switch) the model no longer points into our buffer; it is applied again at
   the next frame in control. */
static unsigned char self_mdl[131072] __attribute__((aligned(64)));
static struct {
    ML_WORK mdl[16];
    int* skp[16];
    NJS_CNK_OBJECT* mbp[16];
    NJS_TEXLIST* txp[16];
    int mdl_n, mlw;
} self_orig;
int self_mesh = -1, self_fail = -1, self_tex_gone;
static int self_cine;                   /* self_cine_hair: the character's own head part is up for a cutscene double */
/* What a cutscene leaves on the played character's model, set up for that character: scripts
   re-attach the head part sys->obwp[2] with its offsets (ObjLinkSetPly; Claire's ponytail at
   0, 1.58, 0.77, behind the head: Wesker's sunglasses ended up there) and a few room motions
   carry node positions (bone lengths). For another mesh both are put back every tick. */
static struct { NJS_CNK_OBJECT* objP; unsigned char* lkwkp; int lkono; float lox, loy, loz; } self_part;

static int self_ours(void) { return plp->mdl[0].objP && (unsigned char*)plp->mdl[0].objP >= self_mdl && (unsigned char*)plp->mdl[0].objP < self_mdl + sizeof(self_mdl); }

static void self_release(ML_WORK* mdl, int n)
{
    int i;
    if (self_tex_gone) return;   /* njReleaseTextureAll took them */
    for (i = 0; i < n; i++)
        if (mdl[i].texP) njReleaseTexture(mdl[i].texP);
    bhGarbageTexture(NULL, 0);
}

static void self_put_back(void);

/* back to the character's own mesh (or forget ours when the game has reloaded the player) */
void self_restore(void)
{
    if (self_mesh < 0) return;
    if (self_ours()) {
        self_release(plp->mdl, plp->mdl_n);
        self_put_back();
        setup_hair(sys->obwp, plp, sys->ply_id);
        self_cine = 0;
    }
    /* else the game has loaded the player again: after the game ended (coop_textures_released) or a
       character switch (coop_player_unload put the original back first). The original's texture lists
       were in the buffer the new load overwrote: releasing them crashed (story switch, Battle Game). */
    self_mesh = -1;
}

/* system.c bhSysCallMonitor: the game releases the player's textures and loads another character */
void coop_player_unload(void) { self_restore(); }

/* The player's node matrices (O_WORK) are last tick's pose: bhFixPosition (motions that plant a foot,
   plp->flg 0x40000) moves the player by the difference between them and the new pose. A model swapped
   in must carry that pose, or the player jumps: fresh O_WORKs are zero (to the room's origin, out of
   the map), the original's were last computed when it was swapped out (back to where the player
   stood then, another room's coordinates after a door). Also no blend from the T-pose. */
static void self_carry_pose(const ML_WORK* from)
{
    ML_WORK* to = plp->mlwP;
    int i, n = from->obj_num < to->obj_num ? from->obj_num : to->obj_num;
    if (!from->objP || !to->objP || !from->owP || !to->owP || from->objP == to->objP) return;
    memcpy(to->objP[0].pos, from->objP[0].pos, sizeof(to->objP[0].pos));
    for (i = 0; i < n && i < 22; i++) memcpy(to->objP[i].ang, from->objP[i].ang, sizeof(to->objP[i].ang));
    bhCalcTree((NJS_MATRIX*)plp->mtx, to);
}

/* called with the original model in place (self_mesh < 0) */
static void self_put_back(void)
{
    ML_WORK cur = *plp->mlwP;
    int i;
    for (i = 0; i < 16; i++) { plp->mdl[i] = self_orig.mdl[i]; plp->skp[i] = self_orig.skp[i]; plp->mbp[i] = self_orig.mbp[i]; plp->txp[i] = self_orig.txp[i]; }
    plp->mdl_n = self_orig.mdl_n;
    plp->mlwP = &plp->mdl[self_orig.mlw];
    self_carry_pose(&cur);
}

static int self_load(int mesh)
{
    MN_WORK *mnw = plp->mnwP, *mnwb = plp->mnwPb;
    int i, mlw = plp->mlwP - plp->mdl, ok = 1;
    if (!read_part(mesh + 10)) return 0;
    memcpy(self_orig.mdl, plp->mdl, sizeof(self_orig.mdl));
    memcpy(self_orig.skp, plp->skp, sizeof(self_orig.skp));
    memcpy(self_orig.mbp, plp->mbp, sizeof(self_orig.mbp));
    memcpy(self_orig.txp, plp->txp, sizeof(self_orig.txp));
    self_orig.mdl_n = plp->mdl_n;
    self_orig.mlw = mlw >= 0 && mlw < 16 ? mlw : 0;
    self_tex_gone = 0;
    memset(plp->skp, 0, sizeof(plp->skp));   /* the loader would apply stale skin pointers */
    read_player(plp, self_mdl, p2sink.bmt, p2sink.mth, p2sink.zmt);
    plp->mnwP = mnw; plp->mnwPb = mnwb;
    for (i = 0; i < plp->mdl_n; i++) ok &= plp->mdl[i].owP != NULL;
    if (!plp->mdl_n || !ok) {   /* keep the original */
        self_release(plp->mdl, plp->mdl_n);
        self_put_back();
        return 0;
    }
    for (i = plp->mdl_n; i < self_orig.mdl_n; i++) {   /* sub-models only the original has (moth egg) */
        plp->mdl[i] = self_orig.mdl[i]; plp->skp[i] = self_orig.skp[i]; plp->mbp[i] = self_orig.mbp[i]; plp->txp[i] = self_orig.txp[i];
    }
    plp->mlwP = &plp->mdl[0];
    plp->mlwP->owP[7].flg |= 0x8;   /* as bhSetPlayer */
    plp->mlwP->owP[11].flg |= 0x8;
    self_carry_pose(&self_orig.mdl[self_orig.mlw]);
    setup_hair(sys->obwp, plp, mesh & 3);
    self_cine = 0;
    {
        O_WRK* op = &sys->obwp[2];
        self_part.objP = (op->flg & 0x1) ? op->mdl[0].objP : NULL;
        self_part.lkwkp = op->lkwkp; self_part.lkono = op->lkono;
        self_part.lox = op->lox; self_part.loy = op->loy; self_part.loz = op->loz;
    }
    save_bones(&plp->mdl[0], self_bone);
    self_mesh = mesh;
    host_log("coop: our own model is now part file %d\n", mesh + 10);
    return 1;
}

/* An enemy/NPC model shown for us (pc/glue/actor_glue.c) instead of the player's: the head part
   (ponytail, sunglasses) goes while it is shown, and comes back as bhSetPlayer sets it up. */
void self_actor(int idx)
{
    int was = actor_shown(0);
    if (actor_set(0, idx)) { if (!self_cine) sys->obwp[2].flg = 0; }
    else if (was) { setup_hair(sys->obwp, plp, self_mesh >= 0 ? self_mesh & 3 : sys->ply_id); self_cine = 0; }
    actor_weapon_hands(0, sys->obwp, 2);   /* (the weapon's hand piece back when no imported model is shown) */
}

/* "Swapped model in cutscenes" off (g_appear_cutscene), or the doubles kept for the played character's
   own cutscene model (actor_double_kept): while a cutscene double of the character played acts, sys->obwp[2] is the character's own head part (Claire's ponytail: the scripts link it to the
   double, ObjLinkSet), from its own model; when the double is gone, ours again. Where the script
   linked it (work, node, x offset) is kept. Every tick before the objects follow their links. */
extern ETTY_WORK lkmtab[2];             /* player.c: the head parts (both sub-model 4) */
static void self_cine_hair(void)
{
    O_WRK* op = &sys->obwp[2];
    int mesh = self_mesh >= 0 && self_ours(), want = (!g_appear_cutscene || actor_double_kept()) && (actor_shown(0) || mesh) && actor_double_acting();
    int v = lkmtab[0].mdlver, n = plp->mdl_n, ono = op->lkono, *s = plp->skp[v];
    unsigned char* lk = op->lkwkp;
    float lox = op->lox;
    ML_WORK m;
    if (want == self_cine) return;
    self_cine = want;
    m = plp->mdl[v];
    if (want && mesh) {   /* the head part of the model the game loaded (self_orig) */
        plp->mdl[v] = self_orig.mdl[v]; plp->skp[v] = self_orig.skp[v]; plp->mdl_n = self_orig.mdl_n;
    }
    if (want || mesh) setup_hair(sys->obwp, plp, want ? sys->ply_id : self_mesh & 3);   /* (as bhSetPlayer / self_load) */
    else op->flg = 0;   /* (as self_actor) */
    plp->mdl[v] = m; plp->skp[v] = s; plp->mdl_n = n;
    op->lkwkp = lk; op->lkono = ono; op->lox = lox;
    if (want && op->id == 1211) {   /* the ponytail's work: the player's, as bhObjClpn takes it linked to plp
                                       (linked to the double it took it from sys->memp, under a pending disc read) */
        op->flg |= 0x100000;
        op->exp0 = &sys->pletcp[4096];
        op->exp3 = sys->pletcp;
    }
}

/* every tick after bhControlPlayer (before the objects follow their links) */
static void self_keep(void)
{
    O_WRK* op = &sys->obwp[2];
    self_cine_hair();
    if (actor_shown(0) && !self_cine) op->flg &= ~0x1;   /* also when the game set up the player again (story switch) */
    actor_source(0, plp, self_mesh >= 0 && self_ours() ? self_mesh & 3 : actor_shown(0) ? -1 : sys->ply_id, sys->ply_id, tps_aim_pitch());   /* gender-matched animations */
    actor_pose(0, plp);
    actor_weapon_hands(0, sys->obwp, 2);   /* an imported model: its own hands on the weapon, not the piece's */
    if (self_mesh < 0 || !self_ours()) return;
    keep_bones(&plp->mdl[0], self_bone);
    if (self_part.objP && op->mdl[0].objP == self_part.objP) {
        op->lkwkp = self_part.lkwkp; op->lkono = self_part.lkono;
        op->lox = self_part.lox; op->loy = self_part.loy; op->loz = self_part.loz;
        op->flg |= 0x80;
    }
}

/* "You appear as" for character c (0..3: "As played (cutscene)": its cutscene model): "Character Model", or with
   "Per Character" Claire's or Chris's own (actors.c appear_choice); BioRand's "Randomize Played as" pick of this game
   for that character first (biorand_glue.c: Claire's, Chris's, Steve's) */
static int look_of(int c)
{
    int o = pc_biorand_appear(c);
    return appear_played(o >= 0 ? o : appear_choice(g_coop.chr, c), c);
}
/* ... for the character played now */
int my_chr(void) { return look_of(sys ? sys->ply_id & 3 : 0); }

/* the model (an actor index) for look chr on the doubles of a character whose own part file is own, -1 the game's */
static int look_index(int chr, int own)
{
    int f = appear_file(chr);
    return !chr ? -1 : appear_actor(chr) >= 0 ? appear_actor(chr) : f == own ? -1 : ACTOR_PART + f;
}

/* "Dreamcast Steve" at Cutscenes and Gameplay (docs/IMPORT_DREAMCAST.md): Steve played as himself wears the Dreamcast
   Steve's part file. The mesh the game loaded for the character played (part file - 10; the game's own load of Steve's
   with that setting: ACTOR_DC_PART) and the one it should wear as played. */
int self_loaded(void) { int own = sys->ply_id + (sys->costume & 3) * 4; return own == 2 && actor_part_dc() ? ACTOR_DC_PART : own; }
int self_played(void)
{
    int own = sys->ply_id + (sys->costume & 3) * 4;
    return own == 2 && g_import.dc_steve == DC_STEVE_GAMEPLAY && __atomic_load_n(&actor_dc_part, __ATOMIC_ACQUIRE) ? ACTOR_DC_PART : own;
}

/* cast_glue.c: Claire's or Chris's records (c 0, 1) in the cutscenes of a part they are not played in show the player's
   look for them, as their doubles do while they are, when they have looks of their own (Separate Looks, or this game's
   random looks per character: biorand_glue.c pc_appear_separate); with one look for both they are themselves (the menu
   rework's D20: no double of your look): the model, -1 themselves */
int coop_look_model(int c)
{
    int kept;
    if (!pc_appear_separate()) return -1;
    return actor_double_model(look_index(look_of(c), c), c, &kept);
}

/* at the hand-off */
void self_frame(void)
{
    int own = sys->ply_id + (sys->costume & 3) * 4, want, chr = my_chr();
    if (!plp || !plp->mdl_n) return;
    if (self_mesh >= 0 && !self_ours()) self_restore();   /* the game reloaded the player */
    if (tx_in_game) actor_doubles(look_index(chr, own));   /* in cutscenes the character played is acted by its event double */
    if (!tx_in_game || (sys->cb_flg & 0x1)) return;
    self_actor(appear_actor(chr));
    want = !chr ? self_played() : appear_file(chr);   /* (as played: the Dreamcast Steve's when the setting says so, also when the
        game loaded Steve before it was set) */
    if (want == self_loaded()) want = -1;
    if (want == self_mesh || (want >= 0 && want == self_fail)) return;   /* (-1 is no failure: back to the original) */
    self_restore();
    self_fail = -1;
    if (want >= 0 && !self_load(want)) { self_fail = want; host_log("coop: part file %d cannot be our model\n", want + 10); }
}

void coop_textures_released(void)
{
    forget();   /* njReleaseTextureAll freed them; the model is reloaded when next needed */
    actor_textures_released();
    self_tex_gone = 1;
    self_cine = 0;
    /* our mesh ends with the game too: the character's own model data back in the player work (its
       head part comes with the next game's bhSetPlayer). Left in place, the session's end at the title
       (back from a game, OPEN THE LOBBY or BACK there, F3) put it back itself and set up the head part
       in sys->obwp, NULL there: the crash. */
    if (self_mesh >= 0 && self_ours()) self_put_back();
    self_mesh = -1;
    /* the game ended (bhExitGame: continue, title, load): our player is not in a game until
       coop_control_remote runs again. Set in this frame before the exit, it made the hand-off load
       the appearance and write sys->obwp[2] of the zeroed game state (NULL): the crash on continue. */
    tx_in_game = 0;
}
int local_visible(void)
{
    /* the game hides its own player (events), a door transition or an event is running */
    return (sys->pt_flg & 0x1) && !(plp->stflg & 0x1000000) && !(sys->cb_flg & 0x5) && !(sys->gm_flg & 0x9);
}

/* The partner's weapon sounds. Each gun has an arms bank of its own (ARMS_<snd_wpno>.SPQ; the shot
   is its program 5, WpnTab[].seno0 0x105), and the game loads only ours into SE bank 1 - so before,
   the partner's shot played our gun's sound. The partner's bank is read from the disc (sound AFS,
   key snd_wpno | 0x4000, as RequestArmsSoundBank) when its weapon changes, and the sound driver keeps
   it beside ours (snd_partner_arms): arms programs SND_PARTNER_TQ.. play from it. 1: loaded. */
#define SND_PARTNER_TQ 0x40   /* pc/src/audio/snd.h (its SDL headers do not mix with the decomp's) */
void snd_partner_arms(const void* hd, unsigned int hd_size, const void* bd, unsigned int bd_size);
static int partner_arms(int no)
{
    static int have = -2, ok;
    ADXF h;
    unsigned char* buf;
    int id, n, i;
    if (no == have) return ok;
    have = no;
    ok = 0;
    if (no < 0 || PatId[2] < 0 || (id = SearchAfsInsideFileId(no | 0x4000)) < 0 || !(h = ADXF_OpenAfs(PatId[2], id))) return 0;
    n = ADXF_GetFsizeByte(h);
    buf = n > 0 && n < (8 << 20) ? malloc((size_t)ADXF_GetFsizeSct(h) * 2048) : NULL;
    if (buf) ADXF_ReadNw32(h, ADXF_GetFsizeSct(h), buf);
    if (buf && ADXF_GetStat(h) == 3)   /* SPQ: {offset, size, type, bank} entries up to offset 0; type 2 a shot bank */
        for (i = 0; !ok && (i + 1) * 16 <= n; i++) {
            const int* e = (const int*)buf + i * 4;
            const unsigned int* b = (const unsigned int*)(buf + e[0]);
            if (e[0] <= 0) break;
            if (e[2] != 2 || e[0] > n - 16 || e[1] < 16 || e[1] > n - e[0] || (e[0] & 3)) continue;
            if (b[0] > (unsigned)e[1] || b[1] > (unsigned)e[1] - b[0] || b[2] > (unsigned)e[1] || b[3] > (unsigned)e[1] - b[2] || !b[1] || !b[3]) continue;
            snd_partner_arms((const unsigned char*)b + b[0], b[1], (const unsigned char*)b + b[2], b[3]);
            ok = 1;
        }
    host_log("coop: the partner's weapon sounds (ARMS_%03d): %s\n", no, ok ? "loaded" : "not found");
    free(buf);
    ADXF_Close(h);
    return ok;
}

/* The partner's sounds, once per received pose. The sender's stflg carries this frame's
   footstep (0x200) and shot (0x100) marks (bhControlPlayer clears both each frame). Its voice:
   what its game voiced (Pose.voice_n/voice, protocol 18: hits, death, grabs, events), in the
   voice of the character it appears as (pc_player_voice). Shots: its weapon bank (partner_arms). */
static int partner_voice;               /* (pc_player_voice) */
static void partner_sounds(void)
{
    static unsigned int seq;
    static unsigned char voice_n;
    NJS_POINT3 pos = { ply2.px, ply2.py, ply2.pz };
    int fresh = rx.seq - seq < 10;      /* not after a gap (another room, busy): no stale voice */
    if (rx.seq == seq) return;
    seq = rx.seq;
    if (rx.voice_n != voice_n) {
        NJS_POINT3 keep = PlayerPos;
        voice_n = rx.voice_n;
        PlayerPos = pos;
        partner_voice = 1;
        if (fresh) CallPlayerVoice(0x400 | rx.voice);
        partner_voice = 0;
        PlayerPos = keep;
        if (trace && fresh) fprintf(stderr, "coop: partner voice %d\n", 0x400 | rx.voice);
    }
    if (rx.mset != MSET_OWN) return;   /* events, room motions, grabs: their own sounds */
    if (rx.stflg & 0x200) {
        int m = rx.mtn_no, run = m == 4 || m == 6 || m == 7 || m == 8;   /* PlMtnAct column 3: running */
        CallPlayerFootStepSeEx(bhCheckFloorSound(&ply2, ply2.flr_no, ply2.px, ply2.pz), run, 1, 1, &pos);
        if (trace) fprintf(stderr, "coop: partner footstep (%s)\n", run ? "run" : "walk");
    }
    if ((rx.stflg & 0x100) && rx.wpnr_no >= 0 && rx.wpnr_no < 23) {
        int se = WpnTab[rx.wpnr_no].seno0, bank = WpnTab[rx.wpnr_no].snd_wpno;
        if (se && (se & 0xFF) < SND_PARTNER_TQ && partner_arms(bank)) se += SND_PARTNER_TQ;   /* from the partner's bank */
        else if (plp->wpnr_no < 0 || plp->wpnr_no >= 23 || WpnTab[plp->wpnr_no].snd_wpno != bank) se = 0;   /* not loaded: none rather than ours */
        if (se) CallPlayerWeaponSeEx((NJS_POINT3*)&ply2.mlwP->owP[9].mtx[12], se, 0);
        if (trace) fprintf(stderr, "coop: partner shot (weapon %d, bank %d, se %#x)\n", rx.wpnr_no, bank, se);
        if (rx.wpnr_no == p2_wpn && ply2.mlwP->obj_num > 9) {
            /* the muzzle flash and smoke as the shooter's game makes them (bhSetGunFire), from the
               partner's hand; its room light and the pad vibration are the shooter's own (they
               follow our player), so not here */
            LGT_WORK keep;
            unsigned int st = ply2.stflg;
            int lit = rom && rom->lgt_n > 0;
            if (lit) keep = rom->lgtp[0];
            ply2.stflg &= ~0x40000000u;
            bhSetGunFire(&ply2, rx.wpnr_no, 9, 0, ply2.ay);
            ply2.stflg = st;
            if (lit) rom->lgtp[0] = keep;
        }
    }
}

/* The lighter (weapon 1: item 55 used from the STANDARD slot). The game holds it up with an arm IK
   after the motion (player.c bhControlPlayer: bhArmIkMdk on arm node 6 towards a point in front of
   the chest node 4, the hand node 9 turned), shows a flame effect at the hand (effect 9 on node 9)
   and switches on room light 1 linked to the hand (lgttab[1], a flickering point light). The
   partner gets the same from its pose (weapon 1; the sender's character for the arm offsets): the
   arm IK on ply2, a flame of its own on ply2's hand and light slot 3 (reserved in every room's
   table, switched on by no game code) at ply2's hand - our own lighter keeps slot 1. */
static O_WRK* p2_flame;
static int p2_light;

static void lighter_arm(void)
{
    BH_PWORK* keep = plp;
    NJS_POINT3 eff1 = { (rx.mfile & 3) == 0 ? 1.8f : 2.4f, -3.0f, (rx.mfile & 3) == 0 ? -3.0f : -3.5f }, eff0;
    if (ply2.mlwP->obj_num < 10) return;
    njCalcPoint(&ply2.mlwP->owP[4].mtx, &eff1, &eff0);
    plp = &ply2;   /* bhArmIkMdk reads the model's nodes through plp */
    bhArmIkMdk(&ply2, 6, &eff0, 35498);
    plp = keep;
    ply2.mlwP->objP[9].ang[1] = -910;
    ply2.mlwP->owP[7].flg |= 0x2; ply2.mlwP->owP[8].flg |= 0x2; ply2.mlwP->owP[9].flg |= 0x2;
}

/* at the hand-off: the partner's flame and light while it holds the lighter in our room, drawn */
void lighter_light(int on)
{
    int alive = p2_flame && (p2_flame->flg & 0x3) && p2_flame->id == 9 && p2_flame->lkwkp == (unsigned char*)&ply2;
    LGT_WORK* lp = rom && rom->lgt_n > 3 ? &rom->lgtp[3] : NULL;
    if (!on) {
        if (alive) p2_flame->flg = 0;
        if (p2_light && lp) lp->flg &= ~0x2;
        p2_flame = NULL;
        p2_light = 0;
        return;
    }
    if (!alive) {   /* the room load clears the effects */
        EF_WORK ef = WpnEffTab[1][0];
        int k;
        ef.type = 1;   /* type 2 follows our own player's lighter (plp->flg 0x1000000): this one we end ourselves */
        k = bhSetEffectTb(&ef, (NJS_POINT3*)&WpnTab[1].wp_fps1, (unsigned char*)&ply2, 9);
        p2_flame = k >= 0 ? &eff[k] : NULL;
    }
    if (lp) {
        if (!p2_light || lp->lkflg) {
            bhSetLightTab(&lgttab[1], 3);
            lp->lkflg = 0;   /* placed here every frame: the light code links only to plp, enemies, objects, items, effects */
            p2_light = 1;
        }
        njCalcPoint(&ply2.mlwP->owP[9].mtx, (NJS_POINT3*)&lp->lx, (NJS_POINT3*)&lp->px);
        lp->flg |= 0x3;
    }
}

/* The partner's poses by its frame (receive) and the one its stand-in shows: that of the playout's frame P, as its
   enemies are shown (docs/COOP.md "The partner's playout"); rx stays the newest (its room, what it is busy with, its look,
   its sounds). Before, the newest pose was applied as it came: at a lost or late datagram the stand-in held for a frame
   and then made two frames' way in one. Test aid RECVX_COOP_PARTNER_RAW=1: the newest, as before (the comparison runs). */
CoopPoseHist pose_hist;
static int pose_shown(Pose* sh)
{
    static int raw = -1, stats = -1;
    static unsigned int n[5], frames;
    Pose at;
    int k;
    *sh = rx;
    if (raw < 0) raw = getenv("RECVX_COOP_PARTNER_RAW") && atoi(getenv("RECVX_COOP_PARTNER_RAW"));
    if (stats < 0) stats = getenv("RECVX_COOP_STATS") != NULL;
    k = raw || echo_delay || !playout.started ? COOP_AT_NONE : coop_pose_at(&pose_hist, playout.P, &at);
    if (k != COOP_AT_NONE && (at.stg != rx.stg || at.rom != rx.rom || at.rcase != rx.rcase)) k = COOP_AT_NONE;   /* of the room it came from */
    if (k != COOP_AT_NONE) {
        sh->px = at.px; sh->py = at.py; sh->pz = at.pz; sh->ax = at.ax; sh->ay = at.ay; sh->az = at.az;
        sh->mtn_no = at.mtn_no; sh->frm_no = at.frm_no; sh->mtn_md = at.mtn_md; sh->hokan_count = at.hokan_count; sh->hokan_rate = at.hokan_rate;
        sh->mset = at.mset; sh->wpnr_no = at.wpnr_no; sh->hp = at.hp; sh->mode0 = at.mode0; sh->mode1 = at.mode1; sh->stflg = at.stflg;
        sh->pflg = at.pflg; sh->flr = at.flr; sh->legs = at.legs; sh->legs_frm = at.legs_frm;
    }
    n[k]++;
    if (trace > 0) fprintf(stderr, "coop: partner shown at frame %u: its frame %u (%d) pos=(%.2f,%.2f,%.2f) ay=%d mtn=%d frm=%d\n", frame_no,
                           k != COOP_AT_NONE ? playout.P : rx.seq, k, sh->px, sh->py, sh->pz, sh->ay, sh->mtn_no, sh->frm_no >> 16);
    if (stats && ++frames == 300) {
        host_log("coop: partner playout: %u frames: exact %u, between %u, carried on %u, held %u, newest %u\n", frames, n[COOP_AT_EXACT],
                 n[COOP_AT_INTERP], n[COOP_AT_EXTRAP], n[COOP_AT_OLD], n[COOP_AT_NONE]);
        frames = 0; memset(n, 0, sizeof(n));
    }
    return k;
}

void coop_control_remote(void)
{
    MN_WORK* set;
    Pose sh;
    int i, mtn, shown;

    if (fx_open == 1) fx_end(plp, 1);   /* the effects our step started (coop_fx_player) */
    if (!plp || !plp->mnwP) return;
    self_keep();
    actor_double_tick();   /* the cutscene double as our model (its hands before the objects follow them) */
    /* our pose for the partner (sent at the frame hand-off) */
    tx.flags = PF_IN_GAME | (local_visible() ? PF_VISIBLE : 0) | (plp->mnwP == sys->rmthp ? PF_ROOM_MTN : 0);
    tx.mset = plp->mnwP == plp->mnwPb ? MSET_OWN : plp->mnwP == sys->rmthp ? MSET_ROOM : MSET_NONE;
    for (i = 0; tx.mset == MSET_NONE && i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
        if ((ene[i].flg & 0x1) && ene[i].mnwP == plp->mnwP) tx.mset = i;   /* grabbed: the player plays the enemy's set */
    tx.mfile = sys->ply_id + (sys->costume & 3) * 4;
    i = my_chr();   /* ("As played (cutscene)": sent as the cutscene model) */
    tx.chr = !i || i >= COOP_CHR_IMPORT ? sys->ply_id : i == 5 ? 0 : i >= COOP_CHR_ACTOR ? 4 + i - COOP_CHR_ACTOR : i - 1;   /* (an imported model, which the partner may not have: shown as played) */
    tx.costume = !i ? sys->costume & 3 : i == 5;
    if (i == COOP_CHR_DC || (appear_actor(i) < 0 && (self_mesh >= 0 && self_ours() ? self_mesh : self_loaded()) == ACTOR_DC_PART)) tx.chr = 2, tx.costume = 1;   /* the
        Dreamcast Steve: Steve in costume 1 (part file 16; a partner without the import shows the PS2's Steve, actor_glue.c afs_read) */
    tx.stg = sys->stg_no; tx.rom = sys->rom_no; tx.pos = sys->pos_no;
    tx.rcase = (unsigned)my_case() <= 9 ? (uint8_t)(my_case() + 1) : 0;   /* (sys->rcase alone: a door's script picks the next room's before it leaves) */
    tx.px = plp->px; tx.py = plp->py; tx.pz = plp->pz;
    tx.ax = plp->ax; tx.ay = plp->ay; tx.az = plp->az;
    tx.mtn_no = plp->mtn_no; tx.frm_no = actor_frame(plp); tx.mtn_md = plp->mtn_md;   /* the frame shown, not the next one */
    tx.hokan_count = plp->hokan_count; tx.hokan_rate = plp->hokan_rate;
    tx.wpnr_no = plp->wpnr_no; tx.hp = plp->hp;
    tx.mode0 = plp->mode0; tx.mode1 = plp->mode1;
    tx.stflg = plp->stflg;
    tx_in_game = 1;
    if (trace) {   /* per room: enemy work pointers, to compare the memory layout across instances */
        static int last_room = -1, wait;
        if (coop_room_key() != last_room) { last_room = coop_room_key(); wait = 60; }
        if (wait && !--wait) {
            fprintf(stderr, "coop: room %d/%d gfrm=%d memp=%p ewk_n=%d\n", sys->stg_no, sys->rom_no, sys->gfrm_ct, sys->memp, sys->ewk_n);
            for (i = 0; i < (int)sys->ewk_n; i++)
                if (ene[i].flg & 1)
                    fprintf(stderr, "coop:  ene %d id=%d hp=%d dead=%d hidden=%d mlwP=%p objP=%p mnwP=%p mnwPb=%p exp0=%p exp1=%p lk=%p\n", i, ene[i].id,
                            ene[i].hp, (ene[i].flg & 2) != 0, (ene[i].stflg & 0x1000000) != 0,
                            ene[i].mlwP, ene[i].mlwP ? ene[i].mlwP->objP : 0, ene[i].mnwP, ene[i].mnwPb, ene[i].exp0, ene[i].exp1, ene[i].lkwkp);
            {   /* the session state, to compare across instances (M3) */
                unsigned int h = 0, *inv = &sys->itm[sys->ply_id * 16];
                for (i = 0; i < 120; i++) h = h * 31 + (i >= 32 && i < 64 ? 0 : sys->ev_flg[i]);   /* progress words only */
                fprintf(stderr, "coop:  progress flags hash %08x, inventory %08x %08x %08x %08x %08x %08x\n", h, inv[1], inv[2], inv[3], inv[4], inv[5], inv[6]);
                for (h = 0, i = 0; i < rom->wal_n; i++) h = h * 31 + (rom->walp[i].flg & 1);
                for (i = 0; i < rom->etc_n; i++) h = h * 31 + (rom->etcp[i].flg & 1);
                for (i = 0; i < rom->flr_n; i++) h = h * 31 + (rom->flrp[i].flg & 1);
                fprintf(stderr, "coop:  collision hash %08x (walls, triggers, floors on)\n", h);
            }
        }
    }

    /* the partner. One that is busy (cutscene, menu, item box: M8) and was shown in this room
       stays, frozen in its last pose: it is drawn only (enemies do not target it, proxy_ok) */
    if (!have_rx || !local_visible()) return;
    if (!partner_here()) { p2_seen_room = -1; return; }
    shown = pose_shown(&sh);
    p2_busy = rx.busy == BUSY_DOOR || rx.busy == BUSY_PAUSED ? 0 :   /* (protocol 56: as BUSY_NONE before) */
              (rx.flags & (PF_IN_GAME | PF_VISIBLE)) != (PF_IN_GAME | PF_VISIBLE) ? rx.busy : rx.busy == BUSY_TRADE ? BUSY_TRADE : 0;   /* trading: drawn live, no target */
    if ((rx.flags & (PF_IN_GAME | PF_VISIBLE)) != (PF_IN_GAME | PF_VISIBLE) && (!p2_busy || p2_seen_room != coop_room_key() || p2_chr < 0)) return;
    if (rx.chr < 0 || rx.chr > 3 + ACTOR_MODELS || rx.costume < 0 || rx.costume > 3 || rx.mfile < 0 || rx.mfile > 15) return;
    if (rx.chr != p2_chr || rx.costume != p2_costume || rx.mfile != p2_mfile) {
        int key = (rx.chr * 4 + rx.costume) * 16 + rx.mfile;
        if (p2_fail == key) return;
        if (!load_player(rx.chr, rx.costume, rx.mfile)) { p2_fail = key; return; }
        p2_fail = -1;
    }
    if ((!p2_busy || p2_busy == BUSY_TRADE) && sh.wpnr_no != p2_wpn) {
        load_weapon(sh.wpnr_no);
        if (sh.wpnr_no >= 0 && sh.wpnr_no < 23 && WpnTab[sh.wpnr_no].seno0) partner_arms(WpnTab[sh.wpnr_no].snd_wpno);   /* before its first shot */
    }
    if (p2_busy && p2_busy != BUSY_TRADE) {   /* the last pose */
        /* (not one of another room load: a room or an enemy motion set of the load before it, freed - a Retry of the room, the
           guest crashed in bhSetMotion showing the partner after the host's game over in 9/32) */
        if (p2_seen_gen != enemy_room_gen && ply2.mnwP != ply2.mnwPb) return;
        set = ply2.mnwP; mtn = ply2.mtn_no; goto pose;
    }

    set = sh.mset == MSET_OWN ? ply2.mnwPb : sh.mset == MSET_ROOM ? sys->rmthp :
          sh.mset >= 0 && sh.mset < (int)sys->ewk_n && sh.mset < PC_ENE_SLOTS && (ene[sh.mset].flg & 0x1) ? ene[sh.mset].mnwP : NULL;
    mtn = sh.mtn_no;
    /* the partner's motion numbers index its own sets; never trust them blindly (room and
       enemy sets have 512 entries) */
    if (!set || mtn < 0 || mtn >= (set == ply2.mnwPb ? MAX_MTN : set_left(sh.mset >= 0 ? &ene[sh.mset] : NULL, set)) || !set[mtn].datP || sh.frm_no < 0) return;
    if ((sh.frm_no >> 16) >= (int)set[mtn].frm_num) {
        if (shown != COOP_AT_EXTRAP) return;
        sh.frm_no = ((int)set[mtn].frm_num - 1) << 16;   /* carried on past the motion's end: its last frame */
    }
    if (mtn != (int)ply2.mtn_no || set != ply2.mnwP) {
        /* a motion change blends from the current pose like on the sender (which already
           took one blend step for this frame) */
        ply2.hokan_count = sh.hokan_count ? sh.hokan_count + 1 : 0;
        ply2.hokan_rate = sh.hokan_rate;
    }
    p2_seen_room = coop_room_key();
    p2_seen_gen = enemy_room_gen;
    ply2.mnwP = set;
    ply2.mtn_no = mtn;
    ply2.frm_no = sh.frm_no;
    ply2.mtn_md = sh.mtn_md;
    ply2.px = sh.px; ply2.py = sh.py; ply2.pz = sh.pz;
    ply2.ax = sh.ax; ply2.ay = sh.ay; ply2.az = sh.az;
    ply2.draw_tp = plp->draw_tp;
    /* its own floor (enemies attack only on theirs: with ours a partner on another level of the room was
       never attacked); from a build without it, the floor of its height on slopes, else ours */
    ply2.flr_no = sh.flr ? sh.flr - 3 : rom->flg & PC_ROOM_SLOPES ? bhCheckFloorNum(sh.py) : plp->flr_no;
    /* what enemies read when ply2 stands in for the partner (host): also whether its game has it hit or
       held (flg 2/4: other enemies wait, one attack at a time, as for our player) */
    ply2.hp = sh.hp; ply2.stflg = sh.stflg; ply2.mode0 = sh.mode0; ply2.mode1 = sh.mode1;
    ply2.flg = (ply2.flg & ~6u) | (sh.pflg & 6);
    /* its aim pitch: the upper body bent as its third-person free aim bends it (the fixed cameras' aim up/down is
       in its motion), eased over the poses; straight from a build without it */
    p2_pitch += (coop_pitch_get(sh.pflg) * COOP_PITCH_STEP - p2_pitch) * 0.5f;
    {   /* its legs walking under its aim ("Move While Aiming"), eased in and out as the sender eases its own */
        TpsLegs* l = &tps_legs_p2;
        l->on = sh.legs_frm != 0;
        if (l->on) l->mtn = coop_legs_mtn(sh.legs), l->frm = (sh.legs_frm - 1) << 16, l->yaw = coop_legs_yaw(sh.legs);
        l->w += ((float)l->on - l->w) * 0.3f;
        if (!l->on && l->w < 0.02f) l->w = 0;
    }
pose:
    if (!set) return;
    if (p2_other_mesh) keep_bones(&ply2.mdl[0], p2_bone);
    bhSetMotion(&ply2, 0, ply2.mtn_md, ply2.mtn_tp);
    if (p2_wpn == 1) lighter_arm();
    bhCalcModel(&ply2);
    tps_legs(&ply2, ply2.mnwPb, &tps_legs_p2);
    tps_bend(&ply2, p2_pitch);   /* (arms, head, weapons and the muzzle flash follow) */
    /* its hit capsule, as player.c sets ours every frame: most enemies' attacks (dogs, Hunters, Tyrant,
       Bandersnatch, Nosferatu, the zombie's lunge) test it - left at 0,0,0 they all missed the stand-in */
    ply2.watr.c1.x = ply2.mlwP->owP[4].mtx[12]; ply2.watr.c1.y = ply2.mlwP->owP[4].mtx[13]; ply2.watr.c1.z = ply2.mlwP->owP[4].mtx[14];
    ply2.watr.c2.x = ply2.mlwP->owP->mtx[12]; ply2.watr.c2.y = ply2.mlwP->owP->mtx[13]; ply2.watr.c2.z = ply2.mlwP->owP->mtx[14];
    ply2.watr.r = 2.5f;
    actor_source(1, &ply2, p2_chr < 4 ? p2_chr : -1, p2_mfile & 3, p2_pitch);   /* gender-matched animations */
    actor_pose(1, &ply2);
    actor_weapon_hands(1, p2wpn, 2);
    for (i = 0; i < 2; i++) {
        O_WRK* op = &p2wpn[i];
        if (!(op->flg & 0x1) || op->mlwP == &no_model || !op->mlwP || !op->mlwP->objP) continue;
        if (op->lkono == 9) {   /* bhObjWpn: weapon angle in the hand depends on aiming */
            int held = sh.mode1 == 0 || sh.mode0 == 7;
            op->ax = held ? WpnAngTab[op->type][0] : 0;
            op->ay = held ? WpnAngTab[op->type][1] : 0;
            op->az = held ? WpnAngTab[op->type][2] : 0;
        }
        bhCalcModel((BH_PWORK*)op);
    }
    if (p2obj[2].flg & 0x1) {   /* hair: as the object loop does for player 1's */
        O_WRK* op = &p2obj[2];
        njCalcPoint(&ply2.mlwP->owP[op->lkono].mtx, (NJS_POINT3*)&op->lox, (NJS_POINT3*)&op->px);
        if (op->id == 1211) bhObjClpn(op);
        bhCalcModel((BH_PWORK*)op);
    }
    if (!p2_busy) partner_sounds();
    p2_show = 1;
}

/* the name tag above the partner's head node: TAG_ABOVE units for a player (head node about
   TAG_HEAD_Y above the feet), in proportion for a taller model */
#define TAG_ABOVE 4.0f
#define TAG_HEAD_Y 16.0f
void coop_draw_remote(void)
{
    int i;
    if (!p2_show) return;
    pose_trace_mark = 2;   /* test aid RECVX_POSE_TRACE: the partner's draw (file.partner) */
    if (actor_shown(1)) actor_draw(1);
    else actor_put_body(1, &ply2);
    pose_trace_mark = 0;
    for (i = 0; i < 2; i++) {
        O_WRK* op = &p2wpn[i];
        if ((op->flg & 0x1) && op->mlwP != &no_model && op->mlwP && op->mlwP->objP && !(op->mdflg & 0x1)) bhPutModel((BH_PWORK*)op);
    }
    if ((p2obj[2].flg & 0x1) && p2obj[2].mlwP->objP && !(p2obj[2].mdflg & 0x1)) bhPutModel((BH_PWORK*)&p2obj[2]);
    p2_drawn = 1;
    {   /* above its head the overlay writes the partner's name and what it is busy with (name tag);
           the point goes with this frame (stream_point), so in-between frames move it with the 3D */
        float* m = (float*)&ply2.mlwP->owP[5].mtx, hp[3], h, z;
        int behind;
        NJS_POINT3 head = { m[12], m[13], m[14] }, v;
        NJS_POINT2 sc;
        if (actor_shown(1) && actor_head(1, hp)) { head.x = hp[0]; head.y = hp[1]; head.z = hp[2]; }   /* a catalogue model's own head */
        h = fabsf(head.y - ply2.py);
        head.y += (head.y < ply2.py ? -TAG_ABOVE : TAG_ABOVE) * (h > TAG_HEAD_Y ? h / TAG_HEAD_Y : 1.0f);   /* y is up: the head is above py */
        njProjectScreen((NJS_MATRIX*)cam.mtx, &head, &sc);   /* the camera: world -> view */
        njCalcPoint((NJS_MATRIX*)cam.mtx, &head, &v);
        z = v.z;
        {   /* in front of the camera: on the side of our own player (always in view) */
            NJS_POINT3 me = { plp->px, plp->py, plp->pz }, vm;
            njCalcPoint((NJS_MATRIX*)cam.mtx, &me, &vm);
            v.z = vm.z < 0 ? v.z : -v.z;   /* as if "in front" were view z < 0 */
        }
        behind = v.z > -1.0f;
        /* GS coordinates: the screen's first pixel is at 1728/1808 */
        stream_point(sc.x - 1728.0f, sc.y - 1808.0f, z, behind ? STREAM_PT_BEHIND : STREAM_PT_FRONT);
        if (trace && frame_no % 150 == 0) fprintf(stderr, "coop: partner tag (busy %d), head (%.1f %.1f %.1f) view (%.1f %.1f %.1f) at screen %.0f %.0f%s dist %.1f py %.1f\n", p2_busy, head.x, head.y, head.z, v.x, v.y, v.z, sc.x - 1728.0f, sc.y - 1808.0f, behind ? " behind" : "", _nj_screen_.dist, ply2.py);
        g_coop.busy_kind = p2_busy;
    }
}

/* Voices (sdfunc.c CallPlayerVoice, docs/ACTORS.md "Voices"): a player vocal sound (SE bank 4,
   0x400 + program) in the voice of the character shown (appear_voice). Ours from "You appear as",
   and it goes to the partner (Pose.voice); the partner's while partner_sounds plays it. Returns the
   extra bank: 0 = the game's bank 4 (CORE_<ply_id>) when that is the voice, else that CORE bank
   (snd_xbank_load); -1 = none: a host enemy's step that targets the partner (plp is ply2) - what
   happens to the partner, and its voice, is up to the partner's own game (its grab, its hit). */
int snd_xbank_load(int key);
static int voice_of(int chr, int played) { int v = appear_voice(chr); return v < 0 ? played : v; }
int pc_player_voice(int se)
{
    int own = sys->ply_id, v;
    if (swap_plp && plp == &ply2) return -1;
    if (partner_voice)
        v = voice_of(rx.chr >= 4 ? COOP_CHR_ACTOR + rx.chr - 4 : rx.chr + 1, rx.mfile & 3);   /* (pose chr 0..3: the player files) */
    else {
        v = !g_appear_cutscene && actor_double_acting() ? own : voice_of(my_chr(), own);   /* "Swapped model in cutscenes" off: the double's own */
        tx.voice = (unsigned char)se;
        tx.voice_n++;
    }
    return v == own ? 0 : snd_xbank_load(0xFFF0 | v);
}

/* host: our enemy options to the guest (docs/ENEMIES.md "Enemy options"): the placements and the
   randomizer change records at the room load on both sides alike */
EnemyOpts eopts_sent;
unsigned int eopts_sid;          /* the partner session they went to */
/* and the cutscene cast's "Custom" list (cast.h cast_off, EV_CASTOFF, protocol 31): each chunk that changed, all of
   it to a new partner; before the options that use it */
_Static_assert(CAST_OFF_BYTES <= COOP_CASTOFF_MAX, "EV_CASTOFF");
static unsigned char castoff_sent[CAST_OFF_BYTES];
static unsigned int castoff_sid;
void castoff_sync(void)
{
    int at, n;
    for (at = 0; at < CAST_OFF_BYTES; at += COOP_CASTOFF_CHUNK) {
        Event e = { 0 };
        n = CAST_OFF_BYTES - at < COOP_CASTOFF_CHUNK ? CAST_OFF_BYTES - at : COOP_CASTOFF_CHUNK;
        if (castoff_sid == peer_sid && !memcmp(castoff_sent + at, cast_off + at, n)) continue;
        e.type = EV_CASTOFF;
        e.u.co.at = (uint16_t)at;
        e.u.co.n = (uint8_t)n;
        memcpy(e.u.co.bits, cast_off + at, n);
        memcpy(castoff_sent + at, e.u.co.bits, n);
        queue_event(&e);
    }
    castoff_sid = peer_sid;
}
/* the host's "Enemy Models" (EV_EMODEL, protocol 61): at the session's start and whenever they change */
/* (protocol 62) an import goes as its key, not as its index in our list: that list is made in the order each player
   imported its games, so the guest drew another import (a UC zombie came out as an Outbreak survivor). One of a game
   unchecked here goes as Original (we draw the original then too) */
static Event emodel_sent;
static unsigned int emodel_sid;
void emodel_sync(void)
{
    Event e = { 0 };
    int c;
    for (c = 0; c < EM_CATS; c++) {
        int v = g_emodel[c], k = v - EM_IMPORT;
        unsigned key = v >= EM_IMPORT && k < actor_import_n && actor_import_shown(k) ? emodel_import_key(k) : 0;
        e.u.em.v[c] = (uint16_t)(v < EM_IMPORT ? v : key ? EM_IMPORT : EM_ORIGINAL);
        e.u.em.key[c] = key;
    }
    if (emodel_sid == peer_sid && !memcmp(&emodel_sent.u.em, &e.u.em, sizeof(e.u.em))) return;
    e.type = EV_EMODEL;
    queue_event(&e);
    emodel_sent = e;
    emodel_sid = peer_sid;
}
/* guest: the host's choices here. An import by its key, Original when we have none with it (not imported, or its game
   unchecked here); a random group that draws imports only while both list the same imports (cast_import_id, the cast's
   rule): the picks go by the position in the member list, so another list drew another model on each screen. Again when
   either list changes */
struct coop_em_in_s em_in;
void emodel_guest(int force)
{
    unsigned short v[COOP_EMODEL_CATS] = { 0 };
    unsigned mine, host;
    int c;
    if (!em_in.have) return;
    mine = cast_import_id(); host = coop_partner_import_id();
    if (!force && mine == em_in.mine && host == em_in.host) return;
    em_in.mine = mine; em_in.host = host;
    for (c = 0; c < EM_CATS; c++) {
        int x = em_in.e.u.em.v[c], k = x == EM_IMPORT ? emodel_import_find(em_in.e.u.em.key[c]) : -1;
        if (x == EM_IMPORT) x = k >= 0 ? EM_IMPORT + k : EM_ORIGINAL;
        else if (emodel_group_imports(x) && mine != host) x = EM_ORIGINAL;
        if (x != em_in.e.u.em.v[c] && (em_in.e.u.em.v[c] != EM_IMPORT || x == EM_ORIGINAL))
            host_log("enemymdl: the host's %s: %s, Original here\n", emodel_cat_name(c),
                     em_in.e.u.em.v[c] == EM_IMPORT ? "an import we do not have (or its game is unchecked)" : "a random group over other imports than ours");
        v[c] = (unsigned short)x;
    }
    emodel_from_host(v);
}

void eopts_send(void)
{
    Event e = { 0 };
    castoff_sync();
    e.type = EV_EOPTS;
    e.u.eo = g_enemy_opts;
    queue_event(&e);
    eopts_sent = g_enemy_opts;
    eopts_sid = peer_sid;
}
