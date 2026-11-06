/* --dev object placer, render thread: the F2 window's section, the gizmo over the game picture,
   keyboard nudges and the file (docs/DEV_PLACEMENTS.md). The game thread's side: pc/glue/placer_glue.c. */
#include "../ps2_platform.h"
#include "placer.h"
#include "cheat.h"
#include "bindings.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern "C" float gs_hor_scale(void);
extern "C" uint64_t gs_frame_count(void);

enum { H_X, H_Y, H_Z, H_FREE, H_RX, H_RY, H_RZ, H_N };
static const ImU32 axis_col[3] = { IM_COL32(235, 70, 60, 255), IM_COL32(90, 220, 90, 255), IM_COL32(80, 140, 255, 255) };
static int drag = -1, hover = -1;
static ImVec2 m0, d_o, d_ax[3];
static float d_pos[3], d_len, d_th0;
static int d_ang[3];
static Uint64 dirty;                    /* SDL_GetTicks of the last change not saved yet (0: none) */
static const char* msg;
static char msg_buf[1200];

static void path(char* b, int n) { snprintf(b, n, "%s/dev_placements.ini", g_config.save_dir); }
static void changed(void) { dirty = SDL_GetTicks() | 1; }

/* game screen (640x480) -> window pixels, as the co-op name tag */
ImVec2 placer_game_to_win(float gx, float gy)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float hs = gs_hor_scale(), a = 4.0f / 3.0f * hs, vw = ds.y * a, vh = ds.y;
    if (vw > ds.x) { vw = ds.x; vh = ds.x / a; }
    if (g_config.aspect == ASPECT_STRETCH && hs == 1.0f) { vw = ds.x; vh = ds.y; }
    float x0 = (ds.x - vw) * 0.5f, y0 = (ds.y - vh) * 0.5f;
    return ImVec2(x0 + (0.5f + (gx - 320.0f) / (640.0f * hs)) * vw, y0 + gy / 480.0f * vh);
}

static Placement* selected(void)
{
    int s = g_placer.sel;
    return s >= 0 && s < PL_MAX && g_placer.p[s].used ? &g_placer.p[s] : nullptr;
}
static bool here(const Placement* p) { return p->used && p->stg == g_placer.stg && p->rom == g_placer.rom; }
static float to_deg(int a) { a &= 0xFFFF; return (a > 32767 ? a - 65536 : a) * 360.0f / 65536.0f; }
static int to_ang(float d) { return (int)lroundf(d * 65536.0f / 360.0f) & 0xFFFF; }

static int add(int type, const float* pos, int ay)
{
    int i;
    for (i = 0; i < PL_MAX && g_placer.p[i].used; i++);
    if (i == PL_MAX) { msg = "The list is full."; return -1; }
    Placement p = {};
    p.type = type;
    p.stg = g_placer.stg; p.rom = g_placer.rom; p.rcase = g_placer.rcase; p.ply = g_placer.ply;
    memcpy(p.pos, pos, sizeof(p.pos));
    p.ang[1] = ay & 0xFFFF;
    p.used = 1;
    g_placer.p[i] = p;
    g_placer.sel = i;
    changed();
    return i;
}

/* ---------------------------------------------------------------- the gizmo */
static float dist_seg(ImVec2 p, ImVec2 a, ImVec2 b)
{
    ImVec2 ab(b.x - a.x, b.y - a.y), ap(p.x - a.x, p.y - a.y);
    float l = ab.x * ab.x + ab.y * ab.y, t = l > 0 ? (ap.x * ab.x + ap.y * ab.y) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float dx = a.x + ab.x * t - p.x, dy = a.y + ab.y * t - p.y;
    return sqrtf(dx * dx + dy * dy);
}
static ImVec2 W(const float* g) { return placer_game_to_win(g[0], g[1]); }

