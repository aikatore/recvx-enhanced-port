# ctest voice_curation: pc/tools/voice_curation.py on a made-up voice_curation.json (no game data) -> the table's rows. A group (the
# voice tool's split lines, or the lines its Cutscenes page joined as one): every part in order, the first with the next part and
# the flags of its own, the later ones VOICE_PART (8); a part in two groups, a part twice, both discs: the group skipped.
import json, os, re, subprocess, sys, tempfile
tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "voice_curation.py")
CH = ["Claire", "Chris", "Steve", "Wesker", "Alfred", "Alexia", "Rodrigo"]

def line(disc, vid, speaker=None, confirmed=False, exclude=False, grunt=False):
    return {"disc": disc, "vid": vid, "speaker": speaker, "speaker_name": CH[speaker] if speaker is not None else None,
            "confirmed": confirmed, "exclude": exclude, "grunt": grunt, "notes": ""}

def run(groups, lines):
    with tempfile.TemporaryDirectory() as tmp:
        src, out = os.path.join(tmp, "c.json"), os.path.join(tmp, "c.inc")
        json.dump({"version": 1, "characters": CH, "groups": groups, "lines": {"%d:%d" % (l["disc"], l["vid"]): l for l in lines}}, open(src, "w"))
        subprocess.run([sys.executable, tool, src, out], check=True, stdout=subprocess.DEVNULL)
        rows = [tuple(int(x, 0) for x in m) for m in re.findall(r"\{ (\d+), (\d+), (-?\d+), (\d+), (0x[0-9A-F]+|0) \}", open(out).read())]
    assert rows[-1] == (0, 0, 0, 0, 0), rows
    return rows[:-1]

lines = [line(1, 10, 2, True), line(1, 11, 2, True), line(1, 12, 2, True),   # Steve's line said in three files: joined
         line(1, 20), line(1, 21),                                          # a group of unchecked lines: still in the table
         line(1, 30, 0, True), line(2, 31, 0, True),                        # both discs: skipped
         line(1, 40, 3, True, grunt=True), line(1, 50, 4, True, exclude=True), line(1, 60)]
rows = run([[[1, 10], [1, 11], [1, 12]], [[1, 20], [1, 21]], [[1, 30], [2, 31]], [[1, 11], [1, 40]], [[1, 50], [1, 50]]], lines)
assert rows == [(1, 10, 2, 1, 11), (1, 11, 2, 9, 12), (1, 12, 2, 9, 0xFFFF), (1, 20, -1, 0, 21), (1, 21, -1, 8, 0xFFFF),
                (1, 30, 0, 1, 0xFFFF), (1, 40, 3, 5, 0xFFFF), (1, 50, 4, 3, 0xFFFF), (2, 31, 0, 1, 0xFFFF)], rows
assert run([], lines) == [(1, 10, 2, 1, 0xFFFF), (1, 11, 2, 1, 0xFFFF), (1, 12, 2, 1, 0xFFFF), (1, 30, 0, 1, 0xFFFF), (1, 40, 3, 5, 0xFFFF),
                          (1, 50, 4, 3, 0xFFFF), (2, 31, 0, 1, 0xFFFF)]   # (no groups: as before, the unchecked lines left out)
print("voice_curation: ok")
