/* CHANGELOG.md for the WHAT'S NEW screen (changelog.h). */
#include "changelog.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#if defined(_WIN32)
const char* changelog_text(size_t* n)   /* res/recvx.rc: CHANGELOG RCDATA */
{
    HRSRC r = FindResourceA(NULL, "CHANGELOG", MAKEINTRESOURCEA(10));   /* RT_RCDATA */
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    *n = g ? SizeofResource(NULL, r) : 0;
    return g ? (const char*)LockResource(g) : NULL;
}
#elif defined(CHANGELOG_MD)   /* (beside our texts' table, outside the game's address window: tr.c) */
__asm__(".pushsection .trtab,\"a\"\nchangelog_md:\n.incbin \"" CHANGELOG_MD "\"\nchangelog_md_end:\n.popsection");
extern const char changelog_md[], changelog_md_end[];
const char* changelog_text(size_t* n) { *n = (size_t)(changelog_md_end - changelog_md); return changelog_md; }
#else
const char* changelog_text(size_t* n) { *n = 0; return NULL; }
#endif

char g_whatsnew_seen[32];

typedef struct Out { char* p; char* end; } Out;
static void put(Out* o, char c)
{
    if (c == ' ' && (o->p[-1] == ' ' || !o->p[-1])) return;   /* (no doubled spaces, none at the start) */
    if (o->p < o->end - 1) *o->p++ = c;
}
static void puts_(Out* o, const char* s) { while (*s) put(o, *s++); }

/* one line's text (s..e) as the font draws it, appended */
static void inline_text(Out* o, const char* s, const char* e)
{
    for (; s < e; s++) {
        char c = *s;
        if (c == '*' && s + 1 < e && s[1] == '*') { put(o, CL_BOLD); s++; }
        else if (c == ']' && s + 1 < e && s[1] == '(') { while (s < e && *s != ')') s++; }   /* [text](link): the text */
        else if (c == '>' && o->p[-1] == ' ' && s + 1 < e && s[1] == ' ') { o->p--; put(o, ','); }   /* "EXTRA > IMPORT" */
        else if (c == '=') { if (o->p[-1] == ' ') o->p--; put(o, ':'); }   /* "save_system = 0": "save_system: 0" */
        else if (c == '&') puts_(o, "and");
        else if (c == '@') puts_(o, "at");
        else if (strchr("*`[]<>#{}|\\^$", c)) {}
        else if (c == '\t') put(o, ' ');
        else put(o, c);
    }
}

int changelog_parse(const char* md, size_t n, char* buf, size_t bufn, ClLine* lines, int max)
{
    const char *p = md, *end = md + n;
    Out o = { buf + 1, buf + bufn };
    int k = 0, open = 0;   /* open: the last line takes the next one's text (a bullet's or paragraph's) */
    if (bufn < 2 || !md) return 0;
    buf[0] = 0;
    for (const char* e; p < end; p = e + 1) {
        const char *t = p, *te;
        int indent;
        if (!(e = memchr(p, '\n', (size_t)(end - p)))) e = end;
        te = e;
        while (te > t && (te[-1] == '\r' || te[-1] == ' ')) te--;
        while (t < te && *t == ' ') t++;
        indent = (int)(t - p);
        if (t < te && *t == '>') { t++; while (t < te && *t == ' ') t++; if (t < te && *t == '[') { open = 0; continue; } }   /* "> [!NOTE]" */
        if (t == te) { open = 0; continue; }
        if (*t == '#') {
            int h = 0;
            while (t + h < te && t[h] == '#') h++;
            open = 0;
            if (h < 2 || h > 3 || k >= max) continue;   /* (the page's title) */
            t += h;
            while (t < te && *t == ' ') t++;
            if (h == 2 && te - t == 10 && !memcmp(t, "Unreleased", 10)) t = te;
            put(&o, 0);
            lines[k].kind = h == 2 ? CL_VERSION : CL_HEAD;
            lines[k].depth = 0;
            lines[k++].text = o.p;
            inline_text(&o, t, te);
            continue;
        }
        if ((*t == '-' || *t == '*') && t + 1 < te && t[1] == ' ') {
            if (k >= max) break;
            put(&o, 0);
            lines[k].kind = CL_ITEM;
            lines[k].depth = indent >= 2;
            lines[k++].text = o.p;
            inline_text(&o, t + 2, te);
            open = 1;
            continue;
        }
        if (open) { put(&o, ' '); inline_text(&o, t, te); continue; }
        if (k >= max) break;
        put(&o, 0);
        lines[k].kind = CL_TEXT;
        lines[k].depth = 0;
        lines[k++].text = o.p;
        inline_text(&o, t, te);
        open = 1;
    }
    *o.p = 0;
    for (int i = 0; i < k; i++) {   /* (a trailing space, kept by put before the line ended) */
        char* s = (char*)lines[i].text;
        size_t l = strlen(s);
        while (l && s[l - 1] == ' ') s[--l] = 0;
    }
    return k;
}
