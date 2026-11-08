#!/bin/bash
# Floor check for the enemy options (docs/ENEMIES.md "Enemy options"): a new game teleports through every room with records
# the options touch, per seed in three runs (Wild, Mild, Custom with every kind at 5), each with the randomizer, rearranged
# placements and eight extra enemies on, and logs every enemy two frames into each room (RECVX_ENEMY_FLOORS=2: its floor and
# the ground under it). Flags each walking enemy the options moved or added, or turned into another type at its own spot
# (fliers, bats and Spotters, keep their height), that stands more than a unit above the ground under it (mid-air: over a
# pool, off a ledge), more than a unit below a surface (in a crate, a step or under a floor), or over no ground away from
# its floor's height. Records that keep their type at the game's own spot are listed apart (those high or low on purpose:
# event enemies, scripted or lying ones). Passes when nothing of the first two is flagged. About 25 minutes, all runs in
# parallel.
#   pc/tools/enemy_floor_check.sh OUT [SEED...]      (default seeds 1234 77 900001 31337)
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), ROOMS (stg:rom:pos:case list), DELAY
# (frames per room, default 150), FLOORS (the frame of the log, default 2; later shows where the enemies went, e.g. 900
# with DELAY=1000).
out=$1; shift; seeds=${*:-1234 77 900001 31337}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
rooms=${ROOMS:-"0:2:0:0 0:2:0:1 0:3:0:0 0:3:0:1 0:5:0:0 0:6:0:0 0:7:0:0 0:8:0:0 0:9:0:0 0:11:0:0 0:12:0:0 0:13:0:0 0:14:0:0
0:15:0:0 0:16:0:0 1:0:0:0 1:2:0:1 1:3:0:0 1:6:0:0 1:7:0:0 1:9:0:0 1:10:0:0 1:12:0:0 1:12:0:1 2:0:0:0 2:1:0:0 2:1:0:1 2:2:0:0
2:3:0:1 3:1:0:1 3:2:0:0 3:3:0:0 3:5:0:0 3:6:0:0 3:7:0:0 3:9:0:0 3:10:0:0 3:12:0:0 3:13:0:0 3:14:0:0 3:16:0:0 3:17:0:0 3:18:0:0
3:20:0:0 4:0:0:0 4:0:0:1 4:1:0:0 4:1:0:2 4:2:0:0 5:52:0:0 5:53:0:0 5:54:0:0 5:55:0:0 5:58:0:0 5:59:0:0 5:60:0:0 5:61:0:0
5:62:0:0 5:63:0:0 5:64:0:0 5:65:0:0 5:66:0:0 5:67:0:0 5:68:0:0 5:69:0:0 5:70:0:0 5:71:0:0 6:1:0:0 6:2:0:0 6:3:0:0 6:5:0:0
6:7:0:0 6:8:0:0 6:9:0:0 7:3:0:1 7:5:0:0 7:6:0:0 7:7:0:0 7:7:0:1 7:8:0:0 7:8:0:1 7:10:0:0 7:11:0:0 7:12:0:0 7:13:0:0 7:14:0:0
7:15:0:0 7:18:0:0 7:18:0:1 7:19:0:0 7:19:0:1 7:20:0:0 7:21:0:0 7:25:0:0 8:0:0:0 8:0:0:1 8:1:0:0 8:2:0:0 8:3:0:0 9:1:0:0
9:2:0:0 9:3:0:0 9:6:0:0 9:8:0:0 9:10:0:0 9:11:0:0 9:12:0:0 9:13:0:0 9:19:0:0 9:20:0:0 9:22:0:0 9:29:0:0 9:31:0:0 9:34:0:0"}
delay=${DELAY:-150}
[ -n "$out" ] || { echo "usage: $0 OUT [SEED...]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
tp=""; f=1
for r in $rooms; do d=$delay; [ -z "$tp" ] && d=1; tp="$tp${tp:+,}$d:$r"; f=$((f + delay)); done
rm -rf "$out"; mkdir -p "$out/mods"
five="1:5 4:5 9:5 5.0:5 5.1:5 3:5 26:5 19:5 23:5 7:5 11:5 v1:5"
for seed in $seeds; do
    for run in wild mild custom; do
        case $run in wild) v=1; r="" ;; mild) v=0; r="" ;; custom) v=2; r="$five" ;; esac
        d=$out/$seed-$run; mkdir -p "$d/save"; echo "show_fps = 0" > "$d/save/recvx.ini"
        SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_SKIP_CINE=30 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
        RECVX_INPUT="$newgame" RECVX_TELEPORT="$tp" RECVX_EXIT_FRAME=$((f * 3 / 2 + 300)) RECVX_INF_HEALTH=1 RECVX_ENEMY_FLOORS=${FLOORS:-2} \
        RECVX_ENEMY_RANDOM=1 RECVX_ENEMY_PLACE=1 RECVX_ENEMY_EXTRA=8 RECVX_ENEMY_SEED=$seed RECVX_ENEMY_VARIETY=$v RECVX_ENEMY_RATES="$r" \
        timeout -s KILL 2400 "$bin" "$iso" --skip-intro --size 320x240 --save-dir "$d/save" --log="$d/log.txt" > "$d/stdout.txt" 2>&1 &
    done
