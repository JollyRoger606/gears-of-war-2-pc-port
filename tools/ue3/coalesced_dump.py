"""Dumps sections of a GoW2 Coalesced_*.bin bundle (read-only).
python -I tools/ue3/coalesced_dump.py <Coalesced.bin> <section-substring> [key-substring]"""
import struct, sys
d = open(sys.argv[1], "rb").read()
p = 0
def i32():
    global p
    v = struct.unpack_from(">i", d, p)[0]; p += 4; return v
def fstr():
    global p
    n = i32()
    if n == 0: return ""
    if n < 0:
        n = -n; s = d[p:p+2*(n-1)].decode("utf-16-le", "replace"); p += 2*n; return s
    s = d[p:p+n-1].decode("latin-1"); p += n; return s
nfiles = i32()
for _ in range(nfiles):
    fname = fstr()
    nsec = i32()
    for _ in range(nsec):
        sec = fstr()
        nkeys = i32()
        keys = [(fstr(), fstr()) for _ in range(nkeys)]
        if sys.argv[2].lower() in sec.lower():
            for k, v in keys:
                if len(sys.argv) < 4 or sys.argv[3].lower() in (k + "=" + v).lower():
                    print(f"{fname} [{sec}] {k}={v}")
