#!/bin/bash
# Game over retry check: Chris (part 2) in the prison's main gate (0/3 case 1) with "Extra Item Boxes and Herbs" on (an
# extra item box and planter stand there), a retry point there, killed by two zombies, "Yes" on the retry. Passes when
# the retry sets the room up again from the same file (both loads expand to the same size) and nothing crashed. Before
# the fix the extras' models loaded their textures while the retry read the room; the texture swizzle scratch lay at
# the room arena's top, over the packed room file, and the room set up from it crashed (bhSetRoom). About 2 minutes.
#   pc/tools/retry_check.sh OUT
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx).
out=$1
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT"; exit 2; }
input="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
for f in $(seq 900 40 2500); do input="$input,$f:cross,$((f + 3)):-"; done   # the retry's "Yes" (its default)
rm -rf "$out"; mkdir -p "$out/save"
printf 'extra_objects = 1\nshow_fps = 0\n' > "$out/save/recvx.ini"
(cd "$out" && SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_SKIP_CINE=30 \
    RECVX_TELEPORT=1:0:3:0:1 RECVX_PLAYER=1 RECVX_RETRY=300 RECVX_HP=380:1 RECVX_SPAWN=400:1@3,410:1@3 RECVX_INPUT="$input" \
    RECVX_EXIT_FRAME=2600 timeout -s KILL 300 "$bin" "$iso" --skip-intro --size 320x240 --save-dir save > log.txt 2>&1)
l=$out/log.txt; fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
check "the extras are in the room" "grep -aq 'extras: item_box in 0/3 case 1' $l"
check "the player died (health set, zombies hit)" "[ \$(grep -ac 'player hit' $l) -ge 2 ]"
check "the retry set the room up again" "grep -aq 'room: RM_0031 ready .* after the game start' $l"
check "both loads expanded the same file" "[ \$(grep -a 'room: RM_0031 .* KB expanded' $l | sed 's/,.*//' | sort -u | wc -l) -eq 1 ]"
check "no crash" "[ ! -e $out/recvx-crash.log ]"
exit $fail
