/*
 * F1 overlay (Dear ImGui): display, audio and port settings. While it is open the game
 * keeps running but receives no pad input. Settings are applied immediately and saved
 * to <save_dir>/recvx.ini.
 */
#include <stdarg.h>
#include "../ps2_platform.h"
extern "C" {
#include "../audio/audio.h"
#include "../render/gl_loader.h"   /* the HUD's icon texture */
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
#include "lang.h"
#include "devcine.h"
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
extern "C" void placer_poll(int f2_open), placer_overlay(void);   /* --dev object placer (placer_ui.cpp, English only) */

static SDL_Window* win;
static bool ready, visible, cheats_visible, coop_visible, import_visible, devcine_visible;
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
    const char* show = SDL_getenv("RECVX_SHOW_OVERLAY");   /* debugging aid (screenshots): 1 F1, 2 the cheats (F2), 3 the co-op window, 4 import (F9), 5 the --dev cutscene log (F6) */
    visible = show && strcmp(show, "3") && strcmp(show, "2") && strcmp(show, "4") && strcmp(show, "5");
    devcine_visible = show && !strcmp(show, "5");
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
    for (int r = B_CHEAT_SAVE; r <= B_CHEAT_LOAD; r++)   /* (unbound by default; not while typing in a field) */
        if (!ImGui::GetIO().WantTextInput && bind_hotkey(ev, r)) {
            __atomic_or_fetch(&g_cheat.request, r == B_CHEAT_SAVE ? CHEAT_SAVE : r == B_CHEAT_BOX ? CHEAT_BOX : CHEAT_LOAD, __ATOMIC_ACQ_REL);
            return 1;
        }
    if (g_dev_mode && bind_hotkey(ev, B_IMPORT)) {
        import_visible = !import_visible;
        return 1;
    }
    if (g_dev_tools && bind_hotkey(ev, B_DEVCINE)) {
        devcine_visible = !devcine_visible;
        return 1;
    }
    ImGui_ImplSDL3_ProcessEvent(ev);
    if (!visible && !cheats_visible && !coop_visible && !import_visible && !devcine_visible) return 0;
    return (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP || ev->type == SDL_EVENT_TEXT_INPUT ||
            ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP || ev->type == SDL_EVENT_MOUSE_WHEEL);
}

/* our texts here are right to left (Arabic, with Translate Overlay Windows on) */
static bool ov_rtl(void) { return g_tr_overlay && lang_rtl_text(); }

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

/* the cutscene cast's "Custom" models under Model Selection, or (list 2) its enemies under Enemy Models (cast.h; the lobby
   and the BIORAND screen list the same) */
static bool cast_custom_imgui(int list)
{
    static short pos[CAST_CUSTOM_MAX];
    static int mdl[CAST_CUSTOM_MAX];
    unsigned char* off = list ? g_enemy_opts.cast_enemy_off : cast_off;
    int n = cast_custom_list(list, !list && g_enemy_opts.cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0, pos, mdl, CAST_CUSTOM_MAX), on = 0, set = -1;
    bool changed = false;
    char head[96];
    for (int k = 0; k < n; k++) on += !cast_off_at(off, pos[k]);
    SDL_snprintf(head, sizeof head, list ? tr("Custom Enemies (%d of %d checked)###castenemies") : tr("Custom Models (%d of %d checked)###castcustom"), on, n);
    if (!ImGui::TreeNodeEx(head, ImGuiTreeNodeFlags_DefaultOpen)) return false;
    ImGui::PushID(list);
    if (ImGui::Button(tr("Select All"))) set = 0;
    ImGui::SameLine();
    if (ImGui::Button(tr("Deselect All"))) set = 1;
    if (set >= 0) { for (int k = 0; k < n; k++) cast_off_put(off, pos[k], set); changed = true; }
    if (!on) { ImGui::SameLine(); ImGui::TextDisabled("%s", list ? tr("None checked: the monsters keep their own looks.") : tr("None checked: the characters keep their own looks.")); }
    if (ImGui::BeginChild("##castcustom", ImVec2(0, ImGui::GetFrameHeightWithSpacing() * (n < 12 ? n + 1 : 12)), ImGuiChildFlags_Borders)) {
        const char* grp = nullptr;
        for (int k = 0; k < n; k++) {
            const char* g = cast_entry_group(mdl[k]);
            char label[128];
            bool b = !cast_off_at(off, pos[k]);
            if (!grp || strcmp(g, grp)) ImGui::SeparatorText(g);
            grp = g;
            SDL_snprintf(label, sizeof label, "%s##cc%d", cast_entry_name(mdl[k]), k);
            if (ImGui::Checkbox(label, &b)) { cast_off_put(off, pos[k], !b); changed = true; }
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::TreePop();
    if (changed) settings_save_later();
    return changed;
}

/* the enemy randomizer's "Custom" rates under Enemy Selection (enemies.h; the lobby and the BIORAND screen list the same) */
static bool enemy_rates_imgui()
{
    bool changed = false;
    char head[96];
    SDL_snprintf(head, sizeof head, "%s###enemyrates", tr("Enemy Rates"));
    if (!ImGui::TreeNodeEx(head, ImGuiTreeNodeFlags_DefaultOpen)) return false;
    for (int k = 0; k < enemy_rate_count(); k++) {
        int i = enemy_rate_at(k), v = g_enemy_opts.rate[i];
        char label[96];
        SDL_snprintf(label, sizeof label, "%s##er%d", tr(enemy_rate_name(i)), k);
        if (ImGui::SliderInt(label, &v, 0, ENEMY_RATE_MAX, v ? "%d" : tr("Off"))) { g_enemy_opts.rate[i] = (unsigned char)v; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("How often this enemy is chosen, against the others. Note: Some rooms can only contain certain enemies."));
    }
    ImGui::TreePop();
    if (changed) settings_save_later();
    return changed;
}

/* one settings table row (F1's pages; F3's host rules) */
extern "C" void pc_biorand_appear_drop(void);   /* biorand.h: the user chose a look in game, this game's BioRand picks go */
static bool setting_row(const Setting* s)
{
    bool changed = false;
    int i = (int)(s - g_settings);
    const char* lock = settings_locked(s);
    char label[256], val[96];   /* (Arabic: 3 bytes a letter) */
    int v = settings_get(s), hi = settings_hi(s), st = s->step > 0 ? s->step : 1;
    if (ov_rtl())   /* (right to left: the note on the label's left) */
        SDL_snprintf(label, sizeof label, "%s%s##set%d", (s->flags & SET_RESTART) ? tr(" (restart needed)") : "", tr(s->label), i);
    else SDL_snprintf(label, sizeof label, "%s%s##set%d", tr(s->label), (s->flags & SET_RESTART) ? tr(" (restart needed)") : "", i);
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
                if (ImGui::Selectable(s->name ? s->name(k, nm, sizeof nm) : tr(s->names[k - s->lo]), k == v)) { settings_set(s, k); changed = true; }
                if (k == v) ImGui::SetItemDefaultFocus();   /* the list opens at the current value (a long one: "You appear as") */
            }
            ImGui::EndCombo();
        }
        if (s->val == &g_enemy_opts.cast_all && v == 2) {   /* "Custom": its list below (its tooltip stays the row's) */
            bool hover = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            if (hover && s->help) ImGui::SetTooltip("%s", tr(s->help));
            ImGui::EndDisabled();
            return cast_custom_imgui(0) | changed;
        }
        if (s->val == &g_enemy_opts.cast_enemy && v == CAST_ENEMY_CUSTOM) {   /* ... and "Enemy Models"' */
            bool hover = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            if (hover && s->help) ImGui::SetTooltip("%s", tr(s->help));
            ImGui::EndDisabled();
            return cast_custom_imgui(2) | changed;
        }
        if (s->val == &g_enemy_opts.variety && v == 2) {   /* "Custom": the rates below */
            bool hover = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            if (hover && s->help) ImGui::SetTooltip("%s", tr(s->help));
            ImGui::EndDisabled();
            return enemy_rates_imgui() | changed;
        }
    } else {   /* a number; a value name (tenths) is shown as the slider's text */
        char fmt[96];
        const char* f = s->fmt ? tr(s->fmt) : "%d";
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
        ImGui::Text("%s%s%s", lock ? lock : "", lock && s->help ? "\n" : "", s->help ? tr(s->help) : "");
        if (s->flags & SET_CALIB) calib_pattern();
        ImGui::EndTooltip();
    }
    return changed;
}

