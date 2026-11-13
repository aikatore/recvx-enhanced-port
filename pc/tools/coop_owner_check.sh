#!/bin/bash
# Two-instance co-op check (docs/COOP.md "Who runs the enemies", protocol 56): whoever is first in a room runs its enemies until
# it leaves, then hands them to the player staying with their state; the other's copies wait hidden for the runner's state;
# hits reach the runner once. Every pair over an in-order path with jitter and loss in both directions
# (RECVX_COOP_NETSIM, default "40,10,2"), "Extra Enemies" 5, the randomizer, seed 88119; the guest with coop_follow = 0.
#   E1  0/6: the host there from the start, the guest comes at ~1500: on the guest no enemy is shown where its record put it
#       and then moved (no step over 12 units in 5 frames from its first frame in the room)
#   E2  0/6: the guest there from ~1000, the host comes at ~3000: on the host nothing jumps either; the guest runs them on
#       (only records the room's scripts act on go to the host)
#   L1  6/9 (Black Widows): both there (the host first), the host leaves at ~3300: the guest takes each Black Widow (with its
#       state unless it is mid-attack), nothing jumps on the guest (before: the guest ran them from a puppet's pose, its own work
#       stale: one jumped 47 units)
#   L2  0/6: the guest first, the host comes at ~2500, the guest leaves at ~3300: the host takes them with their state
#   R   0/2 with ev_flg 104 (walking zombies): both come in at the same host frame: both list the same enemies after it,
#       nothing jumps
#   H   0/2 with ev_flg 104 (walking zombies): the guest hits slots 0..2 once every 10 frames (RECVX_COOP_KILL, 1 each) while
#       the host goes out and comes back twice (hand-overs both ways under fire): every hit lands (each slot's HP falls by
#       its hits, as the guest shows it)
#   S   as H with Shared Enemy Control (RECVX_COOP_OWNERSHIP=1 on the host)
# About 4 minutes (14 instances at once).
#   pc/tools/coop_owner_check.sh OUT [PORT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), NETSIM ("delay,jitter,loss" or "" none).
out=$1; port=${2:-48581}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
net=${NETSIM-40,10,2}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
run() {   # run DIR INI exitframe args... (the caller exports the test aids)
    local d=$1 ini=$2 x=$3; shift 3
    mkdir -p "$d/save"
    printf "show_fps = 0\n$ini" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
    RECVX_EXIT_FRAME=$x RECVX_INF_HEALTH=1 RECVX_INF_AMMO=1 RECVX_COOP_TRACE=1 env ${net:+RECVX_COOP_NETSIM=$net} \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" --coop-conn direct "$@" > "$d/log.txt" 2>&1
    echo $? > "$d/exit"
}
pos=$(seq -s, 900 5 3800)
pair() {   # pair NAME PORT HOST_TP GUEST_TP [FLAGS [GUEST_KILL [HOST_ENV]]]
    local o=$out/$1 p=$2
    (export RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT="$3" RECVX_COOP_ENEPOS="$pos" RECVX_FLAGS="$5" RECVX_COOP_OWNERSHIP=${7:-0}
     run "$o/host" "enemy_extra = 5\nenemy_random = 1\nenemy_seed = 88119\n" 3850 --coop-host "$p") &
    sleep 2
    (export RECVX_TELEPORT="$4" RECVX_COOP_KILL="$6" RECVX_COOP_ENEPOS="$pos" RECVX_FLAGS="$5"
     run "$o/guest" "coop_follow = 0\n" 3910 --coop-join "127.0.0.1:$p#TEST-KEYS") &
    wait
}
# per side: the largest step of a shown, living enemy between two samples (5 frames apart) in a room visit, and the first
# visible frames' (entry); the two sides' last sample of a room both are in; lost hits (H, S)
ana() {
    python3 - "$out/$1" <<'EOF'
import re, sys
o = sys.argv[1]
def load(side):
    s, fl = [], 0
    for l in open('%s/%s/log.txt' % (o, side), errors='replace'):
        m = re.search(r'coop: tx seq=\d+ flags=(\d+)', l)
        if m: fl = int(m.group(1)); continue
        m = re.search(r'coop: test: enemies in (\S+) at host frame (\d+):(.*)', l)
        if not m: continue
        d = {}
        for e in m.group(3).split():
            k, rest = e.split(':', 1); tl, st = rest.split('@'); v = st.split(',')
            d[int(k)] = (tl, float(v[0]), float(v[1]), int(v[2]), 'D' in v[4:] or 'H' in v[4:])
        s.append((int(m.group(2)), m.group(1), bool(fl & 2), d))
    return s
def steps(s):   # slot -> [(frame a, frame b, room, step per 5 frames, visible)]
    out = {}
    for (fa, ra, va, a), (fb, rb, vb, b) in zip(s, s[1:]):
        if ra != rb or fb - fa > 10 or fb == fa: continue
        for k in set(a) & set(b):
            if a[k][4] or b[k][4] or a[k][0] != b[k][0]: continue
            out.setdefault(k, []).append((fa, fb, ra, (abs(a[k][1] - b[k][1]) + abs(a[k][2] - b[k][2])) * 5.0 / (fb - fa), vb))
    return out
# a jump: a step of a shown enemy more than 6 units (per 5 frames) over the fastest step the other side shows of it within 10
# frames (it moves that fast there: a Hunter's leap, a dog's run), or over 30 where the other side does not show it
def jumps(s, o):
    worst, at = 0.0, ''
    for k, l in s.items():
        for fa, fb, r, d, v in l:
            if not v: continue
            ref = [e[3] for e in o.get(k, []) if e[2] == r and e[0] >= fa - 10 and e[1] <= fb + 10]
            over = d - (max(ref) + 6 if ref else 30)
            if over > 0 and d > worst: worst, at = d, 'slot %d %d->%d (the other side: %s)' % (k, fa, fb, '%.1f' % max(ref) if ref else '-')
    return worst, at
h, g = load('host'), load('guest')
sh, sg = steps(h), steps(g)
jh, ah = jumps(sh, sg); jg, ag = jumps(sg, sh)
print('jump host %.1f %s' % (jh, ah)); print('jump guest %.1f %s' % (jg, ag))   # (0: none over the other side's speed)
# both in one room: their last common sample
hv = {f: (r, d) for f, r, v, d in h}; gv = {f: (r, d) for f, r, v, d in g}
common = [f for f in sorted(set(hv) & set(gv)) if hv[f][0] == gv[f][0]]
bad = []
if common:
    f = common[-1]; a, b = hv[f][1], gv[f][1]
    for k in sorted(set(a) | set(b)):
        x, y = a.get(k), b.get(k)
        if not x or not y:
            if not (x or y)[4]: bad.append('slot %d only on the %s' % (k, 'host' if x else 'guest'))
        elif x[0] != y[0] or x[4] != y[4] or (not x[4] and x[3] != y[3]): bad.append('slot %d: host %s guest %s' % (k, x, y))
        elif not x[4] and abs(x[1] - y[1]) + abs(x[2] - y[2]) > 8: bad.append('slot %d %.0f,%.0f / %.0f,%.0f' % (k, x[1], x[2], y[1], y[2]))
print('alike at %d' % common[-1] if common and not bad else 'differ: ' + '; '.join(bad[:6]) if common else 'never in one room')
# the guest's scripted hits of 1: per slot, how far its HP fell (as the guest shows it: its own or the runner's) from before the
# first hit to 150 frames after the last; a slot that died is left out
hits = {}
for l in open(o + '/guest/log.txt', errors='replace'):
    m = re.search(r'test: hit 1 on enemy (\d+) .* at host frame (\d+)', l)
    if m: hits.setdefault(int(m.group(1)), []).append(int(m.group(2)))
lost = n = 0
for k, fs in hits.items():
    ser = [(f, d[k]) for f, r, v, d in g if k in d]
    a = [e for f, e in ser if f < fs[0]]; b = [e for f, e in ser if f <= fs[-1] + 150]
    if not a or not b or b[-1][4] or b[-1][3] < 0: continue
    n += len(fs); lost += max(0, len(fs) - (a[-1][3] - b[-1][3]))
hits = [0] * n
print('hits %d lost %d' % (len(hits), lost))
EOF
}
rm -rf "$out"; mkdir -p "$out/mods"
hits=""; for f in $(seq 1300 10 2800); do hits="$hits${hits:+,}$f:$(( (f / 10) % 3 )):1"; done
# (the guest joins into the host's room: its teleports go from there, its frames are about the host's)
pair E1 "$port" "1:0:6:0" "1:0:1:0,1200:0:6:0" &
pair E2 "$((port + 1))" "1:0:1:0,2600:0:6:0" "1000:0:6:0" &
pair L1 "$((port + 2))" "1:6:9:0,2900:0:1:0" "" &
pair L2 "$((port + 3))" "1:0:1:0,2100:0:6:0" "1000:0:6:0,1940:0:1:0" &
pair R "$((port + 4))" "1:0:1:0,2600:0:2:0" "1:0:1:0,2574:0:2:0" 1:104 &
pair H "$((port + 5))" "1:0:2:0,1000:0:1:0,250:0:2:0,250:0:1:0,250:0:2:0" "" 1:104 "$hits" &
pair S "$((port + 6))" "1:0:2:0,1000:0:1:0,250:0:2:0,250:0:1:0,250:0:2:0" "" 1:104 "$hits" 1 &
wait
for r in E1 E2 L1 L2 R H S; do
    check "$r: neither side crashed" "[ \$(cat $out/$r/host/exit) = 0 ] && [ \$(cat $out/$r/guest/exit) = 0 ] && ! grep -q 'fatal signal' $out/$r/*/log.txt"
    ana $r > "$out/$r/ana.txt"
    check "$r: no enemy jumps on the host ($(sed -n 1p $out/$r/ana.txt))" "sed -n 1p $out/$r/ana.txt | awk '{ exit !(\$3 == 0) }'"
    check "$r: no enemy jumps on the guest ($(sed -n 2p $out/$r/ana.txt))" "sed -n 2p $out/$r/ana.txt | awk '{ exit !(\$3 == 0) }'"
