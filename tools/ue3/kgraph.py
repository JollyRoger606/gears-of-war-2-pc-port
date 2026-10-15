"""Follow Kismet output links from every object of a class in one map.

python -I tools/ue3/kgraph.py <map> <class-regex> [depth]
Prints each start object, then a tree of linked ops (via OutputLinks) with their variable links summarised.
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue3paths  # noqa: E402

KDIR = str(ue3paths.kismet_dir())


def load(m):
    objs = [json.loads(l) for l in open(os.path.join(KDIR, m + ".jsonl"), encoding="utf-8")]
    return objs, {o["cls"] + ":" + o["path"]: o for o in objs}


def var_summary(o, byp):
    out = []
    for vl in o["props"].get("VariableLinks") or []:
        if not isinstance(vl, dict):
            continue
        lv = vl.get("LinkedVariables") or []
        if not lv:
            continue
        names = []
        for ref in lv:
            v = byp.get(ref)
            short = ref.split(":", 1)[0]
            if v is not None:
                ov = v["props"].get("ObjValue") or v["props"].get("FindVarName") or v["props"].get("IntValue") \
                    or v["props"].get("bValue") or v["props"].get("FloatValue")
                if ov is not None:
                    short += "=" + (str(ov).split(".")[-1] if isinstance(ov, str) else str(ov))
            names.append(short)
        out.append(f"{vl.get('LinkDesc')}:[{', '.join(names)}]")
    return " ".join(out)


def extra(o):
    keep = {k: v for k, v in o["props"].items() if k not in ("InputLinks", "OutputLinks", "VariableLinks",
            "EventLinks", "ObjInstanceVersion", "ParentSequence", "DrawWidth", "DrawHeight", "ObjPosX", "ObjPosY")}
    s = json.dumps(keep, default=str)
    return s if len(s) < 200 else s[:200] + "..."


def walk(o, byp, depth, seen, indent):
    if depth < 0:
        return
    for ol in o["props"].get("OutputLinks") or []:
        if not isinstance(ol, dict):
            continue
        for link in ol.get("Links") or []:
            ref = link.get("LinkedOp") if isinstance(link, dict) else None
            if not ref:
                continue
            t = byp.get(ref)
            key = ref
            tag = "" if key not in seen else " (seen)"
            print(f"{indent}-{ol.get('LinkDesc')}-> {ref.split(':', 1)[0]} {ref.split('.')[-1]}{tag}  "
                  f"{var_summary(t, byp) if t else ''}  {extra(t) if t else ''}")
            if t is not None and key not in seen:
                seen.add(key)
                walk(t, byp, depth - 1, seen, indent + "    ")


def main():
    m, rx = sys.argv[1], re.compile(sys.argv[2])
    depth = int(sys.argv[3]) if len(sys.argv) > 3 else 2
    objs, byp = load(m)
    for o in objs:
        if rx.search(o["cls"]):
            print(f"== {o['cls']} {o['path']}  {var_summary(o, byp)}  {extra(o)}")
            walk(o, byp, depth, set(), "  ")


if __name__ == "__main__":
    main()
