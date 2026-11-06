#!/bin/bash
# Check of the enemy randomizer's "Custom" rates and "Types per Room" (docs/ENEMIES.md "Randomizer"): a new game teleports
# through rooms with randomizable enemies, per seed in five runs, each with the most extra enemies:
#   rates   Custom: zombies Off, Cerberus 10, the other kinds 1 (Mutated Steve too). Passes when no record became a zombie,
#           and Cerberus took more than half of the records that changed to a ground kind.
#   hunter, sweeper  Custom: the other kinds 1, Hunter 10 and Sweeper Off (sweeper: the other way round). Pass when no record
#           became the one at Off and some became the one at 10 (the two looks of type 5 are separate kinds).
#   types1  Wild, Types per Room 1;  types2  Custom with every kind at 5, Types per Room 2. Pass when every room has at
#           most that many kinds (types2: the Hunter and the Sweeper two; unless its log says some records could only be
#           another kind) and every extra enemy is of a type the room's records have.
# About 3 minutes, all runs in parallel.
#   pc/tools/enemy_rates_check.sh OUT [SEED...]      (default seeds 1234 77 900001 31337)
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), ROOMS (stg:rom:pos:case list).
out=$1; shift; seeds=${*:-1234 77 900001 31337}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
rooms=${ROOMS:-"0:6:0:0 0:12:0:0 1:6:0:0 3:6:0:0 3:9:0:0 7:15:0:0 7:21:0:0 1:12:0:0 0:13:0:0 2:1:0:1 7:18:0:0 7:12:0:0 5:68:0:0 5:53:0:0 5:58:0:0 5:63:0:0 5:64:0:0 5:55:0:0 5:70:0:0 0:2:0:1 3:5:0:0 5:62:0:0"}
[ -n "$out" ] || { echo "usage: $0 OUT [SEED...]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
tp=""; f=1
for r in $rooms; do d=300; [ -z "$tp" ] && d=1; tp="$tp${tp:+,}$d:$r"; f=$((f + 300)); done
rm -rf "$out"; mkdir -p "$out/mods"
one="1:1 4:1 9:1 5.0:1 5.1:1 3:1 26:1 19:1 23:1 7:1 11:1 v1:1"
for seed in $seeds; do
    for run in rates hunter sweeper types1 types2; do
        case $run in
        rates) v=2; t=0; r="${one/1:1 4:1/1:0 4:10}" ;;
        hunter) v=2; t=0; r="${one/5.0:1 5.1:1/5.0:10 5.1:0}" ;;
        sweeper) v=2; t=0; r="${one/5.0:1 5.1:1/5.0:0 5.1:10}" ;;
        types1) v=1; t=1; r="" ;;
        types2) v=2; t=2; r="${one//:1/:5}" ;;
        esac
        d=$out/$seed-$run; mkdir -p "$d/save"; echo "show_fps = 0" > "$d/save/recvx.ini"
        SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_SKIP_CINE=30 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
        RECVX_INPUT="$newgame" RECVX_TELEPORT="$tp" RECVX_EXIT_FRAME=$((f * 3 / 2 + 300)) RECVX_INF_HEALTH=1 \
        RECVX_ENEMY_RANDOM=1 RECVX_ENEMY_EXTRA=8 RECVX_ENEMY_SEED=$seed RECVX_ENEMY_VARIETY=$v RECVX_ENEMY_TYPES=$t RECVX_ENEMY_RATES="$r" \
        timeout -s KILL 900 "$bin" "$iso" --skip-intro --size 320x240 --save-dir "$d/save" --log="$d/log.txt" > "$d/stdout.txt" 2>&1 &
    done
done
wait
python3 - "$out" $seeds <<'EOF'
import re, sys
out, seeds, fail = sys.argv[1], sys.argv[2:], 0
KINDS = {1, 3, 4, 5, 7, 9, 11, 17, 19, 23, 26}      # what the randomizer places (17: Mutated Steve's base)
GROUND = {1, 3, 4, 5, 9, 17, 19, 23, 26}
for seed in seeds:
    for run in ('rates', 'hunter', 'sweeper', 'types1', 'types2'):
        rooms, changed, looks, extras, bad = {}, [], [], 0, []
        cur = None
        for line in open('%s/%s-%s/log.txt' % (out, seed, run), errors='replace'):
            line = re.sub(r'^\[ *[0-9.]+\] ', '', line.rstrip())
            m = re.match(r'room: RM_(\d{4}) \d+ KB expanded', line)
            if m: cur = m.group(1)
            m = re.match(r'enemies: room (\d+) record \d+: type \d+ model \d+ -> .*\((variant \d+: )?type (\d+) model (\d+)\)', line)
            if m and 'event enemy: same type' not in line: changed.append(int(m.group(3)))   # (an event's: another look of its own)
            if m and not m.group(2): looks.append((int(m.group(3)), int(m.group(4))))       # (a variant is its own kind)
            m = re.match(r'enemies: room (\d+): \d+ of \d+ records randomized .*limit (\d+)(: over it[^)]*)?\), now(.*)', line)
            if m:
                ids = {int(x.split(':')[1].split('.')[0]) for x in m.group(4).split()}
                # kinds: the type ids; under Custom (types2) the Hunter and the Sweeper (type 5 models 0, 1) are two
                kinds = {(int(i), int(v) if i == '5' and run == 'types2' else 0) for i, v in (x.split(':')[1].split('.') for x in m.group(4).split())}
                rooms[m.group(1)] = (ids, int(m.group(2)), bool(m.group(3)), {k for k in kinds if k[0] in KINDS})
            m = re.match(r'enemies: extra .*?\(type (\d+) model \d+, from record \d+.*: spawned', line)
            if m and cur in rooms:
                extras += 1
                if int(m.group(1)) not in rooms[cur][0]: bad.append('%s extra of type %s' % (cur, m.group(1)))
        if run == 'rates':
            ground = [t for t in changed if t in GROUND]
            cer = sum(t == 4 for t in ground)
            if 1 in changed: bad.append('a record became a zombie')
            if not ground or cer * 2 <= len(ground): bad.append('Cerberus %d of %d' % (cer, len(ground)))
            what = '%d records changed, %d to ground kinds, %d of them Cerberus, %d zombies' % (len(changed), len(ground), cer, changed.count(1))
        elif run in ('hunter', 'sweeper'):
            on, off = ((5, 0), (5, 1)) if run == 'hunter' else ((5, 1), (5, 0))
            if looks.count(off): bad.append('%d records became the %s' % (looks.count(off), 'Sweeper' if run == 'hunter' else 'Hunter'))
            if not looks.count(on): bad.append('no record became the %s' % ('Hunter' if run == 'hunter' else 'Sweeper'))
            what = '%d records changed, %d to Hunters, %d to Sweepers' % (len(changed), looks.count((5, 0)), looks.count((5, 1)))
        else:
            over = 0
            for room, (ids, lim, forced, kinds) in rooms.items():
                n = len(kinds)
                if n > lim and not forced: bad.append('%s has %d kinds' % (room, n))
                over += forced
            what = '%d rooms randomized (%d over the limit by records that could only be another kind), most kinds in a room %d' % (
                len(rooms), over, max([len(k) for _, _, _, k in rooms.values()] or [0]))
        ok = len(rooms) >= 8 and not bad
        fail |= not ok
        print('%s seed %s, %s: %s; %d extra enemies spawned%s' % ('ok:  ' if ok else 'FAIL:', seed, run, what, extras,
              '; ' + ', '.join(bad[:8]) if bad else ''))
sys.exit(fail)
EOF
