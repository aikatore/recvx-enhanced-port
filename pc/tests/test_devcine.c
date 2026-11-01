/* The --dev cutscene log's list (pc/src/host/devcine.c): repeats counted, the oldest dropped past DC_MAX, clear,
   the text and the appended export; the scene list's reading of a room file (devcat_room). */
#include "../src/host/devcine.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static DevCine s[DC_MAX];

static void scene(int kind, int room, const char* key, const char* what)
{
    DevCine e;
    memset(&e, 0, sizeof e);
    e.kind = kind; e.room = room; e.ply = 1;
    snprintf(e.key, sizeof e.key, "%s", key);
    snprintf(e.what, sizeof e.what, "%s", what);
    devcine_begin(&e);
}

/* a room file: two records, a message, scd1 starting events 1 and 2 behind two tests (and the skip's ending), event 1 a
   cinema with a camera, Alfred and the message, event 2 a voice line, event 3 a cinema no script starts */
static unsigned char room[0x600];
static int at;
static void b(int n, ...) { va_list a; va_start(a, n); while (n--) room[at++] = (unsigned char)va_arg(a, int); va_end(a); }
static void put32(int o, unsigned v) { room[o] = v & 0xFF; room[o + 1] = v >> 8 & 0xFF; room[o + 2] = v >> 16 & 0xFF; room[o + 3] = v >> 24; }
static const char* who(int id, int ver) { (void)ver; return id == 94 ? "Alfred" : NULL; }
static int scan(void)
{
    enum { R = 0x100, E = 0x200, M = 0x300, B = 0x400 };
    DevScene sc[4];
    char line[400];
    const char* t = "Hi Alfred";
    int ev[6], h, u;
    put32(0x10, R); put32(0x1C, B);
    put32(R + 2 * 4, E); put32(R + 0x80 + 2 * 4, 2); put32(R + 14 * 4, M); put32(R + 0x80 + 12 * 4, 1);
    room[E + 4] = 94; room[E + 0x24 + 4] = 5;
    put32(M + 4, 8);
    for (at = M + 8; *t; t++) b(2, *t - 32, 0);
    b(6, 0x02, 0xFF, '!' - 32, 0, 0xFF, 0xFF);    /* (a wait and its operand, the end) */
    at = B + 24;
    b(2, 0, 0);                                    /* scd0: End */
    ev[1] = at;                                    /* scd1 */
    h = at; b(2, 1, 0);                            /* IfelCk, its skip below */
    b(6, 4, 1, 15, 0, 0, 1);                       /* Ck ev_flg bit 15 is clear */
    b(6, 4, 10, 23, 0, 2, 0);                      /* Ck player at trigger 2 */
    b(6, 5, 4, 3, 0, 0, 0);                        /* Set rm_flg bit 3 */
    b(8, 20, 0, 1, 1, 20, 0, 2, 2);                /* EvtOn 1:1, 2:2 */
    room[h + 1] = (unsigned char)(at - h);
    b(2, 3, 0);                                    /* Endif */
    h = at; b(2, 1, 0);
    b(6, 4, 10, 3, 0, 0, 0);                       /* Ck cb_flg bit 3 is set: the skip */
    b(4, 20, 0, 3, 2);
    room[h + 1] = (unsigned char)(at - h);
    b(4, 3, 0, 0, 0);                              /* Endif, End */
    ev[2] = at; b(2, 255, 0);                      /* event 0 */
    ev[3] = at; b(16, 18, 0, 19, 0, 5, 0, 101, 1, 0, 0, 31, 0, 0, 1, 255, 0);   /* event 1: CineSet, CamSet, LoadWork r0, MessageSet 0 */
    ev[4] = at; b(10, 25, 1, 0, 0, 1, 0, 1, 0, 255, 0);   /* event 2: VoiceOn */
    ev[5] = at; b(4, 18, 0, 255, 0);               /* event 3 */
    put32(B, 24);
    for (h = 1; h < 6; h++) put32(B + 4 * h, (unsigned)(ev[h] - B));
    CHECK(devcat_room(1020, room, sizeof room, who, sc, 4, &u) == 1 && u == 1);
    CHECK(sc[0].room == 1020 && sc[0].from == 1 && sc[0].nev == 2 && sc[0].ev[0] == (1 << 8 | 1) && sc[0].ev[1] == (2 << 8 | 2));
    CHECK(sc[0].nflag == 1 && sc[0].flag[0].set == 1 && sc[0].flag[0].bit == 15 && !sc[0].flag[0].on && sc[0].ply == 0xFF);
    CHECK(sc[0].block == (unsigned)(ev[1] + 14 - B) && sc[0].blen == 14);   /* the then-part: Set and the two EvtOn */
    CHECK(sc[0].shots == 1 && sc[0].voices == 1 && sc[0].lines == 1 && !strcmp(sc[0].who, "Alfred") && !strcmp(sc[0].text, "Hi Alfred"));
    devcat_line(&sc[0], "Palace: Main hall", line, sizeof line);
    CHECK(!strcmp(line, "RM_1020 Palace: Main hall | Alfred's first appearance | ev 1:1, 2:2 | Alfred | 1 shots, 1 lines, 1 voice | \"Hi Alfred\""));
    sc[0].room = 1030;
    devcat_line(&sc[0], "x", line, sizeof line);
    CHECK(strstr(line, "| Scene with Alfred |") && !devcat_short(&sc[0]));
    return 0;
}

