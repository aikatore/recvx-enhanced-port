#!/bin/bash
# Two-instance co-op check (docs/COOP.md "Enemies: host authority and puppets"): the joiner's puppets of the host's zombies
# are posed as the host's. Host and guest over localhost, a new game, both in 0/6 (three zombies come); the guest fires at them
# (handgun, or ITEM:COUNT, e.g. 8:1 the knife, 6:60 the grenade launcher), both run with RECVX_ENEMY_POSE=1: at every
# frame each zombie's joint angles and drawn joints with the host frame they show. Each guest pose is compared with the
# host's of the same frame (or the next: the guest poses the frame the host's step advanced to). Passes when the guest's
# shots land, at least 300 poses compare, under 3% of them have a drawn joint more than a unit off, no slot is off for
# 25 frames in a row (a motion's blend lasts up to 20), no puppet shows the same host frame for 15 frames in a row (a
# grab handed over stands still for a round trip), and neither side logs STALL or ORPHAN. Before the zombie's upper half
# was posed on the guest, nearly every pose was off (its arms and head showed the body motion's tracks). About 2 minutes.
#   pc/tools/coop_enemy_sync_check.sh OUT [PORT] [ITEM:COUNT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx).
out=$1; port=${2:-48397}; weapon=${3:-5:200}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT] [ITEM:COUNT]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fire="1300:r1"; for f in $(seq 1310 25 2600); do fire="$fire,$f:r1+cross,$((f + 4)):r1"; done; fire="$fire,2600:-"
rm -rf "$out"; mkdir -p "$out/host/save" "$out/guest/save" "$out/host/mods" "$out/guest/mods"
run() {   # run DIR exitframe args...
    local d=$1 x=$2; shift 2
    echo "show_fps = 0" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$d/mods" \
    RECVX_EXIT_FRAME=$x RECVX_COOP_TRACE=1 RECVX_ENEMY_POSE=1 RECVX_INF_HEALTH=1 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" --coop-conn direct "$@" > "$d/log.txt" 2>&1
}
RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT=1:0:6:0 run "$out/host" 2800 --coop-host "$port" &
sleep 2
RECVX_EQUIP="200:$weapon" RECVX_INF_AMMO=1 RECVX_INPUT="$fire" run "$out/guest" 2860 --coop-join "127.0.0.1:$port#TEST-KEYS" &
wait
h=$out/host/log.txt; g=$out/guest/log.txt; fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
check "the guest joined in 0/6" "grep -q 'joined the host.s game (stage 0 room 6)' $g"
check "the guest's shots land" "[ \$(grep -c 'event [0-9]* in type=1 ' $h) -ge 3 ]"
check "the guest's zombies are posed as the host's" "python3 - $h $g <<'EOF'
import re, sys
from collections import defaultdict
P = re.compile(r'^epose ([HG]) seq=(\d+) s=(\d+) .* run=(\d) ')
J = re.compile(r'^emtx ([HG]) seq=(\d+) s=(\d+) j=(\S+)')
def load(path):
    joints, order = {}, []
    for l in open(path, errors='replace'):
        m = J.match(l)
        if m: joints.setdefault((int(m.group(2)), int(m.group(3))), [tuple(map(float, j.split('/'))) for j in m.group(4).split(',')])
        m = P.match(l)
        if m: order.append((int(m.group(3)), int(m.group(2)), m.group(4) == '1'))
    return joints, order
def off(a, b): return max([sum((x - y) ** 2 for x, y in zip(p, q)) ** 0.5 for p, q in zip(a, b)] or [0])
hj, _ = load(sys.argv[1]); gj, gorder = load(sys.argv[2])
n = bad = 0; run = defaultdict(int); worst_run = 0
for (seq, s), joints in sorted(gj.items(), key=lambda kv: (kv[0][1], kv[0][0])):
    near = [hj[k] for k in ((seq, s), (seq + 1, s)) if k in hj]
    if not near: continue
    n += 1
    if min(off(j, joints) for j in near) > 1.0:
        bad += 1; run[s] += 1; worst_run = max(worst_run, run[s])
    else: run[s] = 0
last, still, frozen = {}, defaultdict(int), 0
for s, seq, ran in gorder:
    still[s] = still[s] + 1 if not ran and last.get(s) == seq else 0
    last[s] = seq; frozen = max(frozen, still[s])
print(f'      {n} poses compared, {bad} off, longest run off {worst_run}, longest puppet standing still {frozen}')
sys.exit(0 if n >= 300 and bad * 100 < n * 3 and worst_run < 25 and frozen < 15 else 1)
EOF"
check "no enemy without an active owner" "! grep -q 'STALL\\|ORPHAN' $h $g"
exit $fail
