"""Expand a chunk-compressed UE3 X360 package (summary lists CompressedChunks) into a plain one.

Each chunk is a fully compressed stream (tag, block size, sizes, LZX blocks) that
ue3x360_decompress.exe already understands. The summary's CompressionFlags and chunk list are
zeroed in the output so ue3pkg.py reads it as an uncompressed package.

python -I tools/ue3/decompress_chunked.py <in.xxx> <out.upk>
"""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue3paths  # noqa: E402

TOOL = ue3paths.decompressor()


def read_summary(b):
    pos = 0

    def u32():
        nonlocal pos
        v = struct.unpack_from(">I", b, pos)[0]
        pos += 4
        return v

    def fstring():
        nonlocal pos
        n = struct.unpack_from(">i", b, pos)[0]
        pos += 4
        pos += n if n > 0 else -2 * n

    assert u32() == 0x9E2A83C1
    u32()  # version
    u32()  # header size
    fstring()
    for _ in range(8):  # flags, name count/off, export count/off, import count/off, depends off
        u32()
    pos += 16  # guid
    gens = u32()
    pos += gens * 12
    u32()  # engine version
    u32()  # cooker version
    flags_at = pos
    flags = u32()
    count_at = pos
    n = u32()
    chunks = []
    for _ in range(n):
        uo, us, co, cs = struct.unpack_from(">4I", b, pos)
        pos += 16
        chunks.append((uo, us, co, cs))
    return flags_at, flags, count_at, chunks, pos


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    b = src.read_bytes()
    flags_at, flags, count_at, chunks, end = read_summary(b)
    print(f"compression flags=0x{flags:X} chunks={len(chunks)}")
    if not chunks:
        dst.write_bytes(b)
        return
    total = max(uo + us for uo, us, _, _ in chunks)
    out = bytearray(total)
    head = bytearray(b[:chunks[0][0]])
    struct.pack_into(">I", head, flags_at, 0)
    struct.pack_into(">I", head, count_at, 0)
    # The chunk table shrinks to nothing; keep the header length by leaving its bytes behind the
    # zeroed count (the reader stops at the count and jumps to the name table by offset).
    out[:len(head)] = head
    with tempfile.TemporaryDirectory() as td:
        for i, (uo, us, co, cs) in enumerate(chunks):
            cin = Path(td) / f"c{i}.bin"
            cout = Path(td) / f"c{i}.out"
            cin.write_bytes(b[co:co + cs])
            r = subprocess.run([str(TOOL), str(cin), str(cout)], capture_output=True, text=True)
            if r.returncode != 0:
                raise SystemExit(f"chunk {i} failed: {r.stdout} {r.stderr}")
            data = cout.read_bytes()
            if len(data) != us:
                raise SystemExit(f"chunk {i}: got {len(data)} bytes, expected {us}")
            out[uo:uo + us] = data
    dst.write_bytes(bytes(out))
    print(f"wrote {len(out)} bytes")


if __name__ == "__main__":
    main()
