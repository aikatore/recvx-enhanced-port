# ctest voice_plan: recvx --voice-plan SEED --out FILE (cast_glue.c voice_plan_export; docs/ACTORS.md "Voice plan"). Without a
# disc image it refuses cleanly (exit 2, nothing written); with RECVX_ISO: the JSON's shape (the voice tool reads exactly
# this), every line once, the same file twice, the save folder untouched. Skipped (77) without RECVX_ISO.
import json, os, subprocess, sys, tempfile
recvx = sys.argv[1]
KINDS = {"line", "grunt", "quiet", "enemy", "original", "group_silent"}

def run(tmp, iso, seed, out, ini=""):
    save = os.path.join(tmp, "save")
    os.makedirs(save, exist_ok=True)
    open(os.path.join(save, "recvx.ini"), "w").write(ini)
    env = dict(os.environ, SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy", RECVX_UPDATE_CHECK="0", RECVX_MODS=os.path.join(tmp, "mods"))
    env.pop("RECVX_ISO", None)
    r = subprocess.run([recvx, iso, "--save-dir", save, "--voice-plan", str(seed), "--out", out], env=env, capture_output=True, timeout=300)
    assert sorted(os.listdir(save)) == ["recvx.ini"] and open(os.path.join(save, "recvx.ini")).read() == ini, os.listdir(save)   # (nothing written there)
    return r.returncode

with tempfile.TemporaryDirectory() as tmp:
    out = os.path.join(tmp, "plan.json")
    assert run(tmp, os.path.join(tmp, "none.iso"), 1, out) == 2 and not os.path.exists(out)
    iso = os.environ.get("RECVX_ISO")
    if not iso or not os.path.exists(iso): print("voice_plan: no RECVX_ISO, skipped"); sys.exit(77)
    assert run(tmp, iso, 7, out, "cast_random = 1\ncast_assign = 1\n") == 0
    d = json.load(open(out))
    assert d["version"] == 2 and d["seed"] == 7 and d["options"]["voices"] == 1 and d["options"]["cast"] == 1, d["options"]
    n = 0
    for s in d["scenes"]:
        assert s["room"].startswith("RM_") and isinstance(s["script"], str) and s["disc"] in (1, 2) and s["lines"]
        for i, l in enumerate(s["lines"]):
            n += 1
            assert l["order"] == i and l["disc"] == s["disc"] and l["orig_ms"] > 0 and l["played_ms"] >= 0, l
            assert isinstance(l["played_known"], bool) and (l["group"] is None or isinstance(l["group"], int)) and l["group_part"] >= 0
            assert l["drawn_as"] is None or isinstance(l["drawn_as"], str), l   # (version 2: the model the speaker is drawn as)
            r = l["repl"]
            assert r["kind"] in KINDS and isinstance(r["fade"], bool) and isinstance(r["why"], str), r
            if r["kind"] in ("line", "grunt", "original"): assert r["disc"] in (1, 2) and r["vid"] >= 0 and r["ms"] > 0, r
            else: assert r["disc"] is None and r["vid"] is None and r["ms"] is None, r
            assert (r["cut_ms"] is None) == (not r["fade"]), r
            if r["kind"] == "original": assert (r["disc"], r["vid"]) == (l["disc"], l["vid"])
            if r["kind"] == "group_silent": assert l["group"] is not None and l["group_part"] > 0
    assert n > 600, n
    kinds = {l["repl"]["kind"] for s in d["scenes"] for l in s["lines"]}
    assert {"line", "original"} <= kinds, kinds   # (the cast on: replaced lines and kept ones)
    first = open(out).read()
    assert run(tmp, iso, 7, out, "cast_random = 1\ncast_assign = 1\n") == 0 and open(out).read() == first   # (the same again)
print("voice_plan: ok")
