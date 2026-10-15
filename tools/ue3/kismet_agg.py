"""Aggregate Kismet dumps (*.jsonl) from UE3_KISMET_DIR (default out/ue3/kismet).

python -I tools/ue3/kismet_agg.py links [op-class-regex]   -> (op class, link desc, linked var classes) counts + sample maps
python -I tools/ue3/kismet_agg.py classes                  -> object class counts over all maps
python -I tools/ue3/kismet_agg.py props <class-regex> [prop-regex]  -> distinct non-link property values for a class
python -I tools/ue3/kismet_agg.py where <class-regex>      -> maps containing the class, with counts
"""
import collections
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue3paths  # noqa: E402

KDIR = str(ue3paths.kismet_dir())
LINK_KEYS = {"InputLinks", "OutputLinks", "VariableLinks", "EventLinks", "ObjInstanceVersion", "ParentSequence",
             "DrawWidth", "DrawHeight", "ObjPosX", "ObjPosY", "ObjComment", "bDeleteMe"}


def load():
    for path in sorted(glob.glob(os.path.join(KDIR, "*.jsonl"))):
        m = os.path.basename(path)[:-6]
        with open(path, encoding="utf-8") as fh:
            for line in fh:
                yield m, json.loads(line)


def var_class(ref):
    return ref.split(":", 1)[0] if isinstance(ref, str) else str(ref)


def cmd_links(rx):
    cnt = collections.Counter()
    samples = collections.defaultdict(set)
    for m, o in load():
        if rx and not re.search(rx, o["cls"]):
            continue
        for vl in o["props"].get("VariableLinks", []) if isinstance(o["props"].get("VariableLinks"), list) else []:
            if not isinstance(vl, dict):
                continue
            lv = vl.get("LinkedVariables") or []
            if not isinstance(lv, list) or not lv:
                continue
            key = (o["cls"], vl.get("LinkDesc"), tuple(sorted(collections.Counter(var_class(x) for x in lv).items())))
            cnt[key] += 1
            if len(samples[key]) < 4:
                samples[key].add(m)
    for (cls, desc, vc), n in sorted(cnt.items(), key=lambda kv: (kv[0][0], -kv[1])):
        print(f"{n:5d}  {cls:32s} {str(desc):18s} {dict(vc)}  e.g. {sorted(samples[(cls, desc, vc)])}")


def cmd_classes():
    cnt = collections.Counter(o["cls"] for _, o in load())
    for c, n in cnt.most_common():
        print(f"{n:6d} {c}")


def cmd_props(rx, prx):
    vals = collections.Counter()
    for m, o in load():
        if not re.search(rx, o["cls"]):
            continue
        for k, v in o["props"].items():
            if k in LINK_KEYS or (prx and not re.search(prx, k)):
                continue
            vals[(o["cls"], k, json.dumps(v, default=str)[:160])] += 1
    for (c, k, v), n in sorted(vals.items()):
        print(f"{n:5d} {c} {k} = {v}")


def cmd_where(rx):
    per = collections.Counter()
    for m, o in load():
        if re.search(rx, o["cls"]):
            per[(m, o["cls"])] += 1
    for (m, c), n in sorted(per.items()):
        print(f"{n:4d} {c:32s} {m}")


def main():
    cmd = sys.argv[1]
    if cmd == "links":
        cmd_links(sys.argv[2] if len(sys.argv) > 2 else None)
    elif cmd == "classes":
        cmd_classes()
    elif cmd == "props":
        cmd_props(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    elif cmd == "where":
        cmd_where(sys.argv[2])


if __name__ == "__main__":
    main()
