/* Host side of the co-op lobby screen (pc/glue/lobby_glue.c): keyboard text for its text
   fields, the clipboard, recvx.ini and the save files (memory card or native). Main thread except
   lobby_read_save / lobby_settings_changed. */
#include "ps2_platform.h"
#include "lobby.h"
#include "settings.h"
#include "savestore.h"
#include "coop.h"
#include "net/net.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LobbyIO g_lobby_io;
uint64_t gs_frame_count(void);

static void push(int k)
{
    unsigned int h = g_lobby_io.head;
    if (h - g_lobby_io.tail >= 64) return;   /* full: the game thread is behind */
    g_lobby_io.keys[h & 63] = k;
    __atomic_store_n(&g_lobby_io.head, h + 1, __ATOMIC_RELEASE);
}

static void push_text(const char* s)
{
    for (; *s; s++)
        if (*s >= 32 && *s < 127) push(*s);   /* the game's font has no other characters */
}

void lobby_io_event(const void* e)
{
    const SDL_Event* ev = (const SDL_Event*)e;
    if (!g_lobby_io.text_on) {
        if (g_lobby_io.list_on && ev->type == SDL_EVENT_KEY_DOWN)
            switch (ev->key.key) {
            case SDLK_PAGEUP: push(LK_PGUP); break;
            case SDLK_PAGEDOWN: push(LK_PGDN); break;
            case SDLK_HOME: push(LK_HOME); break;
            case SDLK_END: push(LK_END); break;
            }
        return;
    }
    if (ev->type == SDL_EVENT_TEXT_INPUT) push_text(ev->text.text);
    else if (ev->type == SDL_EVENT_KEY_DOWN) {
        switch (ev->key.key) {
        case SDLK_RETURN: case SDLK_KP_ENTER: push(LK_ENTER); break;
        case SDLK_ESCAPE: push(LK_ESC); break;
        case SDLK_BACKSPACE: push(LK_BACK); break;
        case SDLK_DELETE: push(LK_DEL); break;
        case SDLK_LEFT: push(LK_LEFT); break;
        case SDLK_RIGHT: push(LK_RIGHT); break;
        case SDLK_HOME: push(LK_HOME); break;
        case SDLK_END: push(LK_END); break;
        case SDLK_V:
            if (ev->key.mod & SDL_KMOD_CTRL) {
                char* t = SDL_GetClipboardText();
                if (t) { push_text(t); SDL_free(t); }
            }
            break;
        }
    }
}

/* Test aids: RECVX_LOBBY_KEYS="frame:text,..." types text into the field being edited at that
   presented frame; '|' = Enter, '`' = Escape, '<' = Backspace (none of them is allowed in a field).
   RECVX_CLIPBOARD_TEST="text" puts text on the clipboard at the start. */
static void scripted(void)
{
    static const char* p;
    static int checked;
    if (!checked++) {
        const char* c = SDL_getenv("RECVX_CLIPBOARD_TEST");
        if (c) SDL_SetClipboardText(c);
        p = SDL_getenv("RECVX_LOBBY_KEYS");
    }
    while (p && *p && strtoul(p, NULL, 10) <= gs_frame_count()) {
        char* e;
        strtoul(p, &e, 10);
        if (*e++ != ':') { p = NULL; break; }
        for (; *e && *e != ','; e++)
            push(*e == '|' ? LK_ENTER : *e == '`' ? LK_ESC : *e == '<' ? LK_BACK : *e);
        p = *e == ',' ? e + 1 : e;
    }
}

void lobby_io_frame(void* window)
{
    static int started;
    int on = g_lobby_io.text_on;
    if (on != started) {
        if (on) SDL_StartTextInput((SDL_Window*)window); else SDL_StopTextInput((SDL_Window*)window);
        started = on;
    }
    if (g_lobby_io.clip_req) {
        SDL_SetClipboardText(g_lobby_io.clip);
        g_lobby_io.clip_req = 0;
    }
    if (g_lobby_io.paste_req == 1) {   /* (a longer text is cut; its reader then reports the wrong length) */
        char* t = SDL_GetClipboardText();
        SDL_snprintf(g_lobby_io.paste, sizeof(g_lobby_io.paste), "%s", t ? t : "");
        SDL_free(t);
        __atomic_store_n(&g_lobby_io.paste_req, 2, __ATOMIC_RELEASE);
    }
    scripted();
}

/* ---------------------------------------------------------------- saves (lobby.h) */
int save_native(void) { return g_config.save_system == 0; }

const char* save_native_dir(void)
{
    static char dir[1100];
    if (!dir[0]) {
        snprintf(dir, sizeof(dir), "%s/saves", g_config.save_dir);
        if (!SDL_GetPathInfo(dir, NULL)) {   /* the first native start: the memory card's saves come along (copies) */
            int n;
            SDL_CreateDirectory(dir);
            n = savestore_import_card(g_config.save_dir, dir);
            if (n) host_log("saves: native save files in %s; copied %d memory card save%s (card0 keeps them)\n", dir, n, n == 1 ? "" : "s");
            else host_log("saves: native save files in %s\n", dir);
        }
    }
    return dir;
}

static void card_path(char* p, int n, int slot) { SDL_snprintf(p, n, "%s/card0/BASLUS-20184/SAVEDATA-%02d", g_config.save_dir, slot); }

/* native: the file of save number slot + 1 */
static int native_path(char* p, int n, int slot)
{
    char file[256];
    if (slot < 0 || slot >= 999999999 || !savestore_find(save_native_dir(), slot + 1, file, sizeof(file))) return 0;
    SDL_snprintf(p, n, "%s/%s", save_native_dir(), file);
    return 1;
}

