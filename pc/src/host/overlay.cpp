/*
 * F1 overlay (Dear ImGui): display, audio and port settings. While it is open the game
 * keeps running but receives no pad input. Settings are applied immediately and saved
 * to <save_dir>/recvx.ini.
 */
#include "../ps2_platform.h"
extern "C" {
#include "../audio/audio.h"
}
#include "prof.h"
#include "cheat.h"
#include "coop.h"
#include "actors.h"
#include "cast.h"
#include "enemies.h"
#include "net/net.h"
#include "net/coop_proto.h"   /* REC_*: what the host keeps for us */
#include "../gs/stream.h"
#include "tps.h"
#include "menu_mouse.h"
#include "bindings.h"
#include "settings.h"
#include "lobby.h"
#include "../import/import.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

extern "C" void pad_set_game_input_enabled(int on);

static SDL_Window* win;
static bool ready, visible, cheats_visible, coop_visible, import_visible;
static bool tps_key_wait;   /* the next key pressed becomes the third-person toggle key */

void overlay_init(void* window, void* gl_context)
{
    win = (SDL_Window*)window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   /* our settings live in recvx.ini */
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(win, (SDL_GLContext)gl_context);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ready = true;
    const char* show = SDL_getenv("RECVX_SHOW_OVERLAY");   /* debugging aid (screenshots): 1 F1, 2 the cheats (F2), 3 the co-op window, 4 import (F9) */
    visible = show && strcmp(show, "3") && strcmp(show, "2") && strcmp(show, "4");
    import_visible = show && !strcmp(show, "4");
    cheats_visible = show && !strcmp(show, "2");
    coop_visible = show && !strcmp(show, "3");
}

int overlay_event(const void* e)
{
    const SDL_Event* ev = (const SDL_Event*)e;
    if (!ready) return 0;
    if (tps_key_wait && ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat) {
        if (ev->key.scancode != SDL_SCANCODE_ESCAPE) { g_tps.key = ev->key.scancode; settings_save_later(); }
        tps_key_wait = false;
        return 1;
    }
    if (bind_hotkey(ev, B_SETTINGS)) {
        visible = !visible;
        return 1;
    }
    if (bind_hotkey(ev, B_CHEATS)) {
        cheats_visible = !cheats_visible;
        return 1;
    }
    if (bind_hotkey(ev, B_COOP)) {
        coop_visible = !coop_visible;
        return 1;
    }
    if (g_dev_mode && bind_hotkey(ev, B_IMPORT)) {
        import_visible = !import_visible;
        return 1;
    }
    ImGui_ImplSDL3_ProcessEvent(ev);
    if (!visible && !cheats_visible && !coop_visible && !import_visible) return 0;
    return (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
            ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL);
}

/* the rows of a page of the shared table (settings.c): all of them, or on the pages that have
   their own windows (CO-OP: F3, EXTRAS: F2) the ones flagged SET_F1. Values are applied by
   settings_poll (next pumped frame) and recvx.ini written once they stop changing. true = changed */
/* the picture calibration test pattern (settings_calib), in the colours the game's picture gets
   (the overlay itself is drawn without the calibration) */
static void calib_pattern(void)
{
    float sq = ImGui::GetFontSize() * 1.5f, gap = sq * 0.5f, w = CALIB_STEPS * (sq + gap) + gap;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto grey = [](int v) { int g = (int)(gs_picture(v / 255.0f) * 255.0f + 0.5f); return IM_COL32(g, g, g, 255); };
    for (int k = 0; k < 2; k++) {
        float x = p.x + k * (w + gap);
        dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + w, p.y + sq + 2 * gap), grey(k ? 255 : 0));
        for (int i = 0; i < CALIB_STEPS; i++)
            dl->AddRectFilled(ImVec2(x + gap + i * (sq + gap), p.y + gap), ImVec2(x + gap + i * (sq + gap) + sq, p.y + gap + sq), grey(settings_calib[k][i]));
    }
    ImGui::Dummy(ImVec2(2 * w + gap, sq + 2 * gap));
}

/* the cutscene cast's "Custom" models under Model Selection (cast.h; the lobby and the BIORAND screen list the same) */
static bool cast_custom_imgui()
{
    static short pos[CAST_CUSTOM_MAX];
    static int mdl[CAST_CUSTOM_MAX];
    int n = cast_custom_list(g_enemy_opts.cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0, pos, mdl, CAST_CUSTOM_MAX), on = 0, set = -1;
    bool changed = false;
    char head[80];
    for (int k = 0; k < n; k++) on += !cast_off_at(cast_off, pos[k]);
    SDL_snprintf(head, sizeof head, "Custom Models (%d of %d checked)###castcustom", on, n);
    if (!ImGui::TreeNodeEx(head, ImGuiTreeNodeFlags_DefaultOpen)) return false;
    if (ImGui::Button("Select All")) set = 0;
    ImGui::SameLine();
    if (ImGui::Button("Deselect All")) set = 1;
    if (set >= 0) { for (int k = 0; k < n; k++) cast_off_put(cast_off, pos[k], set); changed = true; }
    if (!on) { ImGui::SameLine(); ImGui::TextDisabled("None checked: the characters keep their own looks."); }
    if (ImGui::BeginChild("##castcustom", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * (n < 12 ? n + 1 : 12)), ImGuiChildFlags_Borders)) {
        const char* grp = nullptr;
        for (int k = 0; k < n; k++) {
            const char* g = cast_entry_group(mdl[k]);
            char label[128];
            bool b = !cast_off_at(cast_off, pos[k]);
            if (!grp || strcmp(g, grp)) ImGui::SeparatorText(g);
            grp = g;
            SDL_snprintf(label, sizeof label, "%s##cc%d", cast_entry_name(mdl[k]), k);
            if (ImGui::Checkbox(label, &b)) { cast_off_put(cast_off, pos[k], !b); changed = true; }
        }
    }
    ImGui::EndChild();
    ImGui::TreePop();
    if (changed) settings_save_later();
    return changed;
}

