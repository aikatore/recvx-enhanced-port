/* The binding layer (pc/src/input/bindings.c) on its own: defaults as the old fixed layout,
   third person taking its keys over, rebinding and swapping, conflicts, recvx.ini round trip,
   the gamepad set through SDL's gamepad layer with a virtual Xbox pad, the input source filter
   and the choice of gamepad (two virtual pads, one reconnecting). */
#include "../src/host/bindings.h"
#include "../src/host/tps.h"
#include "../src/host/weapons.h"
#include <SDL3/SDL.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)
#include <stdio.h>
#include <string.h>

TpsConfig g_tps = { .key = SDL_SCANCODE_F5 };
SDL_Window* g_window;
uint64_t gs_frame_count(void) { return 0; }
void host_log(const char* fmt, ...) { (void)fmt; }
const char* tr(const char* en) { return en; }   /* (host/tr.c) */
int pc_keytype_mask(int v) { return v == 4 ? 1 << B_R1 : -1; }   /* aim on R1, as in every key type */
static int playing = 1;
int reload_combo_ok(void) { return playing; }
int pc_keytype(void) { return 0; }

static bool keys[SDL_SCANCODE_COUNT];
static int held(int row, int sc) { memset(keys, 0, sizeof(keys)); if (sc) keys[sc] = 1; return bind_held(row, BIND_KB, keys, 0); }
/* a pad report with these keys down: the reload combination first, as pad.c */
static void report(int a, int b) { memset(keys, 0, sizeof(keys)); keys[a] = keys[b] = 1; keys[0] = 0; bind_combo_frame(1, keys, 0); }
static int now(int row) { return bind_held(row, BIND_KB, keys, 0); }

static void key_ev(int sc, int down) { SDL_Event e = { 0 }; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.scancode = (SDL_Scancode)sc; bind_latch_event(&e); }

