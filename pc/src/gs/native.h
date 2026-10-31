/* Native 3D path (pc/glue/native_glue.c): GPU-projected triangles in the GS stream. */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    float x, y, z, w;           /* homogeneous GS screen position (pixels before XYOFFSET) and GS depth, times w */
    float s, t;                 /* texture coordinates (0..1), interpolated perspective-correct */
    float fog;                  /* GS fog coefficient as computed (the GS takes its low 8 bits) */
    float near_d, far_d;        /* clip distances: >= 0 inside the near/far planes */
    uint8_t r, g, b, a;         /* GS colour (0x80 = 1.0) */
    uint8_t cull;               /* bit 0: normal faces away (a triangle of three such is skipped,
                                   SingleMain); bit 1: the triangle ending here is skipped;
                                   bit 2 (on the last vertex): the triangle is drawn only outside
                                   the 4:3 screen, in the hor+ margins (NATIVE_43_X0..X1) */
} NativeVertex;
#define NATIVE_43_X0 1728.0f   /* the 4:3 screen (the PS2's 640-wide display), GS x */
#define NATIVE_43_X1 2368.0f

/* a + (b - a) * k for every attribute (all linear in view space) */
static inline NativeVertex native_lerp(const NativeVertex* a, const NativeVertex* b, float k)
{
    NativeVertex o = *b;
    const float* fa = &a->x; const float* fb = &b->x; float* fo = &o.x;
    for (int i = 0; i < 9; i++) fo[i] = fa[i] + (fb[i] - fa[i]) * k;   /* x y z w s t fog near_d far_d */
    o.r = (uint8_t)(a->r + (b->r - a->r) * k + 0.5f); o.g = (uint8_t)(a->g + (b->g - a->g) * k + 0.5f);
    o.b = (uint8_t)(a->b + (b->b - a->b) * k + 0.5f); o.a = (uint8_t)(a->a + (b->a - a->a) * k + 0.5f);
    return o;
}

/* distance to plane 0 near (near_d >= 0), 1 far (far_d >= 0), 2 left of the 4:3 screen, 3 right of it */
static inline float native_plane_d(const NativeVertex* v, int plane)
{
    return plane == 0 ? v->near_d : plane == 1 ? v->far_d : plane == 2 ? NATIVE_43_X0 * v->w - v->x : v->x - NATIVE_43_X1 * v->w;
}

/* clip a polygon against native_plane_d >= 0; returns the count */
static inline int native_clip_plane(const NativeVertex* in, int n, NativeVertex* out, int plane)
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const NativeVertex *a = &in[i], *b = &in[(i + 1) % n];
        float da = native_plane_d(a, plane), db = native_plane_d(b, plane);
        if (da >= 0) out[m++] = *a;
        if ((da >= 0) != (db >= 0)) out[m++] = native_lerp(a, b, da / (da - db));
    }
    return m;
}

/* game thread: triangles with GS PRIM bits (type 3 list, 4 strip, 5 fan; TME/FGE/ABE/IIP/
   FST/CTXT); see NativeVertex.cull */
void native_strip(unsigned int prim, const NativeVertex* v, int n);
/* game thread: a strip drawn later from GS data (ordering-table entries, see
   ps2_dummy.c Ps2AddPrim3DEx): the record is stored and a write of the returned number to
   GS register NATIVE_MARKER_REG takes the place of its vertices in the GS packet, so it is
   emitted exactly where the GS would have drawn it. mesh/strip: identity for interpolation
   (0: draw within the current mesh tag). */
#define NATIVE_MARKER_REG 0x7F
/* GS register write that follows an ordering-table entry drawn by the GS path inside an effect
   (ps2_dummy.c Ps2AddOT): the primitive group just drawn is the effect's (low 32 bits: its
   tag, high: the polygon's number in it), for frame interpolation (stream_tag_last) */
#define STREAM_TAG_REG 0x7E
uint32_t native_defer(unsigned int prim, const NativeVertex* v, int n, uint32_t mesh, uint32_t strip);
void     native_marker(uint64_t v);
/* Points, lines and sprites (and anything else the GS would draw from its registers): the
   values the GS registers would receive, drawn by the same vertex kick logic (gs.c
   gs_draw_raw), so sprites keep the renderer's GS semantics (e.g. widescreen fills). */
typedef struct {
    uint16_t x, y;              /* XYZ in 12.4 fixed point, before XYOFFSET */
    uint32_t z;
    float s, t, q;              /* ST / RGBAQ.Q */
    uint16_t u, v;              /* UV, 10.4 fixed point */
    uint8_t r, g, b, a, f;
    uint8_t adc;                /* 1: the vertex does not draw (XYZ3 / ADC) */
} NativeRawVertex;
uint32_t native_defer_raw(unsigned int prim, const NativeRawVertex* v, int n, uint32_t mesh);   /* mesh: a menu tag (view_glue.c pc_menu_tag) or 0 */
#define NATIVE_RAW_3D 0x800     /* prim: drawn in 3D (particles), not 2D (stream.c aa_split; gs_draw_raw ignores it) */
void     gs_draw_raw(unsigned int prim, const NativeRawVertex* v, int n);

/* render thread: draw n vertices (triangle list) with the current GS context; ev >= 0: the
   executed frame's strip of that stream event, kept for its replays (gs_draw_native_cached) */
void gs_draw_native(unsigned int prim, const NativeVertex* v, int n, long ev);
void gs_native_cache_begin(size_t nev);                 /* an executed frame starts */
int  gs_draw_native_cached(unsigned int prim, size_t ev);   /* replay: 0 if not kept */
