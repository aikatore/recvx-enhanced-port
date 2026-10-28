/* "You appear as" list (pc/src/host/actors.c): "As played (cutscene)" second, event doubles left out, the cutscene models last,
   every chr still valid. */
#include "../src/host/actors.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void)
{
    int n = appear_count();
    CHECK(n == COOP_CHR_MAX + 1 - 8 + 1);   /* (+ "As played (cutscene)") */
    for (int i = 0; i < n; i++) {
        CHECK(appear_pos(appear_at(i)) == i && appear_canon(appear_at(i)) == appear_at(i));
        if (i > 2 && i != n - 4) CHECK(appear_at(i) > appear_at(i - 1));   /* catalogue order: player files first, then the tail */
    }
    for (int c = 0; c <= COOP_CHR_MAX; c++) CHECK(appear_canon(appear_canon(c)) == appear_canon(c));
    CHECK(appear_at(0) == 0 && appear_at(1) == COOP_CHR_CINE && appear_at(2) == 1 && appear_at(6) == 5);   /* As played, As played (cutscene), the player files */
    CHECK(appear_pos(COOP_CHR_CINE) == 1 && appear_pos(1) == 2 && appear_step(0, 1) == COOP_CHR_CINE && appear_step(COOP_CHR_CINE, 1) == 1);
    CHECK(!strcmp(appear_name(COOP_CHR_CINE), "Current Character (Cutscene)") && !strcmp(appear_group(COOP_CHR_CINE), "Code: Veronica Characters") && appear_voice(COOP_CHR_CINE) < 0);
    /* "As played (cutscene)" resolved for the character played (Claire, Chris, Steve, Wesker); every stored value keeps its model */
    CHECK(appear_played(COOP_CHR_CINE, 0) == COOP_CHR_ACTOR + 0 && appear_played(COOP_CHR_CINE, 1) == COOP_CHR_ACTOR + 1);
    CHECK(appear_played(COOP_CHR_CINE, 2) == COOP_CHR_ACTOR + 3 && appear_played(COOP_CHR_CINE, 3) == COOP_CHR_ACTOR + 5 && appear_played(COOP_CHR_CINE, 4) == 0);
    for (int c = 0; c <= COOP_CHR_MAX; c++) CHECK(appear_played(c, 1) == c);
    CHECK(appear_canon(COOP_CHR_CINE - 1) == 0 && appear_canon(COOP_CHR_CINE + 1) == 0);
    /* the player characters' cutscene models last: Claire, Chris, Steve, Wesker */
    CHECK(appear_at(n - 4) == COOP_CHR_ACTOR + 0 && appear_at(n - 3) == COOP_CHR_ACTOR + 1);
    CHECK(appear_at(n - 2) == COOP_CHR_ACTOR + 3 && appear_at(n - 1) == COOP_CHR_ACTOR + 5);
    CHECK(appear_at(n - 5) == COOP_CHR_MAX && appear_at(n) == 0 && appear_at(-1) == 0);   /* before them the zombies */
    CHECK(appear_canon(COOP_CHR_ACTOR + 0) == COOP_CHR_ACTOR + 0);       /* Claire's cutscene model: listed */
    CHECK(appear_canon(COOP_CHR_ACTOR + 2) == 2);                        /* Chris's event model: Chris */
    CHECK(appear_canon(COOP_CHR_ACTOR + 7) == 4);                        /* a Wesker event model: Wesker */
    CHECK(appear_canon(COOP_CHR_ACTOR + 18) == COOP_CHR_ACTOR + 17);     /* Tyrant 2: Tyrant */
    CHECK(appear_canon(COOP_CHR_MAX + 1) == 0 && appear_canon(-1) == 0);
    CHECK(appear_step(5, 1) == COOP_CHR_ACTOR + 8);                      /* after the player files: Alfred */
    CHECK(appear_step(COOP_CHR_MAX, 1) == COOP_CHR_ACTOR + 0);           /* after the zombies: Claire's cutscene model */
    CHECK(appear_step(0, -1) == COOP_CHR_ACTOR + 5 && appear_step(COOP_CHR_ACTOR + 5, 1) == 0);   /* wrapping at Wesker's */
    CHECK(appear_step(COOP_CHR_ACTOR + 6, 1) == 5);                      /* from a hidden one (Wesker event model): on from Wesker */
    CHECK(!strcmp(appear_name(1), "Claire"));
    /* gender-matched animations: Chris's for a man while Claire is played, Claire's for a woman while a man is */
    CHECK(actor_models[8].anim == ANIM_MALE && actor_models[12].anim == ANIM_FEMALE && actor_models[24].anim == ANIM_IDLE);   /* Alfred, Alexia, Cerberus */
    CHECK(appear_motion_chr(ANIM_MALE, 0) == 1 && appear_motion_chr(ANIM_MALE, 1) < 0 && appear_motion_chr(ANIM_MALE, 3) < 0);
    CHECK(appear_motion_chr(ANIM_FEMALE, 0) < 0 && appear_motion_chr(ANIM_FEMALE, 2) == 0);
    CHECK(appear_motion_chr(ANIM_IDLE, 0) < 0 && appear_motion_chr(ANIM_PLAYED, 0) < 0);
    CHECK(appear_part_anim(0) == ANIM_FEMALE && appear_part_anim(4) == ANIM_FEMALE && appear_part_anim(3) == ANIM_MALE);
    g_appear_anim = 0;
    CHECK(appear_motion_chr(ANIM_MALE, 0) < 0);
    g_appear_anim = 1;
    CHECK(appear_grab_motion(1, 406, 0, 1) == 431 && appear_grab_motion(1, 431, 1, 0) == 406);   /* zombie */
    CHECK(appear_grab_motion(3, 60, 0, 1) == 74 && appear_grab_motion(7, 31, 0, 1) == 34);       /* black widow, bat */
    CHECK(appear_grab_motion(1, 10, 0, 1) < 0 && appear_grab_motion(5, 406, 0, 1) < 0);          /* not a grab motion, no table */
    /* voices: own bank for the four player characters and their doubles, men Chris's, women Claire's */
    CHECK(appear_voice(0) < 0 && appear_voice(1) == 0 && appear_voice(2) == 1 && appear_voice(3) == 2 && appear_voice(4) == 3 && appear_voice(5) == 0);
    CHECK(appear_voice(COOP_CHR_ACTOR + 4) == 2 && appear_voice(COOP_CHR_ACTOR + 7) == 3 && appear_voice(COOP_CHR_ACTOR + 19) == 2);   /* Steve, Wesker, Monster Steve */
    CHECK(appear_voice(COOP_CHR_ACTOR + 0) == 0 && appear_voice(COOP_CHR_ACTOR + 1) == 1 && appear_voice(COOP_CHR_ACTOR + 3) == 2 && appear_voice(COOP_CHR_ACTOR + 5) == 3);   /* the cutscene models */
    CHECK(appear_voice(COOP_CHR_ACTOR + 8) == 1 && appear_voice(COOP_CHR_ACTOR + 13) == 1 && appear_voice(COOP_CHR_ACTOR + 17) == 1);  /* Alfred, Rodrigo, Tyrant */
    CHECK(appear_voice(COOP_CHR_ACTOR + 12) == 0 && appear_voice(COOP_CHR_ACTOR + 24) < 0 && appear_voice(COOP_CHR_MAX + 1) < 0);    /* Alexia, Cerberus */
    /* imported player models (pc/src/import): listed last, after the cutscene models */
    actor_imports[0] = (ActorModel){ .name = "Kevin", .anim = ANIM_MALE };
    actor_imports[1] = (ActorModel){ .name = "Alyssa", .anim = ANIM_FEMALE };
    actor_import_n = 2;
    CHECK(appear_count() == n + 2 && appear_at(n) == COOP_CHR_IMPORT && appear_at(n + 1) == COOP_CHR_IMPORT + 1 && appear_at(n + 2) == 0);
    CHECK(appear_pos(COOP_CHR_IMPORT + 1) == n + 1 && appear_canon(COOP_CHR_IMPORT + 1) == COOP_CHR_IMPORT + 1 && appear_canon(COOP_CHR_IMPORT + 2) == 0);
    CHECK(appear_step(COOP_CHR_ACTOR + 5, 1) == COOP_CHR_IMPORT && appear_step(COOP_CHR_IMPORT + 1, 1) == 0);
    CHECK(!strcmp(appear_name(COOP_CHR_IMPORT + 1), "Alyssa") && appear_voice(COOP_CHR_IMPORT) == 1 && appear_voice(COOP_CHR_IMPORT + 1) == 0);
    actor_import_group[1] = 2;
    CHECK(!strcmp(appear_group(1), "Code: Veronica Characters") && !strcmp(appear_group(COOP_CHR_IMPORT + 1), "Outbreak Supporting Cast"));
    CHECK(!strcmp(appear_group(COOP_CHR_ACTOR), "Code: Veronica Cutscenes") && !strcmp(appear_group(COOP_CHR_ACTOR + 13), "Code: Veronica Cast"));
    CHECK(!strcmp(appear_group(COOP_CHR_ACTOR + 61), "Code: Veronica Enemies"));
    CHECK(actor_model(ACTOR_MODELS + 1) == &actor_imports[1] && !actor_model(ACTOR_MODELS + 2) && actor_model(3) == &actor_models[3]);
    actor_import_n = 0;
    printf("appear: ok, %d of %d listed\n", n, COOP_CHR_MAX + 1);
    return 0;
}
