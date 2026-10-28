/*
 * GS command stream between the game thread and the render thread, and frame interpolation.
 *
 * Game thread: the GIF parser and libgraph record every GS register write, image upload,
 * display-buffer change and download request of a frame (stream_*). At the buffer swap the
 * frame is queued for the render thread; at most one frame waits, so the game runs at most
 * one frame ahead. A download (StoreImage) is a sync point: the partial frame is executed
 * and the game waits for the pixels.
 *
 * Render thread: frames are executed in order against the GS emulation and OpenGL
 * (stream_exec). For interpolation (the "enhanced" mode) each executed frame keeps its
 * starting GS state and its primitive groups (runs of vertices between PRIM/TEX0 changes).
 * The engine tags every 3D mesh it draws (njCnkDrawModelLocal -> stream_tag), so a mesh
 * instance's groups correspond exactly, vertex for vertex, to the same mesh's groups in the
 * previous frame; among instances of one mesh (several enemies of a type) the one that moved
 * least is taken. The 3D effects (fire, muzzle flashes, sparks, blood) are tagged per effect
 * (pc_effect_tag), their polygons numbered. The frame can then be replayed with matched vertex
 * positions blended towards the previous frame (stream_replay). Untagged output (2D) is shown as
 * is, and frames of different scenes, camera cuts or camera jumps are never blended (view_cut).
 */
#include "gs.h"
#include "stream.h"
#include "native.h"
#include "textures.h"
#include "hdpack.h"
#include "../host/prof.h"
#include "../ps2_platform.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOEV 0xFFFFFFFFu
#define NATIVE_KEY (1ull << 63)   /* Group.key0 of a native 3D strip (v0 = its event) */
#define RAW_KEY (1ull << 62)      /* ... of a menu's points, lines or sprites (EV_RAW under a menu tag, pc_menu_tag) */
#define MENU_TAG(m) (((m) & 3) == 2)
enum { EV_REG, EV_IMAGE, EV_DISP, EV_DOWNLOAD, EV_TAG, EV_STRIP, EV_NATIVE, EV_TEXDEF, EV_TEXDEF_END, EV_TEXBIND, EV_TEXFORGET, EV_RAW, EV_RTEX, EV_MOVIE, EV_TAGLAST };
typedef struct { uint8_t kind, reg; uint32_t len; uint64_t v; } Ev;
typedef struct { uint64_t key0; uint32_t v0, nv, strip, ord, sp; } Group;   /* sp: native strip's first Frame.spos */
typedef struct { uint32_t mesh, g0, ng; } Inst;   /* a tagged mesh draw: groups g0.. */

struct Frame {
    Ev* ev; size_t nev, cev;
    uint8_t* pool; size_t npool, cpool;
    int partial, continuation;
    uint64_t scene;                          /* pc_frame_view: stage, room, cut */
    float view[6];                           /* ... and the camera's eye and view direction */
    float cam[STREAM_CAM];                   /* ... and its matrix and projection (unmatched meshes move with it) */
    float pt[3]; int pt_flags;               /* stream_point */
    int fade;                                /* pc_frame_fade: the picture shows through a screen fade */
    /* filled by the render thread when executed */
    Group* g; size_t ng, cg;
    Inst* in; size_t nin, cin;
    uint32_t* vx; size_t nvx, cvx;           /* event index of each vertex */
    uint32_t* stq; size_t cstq;              /* per vertex: its ST and RGBAQ events (NOEV: none) */
    uint64_t* spos; size_t nspos, cspos;     /* native strip vertices' screen positions (gpos), once per frame */
    uint16_t (*box)[4]; size_t cbox;         /* per instance: x0, y0, x1, y1 of its screen positions (x0 > x1: none) */
    int spos_done;
    GsState start;
    uint8_t clut_start[GS_CLUT_STATE_SIZE];
    uint8_t ntex_start[NTEX_STATE_SIZE];
    int new_group;
};

#define POOL 4
static Frame frames[POOL];
static Frame* free_list[POOL]; static int nfree;
static Frame* queue[POOL]; static int qhead, qlen;
static SDL_Mutex* mtx;
static SDL_Condition* cond;
static Frame* rec;                           /* being recorded (game thread) */
static int continuation_next;
static volatile int download_done;
static SDL_AtomicInt game_frames;
int gs_replaying;
uint32_t gs_tex_epoch;

static int tex_event(const Ev* e)
{
    switch (e->kind) {
    case EV_IMAGE: case EV_TEXBIND: case EV_TEXDEF_END: case EV_TEXFORGET: case EV_DOWNLOAD: case EV_RTEX: case EV_MOVIE: return 1;
    case EV_REG:   /* TEX0, TEX2, TEXCLUT, TEXA, transfers */
        return e->reg == 0x06 || e->reg == 0x07 || e->reg == 0x16 || e->reg == 0x17 || e->reg == 0x1c ||
               e->reg == 0x3b || (e->reg >= 0x50 && e->reg <= 0x54);
    default: return 0;
    }
}

static void* xrealloc(void* p, size_t n)
{
    void* q = realloc(p, n);
    if (!q && n) host_fatal("Not enough memory (%zu bytes).", n);
    return q;
}
#define GROW(ptr, n, cap, extra) do { if ((n) + (extra) > (cap)) { (cap) = ((n) + (extra)) * 2 + 1024; (ptr) = xrealloc((ptr), (cap) * sizeof(*(ptr))); } } while (0)

void stream_init(void)
{
    mtx = SDL_CreateMutex();
    cond = SDL_CreateCondition();
    for (int i = 0; i < POOL; i++) free_list[nfree++] = &frames[i];
}

uint64_t stream_game_frames(void) { return (uint64_t)SDL_GetAtomicInt(&game_frames); }

/* ---------------------------------------------------------------- game thread */

static Frame* recording(void)
{
    if (!rec) {
        prof_push(PROF_QUEUE);   /* all frames in use: the render thread is behind */
        SDL_LockMutex(mtx);
        while (!nfree) SDL_WaitCondition(cond, mtx);
        rec = free_list[--nfree];
        SDL_UnlockMutex(mtx);
        prof_pop();
        rec->nev = rec->npool = 0;
        rec->partial = 0;
        rec->continuation = continuation_next;
        continuation_next = 0;
    }
    return rec;
}

static void push_ev(int kind, int reg, uint32_t len, uint64_t v)
{
    Frame* f = recording();
    GROW(f->ev, f->nev, f->cev, 1);
    f->ev[f->nev++] = (Ev){ (uint8_t)kind, (uint8_t)reg, len, v };
}

void stream_reg(int reg, uint64_t v) { push_ev(EV_REG, reg, 0, v); }

void stream_image(const uint8_t* data, uint32_t len)
{
    Frame* f = recording();
    GROW(f->pool, f->npool, f->cpool, len);
    memcpy(f->pool + f->npool, data, len);
    push_ev(EV_IMAGE, 0, len, f->npool);
    f->npool += len;
}

void stream_tag(uint32_t mesh) { push_ev(EV_TAG, 0, 0, mesh); }

/* native textures (textures.c): the GS writes between def_begin and def_end are a texture
   definition, recorded but not executed; a bind makes a slot show a definition */
