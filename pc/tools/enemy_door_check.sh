#!/bin/bash
# Door check for the extra enemies (docs/ENEMIES.md "Extra enemies"): a new game teleports through the 94 rooms that get
# extras, per seed with the randomizer (Wild) and eight extras on, and in each room also logs where the extras of seeds
# 1..SPOTS would stand (RECVX_ENEMY_SPOTS: spots only, nothing spawns). Every spot, those surveyed and those of the extras
# that came, must be at least 30 units from every door trigger and entry position as the game measures them, and at least
# 30 units from every entry position and type 0 door trigger of the room file as pc/tools/rdx.py reads it (an independent
# check of the same rule). Prints per room how many of the eight found a spot over the surveyed seeds. About 15 minutes.
#   pc/tools/enemy_door_check.sh OUT [SEED...]      (default seeds 1234 77)
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), ROOMS (stg:rom:pos:case list), SPOTS
# (surveyed seeds per room, default 50), DELAY (frames per room, default 60).
out=$1; shift; seeds=${*:-1234 77}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
rooms=${ROOMS:-"0:2:0:0 0:2:0:1 0:3:0:0 0:3:0:1 0:5:0:0 0:6:0:0 0:7:0:0 0:8:0:0 0:9:0:0 0:11:0:0 0:12:0:0 0:13:0:0 0:14:0:0
0:15:0:0 0:16:0:0 1:0:0:0 1:2:0:1 1:3:0:0 1:6:0:0 1:7:0:0 1:9:0:0 1:10:0:0 1:12:0:0 1:12:0:1 2:0:0:0 2:1:0:0 2:1:0:1 2:2:0:0
2:3:0:1 3:1:0:1 3:2:0:0 3:3:0:0 3:5:0:0 3:6:0:0 3:7:0:0 3:9:0:0 3:10:0:0 3:12:0:0 3:13:0:0 3:14:0:0 3:16:0:0 3:17:0:0 3:18:0:0
3:20:0:0 4:0:0:0 4:0:0:1 4:1:0:0 4:1:0:2 4:2:0:0 6:1:0:0 6:2:0:0 6:3:0:0 6:5:0:0 6:7:0:0 6:8:0:0 6:9:0:0 7:3:0:1 7:5:0:0
7:6:0:0 7:7:0:0 7:7:0:1 7:8:0:0 7:8:0:1 7:10:0:0 7:11:0:0 7:12:0:0 7:13:0:0 7:14:0:0 7:15:0:0 7:18:0:0 7:18:0:1 7:19:0:0
7:19:0:1 7:20:0:0 7:21:0:0 7:25:0:0 8:0:0:0 8:0:0:1 8:1:0:0 8:2:0:0 9:1:0:0 9:2:0:0 9:3:0:0 9:6:0:0 9:8:0:0 9:10:0:0
9:11:0:0 9:12:0:0 9:13:0:0 9:19:0:0 9:20:0:0 9:22:0:0 9:29:0:0 9:31:0:0"}
delay=${DELAY:-60}
[ -n "$out" ] || { echo "usage: $0 OUT [SEED...]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
tp=""; f=1
for r in $rooms; do d=$delay; [ -z "$tp" ] && d=1; tp="$tp${tp:+,}$d:$r"; f=$((f + delay)); done
rm -rf "$out"; mkdir -p "$out/mods"
for seed in $seeds; do
    d=$out/$seed; mkdir -p "$d/save"; echo "show_fps = 0" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_SKIP_CINE=30 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
    RECVX_INPUT="$newgame" RECVX_TELEPORT="$tp" RECVX_EXIT_FRAME=$((f * 3 / 2 + 300)) RECVX_INF_HEALTH=1 RECVX_ENEMY_SPOTS=${SPOTS:-50} \
    RECVX_ENEMY_RANDOM=1 RECVX_ENEMY_EXTRA=8 RECVX_ENEMY_SEED=$seed RECVX_ENEMY_VARIETY=1 \
    timeout -s KILL 3600 "$bin" "$iso" --skip-intro --size 320x240 --save-dir "$d/save" --log="$d/log.txt" > "$d/stdout.txt" 2>&1 &
done
wait
RECVX_ISO="$iso" python3 - "$out" "$root/pc/tools" $seeds <<'EOF'
import re, sys, math, argparse, collections
out, tools, seeds = sys.argv[1], sys.argv[2], sys.argv[3:]
sys.path.insert(0, tools)
import rdx
R = 30.0                                 # XR_DOOR (enemyopt_glue.c)
args = argparse.Namespace(iso=None)
files = {}
def room_file(r):                        # (entry positions, type 0 door triggers) of RM_<r>
    if r not in files:
        j = rdx.load_room(args, r).to_json()
        files[r] = ([(p['px'], p['pz']) for p in j['positions']], [t for t in j['triggers'] if t['type'] == 0])
    return files[r]
def file_dist(r, x, z):
    pos, doors = room_file(r)
    d = min([math.hypot(x - px, z - pz) for px, pz in pos] or [1e9])
    for t in doors:
        dx = max(t['px'] - x, 0, x - t['px'] - t['w']); dz = max(t['pz'] - z, 0, z - t['pz'] - t['d'])
        d = min(d, math.hypot(dx, dz))
    return d
spots, found, real, near, cur = 0, collections.defaultdict(list), 0, [], ''
for seed in seeds:
    for line in open('%s/%s/log.txt' % (out, seed), errors='replace'):
        m = re.search(r'test: spots room (\d+) seed \d+: (\d+) of 8(.*)', line)
        if m and seed == seeds[0]:
            found[m.group(1)].append(int(m.group(2)))
        if m:
            for s in m.group(3).split():
                x, z, door, entry = map(float, s.split(','))
                spots += 1
                fd = file_dist(m.group(1), x, z)
                if min(door, entry, fd + 1.0) < R: near.append('%s spot %.0f %.0f: door %.0f, entry %.0f, room file %.1f' % (m.group(1), x, z, door, entry, fd))
        m2 = re.search(r'room: RM_(\d{4}) \d+ KB expanded', line)
        if m2: cur = m2.group(1)
        m = re.search(r'enemies: extra .*\(type \d+ model \d+, extra \d+, dead flag \d+\) at (-?\d+) (-?\d+) \(door (\d+), entry (\d+)\): spawned', line)
        if m:
            real += 1
            x, z, door, entry = map(float, m.group(1, 2, 3, 4))
            fd = file_dist(cur, x, z)
            if min(door, entry, fd + 1.0) < R: near.append('%s extra %.0f %.0f: door %.0f, entry %.0f, room file %.1f' % (cur, x, z, door, entry, fd))
print('room  extras found per seed (of 8; average, fewest, most)')
for r in sorted(found):
    v = found[r]
    print('%s  %.2f %d %d' % (r, sum(v) / len(v), min(v), max(v)))
allv = [x for v in found.values() for x in v]
print('%d rooms, %d surveyed seeds: %.2f of 8 on average; %d rooms always 8, %d never any' % (len(found), len(allv), sum(allv) / max(len(allv), 1),
      sum(min(v) == 8 for v in found.values()), sum(max(v) == 0 for v in found.values())))
print('%d surveyed spots and %d spawned extras checked; %d nearer than %.0f units to a door or entry' % (spots, real, len(near), R))
for n in near[:20]: print('    ' + n)
ok = len(found) >= 80 and spots > 0 and real > 0 and not near
print('ok' if ok else 'FAIL')
sys.exit(not ok)
EOF