int main(void)
{
    char buf[64];
    {   /* quick presses in menus: a tap and a release-and-press between two reports are each seen, in order */
        static const bool up[SDL_SCANCODE_COUNT];
        key_ev(SDL_SCANCODE_Z, 1); key_ev(SDL_SCANCODE_Z, 0);   /* tapped between two reports, now up */
        CHECK(bind_latch_frame(1, up)[SDL_SCANCODE_Z]);
        CHECK(!bind_latch_frame(1, up)[SDL_SCANCODE_Z]);
        CHECK(!bind_latch_frame(1, up)[SDL_SCANCODE_Z]);
        memset(keys, 0, sizeof(keys)); keys[SDL_SCANCODE_X] = 1;   /* held, let go and pressed again */
        key_ev(SDL_SCANCODE_X, 0); key_ev(SDL_SCANCODE_X, 1); key_ev(SDL_SCANCODE_Z, 1); key_ev(SDL_SCANCODE_Z, 0);
        CHECK(!bind_latch_frame(1, keys)[SDL_SCANCODE_X] && bind_latch_frame(1, keys)[SDL_SCANCODE_X]);
        key_ev(SDL_SCANCODE_Z, 1); key_ev(SDL_SCANCODE_Z, 0);   /* outside menus: dropped, the state as it is */
        CHECK(!bind_latch_frame(0, up)[SDL_SCANCODE_Z] && !bind_latch_frame(1, up)[SDL_SCANCODE_Z]);
        bind_latch_end();
    }
    /* defaults: the layout pad.c had built in */
    CHECK(held(B_CROSS, SDL_SCANCODE_Z) && held(B_CIRCLE, SDL_SCANCODE_X) && held(B_SQUARE, SDL_SCANCODE_A));
    CHECK(held(B_TRIANGLE, SDL_SCANCODE_S) && held(B_L1, SDL_SCANCODE_Q) && held(B_R1, SDL_SCANCODE_W));
    CHECK(held(B_START, SDL_SCANCODE_RETURN) && held(B_SELECT, SDL_SCANCODE_BACKSPACE) && held(B_UP, SDL_SCANCODE_UP));
    CHECK(held(B_L2, SDL_SCANCODE_9) && held(B_R2, SDL_SCANCODE_0) && !held(B_CROSS, SDL_SCANCODE_X));
    /* weapon shortcuts: 1-8 (L2 and R2's 1 and 2 before), held with C or the right stick click (R3's before) */
    for (int n = 0; n < 8; n++) CHECK(held(B_WPN1 + n, SDL_SCANCODE_1 + n) && !bind_get(B_WPN1 + n, BIND_PAD, 0));
    CHECK(held(B_WPN_HOLD, SDL_SCANCODE_C) && bind_get(B_WPN_HOLD, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_RIGHT_STICK && !bind_get(B_R3, BIND_PAD, 0));
    /* the wheel's other page: E and RB, which free aim / R1 (and third-person action / reload) keep outside the wheel */
    CHECK(held(B_WPN_PAGE, SDL_SCANCODE_E) && held(B_FREEAIM, SDL_SCANCODE_E) && bind_get(B_WPN_PAGE, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    CHECK(bind_get(B_R1, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && bind_get(B_L1, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    CHECK(held(B_TPS_RUN, SDL_SCANCODE_RSHIFT) && held(B_NOCLIP_UP, SDL_SCANCODE_PAGEUP));
    CHECK(bind_get(B_TPS_FIRE, BIND_KB, 0) == BIND_MOUSE + SDL_BUTTON_LEFT);
    for (int r = 0; r < B_COUNT; r++) CHECK(!bind_conflict(r, BIND_KB) && !bind_conflict(r, BIND_PAD));
    {   /* a mouse button captured for a game row (F1, ENHANCED SETTINGS) plays it (fixed cameras: pad.c passes the buttons) */
        SDL_Event e = { 0 };
        bind_capture_start(BIND_KB);
        SDL_Delay(260);   /* not the click that started the capture */
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = SDL_BUTTON_RIGHT;
        CHECK(bind_event(&e) && bind_capture_poll() == BIND_MOUSE + SDL_BUTTON_RIGHT);
        bind_set(B_R1, BIND_KB, 1, BIND_MOUSE + SDL_BUTTON_RIGHT);
        CHECK(!bind_conflict(B_R1, BIND_KB) && bind_get(B_TPS_AIM, BIND_KB, 0) == BIND_MOUSE + SDL_BUTTON_RIGHT);   /* shared with third person */
        memset(keys, 0, sizeof(keys));
        CHECK(bind_held(B_R1, BIND_KB, keys, SDL_BUTTON_RMASK) && !bind_held(B_R1, BIND_KB, keys, SDL_BUTTON_LMASK));
        bind_set(B_R1, BIND_KB, 1, 0);
        bind_set(B_FREEAIM, BIND_KB, 1, BIND_MOUSE + SDL_BUTTON_RIGHT);   /* free aim too */
        CHECK(bind_held(B_FREEAIM, BIND_KB, keys, SDL_BUTTON_RMASK) && !bind_held(B_FREEAIM, BIND_KB, keys, 0));
        bind_set(B_FREEAIM, BIND_KB, 1, 0);
    }
    /* free aim: E and the left trigger (L2's before); a file from before it (bindings_version < 3) drops L2's old left
       trigger, and a key it gave another game row keeps it (free aim's default gives way) */
    CHECK(held(B_FREEAIM, SDL_SCANCODE_E) && bind_get(B_FREEAIM, BIND_PAD, 0) == BIND_AXIS + SDL_GAMEPAD_AXIS_LEFT_TRIGGER * 2 && !bind_get(B_L2, BIND_PAD, 0));
    CHECK(bind_ini_line("pad_l2", "+lefttrigger") && !bind_get(B_L2, BIND_PAD, 0) && !bind_conflict(B_FREEAIM, BIND_PAD));
    CHECK(bind_ini_line("key_l3", "E") && held(B_L3, SDL_SCANCODE_E) && !bind_get(B_FREEAIM, BIND_KB, 0) && !bind_conflict(B_L3, BIND_KB));
    CHECK(bind_ini_line("key_l3", "none"));
    bind_set(B_FREEAIM, BIND_KB, 0, SDL_SCANCODE_E);
    /* a file from before the weapon shortcuts (bindings_version < 4): L2 and R2's 1 and 2, R3's right stick click are
       dropped (now the shortcuts'); a key it gave another row keeps it (the shortcut's default gives way) */
    CHECK(bind_ini_line("key_l2", "1") && bind_ini_line("key_r2", "2") && bind_ini_line("pad_r3", "rightstick"));
    CHECK(held(B_L2, SDL_SCANCODE_9) && held(B_WPN1, SDL_SCANCODE_1) && held(B_WPN2, SDL_SCANCODE_2) && !bind_get(B_R3, BIND_PAD, 0));
    CHECK(bind_ini_line("key_l3", "3") && held(B_L3, SDL_SCANCODE_3) && !bind_get(B_WPN3, BIND_KB, 0) && !bind_conflict(B_L3, BIND_KB));
    CHECK(bind_ini_line("key_l3", "none"));
    bind_set(B_WPN3, BIND_KB, 0, SDL_SCANCODE_3);
    /* ... and before the page row (< 5): it gets E / RB, unless such a file gave a hotkey E */
    CHECK(bind_ini_line("key_settings", "E") && !bind_get(B_WPN_PAGE, BIND_KB, 0) && bind_get(B_WPN_PAGE, BIND_PAD, 0));
    CHECK(bind_ini_line("key_settings", "F1") && bind_ini_line("key_l3", "none"));
    bind_set(B_WPN_PAGE, BIND_KB, 0, SDL_SCANCODE_E);
    bind_set(B_FREEAIM, BIND_KB, 0, SDL_SCANCODE_E);
    CHECK(held(B_FREEAIM, SDL_SCANCODE_E) && held(B_WPN_PAGE, SDL_SCANCODE_E) && held(B_SETTINGS, SDL_SCANCODE_F1));
    for (int r = 0; r < B_COUNT; r++) CHECK(!bind_conflict(r, BIND_KB) && !bind_conflict(r, BIND_PAD));
    /* the slots: one per weapon (assigning moves it), by item id; the bag lookup skips word 0 (the equipped slot) */
    {
        unsigned int bag[16] = { 3, 0, 0x80001, 0x90000 | 15, 0x21000 | 20 };   /* knife, handgun, grenade launcher (acid rounds) */
        wpn_assign(0, 0, 9); wpn_assign(0, 4, 2);
        CHECK(wpn_slot_of(0, 9) == 0 && wpn_slot_of(1, 9) == -1 && wpn_slot_of(0, 0) == -1);
        wpn_assign(0, 2, 9);
        CHECK(g_wpn_slots[0][0] == 0 && wpn_slot_of(0, 9) == 2);
        CHECK(wpn_bag_find(bag, 9, 9) == 3 && wpn_bag_find(bag, 9, 2) == 4 && !wpn_bag_find(bag, 9, 6) && !wpn_bag_find(bag, 9, 0) && !wpn_bag_find(bag, 9, 3));
        FILE* f = tmpfile();
        char line[128], k[64], v[64];
        wpn_ini_write(f);
        wpn_assign(0, 2, 0); wpn_assign(0, 4, 0);
        rewind(f);
        while (fgets(line, sizeof(line), f)) if (sscanf(line, " %63[a-z_0-9] = %63[^\r\n]", k, v) == 2) CHECK(wpn_ini_line(k, v));
        fclose(f);
        CHECK(g_wpn_slots[0][2] == 9 && g_wpn_slots[0][4] == 2 && !g_wpn_slots[1][2]);
        CHECK(wpn_ini_line("weapon_slots_chris", "8 8 300 -1 5") && g_wpn_slots[1][1] == 8 && !g_wpn_slots[1][0] && !g_wpn_slots[1][2] && g_wpn_slots[1][4] == 5);
        CHECK(!wpn_ini_line("weapon_slots_ada", "1"));
    }
    /* third person on the gamepad (modern scheme): its own rows, which share buttons with the game rows on purpose */
    CHECK(bind_get(B_TPS_FIRE, BIND_PAD, 0) == BIND_AXIS + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER * 2 && bind_get(B_TPS_AIM, BIND_PAD, 0) == BIND_AXIS + SDL_GAMEPAD_AXIS_LEFT_TRIGGER * 2);
    CHECK(bind_get(B_TPS_ACTION, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_SOUTH && bind_get(B_CROSS, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK(!bind_row_shown(B_TPS_FWD, BIND_PAD) && bind_row_shown(B_TPS_FIRE, BIND_PAD) && bind_row_shown(B_TPS_KNIFE, BIND_PAD));
    CHECK(bind_row_shown(B_TPS_FIRE, BIND_KB) && !bind_row_shown(B_TPS_KNIFE, BIND_KB) && !bind_row_shown(B_TPS_RECENTER, BIND_KB));
    CHECK(!bind_ini_line("key_tps_knife", "K") && bind_get(B_TPS_FIRE, BIND_KB, 0) == BIND_MOUSE + SDL_BUTTON_LEFT);
    /* quick knife: Space (no longer third-person action's second key) and the right trigger (no longer R2's) */
    CHECK(held(B_QUICKKNIFE, SDL_SCANCODE_SPACE) && !held(B_TPS_ACTION, SDL_SCANCODE_SPACE) && held(B_TPS_ACTION, SDL_SCANCODE_E));
    CHECK(bind_get(B_QUICKKNIFE, BIND_PAD, 0) == BIND_AXIS + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER * 2 && !bind_get(B_R2, BIND_PAD, 0));
    /* a recvx.ini from before it: its old defaults are dropped, the rest is read */
    CHECK(bind_ini_line("key_tps_action_2", "Space") && !bind_get(B_TPS_ACTION, BIND_KB, 1));
    CHECK(bind_ini_line("pad_r2", "+righttrigger") && !bind_get(B_R2, BIND_PAD, 0));
    CHECK(bind_ini_line("key_tps_action_2", "F") && bind_get(B_TPS_ACTION, BIND_KB, 1) == SDL_SCANCODE_F);
    CHECK(bind_ini_line("key_tps_action_2", "none"));
    /* reload: R and the right shoulder in third person (once noclip fly up's and the quick turn's); the game row R and
       Aim + Triangle, a combination as it shares Triangle; R is no combination (no other game row has it) */
    CHECK(held(B_TPS_RELOAD, SDL_SCANCODE_R) && !held(B_NOCLIP_UP, SDL_SCANCODE_R) && bind_get(B_RELOAD, BIND_KB, 0) == SDL_SCANCODE_R);
    CHECK(bind_get(B_RELOAD, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_NORTH && bind_get(B_TRIANGLE, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(bind_combo(B_RELOAD, BIND_PAD, 0) && !bind_combo(B_RELOAD, BIND_KB, 0) && !bind_combo(B_TPS_RELOAD, BIND_KB, 0) && !bind_combo(B_TPS_RELOAD, BIND_PAD, 0));
    report(SDL_SCANCODE_R, 0);
    CHECK(now(B_RELOAD));   /* on its own: without aim too (the lowered weapon's reload) */
    /* rebinding the reload onto a game row's key shares it (no swap, no conflict): Aim + Square */
    bind_set(B_RELOAD, BIND_KB, 0, SDL_SCANCODE_A);
    CHECK(!bind_swapped() && bind_get(B_SQUARE, BIND_KB, 0) == SDL_SCANCODE_A && bind_combo(B_RELOAD, BIND_KB, 0));
    CHECK(!bind_conflict(B_RELOAD, BIND_KB) && !bind_conflict(B_SQUARE, BIND_KB));
    report(SDL_SCANCODE_A, 0);                       /* without aim: Square's, until let go */
    CHECK(now(B_SQUARE) && !now(B_RELOAD));
    report(SDL_SCANCODE_A, SDL_SCANCODE_W);
    CHECK(now(B_SQUARE) && !now(B_RELOAD) && now(B_R1));
    report(SDL_SCANCODE_W, 0);
    report(SDL_SCANCODE_A, SDL_SCANCODE_W);          /* pressed with aim held: the reload's, Square never sees it */
    CHECK(now(B_RELOAD) && !now(B_SQUARE) && now(B_R1));
    report(SDL_SCANCODE_A, 0);                       /* aim let go first: still the reload's, no late Square */
    CHECK(now(B_RELOAD) && !now(B_SQUARE));
    report(0, 0);
    report(SDL_SCANCODE_A, SDL_SCANCODE_W);          /* both at once: the reload's */
    CHECK(now(B_RELOAD) && !now(B_SQUARE));
    report(0, 0);
    report(SDL_SCANCODE_A, SDL_SCANCODE_E);          /* free aim is aim too */
    CHECK(now(B_RELOAD) && !now(B_SQUARE));
    report(0, 0);
    playing = 0;                                     /* not in play (a menu, an event): Square's */
    report(SDL_SCANCODE_A, SDL_SCANCODE_W);
    CHECK(!now(B_RELOAD) && now(B_SQUARE));
    playing = 1;
    report(0, 0);
    bind_set(B_RELOAD, BIND_KB, 0, SDL_SCANCODE_W);  /* on the aim button itself: no combination */
    CHECK(!bind_combo(B_RELOAD, BIND_KB, 0) && bind_get(B_R1, BIND_KB, 0) == SDL_SCANCODE_W);
    bind_set(B_TPS_RELOAD, BIND_KB, 1, SDL_SCANCODE_E);   /* third person: shares with action */
    CHECK(!bind_swapped() && bind_combo(B_TPS_RELOAD, BIND_KB, 1) && bind_get(B_TPS_ACTION, BIND_KB, 0) == SDL_SCANCODE_E);
    bind_set(B_TPS_RELOAD, BIND_KB, 1, 0);
    bind_set(B_RELOAD, BIND_KB, 0, SDL_SCANCODE_F1);      /* a hotkey's: swapped as before */
    CHECK(bind_swapped() && bind_get(B_SETTINGS, BIND_KB, 0) == SDL_SCANCODE_W);
    bind_set(B_SETTINGS, BIND_KB, 0, SDL_SCANCODE_F1);
    bind_set(B_RELOAD, BIND_KB, 0, SDL_SCANCODE_R);
    CHECK(bind_get(B_R1, BIND_KB, 0) == SDL_SCANCODE_W && bind_get(B_RELOAD, BIND_KB, 0) == SDL_SCANCODE_R);
    /* a recvx.ini from before the combination (no bindings_version line first): the reload rows' "none" (their default
       then) keeps today's defaults; a binding stays, and after the version line "none" unbinds */
    CHECK(bind_ini_line("key_reload", "none") && bind_ini_line("pad_reload", "none") && bind_get(B_RELOAD, BIND_KB, 0) == SDL_SCANCODE_R);
    CHECK(bind_get(B_RELOAD, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(bind_ini_line("key_reload", "G") && bind_get(B_RELOAD, BIND_KB, 0) == SDL_SCANCODE_G);
    CHECK(bind_ini_line("bindings_version", "2") && bind_ini_line("key_reload", "none") && !bind_get(B_RELOAD, BIND_KB, 0));
    CHECK(bind_ini_line("key_reload", "R"));
    CHECK(bind_get(B_TPS_RELOAD, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && bind_get(B_TPS_QUICKTURN, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    CHECK(bind_row_shown(B_TPS_RELOAD, BIND_KB) && bind_row_shown(B_TPS_RELOAD, BIND_PAD) && bind_row_shown(B_RELOAD, BIND_KB) && bind_row_shown(B_RELOAD, BIND_PAD));
    /* a recvx.ini from before it (no key_tps_reload line before these): those old defaults are dropped */
    CHECK(bind_ini_line("key_noclip_up", "R") && !bind_get(B_NOCLIP_UP, BIND_KB, 0) && !bind_conflict(B_TPS_RELOAD, BIND_KB));
    CHECK(bind_ini_line("pad_tps_quickturn", "rightshoulder") && bind_get(B_TPS_QUICKTURN, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    CHECK(bind_ini_line("key_noclip_up", "PageUp") && held(B_NOCLIP_UP, SDL_SCANCODE_PAGEUP));
    /* third person takes W/A/S/Q from the DS2 rows in a game, nothing else (tps_glue.c kb_play) */
    g_tps.on = g_tps.kb_play = 1;
    CHECK(!held(B_SQUARE, SDL_SCANCODE_A) && !held(B_R1, SDL_SCANCODE_W) && held(B_CROSS, SDL_SCANCODE_Z) && held(B_TPS_FWD, SDL_SCANCODE_W));
    /* the title menus and OPTION (ENHANCED SETTINGS' L1/R1 pages) read the DS2 buttons: Q/W are L1/R1 there */
    g_tps.kb_play = 0;
    CHECK(held(B_L1, SDL_SCANCODE_Q) && held(B_R1, SDL_SCANCODE_W) && held(B_SQUARE, SDL_SCANCODE_A) && held(B_TPS_FWD, SDL_SCANCODE_W));
    g_tps.on = 0;
    /* rebinding: the new key drives the row, the old one no longer does */
    bind_set(B_CROSS, BIND_KB, 0, SDL_SCANCODE_V);
    CHECK(held(B_CROSS, SDL_SCANCODE_V) && !held(B_CROSS, SDL_SCANCODE_Z) && !bind_swapped());
    /* a key of another game row swaps; a third-person key does not (they share on purpose) */
    bind_set(B_CROSS, BIND_KB, 0, SDL_SCANCODE_X);
    CHECK(held(B_CROSS, SDL_SCANCODE_X) && held(B_CIRCLE, SDL_SCANCODE_V) && bind_swapped() && !strcmp(bind_swapped(), "Circle"));
    bind_set(B_TPS_ACTION, BIND_KB, 1, SDL_SCANCODE_X);
    CHECK(!bind_swapped() && held(B_CROSS, SDL_SCANCODE_X));
    /* hotkeys share with nobody; the toggle key's first slot is g_tps.key */
    bind_set(B_TPS_TOGGLE, BIND_KB, 0, SDL_SCANCODE_F6);
    CHECK(g_tps.key == SDL_SCANCODE_F6);
    /* rebinding the reload: within the third-person set a key is shared (Aim + E), the game row's is its own */
    bind_set(B_TPS_RELOAD, BIND_KB, 0, SDL_SCANCODE_E);
    CHECK(!held(B_TPS_RELOAD, SDL_SCANCODE_E) && held(B_TPS_ACTION, SDL_SCANCODE_E) && !bind_swapped() && bind_combo(B_TPS_RELOAD, BIND_KB, 0));
    bind_set(B_TPS_RELOAD, BIND_KB, 0, SDL_SCANCODE_R);
    CHECK(held(B_TPS_ACTION, SDL_SCANCODE_E) && held(B_TPS_RELOAD, SDL_SCANCODE_R));
    bind_set(B_TPS_RELOAD, BIND_KB, 1, SDL_SCANCODE_G);
    bind_set(B_RELOAD, BIND_PAD, 0, 1 + SDL_GAMEPAD_BUTTON_DPAD_LEFT);   /* shared with Left */
    CHECK(!bind_swapped() && bind_get(B_LEFT, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_LEFT && bind_combo(B_RELOAD, BIND_PAD, 0));
    /* conflicts from a hand-edited ini */
    CHECK(bind_ini_line("key_l3", "Z") && bind_conflict(B_L3, BIND_KB) == 0);   /* Z is free now (Cross moved to X) */
    CHECK(bind_ini_line("key_l3", "Return") && bind_conflict(B_L3, BIND_KB) == B_START + 1);
    CHECK(bind_ini_line("key_l3", "none") && !bind_conflict(B_L3, BIND_KB));
    /* rebinding a third-person gamepad action leaves the game row of the same button alone, and the other way round */
    bind_set(B_TPS_RUN, BIND_PAD, 0, 1 + SDL_GAMEPAD_BUTTON_DPAD_UP);
    CHECK(!bind_swapped() && bind_get(B_UP, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_UP && !bind_conflict(B_TPS_RUN, BIND_PAD));
    bind_set(B_TPS_FIRE, BIND_PAD, 0, 1 + SDL_GAMEPAD_BUTTON_SOUTH);   /* within the set: swaps with action */
    CHECK(bind_swapped() && !strcmp(bind_swapped(), "Action / Confirm") && bind_get(B_TPS_ACTION, BIND_PAD, 0) == BIND_AXIS + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER * 2);
    CHECK(bind_get(B_CROSS, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_SOUTH && bind_get(B_QUICKKNIFE, BIND_PAD, 0) == BIND_AXIS + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER * 2);
    bind_set(B_TPS_FIRE, BIND_PAD, 0, BIND_AXIS + SDL_GAMEPAD_AXIS_RIGHT_TRIGGER * 2);   /* back */
    CHECK(bind_get(B_TPS_ACTION, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_SOUTH);
    bind_set(B_SQUARE, BIND_PAD, 0, 1 + SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);   /* a game row onto the knife's button: the knife keeps it */
    CHECK(bind_get(B_TPS_KNIFE, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && bind_get(B_L1, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_WEST);
    bind_set(B_SQUARE, BIND_PAD, 0, 1 + SDL_GAMEPAD_BUTTON_WEST);
    CHECK(bind_get(B_L1, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && bind_get(B_TPS_RUN, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_UP);
    CHECK(bind_ini_line("pad_tps_knife", "rightshoulder") && bind_get(B_TPS_KNIFE, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    CHECK(bind_ini_line("pad_tps_knife", "leftshoulder") && !bind_ini_line("pad_tps_forward", "dpup"));
    /* recvx.ini round trip */
    {
        FILE* f = tmpfile();
        char line[256], k[64], v[128];
        int n = 0;
        bind_ini_write(f);
        bind_reset(BIND_KB); bind_reset(BIND_PAD);
        CHECK(held(B_CROSS, SDL_SCANCODE_Z) && g_tps.key == SDL_SCANCODE_F5);
        rewind(f);
        while (fgets(line, sizeof(line), f))
            if (sscanf(line, " %63[a-z_0-9] = %127[^\r\n]", k, v) == 2) n += bind_ini_line(k, v);
        fclose(f);
        CHECK(n > 50 && held(B_CROSS, SDL_SCANCODE_X) && held(B_CIRCLE, SDL_SCANCODE_V) && g_tps.key == SDL_SCANCODE_F6);
        CHECK(held(B_WPN3, SDL_SCANCODE_3) && held(B_L2, SDL_SCANCODE_9) && held(B_WPN_HOLD, SDL_SCANCODE_C));   /* version 4: 1 and 2 stay */
        CHECK(bind_get(B_TPS_ACTION, BIND_KB, 1) == SDL_SCANCODE_X && bind_get(B_TPS_FIRE, BIND_KB, 0) == BIND_MOUSE + SDL_BUTTON_LEFT);
        CHECK(bind_get(B_TPS_RUN, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_UP && bind_get(B_UP, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_UP);
        CHECK(bind_get(B_TPS_RELOAD, BIND_KB, 0) == SDL_SCANCODE_R && bind_get(B_TPS_RELOAD, BIND_KB, 1) == SDL_SCANCODE_G);
        CHECK(bind_get(B_RELOAD, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_LEFT && bind_get(B_TPS_QUICKTURN, BIND_PAD, 0) == 1 + SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    }
    bind_set(B_TPS_RUN, BIND_PAD, 0, 1 + SDL_GAMEPAD_BUTTON_WEST);
    CHECK(bind_ini_line("pad_l2", "-lefty") && bind_get(B_L2, BIND_PAD, 0) == BIND_AXIS + SDL_GAMEPAD_AXIS_LEFTY * 2 + 1);
    CHECK(bind_ini_line("pad_l2", "none") && bind_ini_line("pad_cross", "south") && !bind_ini_line("coop_port", "7140"));
    /* input source (pure): keyboard/mouse in Auto and K/M only; a gamepad in Auto and Gamepad only, any or the chosen player */
    CHECK(input_accept(INPUT_AUTO, 0, BIND_KB, 0) && input_accept(INPUT_KBM, 0, BIND_KB, 0) && !input_accept(INPUT_PAD, 0, BIND_KB, 0));
    CHECK(input_accept(INPUT_AUTO, 0, BIND_PAD, 3) && !input_accept(INPUT_KBM, 0, BIND_PAD, 0) && input_accept(INPUT_PAD, 0, BIND_PAD, -1));
    CHECK(input_accept(INPUT_PAD, 2, BIND_PAD, 1) && !input_accept(INPUT_PAD, 2, BIND_PAD, 0) && !input_accept(INPUT_PAD, 1, BIND_PAD, -1));
    CHECK(input_accept(INPUT_AUTO, 1, BIND_PAD, 0) && !input_accept(INPUT_AUTO, 1, BIND_PAD, 1) && !input_accept(INPUT_KBM, 1, BIND_PAD, 0));
    /* Gamepad only: the keyboard (and mouse buttons) no longer hold any row */
    g_input_source = INPUT_PAD;
    CHECK(!held(B_CROSS, SDL_SCANCODE_X) && !held(B_TPS_FWD, SDL_SCANCODE_W) && !bind_held(B_TPS_FIRE, BIND_KB, keys, SDL_BUTTON_LMASK));
    g_input_source = INPUT_KBM;
    CHECK(held(B_CROSS, SDL_SCANCODE_X) && bind_held(B_TPS_FIRE, BIND_KB, keys, SDL_BUTTON_LMASK));
    g_input_source = INPUT_AUTO;
    /* gamepad: a virtual Xbox pad through SDL's gamepad layer */
    if (SDL_Init(SDL_INIT_GAMEPAD)) {
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_MISC1;
        d.button_mask = (1u << SDL_GAMEPAD_BUTTON_MISC1) - 1;
        d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        d.vendor_id = 0x045e; d.product_id = 0x02ea; d.name = "test pad";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
        SDL_Gamepad* g = SDL_OpenGamepad(id);
        SDL_Joystick* j = SDL_GetGamepadJoystick(g);
        CHECK(g);
        bind_set_gamepad(g);
        CHECK(!strcmp(bind_code_name(BIND_PAD, bind_get(B_CROSS, BIND_PAD, 0), buf, sizeof(buf)), "A"));
        CHECK(!strcmp(bind_code_name(BIND_PAD, bind_get(B_FREEAIM, BIND_PAD, 0), buf, sizeof(buf)), "LT"));
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_SOUTH, true);
        SDL_UpdateGamepads();
        CHECK(bind_held(B_CROSS, BIND_PAD, keys, 0) && !bind_held(B_TRIANGLE, BIND_PAD, keys, 0));
        bind_set(B_CROSS, BIND_PAD, 0, 1 + SDL_GAMEPAD_BUTTON_NORTH);   /* swaps with Triangle */
        CHECK(!bind_held(B_CROSS, BIND_PAD, keys, 0) && bind_held(B_TRIANGLE, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_SOUTH, false);
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
        SDL_UpdateGamepads();
        CHECK(bind_held(B_FREEAIM, BIND_PAD, keys, 0) && !bind_held(B_L2, BIND_PAD, keys, 0) && !bind_held(B_TRIANGLE, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
        /* third-person play with the modern scheme: the third-person rows play, the game rows lose their buttons */
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
        SDL_UpdateGamepads();
        CHECK(bind_held(B_QUICKKNIFE, BIND_PAD, keys, 0) && !bind_held(B_TPS_FIRE, BIND_PAD, keys, 0));   /* not in third-person play */
        g_tps.pad_play = 1;
        CHECK(!bind_held(B_QUICKKNIFE, BIND_PAD, keys, 0) && !bind_held(B_TPS_FIRE, BIND_PAD, keys, 0));   /* held across the change: neither */
        CHECK(bind_held(B_UP, BIND_PAD, keys, 0));                                                         /* the d-pad (but the quick turn's back) stays the game's */
        /* the weapon wheel up: the d-pad is its own, for game and third-person rows; its button reads on a third-person
           button too (R3, Center Camera), which then does not */
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true);
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_DPAD_DOWN, true);
        SDL_UpdateGamepads();
        CHECK(bind_held(B_WPN_HOLD, BIND_PAD, keys, 0) && bind_held(B_TPS_QUICKTURN, BIND_PAD, keys, 0));
        g_wpn_overlay = 1;
        CHECK(!bind_held(B_UP, BIND_PAD, keys, 0) && !bind_held(B_TPS_QUICKTURN, BIND_PAD, keys, 0) && !bind_held(B_TPS_RECENTER, BIND_PAD, keys, 0));
        CHECK(bind_held(B_WPN_HOLD, BIND_PAD, keys, 0) && bind_held_through(B_UP, BIND_PAD, keys, 0) && bind_held_through(B_DOWN, BIND_PAD, keys, 0));
        CHECK(held(B_CROSS, SDL_SCANCODE_X) && !held(B_UP, SDL_SCANCODE_UP) && !held(B_FREEAIM, SDL_SCANCODE_E));   /* the keyboard's arrows and E too */
        CHECK(held(B_L1, SDL_SCANCODE_Q) && held(B_R1, SDL_SCANCODE_W));   /* L1 / R1 not (Q/W paged it before the page row) */
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true);   /* LB: the third-person knife's still */
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);   /* RB: the page, not reload */
        SDL_UpdateGamepads();
        CHECK(bind_held(B_TPS_KNIFE, BIND_PAD, keys, 0));
        CHECK(bind_held_through(B_WPN_PAGE, BIND_PAD, keys, 0) && !bind_held(B_TPS_RELOAD, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false);
        SDL_UpdateGamepads();
        g_tps.kb_play = 1;   /* the page key in third person, where E is action: the wheel's */
        held(B_TPS_ACTION, SDL_SCANCODE_E);
        CHECK(bind_held_through(B_WPN_PAGE, BIND_KB, keys, 0) && !bind_held(B_TPS_ACTION, BIND_KB, keys, 0));
        g_tps.kb_play = 0;
        g_wpn_overlay = 0;
        CHECK(bind_held(B_UP, BIND_PAD, keys, 0) && bind_held(B_TPS_RECENTER, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false);
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_DPAD_DOWN, false);
        SDL_UpdateGamepads();
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        SDL_UpdateGamepads();
        CHECK(!bind_held(B_TPS_FIRE, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_WEST, true);
        SDL_UpdateGamepads();
        CHECK(bind_held(B_TPS_FIRE, BIND_PAD, keys, 0) && !bind_held(B_QUICKKNIFE, BIND_PAD, keys, 0));
        CHECK(bind_held(B_TPS_RUN, BIND_PAD, keys, 0) && !bind_held(B_SQUARE, BIND_PAD, keys, 0));
        /* Start opens the options there (Select is the map, L2 of type D the aim) */
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_START, true);
        SDL_UpdateGamepads();
        CHECK(bind_held(B_TPS_OPTIONS, BIND_PAD, keys, 0) && !bind_held(B_START, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_START, false);
        SDL_UpdateGamepads();
        g_tps.pad_play = 0;   /* a menu: the game rows again, once let go */
        CHECK(!bind_held(B_QUICKKNIFE, BIND_PAD, keys, 0) && !bind_held(B_TPS_FIRE, BIND_PAD, keys, 0) && !bind_held(B_SQUARE, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_WEST, false);
        SDL_SetJoystickVirtualButton(j, SDL_GAMEPAD_BUTTON_DPAD_UP, false);
        SDL_UpdateGamepads();
        CHECK(!bind_held(B_QUICKKNIFE, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
        SDL_UpdateGamepads();
        CHECK(bind_held(B_QUICKKNIFE, BIND_PAD, keys, 0) && !bind_held(B_TPS_FIRE, BIND_PAD, keys, 0));
        SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
        SDL_UpdateGamepads();
        SDL_CloseGamepad(g);
        bind_set_gamepad(NULL);
        {   /* which gamepad: SDL's player numbers, kept by a pad that reconnects */
            SDL_JoystickID id2, id1;
            id2 = SDL_AttachVirtualJoystick(&d);
            CHECK(SDL_GetGamepadPlayerIndexForID(id) == 0 && SDL_GetGamepadPlayerIndexForID(id2) == 1);
            CHECK(bind_pick_pad() == id);                                   /* Auto, any: the first */
            g_input_pad = 2; CHECK(bind_pick_pad() == id2);
            CHECK(!strcmp(bind_pad_choice(2, buf, sizeof(buf)), "2: test pad")   /* the gamepad mapping's name (same VID/PID) */ && !strcmp(bind_pad_choice(0, buf, sizeof(buf)), "Any"));
            CHECK(!strcmp(bind_pad_choice(3, buf, sizeof(buf)), "3: not connected"));
            g_input_pad = 3; CHECK(bind_pick_pad() == 0);
            g_input_source = INPUT_KBM; g_input_pad = 0; CHECK(bind_pick_pad() == 0);
            g_input_source = INPUT_PAD; g_input_pad = 1; CHECK(bind_pick_pad() == id);
            SDL_DetachVirtualJoystick(id);                                  /* player 1 unplugged: nobody else takes its number */
            CHECK(bind_pick_pad() == 0);
            g_input_pad = 2; CHECK(bind_pick_pad() == id2);
            id1 = SDL_AttachVirtualJoystick(&d);                            /* back: player 1 again, though listed after pad 2 */
            g_input_pad = 1; CHECK(id1 != id && bind_pick_pad() == id1);
            g_input_pad = 0; CHECK(bind_pick_pad() == id2);                 /* any: the first listed */
            /* Gamepad only lets SDL deliver gamepad input while the window is in the background */
            bind_apply_source(); CHECK(SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, false));
            g_input_source = INPUT_AUTO;
            bind_apply_source(); CHECK(!SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, true));
        }
        SDL_Quit();
    } else printf("no SDL gamepad subsystem (%s): gamepad part skipped\n", SDL_GetError());
    printf("bindings: ok\n");
    return 0;
}
