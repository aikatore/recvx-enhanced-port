#!/usr/bin/env python3
"""ctest "menu_help": every settings-table row the new menu shows (src/host/menu_layout.c) has a help line, its own
(settings.c .help) or the layout's (.help on its ML_SET / ML_SHORTCUT entry). Retired and merged rows are not shown.
A row without one fails. Reads the sources only.
   test_menu_help.py <pc dir>"""
import re
import sys


def missing(table, layout):
    body = table[table.index('const Setting g_settings[]'):]
    body = body[:body.index('\n};')]
    nohelp = []
    for row in re.split(r'\n\s*\{ SP_', body)[1:]:
        label = re.match(r'[A-Z]+, "([^"]+)"', row).group(1)
        if '.help' not in row:
            nohelp.append(label)
    rows = layout[layout.index('const MenuRow menu_rows[]'):layout.index('const int menu_rows_n')]
    helped, hidden = set(), set()
    for entry in re.split(r'\n\s*\{ MC_', rows)[1:]:
        m = re.search(r'ML_(SET|SHORTCUT|MERGED|RETIRED), "([^"]+)"', entry)
        if not m:
            continue
        if m.group(1) in ('MERGED', 'RETIRED'):
            hidden.add(m.group(2))
        if '.help' in entry:
            helped.add(m.group(2))
    return [label for label in nohelp if label not in helped and label not in hidden]


def main():
    pc = sys.argv[1]
    table = open(pc + '/src/host/settings.c', encoding='utf-8').read()
    layout = open(pc + '/src/host/menu_layout.c', encoding='utf-8').read()
    bad = missing(table, layout)
    # self-test: a row's help taken away must be caught
    broken = layout.replace('"Crosshair",\n      .help =', '"Crosshair",\n      .nohelp =')
    if 'Crosshair' not in missing(table, broken):
        print('menu_help self-test: a row without help passed')
        return 1
    if bad:
        print('rows of the new menu without help (give them .help in menu_layout.c):', ', '.join(bad))
        return 1
    print('menu_help: every row has help')
    return 0


if __name__ == '__main__':
    sys.exit(main())