int lobby_save_list(LobbySave** out)
{
    LobbySave* l;
    int i, n;
    *out = NULL;
    if (save_native()) {
        SaveEntry* e;
        n = savestore_list(save_native_dir(), &e);   /* sorted, at most SAVESTORE_MAX */
        int k = 0;
        if (n && (l = malloc(sizeof(*l) * n))) {
            for (i = 0; i < n; i++)
                if (!k || l[k - 1].slot != e[i].number - 1) {   /* two files of one number: the first */
                    l[k].slot = e[i].number - 1;
                    memcpy(l[k].file, e[i].file, sizeof(l[k].file));
                    k++;
                }
            *out = l;
        }
        free(e);
        return k;
    }
    if (!(l = malloc(sizeof(*l) * 15))) return 0;
    for (i = 0; i < 15; i++) { l[i].slot = i; l[i].file[0] = 0; }   /* lobby_read_listed reads the slot */
    *out = l;
    return 15;
}

int lobby_read_listed(const LobbySave* s, unsigned char* out)
{
    char p[1400];
    if (!save_native()) return lobby_read_save(s->slot, out);
    SDL_snprintf(p, sizeof(p), "%s/%s", save_native_dir(), s->file);
    return savestore_read(p, out);
}

int lobby_read_save(int slot, unsigned char* out)
{
    char p[1200];
    FILE* f;
    unsigned int sum = 0, ck;
    int i, n;
    if (save_native()) return native_path(p, sizeof(p), slot) && savestore_read(p, out);   /* a folder scan: lists read lobby_read_listed */
    if (slot < 0 || slot > 14) return 0;
    card_path(p, sizeof(p), slot);
    if (!(f = pc_fopen(p, "rb"))) return 0;
    n = (int)fread(out, 1, LOBBY_SAVE_BYTES, f);
    fclose(f);
    if (n != LOBBY_SAVE_BYTES) return 0;
    for (i = 0; i < LOBBY_SAVE_BYTES - 4; i++) sum += out[i];   /* MemoryCardGetSum */
    memcpy(&ck, out + LOBBY_SAVE_BYTES - 4, 4);
    return ck == sum && (out[0] | out[1] | out[2] | out[3]);   /* an empty slot is all zero */
}

int lobby_write_save(int slot, const void* region, int size)
{
    unsigned char b[LOBBY_SAVE_BYTES] = { 0 };
    unsigned int sum = 0;
    char p[1200];
    FILE* f;
    int i, ok;
    if (slot < 0 || slot > (save_native() ? 999999998 : 14) || size != LOBBY_SAVE_BYTES - 4) return 0;
    memcpy(b, region, size);
    for (i = 0; i < size; i++) sum += b[i];
    memcpy(b + size, &sum, 4);
    if (save_native()) {
        char file[256];
        return savestore_write(save_native_dir(), slot + 1, b, savestore_find(save_native_dir(), slot + 1, file, sizeof(file)) ? file : NULL) != 0;
    }
    SDL_snprintf(p, sizeof(p), "%s/card0/BASLUS-20184", g_config.save_dir);
    SDL_CreateDirectory(p);
    card_path(p, sizeof(p), slot);
    if (!(f = pc_fopen(p, "wb"))) return 0;
    ok = fwrite(b, 1, sizeof(b), f) == sizeof(b);
    fclose(f);
    return ok;
}

/* co-op: the partners' saved items that go with save `slot` (docs/COOP.md "Saved partner
   inventories"): <save_dir>/coop-partners-nn.bin, outside the card directory (the game lists it);
   native: <save_dir>/saves/NNN.coop-partners.bin */
static int partners_path(char* p, int n, int slot)
{
    if (save_native()) {
        if (slot < 0 || slot >= 999999999) return 0;
        SDL_snprintf(p, n, "%s/" SAVE_PARTNERS_FMT, save_native_dir(), slot + 1);
        return 1;
    }
    if (slot < 0 || slot > 14) return 0;
    SDL_snprintf(p, n, "%s/coop-partners-%02d.bin", g_config.save_dir, slot);
    return 1;
}

int lobby_read_partners(int slot, void* buf, int cap)
{
    char p[1200];
    FILE* f;
    int n;
    if (!partners_path(p, sizeof(p), slot)) return 0;
    if (!(f = pc_fopen(p, "rb"))) return 0;
    n = (int)fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

typedef struct { const void* b; int n; } Blob;
static int put_blob(FILE* f, void* ud) { const Blob* x = ud; return fwrite(x->b, 1, x->n, f) == (size_t)x->n ? 0 : -1; }

int lobby_write_partners(int slot, const void* buf, int n)
{
    char p[1200];
    Blob x = { buf, n };
    if (!partners_path(p, sizeof(p), slot)) return 0;
    if (n <= 0) { SDL_RemovePath(p); return 1; }   /* none: no stale file next to a new save */
    return pc_save_file(p, "wb", put_blob, &x) == 0;
}

void lobby_settings_changed(void) { settings_save_later(); }   /* the main thread writes recvx.ini */

const char* coop_shown(const char* text)
{
    static __thread char ring[4][512];
    static __thread int k;
    char join[sizeof(g_coop.join)], srv[sizeof(g_coop.server)];
    const char* hosts[] = { join, srv, NULL };
    if (!g_coop.hide_ip || !text) return text;
    memcpy(join, (const void*)g_coop.join, sizeof(join));   /* another thread may be editing them: bounded copies */
    memcpy(srv, (const void*)g_coop.server, sizeof(srv));
    join[sizeof(join) - 1] = srv[sizeof(srv) - 1] = 0;
    k = (k + 1) & 3;
    return net_mask_addrs(text, hosts, ring[k], sizeof(ring[k]));
}
