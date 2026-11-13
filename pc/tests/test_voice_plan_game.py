# ctest voice_plan_game: recvx --voice-plan (cast_glue.c voice_plan_export) against what the game itself does. A few scenes run
# headless in the real game (RECVX_TEST_SCENE, SDL offscreen, a temp save folder) with the cast and the voice lines on; each
# line the game logs ("voices: room ...") must be the plan's for that room and file: the same kind (kept, replaced, quiet,
# a group's later part, an enemy's sounds), voiced as the same character (the model the speaker is drawn as) and the same
# replacement. The plan with RECVX_VOICE_PLAN_FRESH=1: each script as if nothing was said before (a scene run's history;
# a playthrough's differs, docs/ACTORS.md "Voice plan"). Skipped (77) without RECVX_ISO. About two minutes.
import json, os, re, subprocess, sys, tempfile
recvx = sys.argv[1]
iso = os.environ.get("RECVX_ISO")
if not iso or not os.path.exists(iso): print("voice_plan_game: no RECVX_ISO, skipped"); sys.exit(77)
SEED = 70671
INI = ("enemy_seed = %d\ncast_random = 1\ncast_models = 2\ncast_assign = 0\ncast_custom_set = 1\ncast_enemy_models = 2\nvoice_random = 1\n"
       "voice_mouths = 1\nappear_per_char = 1\nappear_claire = 4095\nappear_chris = 1\n" % SEED
       + "".join("cast_custom_off = %s\n" % m for m in ("Alexia (First Form)", "Nosferatu", "Monster Steve", "Hunter", "Hunter (Sweeper)",
                                                       "Bandersnatch", "Anatomist Zombie", "Zombies", "Claire (Alternate Costume)")))
SCENES = [("0030", 0, 0, 3000), ("1020", 1, 1, 3000), ("7070", 0, 5, 2400)]   # (room, level, event, frames: Steve, Alfred, Chris's part)
NEWGAME = "100:down,103:-,110:cross,113:-,160:cross,163:-,200:cross,203:-,240:cross,243:-,280:cross,283:-"

R_KEEP = re.compile(r"voices: room (\d+) line (\d+)/(\d+) kind \d+ rec -?\d+, speaker -?\d+ \([^)]*\), shown as (-?\d+): stays")
R_LINE = re.compile(r"voices: room (\d+) record \d+: \w+'s line (\d+)/(\d+) \(.*?\), shown as .*? -> (\w+)'s line (\d+)/(\d+) \(\d+ ms(, a grunt)?")
R_QUIET = re.compile(r"voices: room (\d+) record \d+: \w+'s line (\d+)/(\d+) \(\d+ ms\) shown as (\w+): quiet")
R_GRP = re.compile(r"voices: room (\d+) record \d+: line (\d+)/(\d+) \(\d+ ms\), a later part of a group")
R_ENEMY = re.compile(r"voices: room (\d+) record \d+: \w+'s line (\d+)/(\d+) \(\d+ ms\)(?:, shown as .*? -> \d+ of its sounds| is a short sound)")

def game_lines(log):
    out = []
    for l in open(log, errors="replace"):
        if m := R_KEEP.search(l): out.append((int(m[1]), int(m[2]), int(m[3]), "original", None, None, int(m[4]) >= 0))
        elif m := R_LINE.search(l): out.append((int(m[1]), int(m[2]), int(m[3]), "grunt" if m[7] else "line", m[4], (int(m[5]), int(m[6])), True))
        elif m := R_QUIET.search(l): out.append((int(m[1]), int(m[2]), int(m[3]), "quiet", m[4], None, True))
        elif m := R_GRP.search(l): out.append((int(m[1]), int(m[2]), int(m[3]), "group_silent", None, None, True))
        elif m := R_ENEMY.search(l): out.append((int(m[1]), int(m[2]), int(m[3]), "enemy", None, None, True))
    return out

with tempfile.TemporaryDirectory() as tmp:
    def save(name):
        d = os.path.join(tmp, name)
        os.makedirs(os.path.join(d, "save")); os.makedirs(os.path.join(d, "mods"))
        open(os.path.join(d, "save", "recvx.ini"), "w").write(INI)
        return d
    base = dict(os.environ, SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", RECVX_UPDATE_CHECK="0")
    base.pop("RECVX_ISO", None)
    d = save("plan")
    plan_path = os.path.join(d, "plan.json")
    r = subprocess.run([recvx, iso, "--save-dir", os.path.join(d, "save"), "--voice-plan", str(SEED), "--out", plan_path],
                       env=dict(base, RECVX_MODS=os.path.join(d, "mods"), RECVX_VOICE_PLAN_FRESH="1"), capture_output=True, timeout=300)
    assert r.returncode == 0, r.stdout[-2000:]
    plan = json.load(open(plan_path))
    assert plan["version"] == 2 and plan["seed"] == SEED
    runs = []
    for room, lv, ev, frames in SCENES:
        d = save("rm%s" % room)
        env = dict(base, RECVX_MODS=os.path.join(d, "mods"), RECVX_SKIP_MOVIES="1", RECVX_INPUT=NEWGAME, RECVX_EXIT_FRAME=str(frames),
                   RECVX_INF_HEALTH="1", RECVX_CAST_TRACE="1", RECVX_TEST_SCENE="400:RM_%s:%d:%d" % (room, lv, ev))
        runs.append((room, d, subprocess.Popen([recvx, iso, "--skip-intro", "--size", "320x240", "--dev", "--save-dir", os.path.join(d, "save"),
                                                "--log=" + os.path.join(d, "log.txt")], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)))
    by = {}
    for s in plan["scenes"]:
        for l in s["lines"]: by.setdefault((int(s["room"][3:]), l["disc"], l["vid"]), []).append(l)
    bad, n = [], 0
    for room, d, p in runs:
        p.wait(timeout=900)
        got = [g for g in game_lines(os.path.join(d, "log.txt")) if g[0] == int(room)]
        assert len(got) >= 3, (room, len(got))   # (the scene ran and said something)
        used = set()
        for rm, disc, vid, kind, voiced, repl, drawn in got:
            c = [l for l in by.get((rm, disc, vid), []) if id(l) not in used]
            if not c: bad.append("RM_%04d %d/%d: not in the plan" % (rm, disc, vid)); continue
            l = c[0]; used.add(id(l)); n += 1
            r = l["repl"]
            want = (r["kind"], r["speaker_of_line"] if r["kind"] in ("line", "grunt") else l["drawn_as"] if r["kind"] == "quiet" else None,
                    (r["disc"], r["vid"]) if r["kind"] in ("line", "grunt") else None)
            if want != (kind, voiced, repl) or (kind == "original" and drawn != (l["drawn_as"] is not None)):
                bad.append("RM_%04d %d/%d: plan %s, game %s" % (rm, disc, vid, want, (kind, voiced, repl)))
    print("voice_plan_game: %d lines in %d scenes compared, %d differ" % (n, len(runs), len(bad)))
    for b in bad: print("  " + b)
    assert not bad
print("voice_plan_game: ok")
