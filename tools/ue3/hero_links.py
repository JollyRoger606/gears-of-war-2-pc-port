"""Per op class: how often a variable link targets the heroes (SeqVar_Marcus/SeqVar_Dom) without SeqVar_Player,
with SeqVar_Player, or both. Event output vars (Instigator) included.

python -I tools/ue3/hero_links.py [min-count]
"""
import collections
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue3paths  # noqa: E402

KDIR = str(ue3paths.kismet_dir())
HERO = {"SeqVar_Marcus", "SeqVar_Dom"}
minc = int(sys.argv[1]) if len(sys.argv) > 1 else 1
stats = collections.defaultdict(lambda: collections.Counter())
maps = collections.defaultdict(set)
for path in sorted(glob.glob(os.path.join(KDIR, "*.jsonl"))):
    m = os.path.basename(path)[:-6]
    for line in open(path, encoding="utf-8"):
        o = json.loads(line)
        vls = o["props"].get("VariableLinks")
        if not isinstance(vls, list):
            continue
        for vl in vls:
            if not isinstance(vl, dict):
                continue
            lv = vl.get("LinkedVariables") or []
            if not isinstance(lv, list) or not lv:
                continue
            cls = {x.split(":", 1)[0] for x in lv if isinstance(x, str)}
            hero = bool(cls & HERO)
            player = "SeqVar_Player" in cls
            if not (hero or player):
                continue
            key = (o["cls"], vl.get("LinkDesc"))
            if hero and not player:
                kind = "heroOnly"
                if cls & HERO == {"SeqVar_Marcus"}:
                    kind = "marcusOnly"
                elif cls & HERO == {"SeqVar_Dom"}:
                    kind = "domOnly"
            elif player and not hero:
                kind = "playerOnly"
            else:
                kind = "player+hero"
            stats[key][kind] += 1
            maps[key].add(m)
rows = sorted(stats.items(), key=lambda kv: -sum(kv[1].values()))
print(f"{'op class':34s} {'link':16s} marcusOnly domOnly heroOnly(M+D) playerOnly player+hero  maps")
for (c, d), ctr in rows:
    if sum(ctr.values()) < minc:
        continue
    print(f"{c:34s} {str(d)[:16]:16s} {ctr['marcusOnly']:10d} {ctr['domOnly']:7d} {ctr['heroOnly']:13d} "
          f"{ctr['playerOnly']:10d} {ctr['player+hero']:11d}  {len(maps[(c, d)])}")
