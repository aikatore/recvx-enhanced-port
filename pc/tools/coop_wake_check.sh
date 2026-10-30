#!/bin/bash
# Two-instance co-op check (docs/COOP.md "Enemies a room script wakes"): host and guest over localhost, a new game,
# both in 0/8 by its first door, where three zombies wait, not hittable, until a player crosses floor 6 or 7. Only the
# guest steps onto floor 6 (host frame 1100) and fires; the host stays at the door. Passes when the host's zombies wake
# for the guest (the host logs the partner on floor 6, its zombie in slot 0 turns hittable and leaves its waiting type
# 8) and the guest's shots land (EV_ENE_HIT applied), and when neither side logs an enemy without an active owner
# (STALL: the host's enemies did not run while the partner played in the room; ORPHAN: a guest puppet without the
# host's state). About 2 minutes.
#   pc/tools/coop_wake_check.sh OUT [PORT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx).
out=$1; port=${2:-48391}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fire="1300:r1"; for f in $(seq 1310 20 2000); do fire="$fire,$f:r1+cross,$((f + 4)):r1"; done; fire="$fire,2000:-"
rm -rf "$out"; mkdir -p "$out/host/save" "$out/guest/save" "$out/host/mods" "$out/guest/mods"
run() {   # run DIR exitframe args...
    local d=$1 x=$2; shift 2
    echo "show_fps = 0" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$d/mods" \
    RECVX_EXIT_FRAME=$x RECVX_COOP_TRACE=1 RECVX_ENEMY_TRACE=2000 RECVX_INF_HEALTH=1 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" "$@" > "$d/log.txt" 2>&1
}
RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT=1:0:8:0 run "$out/host" 2200 --coop-host "$port" &
sleep 2
RECVX_COOP_GOTO=1100:-22:50:32768 RECVX_EQUIP=200:5:200 RECVX_INF_AMMO=1 RECVX_INPUT="$fire" \
    run "$out/guest" 2260 --coop-join "127.0.0.1:$port#TEST-KEYS" &
wait
h=$out/host/log.txt; g=$out/guest/log.txt; fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
check "the guest joined in 0/8" "grep -q 'joined the host.s game (stage 0 room 8)' $g"
check "the host counts the guest on floor 6" "grep -q 'the partner stands on floor 6 of 0/8' $h"
check "the host's zombie 0 wakes for the guest (type 0, hittable, the guest its target)" \
      "grep -E '^etr g=[0-9]+ s=0 id=1 ty=0 t=P .* run .* fl=[0-9a-f]*[2367abef][0-9a-f] ' $h | grep -q ."
check "the guest's shots land" "[ \$(grep -c 'event [0-9]* in type=1 ' $h) -ge 3 ]"
check "no enemy without an active owner" "! grep -q 'STALL\\|ORPHAN' $h $g"
exit $fail
