#!/bin/bash
# Two-instance co-op check (docs/COOP.md "Items from events"): Steve's swap in 3/10 (event 3 on floor 17: he takes the
# sub machine guns, Claire gets the Gold Lugers) skipped by a host that holds no sub machine guns (the guest has them),
# while the guest waits in 3/9. The skip (event 27) trades sub machine guns for the Lugers in the bag; passes when the
# host gets the Lugers all the same, sends them to the guest, and the guest finds them in 3/10. About 3 minutes.
#   pc/tools/coop_lugers_check.sh OUT [PORT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx).
out=$1; port=${2:-48393}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
rm -rf "$out"; mkdir -p "$out/host/save" "$out/guest/save" "$out/host/mods" "$out/guest/mods"
run() {   # run DIR exitframe args...
    local d=$1 x=$2; shift 2
    printf "show_fps = 0\ncoop_follow = 0\n" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$d/mods" \
    RECVX_EXIT_FRAME=$x RECVX_COOP_TRACE=1 RECVX_ITEM_TRACE=1 RECVX_INF_HEALTH=1 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" "$@" > "$d/log.txt" 2>&1
}
RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT=900:3:10:1 RECVX_FLAGS=1:215 RECVX_WARP=3:10:60:44:53.5:0 \
    RECVX_SKIP_CINE=30 run "$out/host" 4500 --coop-host "$port" &
sleep 2
RECVX_TELEPORT=300:3:9:0,1500:3:10:1 RECVX_EQUIP=250:34:0 run "$out/guest" 4560 --coop-join "127.0.0.1:$port#TEST-KEYS" &
wait
h=$out/host/log.txt; g=$out/guest/log.txt; fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
check "the host skipped the swap in 3/10" "grep -q 'cutscene skipped in 3/10' $h"
check "the host got the Gold Lugers" "grep -a -q '^  inventory: .*Gold Lugers(33)' $h"
check "the host sends them to the guest" "grep -a -q 'the partner gets Gold Lugers' $h"
check "the guest keeps them for 3/10" "grep -a -q 'the partner got Gold Lugers .* in room 3/10' $g"
check "the guest finds them in 3/10" "grep -a -q \"the partner's Gold Lugers .* lies here\" $g"
exit $fail