int main(int argc, char** argv)
{
    char path[1024], buf[DEVCINE_TEXT], line[600];
    int open, k;
    FILE* f;
    snprintf(path, sizeof path, "%s/test_devcine.txt", argc > 1 ? argv[1] : ".");
    remove(path);
    CHECK(devcine_snapshot(s, &open) == 0 && !open);
    scene(DC_EVENT, 1020, "3:7", "ev 3:7");
    devcine_update(372, NULL, "cinema+event camera", "cast seed 5 per cutscene (scene 1021): r3 Alfred(94)->Tyrant");
    devcine_end();
    scene(DC_EVENT, 1020, "3:7", "ev 3:7");   /* the same right after: a repeat */
    devcine_end();
    scene(DC_FMV, 1020, "MV_005.PSS", "MV_005.PSS (ev 3:8)");
    CHECK(devcine_snapshot(s, &open) == 2 && open);
    CHECK(s[0].no == 1 && s[0].repeat == 2 && s[0].frames == 372 && s[1].no == 2 && s[1].kind == DC_FMV);
    devcine_format(&s[0], 0, line, sizeof line);
    CHECK(!strcmp(line, "#1 RM_1020 ev 3:7  12.4 s x2  Chris"));
    devcine_format(&s[0], 1, line, sizeof line);
    CHECK(strstr(line, "RM_1020 (stage 1 room 02 case 0) | event ev 3:7 | cinema+event camera | 12.4 s x2 | player Chris | cast seed 5 per cutscene (scene 1021): r3 Alfred(94)->Tyrant"));
    devcine_format(&s[1], 1, line, sizeof line);
    CHECK(strstr(line, "| movie MV_005.PSS (ev 3:8) | - | 0.0 s | player Chris | no cast"));
    CHECK(devcine_text(buf, sizeof buf) > 0 && !strncmp(buf, "#1 ", 3) && strstr(buf, "\n#2 ") && buf[strlen(buf) - 1] == '\n');
    CHECK(devcine_export(path) == 2 && devcine_export(path) == 2);   /* appended */
    CHECK((f = fopen(path, "r")) != NULL);
    k = (int)fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[k] = 0;
    CHECK(!strncmp(buf, "# recvx cutscene log, exported ", 31) && strstr(buf, ": 2 scenes\n#1 ") && strstr(strstr(buf, "\n# recvx") + 1, "#2 "));
    for (k = 0; k < DC_MAX + 5; k++) { char key[8]; snprintf(key, sizeof key, "%d", k); scene(DC_EVENT, 2000, key, "ev"); }
    CHECK(devcine_snapshot(s, &open) == DC_MAX && s[0].no == 8 && s[DC_MAX - 1].no == DC_MAX + 7);   /* the oldest dropped */
    devcine_clear();   /* the one playing stays */
    CHECK(devcine_snapshot(s, &open) == 1 && open && s[0].no == DC_MAX + 7);
    devcine_end();
    devcine_clear();
    CHECK(devcine_snapshot(s, &open) == 0 && !open);
    scene(DC_EVENT, 2000, "x", "ev");
    CHECK(devcine_snapshot(s, &open) == 1 && s[0].no == DC_MAX + 8);   /* (numbers count on) */
    remove(path);
    if (scan()) return 1;
    printf("devcine: ok\n");
    return 0;
}
