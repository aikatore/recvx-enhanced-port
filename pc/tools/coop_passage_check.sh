#!/bin/bash
# Two-instance co-op check (docs/COOP.md "The room as the partner's flags have it"): the palace's passage to the residence
# (1/10). Its trigger 1 at the foot of the stairs first starts the scene of Alfred's laugh, which takes the player through
# to the residence (2/0) and sets mp_flg 70; once that is set, the trigger is the door to 2/0. One player (the watcher)
# examines the trigger while the other stands halfway up the passage; then the other walks to the foot of the stairs and
# presses the action button. Passes when the scene ran for the watcher alone and the other player goes through to 2/0
# as well (before, its trigger stayed the scene's, which no longer answers: stuck at the foot of the stairs until it
# entered 1/10 again). About 2 minutes.
#   pc/tools/coop_passage_check.sh OUT [PORT] [host|guest]   (who watches the scene, default host)
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx).
out=$1; port=${2:-48395}; watcher=${3:-host}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT] [host|guest]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
press=""; for f in $(seq 2000 30 3000); do press="$press${press:+,}$f:cross,$((f + 4)):-"; done
rm -rf "$out"; mkdir -p "$out/host/save" "$out/guest/save" "$out/host/mods" "$out/guest/mods"
run() {   # run DIR exitframe args...
    local d=$1 x=$2; shift 2
    printf "show_fps = 0\ncoop_follow = 0\n" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$d/mods" \
    RECVX_EXIT_FRAME=$x RECVX_COOP_TRACE=1 RECVX_INF_HEALTH=1 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" --coop-conn direct "$@" > "$d/log.txt" 2>&1
}
# the watcher examines trigger 1 at game frame 900; the other is halfway up the passage, then at the foot of the stairs
# (z 24, facing the trigger) from host frame 1800, pressing the action button
other="RECVX_COOP_GOTO=600:12.5:150:0,1800:12.5:24:0"
if [ "$watcher" = host ]; then w=host; o=guest; hw="RECVX_TEST_EXAMINE=900:1 RECVX_INPUT=$newgame"; gw="$other RECVX_INPUT=$press"
else w=guest; o=host; hw="$other RECVX_INPUT=$newgame,$press"; gw="RECVX_TEST_EXAMINE=900:1"; fi
(export RECVX_COOP_CODE=TEST-KEYS RECVX_TELEPORT=1:1:10:1 $hw; run "$out/host" 3200 --coop-host "$port") &
sleep 2
(export $gw; run "$out/guest" 3260 --coop-join "127.0.0.1:$port#TEST-KEYS") &
wait
wl=$out/$w/log.txt; ol=$out/$o/log.txt; fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
check "both in 1/10" "grep -q 'joined the host.s game (stage 1 room 10)' $out/guest/log.txt"
check "the $w watches the scene (block 03a4, mp_flg 70)" "grep -q 'world event block 03a4 fired here (gate flag 8/70)' $wl"
check "the $w goes through to 2/0" "grep -q 'read file = RM_2000' $wl"
check "the $o gets the flags only" "grep -q 'block 03a4 takes it through a door: its flags only here' $ol"
check "the $o's trigger 1 becomes the door (entry script)" "grep -q 'world flags of 1/10: [0-9]* writes of its entry script run again' $ol"
check "the $o goes down to 2/0 as well" "grep -q 'read file = RM_2000' $ol"
exit $fail
