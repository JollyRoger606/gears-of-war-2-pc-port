"""Disassembles every function in a package and prints the lines that mention any of the given
substrings, prefixed with the function path. Also reports how many functions failed to decode.

python -I tools/ue3/scan_scripts.py <pkg.upk> <substring> [...]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upk import Package  # noqa: E402
from ubc import Lister  # noqa: E402

pk = Package(sys.argv[1])
needles = sys.argv[2:]
lister = Lister(pk)
total = bad = 0
for e in pk.exports:
    if pk.class_name(e) != "Function" or e["size"] < 48:
        continue
    b = pk.blob(e)
    n = struct.unpack_from(">i", b, 40)[0]
    if n <= 0 or 44 + n > len(b):
        continue
    total += 1
    lines = lister.disasm(b[44:44 + n])
    if any("!!" in l for l in lines):
        bad += 1
    path = pk.path(e["index"] + 1)
    for l in lines:
        if any(s in l for s in needles):
            print(f"{path}: {l}")
print(f"# decoded {total - bad}/{total} functions cleanly", file=sys.stderr)