static bool settings_imgui(int page, bool all = true)
{
    bool changed = false;
    for (int i = 0; i < g_settings_n; i++) {
        const Setting* s = &g_settings[i];
        if (s->page != page || !(all || (s->flags & SET_F1)) || (s->shown && !s->shown())) continue;
        if (s->group && all) ImGui::SeparatorText(s->group);
        const char* lock = settings_locked(s);
        char label[96], val[96];
        int v = settings_get(s), hi = settings_hi(s), st = s->step > 0 ? s->step : 1;
        SDL_snprintf(label, sizeof label, "%s%s##set%d", s->label, (s->flags & SET_RESTART) ? " (restart needed)" : "", i);
        ImGui::BeginDisabled(lock != nullptr);
        if (s->flags & SET_ACTION) {
            if (ImGui::Button(label)) settings_act(s);
        } else if (settings_is_bool(s)) {
            bool b = v != 0;
            if (ImGui::Checkbox(label, &b)) { settings_set(s, b); changed = true; }
        } else if (s->names || (s->name && (s->count || (hi - s->lo) / st <= 10))) {   /* a list (tenths: a slider) */
            if (ImGui::BeginCombo(label, settings_value(s, val, sizeof val), ImGuiComboFlags_HeightLarge)) {
                for (int k = s->lo; k <= hi; k += st) {
                    char nm[96];
                    if (ImGui::Selectable(s->name ? s->name(k, nm, sizeof nm) : s->names[k - s->lo], k == v)) { settings_set(s, k); changed = true; }
                    if (k == v) ImGui::SetItemDefaultFocus();   /* the list opens at the current value (a long one: "You appear as") */
                }
                ImGui::EndCombo();
            }
            if (s->val == &g_enemy_opts.cast_all && v == 2) {   /* "Custom": its list below (its tooltip stays the row's) */
                bool hover = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
                if (hover && s->help) ImGui::SetTooltip("%s", s->help);
                ImGui::EndDisabled();
                changed |= cast_custom_imgui();
                continue;
            }
        } else {   /* a number; a value name (tenths) is shown as the slider's text */
            char fmt[96];
            const char* f = s->fmt ? s->fmt : "%d";
            if (s->name) {
                char* o = fmt;
                for (const char* c = s->name(v, val, sizeof val); *c && o < fmt + sizeof fmt - 2; c++) { if (*c == '%') *o++ = '%'; *o++ = *c; }
                *o = 0;
                f = fmt;
            }
            if (ImGui::SliderInt(label, &v, s->lo, hi, f)) {
                settings_set(s, st > 1 ? (v + st / 2) / st * st : v);
                changed = true;
            }
        }
        ImGui::EndDisabled();
        if ((ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ((s->flags & SET_CALIB) && ImGui::IsItemActive())) && (s->help || lock)) {
            ImGui::BeginTooltip();
            ImGui::Text("%s%s%s", lock ? lock : "", lock && s->help ? "\n" : "", s->help ? s->help : "");
            if (s->flags & SET_CALIB) calib_pattern();
            ImGui::EndTooltip();
        }
    }
    return changed;
}

static bool settings_imgui_any(int page)
{
    for (int i = 0; i < g_settings_n; i++) if (g_settings[i].page == page && (g_settings[i].flags & SET_F1)) return true;
    return false;
}

