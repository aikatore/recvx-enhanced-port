/* --dev cutscene log: the list and its text (devcine.h, docs/DEV_TOOLS.md). Pure; ctest "devcine". */
#include "devcine.h"
#include "fileio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_devcine_show = 1;

static struct {
    volatile int lock;
    DevCine e[DC_MAX];                  /* ring: e[(head + i) % DC_MAX], oldest first */
    int head, n, open;
    unsigned no;
} lg;

static void lock(void) { while (__atomic_exchange_n(&lg.lock, 1, __ATOMIC_ACQUIRE)) {} }
static void unlock(void) { __atomic_store_n(&lg.lock, 0, __ATOMIC_RELEASE); }
static DevCine* last(void) { return lg.n ? &lg.e[(lg.head + lg.n - 1) % DC_MAX] : NULL; }

void devcine_begin(const DevCine* in)
{
    DevCine* l;
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);   /* (game thread only) */
    lock();
    l = last();
    if (l && l->kind == in->kind && l->room == in->room && !strcmp(l->key, in->key)) l->repeat++;   /* the same again: counted, its first start kept */
    else {
        if (lg.n == DC_MAX) { lg.head = (lg.head + 1) % DC_MAX; lg.n--; }
        l = &lg.e[(lg.head + lg.n++) % DC_MAX];
        *l = *in;
        l->no = ++lg.no;
        l->repeat = 1;
        if (tm) strftime(l->when, sizeof l->when, "%H:%M:%S", tm);
        else snprintf(l->when, sizeof l->when, "?");
    }
    lg.open = 1;
    unlock();
}

void devcine_update(int frames, const char* what, const char* flags, const char* cast)
{
    DevCine* l;
    lock();
    if (lg.open && (l = last())) {
        l->frames = frames;
        if (what) snprintf(l->what, sizeof l->what, "%s", what);
        if (flags) snprintf(l->flags, sizeof l->flags, "%s", flags);
        if (cast) snprintf(l->cast, sizeof l->cast, "%s", cast);
    }
    unlock();
}

void devcine_end(void) { lock(); lg.open = 0; unlock(); }

int devcine_snapshot(DevCine* out, int* open)
{
    int i, n;
    lock();
    n = lg.n;
    for (i = 0; i < n; i++) out[i] = lg.e[(lg.head + i) % DC_MAX];
    if (open) *open = lg.open && n;
    unlock();
    return n;
}

void devcine_clear(void)   /* (the scene playing now stays) */
{
    lock();
    if (lg.open && lg.n) { lg.e[0] = *last(); lg.n = 1; }
    else lg.n = 0;
    lg.head = 0;
    unlock();
}

const char* devcine_who(int ply)
{
    static const char* const who[4] = { "Claire", "Chris", "Steve", "Wesker" };
    return ply >= 0 && ply < 4 ? who[ply] : "?";
}

int devcine_format(const DevCine* e, int full, char* buf, int n)
{
    char rep[16] = "";
    int r;
    if (e->repeat > 1) snprintf(rep, sizeof rep, " x%d", e->repeat);
    if (!full)
        r = snprintf(buf, (size_t)n, "#%u RM_%04d %s  %.1f s%s  %s", e->no, e->room, e->what, e->frames / 30.0, rep, devcine_who(e->ply));
    else
        r = snprintf(buf, (size_t)n, "#%u %s RM_%04d (stage %d room %02d case %d) | %s %s | %s | %.1f s%s | player %s | %s", e->no, e->when, e->room,
                     e->room / 1000, e->room / 10 % 100, e->room % 10, e->kind == DC_FMV ? "movie" : "event", e->what, e->flags[0] ? e->flags : "-",
                     e->frames / 30.0, rep, devcine_who(e->ply), e->cast[0] ? e->cast : "no cast");
    return r < 0 ? 0 : r >= n ? n - 1 : r;
}

int devcine_text(char* buf, int n)
{
    DevCine* s = malloc(sizeof(DevCine) * DC_MAX);
    int k, cnt = s ? devcine_snapshot(s, NULL) : 0, len = 0;
    if (n > 0) buf[0] = 0;
    for (k = 0; k < cnt && len < n - 1; k++) {
        len += devcine_format(&s[k], 1, buf + len, n - len);
        if (len < n - 1) { buf[len++] = '\n'; buf[len] = 0; }
    }
    free(s);
    return len;
}

int devcine_export(const char* path)
{
    char* text = malloc(DEVCINE_TEXT), stamp[32] = "?";
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);
    int len = text ? devcine_text(text, DEVCINE_TEXT) : 0, lines = 0, i, ok;
    FILE* f = text ? pc_fopen(path, "a") : NULL;
    if (!f) { free(text); return -1; }
    if (tm) strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", tm);
    for (i = 0; i < len; i++) lines += text[i] == '\n';
    fprintf(f, "# recvx cutscene log, exported %s: %d %s\n%s", stamp, lines, lines == 1 ? "scene" : "scenes", text);
    ok = !ferror(f);
    free(text);
    return fclose(f) == 0 && ok ? lines : -1;
}
