#!/bin/bash
# screens_check.sh BIN_A BIN_B OUTDIR [ISO]
# The game's menu screens, headless, in both binaries with the same scripted input, compared byte for byte
# (docs/MENUS.md "Menu Style"): a new game in the Prison's save room (0/4) with a set bag, then
#   the inventory (opening, the cursor, the command list, COMBINE's target grid, CHECK's 3D view, the top bar),
#   the MAP (a floor step), FILE (a page step), the item box (F2's Open Item Box: the box list, a scroll),
#   the typewriter's save screen (a new save written: "Save as new?" YES),
# and a second run from the title: the title menu and LOAD GAME with that save.
# Dumps every 10th frame of the menus (640x480 BMPs, ~120 + ~40 per binary). Exit status 1 if any dump differs
# or a run did not get to its last frame. Also prints the menus' transition lengths (presented frames from the
# press to the state that takes input, from RECVX_MENU_TRACE; only a build with that trace prints them) and the
# title's fades (from every frame of the title run's dumps between 60 and 260).
# Env: MENU new (default) or legacy (--legacy-menu and the old title menu's NEW GAME); INI extra recvx.ini lines
#      (e.g. "menu_style = 2"); ARGS / ARGS_A / ARGS_B extra command-line arguments (e.g. --dev); IMPORT a mods/import
#      folder (imported languages, linked in) with INI "language = ar" for another language; MAX_SECS per run
#      (default 600).
# OUTDIR must not exist (nothing is deleted). Never commit it; it holds pictures of the game.
set -u
A=$(realpath "$1"); B=$(realpath "$2"); OUT=$(realpath -m "$3")
ISO=${4:-${RECVX_ISO:-$(dirname "$(realpath "$0")")/../../ISO/SLUS-20184 ROM.iso}}
MAX_SECS=${MAX_SECS:-600}; MENU=${MENU:-new}
mkdir -p "$(dirname "$OUT")" && mkdir "$OUT" || { echo "screens_check: $OUT exists (or cannot be made); give a new folder"; exit 2; }

if [ "$MENU" = legacy ]; then
  NG="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"; MARG=--legacy-menu
else
  NG="90:down,93:-,100:cross,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"; MARG=
fi
# presented frames: 400 inventory, 450 right, 470 the command list, 500/510 down to COMBINE, 520 its grid, 540 a move,
# 560/600 back, 630/660/670 CHECK (the 3D view), 730 back, 770-790 up to the top bar, 800/810 MAP, 870 a floor, 900 back,
# 940/950 FILE, 1000 a page, 1020 back, 1060 closed; the item box at game frame 150 (~1180): 1220 right, 1240 the box
# list, 1270 down, 1300 back, 1330 closed; the typewriter at game frame 200 (~1360): 1440 the slot, 1480 YES, 1640 out
GAME="$NG,400:start,403:-,450:right,453:-,470:cross,473:-,500:down,503:-,510:down,513:-,520:cross,523:-,540:down,543:-,"\
"560:circle,563:-,600:circle,603:-,630:cross,633:-,660:down,663:-,670:cross,673:-,730:circle,733:-,770:up,773:-,780:up,783:-,"\
"790:up,793:-,800:left,803:-,810:cross,813:-,870:down,873:-,900:circle,903:-,940:left,943:-,950:cross,953:-,1000:right,1003:-,"\
"1020:circle,1023:-,1060:start,1063:-,1220:right,1223:-,1240:cross,1243:-,1270:down,1273:-,1300:circle,1303:-,1330:circle,1333:-,"\
"1440:cross,1443:-,1480:cross,1483:-,1640:circle,1643:-"
# the title run: 100 LOAD GAME, 280 down on the list, 300 back
TITLE="100:cross,103:-,280:down,283:-,300:circle,303:-"

