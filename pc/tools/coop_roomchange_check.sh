#!/bin/bash
# Two-instance co-op check (docs/COOP.md "Room changes"): the partner's world event for a room we are still loading.
# A room change clears the room data (room.c bhInitRoom) frames before the new file is set up; a world event of that
# room the partner sent then (its entry block fired at once) used to be read against the cleared script: a crash
# (coop_glue.c scd_sig). RECVX_TEST_READ_HOLD makes the reader's room file take 150 frames more to read (as a disc
# read, or the PS2's wait for a pad's vibration), so the event always comes in that window.
# Normal game: host and guest in 0/8 (a new game), the host to the heliport 6/10, whose entry block (gate 1/304)
# fires at once; the guest follows with the held read. Passes when the guest's event waits until its room is set up,
# then is handled in 6/10 (replayed or found done there), and neither side crashed.
# With RECVX_BIORAND_MODS (a mods folder holding a BioRand pack with random doors in biorand/, e.g. seed 4242) also
# the reported case: both in 6/10 of a BioRand game, the host kills Nosferatu and its read is held; the scene's door
# takes both to 7/24 and the guest, there first, fires 7/24's entry block while the host loads it.
# About 2 minutes (4 with BioRand).
#   pc/tools/coop_roomchange_check.sh OUT [PORT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx), RECVX_BIORAND_MODS (optional).
out=$1; port=${2:-48461}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
run() {   # run DIR MODS INI exitframe args...
    local d=$1 m=$2 ini=$3 x=$4; shift 4
    mkdir -p "$d/save"
    printf "show_fps = 0\n$ini" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$m" \
    RECVX_EXIT_FRAME=$x RECVX_INF_HEALTH=1 RECVX_SKIP_CINE=30 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" "$@" > "$d/log.txt" 2>&1
    echo $? > "$d/exit"
}
rm -rf "$out"; mkdir -p "$out/mods"
o=$out/plain
RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT=1:0:8:0,700:6:10:0 \
    run "$o/host" "$out/mods" "" 1800 --coop-host "$port" &
sleep 2
RECVX_TEST_READ_HOLD=150 run "$o/guest" "$out/mods" "coop_follow = 1\n" 1800 --coop-join "127.0.0.1:$port#TEST-KEYS" &
wait
h=$o/host/log.txt; g=$o/guest/log.txt
check "neither side crashed" "[ \$(cat $o/host/exit) = 0 ] && [ \$(cat $o/guest/exit) = 0 ] && ! grep -q 'fatal signal' $h $g"
check "the guest follows the host from 0/8 to 6/10" "grep -q 'joined the host.s game (stage 0 room 8)' $g && grep -q 'following the host to stage 6 room 10' $g"
check "the host's entry block of 6/10 waits until the guest's room is set up" "grep -q 'world event in 6/10 waits until our room is set up' $g"
check "then the guest handles it in 6/10" \
      "grep -q 'world event block 0212 \\(happened here already\\|is running here already\\|waits until our scene\\)\\|replayed the partner.s world event block 0212' $g"
check "and not as another room's" "! grep -q 'block 0212 is not in our room\\|still not set up' $g"
check "the session lasts until the host leaves" "grep -q 'session ended: The host left the game' $g"
if [ -n "$RECVX_BIORAND_MODS" ]; then   # the lobby's START THE GAME with the BioRand rule on (the title's NEW GAME is no BioRand game)
    o=$out/biorand
    lobby="100:down,103:-,106:down,109:-,112:down,115:-,118:down,121:-,130:cross,133:-"
    start="200:down,203:-,208:down,211:-,216:down,219:-,224:down,227:-,232:down,235:-,240:cross,243:-"
    RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$lobby,$start" RECVX_TELEPORT=1:6:10:0 RECVX_TEST_EXAMINE=400:0 RECVX_ENEMY_KILL=480 \
        RECVX_TEST_READ_HOLD=150 run "$o/host" "$RECVX_BIORAND_MODS" "difficulty_select = 0\ncoop_skip_sync = 0\nbiorand = 1\n" 2600 --coop-host "$((port + 1))" &
    sleep 2
    RECVX_INPUT="$lobby" run "$o/guest" "$RECVX_BIORAND_MODS" "coop_follow = 1\n" 2660 --coop-join "127.0.0.1:$((port + 1))#TEST-KEYS" &
    wait
    h=$o/host/log.txt; g=$o/guest/log.txt
    check "BioRand: neither side crashed" "[ \$(cat $o/host/exit) = 0 ] && [ \$(cat $o/guest/exit) = 0 ] && ! grep -q 'fatal signal' $h $g"
    check "BioRand: a BioRand game" "grep -q 'biorand: this game plays BioRand' $h"
    check "BioRand: Nosferatu dies, the host goes on as Claire to 7/24" \
          "grep -q 'record 2 (type 15) died' $h && grep -q 'door: to 7/24' $h && ! grep -q 'character switch' $h"
    check "BioRand: the guest's entry block of 7/24 waits until the host's room is set up" "grep -q 'world event in 7/24 waits until our room is set up' $h"
    check "BioRand: and not as another room's" "! grep -q 'block 0094 is not in our room\\|still not set up' $h"
fi
exit $fail
