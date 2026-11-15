#!/bin/sh
# Headless room test (docs/modding/BUILDING_ROOMS.md "Headless room testing"): new game, teleport, frame dumps, log.
#   pc/tools/room_test.sh OUT STAGE:ROOM:POS [SECONDS]
# Environment: RECVX_ISO (disc image), RECVX_MODS (mods folder, default ./mods), and any test
# aid of docs/ARCHITECTURE.md "Environment variables", e.g. RECVX_WARP, extra RECVX_INPUT entries in MORE_INPUT, RECVX_BIN (binary)
# (presented frames; the new game is started by frame ~300, the teleport lands ~frame 330).
# Output: OUT/frames/*.bmp (every 30th frame), OUT/log.txt (look for "mods:", "read file =").
set -e
out=$1; target=$2; secs=${3:-60}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
mkdir -p "$out/save" "$out/frames"
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 \
RECVX_DUMP_FRAMES="$out/frames" RECVX_TELEPORT="1:$target" \
RECVX_INPUT="$newgame${MORE_INPUT:+,$MORE_INPUT}" \
timeout -s KILL "$secs" "${RECVX_BIN:-$root/build/pc/recvx}" "$iso" --skip-intro --legacy-menu --size ${SIZE:-320x240} \
    --save-dir "$out/save" --log="$out/log.txt" > "$out/stdout.txt" 2>&1 || true
echo "frames: $(ls "$out/frames" | wc -l), log: $out/log.txt"