void stream_tex_def_begin(uint32_t id, uint32_t tbp, uint32_t cbp, uint32_t image_pages, int flags)
{
    push_ev(EV_TEXDEF, 0, image_pages, id | ((uint64_t)(tbp & 0x3FFF) << 32) | ((uint64_t)(cbp & 0x3FFF) << 46) | ((uint64_t)(flags & 3) << 60));
}
void stream_tex_def_end(void) { push_ev(EV_TEXDEF_END, 0, 0, 0); }
void stream_tex_forget(uint32_t id) { push_ev(EV_TEXFORGET, 0, 0, id); }
void stream_tex_bind(uint32_t id, uint32_t tbp, uint32_t cbp)
{
    push_ev(EV_TEXBIND, 0, 0, id | ((uint64_t)(tbp & 0x3FFF) << 32) | ((uint64_t)(cbp & 0x3FFF) << 46));
}
void stream_strip(uint32_t strip) { push_ev(EV_STRIP, 0, 0, strip); }
void stream_tag_last(uint32_t mesh, uint32_t strip) { push_ev(EV_TAGLAST, 0, strip, mesh); }
/* movie frame `index` (pc/src/movie) is the texture at TBP tbp for the draws that follow */
void stream_movie(uint32_t tbp, int index) { push_ev(EV_MOVIE, 0, (uint32_t)index, tbp & 0x3FFF); }
/* StoreRenderTex on the GPU: the render target FBP/FBW, x 0..w-1, y 0..h-1, becomes
   definition id (a captured texture, bound like any other); no download, no sync point */
void stream_rtex_capture(uint32_t id, uint32_t fbp, uint32_t fbw, uint32_t w, uint32_t h, int wide)
{
    push_ev(EV_RTEX, 0, w | (h << 16), id | ((uint64_t)(fbp & 0x1FF) << 32) | ((uint64_t)(fbw & 0x3F) << 48) | ((uint64_t)(wide != 0) << 56));
}

/* in-between vertex: only the position (and the clip distances, linear in it) is blended;
   texture coordinates, fog and colour are the current frame's, so a strip paired with
   another pass over the same geometry (e.g. the fire light over the courtyard floor and
   truck) keeps its own texture mapping and lighting */
static NativeVertex nv_blend_pos(const NativeVertex* a, const NativeVertex* b, float k)
{
    NativeVertex o = *b;
    o.x = a->x + (b->x - a->x) * k; o.y = a->y + (b->y - a->y) * k;
    o.z = a->z + (b->z - a->z) * k; o.w = a->w + (b->w - a->w) * k;
    o.near_d = a->near_d + (b->near_d - a->near_d) * k; o.far_d = a->far_d + (b->far_d - a->far_d) * k;
    return o;
}

/* the blended fields of n vertices are the same in a and b, and blending would return b's
   (a + (b - a) * t == b for finite values, but not for -0: -0 + 0 = +0), so drawing b
   unblended gives the in-between image exactly (the replay reuses the executed frame's
   vertices for it) */
static int nv_same_pos(const NativeVertex* a, const NativeVertex* b, uint32_t n)
{
    if (n > 64) n = 64;   /* what native_draw draws */
    for (uint32_t k = 0; k < n; k++) {
        const float *fa[6] = { &a[k].x, &a[k].y, &a[k].z, &a[k].w, &a[k].near_d, &a[k].far_d },
                    *fb[6] = { &b[k].x, &b[k].y, &b[k].z, &b[k].w, &b[k].near_d, &b[k].far_d };
        for (int j = 0; j < 6; j++) {
            uint32_t x, y;
            memcpy(&x, fa[j], 4); memcpy(&y, fb[j], 4);
            if (x != y || (x & 0x7F800000u) == 0x7F800000u || x == 0x80000000u) return 0;   /* differ, Inf/NaN, -0 */
        }
    }
    return 1;
}

/* a convex polygon of n vertices as triangles at tri[m]; returns the new count */
static int fan(NativeVertex* tri, int m, const NativeVertex* p, int n)
{
    for (int j = 2; j < n; j++) { tri[m++] = p[0]; tri[m++] = p[j - 1]; tri[m++] = p[j]; }
    return m;
}

/* Native 3D strips are recorded as they come (PRIM attribute bits, len = vertex count, data
   in the pool), so a strip has the same vertices in every frame and an in-between frame can
   blend them one to one; culling and clipping happen when drawn (native_draw). */
void native_strip(unsigned int prim, const NativeVertex* v, int n)
{
    Frame* f = recording();
    uint32_t len = (uint32_t)(n * sizeof(NativeVertex));
    if (n < 3) return;
    GROW(f->pool, f->npool, f->cpool, len);
    memcpy(f->pool + f->npool, v, len);
    GROW(f->ev, f->nev, f->cev, 1);
    f->ev[f->nev++] = (Ev){ EV_NATIVE, 0, (uint32_t)n, f->npool | ((uint64_t)prim << 32) };
    f->npool += len;
}

/* render thread: strip -> triangles; triangles whose three normals face away are dropped
   (SingleMain) and the rest clipped against the near and far planes, as the VU1 SCISSOR
   routine does, so the GPU only ever gets vertices in front of the camera (w >= near > 0):
   how drivers clip primitives reaching behind the camera differs. Margin triangles (cull bit 2)
   are also cut to the parts left and right of the 4:3 screen, so the 4:3 picture stays exact. */
static void native_draw(unsigned int prim, const NativeVertex* v, int n, long ev)
{
    static NativeVertex tri[64 * 24];   /* render thread only; up to 8 triangles per margin triangle */
    int m = 0;
    int type = (int)(prim & 7);   /* 3 triangle list, 5 fan, otherwise strip */
    if (n > 64) n = 64;
    for (int i = 2; i < n; i += type == 3 ? 3 : 1) {
        const NativeVertex* t3[3] = { type == 5 ? &v[0] : &v[i - 2], &v[i - 1], &v[i] };
        int margin = t3[2]->cull & 4;
        if ((t3[2]->cull & 2) || (t3[0]->cull & t3[1]->cull & t3[2]->cull & 1)) continue;
        if (!margin && t3[0]->near_d >= 0 && t3[1]->near_d >= 0 && t3[2]->near_d >= 0 && t3[0]->far_d >= 0 && t3[1]->far_d >= 0 && t3[2]->far_d >= 0) {
            tri[m++] = *t3[0]; tri[m++] = *t3[1]; tri[m++] = *t3[2];
        } else {
            NativeVertex p0[3] = { *t3[0], *t3[1], *t3[2] }, p1[4], p2[5], p3[6];
            int k = native_clip_plane(p0, 3, p1, 0);
            k = k ? native_clip_plane(p1, k, p2, 1) : 0;
            if (!margin) m = fan(tri, m, p2, k);
            else for (int side = 2; side < 4; side++) m = fan(tri, m, p3, k ? native_clip_plane(p2, k, p3, side) : 0);   /* left, right */
        }
    }
    if (m) gs_draw_native(prim, tri, m, ev);
}

/* deferred native strips (game thread), valid until the frame is submitted */
typedef struct { unsigned int prim; uint32_t first, n, mesh, strip; int raw; } Deferred;
static Deferred* deferred; static size_t ndef, cdef;
static NativeVertex* defv; static size_t ndefv, cdefv;
static NativeRawVertex* defr; static size_t ndefr, cdefr;

uint32_t native_defer_raw(unsigned int prim, const NativeRawVertex* v, int n, uint32_t mesh)
{
    GROW(defr, ndefr, cdefr, (size_t)n);
    memcpy(defr + ndefr, v, (size_t)n * sizeof(*v));
    GROW(deferred, ndef, cdef, 1);
    deferred[ndef] = (Deferred){ prim, (uint32_t)ndefr, (uint32_t)n, mesh, 0, 1 };
    ndefr += (size_t)n;
    return (uint32_t)ndef++;
}

uint32_t native_defer(unsigned int prim, const NativeVertex* v, int n, uint32_t mesh, uint32_t strip)
{
    GROW(defv, ndefv, cdefv, (size_t)n);
    memcpy(defv + ndefv, v, (size_t)n * sizeof(*v));
    GROW(deferred, ndef, cdef, 1);
    deferred[ndef] = (Deferred){ prim, (uint32_t)ndefv, (uint32_t)n, mesh, strip, 0 };
    ndefv += (size_t)n;
    return (uint32_t)ndef++;
}

