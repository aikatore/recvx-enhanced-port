#!/bin/sh
# ctest rdv_handover: updating a running recvx-server (docs/SERVER.md "Updating without dropping
# sessions"). A server with a control socket and a fake systemd notify socket; a relayed session,
# a direct one and a host waiting for its partner; then
#   - a take-over by a build that cannot read the state (exit 3): the old server goes on;
#   - a take-over by a second binary while relay traffic flows: exit 0, the old process exits 0,
#     no session drops, the waiting host's code still joins, the counters only grow, systemd
#     hears MAINPID / BARRIER / READY;
#   - status-json parsed by python3 (no room codes or addresses in it);
#   - drain: new rooms refused ("full or busy"), the open room goes on, the server stops after it;
#   - the control socket: mode 0600, a stale one replaced, a live one refused;
#   - exactly one server after a take-over that is slow or queued: behind a silent control
#     connection, a systemd that answers the barrier after the time-out, the old server frozen
#     after "ok" until the new one's time-out has passed.
# usage: test_handover.sh DIR_WITH_BINARIES [PORT]
bin=$1; port=${2:-17251}; tmp=$(mktemp -d); fails=0
command -v python3 >/dev/null || { echo "python3 is missing"; exit 77; }
srv() { "$bin/recvx-client" 127.0.0.1:$port "$@"; }
ok() { echo "ok   $1"; }
alive() { s=$(ps -o stat= -p "$1" 2>/dev/null); [ -n "$s" ] && [ "${s#Z}" = "$s" ]; }   # a zombie (nobody reaped it yet) is gone
bad() { echo "FAIL $1"; fails=$((fails + 1)); }
code_of() { for i in $(seq 1 25); do c=$(sed -n 's/.*rdv: room \([A-Z0-9-]*\).*/\1/p' "$1" | head -1); [ -n "$c" ] && { echo "$c"; return; }; sleep 0.2; done; }
ctl=$tmp/control.sock
p2=$((port + 2)); ctl_s=10   # main.c CTL_TIMEOUT_MS
cleanup() { kill $notifier $pids 2>/dev/null; pkill -f "recvx-server.*--port ($port|$p2)" 2>/dev/null; rm -rf "$tmp"; }
trap cleanup EXIT

# a fake systemd: logs each notify message, closes what a BARRIER passes (that answers it)
python3 -c '
import os, socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); s.bind(sys.argv[1]); out = open(sys.argv[2], "a", buffering=1)
while True:
    msg, anc, flags, addr = s.recvmsg(4096, socket.CMSG_SPACE(64))
    for lvl, typ, data in anc:
        for i in range(0, len(data) - len(data) % 4, 4): os.close(int.from_bytes(data[i:i+4], "little"))
    out.write(msg.decode() + "\n")
' "$tmp/notify" "$tmp/notify.txt" &
notifier=$!
sleep 0.3
export NOTIFY_SOCKET=$tmp/notify

"$bin/recvx-server" --port $port --control "$ctl" --rooms-per-ip-per-min 1000 --rooms-per-ip 100 --debug > "$tmp/s1.txt" 2>&1 &
old=$!
sleep 0.3
[ "$(stat -c %a "$ctl")" = 600 ] && ok "control socket mode 0600" || bad "control socket mode $(stat -c %a "$ctl")"
grep -qx "READY=1" "$tmp/notify.txt" && ok "READY=1 at the start" || bad "no READY=1 at the start"

# the sessions: relayed (both forced), automatic (direct on 127.0.0.1), a host waiting
srv host --secs 16 --relay > "$tmp/rh.txt" 2>&1 & rh=$!
srv host --secs 16 > "$tmp/dh.txt" 2>&1 & dh=$!
srv host --secs 16 > "$tmp/wh.txt" 2>&1 & wh=$!
pids="$rh $dh $wh"
rcode=$(code_of "$tmp/rh.txt"); dcode=$(code_of "$tmp/dh.txt"); wcode=$(code_of "$tmp/wh.txt")
srv join "$rcode" --secs 12 --relay --dgrams 2 > "$tmp/rj.txt" 2>&1 & rj=$!
srv join "$dcode" --secs 12 > "$tmp/dj.txt" 2>&1 & dj=$!
pids="$pids $rj $dj"
sleep 3
srv status-json > "$tmp/before.json" || bad "status-json before"

