#!/bin/bash
# Two-instance co-op check (docs/COOP.md "The hand-in"): a guest that is in a room before the host gets the room's
# randomized enemies and extra enemies at once, and when the host comes in nothing jumps back, pops in or comes alive
# again. The host plays with "Extra Enemies" 5, the randomizer on, seed 88119; the guest with coop_follow = 0. The host
# waits in 0/1 (no enemies) and comes in at host frame ~3000; the guest is there from ~1000. Three pairs at once:
#   A  0/2 (the first zombie room: five scripted zombie records, an extra Cerberus in slot 39); the guest kills record 3
#   B  0/6 (the barracks: Hunters and zombies here, an extra Bandersnatch in slot 23, dead flag 40); the guest kills record 1
#   C  as B, the guest kills the extra
# RECVX_COOP_ENEPOS logs every enemy (slot, type, look, where, HP, mode, dead) every 5 host frames around the entry (2900..3300).
# Checks: the guest has its extras and the randomizer's types before the host comes; the host makes the guest's (no
# second set), not the dead extra; afterwards both list the same slots, types, HP and dead ones, positions within 6
# units (a Hunter on the move); on the guest no enemy moves more than 12 units in 5 frames around the entry (before: the room's start
# positions). About 2 minutes.
#   pc/tools/coop_handin_check.sh OUT [PORT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx).
out=$1; port=${2:-48481}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
run() {   # run DIR INI exitframe args... (the caller exports the test aids)
    local d=$1 ini=$2 x=$3; shift 3
    mkdir -p "$d/save"
    printf "show_fps = 0\n$ini" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
    RECVX_EXIT_FRAME=$x RECVX_INF_HEALTH=1 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" --coop-conn direct "$@" > "$d/log.txt" 2>&1
    echo $? > "$d/exit"
}
pos="1500,$(seq -s, 2900 5 3300),3400"
pair() {   # pair NAME PORT ROOM GUEST_KILL
    local o=$out/$1 p=$2
    (export RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT="1:0:1:0,2600:0:$3:0" RECVX_COOP_ENEPOS="$pos"
     run "$o/host" "enemy_extra = 5\nenemy_random = 1\nenemy_seed = 88119\n" 3500 --coop-host "$p") &
    sleep 2
    (export RECVX_TELEPORT="1000:0:$3:0" RECVX_COOP_KILL="$4" RECVX_COOP_ENEPOS="$pos"
     run "$o/guest" "coop_follow = 0\n" 3560 --coop-join "127.0.0.1:$p#TEST-KEYS") &
    wait
}
# the enemies a side logged at host frame F: "slot:type.look" of the living (dead and hidden ones apart)
types() { sed -n "s/.*coop: test: enemies in [0-9]*\/[0-9]* at host frame $3:\(.*\)/\1/p" "$out/$1/$2/log.txt" | head -1 | tr ' ' '\n' | sed -n 's/^\([0-9]*:[0-9]*\.[0-9]*\)@.*/\1/p' | tr '\n' ' '; }
cmp() {   # cmp NAME: the two sides' last sample alike; the guest's largest step between samples from 2900 to 3300
    python3 - "$out/$1/host/log.txt" "$out/$1/guest/log.txt" <<'EOF'
import re, sys
def samples(path):
    s = {}
    for l in open(path, errors='replace'):
        m = re.search(r'coop: test: enemies in \d+/\d+ at host frame (\d+):(.*)', l)
        if not m: continue
        f = int(m.group(1)); s.setdefault(f, {})
        for e in m.group(2).split():
            k, rest = e.split(':', 1); tl, st = rest.split('@'); v = st.split(',')
            s[f][int(k)] = (tl, float(v[0]), float(v[1]), int(v[2]), 'D' in v[4:] or 'H' in v[4:])
    return s
h, g = samples(sys.argv[1]), samples(sys.argv[2])
f = 3400
a, b = h.get(f, {}), g.get(f, {})
bad = []
for k in sorted(set(a) | set(b)):
    if k not in a or k not in b:   # (a dead one may lie on one side only: the host leaves out an extra the guest killed)
        if not (a.get(k) or b.get(k))[4]: bad.append('slot %d only on the %s' % (k, 'host' if k in a else 'guest'))
        continue
    x, y = a[k], b[k]
    if x[0] != y[0] or x[4] != y[4] or (not x[4] and x[3] != y[3]): bad.append('slot %d: host %s, guest %s' % (k, x, y))
    elif not x[4] and abs(x[1] - y[1]) + abs(x[2] - y[2]) > 6: bad.append('slot %d at %.0f,%.0f on the host, %.0f,%.0f on the guest' % (k, x[1], x[2], y[1], y[2]))
jump, at = 0.0, ''
fs = sorted(f for f in g if 2900 <= f <= 3300)
for f0, f1 in zip(fs, fs[1:]):
    for k in set(g[f0]) & set(g[f1]):
        if g[f0][k][4] or g[f1][k][4]: continue
        d = abs(g[f1][k][1] - g[f0][k][1]) + abs(g[f1][k][2] - g[f0][k][2])
        if d > jump: jump, at = d, 'slot %d between host frames %d and %d' % (k, f0, f1)
print('alike' if not bad else '; '.join(bad))
print('jump %.1f %s' % (jump, at))
EOF
}
rm -rf "$out"; mkdir -p "$out/mods"
pair A "$port" 2 1700:3 &
pair B "$((port + 1))" 6 1700:1 &
pair C "$((port + 2))" 6 1700:23 &
wait
for r in A B C; do
    check "$r: neither side crashed" "[ \$(cat $out/$r/host/exit) = 0 ] && [ \$(cat $out/$r/guest/exit) = 0 ] && ! grep -q 'fatal signal' $out/$r/*/log.txt"
    cmp $r > "$out/$r/cmp.txt"
    check "$r: afterwards both have the same enemies, HP and dead ones, within 6 units ($(head -1 $out/$r/cmp.txt))" "[ \"\$(head -1 $out/$r/cmp.txt)\" = alike ]"
    check "$r: on the guest nothing jumps when the host comes in ($(tail -1 $out/$r/cmp.txt))" "tail -1 $out/$r/cmp.txt | awk '{ exit !(\$2 <= 12) }'"
done
check "A: the guest alone has the randomized zombies and the extra Cerberus at once" "[ '$(types A guest 1500)' = '0:1.10 1:1.37 2:1.36 3:1.7 5:1.39 39:4.0 ' ]"
check "A: the host makes the guest's extra (no second set)" "grep -q \"enemies: the guest's extra type 4 model 0 (dead flag 0) into slot 39: spawned\" $out/A/host/log.txt && ! grep -q 'enemies: extra Cerberus' $out/A/host/log.txt"
check "B: the guest alone has its extra Bandersnatch at once" "[ '$(types B guest 1500)' = '0:5.1 1:1.37 2:5.0 3:1.41 23:9.0 ' ]"
check "B: the host makes the guest's extra (no second set)" "grep -q \"enemies: the guest's extra type 9 model 0 (dead flag 40) into slot 23: spawned\" $out/B/host/log.txt && ! grep -q 'enemies: extra Bandersnatch' $out/B/host/log.txt"
check "C: the extra the guest killed stays dead on the host" "grep -q \"into slot 23: dead (the guest killed it)\" $out/C/host/log.txt"
exit $fail