/* GS write of NATIVE_MARKER_REG: emit the deferred strip here (0xFFFFFFFF: padding) */
void native_marker(uint64_t v)
{
    const Deferred* d;
    if ((uint32_t)v >= ndef) return;
    d = &deferred[(uint32_t)v];
    if (d->raw) {
        Frame* f;
        uint32_t len = d->n * (uint32_t)sizeof(NativeRawVertex);
        if (d->mesh) { stream_tag(d->mesh); stream_strip(0); }
        f = recording();
        GROW(f->pool, f->npool, f->cpool, len);
        memcpy(f->pool + f->npool, defr + d->first, len);
        GROW(f->ev, f->nev, f->cev, 1);
        f->ev[f->nev++] = (Ev){ EV_RAW, 0, d->n, f->npool | ((uint64_t)d->prim << 32) };
        f->npool += len;
        if (d->mesh) stream_tag(0);
        return;
    }
    if (d->mesh) stream_tag(d->mesh);
    stream_strip(d->strip);
    native_strip(d->prim, defv + d->first, (int)d->n);
    if (d->mesh) stream_tag(0);
}

static const NativeVertex* native_verts(const Frame* f, uint32_t ev) { return (const NativeVertex*)(f->pool + (uint32_t)f->ev[ev].v); }
static const NativeRawVertex* raw_verts(const Frame* f, uint32_t ev) { return (const NativeRawVertex*)(f->pool + (uint32_t)f->ev[ev].v); }

void stream_disp(uint32_t fbp, uint32_t fbw, uint32_t psm) { push_ev(EV_DISP, 0, 0, fbp | ((uint64_t)fbw << 16) | ((uint64_t)psm << 32)); }

static void submit(int partial)
{
    extern int pc_frame_fade(void);
    Frame* f = recording();
    f->partial = partial;
    f->fade = pc_frame_fade();
    rec = NULL;
    prof_push(PROF_QUEUE);
    SDL_LockMutex(mtx);
    while (qlen >= 1) SDL_WaitCondition(cond, mtx);   /* backpressure: one frame in flight */
    queue[(qhead + qlen++) % POOL] = f;
    SDL_BroadcastCondition(cond);
    SDL_UnlockMutex(mtx);
    prof_pop();
}

static float point[3];
static int point_flags;
void stream_point(float x, float y, float w, int flags)
{
    point[0] = x; point[1] = y; point[2] = w;
    point_flags = flags;
}

void stream_submit(void)
{
    extern int gs_take_view_change(void);
    extern void pc_view_changed(void);
    extern uint64_t pc_frame_view(float view[6], float cam[STREAM_CAM]);
    recording()->scene = pc_frame_view(rec->view, rec->cam);
    memcpy(rec->pt, point, sizeof(point));
    rec->pt_flags = point_flags;
    point_flags = 0;
    ndef = ndefv = ndefr = 0;   /* deferred native strips are drawn within their frame */
    submit(0);
    SDL_AddAtomicInt(&game_frames, 1);
    { extern void glue_debug_frame(uint64_t); glue_debug_frame(stream_game_frames()); }
    if (gs_take_view_change()) pc_view_changed();    /* widescreen change: game-side culling */
}

void stream_download(void* dst, uint32_t len)
{
    push_ev(EV_DOWNLOAD, 0, len, (uint64_t)(uintptr_t)dst);
    download_done = 0;
    submit(1);
    continuation_next = 1;
    prof_push(PROF_QUEUE);
    SDL_LockMutex(mtx);
    while (!download_done) SDL_WaitCondition(cond, mtx);
    SDL_UnlockMutex(mtx);
    prof_pop();
}

/* ---------------------------------------------------------------- render thread */

Frame* stream_pop(int timeout_ms)
{
    Frame* f = NULL;
    SDL_LockMutex(mtx);
    if (!qlen && timeout_ms) SDL_WaitConditionTimeout(cond, mtx, timeout_ms);
    if (qlen) { f = queue[qhead]; qhead = (qhead + 1) % POOL; qlen--; SDL_BroadcastCondition(cond); }
    SDL_UnlockMutex(mtx);
    return f;
}

int stream_queued(void)
{
    int n;
    SDL_LockMutex(mtx);
    n = qlen;
    SDL_UnlockMutex(mtx);
    return n;
}

void stream_release(Frame* f)
{
    if (!f) return;
    SDL_LockMutex(mtx);
    free_list[nfree++] = f;
    SDL_BroadcastCondition(cond);
    SDL_UnlockMutex(mtx);
}

int stream_is_partial(const Frame* f) { return f->partial; }

static const Frame* cached_for;   /* the frame whose native strips gs_gl.c keeps (gs_draw_native) */

static int is_xyz(int reg) { return reg == 0x04 || reg == 0x05 || reg == 0x0c || reg == 0x0d; }

/* uploads: native texture slots over the pages lose their binding */
void stream_note_upload(const uint64_t* pages)
{
    ntex_note_upload(pages);
}

