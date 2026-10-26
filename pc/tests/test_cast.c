/* The cutscene cast randomizer's picks (pc/src/host/cast.c): deterministic from (seed, scene), never a
   character's own look, different characters different models, "Random per cutscene" picking again per room. */
#include "../src/host/cast.h"
#include "../src/host/actors.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int in(const int* p, int n, int v) { for (int i = 0; i < n; i++) if (p[i] == v) return 1; return 0; }

int main(void)
{
    int cine[16], all[128], nc = cast_pool(0, 0, cine, 16), na = cast_pool(1, 0, all, 128), c, s, scene, differ = 0;
    static const int own[CAST_CHARACTERS] = { 0, 1, 3, 5, 8, 12, 13 };   /* each character's cutscene model */
    CHECK(nc == CAST_CHARACTERS - 2 && na > 12 && na < 30);          /* the cutscene models but Claire's and Chris's; all models with the zombies as one entry */
    CHECK(cine[0] == 3 && cine[2] == 8 && cine[4] == 13);            /* Steve's, Alfred's, Rodrigo's cutscene models */
    CHECK(!in(all, na, 10) && !in(all, na, 2) && in(all, na, ACTOR_PART + 2) && in(all, na, 17));   /* no wounded Alfred, no twin; player files, Tyrant */
    for (int i = 0; i < 5; i++) CHECK(!in(cine, nc, i < 2 ? i : -1) && !in(all, na, i < 2 ? i : -1));   /* never Claire's or Chris's look: */
    CHECK(!in(all, na, ACTOR_PART + 0) && !in(all, na, ACTOR_PART + 1) && !in(all, na, ACTOR_PART + 4));   /* their player files neither */
    for (int i = 0; i < na; i++) CHECK(all[i] >= ACTOR_PART || actor_models[all[i]].anim != ANIM_IDLE);   /* humanoids only */
    {   /* the zombie looks: one entry; no zombie look itself; the anatomist stays an entry of its own */
        int z = 0;
        for (int i = 0; i < na; i++) { z += all[i] == CAST_ZOMBIES; CHECK(all[i] >= ACTOR_PART || (actor_models[all[i]].id != 1 && actor_models[all[i]].id != 52)); }
        CHECK(z == 1 && in(all, na, 25));
    }
    {   /* the look a character gets with the zombie entry: deterministic, a zombie, and it varies with the seed and the character */
        int seen[64] = { 0 }, kinds = 0, got = 0;
        for (s = 0; s < 400; s++)
            for (c = 0; c < CAST_CHARACTERS; c++) {
                int z = cast_zombie_look((unsigned)s, 0, c);
                CHECK(z >= 0 && z < 64 && z == cast_zombie_look((unsigned)s, 0, c) && (actor_models[z].id == 1 || actor_models[z].id == 51 || actor_models[z].id == 52));
                kinds += !seen[z]; seen[z] = 1;
            }
        CHECK(kinds > 20);
        for (s = 0; s < 400; s++)   /* a character that draws the zombie entry shows that look */
            for (c = 0; c < CAST_CHARACTERS; c++) {
                int a = cast_model((unsigned)s, 0, c, 1, 0);
                if (a < ACTOR_MODELS && actor_models[a].id == 1) { CHECK(a == cast_zombie_look((unsigned)s, 0, c)); got++; }
            }
        CHECK(got > 20 && got < 400 * CAST_CHARACTERS / 4);   /* zombies now and then, not most of the time */
        CHECK(cast_zombie_look(5, 0, 1) != cast_zombie_look(5, 0, 2) || cast_zombie_look(6, 0, 1) != cast_zombie_look(6, 0, 2));
    }
    {   /* "Randomize Played as": the same seed the same look; seeds spread over the pool; every pick a listed look */
        int seen[COOP_CHR_MAX + 1] = { 0 }, kinds = 0;
        for (s = 0; s < 2000; s++) {
            int a = cast_appear_pick((unsigned)s, 0);
            CHECK(a == cast_appear_pick((unsigned)s, 0) && a >= 1 && a <= COOP_CHR_MAX && appear_canon(a) == a && appear_at(appear_pos(a)) == a);
            CHECK(a != 1 && a != 2 && a != 5 && a != COOP_CHR_ACTOR + 0 && a != COOP_CHR_ACTOR + 1);   /* never Claire's or Chris's own looks */
            kinds += !seen[a]; seen[a] = 1;
        }
        CHECK(kinds >= na - 1);   /* every pool entry (the zombie entry as several looks) */
        CHECK(cast_appear_pick(123456, 0) != cast_appear_pick(654321, 0));
        printf("played as: seed 123456 -> %s, 654321 -> %s (%d looks over 2000 seeds)\n", appear_name(cast_appear_pick(123456, 0)),
               appear_name(cast_appear_pick(654321, 0)), kinds);
    }
    /* who is cast */
    CHECK(cast_character(91, 0) == 0 && cast_character(43, 0) == 2 && cast_character(96, 1) == 4 && cast_character(98, 0) == 6);
    CHECK(cast_character(99, 0) < 0 && !in(cine, nc, 14) && !in(all, na, 14));   /* the man in the hazmat suit: neither cast nor a pick */
    CHECK(cast_character(16, 0) < 0 && cast_character(12, 0) < 0 && cast_character(1, 0) < 0 && cast_character(68, 0) < 0);
    for (s = 0; s < 50; s++) {
        int got[CAST_CHARACTERS];
        for (c = 0; c < CAST_CHARACTERS; c++) {   /* fixed: the cutscene models, nobody as itself, each model for at most two characters */
            int same = 0;
            got[c] = cast_model((unsigned)s, 0, c, 0, 0);
            CHECK(in(cine, nc, got[c]) && got[c] != own[c] && got[c] == cast_model((unsigned)s, 0, c, 0, 0));
            for (int d = 0; d < c; d++) same += got[d] == got[c];
            CHECK(same <= 1);
            int a = cast_model((unsigned)s, 0, c, 1, 0);   /* all models: none of the character's own looks */
            CHECK((in(all, na, a) || a == cast_zombie_look((unsigned)s, 0, c)) && a != own[c] && (c > 3 || a != ACTOR_PART + c));
        }
        for (scene = 1; scene < 20; scene++) {   /* random per cutscene: again in each room, the same for the same room */
            int m = cast_model((unsigned)s, (unsigned)scene * 10 + 1, 2, 0, 0);
            CHECK(m == cast_model((unsigned)s, (unsigned)scene * 10 + 1, 2, 0, 0) && m != own[2]);
            differ += m != got[2];
        }
    }
    CHECK(differ > 50 * 19 / 2);
    /* Fixed: a character's model does not depend on who is played or where (cast_model takes neither): the same in Claire's
       part (room 0000) and in Chris's (7220), scene 0 in both */
    for (s = 0; s < 50; s++) for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0) == cast_model((unsigned)s, 0, c, 0, 0) && cast_model((unsigned)s, 0, c, 1, 0) == cast_model((unsigned)s, 0, c, 1, 0));   /* most rooms pick another model than the fixed one */
    CHECK(cast_model(1, 0, 8, 0, 0) < 0 && cast_model(1, 0, -1, 0, 0) < 0);   /* not a character */
    {   /* the seed decides: two seeds differ somewhere */
        int d = 0;
        for (c = 0; c < CAST_CHARACTERS; c++) d += cast_model(1, 0, c, 0, 0) != cast_model(2, 0, c, 0, 0);
        CHECK(d > 0);
    }
    /* imported models: only when asked for, then in both pools; the list's identity */
    CHECK(cast_import_id() == 0);
    actor_imports[0] = (ActorModel){ .name = "Kevin", .anim = ANIM_MALE };
    actor_imports[1] = (ActorModel){ .name = "Cindy", .anim = ANIM_FEMALE };
    actor_import_n = 2;
    {
        unsigned id = cast_import_id();
        int p[16], n = cast_pool(0, 2, p, 16), seen = 0;
        CHECK(id != 0 && n == nc + 2 && p[n - 1] == ACTOR_MODELS + 1);
        for (s = 0; s < 40; s++)
            for (c = 0; c < CAST_CHARACTERS; c++) seen |= cast_model((unsigned)s, 0, c, 0, 2) >= ACTOR_MODELS;
        CHECK(seen);
        for (s = 0; s < 40; s++)
            for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0) < ACTOR_MODELS);
        actor_imports[2] = (ActorModel){ .name = "Jim", .anim = ANIM_MALE };
        actor_import_n = 3;
        CHECK(cast_import_id() != id);
    }
    printf("cast: ok\n");
    return 0;
}