done
wait
python3 - "$out" $seeds <<'EOF'
import re, sys, collections
out, seeds = sys.argv[1], sys.argv[2:]
walker = {1, 3, 4, 5, 9, 19, 22, 23, 26}   # (the kinds a record can become but the fliers 7 and 11, and the albinoid adult extras copy)
tol, fail, total = 1.0, 0, collections.Counter()
# spot: the options chose the spot (moved, extra); kind: another type at the game's spot (random); game: the game's spot and type
names = {'spot': 'placed elsewhere or added', 'kind': 'another type at its own spot', 'game': "the game's own spot and type"}
pos = open('%s/positions.txt' % out, 'w')
for seed in seeds:
    for run in ('wild', 'mild', 'custom'):
        rooms, n, bad = set(), collections.Counter(), []
        for line in open('%s/%s-%s/log.txt' % (out, seed, run), errors='replace'):
            m = re.search(r'test: floors room (\d+) slot (\d+) (\w+) id (\d+) from (\d+) flr (-?\d+) pos (\S+) (\S+) (\S+) ground (\S+) bottom (\S+) level (\S+)( slope)?( hidden)?', line)
            if not m:
                continue
            room, slot, what, i, src = m.group(1), int(m.group(2)), m.group(3), int(m.group(4)), int(m.group(5))
            x, y, z, g, bottom, lv = map(float, m.group(7, 8, 9, 10, 11, 12))
            rooms.add(room)
            pos.write('%s %s %s %d %s %d %.1f %.1f %.1f\n' % (seed, run, room, slot, what, i, x, y, z))
            if i not in walker or what == 'other':   # (other: a part an enemy's Init put in a slot of its own, where its enemy is)
                continue
            why = ('mid-air %.1f above the ground' % (y - g) if g > -999 and y - g > tol else
                   'sunk %.1f under a surface' % (g - y) if g > y + tol and bottom <= y + tol else   # (a shelf with room below: not sunk)
                   'over no ground, %.1f off its floor' % (y - lv) if g <= -999 and abs(y - lv) > tol else None)
            k = 'spot' if what in ('moved', 'extra') else 'kind' if src != i else 'game'
            n[k] += 1
            if why:
                n[k + ' flagged'] += 1
                bad.append('%s: %s %s slot %d type %d (was %d) at %.0f %.1f %.0f: %s%s' % (k, what, room, slot, i, src, x, y, z, why, ' (hidden)' if m.group(14) else ''))
        total.update(n)
        ok = len(rooms) >= 80 and not n['spot flagged'] and not n['kind flagged']
        fail |= not ok
        print('%s seed %s %s: %d rooms; walking enemies %s' % ('ok:  ' if ok else 'FAIL:', seed, run, len(rooms),
              ', '.join('%s %d, flagged %d' % (names[c], n[c], n[c + ' flagged']) for c in names)))
        for b in bad:
            print('    ' + b)
print('total: ' + ', '.join('%s %d, flagged %d' % (names[c], total[c], total[c + ' flagged']) for c in names))
sys.exit(fail)
EOF
