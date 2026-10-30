/* Native textures, game thread side (see pc/src/gs/textures.c): identity of TIM2 textures.
   A TIM2 is identified by its address plus a check over its header's sizes and format, its whole CLUT (games
   animate palettes in place) and a sparse sample of its image; the first upload of an
   identity is recorded as a definition, later ones only bind it. Disc reads over an address
   forget what was there (pc_tex_invalidate from cdvd.c). */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../src/gs/stream.h"
#include "../src/gs/textures.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { uintptr_t ptr; uint32_t check, id, bytes; uint64_t last; } Known;
/* set-associative by address: direct-mapped, two TIM2s of a room on the same slot evicted each
   other every frame (courtyard: 2..6 definitions recorded again per frame) */
#define KNOWN_WAYS 8
#define KNOWN_SET_BITS 10
#define NKNOWN (KNOWN_WAYS << KNOWN_SET_BITS)
#define KNOWN_BYTES_MAX (96u << 20)   /* definitions kept by the render thread (32-bit address space) */
static Known known[NKNOWN];
static uint64_t known_bytes, known_clock;

/* definition ids: NTEX_ID_SLOTS slots, each with a generation (textures.h); every id alive is
   in known[] or rtex[] */
#define NRTEX 32
_Static_assert(NKNOWN + NRTEX <= NTEX_ID_SLOTS, "NTEX_ID_SLOTS");
static uint32_t id_gen[NTEX_ID_SLOTS];
static uint16_t id_free[NTEX_ID_SLOTS];
static int id_nfree = -1;

static uint32_t id_new(void)
{
    uint32_t s;
    if (id_nfree < 0)
        for (id_nfree = 0; id_nfree < (int)NTEX_ID_SLOTS; id_nfree++) id_free[id_nfree] = (uint16_t)(NTEX_ID_SLOTS - 1 - id_nfree);
    s = id_free[--id_nfree];
    id_gen[s] = id_gen[s] % ((1u << (32 - NTEX_ID_BITS)) - 1) + 1;   /* 1.., never id 0 */
    return id_gen[s] << NTEX_ID_BITS | s;
}

static void id_release(uint32_t id)
{
    stream_tex_forget(id);   /* before any reuse of the slot in the stream */
    id_free[id_nfree++] = (uint16_t)(id & (NTEX_ID_SLOTS - 1));
}

static void forget(Known* k)
{
    if (!k->id) return;
    id_release(k->id);
    known_bytes -= k->bytes;
    k->id = 0; k->ptr = 0;
}

int pc_native_tex(void)
{
    extern int pc_config_native_tex(void);
    static int env = -2;
    if (env == -2) env = getenv("RECVX_NATIVE_TEX") ? atoi(getenv("RECVX_NATIVE_TEX")) != 0 : -1;
    return env >= 0 ? env : pc_config_native_tex();
}

static uint32_t mix(uint32_t h, uint32_t v) { h ^= v; h *= 0x01000193u; return h; }

static uint32_t check_of(const TIM2_PICTUREHEADER* ph)
{
    const uint32_t* w = (const uint32_t*)ph;
    const uint32_t* img = (const uint32_t*)((const char*)ph + ph->HeaderSize);
    const uint32_t* clut = (const uint32_t*)((const char*)ph + ph->HeaderSize + ph->ImageSize);
    uint32_t h = 0x811C9DC5u, n;
    for (n = 0; n < 6; n++) h = mix(h, w[n]);   /* TotalSize..ImageHeight; Set_GsTex rewrites the GS fields after them */
    for (n = 0; n < ph->ClutSize / 4u; n++) h = mix(h, clut[n]);
    for (n = 0; n < ph->ImageSize / 4u; n += 61) h = mix(h, img[n]);
    return h;
}

/* one of the game's font pages: the first four textures of the effect list, which
   bhDispFont/bhDispFontEx (message.c) draw every message, item name and description from
   (bhSetFontTexture); HD texture packs replace them only with hd_textures_font */
