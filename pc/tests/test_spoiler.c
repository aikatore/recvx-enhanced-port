/* ctest "spoiler": BioRand's spoiler log parser (src/host/spoiler.c) on a small made-up log - segments and their
   rooms and keys, key items with their originals (rdt.json's) and parts, replaced, swapped and linked spots, the start
   inventory, an enemy line - and on odd input (empty, not a log, cut lines). With a file argument: that log's summary. */
#include "../src/host/spoiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int bad;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

static const char logtext[] =
    "BioRand 3.3.2 (7955765)\r\n"
    "    Seed: R840-Z3VU-TEST\r\n"
    "    Player: 0 Claire\r\n"
    "    Scenario: A\r\n"
    "Creating Random Room Graph:\n"
    "    Creating segment from 1000 to 6000:\n"
    "        Connected 1000:0 to A1A0:0\n"
    "        Connected 40C0:1 [EmblemCard] to 5020:2 (locked #129)\n"
    "        Connected 7050:2 [GasMask, MiningRoomKey, PlantPot] to 40D0:0 (locked #133)\n"
    "        Connected 40D0:1 (unblock) [EmblemCard] to 6000:5 (locked #135)\n"
    "    Creating segment from 6000 to 8080:\n"
    "        Connected 6000:255 to 7000:255 (always)\n"
    "        Connected 7000:3 to 8080:0\n"
    "    4011:1 -> null\n"
    "        Connected 4011:1 to 4011:1\n"
    "Randomizing Items:\n"
    "    Placing key items:\n"
    "        Placing key item (EmblemCard x1) in A160:6\n"
    "        ------------ checkpoint ------------\n"
    "        Placing key item (EaglePlate x1) in 2080:7\n"
    "    Randomizing non-key items:\n"
    "        Replaced 8170:7 [BlueHerb x1] with 8170:7 [M1P x1]\n"
    "        Replaced 3070:254 [AirForceProof x1] with 3070:254 [SubMachineGun x224]\n"
    "        Swapped 1040:7 [InkRibbon x3] with 3020:4 [GreenHerb x1]\n"
    "        Replaced 1000:9 [SomeNewItem x2] with 1000:9 [AnotherNewItem x5]\n"
    "        Replaced 1000:x [broken\n"
    "    Setting up linked items:\n"
    "        A160:6 [EmblemCard x1] placed at A161:6\n"
    "Randomizing Inventory 0:\n"
    "    Adding CombatKnife x1\n"
    "    Adding HandgunGlock17 x18 as special item\n"
    "Randomizing enemies:\n"
    "1010 shall have Zombie\n"
    "Replacing enemies:\n";

static const SpItem* at(const Spoiler* sp, int room, int spot)
{
    for (int i = 0; i < sp->nitem; i++)
        if (sp->item[i].room == room && sp->item[i].spot == spot) return &sp->item[i];
    return NULL;
}

static int dump(const char* path)
{
    static Spoiler sp;
    FILE* f = fopen(path, "rb");
    static char buf[1 << 20];
    size_t n;
    if (!f) { printf("%s: not found\n", path); return 1; }
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (spoiler_parse(buf, n, &sp)) { printf("%s: not a BioRand log\n", path); return 1; }
    printf("%s | %s | %s %s: %d spots, %d rooms, %d segments, %d parts, %d start items%s\n", sp.version, sp.seed, sp.player,
           sp.scenario, sp.nitem, sp.nroom, sp.nseg, sp.parts, sp.ninv, sp.truncated ? " (truncated)" : "");
    for (int s = 0; s < sp.nseg; s++) {
        int rooms = 0;
        for (int r = 0; r < sp.nroom; r++) rooms += sp.room[r].seg == s;
        printf("segment %d: %04X -> %04X, %d rooms, %d keys:", s + 1, sp.seg[s].from, sp.seg[s].to, rooms, sp.seg[s].nneed);
        for (int k = 0; k < sp.seg[s].nneed; k++) printf(" %s", spoiler_item_name(sp.seg[s].need[k]));
        printf("\n");
    }
    for (int i = 0; i < sp.nitem; i++) {
        const SpItem* it = &sp.item[i];
        const SpRoom* r = spoiler_room(&sp, it->room);
        printf("%04X:%-3d seg %2d %s%d  %s(%d) x%d -> %s(%d) x%d\n", it->room, it->spot, r ? r->seg + 1 : 0, it->key ? "K" : " ", it->part + 1,
               it->old_name, it->old_id, it->old_n, it->new_name, it->new_id, it->new_n);
    }
    return 0;
}

