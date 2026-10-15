"""Decompress only the header chunks of UE3 X360 .xxx packages and report exports/imports matching a regex.
python -I tools/ue3/scan_hdr.py <regex> <pkg.xxx>...
"""
import os, re, struct, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from decompress_chunked import read_summary, TOOL
from upk import Package

def header_bytes(src):
    b = src.read_bytes()
    flags_at, flags, count_at, chunks, end = read_summary(b)
    hdr_size = struct.unpack_from(">I", b, 8)[0]
    if not chunks:
        return b
    out = bytearray(max(uo + us for uo, us, _, _ in chunks))
    head = bytearray(b[:chunks[0][0]])
    struct.pack_into(">I", head, flags_at, 0)
    struct.pack_into(">I", head, count_at, 0)
    out[:len(head)] = head
    with tempfile.TemporaryDirectory() as td:
        for i, (uo, us, co, cs) in enumerate(chunks):
            if uo >= hdr_size:
                break
            cin = Path(td) / f"c{i}.bin"; cout = Path(td) / f"c{i}.out"
            cin.write_bytes(b[co:co + cs])
            r = subprocess.run([str(TOOL), str(cin), str(cout)], capture_output=True, text=True)
            if r.returncode != 0:
                raise SystemExit(f"{src}: chunk {i} failed")
            out[uo:uo + us] = cout.read_bytes()
    return bytes(out)

class MemPackage(Package):
    def __init__(self, data):
        import os
        tf = tempfile.NamedTemporaryFile(delete=False, suffix=".upk")
        tf.write(data); tf.close()
        super().__init__(tf.name)
        os.unlink(tf.name)

rx = re.compile(sys.argv[1])
for f in sys.argv[2:]:
    p = Path(f)
    try:
        pk = MemPackage(header_bytes(p))
    except SystemExit as ex:
        print("ERR", ex); continue
    except Exception as ex:
        print("ERR", p.name, ex); continue
    hits = []
    for e in pk.exports:
        path = pk.path(e["index"] + 1)
        if rx.search(path):
            hits.append(f"EXP {pk.class_name(e)} {path} size={e['size']}")
    for i, imp in enumerate(pk.imports):
        if rx.search(imp["name"]):
            hits.append(f"IMP {imp['class']} {pk.path(-(i + 1))}")
    if hits:
        print("==", p.name)
        for h in hits[:40]:
            print("  ", h)