static int is_font(const void* tim2)
{
    if (!(sys->ss_flg & 0x40)) return 0;   /* not loaded (the title and other screens release it, and the
                                              texture slots its list points to are reused) */
    for (int i = 0; i < 4; i++) {
        const NJS_TEXMEMLIST* m = (const NJS_TEXMEMLIST*)(uintptr_t)sys->ef_tex[i].texaddr;
        if (m && m->texinfo.texsurface.pSurface == tim2) return 1;
    }
    return 0;
}

/* TIM2s the port cuts its own menu entries from (MULTIPLAYER, ENHANCED SETTINGS, RETURN TO
   TITLE: letters of the title and option textures): HD texture packs keep them original even with
   hd_textures_ui, so the port's entries match the letters around them. Forgotten with a disc
   read over them (pc_tex_invalidate). */
#define NKEEP 16
static const void* keep[NKEEP];
static int nkeep;

void pc_tex_keep_original(int texno)
{
    const NJS_TEXMEMLIST* m;
    const void* t;
    if (!Ps2_current_texlist || texno < 0 || texno >= (int)Ps2_current_texlist->nbTexture) return;
    m = (const NJS_TEXMEMLIST*)(uintptr_t)Ps2_current_texlist->textures[texno].texaddr;
    if (!m || !(t = m->texinfo.texsurface.pSurface)) return;
    for (int i = 0; i < NKEEP; i++) if (keep[i] == t) return;
    keep[nkeep++ % NKEEP] = t;
}

static unsigned int tex_flags(const void* tim2)
{
    unsigned int f = is_font(tim2) ? NTEX_FONT : 0;
    for (int i = 0; i < NKEEP; i++) if (keep[i] == tim2) f |= NTEX_KEEP;
    return f;
}

/* returns the identity; *is_new: record a definition now (uploads between this and
   pc_tex_end) */
uint32_t pc_tex_begin(const void* tim2, const void* phead, unsigned int image_adr, unsigned int clut_adr, int* is_new)
{
    const TIM2_PICTUREHEADER* ph = (const TIM2_PICTUREHEADER*)phead;
    uintptr_t p = (uintptr_t)tim2;
    Known* set = &known[(((uint32_t)(p >> 4) * 2654435761u) >> (32 - KNOWN_SET_BITS)) * KNOWN_WAYS], *k = set;
    unsigned int flags = tex_flags(tim2);
    uint32_t c = check_of(ph) ^ flags * 0x9E3779B9u;   /* a tag that changes makes a new definition */
    known_clock++;
    for (int i = 0; i < KNOWN_WAYS; i++)   /* this address, else a free or the least recently used way */
        if (set[i].ptr == p && set[i].id) { k = &set[i]; break; }
        else if (k->id && (!set[i].id || set[i].last < k->last)) k = &set[i];
    if (k->ptr == p && k->check == c && k->id) { *is_new = 0; k->last = known_clock; return k->id; }
    forget(k);   /* the slot's previous texture (another address, or changed content) */
    while (known_bytes > KNOWN_BYTES_MAX) {   /* least recently used */
        Known* o = NULL;
        for (int i = 0; i < NKNOWN; i++) if (known[i].id && (!o || known[i].last < o->last)) o = &known[i];
        if (!o) break;
        forget(o);
    }
    k->ptr = p; k->check = c; k->id = id_new(); k->last = known_clock;
    k->bytes = ph->TotalSize + 256u;
    known_bytes += k->bytes;
    *is_new = 1;
    stream_tex_def_begin(k->id, image_adr, clut_adr, (ph->ImageSize + 8191u) / 8192u + 1u, flags);
    return k->id;
}

void pc_tex_end(uint32_t id, unsigned int image_adr, unsigned int clut_adr, int is_new)
{
    if (is_new) stream_tex_def_end();
    stream_tex_bind(id, image_adr, clut_adr);
}