done
for r in E1 E2 R H S; do check "$r: both have the same enemies, HP and dead ones ($(sed -n 3p $out/$r/ana.txt))" "sed -n 3p $out/$r/ana.txt | grep -q '^alike'"; done
check "E1: the guest's copies waited for the host's state" "grep -q 'own: room 0/6 loaded, the host in it already' $out/E1/guest/log.txt"
check "E2: the guest runs its enemies on (only script records go to the host)" "grep -q 'own: the host comes into the room we ran alone' $out/E2/guest/log.txt && ! grep 'own: enemy .* handed to the partner' $out/E2/guest/log.txt | grep -vq 'scripts act on'"
check "L1: the guest takes every Black Widow (with its state, or let go where it is when mid-attack)" "[ \$(grep -c 'own: enemy [0-9]* (id 3) comes from the partner.s game (it leaves)' $out/L1/guest/log.txt) = \$(grep -c 'own: enemy [0-9]* (id 3) handed to the partner.s game (we leave' $out/L1/host/log.txt) ] && grep -q 'own: enemy [0-9]* (id 3) comes from the partner.s game (it leaves)' $out/L1/guest/log.txt"
check "L2: the host takes the guest's enemies with their state" "[ \$(grep -c 'own: enemy [0-9]* (id [0-9]*) comes from the partner.s game (it leaves): ours' $out/L2/host/log.txt) -ge 2 ]"
for r in H S; do check "$r: no hit lost ($(sed -n 4p $out/$r/ana.txt))" "sed -n 4p $out/$r/ana.txt | grep -q 'lost 0\$' && ! sed -n 4p $out/$r/ana.txt | grep -q 'hits 0 '"; done
exit $fail
