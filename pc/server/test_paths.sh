#!/bin/sh
# ctest rdv_paths: a local recvx-server and two recvx-clients (the game's client code) on 127.0.0.1.
# Each case: the host creates a room, the joiner joins by its code, both exchange a fake game
# stream; checks the path and that answers came back. Cases: Automatic (hole punching), Via server
# (relay forced), punches dropped (relay fallback), a password (right and wrong), 10% loss, an
# unknown room, no end-to-end encryption; a game-sized stream through the relay (sustained
# delivery, no server drops). Both ends must succeed.
# usage: test_paths.sh DIR_WITH_BINARIES [PORT]
bin=$1; port=${2:-17241}; tmp=$(mktemp -d); fails=0
"$bin/recvx-server" --port "$port" --debug --rooms-per-ip-per-min 1000 > "$tmp/server.txt" 2>&1 &
srv=$!
trap 'kill $srv 2>/dev/null; rm -rf "$tmp"' EXIT
sleep 0.3

# run NAME EXPECTED_PATH HOST_OPTS -- JOIN_OPTS
run() {
    name=$1; want=$2; hopts=$3; jopts=$4
    "$bin/recvx-client" 127.0.0.1:$port host --secs 5 $hopts > "$tmp/h.txt" 2>&1 &
    h=$!
    code=""
    for i in $(seq 1 25); do   # up to 5 s (lossy cases lose WELCOME / ROOM)
        code=$(sed -n 's/.*rdv: room \([A-Z0-9-]*\).*/\1/p' "$tmp/h.txt" | head -1)
        [ -n "$code" ] && break
        sleep 0.2
    done
    "$bin/recvx-client" 127.0.0.1:$port join "${CODE_OVERRIDE:-$code}" --secs 3 $jopts > "$tmp/j.txt" 2>&1
    jr=$?
    wait $h
    hr=$?
    res=$(grep RESULT "$tmp/j.txt")
    if [ "$want" = "fail" ]; then
        if [ $jr -ne 0 ] && grep -q "${WANT_MSG:-FAILED}" "$tmp/j.txt"; then echo "ok   $name: $(grep FAILED "$tmp/j.txt")"
        else echo "FAIL $name"; cat "$tmp/j.txt"; fails=$((fails + 1)); fi
        return
    fi
    if [ $jr -eq 0 ] && [ $hr -eq 0 ] && echo "$res" | grep -q "path=$want "; then echo "ok   $name: $res"
    else echo "FAIL $name (want path=$want, joiner exit $jr, host exit $hr)"; cat "$tmp/h.txt" "$tmp/j.txt"; fails=$((fails + 1)); return; fi
    if [ -n "$MIN_PCT" ]; then   # sustained delivery while both ran (the joiner's 3 s): both directions
        jp=$(sed -n 's/.*pings=\([0-9]*\).*/\1/p' "$tmp/j.txt"); ja=$(sed -n 's/.*answers=\([0-9]*\).*/\1/p' "$tmp/j.txt")
        hr=$(sed -n 's/.*received=\([0-9]*\).*/\1/p' "$tmp/h.txt")   # the joiner's pings and answers
        if [ "${jp:-0}" -ge 100 ] && [ $((ja * 100)) -ge $((jp * MIN_PCT)) ] && [ $((hr * 100)) -ge $((2 * jp * MIN_PCT)) ]; then
            echo "ok   $name: joiner sent $jp, $ja answered; host received $hr of $((2 * jp))"
        else echo "FAIL $name: joiner sent $jp, $ja answered; host received $hr of $((2 * jp)) (want >= $MIN_PCT%)"; fails=$((fails + 1)); fi
    fi
}

run "automatic (punched)" "direct\|direct (LAN)" "" ""
run "via server (relay forced)" relay "--relay" "--relay"
run "punching blocked: relay fallback" relay "--no-punch" "--no-punch"
run "joiner via server, host automatic" relay "" "--relay"
run "password" "direct\|direct (LAN)" "--pw s3cret" "--pw s3cret"
WANT_MSG="Wrong password" run "wrong password" fail "--pw s3cret" "--pw nope"
WANT_MSG="room password" run "missing password" fail "--pw s3cret" ""
run "10% loss both ways, relay" relay "--relay --loss 10" "--relay --loss 10"
run "unencrypted (both --plain)" "direct\|direct (LAN)" "--plain" "--plain"
WANT_MSG="not encrypted" run "plaintext host refused by an encrypting joiner" fail "--plain" ""
WANT_MSG="no password" run "password typed, the room has none" fail "" "--pw s3cret"
run "30% loss on the host only, relay" relay "--relay --loss 30" "--relay"
CODE_OVERRIDE=ZZZZ-2222 WANT_MSG="Room not found" run "unknown room" fail "" ""
# a busy room's stream (5 x 1164 bytes a frame each way plus the answers: ~350 KB/s, 300
# datagrams/s per player) through the relay at the default limits: >= 97% delivered, no drops
MIN_PCT=97 run "game-sized stream through the relay" relay "--relay --dgrams 5" "--relay --dgrams 5"
drops=$("$bin/recvx-client" 127.0.0.1:$port status | awk '/^recvx_drops_total\{reason="relay_/ { s += $2 } END { print s + 0 }')
if [ "${drops:-x}" = 0 ]; then echo "ok   no relay drops at the default limits"; else echo "FAIL relay drops: $drops"; fails=$((fails + 1)); fi
echo "server: $(grep -c . "$tmp/server.txt") log lines"
exit $fails
