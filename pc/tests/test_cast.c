/* The cutscene cast randomizer's picks (pc/src/host/cast.c): deterministic from (seed, scene), never a
   character's own look, different characters different models, "Random per cutscene" picking again per room. */
#include "../src/host/cast.h"
#include "../src/host/actors.h"
int coop_insn_len_raw(const unsigned char* p);   /* net/coop_proto.c */
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int in(const int* p, int n, int v) { for (int i = 0; i < n; i++) if (p[i] == v) return 1; return 0; }

int main(void)
{
    int cine[16], all[128], nc = cast_pool(0, 0, cine, 16, NULL), na = cast_pool(1, 0, all, 128, NULL), c, s, scene, differ = 0;
    static const int own[CAST_CHARACTERS] = { 0, 1, 3, 5, 8, 12, 13 };   /* each character's cutscene model */
    CHECK(nc == CAST_CHARACTERS - 2 && na > 12 && na < 30);          /* the cutscene models but Claire's and Chris's; all models with the zombies as one entry */
    CHECK(cine[0] == 3 && cine[2] == 8 && cine[4] == 13);            /* Steve's, Alfred's, Rodrigo's cutscene models */
    CHECK(!in(all, na, 10) && !in(all, na, 2) && in(all, na, 17));   /* no wounded Alfred, no twin; the Tyrant */
    for (int i = 0; i < 4; i++) CHECK(!in(all, na, ACTOR_PART + i));   /* the player files with a still face: their talking twins */
    CHECK(in(all, na, 3) && in(all, na, 5));                          /* (Steve's and Wesker's cutscene models) */
    for (int i = 0; i < na; i++) for (int j = 0; j < i; j++) CHECK(all[i] != all[j]);   /* each look once */
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
            for (c = 2; c < CAST_CHARACTERS; c++) {
                int a = cast_model((unsigned)s, 0, c, 1, 0, NULL);
                if (a < ACTOR_MODELS && actor_models[a].id == 1) { CHECK(a == cast_zombie_look((unsigned)s, 0, c)); got++; }
            }
        CHECK(got > 20 && got < 400 * (CAST_CHARACTERS - 2) / 4);   /* zombies now and then, not most of the time */
        CHECK(cast_zombie_look(5, 0, 1) != cast_zombie_look(5, 0, 2) || cast_zombie_look(6, 0, 1) != cast_zombie_look(6, 0, 2));
    }
    {   /* "Randomize Played as": the same seed the same looks; seeds spread over the pool; every pick a listed look; Claire's,
           Chris's and Steve's differ, each spread on its own */
        int seen[3][COOP_CHR_MAX + 1] = { { 0 } }, kinds[3] = { 0 }, same = 0;
        for (s = 0; s < 2000; s++) {
            int a[3];
            for (c = 0; c < 3; c++) {
                a[c] = cast_appear_pick((unsigned)s, 1, 0, NULL, c);
                CHECK(a[c] == cast_appear_pick((unsigned)s, 1, 0, NULL, c) && a[c] >= 1 && a[c] <= COOP_CHR_MAX && appear_canon(a[c]) == a[c] && appear_at(appear_pos(a[c])) == a[c]);
                CHECK(a[c] != 1 && a[c] != 2 && a[c] != 5 && a[c] != COOP_CHR_ACTOR + 0 && a[c] != COOP_CHR_ACTOR + 1);   /* never Claire's or Chris's own looks */
                kinds[c] += !seen[c][a[c]]; seen[c][a[c]] = 1;
            }
            CHECK(a[1] != a[0] && a[2] != a[0] && a[2] != a[1]);   /* (two zombie looks would be one entry twice: the entries differ) */
            same += a[1] == cast_appear_pick((unsigned)s + 1, 1, 0, NULL, 1);
        }
        for (c = 0; c < 3; c++) CHECK(kinds[c] >= na + 1 && seen[c][3] && seen[c][4]);   /* every pool entry (the zombie entry as several looks), Steve's and Wesker's player files too */
        CHECK(same < 2000 / 8);   /* Chris's is no function of Claire's */
        CHECK(!strcmp(appear_name(cast_appear_pick(654321, 1, 0, NULL, 0)), "Wesker") && !strcmp(appear_name(cast_appear_pick(123456, 1, 0, NULL, 0)), "Zombie: Base Staff 3"));   /* Claire's: the one look of before */
        CHECK(cast_appear_pick(123456, 1, 0, NULL, 0) != cast_appear_pick(654321, 1, 0, NULL, 0) && cast_appear_pick(1, 1, 0, NULL, 3) == 0);
        printf("played as: seed 123456 -> Claire %s, Chris %s, Steve %s; 654321 -> Claire %s, Chris %s (%d, %d, %d looks over 2000 seeds)\n",
               appear_name(cast_appear_pick(123456, 1, 0, NULL, 0)), appear_name(cast_appear_pick(123456, 1, 0, NULL, 1)), appear_name(cast_appear_pick(123456, 1, 0, NULL, 2)),
               appear_name(cast_appear_pick(654321, 1, 0, NULL, 0)), appear_name(cast_appear_pick(654321, 1, 0, NULL, 1)), kinds[0], kinds[1], kinds[2]);
    }
    {   /* its "Model Selection": "Cutscene Models" the story characters' cutscene models but Claire's and Chris's; "Custom" its own
           list (cast_appear_off, recvx.ini appear_custom_off): "All models"' entries, all checked the same picks, else only the checked
           ones; fewer than three: repeats; none: no pick (0) */
        static unsigned char off[CAST_OFF_BYTES];
        short pos[128];
        int mdl[128], nl = cast_custom_list(1, 0, pos, mdl, 128), seen[128] = { 0 }, tyrant = -1, steve = -1, zomb = -1, total;
        CHECK(nl == na + 2 && cast_custom_count(1, off, 0, &total) == nl && total == nl);   /* (Steve's and Wesker's player files too) */
        for (int k = 0; k < nl; k++) {
            CHECK(pos[k] >= 0 && pos[k] < nl && !seen[pos[k]] && mdl[k] != 0 && mdl[k] != 1 && mdl[k] != ACTOR_PART + 0 && mdl[k] != ACTOR_PART + 1 && mdl[k] != ACTOR_PART + 4);
            seen[pos[k]] = 1;
            if (mdl[k] == 17) tyrant = pos[k];
            if (mdl[k] == ACTOR_PART + 2) steve = pos[k];
            if (mdl[k] == CAST_ZOMBIES) zomb = pos[k];
        }
        CHECK(tyrant >= 0 && steve >= 0 && zomb >= 0);
        for (s = 0; s < 300; s++)
            for (c = 0; c < 3; c++) {
                int a = cast_appear_pick((unsigned)s, 0, 0, NULL, c);
                CHECK(cast_appear_pick((unsigned)s, 2, 0, off, c) == cast_appear_pick((unsigned)s, 1, 0, NULL, c));   /* all checked: as All Models */
                CHECK(a == COOP_CHR_ACTOR + 3 || a == COOP_CHR_ACTOR + 5 || a == COOP_CHR_ACTOR + 8 || a == COOP_CHR_ACTOR + 12 || a == COOP_CHR_ACTOR + 13);
            }
        for (int j = 0; j < nl; j++) cast_off_put(off, j, j != tyrant && j != steve && j != zomb);   /* the Tyrant, Steve's player file, the zombies */
        for (s = 0; s < 300; s++) {
            int a[3];
            for (c = 0; c < 3; c++) {
                a[c] = cast_appear_pick((unsigned)s, 2, 0, off, c);
                CHECK(a[c] == COOP_CHR_ACTOR + 17 || a[c] == 3 || (a[c] >= COOP_CHR_ACTOR && a[c] <= COOP_CHR_MAX && (actor_models[a[c] - COOP_CHR_ACTOR].id == 1 || actor_models[a[c] - COOP_CHR_ACTOR].id == 51 || actor_models[a[c] - COOP_CHR_ACTOR].id == 52)));
            }
            CHECK(a[0] != a[1] && a[0] != a[2] && a[1] != a[2]);   /* (three entries: one each) */
        }
        for (int j = 0; j < nl; j++) cast_off_put(off, j, j != tyrant);   /* one: everyone's */
        for (c = 0; c < 3; c++) CHECK(cast_appear_pick(9, 2, 0, off, c) == COOP_CHR_ACTOR + 17);
        cast_off_put(off, tyrant, 1);   /* none */
        for (c = 0; c < 3; c++) CHECK(cast_appear_pick(9, 2, 0, off, c) == 0);
        {   /* recvx.ini: its own lines; the cast's list untouched; Claire's and Chris's looks are not in it */
            char k[128];
            memset(cast_off, 0, sizeof(cast_off));
            memset(cast_appear_off, 0, sizeof(cast_appear_off));
            CHECK(cast_off_key(1, "Steve") && cast_off_key(1, "Tyrant") && !cast_off_key(1, "Claire") && !cast_off_key(1, "Claire (Cutscene Model)") && !cast_off_key(1, "No Such Model"));
            CHECK(cast_off_at(cast_appear_off, steve) && cast_off_at(cast_appear_off, tyrant) && cast_custom_count(1, cast_appear_off, 0, NULL) == nl - 2);
            CHECK(cast_custom_count(0, cast_off, 0, NULL) == na + 3 && !strcmp(cast_entry_key(ACTOR_PART + 2, k, sizeof(k)), "Steve"));
            memset(cast_appear_off, 0, sizeof(cast_appear_off));
        }
    }
    /* who is cast */
    CHECK(cast_character(91, 0) == 0 && cast_character(43, 0) == 2 && cast_character(96, 1) == 4 && cast_character(98, 0) == 6);
    /* how a record is drawn: Claire and Chris never cast: the one played the game's double, the other one the player's
       look for it (whoever is played, about to be or the partner's: Chris in 7240 and 7220 while the game still plays
       Claire, Claire in Steve's section and in Chris's part); Steve and Wesker as themselves while played, about to be
       or the partner's; everyone else cast */
    for (c = -1; c <= CAST_ALL; c++)
        for (int p = -1; p < 4; p++)
            for (int q = -1; q < 4; q++)
                for (int r = -1; r < 4; r++) {
                    int want = c < 0 || c >= CAST_ALL ? CAST_OWN : c < 2 ? (c == p ? CAST_OWN : CAST_LOOK) : c < 4 && (c == p || c == q || c == r) ? CAST_OWN : CAST_PICK;
                    CHECK(cast_role(c, p, q, r) == want);
                }
    CHECK(cast_role(1, 0, 1, -1) == CAST_LOOK && cast_role(1, 0, -1, -1) == CAST_LOOK && cast_role(1, 1, -1, -1) == CAST_OWN);   /* Chris in 7240 / 7220; played */
    CHECK(cast_role(0, 2, -1, -1) == CAST_LOOK && cast_role(0, 1, -1, 0) == CAST_LOOK && cast_role(2, 0, 2, -1) == CAST_OWN && cast_role(6, 0, 6, 6) == CAST_PICK);
    CHECK(cast_character(99, 0) < 0 && !in(cine, nc, 14) && !in(all, na, 14));   /* the man in the hazmat suit: neither cast nor a pick */
    CHECK(cast_character(16, 0) < 0 && cast_character(12, 0) < 0 && cast_character(1, 0) < 0 && cast_character(68, 0) < 0);
    for (s = 0; s < 50; s++) {
        int got[CAST_CHARACTERS];
        for (c = 0; c < 2; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL) < 0 && cast_model((unsigned)s, 0, c, 1, 0, NULL) < 0 && cast_model((unsigned)s, 9, c, 2, 0, NULL) < 0);   /* Claire, Chris: no pick */
        for (c = 2; c < CAST_CHARACTERS; c++) {   /* fixed: the cutscene models, nobody as itself, each model for one character (five of each) */
            int same = 0;
            got[c] = cast_model((unsigned)s, 0, c, 0, 0, NULL);
            CHECK(in(cine, nc, got[c]) && got[c] != own[c] && got[c] == cast_model((unsigned)s, 0, c, 0, 0, NULL));
            for (int d = 2; d < c; d++) same += got[d] == got[c];
            CHECK(same == 0);
            int a = cast_model((unsigned)s, 0, c, 1, 0, NULL);   /* all models: none of the character's own looks */
            CHECK((in(all, na, a) || a == cast_zombie_look((unsigned)s, 0, c)) && a != own[c] && (a < ACTOR_PART || a >= ACTOR_PART + 4));
        }
        for (scene = 1; scene < 20; scene++) {   /* random per cutscene: again in each room, the same for the same room */
            int m = cast_model((unsigned)s, (unsigned)scene * 10 + 1, 2, 0, 0, NULL);
            CHECK(m == cast_model((unsigned)s, (unsigned)scene * 10 + 1, 2, 0, 0, NULL) && m != own[2]);
            differ += m != got[2];
        }
    }
    CHECK(differ > 50 * 19 / 2);
    /* Fixed: a character's model does not depend on who is played or where (cast_model takes neither): the same in Claire's
       part (room 0000) and in Chris's (7220), scene 0 in both */
    for (s = 0; s < 50; s++) for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL) == cast_model((unsigned)s, 0, c, 0, 0, NULL) && cast_model((unsigned)s, 0, c, 1, 0, NULL) == cast_model((unsigned)s, 0, c, 1, 0, NULL));   /* most rooms pick another model than the fixed one */
    CHECK(cast_model(1, 0, CAST_ALL, 0, 0, NULL) < 0 && cast_model(1, 0, -1, 0, 0, NULL) < 0);   /* not a character */
    {   /* the creatures only cutscenes show: by room file, record and type, nothing else (the fought ones: never) */
        static const int got[][4] = { { 3000, 2, 51, 10 }, { 3230, 1, 19, 8 }, { 6040, 4, 52, 7 }, { 8030, 2, 5, 9 },
                                      { 6100, 2, 15, -1 }, { 1122, 1, 19, -1 }, { 5010, 0, 19, -1 }, { 9280, 0, 17, -1 }, { 9321, 1, 12, -1 },
                                      { 6040, 4, 1, -1 }, { 6041, 4, 52, -1 }, { 6040, 3, 52, -1 }, { 9050, 1, 52, -1 }, { 7220, 0, 2, -1 } };
        static const signed char own[CAST_CREATURES] = { 16, 17, 20, -1 };   /* Nosferatu, the Tyrant, the Hunter, (Steve's father: the zombies) */
        for (unsigned i = 0; i < sizeof(got) / sizeof(got[0]); i++) CHECK(cast_creature(got[i][0], got[i][1], got[i][2]) == got[i][3]);
        CHECK(!strcmp(cast_name(7), "Nosferatu") && !strcmp(cast_name(CAST_ALL - 1), "Steve's father") && !strcmp(cast_name(6), "Rodrigo"));
        for (s = 0; s < 200; s++)
            for (c = CAST_CHARACTERS; c < CAST_ALL; c++) {
                int f = cast_model((unsigned)s, 0, c, 1, 0, NULL), r = cast_model((unsigned)s, 77, c, 1, 0, NULL), z = actor_models[f < ACTOR_MODELS ? f : 0].id;
                CHECK(cast_role(c, 0, -1, -1) == CAST_PICK && f >= 0 && f == cast_model((unsigned)s, 0, c, 1, 0, NULL) && r >= 0);   /* fixed: one model in every scene */
                CHECK(f != own[c - CAST_CHARACTERS] && (c != 8 || f != 18) && (c != 9 || f != 21) && (c != CAST_ALL - 1 || f >= ACTOR_MODELS || (z != 1 && z != 51 && z != 52)));   /* never itself (the Tyrant's twin, the Sweeper) */
                CHECK(in(all, na, f) || f == cast_zombie_look((unsigned)s, 0, c));
                CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL) >= 0);   /* "Cutscene Models": the five used up, a new round */
                for (int d = 2; d < c; d++) CHECK(cast_model((unsigned)s, 0, d, 1, 0, NULL) != f);   /* "All Models" has enough: each its own */
            }
    }
    {   /* the seed decides: two seeds differ somewhere */
        int d = 0;
        for (c = 2; c < CAST_CHARACTERS; c++) d += cast_model(1, 0, c, 0, 0, NULL) != cast_model(2, 0, c, 0, 0, NULL);
        CHECK(d > 0);
    }
    {   /* "Custom": the list is "All models" then Claire's and Chris's looks (each position once, in the menus' order);
           those unchecked is "All models";
           only checked models are picked; one checked: everyone but its own character as it; none: nobody changes */
        static unsigned char off[CAST_OFF_BYTES];
        short pos[128];
        int mdl[128], nl = cast_custom_list(0, 0, pos, mdl, 128), seen[128] = { 0 }, total, tyrant = -1, steve = -1, zomb = -1, cust[128];
        int nf = cast_pool(2, 0, cust, 128, off), claire = 0;
        CHECK(nl == nf && nf == na + 3);   /* (Claire's, Chris's cutscene models; Claire's second costume; no player file with a talking twin) */
        for (int i = 0; i < 4; i++) CHECK(!in(cust, nf, ACTOR_PART + i));
        CHECK(in(cust, nf, 0) && in(cust, nf, 1) && in(cust, nf, ACTOR_PART + 4));
        for (int i = 0; i < nf; i++) for (int j = 0; j < i; j++) CHECK(cust[i] != cust[j]);
        for (int j = 0; j < na; j++) CHECK(cust[j] == all[j]);
        for (int j = 0; j < nf; j++)   /* recvx.ini keys: one per entry */
            for (int i = 0; i < j; i++) { char x[128], y[128]; CHECK(strcmp(cast_entry_key(cust[i], x, sizeof x), cast_entry_key(cust[j], y, sizeof y))); }
        for (int k = 0; k < nl; k++) {
            CHECK(pos[k] >= 0 && pos[k] < nf && !seen[pos[k]] && cust[pos[k]] == mdl[k] && cast_entry_name(mdl[k])[0] && cast_entry_group(mdl[k])[0]);
            seen[pos[k]] = 1;
            if (mdl[k] == 17) tyrant = pos[k];
            if (mdl[k] == 3) steve = pos[k];
            if (mdl[k] == CAST_ZOMBIES) zomb = pos[k];
            claire += mdl[k] == 0;
        }
        CHECK(tyrant >= 0 && steve >= 0 && zomb >= 0 && claire == 1 && cast_custom_count(0, off, 0, &total) == nf && total == nf);
        for (int j = na; j < nf; j++) cast_off_put(off, j, 1);
        for (s = 0; s < 50; s++) for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 2, 0, off) == cast_model((unsigned)s, 0, c, 1, 0, NULL));
        for (int j = 0; j < nf; j++) cast_off_put(off, j, j != tyrant && j != steve && j != zomb);   /* Tyrant, Steve's cutscene model, the zombies */
        CHECK(cast_custom_count(0, off, 0, NULL) == 3 && cast_pool(2, 0, all + 64, 64, off) == 3);
        for (s = 0; s < 200; s++)
            for (c = 2; c < CAST_CHARACTERS; c++) {
                int a = cast_model((unsigned)s, (unsigned)s % 3, c, 2, 0, off);
                CHECK(a == 17 || (a == 3 && c != 2) || a == cast_zombie_look((unsigned)s, (unsigned)s % 3, c));
            }
        for (int j = 0; j < nf; j++) cast_off_put(off, j, j != steve);   /* only Steve's look: Steve stays himself */
        for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model(7, 0, c, 2, 0, off) == (c <= 2 ? -1 : 3));
        cast_off_put(off, steve, 1);   /* none */
        for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model(7, 0, c, 2, 0, off) == -1);
        /* recvx.ini: by key; unknown keys ignored, so are the player files' with a talking twin (an older list's) */
        {
            char k[128];
            memset(cast_off, 0, sizeof(cast_off));
            CHECK(cast_off_key(0, "Tyrant") && cast_off_key(0, "Zombies") && cast_off_key(0, cast_entry_key(5, k, sizeof(k))) && !cast_off_key(0, "No Such Model") && !cast_off_key(0, "import:gone"));
            CHECK(!cast_off_key(0, "Claire") && !cast_off_key(0, "Chris") && !cast_off_key(0, "Steve") && !cast_off_key(0, "Wesker") && cast_off_key(0, "Claire (Alternate Costume)"));
            for (int j = 0; j < nf; j++) if (cust[j] == ACTOR_PART + 4) cast_off_put(cast_off, j, 0);   /* (Claire's costume: checked again) */
            CHECK(cast_off_at(cast_off, tyrant) && cast_off_at(cast_off, zomb) && !cast_off_at(cast_off, steve) && cast_custom_count(0, cast_off, 0, NULL) == nf - 3);
            memset(cast_off, 0, sizeof(cast_off));
        }
    }
    /* imported models: only when asked for, then in both pools; the list's identity */
    CHECK(cast_import_id() == 0);
    actor_imports[0] = (ActorModel){ .name = "Kevin", .anim = ANIM_MALE };
    actor_imports[1] = (ActorModel){ .name = "Cindy", .anim = ANIM_FEMALE };
    actor_import_n = 2;
    {
        unsigned id = cast_import_id();
        int p[16], n = cast_pool(0, 2, p, 16, NULL), seen = 0;
        CHECK(id != 0 && n == nc + 2 && p[n - 1] == ACTOR_MODELS + 1);
        for (s = 0; s < 40; s++)
            for (c = 0; c < CAST_CHARACTERS; c++) seen |= cast_model((unsigned)s, 0, c, 0, 2, NULL) >= ACTOR_MODELS;
        CHECK(seen);
        for (s = 0; s < 40; s++)
            for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL) < ACTOR_MODELS);
        actor_imports[2] = (ActorModel){ .name = "Jim", .anim = ANIM_MALE };
        actor_import_n = 3;
        CHECK(cast_import_id() != id);
        {   /* "Custom" with imports: after the built-in ones, by their folder in recvx.ini; unchecked, never picked */
            short pos[128];
            int mdl[128], nl = cast_custom_list(0, 3, pos, mdl, 128), nimp = 0, nb = na + 3;   /* (the built-in ones: Claire's and Chris's too) */
            char k[128];
            actor_import_file[0] = "mods/import/outbreak2/kevin01/kevin01.cvxm";
            actor_import_file[1] = "mods/import/outbreak2/cindy01/cindy01.cvxm";
            actor_import_file[2] = "mods/import/outbreak2/jim01/jim01.cvxm";
            CHECK(nl == nb + 3 && pos[nb] == nb && mdl[nb + 2] == ACTOR_MODELS + 2 && cast_custom_list(0, 0, pos, mdl, 128) == nb);
            CHECK(!strcmp(cast_entry_key(ACTOR_MODELS + 1, k, sizeof(k)), "import:cindy01") && !strcmp(cast_entry_name(ACTOR_MODELS + 1), "Cindy"));
            CHECK(cast_off_key(0, "import:cindy01") && cast_off_at(cast_off, nb + 1) && !cast_off_at(cast_off, nb) && cast_custom_count(0, cast_off, 3, NULL) == nb + 2);
            for (s = 0; s < 200; s++)
                for (c = 0; c < CAST_CHARACTERS; c++) {
                    int a = cast_model((unsigned)s, 0, c, 2, 3, cast_off);
                    CHECK(a != ACTOR_MODELS + 1);
                    nimp += a == ACTOR_MODELS || a == ACTOR_MODELS + 2;
                }
            CHECK(nimp > 0);
        }
    }
    {   /* "Randomize Character Model" with All Models (its default): every pick as before its Model Selection (5000 seeds, imports or not) */
        unsigned h = 2166136261u;
        for (int imp = 0; imp <= 3; imp += 3)
            for (s = 0; s < 5000; s++)
                for (c = 0; c < 3; c++) h = (h ^ (unsigned)cast_appear_pick((unsigned)s, 1, imp, NULL, c)) * 16777619u;
        CHECK(h == 0x7046d2f6u);
    }
    {   /* "Randomize Voice Lines" (voices.c): a room's lines by the LipSet after each VoiceOn, their lips; picks */
        static unsigned char d[0x400];
        static const unsigned char ev[] = { 25, 1, 5, 0, 1, 0, 0, 0, 61, 0, 1, 1, 63, 0, 1, 0,   /* VoiceOn 5 (record 0), LipSet record 1 entry 1, LipStart */
                                            25, 1, 6, 0, 1, 0, 1, 0, 61, 0, 0, 0,                /* VoiceOn 6 (record 1), LipSet record 0 entry 0 */
                                            25, 1, 7, 0, 1, 0, 1, 0, 25, 0, 8, 0, 1, 0, 0, 0,    /* VoiceOn 7 without one, VoiceOn 8 (kind 0) */
                                            61, 0, 0, 2, 255, 0 };                               /* a LipSet of a missing entry; EvtEnd */
        static const unsigned char lips[] = { 5, 0, 0, 0, 1, 10, 2, 20, 0xFF, 3, 0, 0, 0, 3, 30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        const VoiceLine *l, *a;
        int rec = -1, k;
        unsigned w[] = { 0x10, 0x100, 0x18, 0x200, 0x1C, 0x300, 0x108, 0x180, 0x188, 2, 0x210, 0x280, 0x300, 0x0C, 0x304, 0x0E, 0x308, 0x10 };
        for (k = 0; k < (int)(sizeof w / sizeof w[0]); k += 2) memcpy(d + w[k], &w[k + 1], 4);
        d[0x180 + 4] = 93; d[0x180 + 0x24 + 4] = 91;   /* record 0 Steve, 1 Claire */
        memcpy(d + 0x280, lips, sizeof lips);
        memcpy(d + 0x310, ev, sizeof ev);
        CHECK(voice_count(0) == -1 && !voice_pick(1, 1020, 1, 5, 0));   /* (not read yet) */
        CHECK(voice_lipset(ev + 8, 64, coop_insn_len_raw, &rec) == 1 && rec == 1 && voice_lipset(ev + 36, 64, coop_insn_len_raw, &rec) == -1);
        voice_scan_room(1020, d, sizeof d, coop_insn_len_raw);
        voice_scan_room(1020, d, sizeof d, coop_insn_len_raw);   /* (the same lines again: once) */
        voice_scan_room(9000, d, sizeof d, coop_insn_len_raw);   /* (the other disc's archive: other lines) */
        voice_scan_done();
        CHECK(voice_count(0) == 2 && voice_count(2) == 2 && voice_count(1) == 0 && voice_count(3) == 0);
        l = voice_line(0, 0);
        CHECK(l && l->vid == 5 && l->disc == 1 && l->room == 1020 && l->lip_n == 3 && l->lip[0] == 3 && l->lip[2] == 0xFF);
        l = voice_line(2, 1);
        CHECK(l && l->vid == 6 && l->disc == 2 && l->room == 9000 && l->lip_n == 5 && l->lip[3] == 20 && !voice_line(2, 2));
        for (s = 0, k = 0; s < 64; s++) {   /* deterministic, of the character asked for, both lines in use */
            a = voice_pick((unsigned)s, 1020, 1, 40, 2);
            CHECK(a && a->chr == 2 && a == voice_pick((unsigned)s, 1020, 1, 40, 2));
            k |= 1 << (a->disc - 1);
        }
        CHECK(k == 3 && !voice_pick(1, 1020, 1, 40, 1) && !voice_pick(1, 1020, 1, 40, 7));
        CHECK(voice_model_chr(3) == 2 && voice_model_chr(7) == 3 && voice_model_chr(13) == 6 && voice_model_chr(ACTOR_PART + 3) == 3 &&
              voice_model_chr(ACTOR_PART + 4) == 0 && voice_model_chr(17) == -1 && voice_model_chr(14) == -1 && voice_model_chr(ACTOR_MODELS) == -1 &&
              voice_model_chr(CAST_ZOMBIES) == -1);   /* Steve, Wesker's event model, Rodrigo, files, the Tyrant, the hazmat man, an import */
    }
    printf("cast: ok\n");
    return 0;
}
