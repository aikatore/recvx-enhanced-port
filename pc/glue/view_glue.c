/* Re-derive the game's culling volumes when the hor+ widescreen factor changes. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaView.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaGraphics3D.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_Vu1Strip.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaMatrix.h"
#include <math.h>

extern float fVu1Projection;
extern float _fNaViwClipNear, _fNaViwClipFar;
extern sceVu0FMATRIX ClipMatrix2;
extern NJS_SCREEN _nj_screen_;

void pc_view_changed(void)
{
    extern float fNaViwHalfW;
    extern NJS_SCREEN _nj_screen_;
    _Make_ClipMatrix(ClipMatrix2, fVu1Projection, _fNaViwClipNear, _fNaViwClipFar);
    fNaViwHalfW = _nj_screen_.w / 2.0f * pc_hor_scale();   /* model culling (njCnkDrawModelLocal) */
    Ps2CalcScreenCone();
}

/* enhanced frame interpolation: tags for the renderer (gs/stream.c) */
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../src/gs/stream.h"
#include "../src/gs/native.h"
#include "pc_rules.h"
#include <string.h>

uint32_t pc_cur_mesh, pc_cur_strip;   /* identity of what is being drawn (native_glue.c) */
int pc_margin_mesh;   /* the mesh being drawn shows only in the hor+ margins (native_glue.c); 2, 3: checked there (pc_margin_part) */
float pc_margin_player[2];   /* view x and z of the player, for pc_margin_mesh 3 */
static int margin_next;
void pc_margin_next(int mode) { margin_next = mode; }
static void take_view(void);
static int view_taken;
void pc_mesh_begin(const void* model)
{
    if (!view_taken) take_view();   /* the first mesh of a frame: the camera it is drawn with */
    pc_cur_mesh = (uint32_t)(uintptr_t)model; pc_cur_strip = 0; stream_tag(pc_cur_mesh);
    pc_margin_mesh = margin_next; margin_next = 0;
}
void pc_mesh_end(void) { pc_cur_mesh = 0; pc_margin_mesh = 0; stream_tag(0); }
void pc_mesh_strip(const void* strip) { pc_cur_strip = (uint32_t)(uintptr_t)strip; stream_strip(pc_cur_strip); }

/* effect.c draw loops: the following output is this effect (O_WRK; NULL: none). The tag is odd
   (meshes are aligned pointers): its eff[] slot and that slot's life, which starts again when
   the slot holds another effect or skipped a frame, so the renderer pairs an effect only with
   itself in the previous frame. Its polygons are numbered as strips (native_glue.c), or carry
   the tag after their vertices when drawn from the ordering table (ps2_dummy.c Ps2AddOT). */
void pc_effect_tag(const void* op)
{
    static uint32_t life[512], sig[512];
    static uint64_t seen[512];
    const O_WRK* o = (const O_WRK*)op;
    uint32_t slot, s;
    uint64_t now = stream_game_frames();
    if (!o || o < eff || o >= eff + 512) { pc_mesh_end(); return; }
    slot = (uint32_t)(o - eff);
    s = ((uint32_t)o->id | (uint32_t)o->type << 16 | (uint32_t)o->lkono << 24) ^ (uint32_t)(uintptr_t)o->lkwkp;
    if (sig[slot] != s || seen[slot] + 1 < now) life[slot]++;   /* seen == now: drawn twice this frame */
    sig[slot] = s; seen[slot] = now;
    pc_cur_mesh = 1u | slot << 1 | life[slot] << 10; pc_cur_strip = 0;
    stream_tag(pc_cur_mesh);
}
uint64_t pc_effect_poly(void) { return pc_cur_mesh & 1 ? pc_cur_mesh | (uint64_t)pc_cur_strip++ << 32 : 0; }
/* an effect drawing a list of its own (particle stages, trail slots, drops): the following polygons
   are part n of it, numbered from n << 12, so a part pairs with itself when parts before it come or
   go (numbered in draw order, the oldest stage of a particle stream dropping off paired every stage
   with its neighbour) */
void pc_effect_part(unsigned int n) { if (pc_cur_mesh & 1) pc_cur_strip = (n & 0xFFFFFu) << 12; }

/* "Smooth menus", while a menu task runs (title, status screen with files and item box, map,
   typewriter, OPTION; not during the title's intro movie): the frame is handed over before the
   VBLANK wait (ps2_NaSystem.c Ps2SwapDBuff), quick presses reach the pad report (bindings.c
   bind_latch_frame), and each 2D polygon outside a mesh gets a tag of its look (native_glue.c
   pc_native_prim): its primitive, depth, texture and texture coordinates, not its place or colour.
   The renderer pairs it with the nearest polygon of the same look in the previous frame (as several
   instances of one mesh), so sliding panels, opening windows and the cursor move at the display
   rate. A letter (the font's texture list) also carries the letters before it on its line and the
   steps between them, so a changed text never slides one string's letters into another's (a shared
   pair of letters was enough: a new help text flew in from the old one's place). Pairs further
   apart than any menu moves a piece are not paired (stream.c MENU_JUMP). Tags end in binary 10:
   meshes are aligned pointers, effects odd. 0: no tag. */