void stream_exec(Frame* f)
{
    long inst = -1;
    uint32_t strip = 0, ord = 0, last_st = NOEV, last_q = NOEV;
    cached_for = f;
    gs_native_cache_begin(f->nev);
    f->ng = f->nvx = f->nin = 0;
    f->nspos = 0; f->spos_done = 0;
    f->new_group = 1;
    f->start = gs;
    gs_clut_state_save(f->clut_start);
    ntex_state_save(f->ntex_start);
    gs_tex_epoch = 0;
    hdpack_frame(f->fade);
    for (size_t i = 0; i < f->nev; i++) {
        const Ev* e = &f->ev[i];
        if (tex_event(e)) gs_tex_epoch = (uint32_t)i + 1;
        if (ntex_defining()) {   /* texture definition: recorded, not executed */
            if (e->kind == EV_REG) { ntex_define_reg(e->reg, e->v); continue; }
            if (e->kind == EV_IMAGE) { ntex_define_image(f->pool + e->v, e->len); continue; }
            if (e->kind == EV_TEXDEF_END) { ntex_define_end(); continue; }
        }
        switch (e->kind) {
        case EV_TEXDEF:
            ntex_define_begin((uint32_t)e->v, (uint32_t)(e->v >> 32) & 0x3FFF, (uint32_t)(e->v >> 46) & 0x3FFF, e->len, (int)(e->v >> 60) & 3);
            break;
        case EV_TEXBIND:
            ntex_bind((uint32_t)e->v, (uint32_t)(e->v >> 32) & 0x3FFF, (uint32_t)(e->v >> 46) & 0x3FFF);
            break;
        case EV_TEXFORGET:
            ntex_forget((uint32_t)e->v);
            break;
        case EV_RAW:
            if (inst >= 0 && MENU_TAG(f->in[inst].mesh)) {   /* a group of its own, matched like a native strip */
                GROW(f->g, f->ng, f->cg, 1);
                f->g[f->ng++] = (Group){ RAW_KEY | (e->v >> 32), (uint32_t)i, e->len, strip, ord++, (uint32_t)f->nspos };
                f->nspos += e->len;
                f->in[inst].ng++;
                f->new_group = 1;
            }
            gs_draw_raw((unsigned int)(e->v >> 32), (const NativeRawVertex*)(f->pool + (uint32_t)e->v), (int)e->len);
            break;
        case EV_REG:
            if (e->reg == 0x02) last_st = (uint32_t)i;
            else if (e->reg == 0x01) last_q = (uint32_t)i;
            if (e->reg == 0x00 || e->reg == 0x06 || e->reg == 0x07 || e->reg == 0x16 || e->reg == 0x17) f->new_group = 1;
            else if (is_xyz(e->reg)) {
                if (f->new_group || !f->ng) {
                    GROW(f->g, f->ng, f->cg, 1);
                    f->g[f->ng++] = (Group){ gs.prim, (uint32_t)f->nvx, 0, strip, ord++, 0 };
                    f->new_group = 0;
                    if (inst >= 0) f->in[inst].ng++;
                }
                GROW(f->vx, f->nvx, f->cvx, 1);
                GROW(f->stq, f->nvx * 2, f->cstq, 2);
                f->stq[f->nvx * 2] = last_st; f->stq[f->nvx * 2 + 1] = last_q;
                last_st = last_q = NOEV;
                f->vx[f->nvx++] = (uint32_t)i;
                f->g[f->ng - 1].nv++;
            }
            gs_exec_reg(e->reg, e->v);
            break;
        case EV_IMAGE:
            gs_exec_upload(f->pool + e->v, e->len);
            break;
        case EV_NATIVE:   /* a group of its own (key0 bit 63), matched like the GS groups */
            GROW(f->g, f->ng, f->cg, 1);
            f->g[f->ng++] = (Group){ NATIVE_KEY | (e->v >> 32), (uint32_t)i, e->len, strip, ord++, (uint32_t)f->nspos };
            f->nspos += e->len;
            if (inst >= 0) f->in[inst].ng++;
            f->new_group = 1;
            native_draw((unsigned int)(e->v >> 32), native_verts(f, (uint32_t)i), (int)e->len, g_config.interpolate ? (long)i : -1);
            break;
        case EV_STRIP:
            f->new_group = 1;
            strip = (uint32_t)e->v; ord = 0;
            break;
        case EV_TAG:
            f->new_group = 1;
            inst = -1;
            strip = 0; ord = 0;
            if (e->v) {
                GROW(f->in, f->nin, f->cin, 1);
                f->in[f->nin] = (Inst){ (uint32_t)e->v, (uint32_t)f->ng, 0 };
                inst = (long)f->nin++;
            }
            break;
        case EV_TAGLAST:   /* the group just drawn is an instance of its own (an effect polygon drawn from the ordering table) */
            if (f->ng && !f->new_group) {
                if (inst >= 0 && f->in[inst].ng && f->in[inst].g0 + f->in[inst].ng == f->ng) f->in[inst].ng--;
                GROW(f->in, f->nin, f->cin, 1);
                f->in[f->nin++] = (Inst){ (uint32_t)e->v, (uint32_t)f->ng - 1, 1 };
                f->g[f->ng - 1].strip = e->len; f->g[f->ng - 1].ord = 0;
            }
            inst = -1;
            f->new_group = 1;
            break;
        case EV_MOVIE:
            gs_movie_bind((uint32_t)e->v, (int)e->len);
            break;
        case EV_DISP:
            gs.disp_fbp = (uint32_t)(e->v & 0xFFFF); gs.disp_fbw = (uint32_t)((e->v >> 16) & 0xFFFF); gs.disp_psm = (uint32_t)(e->v >> 32);
            break;
        case EV_RTEX:
            ntex_define_capture((uint32_t)e->v, gs_capture_rt((uint32_t)(e->v >> 32) & 0x1FF, (uint32_t)(e->v >> 48) & 0x3F, e->len & 0xFFFF, e->len >> 16, (int)(e->v >> 56) & 1),
                                (int)(e->len & 0xFFFF), (int)(e->len >> 16));
            break;
        case EV_DOWNLOAD:
            gs_exec_download((uint8_t*)(uintptr_t)e->v, e->len);
            SDL_LockMutex(mtx);
            download_done = 1;
            SDL_BroadcastCondition(cond);
            SDL_UnlockMutex(mtx);
            break;
        }
    }
    gs_flush();
}

/* ---------------------------------------------------------------- interpolation */

static uint64_t* prev_xyz; static float* prev_q; static uint8_t* has_prev; static size_t cap_prev;
static int32_t* nat_prev; static size_t cap_nat;   /* per event of the matched frame: paired native strip in the previous one */
static const Frame* nat_prev_frame;

/* Q (1/w) of a vertex, or 0 if it has no RGBAQ of its own */
static float vtx_q(const Frame* f, uint32_t i)
{
    uint32_t e = f->stq[i * 2 + 1];
    float q;
    uint32_t b;
    if (e == NOEV) return 0.0f;
    b = (uint32_t)(f->ev[e].v >> 32);
    memcpy(&q, &b, 4);
    return q > 1e-12f && q < 1e12f ? q : 0.0f;
}
static const Frame* matched_for;

/* A mesh is blended if at least half of its vertices moved less than 128 pixels (teleports
   move all of them). Not the maximum or the mean: in a camera pan, vertices near the camera
   legitimately move much further on screen than the rest of a large environment mesh, and
   vertices beyond the GS coordinate range (geometry reaching behind the camera, clipped
   later) wrap around and seem to jump thousands of pixels. Single vertices moving more than
   VJUMP are not blended: they are off screen, and blending a wrapped coordinate would draw
   a spike across the screen. */
#define JUMP  (128 * 16)
#define VJUMP (512 * 16)
/* native strips are paired by mesh, strip and order, which is exact: the limit only has to
   catch a teleport within a camera cut. At 128 px, near walls in a fast turn (third person,
   mouse) lost their interpolation and visibly jumped against the rest */
#define NATIVE_JUMP (1024 * 16)
/* a menu's 2D piece (MENU_TAG) is paired by its look only, so another piece of the same look can
   stand in for one that wrapped round or left: the OPTION screen's two picture bands show the same
   pictures 368 px apart and scroll round, the map's lines all look alike. No menu moves a piece
   further than 320 px in a game frame (the status screen's panels sliding in): a pair further
   apart than that is two pieces, not one that moved */
#define MENU_JUMP (336 * 16)

static long moved(uint64_t x, uint64_t y)
{
    long dx = labs((long)(x & 0xFFFF) - (long)(y & 0xFFFF)), dy = labs((long)((x >> 16) & 0xFFFF) - (long)((y >> 16) & 0xFFFF));
    return dx > dy ? dx : dy;
}

static const Frame* sort_frame;
static int by_mesh(const void* a, const void* b)
{
    uint32_t x = sort_frame->in[*(const uint32_t*)a].mesh, y = sort_frame->in[*(const uint32_t*)b].mesh;
    return x < y ? -1 : x > y ? 1 : (int)(*(const uint32_t*)a - *(const uint32_t*)b);
}

static uint64_t vtx(const Frame* f, uint32_t i) { return f->ev[f->vx[i]].v; }

/* screen position of vertex v of a group in GS XYZ form (x | y << 16, 1/16 pixel); native
   strips are projected here (NO_POS: behind the camera) */
#define NO_POS UINT64_MAX
static uint64_t project(const NativeVertex* nv)
{
    float iw;
    long x, y;
    if (nv->w <= 0.0f) return NO_POS;
    iw = 16.0f / nv->w;
    x = (long)(nv->x * iw); y = (long)(nv->y * iw);
    x = x < 0 ? 0 : x > 0xFFFF ? 0xFFFF : x; y = y < 0 ? 0 : y > 0xFFFF ? 0xFFFF : y;
    return (uint64_t)x | ((uint64_t)y << 16);
}
static uint64_t gpos(const Frame* f, const Group* g, uint32_t v);
/* the native strips' screen positions of an executed frame, projected once (a frame is
   matched as the newer and then as the older one, each instance against every candidate),
   and each instance's box */
