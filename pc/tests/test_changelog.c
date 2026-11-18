/* ctest "changelog": the WHAT'S NEW screen's lines (src/host/changelog.c) from a made-up changelog and from the real
   CHANGELOG.md: headings, bullets and their wrapped lines joined, bold toggles, links and code as text, and nothing the
   game's font draws as a picture.   test_changelog <CHANGELOG.md> */
#include "../src/host/changelog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int parse(const char* md, ClLine* l, int max, char* buf, size_t bufn) { return changelog_parse(md, strlen(md), buf, bufn, l, max); }

int main(int argc, char** argv)
{
    static ClLine l[8192];
    static char buf[1 << 20];
    const char* md = "# Changelog\n\n## Unreleased\n\n> [!NOTE]\n> A note.\n\n### Changes\n\n- **Bold title**: see\n  [USER_GUIDE.md](docs/USER_GUIDE.md#x),\n"
                     "  `ini_key = 1` & EXTRA > IMPORT, <partner>.\n  - **Nested**: *Wild* item\r\n    continued.\n\n## 0.8.0 (2026-11-15)\n\nFirst release.\nMore.\n";
    int n = parse(md, l, 64, buf, sizeof(buf)), sections = 0, items = 0;
    CHECK(n == 7, "lines: %d", n);
    CHECK(l[0].kind == CL_VERSION && !l[0].text[0], "Unreleased: %d \"%s\"", l[0].kind, l[0].text);
    CHECK(l[1].kind == CL_TEXT && !strcmp(l[1].text, "A note."), "note: \"%s\"", l[1].text);
    CHECK(l[2].kind == CL_HEAD && !strcmp(l[2].text, "Changes"), "head: \"%s\"", l[2].text);
    CHECK(l[3].kind == CL_ITEM && !l[3].depth && !strcmp(l[3].text, "\001Bold title\001: see USER_GUIDE.md, ini_key: 1 and EXTRA, IMPORT, partner."),
          "item: \"%s\"", l[3].text);
    CHECK(l[4].kind == CL_ITEM && l[4].depth == 1 && !strcmp(l[4].text, "\001Nested\001: Wild item continued."), "nested: \"%s\"", l[4].text);
    CHECK(l[5].kind == CL_VERSION && !strcmp(l[5].text, "0.8.0 (2026-11-15)"), "version: \"%s\"", l[5].text);
    CHECK(l[6].kind == CL_TEXT && !strcmp(l[6].text, "First release. More."), "text: \"%s\"", l[6].text);
    CHECK(parse(md, l, 3, buf, sizeof(buf)) == 3, "max lines");
    CHECK(parse(md, l, 64, buf, 40) == 7 && strlen(buf + 1) < 40, "small buffer");

    if (argc > 1) {   /* the real one */
        FILE* f = fopen(argv[1], "rb");
        static char text[1 << 20];
        size_t len = f ? fread(text, 1, sizeof(text) - 1, f) : 0;
        if (f) fclose(f);
        CHECK(len > 1000, "cannot read %s", argv[1]);
        n = changelog_parse(text, len, buf, sizeof(buf), l, 8192);
        for (int i = 0; i < n; i++) {
            int bold = 0;
            sections += l[i].kind == CL_VERSION;
            items += l[i].kind == CL_ITEM;
            for (const char* s = l[i].text; *s; s++) bold ^= *s == CL_BOLD;
            CHECK(!strpbrk(l[i].text, "&*#<>=@[]`{}|\\^$\t"), "a picture or markup: \"%s\"", l[i].text);
            CHECK(!bold, "bold left open: \"%s\"", l[i].text);
            CHECK(l[i].kind == CL_VERSION || l[i].text[0], "an empty line %d", i);
        }
        CHECK(sections >= 10 && items >= 100, "%d sections, %d items", sections, items);
        CHECK(n && l[0].kind == CL_VERSION, "starts with a version");
    }
    printf(fails ? "changelog: %d failed\n" : "changelog: ok\n", fails);
    return fails != 0;
}
