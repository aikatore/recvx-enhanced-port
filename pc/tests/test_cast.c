/* The cutscene cast randomizer's picks (pc/src/host/cast.c): deterministic from (seed, scene), never a
   character's own look, different characters different models, "Random per cutscene" picking again per room. */
#include "../src/host/cast.h"
#include "../src/host/actors.h"
int coop_insn_len_raw(const unsigned char* p);   /* net/coop_proto.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int in(const int* p, int n, int v) { for (int i = 0; i < n; i++) if (p[i] == v) return 1; return 0; }
static int line_ms(const VoiceLine* l) { return l->disc == 1 ? 3000 : 1000; }   /* (voice_pick: a 3 s line on disc 1, 1 s on disc 2) */
static int line_ms_short(const VoiceLine* l) { return l->disc == 1 ? 3000 : 500; }
static int said_first(const VoiceLine* l) { return l == voice_line(2, 0) ? 0 : -1; }   /* (voice_pick: just said in the room) */
static int said_second(const VoiceLine* l) { return l == voice_line(2, 1) ? 0 : -1; }
static VoiceSaid hist;
static int hist_age(const VoiceLine* l) { return voice_said_age(&hist, l); }
static int len_var(const VoiceLine* l) { return 400 + (int)((l->vid * 7919u) % 9000u); }   /* (0.4 to 9.4 s) */

