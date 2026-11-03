/* pure rules of the PC hooks (pc/include/pc_rules.h, pc/src/gs/native.h), built with the game's flags and types */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "pc_rules.h"
#include "../src/host/biorand.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void p_a(BH_PWORK* e) { (void)e; }
static void p_b(BH_PWORK* e) { (void)e; }
static void p_c(BH_PWORK* e) { (void)e; }

static void enemies(void)
{
    static BH_PWORK ene[8];
    unsigned char owner[8] = { 0 };
    PcEnemyProc snap[8] = { 0 }, helper[8] = { 0 };
    int i;
    for (i = 0; i < 8; i++) { ene[i].id = 6; ene[i].lkwkp = NULL; }
    /* root walk: 3 links at most, only inside the array */
    ene[1].lkwkp = (unsigned char*)&ene[0];
    ene[2].lkwkp = (unsigned char*)&ene[1];
    ene[3].lkwkp = (unsigned char*)&ene[2];
    ene[4].lkwkp = (unsigned char*)&ene[3];
    CHECK(pc_root_in(&ene[0], ene, 8) == &ene[0]);
    CHECK(pc_root_in(&ene[3], ene, 8) == &ene[0]);
    CHECK(pc_root_in(&ene[4], ene, 8) == &ene[1]);   /* (a 4th link is not followed) */
    {
        static BH_PWORK outside;
        ene[5].lkwkp = (unsigned char*)&outside;
        CHECK(pc_root_in(&ene[5], ene, 8) == &ene[5]);
        ene[5].lkwkp = NULL;
    }
    /* family: a part of the holder's root, or the leader of its type */
    CHECK(pc_family_in(&ene[2], &ene[3], ene, 8, owner));
    CHECK(!pc_family_in(&ene[6], &ene[3], ene, 8, owner));   /* same id, not made by the type */
    owner[6] = 6 + 1;
    CHECK(pc_family_in(&ene[6], &ene[3], ene, 8, owner));    /* the type's leader */
    ene[6].id = 3;
    CHECK(!pc_family_in(&ene[6], &ene[3], ene, 8, owner));   /* another type */
    ene[6].id = 6;
    ene[6].lkwkp = (unsigned char*)&ene[7];
    CHECK(!pc_family_in(&ene[6], &ene[3], ene, 8, owner));   /* linked: a part of something else */
    /* the function a step runs */
    snap[2] = p_b; helper[2] = p_c;
    CHECK(pc_enemy_proc(p_a, 10, 5, 1, snap, helper) == p_a);    /* not a part or child */
    CHECK(pc_enemy_proc(p_a, 33, 0, 1, snap, NULL) == p_a);      /* made outside an enemy's code */
    CHECK(pc_enemy_proc(p_a, 33, 5, 1, snap, helper) == p_b);    /* made before the room got added enemies: the snapshot */
    CHECK(pc_enemy_proc(p_a, 34, 5, 1, snap, helper) == p_a);    /* (no snapshot for that id) */
    CHECK(pc_enemy_proc(p_a, 33, 5, 0, snap, helper) == p_c);    /* made since: its creator type's own */
    CHECK(pc_enemy_proc(p_a, 34, 5, 0, snap, helper) == p_a);    /* (the type registered none) */
    CHECK(pc_enemy_proc(p_a, 39, 5, 0, snap, helper) == p_a);
}

