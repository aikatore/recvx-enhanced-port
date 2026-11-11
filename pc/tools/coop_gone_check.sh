#!/bin/bash
# Two-instance co-op check (docs/COOP.md "The shared world"): kills stay dead for both, whoever made them and wherever
# the other player was. A new game in 0/6 (four zombie records, kill bits 12..15) with two extra enemies (host's
# enemy_extra = 3: slots 30 and 31, dead flags 524 and 525); the guest plays with coop_follow = 0. Three runs at once:
#   A  the host goes to 0/7; the guest kills record 1 and extra 30, then leaves for 0/7 too; the host comes back, then
#      the guest. Before, only the host set an extra's dead flag: extra 30 was alive again for both.
#   B  as A, but the guest stays in 0/6 until the host is back (EV_GONE): both see the same enemies in the same slots
#      (before the fix's first half alone, the host's extra 31 took the dead one's slot 30 and the guest's 31 ran on).
#   C  the other way: the guest is in 0/7 while the host kills record 1 and extra 30 and leaves; the guest comes back
#      alone (no extras: only the host adds them).
# RECVX_COOP_ALIVE logs the living enemies; each check compares them. About 3 minutes.
#   pc/tools/coop_gone_check.sh OUT [PORT]
# Environment: RECVX_ISO (disc image), RECVX_BIN (binary, default build/pc/recvx).
out=$1; port=${2:-48471}
root=$(cd "$(dirname "$0")/../.." && pwd)
iso=${RECVX_ISO:-$root/ISO/SLUS-20184 ROM.iso}
bin=${RECVX_BIN:-$root/build/pc/recvx}
[ -n "$out" ] || { echo "usage: $0 OUT [PORT]"; exit 2; }
newgame="100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"
fail=0
check() { if eval "$2"; then echo "ok:   $1"; else echo "FAIL: $1"; fail=1; fi; }
run() {   # run DIR INI exitframe args... (the caller exports the test aids)
    local d=$1 ini=$2 x=$3; shift 3
    mkdir -p "$d/save"
    printf "show_fps = 0\n$ini" > "$d/save/recvx.ini"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy RECVX_SKIP_MOVIES=1 RECVX_UPDATE_CHECK=0 RECVX_MODS="$out/mods" \
    RECVX_EXIT_FRAME=$x RECVX_INF_HEALTH=1 \
    timeout -s KILL 600 "$bin" "$iso" --skip-intro --size 320x240 --vanilla --save-dir "$d/save" "$@" > "$d/log.txt" 2>&1
    echo $? > "$d/exit"
}
pair() {   # pair NAME PORT HOST_TELEPORT HOST_KILL GUEST_TELEPORT GUEST_KILL (RECVX_TELEPORT steps after the first are relative)
    local o=$out/$1 p=$2
    (export RECVX_COOP_CODE=TEST-KEYS RECVX_INPUT="$newgame" RECVX_TELEPORT="$3" RECVX_COOP_KILL="$4" RECVX_COOP_ALIVE=1300,2850,3300
     run "$o/host" "enemy_extra = 3\n" 3400 --coop-host "$p") &
    sleep 2
    (export RECVX_TELEPORT="$5" RECVX_COOP_KILL="$6" RECVX_COOP_ALIVE=1300,3300
     run "$o/guest" "coop_follow = 0\n" 3460 --coop-join "127.0.0.1:$p#TEST-KEYS") &
    wait
}
alive() { sed -n "s/.*coop: test: in 0\/6 at host frame $3, ed_flg [0-9]* bits, alive:\(.*\)/\1/p" "$out/$1/$2/log.txt" | head -1; }
rm -rf "$out"; mkdir -p "$out/mods"
pair A "$port"       1:0:6:0,1400:0:7:0,1000:0:6:0 "" 1650:0:7:0,1000:0:6:0 1850:1,1860:30 &
pair B "$((port + 1))" 1:0:6:0,1400:0:7:0,1000:0:6:0 "" "" 1850:1,1860:30 &
pair C "$((port + 2))" 1:0:6:0,1800:0:7:0 1900:1,1910:30 1300:0:7:0,1000:0:6:0 "" &
wait
for r in A B C; do
    check "$r: neither side crashed" "[ \$(cat $out/$r/host/exit) = 0 ] && [ \$(cat $out/$r/guest/exit) = 0 ] && ! grep -q 'fatal signal' $out/$r/*/log.txt"
    check "$r: 0/6 had records 0..2 and extras 30, 31 at first" "[ '$(alive $r host 1300)' = ' 0:1 1:1 2:1 30:1 31:1' ]"
done
check "A: the guest's kills (record 1 kill bit, extra 30 dead flag 524) reached the host" "grep -q 'enemies: the extra enemy in slot 30 died: ed_flg 524 set' $out/A/guest/log.txt"
check "A: the host, back alone, does not bring extra 30 back" "grep -q 'dead flag 524) at .*: dead (killed before)' $out/A/host/log.txt && [ '$(alive A host 2850)' = ' 0:1 2:1 31:1' ]"
check "A: then both see the same: record 1 and extra 30 dead" "[ '$(alive A host 3300)' = ' 0:1 2:1 31:1' ] && [ '$(alive A guest 3300)' = ' 0:1 2:1 31:1' ]"
check "B: the host takes over the guest's room (EV_GONE)" "grep -q 'the host.s enemies take over' $out/B/guest/log.txt"
check "B: both see the same enemies in the same slots" "[ '$(alive B host 3300)' = ' 0:1 2:1 31:1' ] && [ '$(alive B guest 3300)' = ' 0:1 2:1 31:1' ]"
check "C: the guest, back alone, finds record 1 dead (no extras without the host)" "[ '$(alive C guest 3300)' = ' 0:1 2:1' ]"
exit $fail
