/*
 * libmc on the host: memory card slot 1 is a directory (<save_dir>/card0), slot 2 another
 * (<save_dir>/card1). Commands complete immediately; sceMcSync reports them.
 */
#include "ps2_platform.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define MC_FREE_CLUSTERS 8000   /* KB a card holds; what its folder's files take is used */

static int last_cmd, last_result, pending;
static char curdir[2][256] = { "/", "/" };   /* per card, as libmc keeps them */
static FILE* fds[16];
static signed char fd_save[16];   /* the game's save file open on it: lobby.h slot (card 2: 15 + nn), -1 another file */
static unsigned char fd_wrote[16];
void coop_save_written(int slot);   /* pc/glue/coop_glue.c: the host's saved partner inventories go with the save */
static int save_slot(const char* name)
{
    const char* s = strstr(name, "SAVEDATA-");
    return s && s[9] >= '0' && s[9] <= '9' && s[10] >= '0' && s[10] <= '9' && !s[11] ? (s[9] - '0') * 10 + s[10] - '0' : -1;
}
static int card_seen[2];
#define CARD(port) ((port) ? 1 : 0)

static const char* trace_arg;   /* RECVX_TRACE_MC: log every card command */
static int trace_card = -1;     /* its card's folder, -1 a file descriptor's command */
/* libmc accepts one command at a time: until its result is collected with sceMcSync, new
   requests fail (the game's per-frame card polling then retries instead of stealing the
   result of a save in progress). */
#define MC_BUSY() do { if (pending) return -1; } while (0)

static void done(int cmd, int result)
{
    static int on = -1;
    if (on < 0) on = SDL_getenv("RECVX_TRACE_MC") != NULL;
    if (on) {
        char c[8] = "";
        if (trace_card >= 0) SDL_snprintf(c, sizeof(c), "card%d ", trace_card);
        printf("mc: cmd %02x %s%s -> %d\n", cmd, c, trace_arg ? trace_arg : "", result);
    }
    trace_arg = NULL; trace_card = -1;
    last_cmd = cmd; last_result = result; pending = 1;
}

static void host_path(int port, const char* name, char* out, size_t n)
{
    char rel[512];
    const char* cur = curdir[CARD(port)];
    trace_card = CARD(port);
    if (name[0] == '/') SDL_strlcpy(rel, name, sizeof(rel));
    else SDL_snprintf(rel, sizeof(rel), "%s%s%s", cur, cur[strlen(cur) - 1] == '/' ? "" : "/", name);
    SDL_snprintf(out, n, "%s/card%d%s", g_config.save_dir, CARD(port), rel);
}

static void ensure_card(int port)
{
    char p[1100];
    SDL_snprintf(p, sizeof(p), "%s/card%d", g_config.save_dir, CARD(port));
    SDL_CreateDirectory(p);   /* SDL: UTF-8 paths on Windows too, parents included */
}

/* KB the files in dir take, 1 KB clusters and one per folder (all the card's folder holds, other games' saves too) */
static SDL_EnumerationResult SDLCALL used_entry(void* ud, const char* dir, const char* name)
{
    char full[1400];
    SDL_PathInfo st;
    SDL_snprintf(full, sizeof(full), "%s%s", dir, name);   /* SDL's dir ends in a separator */
    if (!SDL_GetPathInfo(full, &st)) return SDL_ENUM_CONTINUE;
    if (st.type == SDL_PATHTYPE_DIRECTORY) { *(Sint64*)ud += 1; SDL_EnumerateDirectory(full, used_entry, ud); }
    else *(Sint64*)ud += (Sint64)((st.size + 1023) / 1024);
    return SDL_ENUM_CONTINUE;
}

int save_native(void);   /* lobby.h: native save files, the card folders are made only when the game uses them */
int sceMcInit(void) { if (!save_native()) { ensure_card(0); ensure_card(1); } return 0; }

