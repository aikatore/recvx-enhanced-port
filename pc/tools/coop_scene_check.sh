#!/bin/bash
# Two-instance co-op check (docs/COOP.md "Who runs the enemies"): a scene of the host's that starts during a hand-over. The
# host runs the zombies of 0/6 (both there, a new game; the guest fires at them from 1300). At host frame SCENE the host's
# game stops its enemies as a scene does (test aid RECVX_COOP_SCENE): after PAUSE_LEAVE frames it hands them to the guest
# (leaving), and in that frame the scene's event takes zombie SLOT, so the host takes that one back (EV_ENEOWN 2, in the
# same datagram) while the guest's take of it is still pending. Passes when the guest drops its pending take (or gives the zombie back if
# it took it first): one runner, and both show the same zombies, HP and dead ones afterwards. Before, the guest took it at
# its frame anyway and both games ran it to the end of the room load (its HP and place drifted apart).
#   pc/tools/coop_scene_check.sh OUT [PORT] [SCENE] [SLOT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), NETEM (e.g. "delay 40ms 10ms loss 2%":
# put on lo with tc; needs a network namespace of our own, e.g. unshare -rn sh -c 'ip link set lo up; NETEM=... ./this OUT').
out=$1; port=${2:-48471}; scene=${3:-1330}; slot=${4:-0}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT] [SCENE] [SLOT]"; exit 2; }
[ -e "$out" ] && { echo "$out exists"; exit 2; }
[ -n "$NETEM" ] && { tc qdisc add dev lo root netem $NETEM || exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fire="1300:r1"; for f in $(seq 1310 25 2000); do fire="$fire,$f:r1+cross,$((f + 4)):r1"; done; fire="$fire,2000:-"
mkdir -p "$out/host/save" "$out/guest/save" "$out/host/mods" "$out/guest/mods"
pos="$(seq -s, 2100 5 2140)"
run() {   # run DIR exitframe args...
    local d=$1 x=$2; shift 2
    echo "show_fps = 0" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$d/mods" \
    RECVX_EXIT_FRAME=$x RECVX_INF_HEALTH=1 RECVX_COOP_ENEPOS="$pos" \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --legacy-menu --size 320x240 --vanilla --save-dir "$d/save" --coop-conn direct "$@" > "$d/log.txt" 2>&1
    echo $? > "$d/exit"
}
RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT=1:0:6:0 RECVX_COOP_SCENE="$scene:$slot:60" run "$out/host" 2200 --coop-host "$port" &
sleep 2
RECVX_EQUIP="200:5:200" RECVX_INF_AMMO=1 RECVX_INPUT="$fire" run "$out/guest" 2260 --coop-join "127.0.0.1:$port#TEST-KEYS" &
wait
h=$out/host/log.txt; g=$out/guest/log.txt; fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
check "neither side crashed" "[ \$(cat $out/host/exit) = 0 ] && [ \$(cat $out/guest/exit) = 0 ] && ! grep -q 'fatal signal' $h $g"
check "the host's scene gave its zombies away" "grep -q 'handed to the partner.s game (our game stops its enemies)' $h"
check "the scene's event took zombie $slot back" "grep -q 'own: enemy $slot taken back from the partner.s game (an event of ours drives it)' $h"
last=$(grep -E "own: enemy $slot( is ours now|: the partner's game runs it|: its hand-over to us is void)" $g | tail -1)
check "the guest does not run zombie $slot too (${last#*coop: })" "[ -n \"\$last\" ] && ! echo \"\$last\" | grep -q 'is ours now'"
cmp=$(python3 - "$h" "$g" <<'EOF'
import re, sys
def last(path):
    s = {}
    for l in open(path, errors='replace'):
        m = re.search(r'coop: test: enemies in \d+/\d+ at host frame (\d+):(.*)', l)
        if m: s[int(m.group(1))] = m.group(2)
    return s
h, g = last(sys.argv[1]), last(sys.argv[2])
f = max(set(h) & set(g), default=None)
if f is None: print('no common sample'); sys.exit()
def parse(t):
    r = {}
    for e in t.split():
        k, rest = e.split(':', 1); tl, st = rest.split('@'); v = st.split(',')
        r[int(k)] = (tl, float(v[0]), float(v[1]), int(v[2]), 'D' in v[4:])
    return r
a, b = parse(h[f]), parse(g[f]); bad = []
for k in sorted(set(a) | set(b)):
    if k not in a or k not in b: bad.append('slot %d only on one side' % k); continue
    x, y = a[k], b[k]
    if x[0] != y[0] or x[4] != y[4] or (not x[4] and x[3] != y[3]): bad.append('slot %d: host %s, guest %s' % (k, x, y))
    elif abs(x[1] - y[1]) + abs(x[2] - y[2]) > 6: bad.append('slot %d at %.0f,%.0f / %.0f,%.0f' % (k, x[1], x[2], y[1], y[2]))
print('alike' if not bad else '; '.join(bad))
EOF
)
check "afterwards both show the same zombies, HP and dead ones ($cmp)" "[ \"$cmp\" = alike ]"
exit $fail
