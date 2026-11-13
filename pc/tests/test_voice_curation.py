# ctest voice_curation: pc/tools/voice_curation.py on a made-up voice_curation.json (no game data) -> the table's rows. A group (the
# voice tool's split lines, or the lines its Cutscenes page joined as one): every part in order, the first with the next part and
# the flags of its own, the later ones VOICE_PART (8); a part in two groups, a part twice, both discs: the group skipped.
# And on made-up RECVX_VOICE_TIME logs (voice_curation.py --measure -> voice_played.json): how long each line plays in its
# scene (VOICE_PLAYED rows).
import json, os, re, subprocess, sys, tempfile
tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "voice_curation.py")
CH = ["Claire", "Chris", "Steve", "Wesker", "Alfred", "Alexia", "Rodrigo"]

def line(disc, vid, speaker=None, confirmed=False, exclude=False, grunt=False):
    return {"disc": disc, "vid": vid, "speaker": speaker, "speaker_name": CH[speaker] if speaker is not None else None,
            "confirmed": confirmed, "exclude": exclude, "grunt": grunt, "notes": ""}

def run(groups, lines, logs=None):
    with tempfile.TemporaryDirectory() as tmp:
        src, out = os.path.join(tmp, "c.json"), os.path.join(tmp, "c.inc")
        json.dump({"version": 1, "characters": CH, "groups": groups, "lines": {"%d:%d" % (l["disc"], l["vid"]): l for l in lines}}, open(src, "w"))
        if logs:
            paths = []
            for i, text in enumerate(logs):
                os.mkdir(os.path.join(tmp, "r%d" % i)); paths.append(os.path.join(tmp, "r%d" % i, "log.txt"))
                open(paths[-1], "w").write(text)
            subprocess.run([sys.executable, tool, "--measure", os.path.join(tmp, "voice_played.json")] + paths, check=True, stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, tool, src, out], check=True, stdout=subprocess.DEVNULL)
        cur, pl = open(out).read().split("#else")
        rows = [tuple(int(x, 0) for x in m) for m in re.findall(r"\{ (\d+), (\d+), (-?\d+), (\d+), (0x[0-9A-F]+|0) \}", cur)]
        played = [tuple(int(x, 0) for x in m) for m in re.findall(r"\{ (\d+|0x7FFFFFFF), (-?\d+), (\d+), (\d+), (\d+) \}", pl)]
    assert rows[-1] == (0, 0, 0, 0, 0), rows
    assert played[-1] == (0x7FFFFFFF, 0, 0, 0, 0), played
    return (rows[:-1], played[:-1]) if logs else rows[:-1]

lines = [line(1, 10, 2, True), line(1, 11, 2, True), line(1, 12, 2, True),   # Steve's line said in three files: joined
         line(1, 20), line(1, 21),                                          # a group of unchecked lines: still in the table
         line(1, 30, 0, True), line(2, 31, 0, True),                        # both discs: skipped
         line(1, 40, 3, True, grunt=True), line(1, 50, 4, True, exclude=True), line(1, 60)]
groups = [[[1, 10], [1, 11], [1, 12]], [[1, 20], [1, 21]], [[1, 30], [2, 31]], [[1, 11], [1, 40]], [[1, 50], [1, 50]]]
rows = run(groups, lines)
assert rows == [(1, 10, 2, 1, 11), (1, 11, 2, 9, 12), (1, 12, 2, 9, 0xFFFF), (1, 20, -1, 0, 21), (1, 21, -1, 8, 0xFFFF),
                (1, 30, 0, 1, 0xFFFF), (1, 40, 3, 5, 0xFFFF), (1, 50, 4, 3, 0xFFFF), (2, 31, 0, 1, 0xFFFF)], rows