int sceMcGetInfo(int port, int slot, int* type, int* free_, int* format)
{ MC_BUSY();
    (void)slot;
    if (port != 0 && port != 1) { if (type) *type = 0; done(1, -10 /* no card */); return 0; }
    if (type) *type = 2;           /* PS2 memory card */
    if (free_) {
        char p[1100];
        Sint64 used = 0;
        host_path(port, "/", p, sizeof(p));
        SDL_EnumerateDirectory(p, used_entry, &used);
        *free_ = used < MC_FREE_CLUSTERS ? MC_FREE_CLUSTERS - (int)used : 0;
    }
    if (format) *format = 1;
    trace_card = port;
    done(1, card_seen[port] ? 0 : -1);   /* -1: card (re)inserted since last check */
    card_seen[port] = 1;
    return 0;
}

int sceMcOpen(int port, int slot, const char* name, int mode)
{ MC_BUSY();
    trace_arg = name;
    char p[1100]; const char* m;
    (void)slot;
    host_path(port, name, p, sizeof(p));
    if (mode & 0x200) m = (mode & 3) == 1 ? "rb" : "w+b";   /* CREAT */
    else m = (mode & 3) == 1 ? "rb" : "r+b";
    for (int fd = 0; fd < 16; fd++) if (!fds[fd])   /* libmc descriptors start at 0; the game relies on it */ {
        fds[fd] = pc_fopen(p, m);
        fd_save[fd] = (signed char)(save_slot(name) < 0 ? -1 : save_slot(name) + 15 * CARD(port));
        fd_wrote[fd] = 0;
        done(2, fds[fd] ? fd : -4);
        return 0;
    }
    done(2, -7);
    return 0;
}

int sceMcClose(int fd)
{ MC_BUSY();
    if (fd >= 0 && fd < 16 && fds[fd]) {
        fclose(fds[fd]); fds[fd] = NULL; done(3, 0);
        if (fd_wrote[fd] && fd_save[fd] >= 0) coop_save_written(fd_save[fd]);   /* a save written */
    } else done(3, -4);
    return 0;
}
int sceMcSeek(int fd, int off, int whence) { MC_BUSY(); if (fd >= 0 && fd < 16 && fds[fd]) { fseek(fds[fd], off, whence); done(4, (int)ftell(fds[fd])); } else done(4, -4); return 0; }
int sceMcRead(int fd, void* buf, int size)  { MC_BUSY(); done(5, (fd >= 0 && fd < 16 && fds[fd]) ? (int)fread(buf, 1, (size_t)size, fds[fd]) : -4); return 0; }
int sceMcWrite(int fd, const void* buf, int size)
{ MC_BUSY();
    if (fd >= 0 && fd < 16 && fds[fd]) { int n = (int)fwrite(buf, 1, (size_t)size, fds[fd]); fflush(fds[fd]); fd_wrote[fd] = 1; done(6, n); }
    else done(6, -4);
    return 0;
}

int sceMcMkdir(int port, int slot, const char* name)
{ MC_BUSY();
    trace_arg = name;
    char p[1100];
    (void)slot;
    host_path(port, name, p, sizeof(p));
    if (SDL_GetPathInfo(p, NULL)) { done(0x0b, -4); return 0; }
    done(0x0b, SDL_CreateDirectory(p) ? 0 : -4);
    return 0;
}

int sceMcChdir(int port, int slot, const char* newdir, char* olddir)
{ MC_BUSY();
    trace_arg = newdir;
    char p[1100];
    char* cd = curdir[CARD(port)];
    (void)slot;
    if (olddir) strcpy(olddir, cd);
    host_path(port, newdir, p, sizeof(p));
    if (!SDL_GetPathInfo(p, NULL)) { done(0x0c, -4); return 0; }
    if (newdir[0] == '/') SDL_strlcpy(cd, newdir, sizeof(curdir[0]));
    else if (!strcmp(newdir, "..")) { char* s = strrchr(cd, '/'); if (s && s != cd) *s = 0; else strcpy(cd, "/"); }
    else { if (cd[strlen(cd) - 1] != '/') SDL_strlcat(cd, "/", sizeof(curdir[0])); SDL_strlcat(cd, newdir, sizeof(curdir[0])); }
    done(0x0c, 0);
    return 0;
}

