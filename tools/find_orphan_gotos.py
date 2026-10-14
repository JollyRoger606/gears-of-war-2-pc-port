"""List gotos whose loc_ label is not defined inside the same generated function body.

Codegen sometimes lets a function run past its real end (a catch funclet that ends in a
rethrow falls through into the next funclet) and emits gotos to labels owned by the next
function. For each one, print the manifest line that ends the enclosing function where the
label's owner starts.

Usage: python tools/find_orphan_gotos.py [generated/default]
"""
import re
import sys
from pathlib import Path

FUNC = re.compile(r"^DEFINE_REX_FUNC\((sub_([0-9A-F]{8})|\w+)\)\s*\{")
LABEL = re.compile(r"^loc_([0-9A-F]{8}):")
GOTO = re.compile(r"goto loc_([0-9A-F]{8});")


def scan_file(path: Path):
    bodies = []
    current = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = FUNC.match(line)
        if m:
            current = {"addr": m.group(2), "labels": set(), "gotos": set()}
            bodies.append(current)
            continue
        if current is None:
            continue
        m = LABEL.match(line)
        if m:
            current["labels"].add(m.group(1))
        for g in GOTO.findall(line):
            current["gotos"].add(g)
    return bodies


def main(gen_dir: str) -> int:
    bodies = []
    for f in sorted(Path(gen_dir).glob("*_recomp.*.cpp")):
        bodies.extend(scan_file(f))
    label_owner = {}
    for b in bodies:
        for label in b["labels"]:
            label_owner.setdefault(label, b["addr"])
    fixes = {}
    for b in bodies:
        for target in sorted(b["gotos"] - b["labels"]):
            owner = label_owner.get(target)
            fixes.setdefault(b["addr"], set()).add(owner)
    for enc, owners in sorted(fixes.items()):
        known = sorted(o for o in owners if o)
        if len(known) == 1 and enc:
            print(f"0x{enc} = {{ end = 0x{known[0]} }}")
        else:
            print(f"# 0x{enc}: orphan gotos, label owners {sorted(owners, key=str)}")
    return 1 if fixes else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "generated/default"))