static bool settings_imgui(int page, bool all = true)
{
    bool changed = false;
    const char* group = nullptr;   /* a heading on a hidden row: over the next shown one */
    for (int i = 0; i < g_settings_n; i++) {
        const Setting* s = &g_settings[i];
        if (s->page != page || !(all || (s->flags & SET_F1))) continue;
        if (s->group) group = s->group;
        if (s->shown && !s->shown()) continue;
        if (group && all) ImGui::SeparatorText(tr(group));
        group = nullptr;
        changed |= setting_row(s);
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
                if (bind_swapped()) SDL_snprintf(msg, sizeof msg, tr("Swapped with %s"), bind_swapped());
                else if (bind_combo(cap_row, dev, cap_slot)) SDL_snprintf(msg, sizeof msg, tr("Hold Aim and press this button to reload."));
                else msg[0] = 0;
                settings_save_later();
            }
            cap_row = -1;
        }
    }
    settings_imgui(SP_CONTROLS);   /* input source, which gamepad */
    if (ImGui::BeginTabBar("##dev")) {
        if (ImGui::BeginTabItem(tr("Keyboard and Mouse"))) { if (dev != BIND_KB) cap_row = -1; dev = BIND_KB; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(tr("Gamepad"))) { if (dev != BIND_PAD) cap_row = -1; dev = BIND_PAD; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    char pad_name[96];
    if (dev == BIND_PAD) ImGui::TextDisabled(tr("%s. The left stick also moves your character.\n"
                                             "Modern third person uses its own bindings below."), bind_pad_desc(pad_name, sizeof pad_name));
    ImGui::TextDisabled(tr("Click a binding, then press an input. Esc: cancel. Delete or right-click: clear.\n"
                        "Reload can share a button: hold Aim and press it to reload."));
    if (dev == BIND_KB) ImGui::TextDisabled(tr("Mouse buttons work on any row, also with the fixed cameras."));
    if (msg[0]) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", msg);
    if (ImGui::BeginTable("##binds", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Action"), ImGuiTableColumnFlags_WidthStretch, 2.2f);
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
                if (group == BG_GAME) ImGui::SeparatorText(tr("Game Controls (Control Type)"));
                else ImGui::SeparatorText(tr(group == BG_TPS ? (dev ? "Third Person: Modern Gamepad" : "Third Person: Keyboard and Mouse") : "Hotkeys"));
                if (group == BG_GAME) { ImGui::TableSetColumnIndex(1); ImGui::TextDisabled(tr("type %c"), 'A' + pc_keytype()); }
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            int conflict = bind_conflict(r, dev);
            if (conflict) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", bind_row_label(r, label, sizeof label));
            else ImGui::TextUnformatted(bind_row_label(r, label, sizeof label));
            if (conflict && ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Also bound to: %s"), bind_rows[conflict - 1].name);
            for (int k = 0; k < BIND_SLOTS; k++) {
                ImGui::TableSetColumnIndex(1 + k);
                char b[96];
                if (cap_row == r && cap_slot == k) SDL_snprintf(b, sizeof b, tr("press... %ds##%d_%d"), (bind_capture_left_ms() + 999) / 1000, r, k);
                else SDL_snprintf(b, sizeof b, "%s%s##%d_%d", bind_combo(r, dev, k) ? tr("Aim + ") : "", bind_code_name(dev, bind_get(r, dev, k), nm, sizeof nm), r, k);
                if (ImGui::Button(b, ImVec2(-FLT_MIN, 0))) { cap_row = r; cap_slot = k; msg[0] = 0; bind_capture_start(dev); }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { bind_set(r, dev, k, 0); settings_save_later(); }
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(tr(dev ? "Reset Gamepad Bindings" : "Reset Keyboard Bindings"))) { bind_reset(dev); settings_save_later(); msg[0] = 0; }
    ImGui::TextDisabled(tr("Also available in OPTION > ENHANCED SETTINGS > CONTROLS."));
}

static void settings_window(void)
{
    bool changed = false;
    int ww, wh;
    SDL_GetWindowSizeInPixels(win, &ww, &wh);

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    char title[160];
    if (ov_rtl()) SDL_snprintf(title, sizeof title, RECVX_VERSION "%s###f1", tr("Resident Evil Code: Veronica X  -  settings (F1)  -  "));
    else SDL_snprintf(title, sizeof title, "%s" RECVX_VERSION "###f1", tr("Resident Evil Code: Veronica X  -  settings (F1)  -  "));
    if (!ImGui::Begin(title, &visible)) { ImGui::End(); return; }

    if (ImGui::CollapsingHeader(tr("Display"), ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= settings_imgui(SP_DISPLAY);
        ImGui::TextDisabled(tr("Resolution: %d x %d | %.1f FPS"), ww, wh, ImGui::GetIO().Framerate);
    }
    if (ImGui::CollapsingHeader(tr("Gameplay"), ImGuiTreeNodeFlags_DefaultOpen)) changed |= settings_imgui(SP_GAMEPLAY);
    if (settings_page_shown(SP_LANG) && ImGui::CollapsingHeader(tr("Language"), ImGuiTreeNodeFlags_DefaultOpen)) changed |= settings_imgui(SP_LANG);
    if (ImGui::CollapsingHeader(tr("Controls"))) controls_section();
    if (ImGui::CollapsingHeader(tr("Third Person"), ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= settings_imgui(SP_TPS);
        char key[96];
        SDL_snprintf(key, sizeof key, tr(tps_key_wait ? "Press a key (Esc cancels)" : "Camera Key: %s"), SDL_GetScancodeName((SDL_Scancode)g_tps.key));
        if (ImGui::Button(key)) tps_key_wait = true;
        ImGui::TextDisabled("%s", tr(g_tps.free_cam
                            ? "Move in the direction shown on screen; use the mouse to turn the camera.\n"
                              "Change action buttons under Controls; F1 releases the mouse."
                            : "Move forward or back and turn with your movement keys; use the mouse to look.\n"
                              "Change action buttons under Controls; F1 releases the mouse."));
        if (g_tps.pad_modern)
            ImGui::TextDisabled(tr("Left stick: move and run. Right stick: look and aim.\n"
                                "Set aiming, action, inventory and camera buttons under Controls."));
    }
    if (ImGui::CollapsingHeader(tr("Audio"), ImGuiTreeNodeFlags_DefaultOpen)) changed |= settings_imgui(SP_AUDIO);
    if (ImGui::CollapsingHeader(tr("Appearance"), ImGuiTreeNodeFlags_DefaultOpen)) changed |= settings_imgui(SP_APPEAR);
    for (int p = SP_COOP; p <= SP_EXTRAS; p += SP_EXTRAS - SP_COOP)   /* pages with their own windows (F3, F2): the SET_F1 rows */
        if (settings_imgui_any(p) && ImGui::CollapsingHeader(tr(p == SP_COOP ? "Co-op" : "Extras"), ImGuiTreeNodeFlags_DefaultOpen))
            changed |= settings_imgui(p, false);

    if (ImGui::CollapsingHeader(tr("Performance"))) {
        static ProfFrame h[240];
        bool on = prof_enabled != 0;
        if (ImGui::Checkbox(tr("Measure (small overhead; always on with --profile)"), &on)) prof_enabled = on;
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
            ImGui::Text(tr("game frame %.2f ms avg, %.2f ms worst (target 33.37 ms = 29.97 Hz)"), avg_total, worst);
            ImGui::PlotLines("##ft", total, n, 0, tr("frame time (ms)"), 0.0f, 66.7f, ImVec2(-1, 60));
            for (int c = 0; c < PROF_COUNT; c++) {
                ImGui::Text(tr("%-8s %6.2f ms"), prof_names[c], avg[c]);
                ImGui::SameLine(160);
                ImGui::ProgressBar(avg_total > 0 ? avg[c] / avg_total : 0, ImVec2(-1, 0), "");
            }
            ImGui::TextDisabled(tr("'wait' is idle time; run with --profile for a CSV."));
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
        if (ImGui::BeginCombo(tr("Location"), tp_stage_name(c->tp_stg), ImGuiComboFlags_HeightLarge)) {
            for (int s = 0; s < 10; s++) {
                int first = tp_stage_first(s, &k);
                if (k && ImGui::Selectable(tp_stage_name(s), s == c->tp_stg)) { c->tp_stg = s; c->tp_rom = tp_room(first)->rom; c->tp_pos = 0; }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo(tr("Room"), cur >= 0 ? tp_room_name(cur, 1, b, sizeof b) : "-", ImGuiComboFlags_HeightLarge)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##filter", tr("type to search every location"), filter, sizeof filter);
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
            if (ImGui::BeginCombo(tr("Entry"), tp_entry_name(cur, c->tp_pos, b, sizeof b), ImGuiComboFlags_HeightLarge)) {
                for (int p = 0; p < m; p++) {
                    snprintf(e, sizeof e, "%s##%d", tp_entry_name(cur, p, b, sizeof b), p);
                    if (ImGui::Selectable(e, p == c->tp_pos)) c->tp_pos = p;
                    if (ImGui::IsItemHovered() && tp_entry_sources(cur, p, b, sizeof b)[0]) ImGui::SetTooltip(tr("Doors here from:\n%s"), b);
                }
                ImGui::EndCombo();
            }
        }
        ImGui::TextDisabled(tr("A room may not work in every story state."));
    } else {
        if (!n) ImGui::TextDisabled("%s", tr(tp_reading() ? "Loading room list..." : "The room list could not be read."));
        ImGui::TextDisabled(tr("Some destinations may crash the game."));
        ImGui::InputInt(tr("Stage"), &c->tp_stg);
        ImGui::InputInt(tr("Room"), &c->tp_rom);
        ImGui::InputInt(tr("Position"), &c->tp_pos);
        c->tp_stg = SDL_clamp(c->tp_stg, 0, 9); c->tp_rom = SDL_clamp(c->tp_rom, 0, 99); c->tp_pos = SDL_clamp(c->tp_pos, 0, TP_POS - 1);
    }
    ImGui::Checkbox(tr("Show Room Numbers"), &raw);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Stage, room and entry position as numbers (as RECVX_TELEPORT), also rooms not in the list."));
    ImGui::SameLine();
}

static void cheat_window(void)
{
    CheatState* c = &g_cheat;
    ImGui::SetNextWindowPos(ImVec2(500, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr("Cheats (F2)"), &cheats_visible)) { ImGui::End(); return; }
    ImGui::Text(tr("%s  HP %d   stage %d  room %d  pos %d"), c->player ? "Chris" : "Claire", c->hp, c->stg, c->rom, c->pos);
    bool b = c->inf_health;
    if (ImGui::Checkbox(tr("Infinite Health"), &b)) c->inf_health = b;
    ImGui::SameLine();
    if (ImGui::Button(tr("Heal Now"))) __atomic_or_fetch(&c->request, CHEAT_HEAL, __ATOMIC_ACQ_REL);
    b = c->inf_ammo;
    if (ImGui::Checkbox(tr("Infinite Ammo"), &b)) c->inf_ammo = b;
    {
        static const char* modes[] = { "Off", "Walls", "Walls and Flight" };
        int m = c->noclip;
        if (ImGui::Combo(tr("Pass Through Walls"), &m, [](void* d, int i) -> const char* { return tr(((const char* const*)d)[i]); }, (void*)modes, IM_ARRAYSIZE(modes))) c->noclip = m;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("Move through walls, objects and enemies; doors still work.\n"
                              "Flight: Page Up/R2 up, F/Page Down/L2 down;\n"
                              "turn this off to return to the floor below."));
    }
    b = c->unlock;
    if (ImGui::Checkbox(tr("Unlock With No Key"), &b)) c->unlock = b;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Open key locks without carrying the key;\n"
                          "no items are added and story locks stay.\n"
                          "Supported locks update in the current room."));

    if (ImGui::Button(tr("Save Now"))) __atomic_or_fetch(&c->request, CHEAT_SAVE, __ATOMIC_ACQ_REL);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Open the typewriter's save screen here. No ink ribbon needed."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Open Item Box"))) __atomic_or_fetch(&c->request, CHEAT_BOX, __ATOMIC_ACQ_REL);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Open the item box here, the same box as at any item box."));
    ImGui::SameLine();
    ImGui::BeginDisabled(g_coop.state != COOP_OFF);
    if (ImGui::Button(tr("Load Game"))) __atomic_or_fetch(&c->request, CHEAT_LOAD, __ATOMIC_ACQ_REL);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", tr(g_coop.state != COOP_OFF ? "Not during a co-op session." : "Open the load screen here. Back returns to the game as it was."));

    ImGui::SeparatorText(tr("Add Item"));
    char label[48];
    snprintf(label, sizeof label, "%3d  %s", c->give_id, c->give_id > 0 && c->give_id < CHEAT_ITEMS ? c->names[c->give_id] : "-");
    if (ImGui::BeginCombo(tr("Item"), label, ImGuiComboFlags_HeightLarge)) {
        for (int id = 1; id < CHEAT_ITEMS; id++) {
            if (!c->names[id][0]) continue;
            snprintf(label, sizeof label, "%3d  %s", id, c->names[id]);
            if (ImGui::Selectable(label, id == c->give_id)) c->give_id = id;
        }
        ImGui::EndCombo();
    }
    ImGui::InputInt(tr("Quantity"), &c->give_count);
    c->give_count = SDL_clamp(c->give_count, 1, 999);
    if (ImGui::Button(tr("Add to Inventory")) && c->give_id) __atomic_or_fetch(&c->request, CHEAT_GIVE, __ATOMIC_ACQ_REL);

    ImGui::SeparatorText(tr("Inventory"));
    for (int i = 1; i < 2 + c->slots && i < 12; i++) {
        unsigned id = c->inv[i] >> 16 & 0xFF;
        char nm[16];
        if (!id) continue;
        if (id >= CHEAT_ITEMS || !c->names[id][0]) SDL_snprintf(nm, sizeof nm, tr("item %u"), id);
        ImGui::PushID(i);
        if (ImGui::SmallButton("X")) { c->drop_slot = i; __atomic_or_fetch(&c->request, CHEAT_DROP, __ATOMIC_ACQ_REL); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Remove from the inventory"));
        ImGui::PopID();
        ImGui::SameLine();
        ImGui::Text("%s %-24s %u", (unsigned)i == c->inv[0] ? tr("equipped") : "        ", id < CHEAT_ITEMS && c->names[id][0] ? c->names[id] : nm, c->inv[i] & 0xFFFF);
    }

    ImGui::SeparatorText(tr("Spawn Enemy"));
    if (c->spawn_idx < 0 || c->spawn_idx >= spawn_count()) c->spawn_idx = 0;
    if (ImGui::BeginCombo(tr("Enemy"), spawn_name(c->spawn_idx), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < spawn_count(); i++)
            if (ImGui::Selectable(spawn_name(i), i == c->spawn_idx)) c->spawn_idx = i;
        ImGui::EndCombo();
    }
    if (ImGui::Button(tr("Spawn Ahead"))) __atomic_or_fetch(&c->request, CHEAT_SPAWN, __ATOMIC_ACQ_REL);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Spawned enemies disappear when you leave; kills do not count.\n"
                          "Bosses and scripted enemies may fail\n"
                          "outside their own rooms."));

    ImGui::SeparatorText(tr("Teleport"));
    teleport_pick(c);
    if (ImGui::Button(tr("Use Current Room"))) { c->tp_stg = c->stg; c->tp_rom = c->rom; c->tp_pos = c->pos; }
    ImGui::SameLine();
    if (ImGui::Button(tr("Go"))) __atomic_or_fetch(&c->request, CHEAT_TELEPORT, __ATOMIC_ACQ_REL);
    if (c->result) ImGui::TextDisabled("%s", tr(c->result));
    ImGui::End();
}

/* co-op trade (docs/COOP.md "Trading from the F3 window"): offer an item to the partner standing next to you */
static const char* coop_item(unsigned int e, char* buf, size_t n)
{
    unsigned id = (e >> 16) & 0xFF;
    if (id < CHEAT_ITEMS && g_cheat.names[id][0]) snprintf(buf, n, "%s x%u", g_cheat.names[id], e & 0xFFFF);
    else snprintf(buf, n, tr("item %u x%u"), id, e & 0xFFFF);
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
    ImGui::SeparatorText(tr("Trade"));
    if (cs.trade_in) {
        ImGui::Text(tr("Partner offers: %s"), coop_item(cs.trade_in, b, sizeof(b)));
        ImGui::BeginDisabled(!cs.trade_near);
        if (ImGui::Button(tr("Accept"))) __atomic_store_n(&c->trade_req, COOP_TRADE_ACCEPT, __ATOMIC_RELEASE);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(tr("Decline"))) __atomic_store_n(&c->trade_req, COOP_TRADE_DECLINE, __ATOMIC_RELEASE);
    }
    if (cs.trade_out) {
        ImGui::Text(tr("Offering: %s (waiting for your partner)"), coop_item(cs.trade_out, b, sizeof(b)));
        if (ImGui::Button(tr("Withdraw Offer"))) __atomic_store_n(&c->trade_req, COOP_TRADE_CANCEL, __ATOMIC_RELEASE);
    } else if (!cs.trade_near) {
        ImGui::TextDisabled(tr("Stand next to your partner to offer items."));
    } else {
        int slots = cs.trade_slots < 8 ? 8 : cs.trade_slots > 10 ? 10 : cs.trade_slots;
        if (c->trade_slot < 2 || c->trade_slot >= 2 + slots || !(cs.trade_inv[c->trade_slot] >> 16)) c->trade_slot = 2;
        if (ImGui::BeginCombo(tr("Item"), (cs.trade_inv[c->trade_slot] >> 16) ? coop_item(cs.trade_inv[c->trade_slot], b, sizeof(b)) : "-")) {
            for (int s = 2; s < 2 + slots; s++) {
                if (!(cs.trade_inv[s] >> 16)) continue;
                char lb[80];
                snprintf(lb, sizeof(lb), "%s%s##%d", coop_item(cs.trade_inv[s], b, sizeof(b)), (int)cs.trade_inv[0] == s ? tr(" (equipped)") : "", s);
                if (ImGui::Selectable(lb, c->trade_slot == s)) { c->trade_slot = s; c->trade_count = cs.trade_inv[s] & 0xFFFF; }
            }
            ImGui::EndCombo();
        }
        int have = cs.trade_inv[c->trade_slot] & 0xFFFF;
        if (c->trade_count < 1 || c->trade_count > have) c->trade_count = have;
        if (have > 1) ImGui::SliderInt(tr("Quantity"), &c->trade_count, 1, have);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Choose how much ammo or other stackable items to give. Other items are traded whole."));
        if (ImGui::Button(tr("Offer"))) __atomic_store_n(&c->trade_req, COOP_TRADE_OFFER, __ATOMIC_RELEASE);
    }
    if (cs.trade_msg[0]) ImGui::TextDisabled("%s", cs.trade_msg);
}

/* ImGui::TextWrapped, but an Arabic text (in display order: lang.h lang_shape) wrapped from its right end, where it
   starts (ImGui has no right-to-left layout) */
static float rtl_width(const char* s, int n, void*) { return ImGui::CalcTextSize(s, s + n).x; }
static void wrapped(const char* fmt, ...)
{
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (!lang_rtl(b)) { ImGui::TextWrapped("%s", b); return; }
    float w = ImGui::GetContentRegionAvail().x;
    for (int len = (int)strlen(b); len > 0;) {
        int cut = lang_rtl_cut(b, len, w, rtl_width, NULL);
        ImGui::TextUnformatted(b + cut, b + len);
        for (len = cut; len > 0 && b[len - 1] == ' '; len--) {}
    }
}


static void coop_window(void)
{
    CoopState* c = &g_coop;
    bool off = cs.state == COOP_OFF, changed = false;
    ImGui::SetNextWindowPos(ImVec2(940, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(tr(cs.at_title ? "Co-op lobby (F3)###coop" : "Co-op (F3)###coop"), &coop_visible)) { ImGui::End(); return; }
    auto tr_item = [](void* d, int i) -> const char* { return tr(((const char* const*)d)[i]); };   /* a list's names in the language */
    static const char* loots[] = { "Shared loot", "Individual loot" };
    ImGui::BeginDisabled(!off);
    static const char* conns[] = { "Automatic", "Direct", "Server Relay" };
    if (ImGui::Combo(tr("Connection"), &c->conn, tr_item, (void*)conns, IM_ARRAYSIZE(conns))) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Automatic tries direct first, then server;\n"
                          "addresses connect directly, and room codes need a co-op server.\n"
                          "Without a server, Automatic works like Direct;\n"
                          "Server Relay always uses the server."));
    /* Hide IP addresses: the join field as a password, "Show" reveals it until the window closes (the server: public, never) */
    static bool show_addr;
    if (!coop_visible || !c->hide_ip) show_addr = false;
    ImGuiInputTextFlags addr_flags = c->hide_ip && !show_addr ? ImGuiInputTextFlags_Password : 0;
    if (c->conn != COOP_CONN_DIRECT) {   /* first how to connect, then only what it needs (as the lobby) */
        if (ImGui::InputText(tr("Co-op Server"), c->server, sizeof(c->server))) changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(tr("Enter a co-op server address, with an optional port (default: 7141).\n"
                              "Both players use the same server; leave blank to use no server."));
        ImGui::InputText(tr("Room Password"), c->password, sizeof(c->password), ImGuiInputTextFlags_Password);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(tr("Optional, through the server: the host sets it, the joining player types the same. Not saved."));
    }
    if (ImGui::InputInt(tr("Host Port"), &c->port)) { c->port = c->port < 1 ? 1 : c->port > 65535 ? 65535 : c->port; changed = true; }
    bool by_code = coop_join_by_code(c);   /* Via server, or Automatic with a server: a room code (as the lobby's CODE row) */
    if (ImGui::InputText(tr(by_code ? "Room code###join" : "Join address###join"), c->join, sizeof(c->join), addr_flags)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tr(c->conn == COOP_CONN_SERVER ? "Enter the host's room code (XXXX-XXXX); case and dash do not matter.\n"
                                                        "Use the co-op server entered above."
                          : by_code ? "Enter the host's room code to join through the server above.\n"
                                      "A copied join address still connects directly."
                                    : "Paste the host's address from Copy Join Address.\n"
                                      "For a room code, choose Automatic or Server Relay and enter the co-op server."));
    if (c->hide_ip) {   /* also during a session */
        ImGui::EndDisabled();
        ImGui::Checkbox(tr("Show Addresses"), &show_addr);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Hide IP Addresses is on. Select this to reveal the address fields."));
        ImGui::BeginDisabled(!off);
    }
    if (ImGui::Combo(tr("Loot Sharing (Host)"), &c->loot, tr_item, (void*)loots, IM_ARRAYSIZE(loots))) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Shared divides supplies; Individual gives each player a copy.\n"
                          "Keys and story progress go to both;\n"
                          "the host decides."));
    bool important = c->important != 0;
    if (ImGui::Checkbox(tr("Duplicate Weapons and Ribbons"), &important)) { c->important = important; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("With Shared loot, give each player their own weapons and ink ribbons.\n"
                          "The host decides."));
    bool upnp = c->upnp != 0;
    if (ImGui::Checkbox(tr("Automatic Port Forwarding"), &upnp)) { c->upnp = upnp; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Ask your router to allow internet connections while you host.\n"
                          "Port forwarding ends when you stop hosting."));
    ImGui::EndDisabled();
    bool remember = c->remember != 0;   /* also during a session */
    if (ImGui::Checkbox(tr("Remember Partner Items"), &remember)) { c->remember = remember; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Keep a partner's items for their next visit.\n"
                          "After a crash, their items are restored automatically."));
    {   /* the joining player's inventory (as the lobby's ITEMS row) */
        static const char* fixed[] = { "Current Inventory", "Saved with the host", "New inventory" };
        char cur[48], lab[48];
        int v = c->items;
        auto label = [&](int i, char* b, size_t n) {
            if (i < 0) snprintf(b, n, "%s%s", tr(fixed[i + 3]), i == COOP_ITEMS_HOST && cs.state != COOP_OFF && !cs.host && cs.peer_rec >= 0
                                                              ? tr(cs.peer_rec == REC_HAVE ? " (available)" : cs.peer_rec == REC_OFF ? " (host: off)" : " (none)") : "");
            else snprintf(b, n, tr("Save %02d"), i + 1);
        };
        label(v, cur, sizeof(cur));
        ImGui::BeginDisabled(cs.state != COOP_OFF && cs.host);
        static LobbySave* saves;   /* listed when the list opens */
        static int saves_n;
        if (ImGui::BeginCombo(tr("Starting Inventory"), cur)) {
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
            ImGui::SetTooltip(tr("Choose current items, items saved by the host, or items from a save.\n"
                              "Unavailable items fall back to a new inventory;\n"
                              "after a crash, your items are restored automatically."));
    }
    bool tfreeze = c->trade_freeze != 0;   /* also during a session */
    if (ImGui::Checkbox(tr("Pause Enemies During Trades"), &tfreeze)) { c->trade_freeze = tfreeze; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Pause enemies while both players are on the trade screen.\n"
                          "The host decides."));
    bool skipsync = c->skip_sync != 0;   /* also during a session */
    if (ImGui::Checkbox(tr("Skip Cutscenes Together"), &skipsync)) { c->skip_sync = skipsync; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Skip shared cutscenes only after both players choose to skip.\n"
                          "The host decides."));
    bool noribbon = g_config.no_ink_ribbon != 0;   /* the GAMEPLAY setting; in co-op the host's counts */
    if (ImGui::Checkbox(tr("Free Typewriter Saves"), &noribbon)) { g_config.no_ink_ribbon = noribbon; changed = true; }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Save at typewriters without using ink ribbons.\n"
                          "The host decides in co-op."));
    const int* rules[] = { &c->gameover_both, &c->skip_detector, &c->quick_start, &g_config.extra_objects, &g_tps.headshots, &c->shared_enemies };   /* more host rules: their table rows */
    for (const int* v : rules) changed |= setting_row(settings_of(v));
    if (ImGui::InputText(tr("Your Name"), c->name, sizeof(c->name), ImGuiInputTextFlags_CallbackCharFilter,
                         [](ImGuiInputTextCallbackData* d) -> int { return !(d->EventChar >= 32 && d->EventChar < 127 && (isalnum(d->EventChar) || strchr(" -_.!?'()+", d->EventChar))); }))
        changed = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("The name your partner sees overhead and in the lobby."));
    bool tagopt = c->nametag != 0;
    if (ImGui::Checkbox(tr("Partner Name Tag"), &tagopt)) { c->nametag = tagopt; changed = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Show your partner's name overhead, or at the screen edge when nearby.\nTheir menu and cutscene status stays visible either way."));
    bool follow = c->follow != 0;
    ImGui::BeginDisabled(cs.state != COOP_OFF && cs.host);   /* the joining player's choice: the host's own has no effect */
    if (ImGui::Checkbox(tr("Follow Host"), &follow)) { c->follow = follow; changed = true; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(tr("Follow the host through doors.\n"
                          "Off lets you explore separately;\n"
                          "joining, retries and character switches\n"
                          "can still reunite you,\n"
                          "as can long story cutscenes."));
    if (cs.state == COOP_CONNECTED) ImGui::TextDisabled(tr("Session: %s"), tr(loots[cs.loot_now ? 1 : 0]));
    auto appear = [](void*, int i) -> const char* { return tr(appear_name(appear_at(i))); };
    changed |= setting_row(settings_of(&g_appear_per));   /* "Character Models": Same for Both / Per Character */
    for (int k = 0; k < (g_appear_per ? 2 : 1); k++) {   /* "Per Character": Claire's and Chris's */
        static const char* const lbl[3] = { "Character Model", "Claire's Model", "Chris's Model" };
        int pos = appear_pos(appear_choice(c->chr, k));   /* the list leaves out event doubles (actors.c) */
        if (ImGui::Combo(tr(lbl[g_appear_per ? 1 + k : 0]), &pos, appear, nullptr, appear_count(), 20)) {
            (g_appear_per ? g_appear_own[k] : c->chr) = appear_at(pos);
            pc_biorand_appear_drop();   /* (as the table's model rows: this game's BioRand picks go) */
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr("Choose your appearance; your partner sees it too.\nModels can change movement and voice;\nAppearance settings control animations and cutscene models."));
    }
    if (off) {
        /* at the title the window closes, so the pad drives the game's menu again */
        if (ImGui::Button(tr("Host"))) { __atomic_store_n(&c->request, COOP_REQ_HOST, __ATOMIC_RELEASE); coop_visible = !cs.at_title; }
        ImGui::SameLine();
        if (ImGui::Button(tr("Join"))) { __atomic_store_n(&c->request, COOP_REQ_JOIN, __ATOMIC_RELEASE); coop_visible = !cs.at_title; }
    } else {
        if (ImGui::Button(tr("Disconnect"))) __atomic_store_n(&c->request, COOP_REQ_STOP, __ATOMIC_RELEASE);
        if (cs.host && (cs.state == COOP_CONNECTED || cs.state == COOP_LOST)) {
            ImGui::SameLine();
            if (ImGui::Button(tr("Remove Partner"))) __atomic_store_n(&c->request, COOP_REQ_KICK, __ATOMIC_RELEASE);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(tr("Remove your partner and reopen their place in the lobby.\n"
                                  "They can rejoin with the same address or room code."));
        }
    }
    wrapped("%s", cs.status[0] ? coop_shown(cs.status) : tr("Not connected"));
    if (cs.state != COOP_OFF && cs.host && cs.code[0]) ImGui::Text(tr("Join code: %.4s-%.4s"), cs.code, cs.code + 4);
    if (cs.state != COOP_OFF && cs.host && cs.room[0]) {
        char room[16];
        snprintf(room, sizeof(room), "%.4s-%.4s", cs.room, cs.room + 4);
        ImGui::Text(tr("Room code: %s"), room);
        ImGui::SameLine();
        if (ImGui::Button(tr("Copy Room Code"))) SDL_SetClipboardText(room);
    }
    if (cs.state == COOP_CONNECTED && cs.peer_in_game) coop_trade();
    PortmapStatus pm;
    portmap_status(&pm);
    if (pm.local_ip[0] || pm.msg[0]) {   /* hosting */
        char addr[80];
        ImGui::SeparatorText(tr("Internet"));
        ImGui::Text(tr("Local address: %s"), pm.local_ip[0] ? coop_shown(pm.local_ip) : "?");
        if (pm.public_ip[0]) snprintf(addr, sizeof(addr), "%s:%d#%s", pm.public_ip, pm.public_port, cs.code);
        else snprintf(addr, sizeof(addr), "%s:%d#%s", pm.local_ip, c->port, cs.code);
        if (ImGui::Button(tr(pm.public_ip[0] ? "Copy Join Address" : "Copy Local Address"))) SDL_SetClipboardText(addr);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", coop_shown(addr));
        if (!pm.public_ip[0] && pm.state != PM_BUSY && !cs.room[0] && cs.state != COOP_CONNECTED)   /* (the router's status: the log only) */
            ImGui::TextDisabled(tr("If your partner can't connect, use a co-op server (Automatic)."));
    }
    if (cs.at_title)
        ImGui::TextDisabled(tr("Host: start or load a game from the title screen.\n"
                            "Partner: wait at the title screen to join the host's game."));
    ImGui::End();
    if (changed) settings_save_later();
}

/* the game's picture in the window as gs_gl.c presents it: its top left x0, y0 and size vw, vh; returns its widening */
static float picture(float* x0, float* y0, float* vw, float* vh)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float hs = gs_hor_scale(), a = 4.0f / 3.0f * hs;
    *vw = ds.y * a; *vh = ds.y;
    if (*vw > ds.x) { *vw = ds.x; *vh = ds.x / a; }
    if (g_config.aspect == ASPECT_STRETCH && hs == 1.0f) { *vw = ds.x; *vh = ds.y; }
    *x0 = (ds.x - *vw) * 0.5f; *y0 = (ds.y - *vh) * 0.5f;
    return hs;
}