int pc_menu_smooth_on(void);   /* host.c: the setting, with display-rate frames */
int pc_menu_smooth(void)
{
    enum { MENUS = 1 << 3 | 1 << 4 | 1 << 9 | 1 << 10 | 1 << 15 | 1 << 16 };   /* njUserMain tasks: Firstmovie (the title menu), Title, Itemselect, Map, Typewriter, Option */
    extern int pc_movie_on(void);   /* movie_glue.c: the title's intro movie keeps its timing */
    return pc_menu_smooth_on() && (sys->tk_flg & ~sys->ts_flg & MENUS) != 0 && !pc_movie_on();
}
uint32_t pc_menu_tag(unsigned int prim, int n, const NativeVertex* v, const NativeRawVertex* r)
{
    extern NJS_TEXLIST* Ps2_current_texlist;
    extern NJS_TEXMEMLIST* Ps2_now_tex;
    static uint32_t prev_letter; static uint64_t frame; static float px, py;
    uint32_t h = 2166136261u;
    float x, y;
    if (!pc_menu_smooth() || n < 1) return 0;
#define MIX(k) (h = (h ^ (uint32_t)(k)) * 16777619u)
    MIX(prim & 0x7FF); MIX(n); MIX(v ? lroundf(v->z) : (long)r->z);
    if (prim & 0x10) MIX((uintptr_t)Ps2_now_tex);
    for (int i = 0; i < n; i++)   /* triangles: S, T (texels with FST); points, lines, sprites (r): UV and S, T */
        if (v) { MIX(lroundf(v[i].s * 4096.0f)); MIX(lroundf(v[i].t * 4096.0f)); }
        else { MIX(r[i].u | (uint32_t)r[i].v << 16); MIX(lroundf(r[i].s * 4096.0f)); MIX(lroundf(r[i].t * 4096.0f)); }
    x = v ? v->x : r->x / 16.0f; y = v ? v->y : r->y / 16.0f;   /* 2D: w = 1 */
    if (!(prim & 0x10)) {   /* untextured: which way each corner lies from the first (a left arrow is not a right
                               one, a horizontal line not a vertical one; a growing box stays itself). Not its colour:
                               a cursor highlight recolours a row's arrows, and with the colour in the look the old
                               row's arrows paired with the new row's and slid across between frames */
        for (int i = 1; i < n; i++) {
            float dx = (v ? v[i].x : r[i].x / 16.0f) - x, dy = (v ? v[i].y : r[i].y / 16.0f) - y;
            MIX((dx > 0.5f) - (dx < -0.5f) + 3 * ((dy > 0.5f) - (dy < -0.5f)));
        }
    }
    if (Ps2_current_texlist == &sys->ef_tlist && (prim & 0x10)) {   /* a letter: its string up to it */
        float lh = 0.0f;   /* the letter's height */
        for (int i = 1; i < n; i++) lh = fmaxf(lh, fabsf((v ? v[i].y : r[i].y / 16.0f) - y));
        if (frame != stream_game_frames()) { frame = stream_game_frames(); prev_letter = 0; }
        if (fabsf(y - py) > 0.5f || x <= px || x - px > 3.0f * lh) prev_letter = 0;   /* the next line or another string */
        else { MIX(prev_letter); MIX(lroundf(x - px)); }
        prev_letter = h; px = x; py = y;
    }
#undef MIX
    return (h & ~3u) | 2;
}

int pc_is_room_object(const void* obj)
{
    const NJS_CNK_OBJECT* o = (const NJS_CNK_OBJECT*)obj;
    return rom->mdl.objP && o >= rom->mdl.objP && o < rom->mdl.objP + rom->mdl.obj_num;
}

/* hor+ widescreen with a fixed camera (ps2_NinjaCnk.c pc_show_hidden): is a room part the cut
   hides drawn anyway? With the native renderer it is, cut to the margins (pc_margin_next,
   stream.c native_draw), unless it would cover the player or end in a straight edge at the 4:3
   border; native_glue.c pc_margin_reaches_43 checks its vertices after the model's vertex chunk.
   A part whose bounding sphere reaches the camera or the line from the camera to the player
   (pc_margin_mesh 3: a wall the cut looks through, or one running past the camera) is drawn
   only if it lies entirely beside the 4:3 view and the player, like the walls running from the
   camera's corner of a courtyard (0/3, 0/10). Other parts in front of the player (2) are left
   out when a vertex lies inside the 4:3 view. Without the native renderer, only a part
   entirely outside the 4:3 view. Called with the part's model matrix current.
   A part left out takes along the hidden parts below it that lie in its box (pc_rules.h
   pc_margin_inside; ps2_NinjaCnk.c -> pc_margin_children): the dolls on the shelves of Alexia's
   cabinet (2/5, cut 1) stood in the margin in front of an empty frame when the cabinet, which
   reaches the camera and into the 4:3 view, was left out. */