static void dual(void)
{
    const float yaw = PC_DUAL_YAW, pitch = PC_DUAL_PITCH;
    CHECK(pc_dual_arm(4, 4, -1, 1000, 0) == 0);                /* the crosshair's own enemy */
    CHECK(pc_dual_arm(4, 2, -1, 1000, 0) == 2);                /* left of the crosshair: the left arm */
    CHECK(pc_dual_arm(4, 2, -1, -1000, 0) == 1);               /* right */
    CHECK(pc_dual_arm(4, 2, -1, 0, 0) == 1);
    CHECK(pc_dual_arm(4, 2, -1, yaw, pitch) == 2);             /* the cone's edge counts */
    CHECK(pc_dual_arm(4, 2, -1, yaw * 1.1f, 0) == 0);          /* outside 45 degrees */
    CHECK(pc_dual_arm(4, 2, -1, 0, -pitch * 1.1f) == 0);       /* outside 15 degrees */
    CHECK(pc_dual_arm(4, 2, 4, yaw * 1.1f, 0) == 2);           /* last tick's: 15 % more */
    CHECK(pc_dual_arm(4, 2, 4, -yaw * 1.1f, pitch * 1.1f) == 1);
    CHECK(pc_dual_arm(4, 2, 4, yaw * 1.2f, 0) == 0);
    CHECK(pc_dual_arm(4, 4, 4, 10, 0) == 0);
}

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)
static void precise(void)
{
    const float o[3] = { 0, 0, 0 }, fx[3] = { 0, 0, -1 }, a[3] = { 0, -2, -10 }, b[3] = { 0, 2, -10 };
    float m, d[3];
    /* a vertical capsule of radius 1 ten units ahead */
    CHECK(NEAR(pc_ray_capsule(o, fx, a, b, 1, &m), 9) && NEAR(m, 10));
    {   /* beside it: 0.5 inside the radius hits, 1.5 misses */
        const float p1[3] = { 0.5f, 0, 0 }, p2[3] = { 1.5f, 0, 0 }, p3[3] = { -1.01f, 0, 0 };
        CHECK(NEAR(pc_ray_capsule(p1, fx, a, b, 1, &m), 10 - sqrtf(0.75f)));
        CHECK(pc_ray_capsule(p2, fx, a, b, 1, &m) < 0);
        CHECK(pc_ray_capsule(p3, fx, a, b, 1, &m) < 0);
    }
    {   /* over the top: the end sphere is round (2.5 up hits at x 0, not at x 0.9) */
        const float p1[3] = { 0, 2.5f, 0 }, p2[3] = { 0.9f, 2.5f, 0 }, p3[3] = { 0, 3.01f, 0 };
        CHECK(NEAR(pc_ray_capsule(p1, fx, a, b, 1, &m), 10 - sqrtf(0.75f)) && NEAR(m, 10));
        CHECK(pc_ray_capsule(p2, fx, a, b, 1, &m) < 0);
        CHECK(pc_ray_capsule(p3, fx, a, b, 1, &m) < 0);
    }
    /* behind the ray: a miss; from inside: 0 */
    {
        const float bk[3] = { 0, 0, 1 }, in[3] = { 0.2f, 1.5f, -10 }, cap[3] = { 0, 2.5f, -10 };
        CHECK(pc_ray_capsule(o, bk, a, b, 1, &m) < 0);
        CHECK(pc_ray_capsule(in, fx, a, b, 1, &m) == 0);
        CHECK(pc_ray_capsule(cap, fx, a, b, 1, &m) == 0);   /* in the end sphere */
    }
    /* along the axis: enters through the end sphere */
    {
        const float up[3] = { 0, 1, 0 }, below[3] = { 0, -20, -10 };
        CHECK(NEAR(pc_ray_capsule(below, up, a, b, 1, &m), 17));
    }
    /* the second gun's turn: its ray meets the enemy, first */
    CHECK(pc_dual_take(20, -1, -1));
    CHECK(pc_dual_take(20, 25, 30));
    CHECK(!pc_dual_take(-1, -1, -1));    /* beside the turned ray */
    CHECK(!pc_dual_take(20, 15, -1));    /* another enemy in front of it */
    CHECK(!pc_dual_take(20, -1, 12));    /* behind a wall */
    /* a sphere (a == b) */
    {
        const float c[3] = { 0, 0, -10 };
        CHECK(NEAR(pc_ray_capsule(o, fx, c, c, 1, &m), 9) && NEAR(m, 10));
        CHECK(pc_ray_capsule(o, fx, a, a, 1, &m) < 0);   /* 2 below the ray */
    }
    /* slanted: the point nearest the axis lies inside the capsule */
    d[0] = 0.1f; d[1] = 0.05f; d[2] = -1;
    {
        float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), t, q[3], u;
        d[0] /= l; d[1] /= l; d[2] /= l;
        t = pc_ray_capsule(o, d, a, b, 1, &m);
        CHECK(t > 0 && m >= t);
        q[0] = d[0] * m; q[1] = d[1] * m; q[2] = d[2] * m + 10;   /* relative to the axis at y 0 */
        u = q[1] < -2 ? -2 : q[1] > 2 ? 2 : q[1];
        CHECK(q[0] * q[0] + (q[1] - u) * (q[1] - u) + q[2] * q[2] < 1);
    }
}