int main(void)
{
    int cine[16], all[128], nc = cast_pool(0, 0, cine, 16, NULL), na = cast_pool(1, 0, all, 128, NULL), c, s, scene, differ = 0;
    static const int own[CAST_CHARACTERS] = { 0, 1, 3, 5, 8, 12, 13 };   /* each character's cutscene model */
    CHECK(nc == CAST_CHARACTERS && na > 12 && na < 30);              /* the cutscene models, Claire's and Chris's too; all models with the zombies as one entry */
    CHECK(cine[0] == 0 && cine[1] == 1 && cine[2] == 3 && cine[4] == 8 && cine[6] == 13);   /* Claire's, Chris's, Steve's, Alfred's, Rodrigo's */
    CHECK(!in(all, na, 10) && !in(all, na, 2) && in(all, na, 17));   /* no wounded Alfred, no twin; the Tyrant */
    for (int i = 0; i < 4; i++) CHECK(!in(all, na, ACTOR_PART + i));   /* the player files with a still face: their talking twins */
    CHECK(in(all, na, 3) && in(all, na, 5));                          /* (Steve's and Wesker's cutscene models) */
    for (int i = 0; i < na; i++) for (int j = 0; j < i; j++) CHECK(all[i] != all[j]);   /* each look once */
    CHECK(in(all, na, 0) && in(all, na, 1));                          /* Claire's and Chris's cutscene models (others may be shown as them), */
    CHECK(!in(all, na, ACTOR_PART + 0) && !in(all, na, ACTOR_PART + 1) && !in(all, na, ACTOR_PART + 4));   /* not their player files, not Claire's second costume */
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
                int a = cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL);
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
        CHECK(nl == na && cast_custom_count(1, off, 0, &total) == nl && total == nl);   /* (Steve's and Wesker's player files in place of Claire's and Chris's cutscene models) */
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
            CHECK(cast_off_key(1, cast_appear_off, "Steve") && cast_off_key(1, cast_appear_off, "Tyrant") && !cast_off_key(1, cast_appear_off, "Claire") && !cast_off_key(1, cast_appear_off, "Claire (Cutscene Model)") && !cast_off_key(1, cast_appear_off, "No Such Model"));
            CHECK(cast_off_at(cast_appear_off, steve) && cast_off_at(cast_appear_off, tyrant) && cast_custom_count(1, cast_appear_off, 0, NULL) == nl - 2);
            CHECK(cast_custom_count(0, cast_off, 0, NULL) == na + 1 && /* (Claire's second costume: only in "Custom") */ !strcmp(cast_entry_key(ACTOR_PART + 2, k, sizeof(k)), "Steve"));
            memset(cast_appear_off, 0, sizeof(cast_appear_off));
        }
    }
    /* who is cast */
    CHECK(cast_character(91, 0) == 0 && cast_character(43, 0) == 2 && cast_character(96, 1) == 4 && cast_character(98, 0) == 6);
    /* how a record is drawn: Claire and Chris never cast: the one played the game's double, the other one the player's
       look for it (whoever is played, about to be or the partner's: Chris in 7240 and 7220 while the game still plays
       Claire, Claire in Steve's section and in Chris's part); Steve and Wesker as themselves while played, about to be
       or the partner's; everyone else cast (the scene zombies too) */
    for (c = -1; c <= CAST_CROWD + 1; c++)
        for (int p = -1; p < 4; p++)
            for (int q = -1; q < 4; q++)
                for (int r = -1; r < 4; r++) {
                    int want = c == CAST_CROWD ? CAST_PICK : c < 0 || c >= CAST_ALL ? CAST_OWN : c < 2 ? (c == p ? CAST_OWN : CAST_LOOK) : c < 4 && (c == p || c == q || c == r) ? CAST_OWN : CAST_PICK;
                    CHECK(cast_role(c, p, q, r) == want);
                }
    CHECK(cast_role(1, 0, 1, -1) == CAST_LOOK && cast_role(1, 0, -1, -1) == CAST_LOOK && cast_role(1, 1, -1, -1) == CAST_OWN);   /* Chris in 7240 / 7220; played */
    CHECK(cast_role(0, 2, -1, -1) == CAST_LOOK && cast_role(0, 1, -1, 0) == CAST_LOOK && cast_role(2, 0, 2, -1) == CAST_OWN && cast_role(6, 0, 6, 6) == CAST_PICK);
    CHECK(cast_character(99, 0) < 0 && !in(cine, nc, 14) && !in(all, na, 14));   /* the man in the hazmat suit: neither cast nor a pick */
    CHECK(cast_character(16, 0) < 0 && cast_character(12, 0) < 0 && cast_character(1, 0) < 0 && cast_character(68, 0) < 0);
    for (s = 0; s < 50; s++) {
        int got[CAST_CHARACTERS];
        for (c = 0; c < 2; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL) < 0 && cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL) < 0 && cast_model((unsigned)s, 9, c, 2, 0, NULL, CAST_ENEMY_SAME, NULL) < 0);   /* Claire, Chris: no pick */
        for (c = 2; c < CAST_CHARACTERS; c++) {   /* fixed: the cutscene models, nobody as itself, each model for one character (five of each) */
            int same = 0;
            got[c] = cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL);
            CHECK(in(cine, nc, got[c]) && got[c] != own[c] && got[c] == cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL));
            for (int d = 2; d < c; d++) same += got[d] == got[c];
            CHECK(same == 0);
            int a = cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL);   /* all models: none of the character's own looks */
            CHECK((in(all, na, a) || a == cast_zombie_look((unsigned)s, 0, c)) && a != own[c] && (a < ACTOR_PART || a >= ACTOR_PART + 4));
        }
        for (scene = 1; scene < 20; scene++) {   /* random per cutscene: again in each room, the same for the same room */
            int m = cast_model((unsigned)s, (unsigned)scene * 10 + 1, 2, 0, 0, NULL, CAST_ENEMY_SAME, NULL);
            CHECK(m == cast_model((unsigned)s, (unsigned)scene * 10 + 1, 2, 0, 0, NULL, CAST_ENEMY_SAME, NULL) && m != own[2]);
            differ += m != got[2];
        }
    }
    CHECK(differ > 50 * 19 / 2);
    /* Fixed: a character's model does not depend on who is played or where (cast_model takes neither): the same in Claire's
       part (room 0000) and in Chris's (7220), scene 0 in both */
    for (s = 0; s < 50; s++) for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL) == cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL) && cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL) == cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL));   /* most rooms pick another model than the fixed one */
    CHECK(cast_model(1, 0, CAST_ALL, 0, 0, NULL, CAST_ENEMY_SAME, NULL) < 0 && cast_model(1, 0, -1, 0, 0, NULL, CAST_ENEMY_SAME, NULL) < 0);   /* not a character */
    {   /* the creatures only cutscenes show: by room file, record and type, nothing else (the fought ones: never) */
        static const int got[][4] = { { 3000, 2, 51, 10 }, { 3230, 1, 19, 8 }, { 6040, 4, 52, 7 }, { 8030, 2, 5, 9 },
                                      { 6100, 2, 15, -1 }, { 1122, 1, 19, -1 }, { 5010, 0, 19, -1 }, { 9280, 0, 17, -1 }, { 9321, 1, 12, -1 },
                                      { 6040, 4, 1, -1 }, { 6041, 4, 52, -1 }, { 6040, 3, 52, -1 }, { 9050, 1, 52, -1 }, { 7220, 0, 2, -1 } };
        static const signed char own[CAST_CREATURES] = { 16, 17, 20, -1 };   /* Nosferatu, the Tyrant, the Hunter, (Steve's father: the zombies) */
        for (unsigned i = 0; i < sizeof(got) / sizeof(got[0]); i++) CHECK(cast_creature(got[i][0], got[i][1], got[i][2]) == got[i][3]);
        CHECK(!strcmp(cast_name(7), "Nosferatu") && !strcmp(cast_name(CAST_ALL - 1), "Steve's father") && !strcmp(cast_name(6), "Rodrigo"));
        for (s = 0; s < 200; s++)
            for (c = CAST_CHARACTERS; c < CAST_ALL; c++) {
                int f = cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL), r = cast_model((unsigned)s, 77, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL), z = actor_models[f < ACTOR_MODELS ? f : 0].id;
                CHECK(cast_role(c, 0, -1, -1) == CAST_PICK && f >= 0 && f == cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL) && r >= 0);   /* fixed: one model in every scene */
                CHECK(f != own[c - CAST_CHARACTERS] && (c != 8 || f != 18) && (c != 9 || f != 21) && (c != CAST_ALL - 1 || f >= ACTOR_MODELS || (z != 1 && z != 51 && z != 52)));   /* never itself (the Tyrant's twin, the Sweeper) */
                CHECK(in(all, na, f) || f == cast_zombie_look((unsigned)s, 0, c));
                CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL) >= 0);   /* "Cutscene Models": the five used up, a new round */
                for (int d = 2; d < c; d++) CHECK(cast_model((unsigned)s, 0, d, 1, 0, NULL, CAST_ENEMY_SAME, NULL) != f);   /* "All Models" has enough: each its own */
            }
    }
    {   /* the zombies only cutscenes show: 9340's three and 0050's, nothing else; a creature or another zombie look, one per look and
           (seed, scene), whatever the Model Selection; a look of its own never; "Custom" with none of them checked: none */
        static const int vers[3] = { 35, 4, 38 };   /* 9340's records 1..3 */
        int kinds[64] = { 0 }, nk = 0, same = 0;
        unsigned char none[CAST_OFF_BYTES];
        CHECK(cast_scene_zombie(9340, 1, 1) && cast_scene_zombie(9340, 3, 1) && cast_scene_zombie(50, 2, 1) && !cast_scene_zombie(50, 0, 4) && !cast_scene_zombie(9340, 0, 92) && !cast_scene_zombie(9340, 4, 1));
        CHECK(!cast_scene_zombie(9341, 1, 1) && !cast_scene_zombie(70, 3, 1) && !cast_scene_zombie(9290, 3, 1) && !cast_scene_zombie(9340, 1, 52));
        CHECK(cast_role(CAST_CROWD, 1, -1, -1) == CAST_PICK && !strcmp(cast_name(CAST_CROWD), "Zombie"));
        CHECK(cast_crowd_model(1, 0, 1, 99, 1, NULL, CAST_ENEMY_SAME, NULL) < 0 && cast_crowd_model(1, 0, 15, 0, 1, NULL, CAST_ENEMY_SAME, NULL) < 0);   /* not a zombie look */
        for (s = 0; s < 300; s++) {
            int m[3];
            for (int i = 0; i < 3; i++) {
                int a = cast_crowd_model((unsigned)s, 0, 1, vers[i], 0, NULL, CAST_ENEMY_SAME, NULL), z = a >= 0 && a < ACTOR_MODELS ? actor_models[a].id : -1;
                m[i] = a;
                CHECK(a >= 0 && a < ACTOR_MODELS && a == cast_crowd_model((unsigned)s, 0, 1, vers[i], 0, NULL, CAST_ENEMY_SAME, NULL));   /* deterministic */
                CHECK(a == cast_crowd_model((unsigned)s, 0, 1, vers[i], 1, NULL, CAST_ENEMY_SAME, NULL) && a == cast_crowd_model((unsigned)s, 0, 1, vers[i], 2, NULL, CAST_ENEMY_SAME, NULL));   /* any Model Selection */
                CHECK(cast_character(z, 0) < 0 && actor_models[a].anim != ANIM_IDLE && !(z == 1 && actor_models[a].mdlver == vers[i]));   /* a creature or another look */
                nk += !kinds[a]; kinds[a] = 1;
            }
            same += m[0] == m[1] || m[1] == m[2] || m[0] == m[2];
        }
        CHECK(nk > 12 && kinds[17] && kinds[22] && kinds[16] && kinds[25]);   /* the Tyrant, the Bandersnatch, Nosferatu, the anatomist among them */
        CHECK(same < 300 / 4);   /* the looks of a scene mostly differ */
        memset(none, 0xFF, sizeof(none));
        CHECK(cast_crowd_model(5, 0, 1, 35, 2, none, CAST_ENEMY_SAME, NULL) < 0);
    }
    {   /* "Enemy Models": its list is the monsters of "All models" and the zombies, then the characters (their cutscene models,
           Claire's second costume; no import), monsters first; All Enemies gives the creatures and the scene zombies only the
           monsters, each creature still never itself, fixed per seed; Custom the checked ones (the characters unchecked at
           first: then as All Enemies); the characters' picks are Same as Cast's; Off: they stay themselves */
        static const int mons[] = { 15, 16, 17, 19, 20, 21, 22, 25, CAST_ZOMBIES };   /* Alexia's first form, Nosferatu, Tyrant, Monster Steve, Hunter, Sweeper, Bandersnatch, anatomist */
        static const int chars[] = { 0, 1, 3, 5, ACTOR_PART + 4, 8, 12, 13 };   /* the seven cutscene models and Claire's second costume, in the cast list's order */
        static const signed char own[CAST_CREATURES] = { 16, 17, 20, -1 };
        const int nm = (int)(sizeof(mons) / sizeof(mons[0])), nch = (int)(sizeof(chars) / sizeof(chars[0]));
        unsigned char eoff[CAST_ENEMY_BYTES] = { 0 }, def[CAST_ENEMY_BYTES] = { 0 };
        short pos[64];
        int mdl[64], ne = cast_custom_list(2, 3, pos, mdl, 64), total, seen = 0, tyr = -1, stv = -1, drawn = 0, k;
        char key[64];
        CHECK(ne == nm + nch && ne <= CAST_ENEMY_BYTES * 8 && cast_custom_count(2, NULL, 3, &total) == ne && total == ne);
        for (k = 0; k < ne; k++) {
            CHECK((k < nm ? in(mons, nm, mdl[k]) : mdl[k] == chars[k - nm]) && pos[k] >= 0 && pos[k] < ne && (pos[k] < nm) == (k < nm) && !(seen & (1 << pos[k])));
            seen |= 1 << pos[k];
            if (mdl[k] == 17) tyr = pos[k];
            if (mdl[k] == 3) stv = pos[k];
        }
        CHECK(cast_off_key(2, eoff, "Tyrant") && cast_off_at(eoff, tyr) && cast_off_key(2, eoff, cast_entry_key(3, key, sizeof key)) && cast_off_at(eoff, stv) && !cast_off_key(2, eoff, "import:x"));
        CHECK(cast_custom_count(2, eoff, 0, NULL) == ne - 2 && !strcmp(cast_entry_key(CAST_ZOMBIES, key, sizeof key), "Zombies"));
        cast_enemy_default(def);   /* as it starts: the monsters checked, the characters not */
        for (k = 0; k < ne; k++) CHECK(cast_off_at(def, pos[k]) == (k >= nm));
        memset(eoff, 0xFF, sizeof eoff);
        cast_off_put(eoff, tyr, 0);
        cast_enemy_default(eoff);   /* (the monsters' bits kept: an older file's lines) */
        CHECK(cast_custom_count(2, eoff, 0, NULL) == 1 && !cast_off_at(eoff, tyr));
        for (s = 0; s < 300; s++) {
            int got[CAST_ALL];
            for (c = 2; c < CAST_ALL; c++) {
                int a = cast_model((unsigned)s, 0, c, s & 1, 0, NULL, CAST_ENEMY_ALL, NULL);
                CHECK(a == cast_model((unsigned)s, 0, c, s & 1, 0, NULL, CAST_ENEMY_CUSTOM, def));   /* no character checked: All Enemies' picks */
                if (c < CAST_CHARACTERS) {   /* the characters as before */
                    CHECK(a == cast_model((unsigned)s, 0, c, s & 1, 0, NULL, CAST_ENEMY_SAME, NULL) && a == cast_model((unsigned)s, 0, c, s & 1, 0, NULL, CAST_ENEMY_OFF, NULL));
                    continue;
                }
                CHECK(a >= 0 && a == cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_ALL, NULL) && a != own[c - CAST_CHARACTERS]);   /* any Model Selection, never itself */
                CHECK(in(mons, nm, a) || (a < ACTOR_MODELS && (actor_models[a].id == 1 || actor_models[a].id == 51 || actor_models[a].id == 52) && c != CAST_ALL - 1));
                CHECK((c != 8 || a != 18) && (c != 9 || a != 21) && cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_OFF, NULL) == -1);
                for (int d = CAST_CHARACTERS; d < c; d++) CHECK(got[d] != a || in(mons, nm, a) == 0);   /* the creatures differ (the zombies' entry: a look each) */
                got[c] = a;
            }
            for (int i = 0; i < 3; i++) {   /* the scene zombies: All Enemies is All Models' monsters (so is Custom with no character), Off none */
                static const int vers[3] = { 35, 4, 38 };
                int a = cast_crowd_model((unsigned)s, (unsigned)s % 3, 1, vers[i], 1, NULL, CAST_ENEMY_ALL, NULL);
                CHECK(cast_crowd_model((unsigned)s, 0, 1, vers[i], 0, NULL, CAST_ENEMY_ALL, NULL) == cast_crowd_model((unsigned)s, 0, 1, vers[i], 1, NULL, CAST_ENEMY_SAME, NULL));
                CHECK(a == cast_crowd_model((unsigned)s, (unsigned)s % 3, 1, vers[i], 1, NULL, CAST_ENEMY_CUSTOM, def));
                CHECK(cast_crowd_model((unsigned)s, 0, 1, vers[i], 1, NULL, CAST_ENEMY_OFF, NULL) == -1);
            }
        }
        for (k = 0; k < ne; k++) cast_off_put(eoff, k, k != tyr);   /* Custom: only the Tyrant: he stays himself, the others become him */
        for (s = 0; s < 50; s++) {
            for (c = CAST_CHARACTERS; c < CAST_ALL; c++) CHECK(cast_model((unsigned)s, (unsigned)s, c, 1, 0, NULL, CAST_ENEMY_CUSTOM, eoff) == (c == 8 ? -1 : 17));
            CHECK(cast_crowd_model((unsigned)s, 0, 1, 35, 2, NULL, CAST_ENEMY_CUSTOM, eoff) == 17);
        }
        for (k = 0; k < ne; k++) cast_off_put(eoff, k, k != stv);   /* only Steve's cutscene model: every creature and scene zombie becomes it */
        for (s = 0; s < 50; s++) {
            static const int vers[4] = { 35, 4, 38, 0 };   /* (9340's, 0050's) */
            for (c = 2; c < CAST_ALL; c++) {
                int a = cast_model((unsigned)s, (unsigned)s, c, 0, 0, NULL, CAST_ENEMY_CUSTOM, eoff);
                CHECK(c < CAST_CHARACTERS ? a == cast_model((unsigned)s, (unsigned)s, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL) : a == 3);   /* (the characters' as before) */
            }
            for (int i = 0; i < 4; i++) CHECK(cast_crowd_model((unsigned)s, (unsigned)s, 1, vers[i], 1, NULL, CAST_ENEMY_CUSTOM, eoff) == 3);
        }
        memset(eoff, 0, sizeof eoff);   /* everything checked: monsters and characters, never its own look; the characters' picks as before */
        for (s = 0; s < 300; s++)
            for (c = 2; c < CAST_ALL; c++) {
                int a = cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_CUSTOM, eoff);
                if (c < CAST_CHARACTERS) { CHECK(a == cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL)); continue; }
                CHECK(a >= 0 && a != own[c - CAST_CHARACTERS] && (in(mons, nm, a) || in(chars, nch, a) || (a < ACTOR_MODELS && (actor_models[a].id == 1 || actor_models[a].id == 51 || actor_models[a].id == 52) && c != CAST_ALL - 1)));
                drawn += in(chars, nch, a);
            }
        CHECK(drawn > 300);   /* (the characters drawn: about half of the 1200 picks) */
        memset(eoff, 0xFF, sizeof eoff);   /* none checked: nobody changes */
        for (c = CAST_CHARACTERS; c < CAST_ALL; c++) CHECK(cast_model(3, 0, c, 1, 0, NULL, CAST_ENEMY_CUSTOM, eoff) == -1);
        CHECK(cast_crowd_model(3, 0, 1, 35, 1, NULL, CAST_ENEMY_CUSTOM, eoff) == -1);
    }
    {   /* the "Custom" lists start with the four cutscene models (Claire, Chris, Steve, Wesker; "Randomize Character Model"'s has
           Steve's and Wesker's), then the rest in the "You appear as" list's order */
        short pos[128];
        int mdl[128];
        CHECK(cast_custom_list(0, 0, pos, mdl, 128) > 4 && mdl[0] == 0 && mdl[1] == 1 && mdl[2] == 3 && mdl[3] == 5);
        CHECK(cast_custom_list(1, 0, pos, mdl, 128) > 2 && mdl[0] == 3 && mdl[1] == 5);
    }
    {   /* the seed decides: two seeds differ somewhere */
        int d = 0;
        for (c = 2; c < CAST_CHARACTERS; c++) d += cast_model(1, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL) != cast_model(2, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL);
        CHECK(d > 0);
    }
    {   /* "Custom": the list is "All models" and Claire's second costume (each position once, in the menus' order);
           that unchecked is "All models";
           only checked models are picked; one checked: everyone but its own character as it; none: nobody changes */
        static unsigned char off[CAST_OFF_BYTES];
        short pos[128];
        int mdl[128], nl = cast_custom_list(0, 0, pos, mdl, 128), seen[128] = { 0 }, total, tyrant = -1, steve = -1, zomb = -1, cust[128];
        int nf = cast_pool(2, 0, cust, 128, off), claire = 0;
        CHECK(nl == nf && nf == na + 1);   /* ("All models" and Claire's second costume; no player file with a talking twin) */
        for (int i = 0; i < 4; i++) CHECK(!in(cust, nf, ACTOR_PART + i));
        CHECK(in(cust, nf, 0) && in(cust, nf, 1) && in(cust, nf, ACTOR_PART + 4));
        for (int i = 0; i < nf; i++) for (int j = 0; j < i; j++) CHECK(cust[i] != cust[j]);
        for (int j = 0, i = 0; j < nf; j++) if (cust[j] != ACTOR_PART + 4) CHECK(cust[j] == all[i++]);   /* ("All models" is "Custom"'s list without the costume) */
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
        for (int j = 0; j < nf; j++) cast_off_put(off, j, cust[j] == ACTOR_PART + 4);   /* all but the costume: "All models"' picks */
        for (s = 0; s < 50; s++) for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 2, 0, off, CAST_ENEMY_SAME, NULL) == cast_model((unsigned)s, 0, c, 1, 0, NULL, CAST_ENEMY_SAME, NULL));
        for (int j = 0; j < nf; j++) cast_off_put(off, j, j != tyrant && j != steve && j != zomb);   /* Tyrant, Steve's cutscene model, the zombies */
        CHECK(cast_custom_count(0, off, 0, NULL) == 3 && cast_pool(2, 0, all + 64, 64, off) == 3);
        for (s = 0; s < 200; s++)
            for (c = 2; c < CAST_CHARACTERS; c++) {
                int a = cast_model((unsigned)s, (unsigned)s % 3, c, 2, 0, off, CAST_ENEMY_SAME, NULL);
                CHECK(a == 17 || (a == 3 && c != 2) || a == cast_zombie_look((unsigned)s, (unsigned)s % 3, c));
            }
        for (int j = 0; j < nf; j++) cast_off_put(off, j, j != steve);   /* only Steve's look: Steve stays himself */
        for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model(7, 0, c, 2, 0, off, CAST_ENEMY_SAME, NULL) == (c <= 2 ? -1 : 3));
        cast_off_put(off, steve, 1);   /* none */
        for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model(7, 0, c, 2, 0, off, CAST_ENEMY_SAME, NULL) == -1);
        /* recvx.ini: by key; unknown keys ignored, so are the player files' with a talking twin (an older list's) */
        {
            char k[128];
            memset(cast_off, 0, sizeof(cast_off));
            CHECK(cast_off_key(0, cast_off, "Tyrant") && cast_off_key(0, cast_off, "Zombies") && cast_off_key(0, cast_off, cast_entry_key(5, k, sizeof(k))) && !cast_off_key(0, cast_off, "No Such Model") && !cast_off_key(0, cast_off, "import:gone"));
            CHECK(!cast_off_key(0, cast_off, "Claire") && !cast_off_key(0, cast_off, "Chris") && !cast_off_key(0, cast_off, "Steve") && !cast_off_key(0, cast_off, "Wesker") && cast_off_key(0, cast_off, "Claire (Alternate Costume)"));
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
            for (c = 0; c < CAST_CHARACTERS; c++) seen |= cast_model((unsigned)s, 0, c, 0, 2, NULL, CAST_ENEMY_SAME, NULL) >= ACTOR_MODELS;
        CHECK(seen);
        for (s = 0; s < 40; s++)
            for (c = 0; c < CAST_CHARACTERS; c++) CHECK(cast_model((unsigned)s, 0, c, 0, 0, NULL, CAST_ENEMY_SAME, NULL) < ACTOR_MODELS);
        actor_imports[2] = (ActorModel){ .name = "Jim", .anim = ANIM_MALE };
        actor_import_n = 3;
        CHECK(cast_import_id() != id);
        {   /* "Custom" with imports: after the built-in ones, by their folder in recvx.ini; unchecked, never picked */
            short pos[128];
            int mdl[128], nl = cast_custom_list(0, 3, pos, mdl, 128), nimp = 0, nb = na + 1;   /* (the built-in ones: "All models" and Claire's second costume) */
            char k[128];
            actor_import_file[0] = "mods/import/outbreak2/kevin01/kevin01.cvxm";
            actor_import_file[1] = "mods/import/outbreak2/cindy01/cindy01.cvxm";
            actor_import_file[2] = "mods/import/outbreak2/jim01/jim01.cvxm";
            CHECK(nl == nb + 3 && pos[nb] == nb && mdl[nb + 2] == ACTOR_MODELS + 2 && cast_custom_list(0, 0, pos, mdl, 128) == nb);
            CHECK(!strcmp(cast_entry_key(ACTOR_MODELS + 1, k, sizeof(k)), "import:cindy01") && !strcmp(cast_entry_name(ACTOR_MODELS + 1), "Cindy"));
            CHECK(cast_off_key(0, cast_off, "import:cindy01") && cast_off_at(cast_off, nb + 1) && !cast_off_at(cast_off, nb) && cast_custom_count(0, cast_off, 3, NULL) == nb + 2);
            for (s = 0; s < 200; s++)
                for (c = 0; c < CAST_CHARACTERS; c++) {
                    int a = cast_model((unsigned)s, 0, c, 2, 3, cast_off, CAST_ENEMY_SAME, NULL);
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
        int rec = -1, k, n3, n1 = 0, n2 = 0;
        unsigned w[] = { 0x10, 0x100, 0x18, 0x200, 0x1C, 0x300, 0x108, 0x180, 0x188, 2, 0x210, 0x280, 0x300, 0x0C, 0x304, 0x0E, 0x308, 0x10 };
        for (k = 0; k < (int)(sizeof w / sizeof w[0]); k += 2) memcpy(d + w[k], &w[k + 1], 4);
        d[0x180 + 4] = 93; d[0x180 + 0x24 + 4] = 91;   /* record 0 Steve, 1 Claire */
        memcpy(d + 0x280, lips, sizeof lips);
        memcpy(d + 0x310, ev, sizeof ev);
        static const VoiceCurated none[] = { { 0 } };
        voice_curation_use(none);   /* (the scan's own rules first, whatever the built-in table holds) */
        CHECK(voice_count(0) == -1 && !voice_pick(1, 1020, 1, 5, 0, 0, 0, NULL, NULL, VOICE_PICK_ANY));   /* (not read yet) */
        CHECK(voice_lipset(ev + 8, 64, coop_insn_len_raw, &rec) == 1 && rec == 1 && voice_lipset(ev + 36, 64, coop_insn_len_raw, &rec) == -1);
        {   /* a LipSet right before the VoiceOn (RM_3130): only LipStart and MessageSet between */
            static const unsigned char pre[] = { 61, 1, 1, 0, 63, 1, 1, 0, 31, 0, 0, 1, 25, 0, 76, 0, 1, 0, 0, 0, 61, 1, 1, 0, 25, 0, 77, 0, 1, 0, 0, 0 };
            CHECK(voice_lipset_before(pre + 4, pre + 12, coop_insn_len_raw) && voice_lipset_before(pre + 24, pre + 24, coop_insn_len_raw) &&
                  !voice_lipset_before(pre + 4, pre + 24, coop_insn_len_raw) && !voice_lipset_before(pre + 4, pre + 10, coop_insn_len_raw));
        }
        voice_scan_room(1020, d, sizeof d, coop_insn_len_raw);
        voice_scan_room(1020, d, sizeof d, coop_insn_len_raw);   /* (the same lines again: once) */
        voice_scan_room(9000, d, sizeof d, coop_insn_len_raw);   /* (the other disc's archive: other lines) */
        d[0x310 + 34] = 2;   /* 1040: VoiceOn 7 names record 2, which no line with a LipSet names */
        voice_scan_room(1040, d, sizeof d, coop_insn_len_raw);
        d[0x310 + 6] = 1; d[0x310 + 22] = 0;   /* 1050, 1030: the same script naming each line's speaker */
        voice_scan_room(1050, d, sizeof d, coop_insn_len_raw);
        d[0x310 + 34] = 1;
        voice_scan_room(1030, d, sizeof d, coop_insn_len_raw);
        d[0x310 + 29] = 0;   /* 1060: VoiceOn 7 placed at the player (kind 0), as VoiceOn 8 whose LipSet names record 0 */
        voice_scan_room(1060, d, sizeof d, coop_insn_len_raw);
        d[0x310 + 1] = 0;    /* 1070: VoiceOn 5 too, whose LipSet names record 1: two speakers at the player */
        voice_scan_room(1070, d, sizeof d, coop_insn_len_raw);
        CHECK(!voice_sure(1030, 7, 1) && voice_said_by(1060, 7) == -1);   /* (not read yet) */
        voice_scan_done();
        /* "Talking Mouths": VoiceOn 7 without a LipSet is surely its record's where the script's lines with one that name that
           record are said by it (1030, not 1020), or no line names it and every one names its speaker (1050, not 1040) */
        CHECK(voice_sure(1030, 7, 1) && !voice_sure(1020, 7, 1) && voice_sure(1050, 7, 2) && !voice_sure(1040, 7, 2) && !voice_sure(1030, 7, 0) &&
              !voice_sure(1030, 8, 0) && !voice_sure(1030, 5, 1) && voice_sure_count() == 3);
        /* a line at the player without a LipSet: the record the script's other such lines name (1060), none where they name
           two (1070) or the line names a record (1030) */
        CHECK(voice_said_by(1060, 7) == 0 && voice_sure(1060, 7, 0) && voice_said_by(1070, 7) == -1 && voice_said_by(1030, 7) == -1 &&
              voice_said_by(1060, 8) == -1);
        CHECK(voice_count(0) == 2 && voice_count(2) == 2 && voice_count(1) == 0 && voice_count(3) == 0);
        l = voice_line(0, 0);
        CHECK(l && l->vid == 5 && l->disc == 1 && l->room == 1020 && l->lip_n == 3 && l->lip[0] == 3 && l->lip[2] == 0xFF);
        l = voice_line(2, 1);
        CHECK(l && l->vid == 6 && l->disc == 2 && l->room == 9000 && l->lip_n == 5 && l->lip[3] == 20 && !voice_line(2, 2));
        for (s = 0, k = 0, n3 = 0; s < 256; s++) {   /* deterministic, of the character asked for, both lines in use */
            a = voice_pick((unsigned)s, 1020, 1, 40, 2, 0, 0, NULL, NULL, VOICE_PICK_ANY);
            CHECK(a && a->chr == 2 && a == voice_pick((unsigned)s, 1020, 1, 40, 2, 0, 0, NULL, NULL, VOICE_PICK_ANY));
            k |= 1 << (a->disc - 1);
            /* one that ends in time: the 1 s one where 3 s is too long, also without VOICE_PICK_FIT (n2: never the 3 s one);
               the shortest where none fits; where both do, the one filling a 3 s original likelier (6 to 1.6) */
            CHECK(voice_pick((unsigned)s, 1020, 1, 40, 2, 1000, 2000, line_ms, NULL, VOICE_PICK_ANY | VOICE_PICK_FIT) == voice_line(2, 1) &&
                  voice_pick((unsigned)s, 1020, 1, 40, 2, 300, 500, line_ms, NULL, VOICE_PICK_ANY | VOICE_PICK_FIT) == voice_line(2, 1));
            n2 += voice_pick((unsigned)s, 1020, 1, 40, 2, 1000, 2000, line_ms, NULL, VOICE_PICK_ANY) == voice_line(2, 0);
            a = voice_pick((unsigned)s, 1020, 1, 40, 2, 3000, 4000, line_ms, NULL, VOICE_PICK_ANY);
            CHECK(a == voice_pick((unsigned)s, 1020, 1, 40, 2, 3000, 4000, line_ms, NULL, VOICE_PICK_ANY));
            n3 += a == voice_line(2, 0);
            /* the one not said; where only the one just said ends in time, the other (faded out), never the same twice in a
               row; with fit too (cast_glue.c: quiet) */
            CHECK(voice_pick((unsigned)s, 1020, 1, 40, 2, 3000, 4000, line_ms, said_first, VOICE_PICK_ANY) == voice_line(2, 1) &&
                  voice_pick((unsigned)s, 1020, 1, 40, 2, 1000, 2000, line_ms, said_second, VOICE_PICK_ANY) == voice_line(2, 0) &&
                  voice_pick((unsigned)s, 1020, 1, 40, 2, 1000, 2000, line_ms, said_second, VOICE_PICK_ANY | VOICE_PICK_FIT) == voice_line(2, 0));
            n1 += voice_pick((unsigned)s, 1020, 1, 40, 2, 9000, 9000, line_ms_short, NULL, VOICE_PICK_ANY) == voice_line(2, 1);   /* (a 0.5 s gasp for a 9 s line) */
        }
        CHECK(n1 > 0 && n1 < 256 / 10 && k == 3 && n3 > 256 * 3 / 4 && n3 < 256 && n2 == 0 && !voice_pick(1, 1020, 1, 40, 1, 0, 0, NULL, NULL, VOICE_PICK_ANY) && !voice_pick(1, 1020, 1, 40, 7, 0, 0, NULL, NULL, VOICE_PICK_ANY));
        CHECK(voice_model_chr(3) == 2 && voice_model_chr(5) == 3 && voice_model_chr(6) == 3 && voice_model_chr(7) == 3 && voice_model_chr(8) == 4 &&
              voice_model_chr(11) == 4 && voice_model_chr(12) == 5 && voice_model_chr(13) == 6 && voice_model_chr(ACTOR_PART + 3) == 3 &&
              voice_model_chr(ACTOR_PART + 4) == 0 && voice_model_chr(17) == -1 && voice_model_chr(14) == -1 && voice_model_chr(ACTOR_MODELS) == -1 &&
              voice_model_chr(CAST_ZOMBIES) == -1);   /* Steve, Wesker's three, Alfred's, Alexia, Rodrigo, files, the Tyrant, the hazmat man, an import */
        {   /* the voice tool's speakers (voice_curation.json "characters", its "speaker" an index into them; voice_curation.py
               writes it as the table's character) are cast.h's characters in order */
            static const char* tool[CAST_CHARACTERS] = { "Claire", "Chris", "Steve", "Wesker", "Alfred", "Alexia", "Rodrigo" };
            for (k = 0; k < CAST_CHARACTERS; k++) CHECK(!strcmp(cast_name(k), tool[k]) && voice_model_chr(own[k]) == k);
        }
        {   /* "Custom" as it starts: the models with lines and a moving mouth checked, and only those are cast */
            short ps[CAST_CUSTOM_MAX];
            int md[CAST_CUSTOM_MAX], n, on = 0, seen = 0;
            cast_off_default();
            n = cast_custom_list(0, 0, ps, md, CAST_CUSTOM_MAX);
            for (k = 0; k < n; k++) {
                int chr = md[k] >= ACTOR_PART ? -1 : voice_model_chr(md[k]);   /* (a player file: no mouth movement, unchecked) */
                CHECK(cast_off_at(cast_off, ps[k]) == (chr < 0));
                if (chr >= 0) { on++; seen |= 1 << chr; }
                if (getenv("CAST_DEFAULT_LIST")) printf("%s %s\n", chr >= 0 ? "[x]" : "[ ]", cast_entry_name(md[k]));
            }
            CHECK(seen == 0x7F && on == cast_custom_count(0, cast_off, 0, NULL) && on < n);   /* (all seven) */
            for (s = 0; s < 64; s++)
                for (int c = 2; c < CAST_ALL; c++) {
                    int m = cast_model((unsigned)s, (unsigned)s, c, 2, 0, cast_off, CAST_ENEMY_SAME, NULL);
                    CHECK(m < 0 || (voice_model_chr(m) >= 0 && m < ACTOR_PART));
                }
            memset(cast_off, 0, sizeof(cast_off));
        }
        {   /* the lines checked by hand (voice_curated): a speaker over the LipSet's, also without lips; none/other and "do not
               use" out of the pool; grunts in a pool of their own, also the tool's guess for one without a LipSet; a group's
               first part stands for it. Then the catalogue on disk: the same back, nothing from a damaged or other one */
            static const VoiceCurated t[] = {
                { 1, 5, 3, VOICE_CONFIRMED, 0xFFFF },                /* Claire's LipSet: Wesker says it */
                { 1, 7, 2, VOICE_CONFIRMED | VOICE_GRUNT, 0xFFFF },  /* no LipSet: Steve's grunt, without lips */
                { 2, 5, 0, VOICE_EXCLUDE, 0xFFFF },                  /* do not use */
                { 2, 6, -1, VOICE_CONFIRMED, 0xFFFF },               /* none of the cast */
                { 2, 7, 1, VOICE_CONFIRMED, 8 },                     /* Chris's group 2/7, 2/8 */
                { 2, 8, 1, VOICE_CONFIRMED | VOICE_PART, 0xFFFF },
                { 2, 99, 4, VOICE_CONFIRMED, 0xFFFF },               /* Alfred's, which no script says */
                { 2, 98, -1, VOICE_GRUNT, 0xFFFF },                  /* (a grunt of nobody: none) */
                { 0 } };
            unsigned char *b, *b2;
            size_t n, n2;
            int gr = 0, sp = 0;
            voice_curation_use(t);
            memcpy(d + 0x310, ev, sizeof ev);
            voice_scan_room(1020, d, sizeof d, coop_insn_len_raw);
            voice_scan_room(9000, d, sizeof d, coop_insn_len_raw);
            d[0x310 + 6] = 1; d[0x310 + 22] = 0;   /* (1030: VoiceOn 7 surely record 1's, as above) */
            voice_scan_room(1030, d, sizeof d, coop_insn_len_raw);
            voice_scan_done();
            CHECK(voice_sure_count() == 1 && voice_sure(1030, 7, 1));
            CHECK(voice_curated(1, 7) == &t[1] && !voice_curated(1, 6) && !voice_curated(3, 5));
            CHECK(voice_count(0) == 0 && voice_count(1) == 1 && voice_count(2) == 2 && voice_count(3) == 1 && voice_count(4) == 1 && voice_count(5) == 0);
            l = voice_line(4, 0);
            CHECK(l && l->disc == 2 && l->vid == 99 && l->room == VOICE_NO_ROOM && !l->lip);
            l = voice_line(3, 0);
            CHECK(l && l->disc == 1 && l->vid == 5 && l->lip_n == 3 && !l->grunt);   /* (with its lips) */
            l = voice_line(2, 1);
            CHECK(l && l->disc == 1 && l->vid == 7 && !l->lip_n && !l->lip && l->grunt && voice_line(2, 0)->vid == 6 && !voice_line(2, 0)->grunt);
            l = voice_line(1, 0);
            CHECK(l && l->disc == 2 && l->vid == 7 && !l->lip_n);
            for (s = 0; s < 64; s++) {   /* a grunt for a grunt, a spoken line for one; a short one either */
                CHECK(voice_pick((unsigned)s, 1020, 1, 40, 2, 0, 0, NULL, NULL, VOICE_PICK_GRUNTS) == voice_line(2, 1) &&
                      voice_pick((unsigned)s, 1020, 1, 40, 2, 3000, 4000, line_ms, NULL, VOICE_PICK_SPOKEN) == voice_line(2, 0));
                a = voice_pick((unsigned)s, 1020, 1, 40 + s, 2, 0, 0, NULL, NULL, VOICE_PICK_ANY);
                gr |= a == voice_line(2, 1); sp |= a == voice_line(2, 0);
            }
            CHECK(gr && sp && !voice_pick(1, 1020, 1, 40, 3, 0, 0, NULL, NULL, VOICE_PICK_GRUNTS) && voice_pick(1, 1020, 1, 40, 3, 0, 0, NULL, NULL, VOICE_PICK_SPOKEN));
            CHECK((b = voice_cache_save(42, &n)) && n > 28);
            CHECK(!voice_cache_load(b, n, 43) && !voice_cache_load(b, n - 1, 42) && !voice_cache_load(b, 10, 42));   /* (another key, cut short) */
            b[n - 1] ^= 1;
            CHECK(!voice_cache_load(b, n, 42));   /* (a byte changed: its checksum) */
            b[n - 1] ^= 1;
            CHECK(voice_cache_load(b, n, 42) && voice_count(0) == 0 && voice_count(2) == 2 && voice_line(2, 1)->grunt && !voice_line(2, 1)->lip &&
                  voice_line(3, 0)->lip_n == 3 && voice_line(3, 0)->lip[0] == 3 && voice_sure_count() == 1);
            CHECK((b2 = voice_cache_save(42, &n2)) && n2 == n && !memcmp(b, b2, n));   /* (the same catalogue) */
            {   /* the cache's key: another table (a speaker, a flag changed, a line more) is another key, its cache not taken */
                VoiceCurated t2[sizeof t / sizeof t[0] + 1];
                unsigned h0 = voice_cat_hash(), h;
                unsigned char* b3;
                size_t n3;
                CHECK((b3 = voice_cache_save(h0, &n3)));   /* (as voices.cache for this table: the key teleport.c makes has voice_cat_hash) */
                memcpy(t2, t, sizeof t);
                voice_curation_use(t2);
                CHECK(voice_cat_hash() == h0);
                t2[0].chr = 5;   /* (Wesker's line now Alexia's) */
                CHECK((h = voice_cat_hash()) != h0 && !voice_cache_load(b3, n3, h) && voice_cache_load(b3, n3, h0));
                free(b3);
                t2[0].chr = 3; t2[2].flags = VOICE_GRUNT;
                CHECK(voice_cat_hash() != h0 && voice_cat_hash() != h);
                t2[2].flags = VOICE_EXCLUDE;
                memmove(t2 + 1, t2, sizeof t); t2[0] = (VoiceCurated){ 1, 3, 3, VOICE_CONFIRMED, 0xFFFF };
                CHECK(voice_cat_hash() != h0);
                voice_curation_use(none);
                CHECK(voice_cat_hash() != h0);
            }
            free(b); free(b2);
            voice_curation_use(NULL);
        }
        {   /* the picks in a row (cast_glue.c: the lines said since the room load): those not said first; then none of the last
               min(pool - 1, max(3, pool / 2)) again, so no line twice in a row and no A-B-A, across the rounds too; pools of
               2, 3, 5, 14 (Alfred's) and 100 lines, lengths 0.4 to 9.4 s, originals 1.2 to 9 s. Each original gets one line that
               ends by its end and grace (ms + 1 s) whenever the character has one that is not the last; the cooldown gives way
               only to that (never a longer line while one ends in time), and the length is a preference among those */
            static const int pools[] = { 2, 3, 5, 14, 100 };
            static VoiceCurated tp[101];
            for (int q = 0; q < 5; q++) {
                int P = pools[q], N = P - 1 < (P / 2 > 3 ? P / 2 : 3) ? P - 1 : (P / 2 > 3 ? P / 2 : 3), longer = 0, eased = 0;
                for (k = 0; k < P; k++) tp[k] = (VoiceCurated){ 1, (unsigned short)(1000 + k), 4, VOICE_CONFIRMED, 0xFFFF };
                tp[P] = (VoiceCurated){ 0 };
                voice_curation_use(tp);
                voice_scan_done();   /* (lines no script says: the table's alone) */
                CHECK(voice_count(4) == P);
                for (s = 1; s <= 3; s++) {
                    const VoiceLine* seq[600];
                    hist.n = 0;
                    for (k = 0; k < 600; k++) {
                        int ms = 1200 + (int)((k * 3571u + (unsigned)s * 977u) % 7800u), j;
                        a = voice_pick((unsigned)s, 1020, 1, k, 4, ms, ms + 1000, len_var, hist_age, VOICE_PICK_SPOKEN);
                        CHECK(a && a->chr == 4 && a == voice_pick((unsigned)s, 1020, 1, k, 4, ms, ms + 1000, len_var, hist_age, VOICE_PICK_SPOKEN));
                        int fresh = 0, cool = 0, other = 0, ag, shortest = -1;   /* (lines that end in time: not said, not among the last N, not the last) */
                        for (j = 0; j < P; j++) {
                            const VoiceLine* x = voice_line(4, j);
                            ag = voice_said_age(&hist, x);
                            if (ag != 0 && (shortest < 0 || len_var(x) < len_var(voice_line(4, shortest)))) shortest = j;
                            if (len_var(x) > ms + 1000) continue;
                            fresh += ag < 0; cool += ag < 0 || ag >= N; other += ag != 0;
                        }
                        ag = voice_said_age(&hist, a);
                        CHECK(!k || a != seq[k - 1]);                                            /* (never twice in a row) */
                        if (fresh) CHECK(ag < 0 && len_var(a) <= ms + 1000);                     /* (not said first) */
                        else if (cool) CHECK((ag < 0 || ag >= N) && len_var(a) <= ms + 1000);   /* (none of the last N) */
                        else if (other) CHECK(len_var(a) <= ms + 1000);                          /* (the cooldown eased: still in time) */
                        else CHECK(a == voice_line(4, shortest));                                /* (none in time: the shortest, faded out) */
                        eased += !cool && other;
                        longer += len_var(a) > ms + 1000;
                        voice_said_add(&hist, a);
                        seq[k] = a;
                    }
                }
                CHECK((P < 14 || longer == 0) && (P < 100 || eased == 0));   /* (Alfred's 14 or more: always one in time; 100: the cooldown kept too) */
                {   /* gasps (fit: only those that end in time) with one short line: once, then quiet while it is among the last N */
                    int said = 0;
                    hist.n = 0;
                    for (k = 0; k < 40; k++) {
                        int ms = k % 3 ? 900 : 4000;   /* (a gasp, a gasp, a line) */
                        a = voice_pick(7, 1020, 1, k, 4, ms, ms + 1000, len_var, hist_age, VOICE_PICK_SPOKEN | (ms < VOICE_SHORT_MS ? VOICE_PICK_FIT : 0));
                        CHECK(a && voice_said_age(&hist, a) != 0);
                        if (ms < VOICE_SHORT_MS && len_var(a) > ms + 1000) continue;   /* (cast_glue.c: quiet, not said) */
                        CHECK(ms >= VOICE_SHORT_MS || voice_said_age(&hist, a) < 0 || voice_said_age(&hist, a) >= N);   /* (a gasp: never eased) */
                        voice_said_add(&hist, a);
                        said++;
                    }
                    CHECK(said > 40 / 3);
                }
            }
            {   /* a pool of one line (Alexia's only grunt): said, then none (quiet) for the next, never the same twice in a row,
                   also where it is the only one that ends in time or none does */
                const VoiceLine* one;
                tp[0] = (VoiceCurated){ 1, 1000, 4, VOICE_CONFIRMED, 0xFFFF };
                tp[1] = (VoiceCurated){ 0 };
                voice_curation_use(tp);
                voice_scan_done();
                CHECK(voice_count(4) == 1 && (one = voice_line(4, 0)));
                hist.n = 0;
                CHECK(voice_pick(3, 1020, 1, 1, 4, 9000, 10000, len_var, hist_age, VOICE_PICK_SPOKEN) == one);
                voice_said_add(&hist, one);
                CHECK(!voice_pick(3, 1020, 1, 2, 4, 9000, 10000, len_var, hist_age, VOICE_PICK_SPOKEN) &&
                      !voice_pick(3, 1020, 1, 2, 4, 100, 200, len_var, hist_age, VOICE_PICK_SPOKEN));
            }
            for (k = 0; k < 100; k++) tp[k] = (VoiceCurated){ 1, (unsigned short)(1000 + k), 4, VOICE_CONFIRMED, 0xFFFF };
            tp[100] = (VoiceCurated){ 0 };
            voice_curation_use(tp);
            voice_scan_done();
            for (k = 0; k < VOICE_SAID_MAX + 5; k++) voice_said_add(&hist, voice_line(4, k % 100));   /* (full: the oldest go) */
            {
                VoiceLine other = *voice_line(4, 4);
                other.chr = 3;   /* (another character's: not said) */
                CHECK(hist.n == VOICE_SAID_MAX && hist.l[VOICE_SAID_MAX - 1] == voice_line(4, (VOICE_SAID_MAX + 4) % 100) &&
                      voice_said_age(&hist, voice_line(4, 4)) == 56 &&   /* (last at 204 of 0..260) */
                      voice_said_age(&hist, &other) == -1);
            }
            voice_curation_use(NULL);
        }
    }
    {   /* an enemy model's sound in place of a line (voices.c voice_creature, voice_creature_pick) */
        unsigned char pr[64];
        int room = 0, n, k, m, at[3] = { 0 }, len[3] = { 400, 1200, 700 }, seen = 0;
        for (m = 0; m < ACTOR_MODELS && strcmp(actor_models[m].name, "Zombie: Prisoner"); m++);   /* (RM_5520, model 0: moans 48..51) */
        CHECK(m < ACTOR_MODELS && voice_creature(m, 0, &room, pr, 64) == 4 && room == 5520 && pr[0] == 48 && pr[3] == 51);
        CHECK(voice_creature(m, 1, &room, pr, 64) == 44 && room == 5520 && pr[0] == 20 && pr[43] == 63);
        CHECK(voice_creature(m, 2, &room, pr, 64) == 44 && room == 30 && !voice_creature(m, 3, &room, pr, 64));
        for (m = 0; m < ACTOR_MODELS && strcmp(actor_models[m].name, "Zombie: Soldier"); m++);   /* (model 2: 20..23) */
        CHECK(m < ACTOR_MODELS && voice_creature(m, 0, &room, pr, 64) == 4 && room == 1070 && pr[0] == 20);
        for (m = 0; m < ACTOR_MODELS && actor_models[m].id != 52; m++);   /* a scripted zombie: any moans of its room */
        CHECK(m < ACTOR_MODELS && voice_creature(m, 0, &room, pr, 64) == 44 && room == actor_models[m].room);
        for (m = 0; m < ACTOR_MODELS && strcmp(actor_models[m].name, "Zombie: Bandaged 2"); m++);   /* (its room's bank is the spiders') */
        CHECK(m < ACTOR_MODELS && voice_creature(m, 0, &room, pr, 64) == 44 && room == 30);
        for (m = 0; m < ACTOR_MODELS && actor_models[m].id != 4; m++);
        CHECK(m < ACTOR_MODELS && voice_creature(m, 0, &room, pr, 64) == 8 && room == 3020 && pr[0] == 2 && !voice_creature(m, 1, &room, pr, 64));
        for (m = 0, k = 0; m < ACTOR_MODELS; m++)   /* every creature and zombie has sounds, the fish, the hazmat man and the story cast none */
            if (voice_creature(m, 0, &room, pr, 64) > 0) k++;
            else CHECK(actor_models[m].id == 8 || actor_models[m].id == 99 || cast_character(actor_models[m].id, actor_models[m].mdlver) >= 0);
        CHECK(k == 46 && !voice_creature(ACTOR_PART + 1, 0, &room, pr, 64) && !voice_creature(ACTOR_MODELS, 0, &room, pr, 64) && !voice_creature(m - 1, 0, &room, pr, 8));
        for (s = 0; s < 256; s++) {   /* one sound, deterministic; of those ending in time, the longer the likelier for a long line */
            n = voice_creature_pick((unsigned)s, 1020, 1, 40 + s % 3, len, 3, 5000, 6000, -1);
            CHECK(n >= 0 && n < 3 && n == voice_creature_pick((unsigned)s, 1020, 1, 40 + s % 3, len, 3, 5000, 6000, -1));
            seen |= 1 << n; at[n]++;
            CHECK(voice_creature_pick((unsigned)s, 1020, 1, 40, len, 3, 200, 1100, -1) != 1);   /* (the 1.2 s one ends too late) */
            CHECK(voice_creature_pick((unsigned)s, 1020, 1, 40, len, 3, 100, 300, -1) == 0);    /* (none in time: the shortest) */
            n = voice_creature_pick((unsigned)s, 1020, 1, 40, len, 3, 5000, 6000, -1);   /* not the one before again, unless it alone fits */
            CHECK(voice_creature_pick((unsigned)s, 1020, 1, 40, len, 3, 5000, 6000, n) != n && voice_creature_pick((unsigned)s, 1020, 1, 40, len, 3, 200, 450, 0) == 0);
        }
        CHECK(seen == 7 && at[1] > at[0] && voice_creature_pick(1, 1020, 1, 40, len, 0, 5000, 6000, -1) == -1 &&
              voice_creature_pick(1, 1020, 1, 40, len, 1, 0, 0, -1) == 0);
        for (s = 0, seen = 0; s < 256; s++) {   /* a few: one for a short line (as the pick), 2 or 3 in half a long one (the 1.2 s one
                                                    not first after it), never twice in a row */
            int q[4], q2[4], c = voice_creature_plan((unsigned)s, 1020, 1, 40, len, 3, 1500, 1000, -1, q, 4), t, j;
            CHECK(c == 1 && q[0] == voice_creature_pick((unsigned)s, 1020, 1, 40, len, 3, 1500, 2500, -1));
            for (int ms = 3000; ms <= 12000; ms += 3000) {
                c = voice_creature_plan((unsigned)s, 1020, 1, 40 + s % 5, len, 3, ms, 1000, 1, q, 4);
                CHECK(c >= 1 && c <= 3 && c == voice_creature_plan((unsigned)s, 1020, 1, 40 + s % 5, len, 3, ms, 1000, 1, q2, 4) && !memcmp(q, q2, sizeof(int) * (size_t)c));
                for (j = 0, t = 0; j < c; j++) { t += len[q[j]] + (j ? VOICE_CR_GAP_MS : 0); CHECK(!j || q[j] != q[j - 1]); }
                CHECK(t <= ms / 2 && q[0] != 1);   /* (half the line at most; not the line before's sound first) */
                seen |= 1 << c;
            }
        }
        CHECK(seen == 0xC && voice_creature_plan(1, 1020, 1, 40, len, 0, 5000, 1000, -1, at, 3) == 0);
    }
    printf("cast: ok\n");
    return 0;
}