static const void* margin_obj;   /* the last part tested */
int pc_margin_out;               /* it was left out (also native_glue.c pc_margin_reaches_43) */
float pc_margin_box[6];          /* its view-space box, of its vertices (native_glue.c; none without the native renderer) */
static float margin_group[32][6];   /* the boxes of the left-out parts whose children are being drawn */
static int margin_groups;
static int margin_part(const void* obj);
int pc_margin_part(const void* obj)
{
    int shown = margin_part(obj);
    margin_obj = obj;
    pc_margin_out = !shown;
    return shown;
}
/* njCnkEasy*DrawObject, before the children of the node obj: the number of
   groups to restore after them (pc_margin_children_end) */
int pc_margin_children(const void* obj)
{
    int n = margin_groups;
    if (obj == margin_obj && pc_margin_out && n < 32) memcpy(margin_group[margin_groups++], pc_margin_box, sizeof(pc_margin_box));
    margin_obj = NULL;
    return n;
}
void pc_margin_children_end(int n) { margin_groups = n; }
static int margin_part(const void* obj)
{
    const NJS_CNK_MODEL* m = ((const NJS_CNK_OBJECT*)obj)->model;
    NJS_POINT3 ctr = m->center, c, pw, p;
    float z, r, sx, half, t, pl, dx, dy, dz;
    pc_margin_box[0] = 1; pc_margin_box[3] = 0;   /* none (without the native renderer there are no vertices) */
    if (m->r <= 0) return 0;
    njCalcPoint(NULL, &ctr, &c);
    for (int i = 0; i < margin_groups; i++)
        if (pc_margin_inside(&c.x, m->r, margin_group[i])) return 0;
    if (pc_native_3d()) {
        pw.x = plp->px; pw.y = plp->py; pw.z = plp->pz;
        njCalcPoint(cam.mtx, &pw, &p);
        pl = p.x * p.x + p.y * p.y + p.z * p.z;
        t = pl > 0 ? (c.x * p.x + c.y * p.y + c.z * p.z) / pl : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        dx = c.x - t * p.x; dy = c.y - t * p.y; dz = c.z - t * p.z;
        pc_margin_player[0] = p.x; pc_margin_player[1] = p.z;
        pc_margin_next(c.z - m->r < _fNaViwClipNear || dx * dx + dy * dy + dz * dz < m->r * m->r ? 3 : c.z < p.z ? 2 : 1);
        return 1;
    }
    if (c.z - m->r < _fNaViwClipNear) return 0;
    z = fabsf(_nj_screen_.dist / c.z);
    r = m->r * z * fNaViwAspectW;
    sx = c.x * z;
    half = _nj_screen_.w / 2.0f;
    return sx - r > half || sx + r < -half;
}

/* The bounding-sphere test of njCnkDrawModelLocal (ps2_NinjaCnk.c) with the third-person camera
   or in hor+: -1 culled, 1 drawn, 0 the original test (4:3 with a fixed camera). The original
   compares the sphere's screen radius on the view axis (r * dist / z) with the screen edge, but
   away from the axis a sphere's outline reaches further, by sqrt(1 + tan^2) of the angle: at the
   edges of the wide 21:9 and 32:9 views, large room parts near the camera were culled while they
   still reached into the picture. Here the sphere is tested against the view's side, top and
   bottom planes, widened by PC_CULL_MARGIN of the half width and height (the same share of the
   view in every aspect). The margin covers the models whose sphere does not hold all of them
   (some effects near the camera, 0/3 and 1/2) and parts entering the view while the camera
   turns, which are then already in the frame the in-between images blend from. With a fixed
   camera, a part only this test draws (the original culls it, so it is outside the 4:3 view as
   the original sees it) is drawn only in the margins (pc_margin_mesh, as pc_margin_part), so the
   4:3 picture stays the original; without the native renderer it stays culled. Called with the
   model's matrix current. */