/* the 4:3 screen is GS x 1728..2368: with ox 2048 and kx 320 a vertex at x/z -1..1 */
static void margins(void)
{
    VU1_STRIP_BUF v[3];
    const float pl_far[2] = { 0, -1 }, pl_left[2] = { -12, 10 };
    memset(v, 0, sizeof(v));
    /* mode 2: a vertex in front of the camera inside the 4:3 screen */
    v[0].fVx = -30; v[0].fVz = 10; v[1].fVx = -20; v[1].fVz = 10; v[2].fVx = 0; v[2].fVz = 10;
    CHECK(pc_margin_rule(2, v, 3, 320, 2048, 1, pl_far) == 1);
    v[2].fVx = -15;
    CHECK(pc_margin_rule(2, v, 3, 320, 2048, 1, pl_far) == 0);   /* all left of it */
    v[2].fVx = 0; v[2].fVz = 0.5f;
    CHECK(pc_margin_rule(2, v, 3, 320, 2048, 1, pl_far) == 0);   /* that one behind the near plane */
    CHECK(pc_margin_rule(0, v, 3, 320, 2048, 1, pl_far) == 0);   /* not a margin mesh */
    /* mode 3: unless all on one side of the 4:3 view or all behind the near plane */
    v[2].fVx = -15; v[2].fVz = 10;
    CHECK(pc_margin_rule(3, v, 3, 320, 2048, 1, pl_far) == 0);   /* all left */
    v[0].fVx = 30; v[1].fVx = 20; v[2].fVx = 15;
    CHECK(pc_margin_rule(3, v, 3, 320, 2048, 1, pl_far) == 0);   /* all right */
    v[2].fVx = -15;
    CHECK(pc_margin_rule(3, v, 3, 320, 2048, 1, pl_far) == 1);   /* across the view: no vertex inside, still reaches it */
    v[0].fVz = v[1].fVz = v[2].fVz = 0.5f;
    CHECK(pc_margin_rule(3, v, 3, 320, 2048, 1, pl_far) == 0);   /* behind */
    /* the view widened to the player: a player left of the screen takes in what lies between */
    v[0].fVx = -30; v[1].fVx = -20; v[2].fVx = -15; v[0].fVz = v[1].fVz = v[2].fVz = 10;
    CHECK(pc_margin_rule(3, v, 3, 320, 2048, 1, pl_far) == 0);
    CHECK(pc_margin_rule(3, v, 3, 320, 2048, 1, pl_left) == 1);
    /* a part left out takes along what lies in its box (2/5: the cabinet's dolls), not its neighbours */
    {
        float box[6];
        const float doll[3] = { -20, 1, 12 }, pot[3] = { -20, 5, 12 };
        v[0].fVx = -30; v[0].fVy = -4; v[0].fVz = 10; v[1].fVx = -15; v[1].fVy = 2; v[1].fVz = 14; v[2].fVx = -25; v[2].fVy = 0; v[2].fVz = 11;
        pc_margin_bounds(v, 3, box);
        CHECK(box[0] == -30 && box[1] == -4 && box[2] == 10 && box[3] == -15 && box[4] == 2 && box[5] == 14);
        CHECK(pc_margin_inside(doll, 1, box) == 1);
        CHECK(pc_margin_inside(doll, 2.5f, box) == 0);   /* reaching out of it */
        CHECK(pc_margin_inside(doll, 1.2f, box) == 1);   /* by less than a quarter of its radius (1/5: the ship in a corner niche) */
        CHECK(pc_margin_inside(pot, 1, box) == 0);
    }
}

static NativeVertex nv(float x, float w, float near_d)
{
    NativeVertex v;
    memset(&v, 0, sizeof(v));
    v.x = x; v.w = w; v.near_d = near_d; v.far_d = 1; v.r = 0x80; v.s = x / 100;
    return v;
}

