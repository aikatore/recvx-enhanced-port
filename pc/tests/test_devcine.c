/* The --dev cutscene log's list (pc/src/host/devcine.c): repeats counted, the oldest dropped past DC_MAX, clear,
   the text and the appended export. */
#include "../src/host/devcine.h"
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
    printf("devcine: ok\n");
    return 0;
}