/* the angle (65536 units) of ring k nearest to window point m; *d: its distance */
static float ring_at(int k, ImVec2 m, float* d)
{
    float best = 1e9f, th = 0;
    for (int j = 0; j < PL_RING; j++) {
        int j2 = (j + 1) % PL_RING;
        if (!g_placer.gz_ring_ok[k][j] || !g_placer.gz_ring_ok[k][j2]) continue;
        ImVec2 a = W(g_placer.gz_ring[k][j]), b = W(g_placer.gz_ring[k][j2]);
        ImVec2 ab(b.x - a.x, b.y - a.y);
        float l = ab.x * ab.x + ab.y * ab.y, t = l > 0 ? ((m.x - a.x) * ab.x + (m.y - a.y) * ab.y) / l : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float e = dist_seg(m, a, b);
        if (e < best) { best = e; th = (j + t) * 65536.0f / (float)PL_RING; }
    }
    *d = best;
    return th;
}

static int pick(ImVec2 m)
{
    ImVec2 o = W(g_placer.gz_o);
    int h = -1;
    float best = 8.0f, d;
    if ((d = sqrtf((m.x - o.x) * (m.x - o.x) + (m.y - o.y) * (m.y - o.y))) < 9.0f) return H_FREE;
    for (int k = 0; k < 3; k++)
        if ((d = dist_seg(m, o, W(g_placer.gz_axis[k]))) < best) { best = d; h = H_X + k; }
    for (int k = 0; k < 3; k++) {
        if (k != 1 && !g_placer.rings) continue;
        ring_at(k, m, &d);
        if (d < best) { best = d; h = H_RX + k; }
    }
    return h;
}

static void drag_start(int h, ImVec2 m, const Placement* p)
{
    drag = h;
    m0 = m;
    d_o = W(g_placer.gz_o);
    for (int k = 0; k < 3; k++) d_ax[k] = W(g_placer.gz_axis[k]);
    d_len = g_placer.gz_len;
    memcpy(d_pos, p->pos, sizeof(d_pos));
    memcpy(d_ang, p->ang, sizeof(d_ang));
    if (h >= H_RX) { float d; d_th0 = ring_at(h - H_RX, m, &d); }
}

static void drag_move(ImVec2 m, Placement* p, bool snap)
{
    ImVec2 d(m.x - m0.x, m.y - m0.y);
    if (drag <= H_Z) {   /* along the axis as it is drawn */
        int k = drag - H_X;
        ImVec2 a(d_ax[k].x - d_o.x, d_ax[k].y - d_o.y);
        float l = a.x * a.x + a.y * a.y;
        if (l < 4.0f) return;
        float t = (d.x * a.x + d.y * a.y) / l * d_len;
        p->pos[k] = d_pos[k] + t;
        if (snap) p->pos[k] = roundf(p->pos[k] * 2.0f) / 2.0f;
    } else if (drag == H_FREE) {   /* on the floor plane: d = s * x axis + t * z axis */
        ImVec2 a(d_ax[0].x - d_o.x, d_ax[0].y - d_o.y), b(d_ax[2].x - d_o.x, d_ax[2].y - d_o.y);
        float det = a.x * b.y - a.y * b.x;
        if (fabsf(det) < 1.0f) return;
        p->pos[0] = d_pos[0] + (d.x * b.y - d.y * b.x) / det * d_len;
        p->pos[2] = d_pos[2] + (a.x * d.y - a.y * d.x) / det * d_len;
        if (snap) { p->pos[0] = roundf(p->pos[0] * 2.0f) / 2.0f; p->pos[2] = roundf(p->pos[2] * 2.0f) / 2.0f; }
    } else {   /* along the ring: the angle moves by as much as the grabbed point did */
        int k = drag - H_RX;
        float e, th = ring_at(k, m, &e), dt = th - d_th0;
        if (dt > 32768.0f) dt -= 65536.0f;
        if (dt < -32768.0f) dt += 65536.0f;
        int a = (d_ang[k] + (int)lroundf(dt)) & 0xFFFF;
        if (snap) a = (int)lroundf(roundf(a / (65536.0f / 24)) * (65536.0f / 24)) & 0xFFFF;   /* 15 degrees */
        p->ang[k] = a;
    }
    changed();
}

/* test aid RECVX_PLACE_DRAG="frame:handle:dx:dy,...": from that presented frame the mouse grabs the selected
   entry's handle (x y z free rx ry rz) and moves by dx, dy window pixels over 20 presented frames, then lets go */
