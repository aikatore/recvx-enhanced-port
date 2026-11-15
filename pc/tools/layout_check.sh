#!/bin/bash
# layout_check.sh BIN_A BIN_B OUTDIR [ISO]
# Determinism check (docs/ARCHITECTURE.md "Determinism"): runs both binaries headless with the same
# scripted input (new game, teleport to 0/6 with the handgun, walk, shoot, idle while two
# zombies change motions), in vanilla and in enhanced mode (in-between images at
# RECVX_INTERP_T=0.5), dumping every 10th frame from 300 to LAST, and compares the dumps
# byte for byte. Exit status 1 if any dump differs or a run did not get to LAST.
#   Two layouts: build a second tree with -DRECVX_LAYOUT_PAD=20500 (unused bytes before all
#   code, data and bss), e.g.
#     cmake -B build-pad -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/linux-i686.cmake \
#           -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRECVX_LAYOUT_PAD=20500 && ninja -C build-pad recvx
#     pc/tools/layout_check.sh build/pc/recvx build-pad/pc/recvx scratch/layout
#   Run to run: the same binary twice.
# Env: LAST (default 1800), MODES (default "vanilla enhanced"), MAX_SECS per run (default 600),
#      INI (extra recvx.ini lines, e.g. "save_system = 1" for the memory card; not
#      "difficulty_select = 1": its menu step moves the game's start off the script's frames),
#      MENU (both runs) or MENU_A / MENU_B (one run): legacy (default: --legacy-menu, the old title menu's NEW GAME),
#      new (the default menus: NEW GAME 10 frames earlier opens GAME SETUP, START GAME where the old NEW GAME is
#      pressed, so the game starts on the same frame), old (a binary from before --legacy-menu: the old menu
#      without the flag). E.g. MENU_A=old MENU_B=new compares an old build with the new default; frames from 300
#      on (the game) are the same.
# Dumps are 640x480 BMPs (~1.2 MB, ~300 per run); OUTDIR is removed first. Never commit it.
set -u
A=$(realpath "$1"); B=$(realpath "$2"); OUT=$(realpath -m "$3")
ISO=${4:-${RECVX_ISO:-$(dirname "$(realpath "$0")")/../../ISO/SLUS-20184 ROM.iso}}
LAST=${LAST:-1800}; MAX_SECS=${MAX_SECS:-600}
last=$(printf "frame_%05d.bmp" "$LAST")
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
walk="400:up,520:up+right,560:up,640:up+circle,760:left,800:up+circle,900:down,960:-,1000:r1,1020:r1+cross,1024:r1,1040:r1+cross,1044:r1,1060:-,1100:up+left,1200:up,1300:-"
newgame_new="90:down,93:-,100:cross,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
MENU=${MENU:-legacy}; MENU_A=${MENU_A:-$MENU}; MENU_B=${MENU_B:-$MENU}
run() { # BIN DIR MODE MENU
  local ng=$newgame args=--legacy-menu
  [ "$4" = new ] && { ng=$newgame_new; args=; }
  [ "$4" = old ] && args=
  mkdir -p "$2/save" "$2/frames" "$2/mods"
  # show_fps: the counter is drawn into the in-between dumps; difficulty_select: NEW GAME starts the
  # game at once (the script's frames; builds without the difficulty menu compare); fixed_cam_controls:
  # Modern, the default before Combined (the script walks with the d-pad, which Combined plays as
  # tank controls; the version line keeps it from becoming Combined); INI: more lines, later ones win (e.g. "fixed_cam_controls = 2")
  { echo "show_fps = 0"; echo "difficulty_select = 0"; echo "fixed_cam_version = 1"; echo "fixed_cam_controls = 1"; [ -n "${INI:-}" ] && printf "%s\n" "$INI"; } > "$2/save/recvx.ini"
  SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$2/mods" \
  RECVX_TELEPORT=1:0:6:0 RECVX_EQUIP=20:5:60 RECVX_INF_AMMO=1 RECVX_INPUT="$ng,$walk" \
  RECVX_INTERP_T=0.5 RECVX_DUMP_FRAMES="$2/frames" RECVX_DUMP_EVERY=10 RECVX_DUMP_FROM=300 \
  timeout -s KILL "$MAX_SECS" "$1" "$ISO" --skip-intro --size 640x480 --"$3" --save-dir "$2/save" $args \
      >"$2/log.txt" 2>&1 &
  local pid=$!
  until [ -f "$2/frames/$last" ] || ! kill -0 $pid 2>/dev/null; do sleep 1; done
  sleep 1; kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null
  rm -f "$2"/frames/rt*.bmp "$2"/frames/win_*.bmp
}
rm -rf "$OUT"; status=0
for mode in ${MODES:-vanilla enhanced}; do
  run "$A" "$OUT/$mode-a" "$mode" "$MENU_A" & run "$B" "$OUT/$mode-b" "$mode" "$MENU_B" & wait
  same=0; diff=0; list=""
  for f in $(cd "$OUT/$mode-a/frames" && ls); do
    [ -f "$OUT/$mode-b/frames/$f" ] || continue
    if cmp -s "$OUT/$mode-a/frames/$f" "$OUT/$mode-b/frames/$f"; then same=$((same + 1)); else diff=$((diff + 1)); list="$list ${f%.bmp}"; fi
  done
  echo "$mode: $same identical, $diff differ${list:+:$list}"
  [ $diff -eq 0 ] || status=1
  for s in a b; do [ -f "$OUT/$mode-$s/frames/$last" ] || { echo "$mode-$s: did not reach frame $LAST"; status=1; }; done
done
exit $status