#define PC_CULL_MARGIN 1.5f
int pc_model_clip(const void* center, float r)
{
    extern float fNaViwHalfW, fNaViwHalfH, fNaViwAspectW, fNaViwAspectH;
    const NJS_POINT3* c = (const NJS_POINT3*)center;
    float tx, ty, z;
    if (!tps_cam_active && pc_hor_scale() <= 1.0f) return 0;
    tx = fNaViwHalfW / (fNaViwAspectW * _nj_screen_.dist) * PC_CULL_MARGIN;   /* tan of the half angles */
    ty = fNaViwHalfH / (fNaViwAspectH * _nj_screen_.dist) * PC_CULL_MARGIN;
    if (fabsf(c->x) - tx * c->z > r * sqrtf(1.0f + tx * tx) || fabsf(c->y) - ty * c->z > r * sqrtf(1.0f + ty * ty)) return -1;
    if (tps_cam_active) return 1;
    z = fabsf(_nj_screen_.dist / c->z);   /* the original test */
    if (fabsf(c->x * z) - r * z * fNaViwAspectW > fNaViwHalfW || fabsf(c->y * z) - r * z * fNaViwAspectH > fNaViwHalfH) {
        if (!pc_native_3d()) return -1;
        if (!pc_margin_mesh) pc_margin_mesh = 1;
    }
    return 1;
}

/* Frames of different scenes or camera cuts are never blended. Taken when the frame's first mesh
   is drawn (else at its end), which is the cut the picture shows: game.c bhCheckCut changes the
   cut after drawing, so at the frame's end it is the next frame's. The scene: stage, room and,
   unless the third-person camera is on, the cut (ncut, camver), the event or fixed camera mode
   (cam.flg 0x46) with its event camera, and first person / scope. The view: the eye and view
   direction of cam.mtx, which bhControlCamera computed for the drawing. */
static uint64_t scene_now;
static float view_now[6];
/* The camera the native vertices are computed with (native_glue.c pc_native_strip): cam.mtx's
   rotation rows and translation (view = world * R + T), then the projection kx, ky, offset x, y,
   depth a, b and the near and far planes; all 0 without a camera. stream.c moves what has no
   counterpart in the previous frame with the camera from it (stream_match). */
static float cam_now[STREAM_CAM];
static void take_cam(void)
{
    extern float fVu1AspectH, fVu1OffsetY, Ps2_zbuff_a, Ps2_zbuff_b;
    const float* m;
    memset(cam_now, 0, sizeof(cam_now));
    if (!cam.mtx) return;
    m = &(*cam.mtx)[0];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++) cam_now[r * 3 + c] = m[r * 4 + c];
    cam_now[12] = fVu1AspectW * fVu1Projection; cam_now[13] = fVu1AspectH * fVu1Projection;
    cam_now[14] = fVu1OffsetX; cam_now[15] = fVu1OffsetY;
    cam_now[16] = Ps2_zbuff_a; cam_now[17] = Ps2_zbuff_b;
    cam_now[18] = _fNaViwClipNear; cam_now[19] = _fNaViwClipFar;
}
static void take_view(void)
{
    uint32_t cut = 0xFFFFFFFFu;
    if (!tps_cam_active)
        cut = (uint32_t)(cam.ncut & 0xFF) | (uint32_t)(cam.camver & 1) << 8 | (cam.flg & 0x46) << 9 | (sys->gm_flg & 0x40) << 10 |
              (cam.flg & 0x2 ? (uint32_t)(cam.evc_no & 0xFF) << 24 : 0);
    scene_now = (uint64_t)(unsigned char)sys->stg_no << 40 | (uint64_t)(unsigned char)sys->rom_no << 32 | cut;
    memset(view_now, 0, sizeof(view_now));   /* none: not compared (the third-person camera moves fast but never jumps) */
    if (cam.mtx && !tps_cam_active) {   /* row vectors: view = world * R + T, so the eye is -T * R^T, the view axis R's third column */
        const float* m = &(*cam.mtx)[0];
        for (int r = 0; r < 3; r++) {
            view_now[r] = -(m[12] * m[r * 4] + m[13] * m[r * 4 + 1] + m[14] * m[r * 4 + 2]);
            view_now[3 + r] = m[r * 4 + 2];
        }
    }
    take_cam();
    view_taken = 1;
}
/* stream submit: 1 if the screen fade covers the frame but lets it through (a room's fade-in): HD
   texture packs wait for replacements first drawn then, else they are loaded in the background
   (hdpack.c) */
int pc_frame_fade(void) { return sys->fade_an > 0.0f && sys->fade_an < 255.0f; }

/* stream_submit: the scene and view (eye, direction) of the frame being submitted */
uint64_t pc_frame_view(float view[6], float camera[STREAM_CAM])
{
    if (!view_taken) take_view();
    view_taken = 0;
    memcpy(view, view_now, sizeof(view_now));
    memcpy(camera, cam_now, sizeof(cam_now));
    return scene_now;
}