static void frame_spos(const Frame* cf)
{
    Frame* f = (Frame*)cf;   /* a cache of the frame's own data */
    if (f->spos_done) return;
    GROW(f->spos, 0, f->cspos, f->nspos);
    for (size_t i = 0; i < f->ng; i++) {
        const Group* g = &f->g[i];
        const NativeVertex* nv;
        if (g->key0 & RAW_KEY) {   /* already GS XYZ form */
            const NativeRawVertex* rv = raw_verts(f, g->v0);
            for (uint32_t v = 0; v < g->nv; v++) f->spos[g->sp + v] = rv[v].x | (uint64_t)rv[v].y << 16;
            continue;
        }
        if (!(g->key0 & NATIVE_KEY)) continue;
        nv = native_verts(f, g->v0);
        for (uint32_t v = 0; v < g->nv; v++) f->spos[g->sp + v] = project(&nv[v]);
    }
    GROW(f->box, 0, f->cbox, f->nin);
    for (size_t i = 0; i < f->nin; i++) {
        uint16_t* b = f->box[i];
        b[0] = b[1] = 0xFFFF; b[2] = b[3] = 0;
        for (uint32_t k = 0; k < f->in[i].ng; k++) {
            const Group* g = &f->g[f->in[i].g0 + k];
            for (uint32_t v = 0; v < g->nv; v++) {
                uint64_t q = gpos(f, g, v);
                uint16_t x = (uint16_t)q, y = (uint16_t)(q >> 16);
                if (q == NO_POS) continue;
                if (x < b[0]) b[0] = x;
                if (y < b[1]) b[1] = y;
                if (x > b[2]) b[2] = x;
                if (y > b[3]) b[3] = y;
            }
        }
    }
    f->spos_done = 1;
}
static uint64_t gpos(const Frame* f, const Group* g, uint32_t v)
{
    return g->key0 & (NATIVE_KEY | RAW_KEY) ? f->spos[g->sp + v] : vtx(f, g->v0 + v);
}

/* the least inst_pair can give for instances a (frame p) and b (frame c): each judged vertex pair is
   at least as far apart (moved) as their boxes, capped as inst_pair caps it */
static long inst_gap(const Frame* p, size_t a, const Frame* c, size_t b)
{
    const uint16_t *x = p->box[a], *y = c->box[b];
    long d = 0;
    if (x[0] > x[2] || y[0] > y[2]) return 0;   /* no position: nothing known */
    if ((long)x[0] - y[2] > d) d = (long)x[0] - y[2];
    if ((long)y[0] - x[2] > d) d = (long)y[0] - x[2];
    if ((long)x[1] - y[3] > d) d = (long)x[1] - y[3];
    if ((long)y[1] - x[3] > d) d = (long)y[1] - x[3];
    return d < 2 * JUMP ? d : 2 * JUMP;
}

/* Pair the groups of instance b (frame c) with those of instance a (frame p) by strip and
   order within the strip; strips culled in one of the frames are simply unpaired. Returns
   a score (mean screen movement of the paired vertices in 1/16 px, outliers capped), or -1 if
   none could be paired or fewer than half of the paired ones moved less than JUMP (for other
   than native strips: fewer than half of all of b's vertices paired and moved so). A mesh
   coming into view has most of its strips only in the newer frame (a wall beside the
   third-person camera: one strip of five drawn before); requiring half of its vertices paired
   left the whole mesh unblended, so it jumped against its neighbours and opened cracks along
   their edges for one image. TEX0 is not compared: the game streams textures into
   rotating GS memory, so a mesh's texture address changes between frames; the tags
   identify the geometry. */
static int32_t* pair; static size_t cap_pair;
static long inst_pair(const Frame* p, const Inst* a, const Frame* c, const Inst* b)
{
    int64_t sum = 0, n = 0, total = 0, near = 0, paired = 0;
    uint32_t j = 0;
    int native = 1;   /* all its groups native strips */
    for (uint32_t k = 0; k < b->ng; k++) {
        const Group* g = &c->g[b->g0 + k];
        pair[k] = -1;
        total += g->nv;
        native &= !!(g->key0 & NATIVE_KEY);
        for (uint32_t jj = j; jj < a->ng && jj < j + 64; jj++) {
            const Group* h = &p->g[a->g0 + jj];
            if (h->strip != g->strip || h->ord != g->ord) continue;
            if (h->key0 == g->key0 && h->nv == g->nv) {
                pair[k] = (int32_t)(a->g0 + jj);
                for (uint32_t v = 0; v < g->nv; v++) {
                    uint64_t pa = gpos(p, h, v), pb = gpos(c, g, v);
                    long d;
                    if (pa == NO_POS || pb == NO_POS) { total--; n--; continue; }   /* no screen position: not judged */
                    d = moved(pa, pb);
                    sum += d < 2 * JUMP ? d : 2 * JUMP;
                    near += d <= (MENU_TAG(b->mesh) ? MENU_JUMP : (g->key0 & NATIVE_KEY) && !(b->mesh & 1) ? NATIVE_JUMP : JUMP);   /* effects: JUMP (a slot reused within a frame) */
                }
                n += g->nv;
                paired += g->nv;
            }
            j = jj + 1;
            break;
        }
    }
    if (!paired) return -1;
    if (!native || MENU_TAG(b->mesh)) return n * 2 >= total && near * 2 >= total && n ? (long)(sum / n) : -1;   /* GS path, menus: half of all */
    if (!n) return 2 * JUMP;   /* no vertex in front of both cameras to judge it by: paired by its tags alone, last choice */
    return near * 2 >= n ? (long)(sum / n) : -1;
}

/* how far instance b (frame c) lies from instance a (frame p): the first vertex of each, d[0] x,
   d[1] y in 1/16 px (a menu piece: one group) */
#define AMBIG (4 * 16)
static void inst_move(const Frame* p, const Inst* a, const Frame* c, const Inst* b, long d[2])
{
    uint64_t pa = gpos(p, &p->g[a->g0], 0), pb = gpos(c, &c->g[b->g0], 0);
    d[0] = (long)(pb & 0xFFFF) - (long)(pa & 0xFFFF);
    d[1] = (long)((pb >> 16) & 0xFFFF) - (long)((pa >> 16) & 0xFFFF);
}

int stream_point_at(const Frame* prev, const Frame* cur, float t, float* x, float* y)
{
    const float *a = prev ? prev->pt : NULL, *b = cur->pt;
    *x = b[0];
    *y = b[1];
    if (a && t < 1.0f && prev->pt_flags == STREAM_PT_FRONT && cur->pt_flags == STREAM_PT_FRONT && a[2] * b[2] > 0) {
        float w = a[2] + (b[2] - a[2]) * t;   /* as the vertices: x*w, y*w and w blended */
        *x = (a[0] * a[2] + (b[0] * b[2] - a[0] * a[2]) * t) / w;
        *y = (a[1] * a[2] + (b[1] * b[2] - a[1] * a[2]) * t) / w;
    }
    return cur->pt_flags;
}

static int interp_stats(void)
{
    static int st = -1;
    if (st < 0) st = SDL_getenv("RECVX_INTERP_STATS") != NULL;
    return st;
}

/* The fixed or event camera jumped although the scene id stayed (an event camera switched
   before its number did, a teleport within a cut): the eye moved more than VIEW_MOVE units or
   the view turned more than 30 degrees in one game frame. Camera motion stays below (the
   game's smooth pans between cuts up to ~5 units and 6 degrees a frame, cuts from 20 units or
   from 11 degrees with 58 units). The third-person camera is not compared (pc_frame_view:
   no view): it turns fast under the mouse but never jumps. */
#define VIEW_MOVE 10.0f
#define VIEW_TURN 0.8660f   /* cos 30 degrees */
static int view_cut(const Frame* p, const Frame* c)
{
    float d2 = 0.0f, dot = 0.0f, la = 0.0f, lb = 0.0f;
    for (int k = 0; k < 3; k++) {
        float d = c->view[k] - p->view[k];
        d2 += d * d;
        dot += p->view[3 + k] * c->view[3 + k];
        la += p->view[3 + k] * p->view[3 + k]; lb += c->view[3 + k] * c->view[3 + k];
    }
    if (la < 0.5f || lb < 0.5f) return 0;   /* no camera yet */
    if (d2 <= VIEW_MOVE * VIEW_MOVE && dot >= VIEW_TURN) return 0;
    if (interp_stats()) printf("interp: view jump %.1f units, %.0f degrees\n", SDL_sqrtf(d2), SDL_acosf(dot > 1.0f ? 1.0f : dot) * 57.29578f);
    return 1;
}