static bool scripted(ImVec2* m, bool* down)
{
    static const char* s = (const char*)-1;
    static int f0 = -1, h, step, last;
    static float dx, dy;
    static ImVec2 at;
    static const char* names[H_N] = { "x", "y", "z", "free", "rx", "ry", "rz" };
    if (s == (const char*)-1) s = SDL_getenv("RECVX_PLACE_DRAG");
    int f = (int)gs_frame_count();
    if (f0 < 0) {
        if (!s || !*s) return false;
        char hn[8];
        int n = 0;
        if (sscanf(s, "%d:%7[a-z]:%f:%f%n", &f0, hn, &dx, &dy, &n) != 4) { s = nullptr; return false; }
        s = strchr(s + n, ',') ? strchr(s + n, ',') + 1 : "";
        for (h = 0; h < H_N && strcmp(names[h], hn); h++);
        step = -1;
    }
    if (f < f0 || !g_placer.gz_ok) return false;
    if (step < 0) {   /* where the handle is */
        ImVec2 o = W(g_placer.gz_o);
        if (h <= H_Z) { ImVec2 t = W(g_placer.gz_axis[h]); at = ImVec2(o.x + (t.x - o.x) * 0.7f, o.y + (t.y - o.y) * 0.7f); }
        else if (h == H_FREE) at = o;
        else {   /* the ring's point on the screen farthest from the arrows */
            int k = h - H_RX, bj = 0;
            float far = -1;
            ImVec2 ds = ImGui::GetIO().DisplaySize;
            for (int j = 0; j < PL_RING; j++) {
                ImVec2 q = W(g_placer.gz_ring[k][j]);
                float d = 1e9f;
                if (!g_placer.gz_ring_ok[k][j] || q.x < 4 || q.y < 4 || q.x > ds.x - 4 || q.y > ds.y - 4) continue;
                for (int a = 0; a < 3; a++) d = fminf(d, dist_seg(q, o, W(g_placer.gz_axis[a])));
                if (d > far) { far = d; bj = j; }
            }
            at = W(g_placer.gz_ring[k][bj]);
        }
        SDL_Log("test: placer drag %s from %.0f %.0f by %.0f %.0f (presented frame %d)", names[h < H_N ? h : 0], at.x, at.y, dx, dy, f);
        last = f;
    }
    if (f != last || step < 0) { step++; last = f; }
    *m = ImVec2(at.x + dx * (step > 20 ? 20 : step) / 20.0f, at.y + dy * (step > 20 ? 20 : step) / 20.0f);
    *down = step <= 21;
    if (step > 22) f0 = -1;   /* the next one */
    return true;
}

static void gizmo(void)
{
    Placement* p = selected();
    ImGuiIO& io = ImGui::GetIO();
    if (!p || !here(p) || !g_placer.gz_ok) { drag = -1; return; }
    ImVec2 m = io.MousePos;
    bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left), free = !io.WantCaptureMouse, test = scripted(&m, &down);
    static bool was_down;
    if (test) free = true;
    if (drag < 0) hover = free ? pick(m) : -1;
    if (drag < 0 && free && down && !was_down && hover >= 0) drag_start(hover, m, p);
    if (drag >= 0 && down) drag_move(m, p, io.KeyCtrl);
    if (!down) drag = -1;
    was_down = down;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 o = W(g_placer.gz_o);
    for (int k = 0; k < 3; k++) {   /* the rings first, the arrows over them */
        if (k != 1 && !g_placer.rings) continue;
        int act = drag == H_RX + k || (drag < 0 && hover == H_RX + k);
        for (int j = 0; j < PL_RING; j++) {
            int j2 = (j + 1) % PL_RING;
            if (g_placer.gz_ring_ok[k][j] && g_placer.gz_ring_ok[k][j2])
                dl->AddLine(W(g_placer.gz_ring[k][j]), W(g_placer.gz_ring[k][j2]), act ? IM_COL32(255, 230, 90, 255) : axis_col[k], act ? 3.0f : 2.0f);
        }
    }
    for (int k = 0; k < 3; k++) {
        ImVec2 t = W(g_placer.gz_axis[k]), d(t.x - o.x, t.y - o.y);
        float l = sqrtf(d.x * d.x + d.y * d.y);
        int act = drag == H_X + k || (drag < 0 && hover == H_X + k);
        ImU32 c = act ? IM_COL32(255, 230, 90, 255) : axis_col[k];
        dl->AddLine(o, t, IM_COL32(0, 0, 0, 160), 5.0f);
        dl->AddLine(o, t, c, 3.0f);
        if (l > 1.0f) {   /* the arrow head */
            ImVec2 u(d.x / l, d.y / l), n(-u.y, u.x);
            dl->AddTriangleFilled(ImVec2(t.x + u.x * 10, t.y + u.y * 10), ImVec2(t.x + n.x * 5, t.y + n.y * 5), ImVec2(t.x - n.x * 5, t.y - n.y * 5), c);
        }
        dl->AddText(ImVec2(t.x + 6, t.y - 6), c, k == 0 ? "x" : k == 1 ? "y" : "z");
    }
    int act = drag == H_FREE || (drag < 0 && hover == H_FREE);
    dl->AddRectFilled(ImVec2(o.x - 6, o.y - 6), ImVec2(o.x + 6, o.y + 6), act ? IM_COL32(255, 230, 90, 255) : IM_COL32(240, 240, 240, 230));
    dl->AddRect(ImVec2(o.x - 6, o.y - 6), ImVec2(o.x + 6, o.y + 6), IM_COL32(0, 0, 0, 200));
    if (test) dl->AddCircle(m, 6.0f, IM_COL32(255, 0, 255, 255), 12, 2.0f);   /* (the scripted mouse) */
}