int main(int argc, char** argv)
{
    static Spoiler sp;
    const SpItem* it;
    const SpRoom* r;
    if (argc > 1) return dump(argv[1]);

    CHECK(spoiler_parse("", 0, &sp) == -1);
    CHECK(spoiler_parse("hello\nworld\n", 12, &sp) == -1);
    CHECK(spoiler_parse("\xEF\xBB\xBF" "BioRand 9.9\n", 15, &sp) == 0 && sp.nitem == 0 && !strcmp(sp.version, "9.9"));
    CHECK(spoiler_parse(logtext, sizeof(logtext) - 1, &sp) == 0);
    CHECK(!strcmp(sp.version, "3.3.2 (7955765)"));
    CHECK(!strcmp(sp.seed, "R840-Z3VU-TEST"));
    CHECK(!strcmp(sp.player, "Claire") && !strcmp(sp.scenario, "A"));
    CHECK(spoiler_has_doors(&sp) && sp.nseg == 2);
    CHECK(sp.seg[0].from == 0x1000 && sp.seg[0].to == 0x6000 && sp.seg[1].from == 0x6000 && sp.seg[1].to == 0x8080);
    CHECK(sp.seg[0].nneed == 4 && sp.seg[0].need[0] == spoiler_item_id("EmblemCard") && sp.seg[0].need[3] == spoiler_item_id("PlantPot"));
    CHECK(sp.seg[1].nneed == 0);
    CHECK((r = spoiler_room(&sp, 0x1000)) && r->seg == 0);
    CHECK((r = spoiler_room(&sp, 0x6000)) && r->seg == 1);   /* the start of segment 2, the end of segment 1 */
    CHECK((r = spoiler_room(&sp, 0x8080)) && r->seg == 1);
    CHECK(!spoiler_room(&sp, 0x4011));                         /* (after the segments) */
    CHECK((r = spoiler_room(&sp, 0x1010)) && !strcmp(r->enemy, "Zombie"));
    CHECK(sp.parts == 2);
    CHECK((it = at(&sp, 0xA160, 6)) && it->key && it->part == 0 && it->new_id == spoiler_item_id("EmblemCard") &&
          it->old_id == spoiler_item_id("GreenHerb") && it->old_n == 1);   /* rdt.json: A160:6 a green herb */
    CHECK((it = at(&sp, 0x2080, 7)) && it->key && it->part == 1 && !strcmp(it->new_name, "EaglePlate"));
    CHECK((it = at(&sp, 0x8170, 7)) && !it->key && it->part == -1 && it->old_id == spoiler_item_id("BlueHerb") && it->new_id == spoiler_item_id("M1P"));
    CHECK((it = at(&sp, 0x3070, 254)) && it->new_n == 224 && !strcmp(it->old_name, "AirForceProof"));
    CHECK((it = at(&sp, 0x1040, 7)) && it->old_n == 3 && it->new_id == spoiler_item_id("GreenHerb"));   /* swapped: the first spot */
    CHECK(!at(&sp, 0x3020, 4));
    CHECK((it = at(&sp, 0x1000, 9)) && it->old_id == -1 && it->new_id == -1 && !strcmp(it->new_name, "AnotherNewItem") && it->new_n == 5);
    CHECK((it = at(&sp, 0xA161, 6)) && it->key && it->new_id == spoiler_item_id("EmblemCard"));   /* linked */
    CHECK(sp.nitem == 7);
    for (int i = 1; i < sp.nitem; i++) CHECK(sp.item[i - 1].room < sp.item[i].room || (sp.item[i - 1].room == sp.item[i].room && sp.item[i - 1].spot < sp.item[i].spot));
    CHECK(sp.ninv == 2 && sp.inv[1].id == spoiler_item_id("HandgunGlock17") && sp.inv[1].n == 18);
    CHECK(!sp.truncated);
    CHECK(spoiler_room_stage(0xA1A0) == 9 && spoiler_room_room(0xA1A0) == 26 && spoiler_room_variant(0xA1A0) == 0);   /* RM_9260 */
    CHECK(spoiler_item_id("GreenHerb") == 0x15 && !strcmp(spoiler_item_name(0x15), "GreenHerb") && spoiler_item_id("Nope") == -1);

    /* every prefix of the log: no crash, no stray read (ASan/valgrind), a sane count */
    for (size_t n = 0; n < sizeof(logtext); n++) {
        int rc = spoiler_parse(logtext, n, &sp);
        CHECK(rc == -1 || (sp.nitem <= 7 && sp.nseg <= 2));
    }
    /* too many segments and spots: kept to the arrays, marked */
    {
        static char big[200000];
        size_t k = (size_t)snprintf(big, sizeof(big), "BioRand x\n");
        for (int s = 0; s < SP_SEGS + 3; s++) k += (size_t)snprintf(big + k, sizeof(big) - k, "    Creating segment from 1000 to 2000:\n");
        for (int i = 0; i < SP_ITEMS + 50; i++)
            k += (size_t)snprintf(big + k, sizeof(big) - k, "Replaced %04X:%d [GreenHerb x1] with %04X:%d [RedHerb x1]\n", 0x1000 + (i / 200) * 16, i % 200, 0x1000 + (i / 200) * 16, i % 200);
        CHECK(spoiler_parse(big, k, &sp) == 0 && sp.nseg == SP_SEGS && sp.nitem == SP_ITEMS && sp.truncated);
    }
    printf(bad ? "%d failed\n" : "spoiler: all passed\n", bad);
    return bad != 0;
}
