#!/usr/bin/env python3
"""ctest "menu_tr": every text the new menu (docs/MENUS.md) shows is translated into every language of
src/host/tr_table.inc. The texts: menu_layout.c's category names, headings, labels, value names and help; the string
literals the new menu's screens pass to tr(), ui_hint(), the text functions and the dialogs (ui_rows.c, setup_glue.c,
pause_glue.c, text_grid.c, import_menu.c, lobby_glue.c: the MULTIPLAYER screens and the lobby, give_list.c; menu_glue.c: the Redesigned overlay), with
their static string arrays an element of which goes to tr(), and settings.c's value lists. A text without a row, or
with an empty column, fails ("" = English shown; where the word is the same in a language, write it out). A button bar's
word is looked up as "word (button hint)" first (lang.h tr_ctx). A translation that is another row's English must translate to itself (the texts are translated
twice: the callers' tr() and the text functions'). Reads the sources.
   test_menu_tr.py <pc dir>"""
import re
import sys

LIT = r'"((?:[^"\\\n]|\\.)*)"'
LANGS = ['fr', 'de', 'es', 'ja', 'ru', 'tr', 'pt-BR', 'ar', 'ko', 'it']
# not words: names, keys, codes the screens draw as they are
KEEP = {'Claire', 'Chris', 'Steve', 'Wesker', 'BioRand', 'BIORAND', 'FXAA', 'SMAA', 'Esc', 'Del', 'Enter', 'Return', 'Backspace', 'Ctrl F',
        'Ctrl+V', 'Ctrl\\tV', 'button hint', 'XXXX-XXXX', 'OK', 'Resident Evil Code: Veronica X', 'COPY JOIN ADDRESS: both'}
SCREENS = ['glue/ui_rows.c', 'glue/setup_glue.c', 'glue/pause_glue.c', 'glue/text_grid.c', 'glue/import_menu.c', 'glue/lobby_glue.c', 'glue/give_list.c', 'glue/whatsnew_menu.c', 'glue/enemymdl_list.c',
           'glue/menu_glue.c']
CALLS = r'\b(?:tr|ui_hint|text|text_c|text_m|text_fit|line_at|say|ui_rows_say|pc_ui_text\w*|pc_ui_dialog\w*|nm_text)\('


def joined(src):
    """the source with comments dropped and adjacent literals joined"""
    src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
    src = re.sub(r'(?m)//.*$', '', src)
    return re.sub(r'"\s*\n\s*"', '', re.sub(r'"[ \t]+"', '', src))


def layout_texts(lay):
    out = set()
    for m in re.finditer(r'\[MC_\w+\] = \{ MS_\w+, ' + LIT, lay):
        out.add(m.group(1))
    rows = lay[lay.index('const MenuRow menu_rows[]'):lay.index('const int menu_rows_n')]
    for e in re.split(r'\n\s*\{ MC_', rows)[1:]:
        g = re.match(r'\w+, ' + LIT, e)
        if g:
            out.add(g.group(1))
        for f in ('label', 'help'):
            m = re.search(r'\.' + f + ' = ' + LIT, e)
            if m:
                out.add(m.group(1))
        m = re.match(r'\w+, (?:' + LIT + r'|NULL), ML_(?:VAR|ACTION|INFO), NULL, \w+, ' + LIT, e)
        if m:
            out.add(m.group(2))
        for n in re.finditer(r'N\(([^)]*)\)', e):
            out.update(re.findall(LIT, n.group(1)))
    return out


def call_texts(src):
    """literals inside the calls' parentheses (one level of nesting), but those of host_log / snprintf targets"""
    out = set()
    for m in re.finditer(CALLS, src):
        depth, i = 1, m.end()
        while depth and i < len(src):
            depth += {'(': 1, ')': -1}.get(src[i], 0)
            i += 1
        out.update(re.findall(LIT, src[m.end():i]))
    return out


def array_texts(src, every=False):
    """the literals of static string arrays an element of which goes to tr() (tr(st[story])); every: also all the
    (const char* const[]){ ... } value lists (settings.c's, which the new menu's rows translate)"""
    out = set()
    for m in re.finditer(r'static const char\* const (\w+)\[\w*\] = \{([^}]*)\}', src):
        if re.search(r'\btr\(\s*' + m.group(1) + r'\[', src):
            out.update(re.findall(LIT, m.group(2)))
    if every:
        for m in re.finditer(r'\(const char\* const\[\]\)\{([^}]*)\}', src):
            out.update(re.findall(LIT, m.group(1)))
    return out


def table(text):
    rows = {}
    for line in text.split('\n'):
        if line.startswith('{ "'):
            p = re.findall(r'"((?:[^"\\]|\\.)*)"', line)
            rows[p[0]] = p[1:]
    return rows


def missing(texts, rows):
    bad = []
    for t in sorted(texts):
        if t in KEEP or not re.search(r'[A-Za-z]{2}', t) or '\\n' in t or t.startswith('menu'):
            continue
        r = rows.get(t + ' (button hint)') or rows.get(t)
        if r is None:
            bad.append('"%s": no row' % t)
        else:
            empty = [LANGS[i] for i, c in enumerate(r) if not c]
            if empty:
                bad.append('"%s": empty for %s' % (t, ' '.join(empty)))
    return bad


def twice(rows):
    """the text functions translate what they draw, and many callers pass tr()'s result: a translation that is itself
    another row's English must translate to itself (or not at all) in that language, else it is shown as that row's"""
    bad = []
    for en, r in rows.items():
        for i, t in enumerate(r):
            again = rows.get(t)
            if t and t != en and again and again[i] not in ('', t):
                bad.append('"%s" in %s is "%s", which tr() turns into "%s"' % (en, LANGS[i], t, again[i]))
    return bad


def main():
    pc = sys.argv[1]
    rd = lambda f: open(pc + '/' + f, encoding='utf-8').read()
    texts = layout_texts(joined(rd('src/host/menu_layout.c')))
    for f in SCREENS:
        texts |= call_texts(joined(rd(f))) | array_texts(joined(rd(f)))
    texts |= array_texts(joined(rd('src/host/settings.c')), True)
    rows = table(rd('src/host/tr_table.inc'))
    bad = missing(texts, rows) + twice(rows)
    # self-test: a text without a row must be caught
    if not missing({'A text no table has'}, rows):
        print('menu_tr self-test: a missing text passed')
        return 1
    if not twice({'A': ['B'] + [''] * 9, 'B': ['C'] + [''] * 9}):
        print('menu_tr self-test: a translation translated again passed')
        return 1
    if array_texts('static const char* const zz[] = { "", "Zzz" }; tr(zz[k]);') != {'', 'Zzz'}:
        print('menu_tr self-test: a static array given to tr() was not read')
        return 1
    if bad:
        print('new-menu texts not translated in src/host/tr_table.inc (%d):' % len(bad))
        print('\n'.join('  ' + b for b in bad))
        return 1
    print('menu_tr: %d texts, every one translated' % len(texts))
    return 0


if __name__ == '__main__':
    sys.exit(main())