/* keyboard: arrows on the floor plane as the camera shows it, Page Up/Down up and down, Q/E turn; Shift: more */
static void keys(Placement* p)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !g_placer.gz_ok) return;
    float step = io.KeyShift ? 1.0f : 0.1f;
    int turn = io.KeyShift ? 2731 : 182;   /* 15 / 1 degree */
    ImVec2 o = W(g_placer.gz_o), ax = W(g_placer.gz_axis[0]), az = W(g_placer.gz_axis[2]);
    ImVec2 vx(ax.x - o.x, ax.y - o.y), vz(az.x - o.x, az.y - o.y);
    struct { ImGuiKey key; float sx, sy; } dirs[4] = { { ImGuiKey_RightArrow, 1, 0 }, { ImGuiKey_LeftArrow, -1, 0 }, { ImGuiKey_UpArrow, 0, -1 }, { ImGuiKey_DownArrow, 0, 1 } };
    for (auto& d : dirs) {
        if (!ImGui::IsKeyPressed(d.key, true)) continue;
        float lx = sqrtf(vx.x * vx.x + vx.y * vx.y), lz = sqrtf(vz.x * vz.x + vz.y * vz.y);   /* (not hypotf: GLIBC_2.35 on i686, past the release floor 2.34) */
        float dx = lx > 0 ? (vx.x * d.sx + vx.y * d.sy) / lx : 0, dz = lz > 0 ? (vz.x * d.sx + vz.y * d.sy) / lz : 0;
        if (fabsf(dx) >= fabsf(dz)) p->pos[0] += dx > 0 ? step : -step;
        else p->pos[2] += dz > 0 ? step : -step;
        changed();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) { p->pos[1] += step; changed(); }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) { p->pos[1] -= step; changed(); }
    if (ImGui::IsKeyPressed(ImGuiKey_Q, true)) { p->ang[1] = (p->ang[1] + turn) & 0xFFFF; changed(); }
    if (ImGui::IsKeyPressed(ImGuiKey_E, true)) { p->ang[1] = (p->ang[1] - turn) & 0xFFFF; changed(); }
}

/* ---------------------------------------------------------------- the F2 section */
static void export_now(void)
{
    char pth[1100];
    int cap = PL_MAX * 256 + 4096;
    char* buf = (char*)SDL_malloc(cap);
    path(pth, sizeof pth);
    if (!buf) return;
    placer_format(buf, cap);
    SDL_SetClipboardText(buf);
    SDL_free(buf);
    if (placer_save(pth)) snprintf(msg_buf, sizeof msg_buf, "Could not write %s", pth);
    else snprintf(msg_buf, sizeof msg_buf, "Saved to %s and copied to the clipboard.", pth);
    msg = msg_buf;
    dirty = 0;
}

