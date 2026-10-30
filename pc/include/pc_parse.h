/* Checked reading of files the port gets from the disc, the mods folder, HD packs and the import
   (pc/src/host/parse.c). Pure: pc/tests/test_parsers.c feeds them crafted bytes. Every count a file
   gives is bounded by the file's size and by a limit well above what real data has. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* [off, off + len) lies in a buffer of `size` bytes (by subtraction: no wrap-around) */
static inline int pc_range_ok(uint64_t off, uint64_t len, uint64_t size) { return off <= size && len <= size - off; }
/* *r = a * b, a + b; 0 if it does not fit 32 bits */
static inline int pc_mul_ok(uint32_t a, uint32_t b, uint32_t* r) { return !__builtin_mul_overflow(a, b, r); }
static inline int pc_add_ok(uint32_t a, uint32_t b, uint32_t* r) { return !__builtin_add_overflow(a, b, r); }
static inline uint32_t pc_rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint16_t pc_rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

/* ---------------------------------------------------------------- PRS (decomp expand.c Expand)
   n source bytes into d (cap bytes): the output size, PC_PRS_BROKEN if the data runs past its end
   or copies from before the output's start, PC_PRS_FULL if the output does not fit. Reads and
   writes in Expand's order, so the room loader's in-place expansion (the packed copy at the top of
   the arena it expands into) behaves as before. */
#define PC_PRS_BROKEN (-1)
#define PC_PRS_FULL (-2)
#define PC_PRS_MAX (64u << 20)   /* pc_prs_expand's largest output (the game's room arena: 12 MB) */
int32_t pc_prs_decode(const uint8_t* s, uint32_t n, uint8_t* d, uint32_t cap);
/* the same into *out (malloc'd, *cap bytes), grown as needed up to PC_PRS_MAX: the size, -1 if broken */
long pc_prs_expand(const unsigned char* s, long n, unsigned char** out, long* cap);
/* lax: a stream that stops at the data's end without the end mark gives what it expanded so far (the Russian fan
   translation's rooms, docs/LANGUAGES.md) */
long pc_prs_expand_ex(const unsigned char* s, long n, unsigned char** out, long* cap, int lax);

/* ---------------------------------------------------------------- AFS archives (adx.c, import ob2.c)
   Header (8 bytes): "AFS", u32 count; then count x (u32 offset, u32 size). */
#define PC_AFS_MAX 65536u        /* entries (the largest archive of the game has 525, of Outbreak 1087) */
/* the entry count of the archive of `size` bytes whose first 8 bytes are h; -1 if it is not AFS,
   its count is above PC_AFS_MAX or its table does not fit the archive */
int pc_afs_count(const uint8_t* h, uint64_t size);
/* entry i of the table t (8 + 8 * count bytes, as read from the archive start): 0 if it does not
   lie inside the archive */
int pc_afs_entry(const uint8_t* t, uint32_t i, uint64_t size, uint32_t* off, uint32_t* len);

/* ---------------------------------------------------------------- .cvxm (pc/src/import/impmodel.h)
   Everything pc/glue/actor_glue.c relocates, realizes, uploads, draws and skins checked against the
   file: the header, the one MLB of the chunk list (objects, their links forward only, the model,
   the vertex chunk of nv x 32 bytes, the polygon chunks with every strip index, the texture list),
   the TIM2 chunks of the texture block and the skin array. */
typedef struct PcCvxm {
    uint32_t vtx;       /* file offset of the vertex chunk's first vertex (imp_skin writes nv x 32 bytes there) */
    uint32_t skin;      /* of the skin array (nv entries) */
    uint32_t nv;
} PcCvxm;
#define PC_CVXM_NODES 64          /* objects (impmodel.c CV_MAX; the rig's NODES) */
#define PC_CVXM_TEX 16            /* textures (IMP_TEX) */
int pc_cvxm_check(const uint8_t* f, uint32_t n, PcCvxm* out, char* why, int wn);

/* ---------------------------------------------------------------- HD textures (hdpack.c)
   Limits per replacement (docs/HD_PACK.md): at most 8192 x 8192 (8x the PS2's largest texture,
   1024 x 1024) and 384 MB in memory (a DDS file as read: an uncompressed 8192 x 8192 with all its
   mipmaps is 341 MB; a PNG decoded to RGBA, 256 MB at 8192 x 8192). */
#define PC_HD_MAX_DIM 8192u
#define PC_HD_MAX_BYTES (384ull << 20)
typedef struct PcDds { uint32_t w, h, mips, off, dxgi, block, bgra; } PcDds;
/* the header of a DDS file of n bytes (d: its first `avail` bytes, 148 suffice): 0 if it is not one
   of the formats used, too large, declares more mipmap levels than its size has or its first level
   does not fit */
int pc_dds_parse(const uint8_t* d, size_t avail, uint64_t n, PcDds* x);
/* bytes of mipmap level k */
uint64_t pc_dds_level(const PcDds* x, uint32_t k);
/* a PNG's dimensions from its IHDR (the first 24 bytes); 0 if it is not a PNG */
int pc_png_size(const uint8_t* d, size_t n, uint32_t* w, uint32_t* h);

/* ---------------------------------------------------------------- BioRand start kit (biorand_glue.c)
   The C7/C9 actions (PlItemChangeM, PlItemTamaSet: 4 bytes each) of the "ev_flg 1 clear" blocks of
   room 0/0's entry script, d: the expanded room file (n bytes). at[]: their offsets, at most max.
   Returns their count; -1 if the script is cut off, an instruction's operands are incomplete or a
   block holds an action with an item id >= 156 or a count above 999 (nothing is applied then).
   insn: the script instructions' lengths (net/coop_proto.c coop_insn_len_raw). */
#define PC_KIT_MAX 256
int pc_startkit_scan(const uint8_t* d, uint32_t n, uint32_t* at, int max, int (*insn)(const uint8_t*));

#ifdef __cplusplus
}
#endif