static void clipping(void)
{
    NativeVertex in[3], out[6];
    int n, i;
    /* near plane: one vertex behind (near_d < 0) -> a quad; the new vertices lie on the plane */
    in[0] = nv(2048, 1, 1); in[1] = nv(2100, 1, 1); in[2] = nv(2000, 1, -1);
    n = native_clip_plane(in, 3, out, 0);
    CHECK(n == 4);
    for (i = 0; i < n; i++) CHECK(out[i].near_d >= 0);
    CHECK(out[2].near_d == 0 && out[2].x == 2050 && out[3].near_d == 0 && out[3].x == 2024);
    CHECK(native_clip_plane(in, 3, out, 1) == 3);   /* far: all inside */
    /* sides: plane 2 keeps what is left of the 4:3 screen (x / w <= 1728), plane 3 what is right of it (>= 2368) */
    in[0] = nv(1600, 1, 1); in[1] = nv(1800, 1, 1); in[2] = nv(1600, 2, 1);   /* (x / w: 1600, 1800, 800) */
    n = native_clip_plane(in, 3, out, 2);
    CHECK(n == 4);
    for (i = 0; i < n; i++) CHECK(out[i].x <= 1728 * out[i].w + 0.01f);
    CHECK(native_clip_plane(in, 3, out, 3) == 0);
    in[0] = nv(2500, 1, 1); in[1] = nv(2400, 1, 1); in[2] = nv(2600, 1, 1);
    CHECK(native_clip_plane(in, 3, out, 3) == 3);
    CHECK(native_clip_plane(in, 3, out, 2) == 0);
}

static void warp_music(void)
{
    /* the next room's entry script asked for the same music again (request 0x8 set by its BgmOn2) */
    CHECK(pc_warp_bgm(0x8, 5, 100, 5, 100, 1) == 0);      /* plays on */
    CHECK(pc_warp_bgm(0x8, 5, 80, 5, 100, 1) == 0x2);     /* plays on, its volume eased */
    CHECK(pc_warp_bgm(0x8, 6, 100, 5, 100, 1) == 0x8);    /* other music: starts as usual */
    CHECK(pc_warp_bgm(0, -1, 0, 5, 100, 1) == -1);        /* none: the old one fades out */
    CHECK(pc_warp_bgm(0, -1, 0, 5, 100, 0) == 0);         /* (nothing playing any more) */
    CHECK(pc_warp_bgm(0, -1, 0, -1, 0, 1) == 0);          /* there was none */
    CHECK(pc_warp_bgse(0x2, 3, 3) == 0);
    CHECK(pc_warp_bgse(0x2, 4, 3) == 0x2);
    CHECK(pc_warp_bgse(0, -1, 3) == -1);
    CHECK(pc_warp_bgse(0, -1, -1) == 0);
}

static void unlock(void)
{
    const unsigned int t[4] = { 16, 0x40, 0x80, 0x90 };       /* scd0, scd1, events 0 and 1 */
    const unsigned int u[4] = { 0x40, 16, 0x80, 0x90 };       /* scd1 first */
    CHECK(pc_script_part(t, 20) == 0);
    CHECK(pc_script_part(t, 0x50) == 1);
    CHECK(pc_script_part(t, 0x85) == 2);
    CHECK(pc_script_part(t, 0x95) == 2);
    CHECK(pc_script_part(u, 0x44) == 0);
    CHECK(pc_script_part(u, 20) == 1);
    CHECK(pc_script_part(u, 0x80) == 2);
    CHECK(pc_unlock_rule(1, 0, 0x25, 1, 7, 1));     /* scd0 */
    CHECK(pc_unlock_rule(1, 1, 0x25, 1, 7, 1));     /* scd1 */
    CHECK(!pc_unlock_rule(1, 2, 0x25, 1, 7, 1));    /* an event: real inventory */
    CHECK(!pc_unlock_rule(1, 0, 36, 3, 16, 1));     /* guards a model change */
    CHECK(!pc_unlock_rule(1, 1, 0x25, 0, 9, 1));    /* the metal detector room */
    CHECK(pc_unlock_rule(1, 1, 0x25, 0, 8, 1));
    CHECK(!pc_unlock_rule(0, 0, 0x25, 1, 7, 1));    /* not a key */
    CHECK(!pc_unlock_rule(1, 0, 0x25, 1, 7, 0));    /* no use spot for it here */
}

/* co-op trades and the item box: an item word in and out of an inventory block, never lost or doubled;
   a count of 0 (the Combat Knife as picked up, an empty gun) is an item like any other */