/* Above the partner's head (co-op): its name (option) and, while it is busy, what with, one
   box. Off screen or behind the camera (same room): a marker at the edge of the view pointing
   at it, with the name. Game screen (640x480, 4:3 area) to the window as gs_gl.c presents it;
   the text scales with the window height (FontScaleDpi). */
static void name_tag(void)
{
    static const char* what[] = { "", "cutscene", "menu", "item box", "trading" };
    float x0, y0, vw, vh, hs = picture(&x0, &y0, &vw, &vh);
    float x = x0 + (0.5f + (tag_x - 320.0f) / (640.0f * hs)) * vw;
    float y = y0 + tag_y / 480.0f * vh;
    bool behind = tag_flags == STREAM_PT_BEHIND;
    const char* name = g_coop.nametag ? (cs.peer_name[0] ? cs.peer_name : tr("Partner")) : nullptr;
    const char* busy = cs.trade_hint[0] ? cs.trade_hint : cs.busy_kind > 0 && cs.busy_kind < 5 ? tr(what[cs.busy_kind]) : nullptr;   /* it asks us to trade / is busy */
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

/* The HUD (pc/glue/hud_glue.c, docs/THIRDPERSON.md "HUD"): in a bottom corner of the picture, by "HUD Style":
   Weapon Circle, a dark disc with the equipped weapon's inventory icon and its loaded rounds, the health gauge an arc of
   HUD_SWEEP round its outer side from the bottom up, the reload ammo beside it on the inner side; Minimal, a health bar
   with "loaded/spare" and a small icon above it. The gauge is coloured by the condition (pulsing in Danger), its whole
   length outlined. On the left mirrored. It fades in and out (moving to the other corner too) and with the screen.
   Behind the windows. */
#define HUD_SWEEP (200.0f * 3.14159265f / 180.0f)
static float hud_a;              /* shown, 0..1 (eased) */
static int hud_left;             /* the side drawn */
static GLuint hud_tex;           /* the item icons */
static int hud_tw, hud_th;

static void hud_text(ImDrawList* dl, float size, float x, float y, ImU32 col, const char* s)
{
    float o = size * 0.06f > 1.0f ? size * 0.06f : 1.0f;
    dl->AddText(ImGui::GetFont(), size, ImVec2(x + o, y + o), (col >> 25 << 24), s);   /* (half its alpha, black) */
    dl->AddText(ImGui::GetFont(), size, ImVec2(x, y), col, s);
}

/* the rounds (n, its % or unlimited: a drawn infinity, the fonts have none) centred on x, top at y */
static void hud_count(ImDrawList* dl, float size, float x, float y, int n, int pct, ImU32 col)
{
    char b[16];
    if (n == HUD_INF) {
        float r = size * 0.17f;
        dl->AddCircle(ImVec2(x - r, y + size * 0.5f), r, col, 16, size * 0.08f);
        dl->AddCircle(ImVec2(x + r, y + size * 0.5f), r, col, 16, size * 0.08f);
        return;
    }
    snprintf(b, sizeof b, pct ? "%d%%" : "%d", n);
    hud_text(dl, size, x - ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, b).x * 0.5f, y, col, b);
}