/* A mesh none of whose instances was drawn in the previous frame cannot be blended vertex for
   vertex. Drawn as in the new frame it stood still on the screen while the camera's in-between
   images moved everything around it: with a moving camera (third person, a fixed camera
   panning) it jumped against the scene and sank into or rose out of the wall or floor it lies
   on. Enemies that animate by switching models are such meshes in every switch frame: the bugs
   of 9/17 cycle four models (en10.c, en71.c), all switching every third game frame. Instead the
   mesh moves with the camera: its view-space position in the new frame, put back into the
   previous frame's view (as if it stood still in the world), is its previous position
   (cam_move, nv_camera); its own motion in that one game frame is not blended. Not for effects
   (their first frame, mostly at something moving: a muzzle flash) or a mesh whose instances all
   moved too far to pair (the player when the third-person camera swings past her at a wall):
   those stay as in the new frame. Strips new in a matched mesh move with the camera only if the
   mesh stands still (inst_still; not parts of the player turning with the third-person camera).
   NAT_CAMERA in nat_prev. */
#define NAT_CAMERA (-2)
static float cam_a[9], cam_b[3];      /* view_prev = view_cur * A + b (row vectors) */
static const float *cam_cur, *cam_prev;
/* 1 if both frames have a camera and it differs; sets cam_a, cam_b */
static int cam_move(const Frame* p, const Frame* c)
{
    const float *r = c->cam, *q = p->cam;
    float inv[9], det;
    if (r[12] == 0.0f || q[12] == 0.0f || r[13] == 0.0f || q[13] == 0.0f || !memcmp(r, q, sizeof(c->cam))) return 0;
    inv[0] = r[4] * r[8] - r[5] * r[7]; inv[1] = r[2] * r[7] - r[1] * r[8]; inv[2] = r[1] * r[5] - r[2] * r[4];
    inv[3] = r[5] * r[6] - r[3] * r[8]; inv[4] = r[0] * r[8] - r[2] * r[6]; inv[5] = r[2] * r[3] - r[0] * r[5];
    inv[6] = r[3] * r[7] - r[4] * r[6]; inv[7] = r[1] * r[6] - r[0] * r[7]; inv[8] = r[0] * r[4] - r[1] * r[3];
    det = r[0] * inv[0] + r[1] * inv[3] + r[2] * inv[6];
    if (!(det > 1e-12f || det < -1e-12f)) return 0;
    for (int i = 0; i < 9; i++) inv[i] /= det;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) cam_a[i * 3 + j] = inv[i * 3] * q[j] + inv[i * 3 + 1] * q[3 + j] + inv[i * 3 + 2] * q[6 + j];
    for (int j = 0; j < 3; j++) cam_b[j] = q[9 + j] - (r[9] * cam_a[j] + r[10] * cam_a[3 + j] + r[11] * cam_a[6 + j]);
    cam_cur = r; cam_prev = q;
    return 1;
}
/* v (the new frame's) as the previous frame's camera would have drawn it */
static NativeVertex nv_camera(const NativeVertex* v)
{
    const float *r = cam_cur, *q = cam_prev;
    NativeVertex o = *v;
    float vx = (v->x - r[14] * v->w) / r[12], vy = (v->y - r[15] * v->w) / r[13], vz = v->w, x, y, z;
    x = vx * cam_a[0] + vy * cam_a[3] + vz * cam_a[6] + cam_b[0];
    y = vx * cam_a[1] + vy * cam_a[4] + vz * cam_a[7] + cam_b[1];
    z = vx * cam_a[2] + vy * cam_a[5] + vz * cam_a[8] + cam_b[2];
    o.x = q[14] * z + q[12] * x; o.y = q[15] * z + q[13] * y;   /* as native_glue.c pc_native_strip */
    o.z = -q[17] - q[16] * z; o.w = z;
    o.near_d = z - q[18]; o.far_d = q[19] - z;
    return o;
}

/* group g of the matched frame c moves with the camera (NAT_CAMERA): a native strip in 3D; a 2D
   polygon (w = 1, native_glue.c pc_native_prim) is not in the world. Returns its vertices. */
static size_t mark_camera(const Frame* c, const Group* g)
{
    const NativeVertex* v;
    if (!(g->key0 & NATIVE_KEY)) return 0;
    v = native_verts(c, g->v0);
    for (uint32_t k = 0; k < g->nv && k < 64; k++)
        if (v[k].w == 1.0f) return 0;
    nat_prev[g->v0] = NAT_CAMERA;
    return g->nv;
}

/* Does matched instance b (frame c, its strips paired in pair[]) stand still in the world, so that
   only the camera moved it? Its paired native vertices in front of both cameras, moved with the
   camera, land within a pixel of where they were. Then a strip of it that has no pair (culled in
   the previous frame: a wall strip coming into view) moves with the camera too; drawn as in the
   new frame it opened a crack along the edge it shares with the blended strips beside it. */
static int inst_still(const Frame* p, const Frame* c, const Inst* b)
{
    int n = 0;
    for (uint32_t k = 0; k < b->ng; k++) {
        const Group* g = &c->g[b->g0 + k];
        const NativeVertex *pv, *cv;
        if (pair[k] < 0 || !(g->key0 & NATIVE_KEY)) continue;
        pv = native_verts(p, p->g[pair[k]].v0);
        cv = native_verts(c, g->v0);
        for (uint32_t v = 0; v < g->nv && v < 64; v++) {
            NativeVertex e;
            float dx, dy;
            if (cv[v].near_d < 0.0f || pv[v].near_d < 0.0f) continue;
            e = nv_camera(&cv[v]);
            if (e.near_d < 0.0f) return 0;
            dx = e.x / e.w - pv[v].x / pv[v].w;
            dy = e.y / e.w - pv[v].y / pv[v].w;
            if (dx * dx + dy * dy > 1.0f) return 0;
            n++;
        }
    }
    return n >= 3;
}

