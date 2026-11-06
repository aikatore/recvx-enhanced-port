/* "Extra Item Boxes and Herbs" (docs/DEV_PLACEMENTS.md "Extra item boxes and herbs"): where the extra objects stand,
   and their shapes. The placements are lines of the --dev object placer's export (our own data, not the game's),
   plus `cases=`: the room cases that get the object (default: the case it was placed in). The game side is
   pc/glue/extras_glue.c. Pure: ctest `placer` checks the table and the geometry. */
#include "placer.h"
#include <math.h>
#include <string.h>

static const char* const lines[] = {
    /* the first export (Claire, case 0); the cases a placement has where the spot is the same in each (RM_0031, RM_1021, RM_4001) */
    "type=item_box stage=0 room=3 case=0 player=0 x=-10.31 y=0.00 z=65.52 ax=0 ay=65528 az=0 floor=0 cases=0,1",
    "type=blue_herb_bed stage=0 room=3 case=0 player=0 x=-20.60 y=2.00 z=158.27 ax=0 ay=65400 az=0 floor=0 cases=0,1",
    "type=blue_herb_bed stage=0 room=9 case=0 player=0 x=39.80 y=2.00 z=21.95 ax=0 ay=32724 az=0 floor=0",
    "type=blue_herb_bed stage=1 room=2 case=0 player=0 x=72.63 y=2.02 z=84.17 ax=0 ay=8124 az=0 floor=0 cases=0,1",
    "type=item_box stage=4 room=0 case=0 player=0 x=14.60 y=0.00 z=52.58 ax=0 ay=16249 az=0 floor=0 cases=0,1",
    "type=blue_herb_bed stage=4 room=0 case=0 player=0 x=58.56 y=2.00 z=1.45 ax=0 ay=32633 az=0 floor=0 cases=0,1",
    "type=blue_herb_bed stage=2 room=0 case=0 player=0 x=51.61 y=2.00 z=88.62 ax=0 ay=49511 az=0 floor=0",
    "type=item_box stage=2 room=5 case=0 player=0 x=45.32 y=0.00 z=25.74 ax=0 ay=49059 az=0 floor=0",
    "type=blue_herb_bed stage=7 room=5 case=0 player=0 x=27.35 y=2.00 z=22.78 ax=0 ay=32781 az=0 floor=0",
    "type=blue_herb_bed stage=7 room=12 case=0 player=0 x=2.05 y=2.00 z=66.92 ax=0 ay=48672 az=0 floor=0",
    "type=blue_herb_bed stage=3 room=0 case=0 player=0 x=1.75 y=2.00 z=67.49 ax=0 ay=49868 az=0 floor=0",
    "type=item_box stage=3 room=8 case=0 player=0 x=7.95 y=0.00 z=4.59 ax=0 ay=65208 az=0 floor=0",
};

/* in the model's own frame (angle 0): the wall over it, the trigger box, where the player is when facing it */
const ExtraShape extra_shapes[] = {
    /* RM_1080's box: wall 22 (12 x 9, height 9), trigger 3 (type 4, attr 0x2: the box with its lid) in front, +z */
    { "item_box", { -6.0f, 6.0f, -4.5f, 4.5f }, 9.0f, { -5.5f, 5.5f, 1.5f, 6.5f }, 4, 0x2, 0 },
    /* RM_7090's planter: wall 12 (height 2), trigger 10 (type 3, attr 0) from just in front of the middle to behind it; the
       placements have their back (+z) to a wall and the player on -z */
    { "blue_herb_bed", { -2.7f, 2.7f, -1.0f, 1.3f }, 2.0f, { -3.6f, 3.6f, -1.0f, 4.0f }, 3, 0, 0x8000 },
    /* RM_1080's typewriter (its keys face -x): trigger 6 (type 3; attr 0x9c00, usable facing +x) where it is around the
       typewriter's middle (-10.77, 20.60); its wall the model's (on a desk none: the desk's is there, extras_glue.c) */
    { "typewriter", { -2.1f, 2.1f, -2.4f, 2.4f }, 2.7f, { -6.2f, 0.8f, -2.6f, 4.4f }, 3, 0, 0xC000 },
};
const int extra_shape_n = (int)(sizeof(extra_shapes) / sizeof(extra_shapes[0]));

int extras_n(void) { return (int)(sizeof(lines) / sizeof(lines[0])); }

int extras_get(int i, Placement* p, unsigned* cases, const ExtraShape** shape)
{
    return i >= 0 && i < extras_n() && extras_get_line(lines[i], p, cases, shape);
}

int extras_get_line(const char* line, Placement* p, unsigned* cases, const ExtraShape** shape)
{
    const char* c;
    int k;
    if (!placer_parse_line(line, p)) return 0;
    for (k = 0; k < extra_shape_n && placer_type_find(extra_shapes[k].key) != p->type; k++) {}
    if (k == extra_shape_n) return 0;
    *shape = &extra_shapes[k];
    *cases = 1u << (p->rcase & 15);
    if ((c = strstr(line, " cases=")))
        for (c += 7; *c >= '0' && *c <= '9'; c += c[1] == ',' ? 2 : 1) *cases |= 1u << (*c - '0');
    return 1;
}

/* the rectangle r (x0 x1 z0 z1, model frame) of p turned by its ay, as the world box around it (out: x0 x1 z0 z1) */
void extras_rect(const Placement* p, const float* r, float* out)
{
    float a = (float)p->ang[1] * 6.2831853f / 65536.0f, c = cosf(a), s = sinf(a);
    int i;
    out[0] = out[2] = 1e9f;
    out[1] = out[3] = -1e9f;
    for (i = 0; i < 4; i++) {
        float x = r[i & 1], z = r[2 + (i >> 1)];
        float wx = p->pos[0] + x * c + z * s, wz = p->pos[2] - x * s + z * c;   /* njRotateY */
        out[0] = fminf(out[0], wx); out[1] = fmaxf(out[1], wx);
        out[2] = fminf(out[2], wz); out[3] = fmaxf(out[3], wz);
    }
}

/* a turn more than 10 degrees off the axes (its wall then is a row of cylinders) */
int extras_slanted(int ay)
{
    int d = ay & 0x3FFF;
    return d > 1820 && d < 16384 - 1820;
}

/* the trigger's direction bits (hitchk.c bhCheckExmAtari: 0x400 / 0x800 / 0x1000 / 0x2000 = not usable facing +z / -x
   / -z / +x): usable only facing the object from its use side (`use_ay`: the player's turn using it at the model's ay 0);
   a turn near 45 degrees leaves both directions it lies between */
unsigned extras_facing(int ay, int use_ay)
{
    static const unsigned bit[4] = { 0x1000, 0x800, 0x400, 0x2000 };   /* by (player ay + 8192) & 0xC000: -z, -x, +z, +x */
    int face = (ay + use_ay) & 0xFFFF, q;   /* the player's ay facing it (forward: -sin, -cos) */
    unsigned attr = 0;
    for (q = 0; q < 4; q++) {
        int d = (face - q * 16384) & 0xFFFF;
        if (d > 32768) d = 65536 - d;
        if (d >= 12288) attr |= bit[q];   /* 67.5 degrees or more off that direction */
    }
    return attr;
}