/* Render textures (StoreRenderTex): the game reads the frame buffer into a TIM2 only to
   upload it again, so on the PC the render thread copies the render target into a texture
   (stream_rtex_capture) and the TIM2's image is never written - except for a marker in its
   first 16 bytes naming the capture (id and generation). An upload of a TIM2 carrying a
   current marker binds the capture instead (wherever the TIM2 has moved or been copied to);
   one with a stale or unknown marker is uploaded as is. An address keeps its id across
   captures (the render thread replaces the texture). */
#define RTEX_MAGIC 0x58455452u   /* "RTEX" */
typedef struct { uintptr_t ptr; uint32_t id, gen; uint64_t last; } RTex;
static RTex rtex[NRTEX];

int pc_native_rtex(void)
{
    static int on = -1;
    if (on < 0) on = getenv("RECVX_NATIVE_RTEX") ? atoi(getenv("RECVX_NATIVE_RTEX")) != 0 : 1;
    return on;
}

int pc_rtex_wide;

void pc_rtex_capture(void* tim2, void* image, unsigned int w, unsigned int h)
{
    uintptr_t p = (uintptr_t)tim2;
    uint32_t* m = (uint32_t*)image;
    RTex* r = NULL;
    for (int i = 0; i < NKNOWN; i++) if (known[i].id && known[i].ptr == p) forget(&known[i]);   /* what was there */
    for (int i = 0; i < NRTEX && !r; i++) if (rtex[i].id && rtex[i].ptr == p) r = &rtex[i];
    for (int i = 0; i < NRTEX && !r; i++) if (!rtex[i].id) r = &rtex[i];
    if (!r) {   /* full: the least recently used one */
        r = &rtex[0];
        for (int i = 1; i < NRTEX; i++) if (rtex[i].last < r->last) r = &rtex[i];
        id_release(r->id);
        r->id = 0;
    }
    if (!r->id) { r->id = id_new(); r->gen = 0; }
    r->ptr = p; r->gen++; r->last = ++known_clock;
    m[0] = RTEX_MAGIC; m[1] = r->id; m[2] = r->gen; m[3] = ~RTEX_MAGIC;
    static int trace = -1;
    if (trace < 0) trace = getenv("RECVX_TRACE_NTEX") != NULL;
    if (trace) fprintf(stderr, "rtex: capture %p -> id %u gen %u (game frame %u)\n", tim2, r->id, r->gen, (unsigned int)stream_game_frames());
    stream_rtex_capture(r->id, 0, 10, w, h, pc_rtex_wide);   /* StoreRenderTex reads FBP 0, FBW 10 */
    pc_rtex_wide = 0;
}

int pc_rtex_bind(const void* image, unsigned int image_adr, unsigned int clut_adr)
{
    const uint32_t* m = (const uint32_t*)image;
    if (m[0] != RTEX_MAGIC || m[3] != ~RTEX_MAGIC) return 0;
    for (int i = 0; i < NRTEX; i++)
        if (rtex[i].id && rtex[i].id == m[1] && rtex[i].gen == m[2]) {
            rtex[i].last = ++known_clock;
            stream_tex_bind(rtex[i].id, image_adr, clut_adr);
            return 1;
        }
    {
        static int n;
        if (n++ < 10) fprintf(stderr, "rtex: TIM2 with a stale capture marker (id %u gen %u) uploaded from memory\n", m[1], m[2]);
    }
    return 0;
}

void pc_tex_invalidate(const void* dst, unsigned int bytes)
{
    uintptr_t lo = (uintptr_t)dst, hi = lo + bytes;
    for (int i = 0; i < NKNOWN; i++) if (known[i].id && known[i].ptr >= lo && known[i].ptr < hi) forget(&known[i]);
    for (int i = 0; i < NKEEP; i++) if ((uintptr_t)keep[i] >= lo && (uintptr_t)keep[i] < hi) keep[i] = NULL;
    for (int i = 0; i < NRTEX; i++)
        if (rtex[i].id && rtex[i].ptr >= lo && rtex[i].ptr < hi) { id_release(rtex[i].id); rtex[i].id = 0; }
}