/* match frame c against p; 1 if in-between frames make sense */
int stream_match(const Frame* p, const Frame* c)
{
    static uint32_t* order; static uint8_t* used; static size_t cap_order;
    size_t tagged[2] = { 0, 0 }, matched[2] = { 0, 0 }, no_inst = 0, unpaired = 0, with_cam = 0;   /* [1]: effects (odd tags, pc_effect_tag) */
    int moving;
    long menu_move[2] = { 0, 0 };   /* how the last matched menu piece moved (1/16 px) */
    matched_for = NULL;
    if (!p || !c || c->continuation || p->partial || (!c->nvx && !c->nin)) return 0;
    if (p->scene != c->scene) {
        if (interp_stats()) printf("interp: scene %010llx -> %010llx\n", (unsigned long long)p->scene, (unsigned long long)c->scene);
        return 0;
    }
    if (view_cut(p, c)) return 0;
    moving = cam_move(p, c);
    frame_spos(p);
    frame_spos(c);
    if (c->nvx > cap_prev) {
        cap_prev = c->nvx * 2;
        prev_xyz = xrealloc(prev_xyz, cap_prev * sizeof(*prev_xyz));
        prev_q = xrealloc(prev_q, cap_prev * sizeof(*prev_q));
        has_prev = xrealloc(has_prev, cap_prev);
    }
    memset(has_prev, 0, c->nvx);
    if (c->nev > cap_nat) { cap_nat = c->nev * 2; nat_prev = xrealloc(nat_prev, cap_nat * sizeof(*nat_prev)); }
    for (size_t i = 0; i < c->nev; i++) nat_prev[i] = -1;
    if (p->nin > cap_order) {
        cap_order = p->nin * 2;
        order = xrealloc(order, cap_order * sizeof(*order));
        used = xrealloc(used, cap_order);
    }
    for (size_t i = 0; i < p->nin; i++) order[i] = (uint32_t)i;
    sort_frame = p;
    qsort(order, p->nin, sizeof(*order), by_mesh);
    memset(used, 0, p->nin);

    for (size_t i = 0; i < c->nin; i++) {
        const Inst* b = &c->in[i];
        size_t lo = 0, hi = p->nin, best = (size_t)-1;
        long best_d = 2 * JUMP + 1, next_d = 2 * JUMP + 1;
        int fx = b->mesh & 1;
        if (!b->ng) continue;
        if (b->ng > cap_pair) { cap_pair = b->ng * 2; pair = xrealloc(pair, cap_pair * sizeof(*pair)); }
        for (uint32_t k = 0; k < b->ng; k++) tagged[fx] += c->g[b->g0 + k].nv;
        while (lo < hi) {   /* first instance of this mesh in p */
            size_t m = (lo + hi) / 2;
            if (p->in[order[m]].mesh < b->mesh) lo = m + 1; else hi = m;
        }
        if (!MENU_TAG(b->mesh)) {
            /* the same choice as below (the nearest, the first in order of the nearest), with fewer
               candidates judged: the one whose box is nearest first, then only those whose boxes could
               do as well (a crowd of 30 zombies: 3000 judged a frame before, 750 now) */
            size_t first = (size_t)-1;
            long gap = 2 * JUMP + 1, d;
            for (size_t m = lo; m < p->nin && p->in[order[m]].mesh == b->mesh; m++)
                if (!used[m] && (d = inst_gap(p, order[m], c, i)) < gap) { gap = d; first = m; }
            if (first != (size_t)-1 && (d = inst_pair(p, &p->in[order[first]], c, b)) >= 0) { best_d = d; best = first; }
            for (size_t m = lo; m < p->nin && p->in[order[m]].mesh == b->mesh; m++) {
                if (used[m] || m == first) continue;
                gap = inst_gap(p, order[m], c, i);
                if (gap > best_d || (gap == best_d && m > best)) continue;
                d = inst_pair(p, &p->in[order[m]], c, b);
                if (d >= 0 && (d < best_d || (d == best_d && m < best))) { best_d = d; best = m; }
            }
        } else
        for (size_t m = lo; m < p->nin && p->in[order[m]].mesh == b->mesh && best_d; m++) {
            const Inst* a = &p->in[order[m]];
            long d;
            if (used[m]) continue;
            d = inst_pair(p, a, c, b);
            if (d >= 0 && d < best_d) { next_d = best_d; best_d = d; best = m; }
            else if (d >= 0 && d < next_d) next_d = d;
        }
        if (MENU_TAG(b->mesh) && best != (size_t)-1) {
            /* a menu piece whose look is there more than once (the "On" of every row of a list):
               when two candidates are about as near (the list scrolled a row) the nearer one is
               a guess, and a wrong one slides a row's text over the next. Take the candidate that
               moved as the menu piece matched before it did (its row), or none */
            if (next_d - best_d < AMBIG) {
                long err_best = AMBIG;
                best = (size_t)-1;
                for (size_t m = lo; m < p->nin && p->in[order[m]].mesh == b->mesh; m++) {
                    long d[2], err;
                    if (used[m] || inst_pair(p, &p->in[order[m]], c, b) < 0) continue;
                    inst_move(p, &p->in[order[m]], c, b, d);
                    err = labs(d[0] - menu_move[0]) > labs(d[1] - menu_move[1]) ? labs(d[0] - menu_move[0]) : labs(d[1] - menu_move[1]);
                    if (err < err_best) { err_best = err; best = m; }
                }
            }
            if (best != (size_t)-1) inst_move(p, &p->in[order[best]], c, b, menu_move);
        }
        if (best == (size_t)-1) {
            for (uint32_t k = 0; k < b->ng; k++) {
                const Group* g = &c->g[b->g0 + k];
                no_inst += g->nv;
                if (moving && !fx && (lo == p->nin || p->in[order[lo]].mesh != b->mesh)) with_cam += mark_camera(c, g);   /* no instance of it at all */
            }
            continue;
        }
        used[best] = 1;
        inst_pair(p, &p->in[order[best]], c, b);   /* the pairs of the chosen instance */
        {
            int lone = 0;   /* native strips without a pair */
            for (uint32_t k = 0; k < b->ng; k++) {
                const Group *g = &c->g[b->g0 + k], *h;
                if (pair[k] < 0) { unpaired += g->nv; lone += !!(g->key0 & NATIVE_KEY); continue; }
                h = &p->g[pair[k]];
                if (g->key0 & RAW_KEY) {   /* a menu's sprites: blended in replay unless they stand still */
                    const NativeRawVertex *ra = raw_verts(p, h->v0), *rb = raw_verts(c, g->v0);
                    for (uint32_t v = 0; v < g->nv; v++) if (ra[v].x != rb[v].x || ra[v].y != rb[v].y) { nat_prev[g->v0] = (int32_t)h->v0; break; }
                    matched[fx] += g->nv;
                    continue;
                }
                if (g->key0 & NATIVE_KEY) {   /* native strip: blended vertex for vertex in replay, unless it stands still */
                    if (!nv_same_pos(native_verts(p, h->v0), native_verts(c, g->v0), g->nv)) nat_prev[g->v0] = (int32_t)h->v0;
                    matched[fx] += g->nv;
                    continue;
                }
                for (uint32_t v = 0; v < g->nv; v++) {
                    prev_xyz[g->v0 + v] = vtx(p, h->v0 + v);
                    prev_q[g->v0 + v] = vtx_q(p, h->v0 + v);
                    has_prev[g->v0 + v] = moved(prev_xyz[g->v0 + v], vtx(c, g->v0 + v)) <= VJUMP;
                }
                matched[fx] += g->nv;
            }
            if (lone && moving && !fx && inst_still(p, c, b))   /* its strips new in this frame go with it */
                for (uint32_t k = 0; k < b->ng; k++)
                    if (pair[k] < 0) with_cam += mark_camera(c, &c->g[b->g0 + k]);
        }
    }
    {
        if (interp_stats()) printf("interp: meshes %zu/%zu, vertices %zu/%zu tagged matched, effects %zu/%zu, %zu total; unmatched: %zu no instance, %zu unpaired groups, %zu moved with the camera\n", c->nin, p->nin, matched[0], tagged[0], matched[1], tagged[1], c->nvx, no_inst, unpaired, with_cam);
    }
    /* most meshes lost or jumping although the camera did not cut: a scene change */
    if (!matched[0] || matched[0] * 2 < tagged[0]) return 0;
    matched_for = c;
    nat_prev_frame = p;
    return 1;
}

static uint64_t blend_xyz(int reg, uint64_t a, uint64_t b, float t)
{
    uint32_t ax = a & 0xFFFF, ay = (a >> 16) & 0xFFFF, bx = b & 0xFFFF, by = (b >> 16) & 0xFFFF;
    uint32_t x = (uint32_t)((float)ax + ((float)bx - (float)ax) * t + 0.5f) & 0xFFFF;
    uint32_t y = (uint32_t)((float)ay + ((float)by - (float)ay) * t + 0.5f) & 0xFFFF;
    if (reg == 0x04 || reg == 0x0c) {   /* XYZF: 24-bit Z, fog kept from the new frame */
        double az = (double)((a >> 32) & 0xFFFFFF), bz = (double)((b >> 32) & 0xFFFFFF);
        uint64_t z = (uint64_t)(az + (bz - az) * t + 0.5) & 0xFFFFFF;
        return x | ((uint64_t)y << 16) | (z << 32) | (b & 0xFF00000000000000ull);
    } else {
        double az = (double)(uint32_t)(a >> 32), bz = (double)(uint32_t)(b >> 32);
        uint64_t z = (uint64_t)(az + (bz - az) * t + 0.5) & 0xFFFFFFFFull;
        return x | ((uint64_t)y << 16) | (z << 32);
    }
}

