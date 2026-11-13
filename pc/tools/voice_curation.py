# pc/src/host/voice_curation.inc ("Randomize Voice Lines": the lines checked by hand, voices.c) from the local voice tool's
# voice_curation.json (docs/ACTORS.md "Voice lines", "Lines checked by hand"), and from voice_played.json (beside it) how long
# each line plays in each scene, measured in the game. Ids and numbers only: no sound, no text.
# usage: voice_curation.py voice_curation.json [pc/src/host/voice_curation.inc [voice_played.json]]
#        voice_curation.py --measure voice_played.json LOG...   (the logs of headless runs with RECVX_VOICE_TIME=1, voices off)
import json, os, re, sys

def measure(out, logs):
    """the voicetime lines of each log (cast_glue.c pc_voice_on, pc_voice_stop: game ms, 30 frames a second) -> voice_played.json;
    a run whose frames fell behind the clock (under 97 %: an AdxCk wait then ends in fewer frames) left out"""
    R = re.compile(r"^\[ *([0-9.]+)\] voicetime: (\d+) (?:on room (\d+) ofs (-?\d+) line (\d+)/(\d+) \((-?\d+) ms\)|stop)")
    RA = re.compile(r"adxt: (?:start \d+/(\d+) .* talk 1$|stop talk 1 at (\d+) of \d+ ms|end talk 1 )")   # (RECVX_CAST_TRACE: the voice slot's player)
    runs = []
    for p in logs:
        ev, real, end, cur = [], [], 0, None
        for l in open(p, errors="replace"):
            m = R.match(l)
            if m:
                ev.append([int(m[2])] + ([int(m[i]) for i in range(3, 8)] if m[3] else []))
                real.append(float(m[1]))
            elif (a := RA.search(l.rstrip("\n"))):   # how long the line's sound really played (element 6): stopped at, or its end
                if a[1]: cur = next((e for e in reversed(ev) if len(e) == 6 and e[4] == int(a[1])), None)
                elif cur is not None: cur.append(int(a[2]) if a[2] else cur[5]); cur = None
            elif "test: exit at frame" in l: end = int(l.split()[-1]) * 1000 // 30
        if len(ev) > 1 and (ev[-1][0] - ev[0][0]) < 1000 * (real[-1] - real[0]) * 0.97:   # (the game slower than the clock: a busy machine)
            print("voice_curation.py: %s: the frames fell behind, left out" % p); continue
        if ev: runs.append({"log": os.path.basename(os.path.dirname(os.path.abspath(p))), "end": end or ev[-1][0], "events": ev})
    json.dump({"version": 1, "runs": runs}, open(out, "w"), separators=(",", ":"))
    print("voice_curation.py: %d runs, %d lines -> %s" % (len(runs), sum(len(e) > 1 for r in runs for e in r["events"]), out))

if sys.argv[1] == "--measure": measure(sys.argv[2], sys.argv[3:]); sys.exit(0)
src = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else __file__.rsplit("/", 2)[0] + "/src/host/voice_curation.inc"
pfile = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(src)), "voice_played.json")
d = json.load(open(src, encoding="utf-8"))
if d.get("version") != 1: sys.exit("voice_curation.py: format version %r, not 1" % d.get("version"))
CHARS = ["Claire", "Chris", "Steve", "Wesker", "Alfred", "Alexia", "Rodrigo"]   # cast.h's characters in order (test_cast): "speaker" indexes them
if d.get("characters") != CHARS: sys.exit("voice_curation.py: characters %r, not %r" % (d.get("characters"), CHARS))
if any(e["speaker"] is not None and (e["speaker"] not in range(7) or e.get("speaker_name") != CHARS[e["speaker"]]) for e in d["lines"].values()):
    sys.exit("voice_curation.py: a speaker index and its name disagree")
lines = d["lines"]
nxt, part = {}, set()
for g in d.get("groups", []):   # (the tool's groups: split lines, and the Cutscenes page's lines joined as one; said as one line)
    g = [tuple(p) for p in g]
    if len(g) < 2 or len({p[0] for p in g}) != 1 or len(set(g)) != len(g) or any(p in nxt or p in part for p in g):
        print("voice_curation.py: group %s skipped (fewer than two parts, parts of both discs, or a part twice or in another group)" % g); continue
    for a, b in zip(g, g[1:]): nxt[a] = b[1]; part.add(b)
rows = []
for e in lines.values():
    k = (e["disc"], e["vid"])
    f = (e["confirmed"] and 1) | (e["exclude"] and 2) | (e["grunt"] and 4) | (k in part and 8)
    if not f and k not in nxt: continue
    chr_ = e["speaker"] if e["speaker"] is not None else -1   # (unconfirmed: the tool's prediction, used for a grunt only)
    rows.append((k[0], k[1], chr_, f, nxt.get(k, 0xFFFF)))