# a build that cannot read this state: exit 3, nothing taken over
RECVX_SERVER_TEST_STATE_FORMAT=99 "$bin/recvx-server" --take-over --port $port --control "$ctl" > "$tmp/t0.txt" 2>&1
r=$?
if [ $r -eq 3 ] && kill -0 $old 2>/dev/null && grep -q "nothing handed over, the server goes on" "$tmp/s1.txt"; then ok "incompatible take-over: exit 3, the old server goes on"
else bad "incompatible take-over: exit $r"; cat "$tmp/t0.txt"; fi
srv status-json > "$tmp/mid.json" && ok "the old server still answers" || bad "the old server does not answer after the refused take-over"

# the take-over by a second binary, relay traffic flowing
mkdir "$tmp/v2" && cp "$bin/recvx-server" "$tmp/v2/"
"$tmp/v2/recvx-server" --take-over --port $port --control "$ctl" --rooms-per-ip-per-min 1000 --rooms-per-ip 100 --debug > "$tmp/s2.txt" 2>&1
r=$?
wait $old
ro=$?
if [ $r -eq 0 ] && [ $ro -eq 0 ] && grep -q "handed over to pid" "$tmp/s1.txt"; then ok "take-over: exit 0, the old process exited 0"
else bad "take-over: exit $r, the old process $ro"; cat "$tmp/s1.txt" "$tmp/s2.txt"; fi
new=$(pgrep -f "$tmp/v2/recvx-server")
grep -qx "MAINPID=$new" "$tmp/notify.txt" && grep -qx "BARRIER=1" "$tmp/notify.txt" && [ "$(grep -cx 'READY=1' "$tmp/notify.txt")" = 2 ] \
    && ok "systemd: MAINPID=$new, BARRIER, READY" || { bad "systemd notify"; cat "$tmp/notify.txt"; }
sleep 1
srv status-json > "$tmp/after.json" || bad "status-json after"
srv join "$wcode" --secs 3 > "$tmp/wj.txt" 2>&1 && ok "a code from before the take-over joins: $(grep RESULT "$tmp/wj.txt")" || { bad "join of the waiting room after the take-over"; cat "$tmp/wj.txt"; }

python3 - "$tmp/before.json" "$tmp/after.json" "$rcode $dcode $wcode" <<'EOF' && ok "status-json: valid, no codes or addresses, counters only grow" || bad "status-json"
import json, sys
b, a = (json.load(open(p)) for p in sys.argv[1:3])
for t in (open(sys.argv[1]).read(), open(sys.argv[2]).read()):
    assert t.count("\n") <= 1, "one line"
    for c in sys.argv[3].split(): assert c not in t and c.replace("-", "") not in t, "a room code"
    assert "127.0.0.1" not in t, "an address"
for k in ("version", "state_format", "started_at", "uptime_s", "process_started_at", "handovers", "draining", "handover_capable", "rooms", "room_list", "players", "sessions", "counters", "drops"):
    assert k in a, k
assert a["handover_capable"] is True and a["draining"] is False and a["state_format"] == 1
assert b["handovers"] == 0 and a["handovers"] == 1 and a["started_at"] == b["started_at"] and b["uptime_s"] >= 2 and a["uptime_s"] >= b["uptime_s"], "service uptime"
assert a["process_started_at"] >= b["process_started_at"] and a["drops"] == a["counters"]["drops"] == a["counters"]["recvx_drops_total"]
assert b["rooms"] == 3 and a["rooms"] == 3, (b["rooms"], a["rooms"])
assert b["sessions"]["relay"] == 1 and a["sessions"]["relay"] == 1, (b["sessions"], a["sessions"])
assert {r["id"] for r in b["room_list"]} == {r["id"] for r in a["room_list"]}, "room ids change"
assert all(set(r) == {"id", "age_s", "players", "path", "relay_bps"} for r in a["room_list"])
assert sorted(r["path"] for r in a["room_list"]) == ["direct", "relay", "waiting"], a["room_list"]
def flat(d, p=""):
    for k, v in d.items():
        if isinstance(v, dict): yield from flat(v, p + k + ".")
        else: yield p + k, v
