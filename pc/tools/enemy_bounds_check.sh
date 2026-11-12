#!/bin/bash
# Bounds check for the extra enemies (docs/ENEMIES.md "Extra enemies"): a new game teleports through the 94 rooms that get
# extras, and in each the extras' spots are checked apart from the rules that chose them (RECVX_ENEMY_OOB, nothing spawns):
# a flood fill of 2-unit cells by the player's own wall check from where the player stands and the room's entry positions
# must reach every spot of the room's grid and every spot the extras of seeds 1..SEEDS take ("reach"), the spot must be in
# a camera zone of its floor as the game tests it ("cam"), the player's body must fit there ("wall") and it must not be in
# water. Prints the rooms with flagged spots, the totals, and how many extras the rooms got per seed with Extra Enemies 8
# (0..8, all equally likely). Passes when nothing is flagged. About 5 minutes.
#   pc/tools/enemy_bounds_check.sh OUT [SEEDS]      (default 100)
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), DELAY (frames per room, default 60).
out=$1; n=${2:-100}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [SEEDS]"; exit 2; }
rooms=$(sed -n '/^rooms=\${ROOMS:-"/,/"}$/p' "$root/pc/tools/enemy_door_check.sh" | sed 's/^rooms=\${ROOMS:-"//; s/"}$//')
delay=${DELAY:-60}
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
tp=""; f=1
for r in $rooms; do d=$delay; [ -z "$tp" ] && d=1; tp="$tp${tp:+,}$d:$r"; f=$((f + delay)); done
rm -rf "$out"; mkdir -p "$out/save" "$out/mods"; echo "show_fps = 0" > "$out/save/recvx.ini"
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_SKIP_CINE=30 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
RECVX_INPUT="$newgame" RECVX_TELEPORT="$tp" RECVX_EXIT_FRAME=$((f * 3 / 2 + 300)) RECVX_INF_HEALTH=1 RECVX_ENEMY_OOB="$n" \
timeout -s KILL 3600 "$bin" "$iso" --skip-intro --size 320x240 --save-dir "$out/save" --log="$out/log.txt" > "$out/stdout.txt" 2>&1
python3 - "$out/log.txt" <<'EOF'
import re, sys
rooms, flagged, hist = {}, [], [0] * 9
for line in open(sys.argv[1], errors='replace'):
    m = re.search(r'test: oob room (\d+): (\d+) of (\d+) grid spots flagged \(([^)]*)\); (\d+) of (\d+) extras.*\(0\.\.8\): ([\d ]+)', line)
    if m:
        rooms[m.group(1)] = [int(m.group(i)) for i in (2, 3, 5, 6)]
        for i, v in enumerate(m.group(7).split()): hist[i] += int(v)
        if m.group(2) != '0' or m.group(5) != '0': print('%s: %s of %s grid spots (%s), %s of %s extras flagged' % m.group(1, 2, 3, 4, 5, 6))
    elif 'test: oob room' in line and len(flagged) < 20: flagged.append(line.split('test: ')[1].strip())
for f in flagged: print('    ' + f)
t = lambda i: sum(v[i] for v in rooms.values())
print('%d rooms: %d of %d grid spots and %d of %d extras\' spots flagged; %d rooms without a spot' % (len(rooms), t(0), t(1), t(2), t(3), sum(not v[1] for v in rooms.values())))
print('extras per room with 8 over the seeds: %s (mean %.2f)' % (' '.join('%d:%d' % (i, h) for i, h in enumerate(hist)), sum(i * h for i, h in enumerate(hist)) / max(sum(hist), 1)))
ok = len(rooms) >= 80 and t(3) > 0 and not t(0) and not t(2)
print('ok' if ok else 'FAIL')
sys.exit(not ok)
EOF