static void hud_draw(void)
{
    static const ImU32 cond_col[] = { IM_COL32(70, 225, 95, 255), IM_COL32(230, 215, 40, 255), IM_COL32(240, 120, 25, 255),
                                      IM_COL32(230, 35, 30, 255), IM_COL32(190, 60, 200, 255) };   /* the status screen's ECG (sub1.c), brighter */
    float dt = ImGui::GetIO().DeltaTime, target = g_hud.on && g_hud.left == hud_left ? 1.0f - g_hud.fade : 0.0f, step = dt * 6.0f;
    hud_a = target > hud_a ? (hud_a + step < target ? hud_a + step : target) : (hud_a - step > target ? hud_a - step : target);
    if (hud_a <= 0.0f) { hud_left = g_hud.left; return; }   /* out: then in at the other side */
    if (!hud_tex) {   /* once the game thread read them */
        const unsigned int* px = hud_icons(&hud_tw, &hud_th);
        if (px) {
            glGenTextures(1, &hud_tex);
            glBindTexture(GL_TEXTURE_2D, hud_tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hud_tw, hud_th, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    float x0, y0, vw, vh;
    picture(&x0, &y0, &vw, &vh);
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float al = hud_a, dir = hud_left ? -1.0f : 1.0f, o = vh / 720.0f > 1.0f ? vh / 720.0f : 1.0f;   /* o: the outline's width */
    auto A = [al](ImU32 col, float k = 1.0f) { return (col & 0xFFFFFF) | (ImU32)((col >> 24) * al * k) << 24; };
    float f = g_hud.hp_max > 0 ? (float)g_hud.hp / g_hud.hp_max : 0.0f;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    ImU32 hp_col = A(cond_col[g_hud.cond < 0 || g_hud.cond > HUD_POISON ? 0 : g_hud.cond],
                     g_hud.cond == HUD_DANGER ? 0.7f + 0.3f * cosf((float)ImGui::GetTime() * 6.0f) : 1.0f);   /* Danger: a pulse */
    const ImU32 outline = A(IM_COL32(220, 225, 235, 110)), track = A(IM_COL32(28, 32, 40, 190));
    auto icon = [&](float x, float y, float k) {   /* the weapon's icon, top left at x, y, scaled by k */
        float iw = (float)g_hud.icon_w, ih = (float)g_hud.icon_h;
        float u0 = (g_hud.icon_x + 0.5f) / hud_tw, v0 = (g_hud.icon_y + 0.5f) / hud_th, u1 = (g_hud.icon_x + iw - 0.5f) / hud_tw, v1 = (g_hud.icon_y + ih - 0.5f) / hud_th;
        dl->AddImage((ImTextureID)(intptr_t)hud_tex, ImVec2(x, y), ImVec2(x + iw * k, y + ih * k), ImVec2(u0, v0), ImVec2(u1, v1), A(IM_COL32(255, 255, 255, 255)));
    };
    const ImU32 red = A(IM_COL32(240, 90, 80, 255));
    /* the counts in the colour of the rounds loaded (g_hud.round_kind), as their inventory icons: plain grenade rounds
       and arrows white (a grey icon), acid rounds gold, flame rounds orange (their icon is red, but red is the empty count's), gas rounds green, gunpowder arrows orange */
    static const ImU32 round_col[] = { IM_COL32(255, 255, 255, 240), IM_COL32(255, 210, 60, 240), IM_COL32(255, 140, 30, 240),
                                       IM_COL32(70, 230, 150, 240), IM_COL32(255, 150, 40, 240) };
    int rk = g_hud.round_kind >= 0 && g_hud.round_kind <= 4 ? g_hud.round_kind : 0;
    const ImU32 tint = A(round_col[rk]);
    if (g_tps.hud_style == 1) {
        /* Minimal: the bar in the corner, filled from the outer end; above it at the outer end the icon, then the rounds
           loaded and "/spare" smaller, on one baseline */
        float m = vh * 0.05f, bw = vh * 0.26f, bh = vh * 0.016f, fs = vh * 0.042f, ss = fs * 0.72f, ox = hud_left ? x0 + m : x0 + vw - m;
        float bx = hud_left ? ox : ox - bw, by = y0 + vh - m - bh, rr = bh * 0.5f;
        dl->AddRectFilled(ImVec2(bx - o, by - o), ImVec2(bx + bw + o, by + bh + o), outline, rr + o);
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), track, rr);
        if (f > 0) dl->AddRectFilled(ImVec2(hud_left ? bx : bx + bw * (1 - f), by), ImVec2(hud_left ? bx + bw * f : bx + bw, by + bh), hp_col, rr);
        ImFont* font = ImGui::GetFont();
        char ls[16] = "", sp[16] = "";
        if (g_hud.rounds >= 0) snprintf(ls, sizeof ls, g_hud.rounds_pct ? "%d%%" : "%d", g_hud.rounds);
        if (g_hud.rounds != HUD_NONE && g_hud.spare != HUD_NONE) snprintf(sp, sizeof sp, g_hud.spare_pct ? "/%d%%" : "/%d", g_hud.spare);
        float wl = g_hud.rounds == HUD_INF ? fs * 0.75f : g_hud.rounds == HUD_NONE ? 0.0f : font->CalcTextSizeA(fs, FLT_MAX, 0, ls).x;
        float wsp = *sp ? font->CalcTextSizeA(ss, FLT_MAX, 0, sp).x : 0.0f, gap = fs * 0.25f;
        float ik = hud_tex && g_hud.icon_w > 0 ? fs * 1.1f / g_hud.icon_h : 0.0f, wi = g_hud.icon_w * ik;
        float tw = wi + (wi > 0 && wl > 0 ? gap : 0.0f) + wl + wsp, x = hud_left ? ox : ox - tw, base = by - vh * 0.012f;
        float asc = font->GetFontBaked(fs)->Ascent, sasc = font->GetFontBaked(ss)->Ascent;
        if (wi > 0) { icon(x, base - asc * 0.5f - g_hud.icon_h * ik * 0.5f, ik); x += wi + gap; }
        if (g_hud.rounds != HUD_NONE) hud_count(dl, fs, x + wl * 0.5f, base - asc, g_hud.rounds, g_hud.rounds_pct, g_hud.rounds == 0 ? red : tint);
        if (*sp) hud_text(dl, ss, x + wl, base - sasc, !g_hud.spare ? red : rk ? tint : A(IM_COL32(215, 215, 220, 230)), sp);
        return;
    }
    float R = vh * 0.07f, th = R * 0.17f, ra = R + th * 1.1f, m = vh * 0.05f + ra + th * 0.5f;
    ImVec2 c(hud_left ? x0 + m : x0 + vw - m, y0 + vh - m);
    /* the disc */
    dl->AddCircleFilled(c, R, A(IM_COL32(8, 12, 18, 150)), 48);
    dl->AddCircle(c, R, A(IM_COL32(255, 255, 255, 45)), 48, 1.5f);
    /* the health: from the bottom (pi/2, y down) up the outer side; the outline o wider all round, the track inside it */
    float a0 = 3.14159265f * 0.5f, a1 = a0 - dir * HUD_SWEEP, e = o / ra;
    dl->PathArcTo(c, ra, a0 + dir * e, a1 - dir * e, 40);
    dl->PathStroke(outline, 0, th + 2.0f * o);
    dl->PathArcTo(c, ra, a0, a1, 40);
    dl->PathStroke(track, 0, th);
    if (f > 0) {
        dl->PathArcTo(c, ra, a0, a0 + (a1 - a0) * f, 2 + (int)(38 * f));
        dl->PathStroke(hp_col, 0, th);
    }
    /* the weapon and its rounds */
    float fs = R * 0.5f, iy = g_hud.rounds != HUD_NONE ? c.y - R * 0.2f : c.y;
    if (hud_tex && g_hud.icon_w > 0) {
        float iw = (float)g_hud.icon_w, ih = (float)g_hud.icon_h, k = 1.5f * R / iw < 0.75f * R / ih ? 1.5f * R / iw : 0.75f * R / ih;
        icon(c.x - iw * k * 0.5f, iy - ih * k * 0.5f, k);
    }
    if (g_hud.rounds != HUD_NONE)
        hud_count(dl, fs, c.x, c.y + R * 0.2f, g_hud.rounds, g_hud.rounds_pct, g_hud.rounds == 0 ? red : tint);
    /* the reload ammo, on the inner side */
    if (g_hud.spare != HUD_NONE) {
        char b[16];
        float ss = R * 0.42f, pad = ss * 0.25f;
        snprintf(b, sizeof b, g_hud.spare_pct ? "%d%%" : "%d", g_hud.spare);
        float w = ImGui::GetFont()->CalcTextSizeA(ss, FLT_MAX, 0, b).x, ex = c.x - dir * (R + pad * 2), x = hud_left ? ex : ex - w, y = c.y + R * 0.15f;
        dl->AddRectFilled(ImVec2(x - pad, y - pad * 0.4f), ImVec2(x + w + pad, y + ss + pad * 0.4f), A(IM_COL32(8, 12, 18, 140)), ss * 0.3f);
        hud_text(dl, ss, x, y, !g_hud.spare ? A(IM_COL32(240, 90, 80, 240)) : rk ? tint : A(IM_COL32(230, 230, 230, 240)), b);
    }
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
    static ImportItem (*items)[1024];   /* per source (heap: the address window, docs/ARCHITECTURE.md) */
    if (!items && !(items = (ImportItem(*)[1024])calloc(IMPORT_SOURCES, sizeof(*items)))) return;
    static int n[IMPORT_SOURCES], loaded, was_running, what[IMPORT_SOURCES] = { IMPORT_PLAYERS };
    static char when[IMPORT_SOURCES][64];
    ImportProgress* pg = &g_import_progress;
    int running = __atomic_load_n(&pg->running, __ATOMIC_ACQUIRE);
    if (!loaded || (was_running && !running)) {   /* the manifests: at the first opening and after an extraction */
        for (int s = 0; s < IMPORT_SOURCES; s++) n[s] = import_results(s, items[s], 1024, when[s], sizeof(when[s]));
        if (loaded) { import_registry_load(); lang_scan(); }   /* new player models for "You appear as", new languages */
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
    if (!ImGui::Begin(tr("Import (F9)"), &import_visible)) { ImGui::End(); return; }
    wrapped("%s", tr("Import from your own disc images: character models (Character Model) and languages (Language)."));
    for (int s = 0; s < IMPORT_SOURCES; s++) {
        const ImportSource* src = &import_sources[s];
        char label[128], dir[1100];
        SDL_snprintf(label, sizeof(label), "%s (%s)", src->title, src->serial);
        if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) continue;
        ImGui::PushID(s);
        if (src->help) wrapped("%s", tr(src->help));   /* (the Russian translation's author) */
        wrapped(tr("Disc image: %s"), g_import.iso[s][0] ? g_import.iso[s] : tr("(none chosen)"));
        ImGui::BeginDisabled(running != 0);
        if (ImGui::Button(tr("Choose Disc Image..."))) {
            static const SDL_DialogFileFilter filters[] = { { "PS2 disc image (*.iso)", "iso" }, { "All files", "*" } };
            SDL_ShowOpenFileDialog(import_picked, (void*)(intptr_t)s, win, filters, 2, g_import.iso[s][0] ? g_import.iso[s] : NULL, false);
        }
        import_source_dir(s, dir, sizeof(dir));
        ImGui::TextDisabled(tr("Output: %s"), dir);
        if (src->cat == IMPORT_CAT_LANGUAGES) {   /* languages (lang.h): every language of the disc */
            ImGui::BeginDisabled(!g_import.iso[s][0]);
            if (ImGui::Button(tr("Import Languages"))) import_start(s, 0);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (running || pg->total) {
                char ov[64];
                SDL_snprintf(ov, sizeof(ov), "%d / %d", pg->done, pg->total);
                ImGui::ProgressBar(pg->total ? (float)pg->done / pg->total : 0.0f, ImVec2(-1, 0), ov);
                wrapped("%s", pg->phase);
            }
            for (int l = 1; l < LANG_COUNT; l++)
                if (lang_installed(l) && lang_dir(l) && !SDL_strncmp(lang_dir(l), dir, SDL_strlen(dir)))
                    ImGui::BulletText("%s%s", ov_rtl() && lang_has_subtitles(l) ? tr(" (cutscene subtitles)") : lang_names[l],
                                      !lang_has_subtitles(l) ? "" : ov_rtl() ? lang_names[l] : tr(" (cutscene subtitles)"));   /* (right to left: the note first) */
            ImGui::PopID();
            continue;
        }
        {   /* (only the groups offered, IMPORT_OFFERED) */
            static const struct { const char* label; int bit; } groups[] = {
                { "Playable Characters", IMPORT_PLAYERS }, { "Cutscene Models", IMPORT_CUTSCENE }, { "Other Characters", IMPORT_NPCS }, { "Enemies", IMPORT_ENEMIES } };
            int first = 1;
            what[s] &= IMPORT_OFFERED;
            for (const auto& g : groups) {
                if (!(IMPORT_OFFERED & g.bit)) continue;
                if (!first) ImGui::SameLine();
                ImGui::CheckboxFlags(tr(g.label), &what[s], g.bit);
                first = 0;
            }
        }
        ImGui::BeginDisabled(!g_import.iso[s][0] || !what[s]);
        if (ImGui::Button(tr("Import Characters"))) import_start(s, what[s]);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (running || pg->total) {
            char ov[64];
            SDL_snprintf(ov, sizeof(ov), "%d / %d", pg->done, pg->total);
            ImGui::ProgressBar(pg->total ? (float)pg->done / pg->total : 0.0f, ImVec2(-1, 0), ov);
            wrapped("%s", pg->phase);
        }
        int ok = 0, players = 0, failed = 0;
        for (int i = 0; i < n[s]; i++) {
            const ImportItem* x = &items[s][i];
            if (x->group < 0 || !(IMPORT_OFFERED & (1 << x->group))) continue;
            ok += x->ok; players += x->player; failed += !x->ok;
        }
        if (!n[s]) ImGui::TextDisabled(tr("Nothing extracted yet."));
        else {
            ImGui::Text(tr("Extracted %s: %d models, %d usable as player models, %d failed"), when[s], ok, players, failed);
            static const char* const gname[IMPORT_GROUPS] = { "Playable Characters", "Cutscene Models", "Other Characters", "Enemies" };
            for (int g = 0; g < IMPORT_GROUPS; g++) {
                if (!(IMPORT_OFFERED & (1 << g))) continue;   /* (an older run's other groups: not listed) */
                int gn = 0, gp = 0;
                for (int i = 0; i < n[s]; i++) if (items[s][i].group == g) { gn++; gp += items[s][i].player != 0; }
                if (!gn) continue;
                char gl[96];
                SDL_snprintf(gl, sizeof(gl), tr("%s: %d, %d player models###g%d"), tr(gname[g]), gn, gp, g);
                if (!ImGui::TreeNodeEx(gl, g == IMPORT_GROUP_PLAYERS ? ImGuiTreeNodeFlags_DefaultOpen : 0)) continue;
                if (ImGui::BeginTable("##items", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV,
                                      ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 15))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn(tr("Character")); ImGui::TableSetupColumn(tr("Variant")); ImGui::TableSetupColumn(tr("File"));
                    ImGui::TableSetupColumn(tr("Meshes")); ImGui::TableSetupColumn(tr("Vertices")); ImGui::TableSetupColumn(tr("Triangles"));
                    ImGui::TableSetupColumn(tr("Textures")); ImGui::TableSetupColumn(tr("Player Model"));
                    ImGui::TableSetupColumn(tr("Notes"), ImGuiTableColumnFlags_WidthStretch);
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
                        if (!x->ok) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), tr("failed"));
                        else if (x->player) ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), tr("yes"));
                        else ImGui::TextDisabled("%s", tr(x->female < 0 ? "glTF only" : "no"));
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

/* our texts in other languages (lang.h): the default font has ASCII and Latin-1 (French, German, Spanish, Portuguese,
   Italian); the letters of Japanese, Korean, Russian, Turkish and Arabic (its presentation forms: our Arabic texts are in
   display order, lang_shape) come from a font of the system, merged in once such a language is chosen (none found: they
   stay missing) */
static void lang_fonts(void)
{
    static const char* const ar[] = { "C:/Windows/Fonts/tahoma.ttf", "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf", NULL };
    static const char* const ja[] = { "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc", "C:/Windows/Fonts/YuGothM.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", NULL };
    static const char* const ko[] = { "C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/gulim.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/TTF/NanumGothic.ttf", "/usr/share/fonts/truetype/nanum/NanumGothic.ttf", "/usr/share/fonts/nanum/NanumGothic.ttf",
        "/usr/share/fonts/TTF/gulim.ttf", "/usr/share/fonts/truetype/baekmuk/gulim.ttf", NULL };
    static const char* const ru[] = { "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/dejavu/DejaVuSans.ttf", NULL };
    static bool done[LANG_COUNT];
    int l = lang_installed(g_lang) ? g_lang : LANG_EN;
    if ((l != LANG_JA && l != LANG_KO && l != LANG_RU && l != LANG_TR && l != LANG_AR) || done[l]) return;   /* (Turkish: ğ ı İ ş, Latin Extended-A) */
    done[l] = true;
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    const char* test = SDL_getenv("RECVX_TEST_OVERLAY_FONT");   /* test aid: a font file to try first */
    if (test && *test) {
        ImFontConfig c;
        c.MergeMode = true;
        if (SDL_GetPathInfo(test, NULL) && io.Fonts->AddFontFromFileTTF(test, 13.0f, &c)) { host_log("overlay: the letters of %s from %s\n", lang_codes[l], test); return; }
    }
    for (const char* const* p = l == LANG_JA ? ja : l == LANG_KO ? ko : l == LANG_AR ? ar : ru; *p; p++) {
        ImFontConfig c;
        c.MergeMode = true;
        if (SDL_GetPathInfo(*p, NULL) && io.Fonts->AddFontFromFileTTF(*p, 13.0f, &c)) { host_log("overlay: the letters of %s from %s\n", lang_codes[l], *p); return; }
    }
    host_log("overlay: no font of the system has the letters of %s\n", lang_codes[l]);
}

/* --dev cutscene log (devcine.h, docs/DEV_TOOLS.md; English only). The small overlay at the top left: what plays
   now and the last scenes, behind the windows, no input. F6: the whole log, copy, export, clear. */
static DevCine* dc_list;                /* (a snapshot per frame shown; 32 entries) */
static char dc_msg[300];

static void devcine_copy(const char* one)
{
    char* t = one ? nullptr : (char*)SDL_malloc(DEVCINE_TEXT);
    int n = 0;
    if (!one && !t) return;
    if (t) {
        devcine_text(t, DEVCINE_TEXT);
        for (const char* c = t; *c; c++) n += *c == '\n';
    }
    SDL_SetClipboardText(one ? one : t);
    char* back = SDL_GetClipboardText();   /* (logged: the test aid's check of what went there) */
    host_log("devcine: %s copied to the clipboard, %d bytes read back\n%s%s", one ? "a line" : n == 1 ? "1 scene" : "the log", back ? (int)strlen(back) : -1,
             back ? back : "", back && *back && back[strlen(back) - 1] != '\n' ? "\n" : "");
    SDL_free(back);
    if (one) SDL_snprintf(dc_msg, sizeof dc_msg, "Copied the line.");
    else SDL_snprintf(dc_msg, sizeof dc_msg, "Copied %d %s to the clipboard.", n, n == 1 ? "scene" : "scenes");
    SDL_free(t);
}

static void devcine_small(const DevCine* l, int n, int open)
{
    char line[256];
    ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##devcine", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PushTextWrapPos(ImGui::GetIO().DisplaySize.x * 0.45f);
    if (open) {
        devcine_format(&l[n - 1], 0, line, sizeof line);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "> %s", line);
        if (l[n - 1].cast[0]) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 0.8f), "%s", l[n - 1].cast);
            ImGui::Unindent();
        }
    } else ImGui::TextDisabled("No cutscene");
    for (int k = n - 1 - open, shown = 0; k >= 0 && shown < 4; k--, shown++) {
        devcine_format(&l[k], 0, line, sizeof line);
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 0.75f), "  %s", line);
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
}