static void inventory(void)
{
    static ITEM_WORK it[156];
    unsigned int a[12] = { 2, 0x370001, 0x5000f, 0x80000 }, b[12] = { 2, 0x370001, 0x5000f }, w;
    int i;
    it[5].type = 0xc01; it[8].type = 0x401; it[12].type = 0xe02; it[33].type = 0xd01;
    w = pc_inv_take(a, 8, 3, 0, 1, it);                        /* the knife out of the giver's bag */
    CHECK(w == 0x80000 && !a[3] && a[0] == 2);
    CHECK(pc_inv_add(b, 8, w, it) && b[3] == 0x80000);         /* into the receiver's: it is there */
    CHECK(pc_inv_room(b, 8, it) == 6);
    CHECK(!pc_inv_take(b, 8, 2, 0, 1, it));                    /* the equipped handgun stays */
    CHECK(pc_inv_add(b, 8, 0x50000, it) && b[4] == 0x50000);   /* an empty gun */
    for (i = 5; i < 10; i++) b[i] = 0x150001 + (i << 16);      /* full */
    memcpy(a, b, sizeof(a));
    CHECK(!pc_inv_add(b, 8, 0x80000, it) && !memcmp(a, b, sizeof(a)));   /* no room: refused, nothing changed */
    CHECK(!pc_inv_add(b, 8, 0x0c0000, it));                    /* a count-0 word of a stackable needs a cell too */
    b[9] = 0x0c03e0;                                           /* 992 handgun bullets */
    CHECK(pc_inv_add(b, 8, 0x0c0007, it) && b[9] == 0x0c03e7); /* onto the stack, no cell needed */
    CHECK(!pc_inv_add(b, 8, 0x0c0001, it) && b[9] == 0x0c03e7);   /* 999: no room for one more */
    {
        unsigned int c[12] = { 2, 0, 0x5000f, 0x80000 };
        CHECK(pc_inv_add(c, 8, 0x210000, it));                 /* Gold Lugers as picked up: two cells, first */
        CHECK(c[2] == 0x210000 && c[3] == 0x5000f && c[4] == 0x80000 && c[0] == 3);
        CHECK(pc_inv_room(c, 8, it) == 4);
    }
    {   /* the item box's and GetItem's special slot (Claire: lighter 55, lockpick 50; Chris: the lighter) */
        unsigned int d[12] = { 0, 0x370001 };
        CHECK(!pc_to_special(d, 50, 55, 50));                  /* the lockpick out of the box: the lighter stays, a cell */
        CHECK(!pc_to_special(d, 55, 55, 50));                  /* a second lighter: a cell */
        d[1] = 0x320001;
        CHECK(!pc_to_special(d, 55, 55, 50));                  /* the lighter with the lockpick in the slot: a cell */
        d[1] = 0;
        CHECK(pc_to_special(d, 55, 55, 50) && pc_to_special(d, 50, 55, 50));   /* an empty slot takes either */
        CHECK(!pc_to_special(d, 50, 55, -1));                  /* Chris: the lockpick is any item */
        CHECK(!pc_to_special(d, 0x08, 55, 50));                /* the knife (count 0) is no special-slot item */
    }
}

/* BioRand: the item a pickup gives (a room's type, else id) and the seed from its log line */
static void biorand(void)
{
    CHECK(pc_pickup_item(0, 21) == 21);       /* retail: type 0, the record's id */
    CHECK(pc_pickup_item(42, 51) == 42);      /* BioRand: type is the item, id the model */
    CHECK(pc_pickup_item(156, 21) == 21);     /* not an item: the id */
    CHECK(pc_biorand_seed("R840-0KR3-T4HH4HZZZMEZ9H0007HZZZZZZZ5ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ") == 123456);
    CHECK(pc_biorand_seed("R840-JZYL-T4HH4HZZZ") == 654321);
    CHECK(pc_biorand_seed("r840-0kr3-x") == -1);   /* (lower case 'r': not a seed string) */
    CHECK(pc_biorand_seed("R840-0kr3-") == 123456);
    CHECK(pc_biorand_seed("R840-0KR") == -1);
    CHECK(pc_biorand_seed("R840-0KS3-") == -1);   /* S is not in its alphabet */
    CHECK(pc_biorand_seed("") == -1);
}

int main(void)
{
    enemies();
    dual();
    precise();
    margins();
    clipping();
    warp_music();
    unlock();
    inventory();
    biorand();
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("rules: all passed\n");
    return 0;
}
