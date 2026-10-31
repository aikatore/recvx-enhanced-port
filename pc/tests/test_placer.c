/* The --dev object placer's file (pc/src/host/placer.c, docs/DEV_PLACEMENTS.md): what is written reads back
   the same, comments and unknown types are skipped, unknown keys ignored. */
#include "../src/host/placer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    CHECK(placer_format(buf, sizeof buf) > 0);
    CHECK(strstr(buf, "type=item_box stage=1 room=8 case=0 player=0 x=-22.50 y=0.00 z=5.25 ax=0 ay=49152 az=0 ax_deg=0.0 ay_deg=-90.0"));
    CHECK(strstr(buf, "file=RM_1080.RDX") && strstr(buf, "file=RM_5121.RDX"));
    CHECK(placer_save(path) == 0);
    memset(g_placer.p, 0, sizeof g_placer.p);
    CHECK(placer_load(path) == 2);
    CHECK(!memcmp(&g_placer.p[0], &a, sizeof a) && !memcmp(&g_placer.p[1], &b, sizeof b) && !g_placer.p[2].used);
    CHECK(!placer_parse_line("; type=item_box stage=1 room=8 x=0 z=0", &q));
    CHECK(!placer_parse_line("type=nothing stage=1 room=8 x=0 z=0", &q));
    CHECK(!placer_parse_line("type=item_box stage=1 x=0 z=0", &q));   /* (no room) */
    CHECK(placer_parse_line("  type=red_herb stage=2 room=3 x=1.5 z=-2 later=1 ay=100", &q) && q.type == placer_type_find("red_herb") &&
          q.stg == 2 && q.rom == 3 && q.pos[0] == 1.5f && q.pos[2] == -2.0f && q.ang[1] == 100 && q.used);
    remove(path);
    printf("placer: ok\n");
    return 0;
}