/* F6 "Scenes": the scene list (devcine.h DevScene), searched, one played */
static char dc_find[96];
static void devcat_tab(void)
{
    char* find = dc_find;
    static char* lines;   /* lines: each scene's devcat_line, made again when the save menu's room names come */
    static int sel = -1, chr, made = -1;
    static bool hide_short;
    static const char* const as[] = { "Auto (the part's character)", "Claire", "Chris", "As now" };
    char path[1100], st[200], buf[160];
    int n = devcat_count(), named = g_cheat.rooms[1][0] != 0;
    enum { W = 400 };
    tp_count();   /* (the first call reads the rooms; the list comes with them) */
    if (!n) { ImGui::TextDisabled(tp_reading() ? "Reading the room files..." : "No scene list (the room files could not be read)."); return; }
    if (made != named || !lines) {
        if (!lines) lines = (char*)SDL_malloc((size_t)n * W);
        if (!lines) return;
        for (int i = 0; i < n; i++) {
            const DevScene* s = devcat_get(i);
            devcat_line(s, tp_room_name(tp_find(s->room / 1000, s->room / 10 % 100), 0, buf, sizeof buf), lines + (size_t)i * W, W);
        }
        made = named;
    }
    SDL_snprintf(path, sizeof path, "%s/dev_cutscene_list.txt", g_config.save_dir);
    ImGui::SetNextItemWidth(320);
    ImGui::InputTextWithHint("##find", "Search: room, name, ev 1:1, character, MV_005", dc_find, sizeof dc_find);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230);
    ImGui::Combo("Play as", &chr, as, IM_ARRAYSIZE(as));
    ImGui::SameLine();
    ImGui::Checkbox("Hide short shots", &hide_short);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scenes of one or two camera shots, nothing said, no one shown (doors, levers, a look at a room).");
    ImGui::BeginDisabled(sel < 0 || sel >= n);
    if (ImGui::Button("PLAY")) devcat_play(sel, chr);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Export list")) {
        int w = devcat_write(path);
        if (w < 0) SDL_snprintf(dc_msg, sizeof dc_msg, "Cannot write %s.", path);
        else SDL_snprintf(dc_msg, sizeof dc_msg, "Wrote %d scenes to %s.", w, path);
        host_log("devcine: %s\n", dc_msg);
    }
    devcat_status(st, sizeof st);
    ImGui::SameLine();
    ImGui::TextUnformatted(st[0] ? st : dc_msg);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%d scenes from the room files (%d cutscene events no script starts are left out). PLAY teleports to the scene's room "
                       "file (its story case), sets the story flags its script tests, switches the character played when the part needs it "
                       "and starts the scene as its script does; the log shows it as usual. Afterwards you stay in that room. Saving "
                       "then may break the story: the scene's flags are set out of order.", n, devcat_unused());
    ImGui::PopStyleColor();
    if (sel >= 0 && sel < n) {   /* what PLAY does for it */
        const DevScene* s = devcat_get(sel);
        int len = SDL_snprintf(buf, sizeof buf, "Started by %s", s->from == 0 ? "the room's entry script" : s->from == 1 ? "the room's script" : "event ");
        if (s->from >= 2) len += SDL_snprintf(buf + len, sizeof buf - len, "%d", s->from - 2);
        for (int k = 0; k < s->nflag && len < (int)sizeof buf - 1; k++)
            len += SDL_snprintf(buf + len, sizeof buf - len, "%s%s %d %s", k ? ", " : "; needs ",
                                s->flag[k].set == 1 ? "ev_flg" : s->flag[k].set == 2 ? "ky_flg" : s->flag[k].set == 3 ? "ed_flg" : s->flag[k].set == 7 ? "it_flg" : "mp_flg",
                                s->flag[k].bit, s->flag[k].on ? "set" : "clear");
        ImGui::TextDisabled("%s", buf);
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##devcat", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        static int* shown;
        int m = 0;
        if (!shown) shown = (int*)SDL_malloc(sizeof(int) * (size_t)n);
        for (int i = 0; shown && i < n; i++) {   /* every word of the search in the line */
            const char* t = lines + (size_t)i * W;
            char w[96];
            int ok = !(hide_short && devcat_short(devcat_get(i)));
            for (const char* f = find; ok && *f;) {
                int k = 0;
                while (*f == ' ') f++;
                while (*f && *f != ' ' && k < (int)sizeof w - 1) w[k++] = *f++;
                w[k] = 0;
                if (k && !SDL_strcasestr(t, w)) ok = 0;
            }
            if (ok) shown[m++] = i;
        }
        ImGuiListClipper clip;
        clip.Begin(m);
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++) {
                int i = shown[r];
                ImGui::PushID(i);
                if (ImGui::Selectable(lines + (size_t)i * W, sel == i, ImGuiSelectableFlags_AllowDoubleClick)) {
                    sel = i;
                    if (ImGui::IsMouseDoubleClicked(0)) devcat_play(sel, chr);
                }
                ImGui::PopID();
            }
        if (!m) ImGui::TextDisabled("No scene matches.");
    }
    ImGui::EndChild();
}