bc, ac = dict(flat(b["counters"])), dict(flat(a["counters"]))
assert set(bc) == set(ac) and "drops.relay_auth" in ac and "recvx_rooms_opened_total" in ac
for k, v in bc.items():
    if k.endswith("_total") or "_total." in k or k.startswith("drops."): assert ac[k] >= v, (k, v, ac[k])
assert ac["recvx_relay_datagrams_total.out"] > bc["recvx_relay_datagrams_total.out"], "relay traffic after the take-over"
assert ac["recvx_rooms_opened_total"] == bc["recvx_rooms_opened_total"] == 3
EOF

# the sessions across the take-over: both ends succeed, nearly every ping answered, no room lost
wait $rj; r1=$?; wait $dj; r2=$?; wait $rh; r3=$?; wait $dh; r4=$?
for f in rj dj; do
    p=$(sed -n 's/.*pings=\([0-9]*\).*/\1/p' "$tmp/$f.txt"); a=$(sed -n 's/.*answers=\([0-9]*\).*/\1/p' "$tmp/$f.txt")
    if [ "${p:-0}" -ge 100 ] && [ $((a * 100)) -ge $((p * 95)) ]; then ok "$f: $(grep RESULT "$tmp/$f.txt")"; else bad "$f: pings $p answers $a"; fi
done
if [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $r3 -eq 0 ] && [ $r4 -eq 0 ] && grep -q "path=relay" "$tmp/rj.txt" && ! grep -q "lost" "$tmp/rh.txt" "$tmp/rj.txt" "$tmp/dh.txt" "$tmp/dj.txt" "$tmp/wh.txt"
then ok "no session dropped"; else bad "sessions: exits $r1 $r2 $r3 $r4"; grep -h "lost\|FAILED" "$tmp"/?[hj].txt; fi
wait $wh
pids=""

# drain: an open room goes on, a new one is refused, the server stops when the room closed
srv host --secs 4 > "$tmp/h3.txt" 2>&1 & h3=$!
pids=$h3
code_of "$tmp/h3.txt" > /dev/null
"$tmp/v2/recvx-server" --drain --control "$ctl" > "$tmp/drain.txt" 2>&1 && grep -q "draining, [0-9]* rooms open" "$tmp/drain.txt" && ok "--drain: $(cat "$tmp/drain.txt")" || { bad "--drain"; cat "$tmp/drain.txt"; }
srv status-json | python3 -c 'import json, sys; d = json.load(sys.stdin); assert d["draining"] is True and d["rooms"] >= 1' && ok "status-json: draining" || bad "status-json while draining"
srv host --secs 3 > "$tmp/h4.txt" 2>&1
grep -q "full or busy" "$tmp/h4.txt" && ok "draining: a new room refused: $(grep -o 'The co-op server is full or busy' "$tmp/h4.txt" | head -1)" || { bad "draining: a new room"; cat "$tmp/h4.txt"; }
alive $new && ok "draining: the server waits for the open room" || bad "draining: the server stopped with a room open"
wait $h3
for i in $(seq 1 50); do alive $new || break; sleep 0.1; done
if ! alive $new && grep -q "drained, the last room closed" "$tmp/s2.txt" && [ ! -e "$ctl" ]; then ok "drained: stopped after the last room, control socket removed"
else bad "drain did not stop the server"; ps -o pid,stat,cmd -p $new; ls -la "$ctl"; tail -3 "$tmp/s2.txt"; fi

