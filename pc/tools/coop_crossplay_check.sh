#!/bin/bash
# Two-instance co-op check (docs/COOP.md "Windows and Linux"): no address of one game's process crosses the wire, so builds
# whose memory lies elsewhere (Windows and Linux: `ene` 0x30a30f60 / 0x30afcea0) play together. The guest runs BIN2, by
# default this build relinked 1 MB higher (build/pc/recvx_shift: the link line with -Ttext-segment=0x30100000), the host
# BIN; both log every word of an event or enemy state they send that points into their own memory (RECVX_COOP_PTRSCAN). Two
# pairs at once:
#   Z  0/6, three zombies: the guest fires at them, they grab both players (EV_GRAB with the stand-in, hand-overs)
#   T  1/0 with the randomizer's Custom rates (only Tyrants): the guest is there first and runs them, the host comes in at
#      ~2600, the guest leaves at ~3420 and each Tyrant goes to the host's game with its state (its FW_WORK: before
#      protocol 62 its ewP at 0xC0 went as the guest's address)
# Checks: neither side crashed, no pointer on the wire, Z had a grab and T handed a Tyrant over with its state.
#   pc/tools/coop_crossplay_check.sh OUT [PORT]
# Environment: RECVX_ISO, RECVX_BIN (host), RECVX_BIN2 (guest, default build/pc/recvx_shift), NETEM (as coop_scene_check.sh).
out=$1; port=${2:-48501}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
bin2=${RECVX_BIN2:-$root/build/pc/recvx_shift}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT]"; exit 2; }
[ -e "$out" ] && { echo "$out exists"; exit 2; }
[ -x "$bin2" ] || { echo "no $bin2 (relink with -Ttext-segment=0x30100000)"; exit 2; }
[ -n "$NETEM" ] && { tc qdisc add dev lo root netem $NETEM || exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fire="1300:r1"; for f in $(seq 1310 25 2600); do fire="$fire,$f:r1+cross,$((f + 4)):r1"; done; fire="$fire,2600:-"
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
run() {   # run DIR BIN INI exitframe args...
    local d=$1 b=$2 ini=$3 x=$4; shift 4
    mkdir -p "$d/save" "$d/mods"
    printf "show_fps = 0\n$ini" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$d/mods" \
    RECVX_EXIT_FRAME=$x RECVX_INF_HEALTH=1 RECVX_COOP_PTRSCAN=1 \
    timeout -s KILL 600 "$b" "$iso" --skip-intro --legacy-menu --size 320x240 --vanilla --save-dir "$d/save" --coop-conn direct "$@" > "$d/log.txt" 2>&1
    echo $? > "$d/exit"
}
rates="enemy_random = 1\nenemy_seed = 88119\nenemy_variety = 2\nenemy_rates = 1:0 4:0 9:0 5.0:0 5.1:0 3:0 26:0 19:5 23:0 7:0 11:0 v1:0\n"
(RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT=1:0:6:0 run "$out/Z/host" "$bin" "" 2800 --coop-host "$port") &
(RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT="1:0:1:0,2600:1:0:0" run "$out/T/host" "$bin" "$rates" 3560 --coop-host "$((port + 1))") &
sleep 2
(RECVX_EQUIP="200:5:200" RECVX_INF_AMMO=1 RECVX_INPUT="$fire" run "$out/Z/guest" "$bin2" "" 2860 --coop-join "127.0.0.1:$port#TEST-KEYS") &
(RECVX_TELEPORT="1000:1:0:0,2015:0:1:0" run "$out/T/guest" "$bin2" "coop_follow = 0\n" 3620 --coop-join "127.0.0.1:$((port + 1))#TEST-KEYS") &
wait
for r in Z T; do
    check "$r: neither side crashed" "[ \$(cat $out/$r/host/exit) = 0 ] && [ \$(cat $out/$r/guest/exit) = 0 ] && ! grep -q 'fatal signal' $out/$r/*/log.txt"
    check "$r: no pointer on the wire ($(cat $out/$r/*/log.txt | grep -c 'pointer on the wire'))" "! grep -q 'pointer on the wire' $out/$r/host/log.txt $out/$r/guest/log.txt"
done
check "Z: a zombie grabbed a player of the other game" "grep -q 'grabs us' $out/Z/host/log.txt $out/Z/guest/log.txt"
check "T: a Tyrant went to the host's game with its state" "grep -q 'own: enemy [0-9]* (id 19) handed to the partner.s game (we leave the room) at' $out/T/guest/log.txt"
exit $fail
