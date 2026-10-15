"""Dump Kismet objects of a decompressed GoW2 map package as JSON lines.

python -I tools/ue3/kismet_scan.py <pkg.upk> [--all]
Each line: {"i": export index, "cls": class, "path": full path, "props": {...}}.
Only Seq* exports (and InterpGroup*) unless --all.
"""
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3pkg import Pkg  # noqa: E402

STRUCT_ARRAYS = {"VariableLinks", "OutputLinks", "InputLinks", "EventLinks", "Links", "GroupInst",
                 "LinkedVariables_struct", "PlayerVarLinks"}
OBJ_ARRAYS = {"LinkedVariables", "LinkedEvents", "SequenceObjects", "Targets", "ObjValue_arr",
              "GroupTracks", "InterpGroups", "Originator", "Instigator", "TrackInst"}


class Reader:
    def __init__(self, pkg):
        self.p = pkg
        self.b = pkg.b

    def name_at(self, pos):
        idx, num = struct.unpack_from(">ii", self.b, pos)
        if not (0 <= idx < len(self.p.names)):
            raise ValueError("bad name")
        n = self.p.names[idx]
        return n if num == 0 else f"{n}_{num - 1}"

    def tagged(self, pos, end, depth=0):
        out = {}
        while pos < end:
            name = self.name_at(pos)
            pos += 8
            if name == "None":
                return out, pos
            typ = self.name_at(pos)
            pos += 8
            size, arr = struct.unpack_from(">ii", self.b, pos)
            pos += 8
            sname = None
            if typ == "StructProperty":
                sname = self.name_at(pos)
                pos += 8
            if typ == "BoolProperty":
                (bv,) = struct.unpack_from(">I", self.b, pos)
                pos += 4
                self.put(out, name, arr, bool(bv))
                continue
            raw_start = pos
            pos += size
            self.put(out, name, arr, self.value(typ, sname, name, raw_start, size, depth))
        return out, pos

    def put(self, out, name, arr, val):
        key = name if arr == 0 else f"{name}[{arr}]"
        out[key] = val

    def obj(self, idx):
        if idx == 0:
            return None
        return f"{self.p.obj_class(idx)}:{self.p.obj_path(idx)}"

    def value(self, typ, sname, name, pos, size, depth):
        b = self.b
        if typ == "IntProperty":
            return struct.unpack_from(">i", b, pos)[0]
        if typ == "FloatProperty":
            return round(struct.unpack_from(">f", b, pos)[0], 4)
        if typ == "ByteProperty":
            return b[pos] if size == 1 else self.name_at(pos)
        if typ == "NameProperty":
            return self.name_at(pos)
        if typ in ("ObjectProperty", "ClassProperty", "ComponentProperty", "InterfaceProperty"):
            return self.obj(struct.unpack_from(">i", b, pos)[0])
        if typ == "StrProperty":
            n = struct.unpack_from(">i", b, pos)[0]
            if n > 0:
                return b[pos + 4:pos + 4 + n - 1].decode("latin-1")
            if n < 0:
                return b[pos + 4:pos + 4 - 2 * n].decode("utf-16-be", "replace").rstrip("\0")
            return ""
        if typ == "StructProperty":
            if sname in ("Vector", "Rotator") and size == 12:
                fmt = ">fff" if sname == "Vector" else ">iii"
                return list(struct.unpack_from(fmt, b, pos))
            if depth < 4:
                try:
                    v, _ = self.tagged(pos, pos + size, depth + 1)
                    return v
                except Exception:
                    pass
            return f"<struct {sname} {size}B>"
        if typ == "ArrayProperty":
            n = struct.unpack_from(">i", b, pos)[0]
            body = pos + 4
            end = pos + size
            if n == 0:
                return []
            elem = (size - 4) // n if n > 0 else 0
            if elem == 4 and (size - 4) == 4 * n:
                vals = [struct.unpack_from(">i", b, body + 4 * k)[0] for k in range(n)]
                if name in OBJ_ARRAYS or all(-len(self.p.imports) <= v <= len(self.p.exports) for v in vals):
                    return [self.obj(v) for v in vals]
                return vals
            if depth < 4:
                try:
                    items = []
                    p2 = body
                    for _ in range(n):
                        v, p2 = self.tagged(p2, end, depth + 1)
                        items.append(v)
                    return items
                except Exception:
                    pass
            return f"<array n={n} {size}B>"
        return f"<{typ} {size}B>"


def main():
    pkg = Pkg(sys.argv[1])
    want_all = "--all" in sys.argv
    r = Reader(pkg)
    for i, e in enumerate(pkg.exports, start=1):
        cls = pkg.obj_class(i)
        if not want_all and not (cls.startswith("Seq") or cls.startswith("InterpGroup")):
            continue
        start = e["serial_offset"] + 4
        try:
            props, _ = r.tagged(start, e["serial_offset"] + e["serial_size"])
        except Exception as ex:  # noqa: BLE001
            props = {"<error>": str(ex)}
        print(json.dumps({"i": i, "cls": cls, "path": pkg.obj_path(i), "props": props}, default=str))


if __name__ == "__main__":
    main()