assert run([], lines) == [(1, 10, 2, 1, 0xFFFF), (1, 11, 2, 1, 0xFFFF), (1, 12, 2, 1, 0xFFFF), (1, 30, 0, 1, 0xFFFF), (1, 40, 3, 5, 0xFFFF),
                          (1, 50, 4, 3, 0xFFFF), (2, 31, 0, 1, 0xFFFF)]   # (no groups: as before, the unchecked lines left out)

# the logs (game ms; [real s] keeps up with them, else the run is left out): a line plays until the next line or StopVoice
def log(events, end_frame, slow=False):
    t = []
    for e in events:
        real = e[0] / 1000 * (3 if slow else 1)
        t.append("[%8.3f] voicetime: %d %s\n" % (real, e[0], "stop" if len(e) == 1 else "on room %04d ofs %d line %d/%d (%d ms) played -1" % e[1:]))
    return "".join(t) + "[%8.3f] test: exit at frame %d\n" % (end_frame / 30, end_frame)
runs = [log([(1000, 1020, 500, 1, 30, 2000), (2000, 1020, 520, 1, 40, 1500), (4000,), (4000, 1020, 540, 1, 60, 4000), (4000,),
             (4000, 1020, 560, 1, 50, 1000)], 300),   # 30 cut after 1 s; 40 whole, the next 0.5 s after it; 60 stopped at once; 50 the last, whole
        log([(500, 1020, 500, 1, 30, 2000), (1533, 1020, 600, 1, 30, 2000), (2333, 1020, 520, 1, 40, 1500), (4333,)], 300),   # 30 again, 33 ms later: alike
        log([(500, 1020, 600, 1, 30, 2000), (1700,)], 300),   # ofs 600: 0.8 s there, 1.2 s here: not alike, unknown
        log([(0, 30, 3800, 1, 10, 900), (700, 30, 3820, 1, 11, 600), (1500, 30, 3840, 1, 12, 3000), (3500, 30, 3860, 1, 60, 4000)], 180),   # a group:
            # its span 3.5 s (12 cut by 60); 60 not to its end before the run's: unknown
        log([(0, 40, 100, 1, 10, 900), (500, 40, 120, 1, 60, 4000), (6000, 40, 300, 1, 11, 600), (6300, 40, 320, 1, 50, 1000)], 300),   # the group's first
            # part on its own: its own 0.5 s (against the parts' 4.5 s); a later part on its own: cut, against its own length
        log([(0, 50, 700, 1, 30, 2000), (500, 50, 720, 1, 40, 1500)], 300, slow=True),   # a run that fell behind the clock: left out
        # the voice slot's player (RECVX_CAST_TRACE) where logged: how long the sound really played. 1/70: StopVoice at 1.5 s fades
        # it out until 1.9 s; 1/71: the sound ends by itself
        "[   0.000] voicetime: 0 on room 0060 ofs 100 line 1/70 (2000 ms) played -1\n[   0.000] adxt: start 0/70 (48000 Hz, 1 ch, 96000 samples, loop 0) voice 1 talk 1\n"
        "[   1.500] voicetime: 1500 stop\n[   1.900] adxt: stop talk 1 at 1900 of 2000 ms\n"
        "[   2.000] voicetime: 2000 on room 0060 ofs 120 line 1/71 (800 ms) played -1\n[   2.000] adxt: start 0/71 (48000 Hz, 1 ch, 38400 samples, loop 0) voice 1 talk 1\n"
        "[   2.800] adxt: end talk 1 (800 ms)\n[   2.900] voicetime: 2900 stop\n[  10.000] test: exit at frame 300\n"]
rows2, played = run(groups, lines, runs)
assert rows2 == rows, rows2   # (the logs change nothing of the lines')
assert played == [(30, 3800, 10, 3500, 3500), (40, 100, 10, 500, 500), (40, 300, 11, 300, 300), (60, 100, 70, 1900, 1500), (60, 120, 71, 800, 900),
                  (1020, 500, 30, 1000, 1000),
                  (1020, 520, 40, 1500, 2000), (1020, 540, 60, 0, 0)], played   # (by
    # room file and offset)
print("voice_curation: ok")
