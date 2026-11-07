/* A skinned model read from another game's disc, and what it is written as: glTF (.glb, with
   PNG textures) and the "You appear as" format of this port (.cvxm). docs/IMPORT_OUTBREAK.md */
#pragma once

#define IMP_NODES 128
#define IMP_TEX 16

typedef struct ImpNode {
    int parent;             /* -1: a root */
    float t[3], q[4];       /* local rest translation, rotation (x, y, z, w) */
    int keep;               /* a node of the converted (.cvxm) skeleton; 0: merged into its parent */
    int face;               /* IMP_FACE_*: a face bone of the converted model (kept) */
} ImpNode;

/* the face bones: per eye the upper lid (its margin: what closes) and the crease above it (Outbreak's two
   lid bones are both on the upper lid; the crease's skin reaches up to the brow and stays), the jaw */
enum { IMP_FACE_NONE, IMP_FACE_LID_L, IMP_FACE_CREASE_L, IMP_FACE_LID_R, IMP_FACE_CREASE_R, IMP_FACE_JAW, IMP_FACES };

typedef struct ImpStrip { int first, n, tex; } ImpStrip;   /* idx[first .. first + n): a triangle strip; tex: texture slot or -1 */

typedef struct ImpModel {
    int nmesh;              /* meshes of the source model (information) */
    int eyes_left_out;      /* eyeball overlay meshes not converted (ob2.c read_mesh) */
    float eye[2][4];        /* per eye (left, right) the overlay's x min, x max, y min, y max: the opening; y max <= y min: not known */
    float lid_travel[2];    /* the blink: how far the upper lid (left, right) goes down to close (model units; ob2.c lid_falloff) */
    int nn;
    ImpNode node[IMP_NODES];
    int nv;                 /* vertices in model space (the bind pose), weights per node */
    float (*pos)[3], (*nrm)[3], (*uv)[2];
    unsigned char (*jnt)[4];
    float (*wgt)[4];
    int ns, nidx;
    ImpStrip* strip;
    int* idx;
    int ntex;
    unsigned char* tim2[IMP_TEX];   /* PS2 TIM2 files (one picture each), owned */
    int tim2_len[IMP_TEX];
} ImpModel;

void imp_free(ImpModel* m);
int  imp_tris(const ImpModel* m);   /* triangles the strips make (degenerate ones left out) */
void imp_world(const ImpModel* m, float (*W)[16]);   /* world rest matrices (column-major) */
/* a TIM2 picture as RGBA8 (malloc'd; 4/8-bit indexed with a 32-bit CLUT, 16/24/32-bit direct); 0 = unsupported */
int  imp_tim2_rgba(const unsigned char* t, int len, int* w, int* h, unsigned char** rgba);
/* a TIM2 picture whose CLUT gives alpha as 0..255 (Outbreak's: 0xFF opaque, every texture's largest)
   moved to the PS2 convention the rest of this code and the game's blending use (0x80 opaque), in place */
void imp_tim2_alpha_255(unsigned char* t, int len);
/* texture slot k as a PNG file */
int  imp_write_png(const ImpModel* m, int k, const char* path);
/* glTF 2.0 binary: the skeleton, one skinned mesh (a primitive per texture), embedded PNGs;
   `scale` turns the model's units into metres */
int  imp_write_glb(const ImpModel* m, const char* path, const char* name, float scale, char* err, int en);
/* Adjustments made while converting (a fit to Code: Veronica's proportions; ob2.c ob2_fit, a table per
   model): each move shifts a source node and everything below it by d (game units, after the turn), and
   with them the vertices by their weights (a vertex half on the moved node moves half way), so a part of
   the mesh follows its bones smoothly (shoulders in and down with the arms, a collar with them). */
#define IMP_MOVES 8
typedef struct ImpFit {
    int n;
    struct { int node; float d[3]; } move[IMP_MOVES];
} ImpFit;

/* the port's player-model format: the kept nodes as a Ninja chunk skeleton (all bind angles 0),
   one mesh on node 1 (the pelvis) that pc/glue/actor_glue.c skins every frame from the weights,
   the textures as the game's TIM2 chunks with global indices gindex + k. `scale`: model units to
   the game's; turned 180 degrees about y (Outbreak faces +z, Code: Veronica -z). */
int  imp_write_cvxm(const ImpModel* m, const char* path, float scale, unsigned gindex, const ImpFit* fit, char* err, int en);

/* .cvxm layout (little endian), read by pc/glue/actor_glue.c:
     0  "RXIM", u32 version (4), u32 model chunk list offset (60: its u32 size word, the MLB at 64),
        u32 texture block offset (64-aligned), u32 skin offset, u32 vertices, u32 nodes,
        28: u8 face[5]: the object index of each face bone (IMP_FACE_* - 1: lid and crease left, lid
        and crease right, jaw), 255 none (version 1: no face table; versions 2 and 3 have the two lid
        bones the other way round: the crease first)
        36: float lid_travel[2] (left, right; the game's units; version 3, else 0; version 4: from the
        lid's margin to the bottom of the eye's opening)
     version 4 also: the CLUTs' alpha in the PS2 range (0x80 opaque; before: Outbreak's 0..255)
     model chunk list: u32 size, MLB (bhMlbBinRealize), u32 -1
     texture block: "TIM2" chunks as bhSetMemPvpTexture reads them, then an end chunk (-1)
     skin: per vertex float pos[3], nrm[3] (model space, the game's units), u8 node[4], float w[4] */
#define CVXM_MAGIC 0x4D495852u   /* "RXIM" */
#define CVXM_VERSION 4
typedef struct CvxmSkin { float pos[3], nrm[3]; unsigned char node[4]; float w[4]; } CvxmSkin;
