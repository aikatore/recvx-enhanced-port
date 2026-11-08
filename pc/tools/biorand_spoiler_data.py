# pc/src/host/spoiler_data.inc (the BioRand spoiler log, pc/src/host/spoiler.c) from BioRand's data/recv/rdt.json and RECV/ReCvItemIds.cs (biorand/classic 7955765, MIT)
# usage: biorand_spoiler_data.py rdt.json ReCvItemIds.cs pc/src/host/spoiler_data.inc
import json, re, sys
rdt, ids, out = sys.argv[1:4]
t = re.sub(r"//[^\n]*", "", open(rdt, encoding="utf-8-sig").read())
t = re.sub(r",(\s*[}\]])", r"\1", t)
d = json.loads(t)
names = {}
for nm, v in re.findall(r"public const byte (\w+) = 0x([0-9A-Fa-f]+);", open(ids, encoding="utf-8-sig").read()):
    if int(v, 16) < 0xFF: names.setdefault(int(v, 16), nm)   # (0xFF: Unknown)
top = max(names)
rows = {}
for r, v in d["rooms"].items():
    code = int(r, 16)
    for it in v.get("items", []):
        rows.setdefault((code, it["id"]), (it["type"], it["amount"]))
with open(out, "w") as f:
    f.write("/* Generated from BioRand's data/recv/rdt.json and RECV/ReCvItemIds.cs (https://github.com/biorand/classic, commit\n"
            "   7955765, MIT, Copyright (c) 2022-2026 Ted John) by pc/tools/biorand_spoiler_data.py: the item names its log uses, by item id, and\n"
            "   the original item of each item spot BioRand knows (room as BioRand writes it, 0xSRRV; the spot's id). */\n")
    f.write("static const char* const br_names[%d] = {\n" % (top + 1))
    for i in range(top + 1):
        f.write('    "%s",\n' % names.get(i, ""))
    f.write("};\n")
    f.write("static const struct { unsigned short room; unsigned char id, type; unsigned short amount; } br_orig[%d] = {\n" % len(rows))
    line = "   "
    for (code, i), (ty, am) in sorted(rows.items()):
        e = " { 0x%04X, %d, %d, %d }," % (code, i, ty, am)
        if len(line) + len(e) > 120:
            f.write(line + "\n"); line = "   "
        line += e
    f.write(line + "\n};\n")
print(top + 1, len(rows))