/* Controls: the bindings of both sets; a click on a binding waits for a key (bindings.c) */
static void controls_section(void)
{
    static int dev, cap_row = -1, cap_slot;
    static char msg[128];
    char label[128], nm[64];
    if (cap_row >= 0) {
        int c = bind_capture_poll();
        if (c != -1) {
            if (c != -2) {
                bind_set(cap_row, dev, cap_slot, c == -3 ? 0 : c);
                if (bind_swapped()) SDL_snprintf(msg, sizeof msg, "Swapped with %s", bind_swapped());
                else if (bind_combo(cap_row, dev, cap_slot)) SDL_snprintf(msg, sizeof msg, "Hold Aim and press this button to reload.");
                else msg[0] = 0;
                settings_save_later();
            }
            cap_row = -1;
        }
    }
    settings_imgui(SP_CONTROLS);   /* input source, which gamepad */
    if (ImGui::BeginTabBar("##dev")) {
        if (ImGui::BeginTabItem("Keyboard and Mouse")) { if (dev != BIND_KB) cap_row = -1; dev = BIND_KB; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Gamepad")) { if (dev != BIND_PAD) cap_row = -1; dev = BIND_PAD; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    char pad_name[96];
    if (dev == BIND_PAD) ImGui::TextDisabled("%s. The left stick also moves your character.\n"
                                             "Modern third person uses its own bindings below.", bind_pad_desc(pad_name, sizeof pad_name));
    ImGui::TextDisabled("Click a binding, then press an input. Esc: cancel. Delete or right-click: clear.\n"
                        "Reload can share a button: hold Aim and press it to reload.");
    if (dev == BIND_KB) ImGui::TextDisabled("Mouse buttons work on any row, also with the fixed cameras.");
    if (msg[0]) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", msg);
    if (ImGui::BeginTable("##binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("1", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("2", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        int group = -1;
        for (int i = 0; i < B_COUNT; i++) {
            int r = bind_order[i];
            if (!bind_row_shown(r, dev)) continue;
            if (bind_rows[r].group != group) {
                group = bind_rows[r].group;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (group == BG_GAME) ImGui::SeparatorText("Game Controls (Control Type)");
                else ImGui::SeparatorText(group == BG_TPS ? (dev ? "Third Person: Modern Gamepad" : "Third Person: Keyboard and Mouse") : "Hotkeys");
                if (group == BG_GAME) { ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("type %c", 'A' + pc_keytype()); }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            int conflict = bind_conflict(r, dev);
            if (conflict) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", bind_row_label(r, label, sizeof label));
            else ImGui::TextUnformatted(bind_row_label(r, label, sizeof label));
            if (conflict && ImGui::IsItemHovered()) ImGui::SetTooltip("Also bound to: %s", bind_rows[conflict - 1].name);
            for (int k = 0; k < BIND_SLOTS; k++) {
                ImGui::TableSetColumnIndex(1 + k);
                char b[96];
                if (cap_row == r && cap_slot == k) SDL_snprintf(b, sizeof b, "press... %ds##%d_%d", (bind_capture_left_ms() + 999) / 1000, r, k);
                else SDL_snprintf(b, sizeof b, "%s%s##%d_%d", bind_combo(r, dev, k) ? "Aim + " : "", bind_code_name(dev, bind_get(r, dev, k), nm, sizeof nm), r, k);
                if (ImGui::Button(b, ImVec2(-FLT_MIN, 0))) { cap_row = r; cap_slot = k; msg[0] = 0; bind_capture_start(dev); }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { bind_set(r, dev, k, 0); settings_save_later(); }
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(dev ? "Reset Gamepad Bindings" : "Reset Keyboard Bindings")) { bind_reset(dev); settings_save_later(); msg[0] = 0; }
    ImGui::TextDisabled("Also available in OPTION > ENHANCED SETTINGS > CONTROLS.");
}

static void settings_window(void)
{
    bool changed = false;
    int ww, wh;
    SDL_GetWindowSizeInPixels(win, &ww, &wh);

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Resident Evil Code: Veronica X  -  settings (F1)  -  " RECVX_VERSION, &visible)) { ImGui::End(); return; }

    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= settings_imgui(SP_DISPLAY);
        ImGui::TextDisabled("Resolution: %d x %d | %.1f FPS", ww, wh, ImGui::GetIO().Framerate);
    }
    if (ImGui::CollapsingHeader("Gameplay", ImGuiTreeNodeFlags_DefaultOpen)) changed |= settings_imgui(SP_GAMEPLAY);
    if (ImGui::CollapsingHeader("Controls")) controls_section();
    if (ImGui::CollapsingHeader("Third Person", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= settings_imgui(SP_TPS);
        char key[96];
        SDL_snprintf(key, sizeof key, tps_key_wait ? "Press a key (Esc cancels)" : "Camera Key: %s", SDL_GetScancodeName((SDL_Scancode)g_tps.key));
        if (ImGui::Button(key)) tps_key_wait = true;
        ImGui::TextDisabled(g_tps.free_cam
                            ? "Move in the direction shown on screen; use the mouse to turn the camera.\n"
                              "Change action buttons under Controls; F1 releases the mouse."
                            : "Move forward or back and turn with your movement keys; use the mouse to look.\n"
                              "Change action buttons under Controls; F1 releases the mouse.");
        if (g_tps.pad_modern)
            ImGui::TextDisabled("Left stick: move and run. Right stick: look and aim.\n"
                                "Set aiming, action, inventory and camera buttons under Controls.");
    }
    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) changed |= settings_imgui(SP_AUDIO);
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) changed |= settings_imgui(SP_APPEAR);
    for (int p = SP_COOP; p <= SP_EXTRAS; p += SP_EXTRAS - SP_COOP)   /* pages with their own windows (F3, F2): the SET_F1 rows */
        if (settings_imgui_any(p) && ImGui::CollapsingHeader(p == SP_COOP ? "Co-op" : "Extras", ImGuiTreeNodeFlags_DefaultOpen))
            changed |= settings_imgui(p, false);

    if (ImGui::CollapsingHeader("Performance")) {
        static ProfFrame h[240];
        bool on = prof_enabled != 0;
        if (ImGui::Checkbox("Measure (small overhead; always on with --profile)", &on)) prof_enabled = on;
        int n = prof_history(h, 240);
        if (n > 0) {
            float total[240], avg[PROF_COUNT] = { 0 }, avg_total = 0, worst = 0;
            int m = n < 60 ? n : 60;
            for (int i = 0; i < n; i++) total[i] = h[i].total;
            for (int i = n - m; i < n; i++) {
                avg_total += h[i].total / m;
                if (h[i].total > worst) worst = h[i].total;
                for (int c = 0; c < PROF_COUNT; c++) avg[c] += h[i].ms[c] / m;
            }
            ImGui::Text("game frame %.2f ms avg, %.2f ms worst (target 33.37 ms = 29.97 Hz)", avg_total, worst);
            ImGui::PlotLines("##ft", total, n, 0, "frame time (ms)", 0.0f, 66.7f, ImVec2(-1, 60));
            for (int c = 0; c < PROF_COUNT; c++) {
                ImGui::Text("%-8s %6.2f ms", prof_names[c], avg[c]);
                ImGui::SameLine(160);
                ImGui::ProgressBar(avg_total > 0 ? avg[c] / avg_total : 0, ImVec2(-1, 0), "");
            }
            ImGui::TextDisabled("'wait' is idle time; run with --profile for a CSV.");
        }
    }
    ImGui::End();
    if (changed) settings_save_later();
}

/* F2 teleport: location, room and entry by name (teleport.c), a search over all rooms, or the raw numbers */
static void teleport_pick(CheatState* c)
{
    static bool raw;
    static char filter[48];
    char b[128], e[160];
    int n = tp_count(), k, cur = tp_find(c->tp_stg, c->tp_rom);
    if (n && !raw) {
        if (ImGui::BeginCombo("Location", tp_stage_name(c->tp_stg), ImGuiComboFlags_HeightLarge)) {
            for (int s = 0; s < 10; s++) {
                int first = tp_stage_first(s, &k);
                if (k && ImGui::Selectable(tp_stage_name(s), s == c->tp_stg)) { c->tp_stg = s; c->tp_rom = tp_room(first)->rom; c->tp_pos = 0; }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Room", cur >= 0 ? tp_room_name(cur, 1, b, sizeof b) : "-", ImGuiComboFlags_HeightLarge)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##filter", "type to search every location", filter, sizeof filter);
            for (int i = 0; i < n; i++) {
                const TpRoom* r = tp_room(i);
                tp_room_name(i, 1, b, sizeof b);
                if (filter[0] ? !SDL_strcasestr(b, filter) && !SDL_strcasestr(tp_stage_name(r->stg), filter) : r->stg != c->tp_stg) continue;
                if (filter[0]) snprintf(e, sizeof e, "%s   %s##%d", b, tp_stage_name(r->stg), i);
                else snprintf(e, sizeof e, "%s##%d", b, i);
                if (ImGui::Selectable(e, i == cur)) { c->tp_stg = r->stg; c->tp_rom = r->rom; c->tp_pos = 0; }
                if (i == cur && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (cur >= 0) {
            int m = tp_entries(cur);
            if (c->tp_pos >= m) c->tp_pos = 0;
            if (ImGui::BeginCombo("Entry", tp_entry_name(cur, c->tp_pos, b, sizeof b), ImGuiComboFlags_HeightLarge)) {
                for (int p = 0; p < m; p++) {
                    snprintf(e, sizeof e, "%s##%d", tp_entry_name(cur, p, b, sizeof b), p);
                    if (ImGui::Selectable(e, p == c->tp_pos)) c->tp_pos = p;
                    if (ImGui::IsItemHovered() && tp_entry_sources(cur, p, b, sizeof b)[0]) ImGui::SetTooltip("Doors here from:\n%s", b);
                }
                ImGui::EndCombo();
            }
        }
        ImGui::TextDisabled("A room may not work in every story state.");
    } else {
        if (!n) ImGui::TextDisabled(tp_reading() ? "Loading room list..." : "The room list could not be read.");
        ImGui::TextDisabled("Some destinations may crash the game.");
        ImGui::InputInt("Stage", &c->tp_stg);
        ImGui::InputInt("Room", &c->tp_rom);
        ImGui::InputInt("Position", &c->tp_pos);
        c->tp_stg = SDL_clamp(c->tp_stg, 0, 9); c->tp_rom = SDL_clamp(c->tp_rom, 0, 99); c->tp_pos = SDL_clamp(c->tp_pos, 0, TP_POS - 1);
    }
    ImGui::Checkbox("Show Room Numbers", &raw);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stage, room and entry position as numbers (as RECVX_TELEPORT), also rooms not in the list.");
    ImGui::SameLine();
}

static void cheat_window(void)
{
    CheatState* c = &g_cheat;
    ImGui::SetNextWindowPos(ImVec2(500, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cheats (F2)", &cheats_visible)) { ImGui::End(); return; }
    ImGui::Text("%s  HP %d   stage %d  room %d  pos %d", c->player ? "Chris" : "Claire", c->hp, c->stg, c->rom, c->pos);
    bool b = c->inf_health;
    if (ImGui::Checkbox("Infinite Health", &b)) c->inf_health = b;
    ImGui::SameLine();
    if (ImGui::Button("Heal Now")) __atomic_or_fetch(&c->request, CHEAT_HEAL, __ATOMIC_ACQ_REL);
    b = c->inf_ammo;
    if (ImGui::Checkbox("Infinite Ammo", &b)) c->inf_ammo = b;
    {
        static const char* modes[] = { "Off", "Walls", "Walls and Flight" };
        int m = c->noclip;
        if (ImGui::Combo("Pass Through Walls", &m, modes, IM_ARRAYSIZE(modes))) c->noclip = m;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Move through walls, objects and enemies; doors still work.\n"
                              "Flight: Page Up/R2 up, F/Page Down/L2 down;\n"
                              "turn this off to return to the floor below.");
    }
    b = c->unlock;
    if (ImGui::Checkbox("Unlock With No Key", &b)) c->unlock = b;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open key locks without carrying the key;\n"
                          "no items are added and story locks stay.\n"
                          "Supported locks update in the current room.");

    ImGui::SeparatorText("Add Item");
    char label[48];
    snprintf(label, sizeof label, "%3d  %s", c->give_id, c->give_id > 0 && c->give_id < CHEAT_ITEMS ? c->names[c->give_id] : "-");
    if (ImGui::BeginCombo("Item", label, ImGuiComboFlags_HeightLarge)) {
        for (int id = 1; id < CHEAT_ITEMS; id++) {
            if (!c->names[id][0]) continue;
            snprintf(label, sizeof label, "%3d  %s", id, c->names[id]);
            if (ImGui::Selectable(label, id == c->give_id)) c->give_id = id;
        }
        ImGui::EndCombo();
    }
    ImGui::InputInt("Quantity", &c->give_count);
    c->give_count = SDL_clamp(c->give_count, 1, 999);
    if (ImGui::Button("Add to Inventory") && c->give_id) __atomic_or_fetch(&c->request, CHEAT_GIVE, __ATOMIC_ACQ_REL);

    ImGui::SeparatorText("Inventory");
    for (int i = 1; i < 2 + c->slots && i < 12; i++) {
        unsigned id = c->inv[i] >> 16 & 0xFF;
        char nm[16];
        if (!id) continue;
        if (id >= CHEAT_ITEMS || !c->names[id][0]) SDL_snprintf(nm, sizeof nm, "item %u", id);
        ImGui::Text("%s %-24s %u", (unsigned)i == c->inv[0] ? "equipped" : "        ", id < CHEAT_ITEMS && c->names[id][0] ? c->names[id] : nm, c->inv[i] & 0xFFFF);
    }

    ImGui::SeparatorText("Spawn Enemy");
    if (c->spawn_idx < 0 || c->spawn_idx >= spawn_count()) c->spawn_idx = 0;
    if (ImGui::BeginCombo("Enemy", spawn_name(c->spawn_idx), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < spawn_count(); i++)
            if (ImGui::Selectable(spawn_name(i), i == c->spawn_idx)) c->spawn_idx = i;
        ImGui::EndCombo();
    }
    if (ImGui::Button("Spawn Ahead")) __atomic_or_fetch(&c->request, CHEAT_SPAWN, __ATOMIC_ACQ_REL);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Spawned enemies disappear when you leave; kills do not count.\n"
                          "Bosses and scripted enemies may fail\n"
                          "outside their own rooms.");

    ImGui::SeparatorText("Teleport");
    teleport_pick(c);
    if (ImGui::Button("Use Current Room")) { c->tp_stg = c->stg; c->tp_rom = c->rom; c->tp_pos = c->pos; }
    ImGui::SameLine();
    if (ImGui::Button("Go")) __atomic_or_fetch(&c->request, CHEAT_TELEPORT, __ATOMIC_ACQ_REL);
    if (c->result) ImGui::TextDisabled("%s", c->result);
    ImGui::End();
}

/* co-op trade (docs/COOP.md "Trading from the F3 window"): offer an item to the partner standing next to you */
static const char* coop_item(unsigned int e, char* buf, size_t n)
{
    unsigned id = (e >> 16) & 0xFF;
    if (id < CHEAT_ITEMS && g_cheat.names[id][0]) snprintf(buf, n, "%s x%u", g_cheat.names[id], e & 0xFFFF);
    else snprintf(buf, n, "item %u x%u", id, e & 0xFFFF);
    return buf;
}

extern "C" float gs_hor_scale(void);

static CoopStatus cs;   /* co-op: what the game thread published (coop_status_get, once per overlay frame) */
static int tag_flags;   /* the partner's head in the shown frame (STREAM_PT_*, 0: not drawn), game screen */
static float tag_x, tag_y;
void overlay_point(int flags, float x, float y) { tag_flags = flags; tag_x = x; tag_y = y; }

static void coop_trade(void)
{
    CoopState* c = &g_coop;
    char b[64];
    ImGui::SeparatorText("Trade");
    if (cs.trade_in) {
        ImGui::Text("Partner offers: %s", coop_item(cs.trade_in, b, sizeof(b)));
        ImGui::BeginDisabled(!cs.trade_near);
        if (ImGui::Button("Accept")) __atomic_store_n(&c->trade_req, COOP_TRADE_ACCEPT, __ATOMIC_RELEASE);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Decline")) __atomic_store_n(&c->trade_req, COOP_TRADE_DECLINE, __ATOMIC_RELEASE);
    }
    if (cs.trade_out) {
        ImGui::Text("Offering: %s (waiting for your partner)", coop_item(cs.trade_out, b, sizeof(b)));
        if (ImGui::Button("Withdraw Offer")) __atomic_store_n(&c->trade_req, COOP_TRADE_CANCEL, __ATOMIC_RELEASE);
    } else if (!cs.trade_near) {
        ImGui::TextDisabled("Stand next to your partner to offer items.");
    } else {
        int slots = cs.trade_slots < 8 ? 8 : cs.trade_slots > 10 ? 10 : cs.trade_slots;
        if (c->trade_slot < 2 || c->trade_slot >= 2 + slots || !(cs.trade_inv[c->trade_slot] >> 16)) c->trade_slot = 2;
        if (ImGui::BeginCombo("Item", (cs.trade_inv[c->trade_slot] >> 16) ? coop_item(cs.trade_inv[c->trade_slot], b, sizeof(b)) : "-")) {
            for (int s = 2; s < 2 + slots; s++) {
                if (!(cs.trade_inv[s] >> 16)) continue;
                char lb[80];
                snprintf(lb, sizeof(lb), "%s%s##%d", coop_item(cs.trade_inv[s], b, sizeof(b)), (int)cs.trade_inv[0] == s ? " (equipped)" : "", s);
                if (ImGui::Selectable(lb, c->trade_slot == s)) { c->trade_slot = s; c->trade_count = cs.trade_inv[s] & 0xFFFF; }
            }
            ImGui::EndCombo();
        }
        int have = cs.trade_inv[c->trade_slot] & 0xFFFF;
        if (c->trade_count < 1 || c->trade_count > have) c->trade_count = have;
        if (have > 1) ImGui::SliderInt("Quantity", &c->trade_count, 1, have);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose how much ammo or other stackable items to give. Other items are traded whole.");
        if (ImGui::Button("Offer")) __atomic_store_n(&c->trade_req, COOP_TRADE_OFFER, __ATOMIC_RELEASE);
    }
    if (cs.trade_msg[0]) ImGui::TextDisabled("%s", cs.trade_msg);
}

static void coop_window(void)
{
    CoopState* c = &g_coop;
    bool off = cs.state == COOP_OFF, changed = false;
    ImGui::SetNextWindowPos(ImVec2(940, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(cs.at_title ? "Co-op lobby (F3)###coop" : "Co-op (F3)###coop", &coop_visible)) { ImGui::End(); return; }
    static const char* loots[] = { "Shared loot", "Individual loot" };
    ImGui::BeginDisabled(!off);
    static const char* conns[] = { "Automatic", "Direct", "Server Relay" };
    if (ImGui::Combo("Connection", &c->conn, conns, IM_ARRAYSIZE(conns))) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Automatic tries direct first, then server;\n"
                          "addresses connect directly, and room codes need a co-op server.\n"
                          "Without a server, Automatic works like Direct;\n"
                          "Server Relay always uses the server.");
    /* Hide IP addresses: the address fields as passwords, "Show" reveals them until the window closes */
    static bool show_addr;
    if (!coop_visible || !c->hide_ip) show_addr = false;
    ImGuiInputTextFlags addr_flags = c->hide_ip && !show_addr ? ImGuiInputTextFlags_Password : 0;
    if (c->conn != COOP_CONN_DIRECT) {   /* first how to connect, then only what it needs (as the lobby) */
        if (ImGui::InputText("Co-op Server", c->server, sizeof(c->server), addr_flags)) changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Enter a co-op server address, with an optional port (default: 7141).\n"
                              "Both players use the same server; leave blank to use no server.");
        ImGui::InputText("Room Password", c->password, sizeof(c->password), ImGuiInputTextFlags_Password);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Optional, through the server: the host sets it, the joining player types the same. Not saved.");
    }
    if (ImGui::InputInt("Host Port", &c->port)) { c->port = c->port < 1 ? 1 : c->port > 65535 ? 65535 : c->port; changed = true; }
    bool by_code = coop_join_by_code(c);   /* Via server, or Automatic with a server: a room code (as the lobby's CODE row) */
    if (ImGui::InputText(by_code ? "Room code###join" : "Join address###join", c->join, sizeof(c->join), addr_flags)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(c->conn == COOP_CONN_SERVER ? "Enter the host's room code (XXXX-XXXX); case and dash do not matter.\n"
                                                        "Use the co-op server entered above."
                          : by_code ? "Enter the host's room code to join through the server above.\n"
                                      "A copied join address still connects directly."
                                    : "Paste the host's address from Copy Join Address.\n"
                                      "For a room code, choose Automatic or Server Relay and enter the co-op server.");
    if (c->hide_ip) {   /* also during a session */
        ImGui::EndDisabled();
        ImGui::Checkbox("Show Addresses", &show_addr);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide IP Addresses is on. Select this to reveal the address fields.");
        ImGui::BeginDisabled(!off);
    }
    if (ImGui::Combo("Loot Sharing (Host)", &c->loot, loots, IM_ARRAYSIZE(loots))) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Shared divides supplies; Individual gives each player a copy.\n"
                          "Keys and story progress go to both;\n"
                          "the host decides.");
    bool important = c->important != 0;
    if (ImGui::Checkbox("Duplicate Weapons and Ribbons", &important)) { c->important = important; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("With Shared loot, give each player their own weapons and ink ribbons.\n"
                          "The host decides.");
    bool upnp = c->upnp != 0;
    if (ImGui::Checkbox("Automatic Port Forwarding", &upnp)) { c->upnp = upnp; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ask your router to allow internet connections while you host.\n"
                          "Port forwarding ends when you stop hosting.");
    ImGui::EndDisabled();
    bool remember = c->remember != 0;   /* also during a session */
    if (ImGui::Checkbox("Remember Partner Items", &remember)) { c->remember = remember; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Keep a partner's items for their next visit.\n"
                          "After a crash, their items are restored automatically.");
    {   /* the joining player's inventory (as the lobby's ITEMS row) */
        static const char* fixed[] = { "Current Inventory", "Saved with the host", "New inventory" };
        char cur[48], lab[48];
        int v = c->items;
        auto label = [&](int i, char* b, size_t n) {
            if (i < 0) snprintf(b, n, "%s%s", fixed[i + 3], i == COOP_ITEMS_HOST && cs.state != COOP_OFF && !cs.host && cs.peer_rec >= 0
                                                              ? cs.peer_rec == REC_HAVE ? " (available)" : cs.peer_rec == REC_OFF ? " (host: off)" : " (none)" : "");
            else snprintf(b, n, "Save %02d", i + 1);
        };
        label(v, cur, sizeof(cur));
        ImGui::BeginDisabled(cs.state != COOP_OFF && cs.host);
        static LobbySave* saves;   /* listed when the list opens */
        static int saves_n;
        if (ImGui::BeginCombo("Starting Inventory", cur)) {
            if (ImGui::IsWindowAppearing()) { free(saves); saves_n = lobby_save_list(&saves); }
            for (int k = 0; k < 3 + saves_n; k++) {
                int i = k < 3 ? COOP_ITEMS_CURRENT + k : saves[k - 3].slot;
                label(i, lab, sizeof(lab));
                if (ImGui::Selectable(lab, i == v)) { c->items = i; changed = true; }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Choose current items, items saved by the host, or items from a save.\n"
                              "Unavailable items fall back to a new inventory;\n"
                              "after a crash, your items are restored automatically.");
    }
    bool tfreeze = c->trade_freeze != 0;   /* also during a session */
    if (ImGui::Checkbox("Pause Enemies During Trades", &tfreeze)) { c->trade_freeze = tfreeze; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pause enemies while both players are on the trade screen.\n"
                          "The host decides.");
    bool skipsync = c->skip_sync != 0;   /* also during a session */
    if (ImGui::Checkbox("Skip Cutscenes Together", &skipsync)) { c->skip_sync = skipsync; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Skip shared cutscenes only after both players choose to skip.\n"
                          "The host decides.");
    bool noribbon = g_config.no_ink_ribbon != 0;   /* the GAMEPLAY setting; in co-op the host's counts */
    if (ImGui::Checkbox("Free Typewriter Saves", &noribbon)) { g_config.no_ink_ribbon = noribbon; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save at typewriters without using ink ribbons.\n"
                          "The host decides in co-op.");
    if (ImGui::InputText("Your Name", c->name, sizeof(c->name), ImGuiInputTextFlags_CallbackCharFilter,
                         [](ImGuiInputTextCallbackData* d) -> int { return !(d->EventChar >= 32 && d->EventChar < 127 && (isalnum(d->EventChar) || strchr(" -_.!?'()+", d->EventChar))); }))
        changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The name your partner sees overhead and in the lobby.");
    bool tagopt = c->nametag != 0;
    if (ImGui::Checkbox("Partner Name Tag", &tagopt)) { c->nametag = tagopt; changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show your partner's name overhead, or at the screen edge when nearby.\nTheir menu and cutscene status stays visible either way.");
    bool follow = c->follow != 0;
    ImGui::BeginDisabled(cs.state != COOP_OFF && cs.host);   /* the joining player's choice: the host's own has no effect */
    if (ImGui::Checkbox("Follow Host", &follow)) { c->follow = follow; changed = true; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Follow the host through doors.\n"
                          "Off lets you explore separately;\n"
                          "joining, retries and character switches\n"
                          "can still reunite you,\n"
                          "as can long story cutscenes.");
    if (cs.state == COOP_CONNECTED) ImGui::TextDisabled("Session: %s", loots[cs.loot_now ? 1 : 0]);
    auto appear = [](void*, int i) -> const char* { return appear_name(appear_at(i)); };
    int pos = appear_pos(c->chr);   /* the list leaves out event doubles (actors.c) */
    if (ImGui::Combo("Character Model", &pos, appear, nullptr, appear_count(), 20)) { c->chr = appear_at(pos); changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose your appearance; your partner sees it too.\nModels can change movement and voice;\nAppearance settings control animations and cutscene models.");
    if (off) {
        /* at the title the window closes, so the pad drives the game's menu again */
        if (ImGui::Button("Host")) { __atomic_store_n(&c->request, COOP_REQ_HOST, __ATOMIC_RELEASE); coop_visible = !cs.at_title; }
        ImGui::SameLine();
        if (ImGui::Button("Join")) { __atomic_store_n(&c->request, COOP_REQ_JOIN, __ATOMIC_RELEASE); coop_visible = !cs.at_title; }
    } else {
        if (ImGui::Button("Disconnect")) __atomic_store_n(&c->request, COOP_REQ_STOP, __ATOMIC_RELEASE);
        if (cs.host && (cs.state == COOP_CONNECTED || cs.state == COOP_LOST)) {
            ImGui::SameLine();
            if (ImGui::Button("Remove Partner")) __atomic_store_n(&c->request, COOP_REQ_KICK, __ATOMIC_RELEASE);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove your partner and reopen their place in the lobby.\n"
                                  "They can rejoin with the same address or room code.");
        }
    }
    ImGui::TextWrapped("%s", cs.status[0] ? coop_shown(cs.status) : "Not connected");
    if (cs.state != COOP_OFF && cs.host && cs.code[0]) ImGui::Text("Join code: %.4s-%.4s", cs.code, cs.code + 4);
    if (cs.state != COOP_OFF && cs.host && cs.room[0]) {
        char room[16];
        snprintf(room, sizeof(room), "%.4s-%.4s", cs.room, cs.room + 4);
        ImGui::Text("Room code: %s", room);
        ImGui::SameLine();
        if (ImGui::Button("Copy Room Code")) SDL_SetClipboardText(room);
    }
    if (cs.state == COOP_CONNECTED && cs.peer_in_game) coop_trade();
    PortmapStatus pm;
    portmap_status(&pm);
    if (pm.local_ip[0] || pm.msg[0]) {   /* hosting */
        char addr[80];
        ImGui::SeparatorText("Internet");
        ImGui::Text("Local address: %s", pm.local_ip[0] ? coop_shown(pm.local_ip) : "?");
        if (pm.public_ip[0]) snprintf(addr, sizeof(addr), "%s:%d#%s", pm.public_ip, pm.public_port, cs.code);
        else snprintf(addr, sizeof(addr), "%s:%d#%s", pm.local_ip, c->port, cs.code);
        if (ImGui::Button(pm.public_ip[0] ? "Copy Join Address" : "Copy Local Address")) SDL_SetClipboardText(addr);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", coop_shown(addr));
        if (!pm.public_ip[0] && pm.state != PM_BUSY && !cs.room[0] && cs.state != COOP_CONNECTED)   /* (the router's status: the log only) */
            ImGui::TextDisabled("If your partner can't connect, use a co-op server (Automatic).");
    }
    if (cs.at_title)
        ImGui::TextDisabled("Host: start or load a game from the title screen.\n"
                            "Partner: wait at the title screen to join the host's game.");
    ImGui::End();
    if (changed) settings_save_later();
}

/* Above the partner's head (co-op): its name (option) and, while it is busy, what with, one
   box. Off screen or behind the camera (same room): a marker at the edge of the view pointing
   at it, with the name. Game screen (640x480, 4:3 area) to the window as gs_gl.c presents it;
   the text scales with the window height (FontScaleDpi). */
static void name_tag(void)
{
    static const char* what[] = { "", "cutscene", "menu", "item box", "trading" };
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float hs = gs_hor_scale(), a = 4.0f / 3.0f * hs, vw = ds.y * a, vh = ds.y;
    if (vw > ds.x) { vw = ds.x; vh = ds.x / a; }
    if (g_config.aspect == ASPECT_STRETCH && hs == 1.0f) { vw = ds.x; vh = ds.y; }
    float x0 = (ds.x - vw) * 0.5f, y0 = (ds.y - vh) * 0.5f;
    float x = x0 + (0.5f + (tag_x - 320.0f) / (640.0f * hs)) * vw;
    float y = y0 + tag_y / 480.0f * vh;
    bool behind = tag_flags == STREAM_PT_BEHIND;
    const char* name = g_coop.nametag ? (cs.peer_name[0] ? cs.peer_name : "Partner") : nullptr;
    const char* busy = cs.trade_hint[0] ? cs.trade_hint : cs.busy_kind > 0 && cs.busy_kind < 5 ? what[cs.busy_kind] : nullptr;   /* it asks us to trade / is busy */
    ImFont* font = ImGui::GetFont();
    float fs = ImGui::GetFontSize() * 1.25f, bs = ImGui::GetFontSize();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    bool off = behind || x < x0 || x > x0 + vw || y < y0 || y > y0 + vh;
    if (!off) {
        ImVec2 ns = name ? font->CalcTextSizeA(fs, FLT_MAX, 0, name) : ImVec2(0, 0);
        ImVec2 bz = busy ? font->CalcTextSizeA(bs, FLT_MAX, 0, busy) : ImVec2(0, 0);
        float w = (ns.x > bz.x ? ns.x : bz.x) + 10, h = ns.y + bz.y + 6, top = y - h - 2;
        dl->AddRectFilled(ImVec2(x - w * 0.5f, top), ImVec2(x + w * 0.5f, y - 2), IM_COL32(0, 0, 0, 140), 4.0f);
        if (name) dl->AddText(font, fs, ImVec2(x - ns.x * 0.5f, top + 3), IM_COL32(255, 255, 255, 240), name);
        if (busy) dl->AddText(font, bs, ImVec2(x - bz.x * 0.5f, top + 3 + ns.y), IM_COL32(255, 230, 120, 255), busy);
        return;
    }
    if (!name) return;
    /* direction from the view's centre (mirrored when behind the camera), to the edge */
    float cx = x0 + vw * 0.5f, cy = y0 + vh * 0.5f, dx = x - cx, dy = y - cy;
    if (behind) { dx = -dx; dy = -dy; if (dy < 0) dy = -dy; }   /* behind: towards the bottom */
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) { dx = 0; dy = 1; len = 1; }
    dx /= len; dy /= len;
    float m = fs * 1.2f, kx = dx ? ((dx > 0 ? vw : -vw) * 0.5f - (dx > 0 ? m : -m)) / dx : 1e9f, ky = dy ? ((dy > 0 ? vh : -vh) * 0.5f - (dy > 0 ? m : -m)) / dy : 1e9f;
    float k = kx < ky ? kx : ky, px = cx + dx * k, py = cy + dy * k, r = fs * 0.5f;
    ImVec2 tip(px + dx * r, py + dy * r), b1(px - dy * r * 0.6f, py + dx * r * 0.6f), b2(px + dy * r * 0.6f, py - dx * r * 0.6f);
    dl->AddTriangleFilled(tip, b1, b2, IM_COL32(255, 230, 120, 220));
    ImVec2 ns = font->CalcTextSizeA(bs, FLT_MAX, 0, name);
    float tx = px - dx * (r + 4) - ns.x * 0.5f - dx * ns.x * 0.5f, ty = py - dy * (r + 4) - ns.y * 0.5f - dy * ns.y * 0.5f;
    dl->AddRectFilled(ImVec2(tx - 4, ty - 2), ImVec2(tx + ns.x + 4, ty + ns.y + 2), IM_COL32(0, 0, 0, 140), 4.0f);
    dl->AddText(font, bs, ImVec2(tx, ty), IM_COL32(255, 255, 255, 230), name);
}

/* the title screen: what a co-op session started from F3 waits for (no hint without one: the menu has MULTIPLAYER) */
static const char* lobby_hint(void)
{
    if (!cs.at_title || coop_visible || cs.lobby) return nullptr;
    switch (cs.state) {
    case COOP_OFF: return nullptr;
    case COOP_HOSTING: return "Co-op: hosting. Start or load a game. F3: co-op settings.";
    case COOP_CONNECTED: return "Co-op: connected. The host starts or loads the game. F3: co-op settings.";
    default: return "Co-op: waiting for the host. F3: co-op settings.";
    }
}

/* F9: imports from other games' discs (pc/src/import, docs/IMPORT_OUTBREAK.md). A section per
   source: its disc image, what to extract, the extraction's progress and what was extracted
   (from the source's manifest, so it shows after a restart too). */
static char picked[1024];
static SDL_AtomicInt picked_src = { -1 };
static void SDLCALL import_picked(void* ud, const char* const* files, int filter)
{
    (void)filter;
    if (files && files[0]) { SDL_strlcpy(picked, files[0], sizeof(picked)); SDL_SetAtomicInt(&picked_src, (int)(intptr_t)ud); }
}

static void import_window(void)
{
    static ImportItem items[IMPORT_SOURCES][1024];
    static int n[IMPORT_SOURCES], loaded, was_running, what[IMPORT_SOURCES] = { IMPORT_PLAYERS };
    static char when[IMPORT_SOURCES][64];
    ImportProgress* pg = &g_import_progress;
    int running = __atomic_load_n(&pg->running, __ATOMIC_ACQUIRE);
    if (!loaded || (was_running && !running)) {   /* the manifests: at the first opening and after an extraction */
        for (int s = 0; s < IMPORT_SOURCES; s++) n[s] = import_results(s, items[s], 1024, when[s], sizeof(when[s]));
        if (loaded) import_registry_load();   /* new player models for "You appear as" */
        loaded = 1;
    }
    was_running = running;
    int ps = SDL_GetAtomicInt(&picked_src);
    if (ps >= 0) {   /* a disc image chosen in the file selector */
        SDL_strlcpy(g_import.iso[ps], picked, sizeof(g_import.iso[ps]));
        SDL_SetAtomicInt(&picked_src, -1);
        settings_save_later();
    }
    ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(820, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Import (F9)", &import_visible)) { ImGui::End(); return; }
    ImGui::TextWrapped("Import character models from your own disc image. Imported models appear under Character Model.");
    for (int s = 0; s < IMPORT_SOURCES; s++) {
        const ImportSource* src = &import_sources[s];
        char label[128], dir[1100];
        SDL_snprintf(label, sizeof(label), "%s (%s)", src->title, src->serial);
        if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        ImGui::PushID(s);
        ImGui::TextWrapped("Disc image: %s", g_import.iso[s][0] ? g_import.iso[s] : "(none chosen)");
        ImGui::BeginDisabled(running != 0);
        if (ImGui::Button("Choose Disc Image...")) {
            static const SDL_DialogFileFilter filters[] = { { "PS2 disc image (*.iso)", "iso" }, { "All files", "*" } };
            SDL_ShowOpenFileDialog(import_picked, (void*)(intptr_t)s, win, filters, 2, g_import.iso[s][0] ? g_import.iso[s] : NULL, false);
        }
        import_source_dir(s, dir, sizeof(dir));
        ImGui::TextDisabled("Output: %s", dir);
        {   /* (only the groups offered, IMPORT_OFFERED) */
            static const struct { const char* label; int bit; } groups[] = {
                { "Playable Characters", IMPORT_PLAYERS }, { "Cutscene Models", IMPORT_CUTSCENE }, { "Other Characters", IMPORT_NPCS }, { "Enemies", IMPORT_ENEMIES } };
            int first = 1;
            what[s] &= IMPORT_OFFERED;
            for (const auto& g : groups) {
                if (!(IMPORT_OFFERED & g.bit)) continue;
                if (!first) ImGui::SameLine();
                ImGui::CheckboxFlags(g.label, &what[s], g.bit);
                first = 0;
            }
        }
        ImGui::BeginDisabled(!g_import.iso[s][0] || !what[s]);
        if (ImGui::Button("Import Characters")) import_start(s, what[s]);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (running || pg->total) {
            char ov[64];
            SDL_snprintf(ov, sizeof(ov), "%d / %d", pg->done, pg->total);
            ImGui::ProgressBar(pg->total ? (float)pg->done / pg->total : 0.0f, ImVec2(-1, 0), ov);
            ImGui::TextWrapped("%s", pg->phase);
        }
        int ok = 0, players = 0, failed = 0;
        for (int i = 0; i < n[s]; i++) {
            const ImportItem* x = &items[s][i];
            if (x->group < 0 || !(IMPORT_OFFERED & (1 << x->group))) continue;
            ok += x->ok; players += x->player; failed += !x->ok;
        }
        if (!n[s]) ImGui::TextDisabled("Nothing extracted yet.");
        else {
            ImGui::Text("Extracted %s: %d models, %d usable as player models, %d failed", when[s], ok, players, failed);
            static const char* const gname[IMPORT_GROUPS] = { "Playable Characters", "Cutscene Models", "Other Characters", "Enemies" };
            for (int g = 0; g < IMPORT_GROUPS; g++) {
                if (!(IMPORT_OFFERED & (1 << g))) continue;   /* (an older run's other groups: not listed) */
                int gn = 0, gp = 0;
                for (int i = 0; i < n[s]; i++) if (items[s][i].group == g) { gn++; gp += items[s][i].player != 0; }
                if (!gn) continue;
                char gl[96];
                SDL_snprintf(gl, sizeof(gl), "%s: %d, %d player models###g%d", gname[g], gn, gp, g);
                if (!ImGui::TreeNodeEx(gl, g == IMPORT_GROUP_PLAYERS ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
                if (ImGui::BeginTable("##items", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV,
                                      ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 15))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Character"); ImGui::TableSetupColumn("Variant"); ImGui::TableSetupColumn("File");
                    ImGui::TableSetupColumn("Meshes"); ImGui::TableSetupColumn("Vertices"); ImGui::TableSetupColumn("Triangles");
                    ImGui::TableSetupColumn("Textures"); ImGui::TableSetupColumn("Player Model");
                    ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (int i = 0; i < n[s]; i++) {
                        const ImportItem* x = &items[s][i];
                        if (x->group != g) continue;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(x->name);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(x->variant);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(x->id);
                        ImGui::TableNextColumn(); ImGui::Text("%d", x->meshes);
                        ImGui::TableNextColumn(); ImGui::Text("%d", x->verts);
                        ImGui::TableNextColumn(); ImGui::Text("%d", x->tris);
                        ImGui::TableNextColumn(); ImGui::Text("%d", x->textures);
                        ImGui::TableNextColumn();
                        if (!x->ok) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "failed");
                        else if (x->player) ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "yes");
                        else ImGui::TextDisabled(x->female < 0 ? "glTF only" : "no");
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(x->note[0] ? x->note : x->ok ? "glTF + PNG" : "");
                        if (x->note[0] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", x->note);
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void overlay_render(void)
{
    if (!ready) return;
    coop_status_get(&cs);
    bool windows = visible || cheats_visible || coop_visible || import_visible;
    pad_set_game_input_enabled(!windows);
    tps_update_mouse_mode(win, windows);
    menu_mouse_update(win, windows);
    /* without a window ImGui leaves the cursor alone (menu_mouse_update shows/hides it) */
    if (windows) ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    else ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    bool crosshair = g_tps.on && g_tps.crosshair && g_tps.cam_active && g_tps.aiming;
    const char* hint = lobby_hint();
    bool tag = coop_on && tag_flags && !cs.trade_open && (g_coop.nametag || cs.busy_kind || cs.trade_hint[0]);   /* co-op: the partner is drawn in the frame shown */
    bool ping = g_config.show_ping && coop_on && cs.state == COOP_CONNECTED;   /* co-op: next to the frame rate */
    if (!visible && !cheats_visible && !coop_visible && !import_visible && !g_config.show_fps && !ping && !crosshair && !hint && !tag) return;
    {   /* readable on large displays: scale text with the window height (1080p = 1x) */
        int ww, wh;
        SDL_GetWindowSizeInPixels(win, &ww, &wh);
        ImGui::GetStyle().FontScaleDpi = wh > 1080 ? wh / 1080.0f : 1.0f;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (visible) settings_window();
    if (cheats_visible) cheat_window();
    if (coop_visible) coop_window();
    if (import_visible) import_window();
    if (crosshair) {   /* third person: the screen centre is where the shot goes (tps_glue.c) */
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 c(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        float s = ImGui::GetIO().DisplaySize.y / 120.0f, g = s * 0.6f, t = s > 8 ? 2.0f : 1.5f;
        ImU32 col = IM_COL32(255, 60, 40, 220), shadow = IM_COL32(0, 0, 0, 160);
        for (int pass = 0; pass < 2; pass++) {
            ImU32 k = pass ? col : shadow;
            float o = pass ? 0.0f : 1.0f;
            dl->AddLine(ImVec2(c.x - g - s + o, c.y + o), ImVec2(c.x - g + o, c.y + o), k, t);
            dl->AddLine(ImVec2(c.x + g + o, c.y + o), ImVec2(c.x + g + s + o, c.y + o), k, t);
            dl->AddLine(ImVec2(c.x + o, c.y - g - s + o), ImVec2(c.x + o, c.y - g + o), k, t);
            dl->AddLine(ImVec2(c.x + o, c.y + g + o), ImVec2(c.x + o, c.y + g + s + o), k, t);
            dl->AddCircleFilled(ImVec2(c.x + o, c.y + o), t, k);
        }
    }
    if (tag) name_tag();
    if (hint) {
        ImGui::SetNextWindowPos(ImVec2(10, ImGui::GetIO().DisplaySize.y - 10), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin("##coophint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::TextUnformatted(hint);
        ImGui::End();
    }
    if (g_config.show_fps || ping) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin("##fps", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        if (g_config.show_fps && ping) ImGui::Text("%.1f fps   ping %d ms", ImGui::GetIO().Framerate, cs.ping_ms);
        else if (ping) ImGui::Text("ping %d ms", cs.ping_ms);
        else ImGui::Text("%.1f fps", ImGui::GetIO().Framerate);
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
