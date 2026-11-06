#!/bin/bash
# Re-entry check for the enemy options (docs/ENEMIES.md "Enemy options"): a new game teleports twice through rooms whose
# roaming enemies stay dead (the entry script hides them: EneSetCk), with the randomizer, rearranged placements and three
# extra enemies on, killing every enemy of each room (RECVX_ENEMY_KILL; ":r" kills only the records, so the extras of the
# first visit live on). Passes when, for every seed, each room got the same randomizer result and the same extras (type,
# source record, spot) on both visits, and nothing that died on the first visit is there on the second (extras of
# records without a kill bit come back, as those records do). About 7 minutes, all runs in parallel.
#   pc/tools/enemy_revisit_check.sh OUT [SEED...]      (default seeds 1234 77 900001)
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), ROOMS (stg:rom:pos:case list), EXTRA (extra
# enemies, default 3); the other RECVX_ENEMY_* (e.g. _VARIETY=2, _RATES, _TYPES) pass through.
out=$1; shift; seeds=${*:-1234 77 900001}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
rooms=${ROOMS:-"0:6:0:0 0:12:0:0 1:6:0:0 3:6:0:0 3:9:0:0 7:15:0:0 7:21:0:0 1:12:0:0 0:13:0:0 2:1:0:1 7:18:0:0 7:12:0:0"}
[ -n "$out" ] || { echo "usage: $0 OUT [SEED...]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
tp=""; f=1
for round in 1 2; do for r in $rooms; do d=300; [ -z "$tp" ] && d=1; tp="$tp${tp:+,}$d:$r"; f=$((f + 300)); done; done   # (later teleports: delays)
rm -rf "$out"; mkdir -p "$out/mods"
for seed in $seeds; do
    for kill in all r; do
        d=$out/$seed-$kill; mkdir -p "$d/save"; echo "show_fps = 0" > "$d/save/recvx.ini"
        k=40; [ $kill = r ] && k=40:r
        SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_SKIP_CINE=30 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
        RECVX_INPUT="$newgame" RECVX_TELEPORT="$tp" RECVX_EXIT_FRAME=$((f * 3 / 2 + 600)) RECVX_INF_HEALTH=1 \
        RECVX_ENEMY_RANDOM=1 RECVX_ENEMY_PLACE=1 RECVX_ENEMY_EXTRA=${EXTRA:-3} RECVX_ENEMY_SEED=$seed RECVX_ENEMY_KILL=$k \
        timeout -s KILL 1200 "$bin" "$iso" --skip-intro --size 320x240 --save-dir "$d/save" --log="$d/log.txt" > "$d/stdout.txt" 2>&1 &
    done
done
wait
python3 - "$out" $seeds <<'EOF'
import re, sys, collections
out, seeds, fail = sys.argv[1], sys.argv[2:], 0
for seed in seeds:
    for kill in ('all', 'r'):
        visits, cur = [], None
        for line in open('%s/%s-%s/log.txt' % (out, seed, kill), errors='replace'):
            line = re.sub(r'^\[ *[0-9.]+\] ', '', line.rstrip())
            m = re.match(r'room: RM_(\d{4}) \d+ KB expanded', line)
            if m:
                cur = {'room': m.group(1), 'now': '', 'extras': [], 'living': None, 'died': set(), 'flagless': set()}
                visits.append(cur)
            elif cur:
                m = re.match(r'enemies: room \d+: .*records randomized.*now(.*)', line)
                if m: cur['now'] = m.group(1)
                m = re.match(r'enemies: extra .*?\(type (\d+) model (\d+), from record (\d+), dead flag (-?\d+)\) at (-?\d+) (-?\d+): (.*)', line)
                if m:
                    cur['extras'].append(m.group(1, 2, 3, 5, 6))
                    if m.group(4) == '-1': cur['flagless'].add(m.group(3))
                m = re.match(r'test: room \d+ (?:\(\d+ ed_flg bits set\) )?living enemies at frame \d+:(.*)', line)
                if m: cur['living'] = m.group(1).split()
                m = re.match(r'test: room \d+ record (\d+) \(type \d+\) died', line)
                if m: cur['died'].add('r' + m.group(1))
                m = re.match(r'test: room \d+ extra in slot (\d+) \(type \d+\) died', line)
                if m: cur['died'].add('x' + m.group(1))
        first, rooms, back, differ = {}, 0, [], []
        for v in visits:
            if v['living'] is None:
                continue
            if v['room'] not in first:
                first[v['room']] = v
                continue
            rooms += 1
            a = first[v['room']]
            if (a['now'], a['extras']) != (v['now'], v['extras']):
                differ.append(v['room'])
            for e in v['living']:   # "k:id" (record k) or "extra@slot:id"
                key = ('x' if e.startswith('extra@') else 'r') + e.split('@')[-1].split(':')[0]
                if key in a['died'] and not (key[0] == 'x' and a['flagless']):   # (extras of records without a kill bit come back)
                    back.append(v['room'] + ' ' + e)
        ok = rooms >= 8 and not differ and not back
        fail |= not ok
        print('%s seed %s, kill %s: %d rooms visited twice; differ: %s; died and back: %s' %
              ('ok:  ' if ok else 'FAIL:', seed, kill, rooms, ' '.join(differ) or 'none', ' '.join(back) or 'none'))
sys.exit(fail)
EOF