/* a typewriter's text: no '"' (the file quotes it), at most PL_TEXT_LEN letters */
static int text_filter(ImGuiInputTextCallbackData* d)
{
    int n = 0;
    for (const char* s = (const char*)d->UserData; *s; s++) n += ((unsigned char)*s & 0xC0) != 0x80;
    return d->EventChar == '"' || d->EventChar < 0x20 || n >= PL_TEXT_LEN;
}

/* its own window, shown with the F2 window */
static void placer_window(void)
{
    static int type;
    char b[160];
    int n_here = 0, n_all = 0;
    for (int i = 0; i < PL_MAX; i++) if (g_placer.p[i].used) { n_all++; n_here += here(&g_placer.p[i]); }
    ImGui::SetNextWindowPos(ImVec2(10, 20), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Object Placer (F2, --dev)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); return; }
    if (!g_placer.in_room) ImGui::TextDisabled("Not in a room.");
    if (type < 0 || type >= placer_types()) type = 0;
    if (ImGui::BeginCombo("Model", placer_type_name(type))) {
        for (int t = 0; t < placer_types(); t++) {
            if (ImGui::Selectable(placer_type_name(t), t == type)) type = t;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", placer_type_source(t, b, sizeof b));
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!g_placer.in_room);
    if (ImGui::Button("Place in Front")) {
        float a = g_placer.pl_ay * 6.2831853f / 65536.0f, d = 12.0f;
        float pos[3] = { g_placer.pl_pos[0] - d * sinf(a), g_placer.pl_pos[1], g_placer.pl_pos[2] - d * cosf(a) };
        add(type, pos, g_placer.pl_ay + 0x8000);   /* facing the player */
    }
    ImGui::SameLine();
    if (ImGui::Button("Place at Player")) add(type, g_placer.pl_pos, g_placer.pl_ay);
    ImGui::EndDisabled();
    ImGui::Text("Room %d/%02d case %d: %d here, %d in all", g_placer.stg, g_placer.rom, g_placer.rcase, n_here, n_all);

    if (ImGui::BeginListBox("##here", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 5.5f))) {
        for (int i = 0; i < PL_MAX; i++) {
            const Placement* p = &g_placer.p[i];
            if (!here(p)) continue;
            snprintf(b, sizeof b, "%3d  %-16s %7.1f %6.1f %7.1f  %4.0f deg##%d", i, placer_type_name(p->type), p->pos[0], p->pos[1], p->pos[2], to_deg(p->ang[1]), i);
            if (ImGui::Selectable(b, g_placer.sel == i)) g_placer.sel = g_placer.sel == i ? -1 : i;
        }
        ImGui::EndListBox();
    }
    Placement* p = selected();
    if (p && here(p)) {
        float deg[3] = { to_deg(p->ang[0]), to_deg(p->ang[1]), to_deg(p->ang[2]) };
        if (ImGui::DragFloat3("Position", p->pos, 0.05f, -2000.0f, 2000.0f, "%.2f")) changed();
        if (ImGui::DragFloat3("Turn (x y z deg)", deg, 0.5f, -180.0f, 180.0f, "%.1f")) {
            for (int k = 0; k < 3; k++) p->ang[k] = to_ang(deg[k]);
            changed();
        }
        if (p->type == placer_type_find("typewriter")) {   /* the save list's location for saves made there */
            if (ImGui::InputTextWithHint("Save Text", "the room's name", p->text, sizeof p->text, ImGuiInputTextFlags_CallbackCharFilter, text_filter, p->text)) {
                placer_text_clean(p->text);
                changed();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The location the save list shows for saves made at this typewriter\n"
                                  "(at most %d letters; empty: the room's name). Letters the game's font\n"
                                  "lacks show as the plain letter or '?'.", PL_TEXT_LEN);
        }
        ImGui::TextDisabled("ax %d  ay %d  az %d   floor %d   placed as %s, case %d", p->ang[0], p->ang[1], p->ang[2], p->flr,
                            p->ply == 1 ? "Chris" : p->ply == 2 ? "Steve" : p->ply == 3 ? "Wesker" : "Claire", p->rcase);
        if (ImGui::Button("Snap to Floor")) __atomic_or_fetch(&g_placer.request, PL_SNAP, __ATOMIC_ACQ_REL);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Onto the floor level or the top of a box (desk, crate) below it.");
        ImGui::SameLine();
        if (ImGui::Button("To Player")) { memcpy(p->pos, g_placer.pl_pos, sizeof(p->pos)); changed(); }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            Placement q = *p;
            q.pos[0] += 3.0f;
            int k = add(q.type, q.pos, q.ang[1]);
            if (k >= 0) { g_placer.p[k].ang[0] = q.ang[0]; g_placer.p[k].ang[2] = q.ang[2]; g_placer.p[k].pos[1] = q.pos[1]; memcpy(g_placer.p[k].text, q.text, PL_TEXT); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) { p->used = 0; g_placer.sel = -1; changed(); }
        bool r = g_placer.rings;
        if (ImGui::Checkbox("Pitch and Roll Rings", &r)) g_placer.rings = r;
        keys(p);
    }
    ImGui::TextDisabled("Drag an arrow to move along it, the square on the floor plane, the green\n"
                        "ring to turn (Ctrl: 0.5 units / 15 degrees). Keys: arrows, Page Up/Down, Q/E\n"
                        "(Shift: more). Placed models are drawn only; they are not saved in the game.");
    if (n_all > n_here && ImGui::TreeNode("Other rooms")) {
        int done[PL_MAX], nd = 0;
        for (int i = 0; i < PL_MAX; i++) {
            const Placement* q = &g_placer.p[i];
            if (!q->used || here(q)) continue;
            int key = q->stg * 100 + q->rom, j, cnt = 0;
            for (j = 0; j < nd && done[j] != key; j++);
            if (j < nd) continue;
            done[nd++] = key;
            for (j = 0; j < PL_MAX; j++) cnt += g_placer.p[j].used && g_placer.p[j].stg == q->stg && g_placer.p[j].rom == q->rom;
            snprintf(b, sizeof b, "Go##%d", key);
            if (ImGui::SmallButton(b)) {   /* the F2 teleport, entry 0 */
                g_cheat.tp_stg = q->stg; g_cheat.tp_rom = q->rom; g_cheat.tp_pos = 0;
                __atomic_or_fetch(&g_cheat.request, CHEAT_TELEPORT, __ATOMIC_ACQ_REL);
            }
            ImGui::SameLine();
            ImGui::Text("room %d/%02d: %d", q->stg, q->rom, cnt);
        }
        ImGui::TreePop();
    }
    if (ImGui::Button("Export")) export_now();
    if (ImGui::IsItemHovered()) {
        char pth[1100];
        path(pth, sizeof pth);
        ImGui::SetTooltip("Writes %s (also saved by itself)\nand copies its text to the clipboard.", pth);
    }
    const char* r = msg ? msg : g_placer.result;
    if (r) { ImGui::SameLine(); ImGui::TextWrapped("%s", r); }
    ImGui::End();
}

/* every overlay frame with --dev, before anything is drawn: the file loaded once and saved half a second
   after the last change */
extern "C" void placer_poll(int f2_open)
{
    static bool loaded;
    char pth[1100];
    if (!g_dev_tools) return;
    path(pth, sizeof pth);
    if (!loaded) {
        int n = placer_load(pth);
        loaded = true;
        if (n >= 0) SDL_Log("placer: %d entries from %s", n, pth);
    }
    g_placer.gizmo = f2_open && selected();
    if (__atomic_exchange_n(&g_placer.edited, 0, __ATOMIC_ACQ_REL)) changed();
    if (!f2_open) drag = -1;
    if (dirty && drag < 0 && SDL_GetTicks() - dirty > 500) {
        if (placer_save(pth)) { snprintf(msg_buf, sizeof msg_buf, "Could not write %s", pth); msg = msg_buf; }
        dirty = 0;
    }
}

/* the F2 window is open (within the ImGui frame): the placer's window and the selected entry's gizmo */
extern "C" void placer_overlay(void)
{
    if (!g_dev_tools) return;
    placer_window();
    gizmo();
}
