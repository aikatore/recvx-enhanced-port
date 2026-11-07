/* The --dev object placer's file (pc/src/host/placer.c, docs/DEV_PLACEMENTS.md): what is written reads back
   the same, comments and unknown types are skipped, unknown keys ignored. */
#include "../src/host/placer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(int argc, char** argv)
{
    char path[1024], buf[8192];
    Placement a = { 1, 0, 1, 8, 0, 0, { -22.5f, 0.0f, 5.25f }, { 0, 49152, 0 }, 0 };
    Placement b = { 1, 4, 5, 12, 1, 1, { 101.75f, 3.5f, -40.0f }, { 16384, 8192, 65000 }, 1 };
    Placement q;
    snprintf(path, sizeof path, "%s/test_placer.ini", argc > 1 ? argv[1] : ".");
    CHECK(placer_types() >= 5 && placer_type_find("item_box") == 0 && placer_type_find("blue_herb") >= 0 && placer_type_find("x") < 0);
    CHECK(placer_type_item(placer_type_find("blue_herb")) == 23 && placer_type_item(0) == 0);
    g_placer.p[0] = a;
    g_placer.p[3] = b;
    CHECK(placer_type_item(placer_type_find("blue_herb_bed")) == 0);   /* the planter gives no item: an object */
    CHECK(placer_format(buf, sizeof buf) > 0);
    CHECK(strstr(buf, "; blue_herb_bed Blue Herb Planter: RM_7090 object record 9 (id 774)\n"));
    CHECK(strstr(buf, "type=item_box stage=1 room=8 case=0 player=0 x=-22.50 y=0.00 z=5.25 ax=0 ay=49152 az=0 ax_deg=0.0 ay_deg=-90.0"));
    CHECK(strstr(buf, "file=RM_1080.RDX") && strstr(buf, "file=RM_5121.RDX"));
    CHECK(strstr(buf, "; blue_herb     Blue Herb Pot: RM_3190 item record 5, item 23\n"));   /* the potted plant, key kept */
    CHECK(placer_save(path) == 0);
    memset(g_placer.p, 0, sizeof g_placer.p);
    CHECK(placer_load(path) == 2);
    CHECK(!memcmp(&g_placer.p[0], &a, sizeof a) && !memcmp(&g_placer.p[1], &b, sizeof b) && !g_placer.p[2].used);
    CHECK(!placer_parse_line("; type=item_box stage=1 room=8 x=0 z=0", &q));
    CHECK(!placer_parse_line("type=nothing stage=1 room=8 x=0 z=0", &q));
    CHECK(!placer_parse_line("type=item_box stage=1 x=0 z=0", &q));   /* (no room) */
    CHECK(placer_parse_line("  type=red_herb stage=2 room=3 x=1.5 z=-2 later=1 ay=100", &q) && q.type == placer_type_find("red_herb") &&
          q.stg == 2 && q.rom == 3 && q.pos[0] == 1.5f && q.pos[2] == -2.0f && q.ang[1] == 100 && q.used);
    {   /* a typewriter's location text: quoted (spaces), anywhere in the line, written back the same; cleaned and cut */
        Placement t = { 1, placer_type_find("typewriter"), 1, 8, 0, 0, { -10.75f, 7.5f, 20.5f }, { 0, 0, 0 }, 0, "Prison: Gate" };
        CHECK(placer_parse_line("type=typewriter text=\"Old  office\" stage=1 room=8 x=1 z=2 cases=0,1", &q) && !strcmp(q.text, "Old  office") && q.rom == 8);
        CHECK(placer_parse_line("type=typewriter stage=1 room=8 x=1 z=2 text=\"a\tb\xC3\xA4\"", &q) && !strcmp(q.text, "ab\xC3\xA4"));
        CHECK(placer_parse_line("type=typewriter stage=1 room=8 x=1 z=2 text=\"12345678901234567890\"", &q) && !strcmp(q.text, "1234567890123456"));
        CHECK(placer_parse_line("type=typewriter stage=1 room=8 x=1 z=2 text=\"\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4"
                                "\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\xC3\xA4\"", &q) && strlen(q.text) == 32);   /* 16 letters */
        CHECK(placer_parse_line("type=typewriter stage=1 room=8 x=1 z=2 text=\"\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5"
                                "\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\"", &q) &&
              strlen(q.text) == 33);   /* the byte limit, 35: 11 letters, the 12th cut in half left out */
        memset(g_placer.p, 0, sizeof g_placer.p);
        g_placer.p[0] = t;
        CHECK(placer_format(buf, sizeof buf) > 0 && strstr(buf, "file=RM_1080.RDX text=\"Prison: Gate\"\n"));
        CHECK(placer_save(path) == 0 && placer_load(path) == 1 && !memcmp(&g_placer.p[0], &t, sizeof t));
        memset(g_placer.p, 0, sizeof g_placer.p);
    }
    {   /* "Extra Item Boxes and Herbs" (extras.c): every placement readable, its cases; the turned rectangles; the trigger's
           directions */
        const ExtraShape* sh;
        unsigned cases;
        float r[4], w[4];
        int i;
        for (i = 0; i < extras_n(); i++) CHECK(extras_get(i, &q, &cases, &sh) && (cases >> q.rcase & 1));
        CHECK(extras_n() == 19 && extras_get(0, &q, &cases, &sh) && cases == 3 && !strcmp(sh->key, "item_box") && sh->trig_type == 4);
        CHECK(extras_get(2, &q, &cases, &sh) && cases == 1 && !strcmp(sh->key, "blue_herb_bed"));
        CHECK(extras_get(12, &q, &cases, &sh) && !strcmp(sh->key, "typewriter") && !strcmp(q.text, "Self-destruct te"));   /* (floor 2) */
        memset(&q, 0, sizeof q);
        q.pos[0] = 10; q.pos[2] = 20; q.ang[1] = 16384;   /* 90 degrees: model +z goes to world +x, model +x to world -z */
        r[0] = 0; r[1] = 1; r[2] = 2; r[3] = 3;
        extras_rect(&q, r, w);
        CHECK(fabsf(w[0] - 12) < 1e-3f && fabsf(w[1] - 13) < 1e-3f && fabsf(w[2] - 19) < 1e-3f && fabsf(w[3] - 20) < 1e-3f);
        CHECK(!extras_slanted(0) && !extras_slanted(65400) && !extras_slanted(16249) && extras_slanted(8124));
        CHECK(extras_facing(0, 0) == (0x400 | 0x800 | 0x2000));    /* a box facing +z: used facing -z */
        CHECK(extras_facing(0, 0x8000) == (0x800 | 0x1000 | 0x2000));  /* a planter used from -z: facing +z */
        CHECK(extras_facing(16384, 0) == (0x400 | 0x1000 | 0x2000)); /* a box facing +x: used facing -x */
        CHECK(extras_facing(8192, 0x8000) == (0x800 | 0x1000));     /* 45 degrees: facing +z or +x */
        CHECK(extras_facing(0, 0xC000) == 0x1C00);                  /* a typewriter as RM_1080's: its attr 0x9c00's bits */
    }
    remove(path);
    printf("placer: ok\n");
    return 0;
}
