"""Minimal reader for the decompressed GoW2 Xbox 360 packages (UE3 file version 575, big-endian).

Reads the summary, name, import and export tables. Object references follow UE3:
>0 = export index+1, <0 = -(import index+1), 0 = null.

python -I tools/ue3/upk.py <file.upk> [--class Name]   prints exports of that class name, or a summary
"""
from __future__ import annotations

import struct
import sys


class Reader:
    def __init__(self, data: bytes, pos: int = 0):
        self.d = data
        self.p = pos

    def i32(self):
        v = struct.unpack_from(">i", self.d, self.p)[0]
        self.p += 4
        return v

    def u32(self):
        v = struct.unpack_from(">I", self.d, self.p)[0]
        self.p += 4
        return v

    def u64(self):
        v = struct.unpack_from(">Q", self.d, self.p)[0]
        self.p += 8
        return v

    def u16(self):
        v = struct.unpack_from(">H", self.d, self.p)[0]
        self.p += 2
        return v

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def fstring(self):
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            s = self.d[self.p:self.p + n - 1].decode("latin-1")
            self.p += n
            return s
        n = -n
        s = self.d[self.p:self.p + 2 * (n - 1)].decode("utf-16-be", errors="replace")
        self.p += 2 * n
        return s


class Package:
    def __init__(self, path: str):
        with open(path, "rb") as f:
            self.data = f.read()
        r = Reader(self.data)
        assert r.u32() == 0x9E2A83C1
        ver = r.u32()
        self.file_version = ver & 0xFFFF
        self.licensee = ver >> 16
        self.header_size = r.i32()
        self.folder = r.fstring()
        self.flags = r.u32()
        self.name_count, self.name_offset = r.i32(), r.i32()
        self.export_count, self.export_offset = r.i32(), r.i32()
        self.import_count, self.import_offset = r.i32(), r.i32()
        self.depends_offset = r.i32()
        self.names = []
        r.p = self.name_offset
        for _ in range(self.name_count):
            s = r.fstring()
            r.u64()  # flags
            self.names.append(s)
        self.imports = []
        r.p = self.import_offset
        for _ in range(self.import_count):
            cpkg = self.fname(r)
            cname = self.fname(r)
            outer = r.i32()
            oname = self.fname(r)
            self.imports.append({"class_package": cpkg, "class": cname, "outer": outer, "name": oname})
        self.exports = []
        r.p = self.export_offset
        for i in range(self.export_count):
            e = {"index": i}
            e["class"] = r.i32()
            e["super"] = r.i32()
            e["outer"] = r.i32()
            e["name"] = self.fname(r)
            e["archetype"] = r.i32()
            e["flags"] = r.u64()
            e["size"] = r.i32()
            e["offset"] = r.i32()
            e["export_flags"] = r.u32()
            n = r.i32()
            r.p += 4 * n  # NetObjectCount
            r.p += 16  # Guid
            e["pkg_flags"] = r.u32()
            self.exports.append(e)

    def fname(self, r: Reader) -> str:
        idx = r.i32()
        num = r.i32()
        if not (0 <= idx < len(self.names)):
            return f"<bad name {idx}>"
        s = self.names[idx]
        return s if num == 0 else f"{s}_{num - 1}"

    def name_at(self, idx: int, num: int = 0) -> str:
        s = self.names[idx] if 0 <= idx < len(self.names) else f"<bad name {idx}>"
        return s if num == 0 else f"{s}_{num - 1}"

    def obj_name(self, ref: int) -> str:
        if ref > 0:
            return self.exports[ref - 1]["name"]
        if ref < 0:
            return self.imports[-ref - 1]["name"]
        return "None"

    def class_name(self, e) -> str:
        return self.obj_name(e["class"]) if e["class"] else "Class"

    def path(self, ref: int) -> str:
        parts = []
        while ref:
            if ref > 0:
                e = self.exports[ref - 1]
                parts.append(e["name"])
                ref = e["outer"]
            else:
                i = self.imports[-ref - 1]
                parts.append(i["name"])
                ref = i["outer"]
        return ".".join(reversed(parts))

    def find(self, name: str, cls: str | None = None):
        out = []
        for e in self.exports:
            if e["name"] == name and (cls is None or self.class_name(e) == cls):
                out.append(e)
        return out

    def children_of(self, outer_index1: int):
        return [e for e in self.exports if e["outer"] == outer_index1]

    def blob(self, e) -> bytes:
        return self.data[e["offset"]:e["offset"] + e["size"]]


if __name__ == "__main__":
    pk = Package(sys.argv[1])
    print(f"version {pk.file_version}/{pk.licensee} names {pk.name_count} exports {pk.export_count} "
          f"imports {pk.import_count}")
    if "--class" in sys.argv:
        want = sys.argv[sys.argv.index("--class") + 1]
        for e in pk.exports:
            if e["name"] == want:
                print(e["index"] + 1, pk.class_name(e), pk.path(e["index"] + 1), e["size"], hex(e["offset"]))
