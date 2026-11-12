#!/bin/bash
# Two-instance co-op check (docs/COOP.md "The hand-in"): a guest that is in a room before the host gets the room's
# randomized enemies and extra enemies at once, and when the host comes in nothing jumps back, pops in or comes alive
# again, whatever the enemy type, and the guest's dead lie on the host too. The host plays with "Extra Enemies" 5, the
# randomizer on, seed 88119; the guest with coop_follow = 0. The host waits in 0/1 (no enemies) and comes in at host
# frame ~3000; the guest is there from ~1000. The pairs, all at once:
#   A  0/2 (the first zombie room: five scripted zombie records, an extra Cerberus in slot 39); the guest kills record 3
#   B  0/6 (the barracks: Hunters and zombies here, an extra Bandersnatch in slot 23, dead flag 40); the guest kills record 1
#   C  as B, the guest kills the extra: the host makes it dead, and the guest hands its corpse in (protocol 54)
#   D  0/2 with ev_flg 104 (a later visit: the zombies walk, no rise event); after the hand-in the host kills zombie 0:
#      its room script's DieCk counts it (ed_flg) in both games
#   M  as D without the randomizer, the guest with a modded RM_0020 (record 1 another look, record 2 moved; made from the
#      disc image into OUT at run time): both warn that the room files differ, the zombie of another look is handed in
#   T1..T4 the types without Shared Enemy Control's hand-over (protocol 54): bats and Spotters (0/13), ants (9/17), Black
#      Widows (6/9), Tyrants (1/0, the randomizer's Custom rates: only Tyrants)
# RECVX_COOP_ENEPOS logs every enemy (slot, type, look, where, HP, mode, dead) every 5 host frames around the entry (2900..3300).
# Checks: the guest has its extras and the randomizer's types before the host comes; the host makes the guest's (no
# second set); afterwards both list the same slots, types, HP and dead ones, positions within 6 units (a Hunter on the
# move); on the guest no enemy moves more than 12 units in 5 frames around the entry (before: the room's start
# positions); each type of T1..T4 goes to the host's game with its state. About 4 minutes (18 instances at once).
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
run() {   # run DIR INI MODS exitframe args... (the caller exports the test aids)
    local d=$1 ini=$2 m=$3 x=$4; shift 4
    mkdir -p "$d/save"
    printf "show_fps = 0\n$ini" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$m" \
    RECVX_EXIT_FRAME=$x RECVX_INF_HEALTH=1 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" --coop-conn direct "$@" > "$d/log.txt" 2>&1
    echo $? > "$d/exit"
}
pos="1500,$(seq -s, 2900 5 3300),3400"
pair() {   # pair NAME PORT STG:ROOM GUEST_KILL [HOST_INI [FLAGS [HOST_KILL [GUEST_MODS]]]]
    local o=$out/$1 p=$2
    (export RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT="1:0:1:0,2600:$3:0" RECVX_COOP_ENEPOS="$pos" RECVX_FLAGS="$6"
     export RECVX_COOP_KILL="$7" RECVX_COOP_ALIVE=3450
     run "$o/host" "enemy_extra = 5\nenemy_random = 1\nenemy_seed = 88119\n$5" "$out/mods" 3500 --coop-host "$p") &
    sleep 2
    (export RECVX_TELEPORT="1000:$3:0" RECVX_COOP_KILL="$4" RECVX_COOP_ENEPOS="$pos" RECVX_FLAGS="$6" RECVX_COOP_ALIVE=3450
     run "$o/guest" "coop_follow = 0\n" "${8:-$out/mods}" 3560 --coop-join "127.0.0.1:$p#TEST-KEYS") &
    wait
}
# the enemies a side logged at host frame F: "slot:type.look" of the living (dead and hidden ones apart)
types() { sed -n "s/.*coop: test: enemies in [0-9]*\/[0-9]* at host frame $3:\(.*\)/\1/p" "$out/$1/$2/log.txt" | head -1 | tr ' ' '\n' | sed -n 's/^\([0-9]*:[0-9]*\.[0-9]*\)@.*/\1/p' | tr '\n' ' '; }
cmp() {   # cmp NAME [LOOKS]: the two sides' last sample alike (LOOKS 1: a look may differ); the guest's largest step between samples from 2900 to 3300
    python3 - "$out/$1/host/log.txt" "$out/$1/guest/log.txt" "${2:-0}" <<'EOF'
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
looks = sys.argv[3] == '1'
near = lambda s: next((s[k] for k in sorted(s) if 3400 <= k <= 3405), {})   # (a sample's frame may come late over a slow path)
a, b = near(h), near(g)
bad = []
for k in sorted(set(a) | set(b)):
    if k not in a or k not in b:
        bad.append('slot %d only on the %s' % (k, 'host' if k in a else 'guest'))
        continue
    x, y = a[k], b[k]
    tx, ty = (x[0].split('.')[0], y[0].split('.')[0]) if looks else (x[0], y[0])
    if tx != ty or x[4] != y[4] or (not x[4] and x[3] != y[3]): bad.append('slot %d: host %s, guest %s' % (k, x, y))
    elif abs(x[1] - y[1]) + abs(x[2] - y[2]) > 6: bad.append('slot %d at %.0f,%.0f on the host, %.0f,%.0f on the guest' % (k, x[1], x[2], y[1], y[2]))
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
# the types that went to the host's game with their state (the guest's log), sorted, unique
given() { sed -n 's/.*hand-in: enemy [0-9]* (id \([0-9]*\), mode.*with its state after.*/\1/p' "$out/$1/guest/log.txt" | sort -n | uniq | tr '\n' ' '; }
rm -rf "$out"; mkdir -p "$out/mods" "$out/mods_guest"
# M's room file: RM_0020 with record 1 another look (model 32, as record 3) and record 2 30 units off, from the disc image
python3 "$root/pc/tools/rdx.py" --iso "$iso" dump 0020 -o "$out/rm0020.json" > /dev/null &&
python3 -c "
import json, sys; d = json.load(open(sys.argv[1])); e = d['enemies']; e[1]['mdlver'] = 32; e[2]['px'] -= 30; e[2]['pz'] += 10
json.dump(d, open(sys.argv[2], 'w'))" "$out/rm0020.json" "$out/rm0020_mod.json" &&
python3 "$root/pc/tools/rdx.py" --iso "$iso" build 0020 "$out/rm0020_mod.json" -o "$out/mods_guest/RM_0020.RDX" > /dev/null || echo "FAIL: M's room file not made"
rates="enemy_variety = 2\nenemy_rates = 1:0 4:0 9:0 5.0:0 5.1:0 3:0 26:0 19:5 23:0 7:0 11:0 v1:0\n"
pair A "$port" 0:2 1700:3 &
pair B "$((port + 1))" 0:6 1700:1 &
pair C "$((port + 2))" 0:6 1700:23 &
pair D "$((port + 3))" 0:2 "" "" 1:104 3300:0 &
pair M "$((port + 4))" 0:2 "" "enemy_random = 0\n" 1:104 "" "$out/mods_guest" &
pair T1 "$((port + 5))" 0:13 "" &
pair T2 "$((port + 6))" 9:17 "" &
pair T3 "$((port + 7))" 6:9 "" &
pair T4 "$((port + 8))" 1:0 "" "$rates" &
wait
for r in A B C D M T1 T2 T3 T4; do
    check "$r: neither side crashed" "[ \$(cat $out/$r/host/exit) = 0 ] && [ \$(cat $out/$r/guest/exit) = 0 ] && ! grep -q 'fatal signal' $out/$r/*/log.txt"
    cmp $r $([ $r = M ] && echo 1) > "$out/$r/cmp.txt"
    check "$r: afterwards both have the same enemies, HP and dead ones, within 6 units ($(head -1 $out/$r/cmp.txt))" "[ \"\$(head -1 $out/$r/cmp.txt)\" = alike ]"
    check "$r: on the guest nothing jumps when the host comes in ($(tail -1 $out/$r/cmp.txt))" "tail -1 $out/$r/cmp.txt | awk '{ exit !(\$2 <= 12) }'"
done
check "A: the guest alone has the randomized zombies and the extra Cerberus at once" "[ '$(types A guest 1500)' = '0:1.10 1:1.37 2:1.36 3:1.7 5:1.39 39:4.0 ' ]"
check "A: the host makes the guest's extra (no second set)" "grep -q \"enemies: the guest's extra type 4 model 0 (dead flag 0) into slot 39: spawned\" $out/A/host/log.txt && ! grep -q 'enemies: extra Cerberus' $out/A/host/log.txt"
check "B: the guest alone has its extra Bandersnatch at once" "[ '$(types B guest 1500)' = '0:5.1 1:1.37 2:5.0 3:1.41 23:9.0 ' ]"
check "B: the host makes the guest's extra (no second set)" "grep -q \"enemies: the guest's extra type 9 model 0 (dead flag 40) into slot 23: spawned\" $out/B/host/log.txt && ! grep -q 'enemies: extra Bandersnatch' $out/B/host/log.txt"
check "C: the host makes the extra the guest killed dead, its corpse comes with the hand-in" "grep -q \"into slot 23: spawned dead\" $out/C/host/log.txt && grep -q 'hand-in: enemy 23 (id 9, mode 4, hp -[0-9]*, dead) to the host' $out/C/guest/log.txt"
check "C: the corpse lies there on the host too" "grep 'host frame 3400:' $out/C/host/log.txt | grep -q ' 23:9.0@[-0-9]*,[-0-9]*,-[0-9]*,4,D'"
check "D: the walking zombies go to the host with their state" "[ \$(grep -c 'hand-in: enemy [0-9]* (id 1, mode 1, .*with its state' $out/D/guest/log.txt) -ge 4 ]"
check "D: the host's kill of a handed-in zombie counts in its script and the guest's (ed_flg)" "grep -q 'host frame 3450, ed_flg 1 bits' $out/D/host/log.txt && grep -q 'host frame 3450, ed_flg 1 bits' $out/D/guest/log.txt"
check "M: both say the room files differ" "grep -q 'room file RM_0020 differs between the players' $out/M/host/log.txt && grep -q 'room file RM_0020 differs between the players' $out/M/guest/log.txt"
check "M: the zombie in another look is handed in" "grep -q 'hand-in: enemy 1 as the guest.s game has it (in another look here' $out/M/host/log.txt"
check "T1: bats and Spotters go with their state ($(given T1))" "[ '$(given T1)' = '7 11 ' ]"
check "T2: ants go with their state ($(given T2))" "given T2 | grep -qw 10 && given T2 | grep -qw 71"
check "T3: Black Widows go with their state ($(given T3))" "given T3 | grep -qw 3"
check "T4: Tyrants go with their state ($(given T4))" "given T4 | grep -qw 19"
exit $fail