/* draw the in-between image of an executed, matched frame (t = 0 previous, 1 current).
   Vertices are blended in homogeneous space: with w = 1/Q, screen x*w, y*w and w are linear
   in the vertex's view-space position, so blending them and dividing gives the position the
   in-between transform would project to, and the texture coordinates (S = s/w, T = t/w,
   Q = 1/w) are rescaled to the blended w. Blending screen positions alone while keeping the
   newer frame's S, T, Q maps textures with the wrong perspective: on large surfaces seen at
   a steep angle (walls next to the camera) the texture swims between real and in-between
   frames. Vertices without their own Q (2D, sprites) are blended linearly. */
typedef struct { uint64_t xyz, st, rgbaq; int has_st, has_q; } Blend;

static Blend blend_vertex(const Frame* r, size_t vi, float t)
{
    const Ev* e = &r->ev[r->vx[vi]];
    uint64_t a = prev_xyz[vi], b = e->v;
    float qa = prev_q[vi], qb = vtx_q(r, (uint32_t)vi);
    Blend o;
    o.has_st = o.has_q = 0;
    if (qa == 0.0f || qb == 0.0f) { o.xyz = blend_xyz(e->reg, a, b, t); return o; }
    {
        double wa = 1.0 / qa, wb = 1.0 / qb, w = wa + (wb - wa) * t, s = (double)qb * w;   /* s = q_b / q_t */
        double xa = (double)(a & 0xFFFF), ya = (double)((a >> 16) & 0xFFFF), xb = (double)(b & 0xFFFF), yb = (double)((b >> 16) & 0xFFFF);
        double x = (xa * wa + (xb * wb - xa * wa) * t) / w, y = (ya * wa + (yb * wb - ya * wa) * t) / w;
        uint64_t lin = blend_xyz(e->reg, a, b, t);   /* Z (and fog) */
        float q = (float)(1.0 / w), S, T;
        uint32_t qb32, sb, tb;
        uint64_t st = r->ev[r->stq[vi * 2]].v, rq = r->ev[r->stq[vi * 2 + 1]].v;
        if (x < 0) x = 0;
        if (x > 65535) x = 65535;
        if (y < 0) y = 0;
        if (y > 65535) y = 65535;
        o.xyz = (lin & ~0xFFFFFFFFull) | (uint64_t)(uint32_t)(x + 0.5) | ((uint64_t)(uint32_t)(y + 0.5) << 16);
        memcpy(&qb32, &q, 4);
        o.rgbaq = (rq & 0xFFFFFFFFull) | ((uint64_t)qb32 << 32);
        o.has_q = 1;
        if (r->stq[vi * 2] != NOEV) {   /* S, T scale with q: S_t = S_b * q_t / q_b */
            float k = (float)(1.0 / s);
            uint32_t s0 = (uint32_t)st, t0 = (uint32_t)(st >> 32);
            memcpy(&S, &s0, 4); memcpy(&T, &t0, 4);
            S *= k; T *= k;
            memcpy(&sb, &S, 4); memcpy(&tb, &T, 4);
            o.st = (uint64_t)sb | ((uint64_t)tb << 32);
            o.has_st = 1;
        }
    }
    return o;
}

void stream_replay(const Frame* r, float t)
{
    GsState end = gs;
    uint8_t clut_end[GS_CLUT_STATE_SIZE];
    uint8_t ntex_end[NTEX_STATE_SIZE];
    size_t vi = 0, bvi = (size_t)-1;
    Blend bl = { 0 };
    if (r != matched_for) return;
    prof_push(PROF_INTERP);
    gs_clut_state_save(clut_end);
    gs = r->start;
    gs_reg_serial++;
    gs_clut_state_restore(r->clut_start);
    ntex_state_save(ntex_end);
    ntex_state_restore(r->ntex_start);
    gs_replaying = 1;
    gs_replay_begin();
    int in_def = 0;
    gs_tex_epoch = 0;
    for (size_t i = 0; i < r->nev; i++) {
        const Ev* e = &r->ev[i];
        if (tex_event(e)) gs_tex_epoch = (uint32_t)i + 1;
        uint64_t v;
        if (e->kind == EV_TEXDEF) { in_def = 1; continue; }
        if (e->kind == EV_TEXDEF_END) { in_def = 0; continue; }
        if (in_def) continue;
        if (e->kind == EV_TEXBIND) { ntex_bind((uint32_t)e->v, (uint32_t)(e->v >> 32) & 0x3FFF, (uint32_t)(e->v >> 46) & 0x3FFF); continue; }
        if (e->kind == EV_MOVIE) { gs_movie_bind((uint32_t)e->v, (int)e->len); continue; }
        if (e->kind == EV_RTEX) continue;    /* draws bind the executed frame's capture (replay resources) */
        if (e->kind == EV_IMAGE) continue;   /* a replay writes no GS memory: its textures and render-target
                                                  reloads are the executed frame's (gs_gl.c, replay resources) */
        if (e->kind == EV_RAW) {
            const NativeRawVertex* cur = raw_verts(r, (uint32_t)i);
            if (nat_prev[i] >= 0) {   /* a menu's sprites that moved (stream_match): the in-between position */
                const NativeRawVertex* prv = raw_verts(nat_prev_frame, (uint32_t)nat_prev[i]);
                NativeRawVertex bl[64];
                int n = e->len > 64 ? 64 : (int)e->len;
                for (int k = 0; k < n; k++) {
                    bl[k] = cur[k];
                    bl[k].x = (uint16_t)(prv[k].x + ((float)cur[k].x - prv[k].x) * t + 0.5f);
                    bl[k].y = (uint16_t)(prv[k].y + ((float)cur[k].y - prv[k].y) * t + 0.5f);
                }
                gs_draw_raw((unsigned int)(e->v >> 32), bl, n);
            } else
                gs_draw_raw((unsigned int)(e->v >> 32), cur, (int)e->len);
            continue;
        }
        if (e->kind == EV_NATIVE) {
            /* native strip: blend each vertex's homogeneous position (linear in view space, so
               this is the in-between camera and object transform) and attributes, then cull,
               clip and draw */
            const NativeVertex* cur = native_verts(r, (uint32_t)i);
            if (nat_prev[i] >= 0 || nat_prev[i] == NAT_CAMERA) {
                const NativeVertex* prv = nat_prev[i] >= 0 ? native_verts(nat_prev_frame, (uint32_t)nat_prev[i]) : NULL;
                NativeVertex bl[64], pv;
                int n = e->len > 64 ? 64 : (int)e->len;
                for (int k = 0; k < n; k++) {
                    if (!prv) pv = nv_camera(&cur[k]);
                    bl[k] = nv_blend_pos(prv ? &prv[k] : &pv, &cur[k], t);
                }
                native_draw((unsigned int)(e->v >> 32), bl, n, -1);
            } else if (r != cached_for || !gs_draw_native_cached((unsigned int)(e->v >> 32), i))   /* as the executed frame */
                native_draw((unsigned int)(e->v >> 32), cur, (int)e->len, -1);
            continue;
        }
        if (e->kind != EV_REG) continue;
        v = e->v;
        if (vi < r->nvx && has_prev[vi] && (i == r->vx[vi] || i == r->stq[vi * 2] || i == r->stq[vi * 2 + 1])) {
            if (bvi != vi) { bl = blend_vertex(r, vi, t); bvi = vi; }
            if (i == r->vx[vi]) v = bl.xyz;
            else if (i == r->stq[vi * 2]) { if (bl.has_st) v = bl.st; }
            else if (bl.has_q) v = bl.rgbaq;
        }
        if (vi < r->nvx && r->vx[vi] == i) vi++;
        gs_exec_reg(e->reg, v);
    }
    gs_flush();
    gs_replaying = 0;
    gs = end;
    gs_reg_serial++;
    gs_clut_state_restore(clut_end);
    ntex_state_restore(ntex_end);
    prof_pop();
}
