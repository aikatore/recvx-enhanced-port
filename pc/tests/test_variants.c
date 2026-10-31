/* Enemy variants (pc/src/host/variants.c): the built-in table and the mods/enemies.ini reader. */
#include "../src/host/actors.h"
#include "../src/host/variants.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

void host_log(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
static int msgs;
static void log_line(int line, const char* m) { printf("  line %d: %s\n", line, m); msgs++; }

int main(void)
{
    const EnemyVariant* v;
    int i, n0 = variant_count();
    /* built in: Mutated Steve, Monster Steve's own AI and model, the Tyrant's death */
    CHECK(n0 >= 1 && (v = variant_by_id(1)) && !strcmp(v->name, "Mutated Steve") && !strcmp(v->ui, "[C] Mutated Steve"));
    CHECK(v->type == 17 && v->mdlver == 0 && v->model == -1 && v->donor_type == 19 && v->donor_mdlver == 0);
    CHECK(v->death_n == 2 && v->death[0] == 18 && v->death[1] == 2 && v->dmotion_n == 0);
    CHECK(v->hp > 1 && v->damage * 50 < 100 && v->wild && !v->mild && v->heavy && !v->user && v->attach_n == 2);   /* (a blow: 50 x damage) */
    CHECK(!strcmp(actor_models[19].name, "Monster Steve") && actor_models[19].id == 17);   /* (its own look, posed from the donor) */
    for (i = 0; i < n0; i++) CHECK(variant_at(i)->id >= 1 && variant_at(i)->id < VARIANT_USER_MIN && variant_by_id(variant_at(i)->id) == variant_at(i));

    /* the mods file */
    CHECK(variants_parse(
        "; custom enemies\n"
        "[Fast zombie]\n"
        "id = 100\n"
        "base = 1.2        ; zombie, model 2\n"
        "speed = 1.5\n"
        "hp = 0.5\n"
        "wild = 3\n"
        "mild = 2\n"
        "stages = 0-2, 9\n"
        "\n"
        "[Giant Tyrant]\n"
        "id = 101\n"
        "base = Tyrant\n"
        "model = monster steve\n"
        "attach = 3:9, 4:13\n"
        "scale = 1.3\n"
        "Armour = 0.5\n"
        "damage = 2\n"
        "motions = 2:19, 6:19.0:16, 4:4, 7 : 19 : 3\n"
        "death = 19.0: 18, 2\n"
        "sounds = 9280\n"
        "sound_map = 0:0, 7:2\n"
        "heavy = 1\n"
        "slots = 4\n", log_line) == 2);
    CHECK(msgs == 0 && variant_count() == n0 + 2);
    v = variant_by_id(100);
    CHECK(v && !strcmp(v->ui, "[C] Fast zombie") && v->user && v->type == 1 && v->mdlver == 2 && v->model == -1);
    CHECK(v->speed == 1.5f && v->hp == 0.5f && v->armour == 1.0f && v->scale == 1.0f && v->wild == 3 && v->mild == 2);
    CHECK(v->stages == (1 | 2 | 4 | 512));
    v = variant_by_id(101);
    CHECK(v && v->type == 19 && v->mdlver == 0 && v->model == 19 && v->attach_n == 2 && v->attach[1][0] == 4 && v->attach[1][1] == 13);
    CHECK(v->scale == 1.3f && v->armour == 0.5f && v->damage == 2.0f && v->heavy == 1 && v->slots == 4 && v->stages == VARIANT_STAGES_ALL);
    CHECK(v->motion_n == 2 && v->motion[0][0] == 2 && v->motion[0][1] == 19 && v->motion[1][0] == 4 && v->sound_room == 9280 && v->sound_n == 2 && v->sound[1][1] == 2);
    CHECK(v->donor_type == 19 && v->donor_mdlver == 0 && v->dmotion_n == 2 && v->dmotion[0][0] == 6 && v->dmotion[0][1] == 16 && v->dmotion[1][1] == 3);
    CHECK(v->death_n == 2 && v->death[0] == 18 && v->death[1] == 2);
    CHECK(variant_at(n0)->id == 100 && variant_at(n0 + 1)->id == 101);   /* the file's order, after the built-in ones */

    /* left out: no id, id taken or out of range, no base, unknown base; bad keys reported, the rest kept */
    msgs = 0;
    CHECK(variants_parse(
        "[No id]\nbase = 1\n"
        "[Taken]\nid = 100\nbase = 1\n"
        "[Low id]\nid = 5\nbase = 1\n"
        "[No base]\nid = 102\n"
        "[Bad base]\nid = 103\nbase = Nobody\n"
        "[Bad keys]\nid = 104\nbase = 4\nspeed = 9\ncolour = red\nmotions = 1:x\ndeath = 19:2\nmotions = 3:5:4\n"
        "loose = line\n", log_line) == 1);
    CHECK(msgs == 12 && variant_count() == n0 + 3);   /* (motions = 3:5:4: a second donor) */
    v = variant_by_id(104);
    CHECK(v && v->type == 4 && v->speed == 1.0f && v->motion_n == 0 && v->donor_type == 19 && v->death_n == 1);
    CHECK(!variant_by_id(102) && !variant_by_id(103) && !variant_by_id(5));
    CHECK(!strcmp(variant_by_id(100)->name, "Fast zombie"));   /* not replaced by [Taken] */

    variants_reset_user();
    CHECK(variant_count() == n0 && !variant_by_id(100));
    printf("variants: ok (%d built in)\n", n0);
    return 0;
}