static void devcine_window(const DevCine* l, int n, int open)
{
    char line[512], path[1100], key[64];
    bool show = g_devcine_show != 0;
    SDL_snprintf(path, sizeof path, "%s/dev_cutscenes.txt", g_config.save_dir);
    ImGui::SetNextWindowPos(ImVec2(60, 140), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900, 440), ImGuiCond_FirstUseEver);
    SDL_snprintf(line, sizeof line, "Cutscene Log (%s)###devcine", bind_code_name(BIND_KB, bind_get(B_DEVCINE, BIND_KB, 0), key, sizeof key));
    if (!ImGui::Begin(line, &devcine_visible)) { ImGui::End(); return; }
    if (!ImGui::BeginTabBar("##devcinetabs")) { ImGui::End(); return; }
    {   /* (after Log; test aid RECVX_DEVCINE_FIND="text": this tab open at start, searched for text) */
        static int first = 1;
        const char* f = first ? SDL_getenv("RECVX_DEVCINE_FIND") : nullptr;
        if (f) SDL_strlcpy(dc_find, f, sizeof dc_find);
        if (ImGui::BeginTabItem("Scenes", nullptr, ImGuiTabItemFlags_Trailing | (f ? ImGuiTabItemFlags_SetSelected : 0))) { devcat_tab(); ImGui::EndTabItem(); }
        first = 0;
    }
    if (!ImGui::BeginTabItem("Log")) { ImGui::EndTabBar(); ImGui::End(); return; }
    if (ImGui::Checkbox("Show overlay", &show)) { g_devcine_show = show; settings_save_later(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The small log at the top left while this window is closed (it records either way).");
    ImGui::SameLine();
    ImGui::BeginDisabled(n == 0);
    if (ImGui::Button("Copy")) devcine_copy(nullptr);
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        int w = devcine_export(path);
        if (w < 0) SDL_snprintf(dc_msg, sizeof dc_msg, "Cannot write %s.", path);
        else SDL_snprintf(dc_msg, sizeof dc_msg, "Appended %d %s to %s.", w, w == 1 ? "scene" : "scenes", path);
        host_log("devcine: %s\n", dc_msg);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { devcine_clear(); SDL_snprintf(dc_msg, sizeof dc_msg, "Cleared."); host_log("devcine: the log cleared\n"); }
    ImGui::EndDisabled();
    if (dc_msg[0]) { ImGui::SameLine(); ImGui::TextUnformatted(dc_msg); }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s opens and closes this window. Click a line to copy it. Export appends the log to %s. \"ev 3:7\": the room script's "
                       "event 7 on task level 3 (RECVX_TEST_EVENT=frame:3:7 replays it; \"?\": guessed); r3: the room's enemy record 3.", key, path);
    ImGui::PopStyleColor();
    ImGui::Separator();
    if (!open) ImGui::TextDisabled("No cutscene playing.");
    if (ImGui::BeginChild("##devcinelist", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        if (!n) ImGui::TextDisabled("Nothing recorded yet.");
        for (int k = n - 1; k >= 0; k--) {   /* newest first */
            devcine_format(&l[k], 1, line, sizeof line);
            ImGui::PushID(k);
            if (k == n - 1 && open) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
            ImGui::PushTextWrapPos(0.0f);
            if (ImGui::Selectable("##line", false, 0, ImVec2(0, ImGui::CalcTextSize(line, nullptr, false, ImGui::GetContentRegionAvail().x).y))) devcine_copy(line);
            ImGui::SameLine(ImGui::GetStyle().WindowPadding.x * 0.5f);
            ImGui::TextUnformatted(line);
            ImGui::PopTextWrapPos();
            if (k == n - 1 && open) ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
    ImGui::EndTabBar();
    ImGui::End();
}

static void overlay_frame(void)
{
    if (!ready) return;
    lang_fonts();
    coop_status_get(&cs);
    placer_poll(cheats_visible);
    bool devcine_on = g_dev_tools && (devcine_visible || g_devcine_show);   /* --dev cutscene log */
    bool windows = visible || cheats_visible || coop_visible || import_visible || (g_dev_tools && devcine_visible);
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
    if (!visible && !cheats_visible && !coop_visible && !import_visible && !devcine_on && !g_config.show_fps && !ping && !crosshair && !hint && !tag && !g_hud.on && hud_a <= 0.0f) return;
    {   /* readable on large displays: scale text with the window height (1080p = 1x) */
        int ww, wh;
        SDL_GetWindowSizeInPixels(win, &ww, &wh);
        ImGui::GetStyle().FontScaleDpi = wh > 1080 ? wh / 1080.0f : 1.0f;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    hud_draw();
    if (devcine_on && (dc_list || (dc_list = (DevCine*)SDL_malloc(sizeof(DevCine) * DC_MAX)))) {
        int open, n = devcine_snapshot(dc_list, &open);
        if (devcine_visible) devcine_window(dc_list, n, open);
        else devcine_small(dc_list, n, open);
    }
    if (visible) settings_window();
    if (cheats_visible) cheat_window();
    if (cheats_visible) placer_overlay();
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
        ImGui::Begin(tr("##coophint"), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::TextUnformatted(hint);
        ImGui::End();
    }
    if (g_config.show_fps || ping) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10, 10), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin(tr("##fps"), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                       ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
        if (g_config.show_fps && ping) ImGui::Text(tr("%.1f fps   ping %d ms"), ImGui::GetIO().Framerate, cs.ping_ms);
        else if (ping) ImGui::Text(tr("ping %d ms"), cs.ping_ms);
        else ImGui::Text(tr("%.1f fps"), ImGui::GetIO().Framerate);
        ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void overlay_render(void)
{
    tr_overlay(1);   /* our windows in English unless Translate Overlay Windows is on (lang.h) */
    overlay_frame();
    tr_overlay(0);
}