static void fill_entry(sceMcTblGetDir* e, const char* name, const char* full)
{
    SDL_PathInfo st;
    memset(e, 0, sizeof(*e));
    SDL_strlcpy((char*)e->EntryName, name, sizeof(e->EntryName));
    if (SDL_GetPathInfo(full, &st)) {
        int dir = st.type == SDL_PATHTYPE_DIRECTORY;
        e->FileSizeByte = dir ? 0 : (u_int)st.size;
        e->AttrFile = dir ? 0x8427 : 0x8497;
    }
    e->_Create.Year = e->_Modify.Year = 2001; e->_Create.Month = e->_Modify.Month = 1; e->_Create.Day = e->_Modify.Day = 1;
}

typedef struct { const char* pattern; sceMcTblGetDir* table; int n, max; } DirScan;

static SDL_EnumerationResult SDLCALL dir_entry(void* ud, const char* dir, const char* name)
{
    DirScan* s = ud;
    char full[1400];
    if (s->n >= s->max) return SDL_ENUM_SUCCESS;
    if (name[0] == '.' || (strcmp(s->pattern, "*") && strcmp(s->pattern, name))) return SDL_ENUM_CONTINUE;
    SDL_snprintf(full, sizeof(full), "%s%s", dir, name);   /* SDL's dir ends in a separator */
    fill_entry(&s->table[s->n++], name, full);
    return SDL_ENUM_CONTINUE;
}

int sceMcGetDir(int port, int slot, const char* name, unsigned mode, int maxent, sceMcTblGetDir* table)
{ MC_BUSY();
    trace_arg = name;
    char dir[1100], pattern[256];
    int n = 0;
    const char* slash = strrchr(name, '/');
    (void)slot; (void)mode;
    /* name is "<dir>/<pattern>" or "<pattern>"; pattern is '*' or an exact name */
    if (slash) {
        char d[256]; size_t len = (size_t)(slash - name);
        if (len >= sizeof(d)) len = sizeof(d) - 1;
        memcpy(d, name, len); d[len] = 0;
        host_path(port, len ? d : "/", dir, sizeof(dir));
        SDL_strlcpy(pattern, slash + 1, sizeof(pattern));
    } else {
        host_path(port, ".", dir, sizeof(dir));
        SDL_strlcpy(pattern, name, sizeof(pattern));
    }
    {
        SDL_PathInfo info;
        DirScan s = { pattern, table, 0, maxent };
        if (!SDL_GetPathInfo(dir, &info) || info.type != SDL_PATHTYPE_DIRECTORY) { done(0x0d, -4); return 0; }
        if (!strcmp(pattern, "*") && s.n < maxent) { fill_entry(&table[s.n++], ".", dir); if (s.n < maxent) fill_entry(&table[s.n++], "..", dir); }
        SDL_EnumerateDirectory(dir, dir_entry, &s);
        n = s.n;
    }
    done(0x0d, n);
    return 0;
}

int sceMcDelete(int port, int slot, const char* name)
{ MC_BUSY();
    trace_arg = name;
    char p[1100];
    (void)slot;
    host_path(port, name, p, sizeof(p));
    done(0x0f, SDL_RemovePath(p) ? 0 : -4);
    return 0;
}

int sceMcFormat(int port, int slot)   { MC_BUSY(); (void)slot; ensure_card(port); done(0x10, 0); return 0; }
int sceMcUnformat(int port, int slot) { MC_BUSY(); (void)port; (void)slot; done(0x11, 0); return 0; }

int sceMcSync(int mode, int* cmd, int* result)
{
    (void)mode;
    ps2_poll_interrupts();
    if (!pending) return -1;  /* no command */
    pending = 0;
    if (cmd) *cmd = last_cmd;
    if (result) *result = last_result;
    return 1;                 /* finished */
}