rows.sort()

# How long a line plays in a scene, measured (voice_played.json: headless runs of the scenes, voices off): until the next
# VoiceOn or StopVoice (that stops whatever plays: PlayVoiceEx2, StopVoice), at most its length; and that gap itself (a line
# in its place must end by then too, also within the grace), at most the length and GRACE_MS. Unknown where the run ended
# first, or where two runs differ by more than 100 ms (another case, the player). A group's first part with its later parts
# right after it: from its start to the last part's end so, its gap to the event after the last part. A row per VoiceOn
# (room file, its offset in the script block, rom->evtp) where either differs from what cast_glue.c takes without one (the
# file's length and the grace; a group's first part: its parts' lengths together).
GRACE_MS = 1000   # (cast.h VOICE_GRACE_MS)
seen, heads = {}, set()
try: runs = json.load(open(pfile, encoding="utf-8"))["runs"]
except (OSError, ValueError, KeyError): runs = []; print("voice_curation.py: no measured times at %s" % pfile)
for r in runs:
    ev = r["events"]
    on = [i for i, e in enumerate(ev) if len(e) > 1]
    j = 0
    while j < len(on):
        i = on[j]
        t, room, ofs, disc, vid, ms = ev[i][:6]
        k, g = (disc, vid), [(disc, vid)]
        while g[-1] in nxt: g.append((disc, nxt[g[-1]]))
        n = len(g) if len(g) > 1 and k not in part and [tuple(ev[x][3:5]) for x in on[j:j + len(g)]] == g else 1   # (its parts follow)
        last = on[j + n - 1]
        full = sum(ev[x][5] for x in on[j:j + n])
        tl, msl = ev[last][0], ev[last][5]
        if last + 1 < len(ev): gap = ev[last + 1][0] - t
        elif t + full + GRACE_MS <= r["end"]: gap = full + GRACE_MS   # (nothing after it: no limit)
        else: gap = None   # (the run ended first)
        v = None if gap is None else (tl - t) + min(msl, gap - (tl - t))
        if n == 1 and len(ev[i]) > 6: v = min(ev[i][6], ms)   # (the sound's own stop or end, where the run logged it)
        if gap is not None: gap = min(gap, full + GRACE_MS)
        if len(g) > 1 and k not in part: heads.add((room, ofs, vid))
        seen.setdefault((room, ofs, vid), []).append((v, gap, full, n))
        j += n
played, bad = [], 0
for (room, ofs, vid), vs in sorted(seen.items()):
    if None in [v for v, _, _, _ in vs] or len({n for *_, n in vs}) > 1 or max(v for v, *_ in vs) - min(v for v, *_ in vs) > 100 \
       or max(x[1] for x in vs) - min(x[1] for x in vs) > 100:
        bad += 1; continue   # (not measured to its end, or not alike in every run)
    v, gap, full = min(x[0] for x in vs), min(x[1] for x in vs), vs[0][2]
    if v < full or gap < full + GRACE_MS or (room, ofs, vid) in heads: played.append((room, ofs, vid, v, gap))
with open(out, "w") as o:
    o.write("/* Generated by pc/tools/voice_curation.py from the voice tool's voice_curation.json (saved %s, %d of %d lines\n"
            "   reviewed): { disc, file, character (-1 none/other), VOICE_* flags, the group's next part (0xFFFF none) }, by disc and file */\n"
            % (d.get("saved", "?"), d.get("reviewed", 0), d.get("total", 0)))
    o.write("#ifndef VOICE_PLAYED\n")
    for r in rows: o.write("{ %d, %d, %d, %d, 0x%04X },\n" % r)
    o.write("{ 0, 0, 0, 0, 0 }\n")
    o.write("#else\n/* ... and from voice_played.json (%d runs), where the next line cuts a line short or starts within the grace (or a\n"
            "   group spans its parts' time): { room file, the VoiceOn's offset in the script block, file, ms it plays there, ms until\n"
            "   the next line (at most the length and the grace) }, by room and offset (%d VoiceOns measured, %d of them not alike or\n"
            "   not to their end) */\n" % (len(runs), len(seen), bad))
    for r in played: o.write("{ %d, %d, %d, %d, %d },\n" % r)
    o.write("{ 0x7FFFFFFF, 0, 0, 0, 0 }\n#endif\n")
c = lambda m: sum(1 for r in rows if r[3] & m)
print("voice_curation.py: %d rows: %d speakers checked, %d do not use, %d grunts, %d group parts; %d played lengths (%d VoiceOns measured, %d unknown) -> %s"
      % (len(rows), c(1), c(2), c(4), c(8) + len([k for k in nxt if k not in part]), len(played), len(seen), bad, out))