# the control socket file: a stale one (a killed server) is replaced, a live one is refused
"$bin/recvx-server" --port $port --control "$ctl" > "$tmp/s3.txt" 2>&1 & s3=$!
sleep 0.3; kill -9 $s3; wait $s3 2>/dev/null
"$bin/recvx-server" --port $port --control "$ctl" > "$tmp/s4.txt" 2>&1 & s4=$!
sleep 0.3
kill -0 $s4 2>/dev/null && ok "a stale control socket is replaced" || { bad "stale control socket"; cat "$tmp/s4.txt"; }
"$bin/recvx-server" --port $((port + 1)) --control "$ctl" > "$tmp/s5.txt" 2>&1
r=$?
[ $r -ne 0 ] && grep -q "in use" "$tmp/s5.txt" && ok "a live control socket is refused" || { bad "a second server on a live control socket: exit $r"; cat "$tmp/s5.txt"; }
"$bin/recvx-server" --drain --port $port --control "$tmp/none.sock" > /dev/null 2>&1 && bad "--drain without a server succeeded" || ok "--drain without a server fails"
kill $s4; wait $s4
[ ! -e "$ctl" ] && ok "SIGTERM removes the control socket" || bad "control socket left after SIGTERM"
if grep -E "$rcode|$dcode|$wcode" "$tmp/s1.txt" "$tmp/s2.txt"; then bad "a room code in the log"; else ok "no room codes in the logs"; fi

# one server after a take-over, whatever is slow: a fake systemd on its own socket per take-over
# that answers the BARRIER after $3 s, or (a pid in $4) at once but freezes that pid for $3 s
slow_systemd() {
    python3 -c '
import os, signal, socket, sys, threading
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM); s.bind(sys.argv[1]); out = open(sys.argv[2], "a", buffering=1)
wait, pid = float(sys.argv[3]), int(sys.argv[4])
def answer(fds):
    for f in fds: os.close(f)
while True:   # (the log goes on meanwhile: a MAINPID back to the old one is seen)
    msg, anc, flags, addr = s.recvmsg(4096, socket.CMSG_SPACE(64))
    out.write(msg.decode() + "\n")
    for lvl, typ, data in anc:
        fds = [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data) - len(data) % 4, 4)]
        if pid: os.kill(pid, signal.SIGSTOP); answer(fds); threading.Timer(wait, os.kill, (pid, signal.SIGCONT)).start()
        else: threading.Timer(wait, answer, (fds,)).start()
' "$1" "$2" "$3" "${4:-0}" &
    pids="$pids $!"
    sleep 0.3
}
# take_over NAME SLOW_SYSTEMD_ARGS: a take-over on port p2; then exactly one server: the new one
# (exit 0) or the old one; the last MAINPID systemd heard (if any) is that one, and it answers
take_over() {
    t=$1; shift
    slow_systemd "$tmp/$t.sock" "$tmp/$t.notify" "$@"
    NOTIFY_SOCKET=$tmp/$t.sock "$bin/recvx-server" --take-over --port $p2 --control "$tmp/c2.sock" > "$tmp/$t.txt" 2>&1
    r=$?
    sleep 0.5
    new2=$(pgrep -f "recvx-server --take-over --port $p2")
    if [ $r -eq 0 ]; then want=$new2; alive $s6 && want=; else want=$s6; [ -n "$new2" ] && want=; fi
    m=$(sed -n 's/^MAINPID=//p' "$tmp/$t.notify" | tail -1)
    if [ -n "$want" ] && alive $want && { [ -z "$m" ] || [ "$m" = "$want" ]; } && "$bin/recvx-client" 127.0.0.1:$p2 status-json > /dev/null; then return 0; fi
    echo "  take-over exit $r, old $s6 $(alive $s6 && echo alive || echo gone), new '$new2', MAINPID '$m'"; cat "$tmp/$t.txt"
    return 1
}
"$bin/recvx-server" --port $p2 --control "$tmp/c2.sock" > "$tmp/s6.txt" 2>&1 & s6=$!
sleep 0.3
python3 -c 'import socket, sys, time; s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(30)' "$tmp/c2.sock" & holder=$!
sleep 0.5
take_over t1 1 && ok "take-over behind a silent control connection (busy or late): one server" || bad "take-over behind a silent control connection"
kill $holder; sleep 0.3
take_over t2 $((ctl_s + 1)) && [ $r -ne 0 ] && ok "systemd slower than the time-out: no \"ok\", the old server goes on" || bad "a slow systemd barrier"
take_over t3 $((ctl_s + 1)) $s6 && [ $r -eq 0 ] && ok "the old server frozen after \"ok\" past the time-out: the new one serves" || bad "the old server frozen after ok"
wait $s6; [ $? -eq 0 ] && ok "the old server exited 0" || bad "the old server's exit"
kill $new2 2>/dev/null
exit $fails