run() { # BIN DIR INPUT LAST FROM EVERY ARGS [env...]
  local bin=$1 d=$2 in=$3 last=$4 from=$5 every=$6 args=$7; shift 7
  mkdir -p "$d/frames" "$d/mods" "$d/save"
  [ -n "${IMPORT:-}" ] && ln -s "$(realpath "$IMPORT")" "$d/mods/import"
  { echo "show_fps = 0"; echo "difficulty_select = 0"; echo "fixed_cam_version = 1"; echo "fixed_cam_controls = 1"; [ -n "${INI:-}" ] && printf "%s\n" "$INI"; } > "$d/save/recvx.ini"
  env SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$d/mods" \
    RECVX_MENU_TRACE=1 RECVX_INTERP_T=0.5 RECVX_INPUT="$in" RECVX_DUMP_FRAMES="$d/frames" RECVX_DUMP_EVERY=$every RECVX_DUMP_FROM=$from \
    RECVX_EXIT_FRAME=$((last + 10)) "$@" \
    timeout -s KILL "$MAX_SECS" "$bin" "$ISO" --skip-intro --size 640x480 --save-dir "$d/save" $MARG $args > "$d/log.txt" 2>&1
  rm -f "$d"/frames/rt*.bmp "$d"/frames/win_*.bmp
  for f in "$d"/frames/*.bmp; do n=${f##*_}; n=${n%.bmp}; [ $((10#$n)) -gt "$last" ] && rm -f "$f"; done   # (the exit races the last dumps)
  [ -f "$d/frames/$(printf frame_%05d.bmp "$last")" ] || { echo "$d: did not reach frame $last"; touch "$d/MISSING"; }
}
side() { # BIN NAME ARGS
  local d="$OUT/$2"
  run "$1" "$d/game" "$GAME" 1700 380 10 "$3" RECVX_TELEPORT=1:0:4:0 RECVX_EQUIP=20:5:60 RECVX_BAG="p380:2:5:15,12:30,21:1,22:1,4:7,8:1,31:3" \
      RECVX_CHEAT_BOX=150 RECVX_TEST_TYPEWRITER=200 RECVX_ITEM_TRACE=1
  mkdir -p "$d/title/save" && cp -r "$d/game/save/saves" "$d/title/save/" 2>/dev/null
  run "$1" "$d/title" "$TITLE" 340 60 2 "$3"
}
side "$A" a "${ARGS_A:-${ARGS:-}}" & side "$B" b "${ARGS_B:-${ARGS:-}}" & wait

status=0
for part in game title; do
  same=0; diff=0; list=""
  for f in $(cd "$OUT/a/$part/frames" && ls); do
    [ -f "$OUT/b/$part/frames/$f" ] || { diff=$((diff + 1)); list="$list ${f%.bmp}(only a)"; continue; }
    if cmp -s "$OUT/a/$part/frames/$f" "$OUT/b/$part/frames/$f"; then same=$((same + 1)); else diff=$((diff + 1)); list="$list ${f%.bmp}"; fi
  done
  echo "$part: $same identical, $diff differ${list:+:$list}"
  [ $diff -eq 0 ] && [ $same -gt 0 ] || status=1
done
ls "$OUT"/*/*/MISSING >/dev/null 2>&1 && status=1

# transition lengths: presses (the script's frames) to the state that takes input
python3 - "$OUT" <<'EOF'
import re, sys, os
out = sys.argv[1]
def states(log):
    r = []
    for l in open(log, errors='replace'):
        m = re.match(r'menu-state: frame (\d+) screen (\d+) sub (\d+) mode (\d+) test (\d+) subcsr (\d+) statusflg (0x[0-9a-f]+|0) st_flg (0x[0-9a-f]+|0)', l)
        if m:
            r.append([int(m.group(i)) for i in range(1, 7)] + [int(m.group(7), 16), int(m.group(8), 16)])
    return r
def first(st, after, cond):
    for s in st:
        if s[0] > after and cond(s): return s[0]
    return None
for side in ('a', 'b'):
    st = states(os.path.join(out, side, 'game', 'log.txt'))
    if not st: continue
    # the last state change after the press, before the script's next one (closing: the first frame back in play)
    rows = [('inventory open', 400, 450), ('command list', 470, 500), ('COMBINE target grid', 520, 540),
            ('CHECK (3D view)', 670, 730), ('CHECK back to the items', 730, 770), ('MAP open', 810, 870),
            ('MAP back to the inventory', 900, 940), ('FILE open', 950, 1000), ('FILE back', 1020, 1060),
            ('item box: box list', 1240, 1270), ('item box: back to the bag', 1300, 1330)]
    print('transitions (%s; presented frames at 30 fps from the press to the last menu state change):' % side)
    for name, at, nxt in rows:
        ch = [s[0] for s in st if at < s[0] < nxt]
        print('  %-28s %s' % (name, '%d frames (%.2f s)' % (ch[-1] - at, (ch[-1] - at) / 30.0) if ch else 'no change'))
    for name, at in (('inventory closed', 1060), ('item box closed', 1330)):
        f = first(st, at, lambda s: s[1] == 0)
        print('  %-28s %s' % (name, '%d frames (%.2f s)' % (f - at, (f - at) / 30.0) if f else 'not seen'))
    box = first(st, 1100, lambda s: s[1] == 1)
    if box:
        ch = [s[0] for s in st if box <= s[0] < 1220]
        print('  %-28s %d frames from its first frame to its last state change' % ('item box open', ch[-1] - box))
    break
# the title's fades: frames whose picture still changes after LOAD GAME (100) until the load screen settles
try:
    from PIL import Image, ImageChops, ImageStat
    d = os.path.join(out, 'a', 'title', 'frames')
    fr = sorted(int(f[6:11]) for f in os.listdir(d) if f.startswith('frame_'))
    prev, moving = None, []
    for n in fr:
        if n < 100 or n > 270: continue
        im = Image.open('%s/frame_%05d.bmp' % (d, n)).convert('L')
        if prev is not None and ImageStat.Stat(ImageChops.difference(im, prev)).mean[0] > 0.5: moving.append(n)
        prev = im
    if moving:
        print('title LOAD GAME (pressed at 100): the picture changes from %d to %d (%d frames, %.2f s; dumps every 2nd frame)'
              % (moving[0], moving[-1], moving[-1] - 100, (moving[-1] - 100) / 30.0))
except ImportError:
    print('(python3 PIL missing: no title fade measure)')
EOF
exit $status
