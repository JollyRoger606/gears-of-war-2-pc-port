"""Minimal reader for a decompressed GoW2 (UE3 ver 575, big-endian) package.

python -I tools/ue3/ue3pkg.py <pkg.upk> summary
python -I tools/ue3/ue3pkg.py <pkg.upk> exports <regex-on-full-path>
python -I tools/ue3/ue3pkg.py <pkg.upk> names <regex>
"""
import re
import struct
import sys


class Pkg:
    def __init__(self, path):
        self.b = open(path, "rb").read()
        self.pos = 0
        b = self.b
        tag = self.u32()
        assert tag == 0x9E2A83C1, hex(tag)
        self.file_version = self.u16()
        self.licensee = self.u16()
        self.header_size = self.u32()
        self.folder = self.fstring()
        self.package_flags = self.u32()
        self.name_count = self.u32()
        self.name_offset = self.u32()
        self.export_count = self.u32()
        self.export_offset = self.u32()
        self.import_count = self.u32()
        self.import_offset = self.u32()
        self.depends_offset = self.u32()
        self.read_names()
        self.read_imports()
        self.read_exports()

    def u8(self):
        v = self.b[self.pos]
        self.pos += 1
        return v

    def u16(self):
        (v,) = struct.unpack_from(">H", self.b, self.pos)
        self.pos += 2
        return v

    def u32(self):
        (v,) = struct.unpack_from(">I", self.b, self.pos)
        self.pos += 4
        return v

    def i32(self):
        (v,) = struct.unpack_from(">i", self.b, self.pos)
        self.pos += 4
        return v

    def u64(self):
        (v,) = struct.unpack_from(">Q", self.b, self.pos)
        self.pos += 8
        return v

    def fstring(self):
        n = self.i32()
        if n == 0:
            return ""
        if n > 0:
            s = self.b[self.pos:self.pos + n]
            self.pos += n
            return s.rstrip(b"\0").decode("latin-1")
        s = self.b[self.pos:self.pos - 2 * n]
        self.pos += -2 * n
        return s.decode("utf-16-le").rstrip("\0")

    def fname(self):
        idx = self.i32()
        num = self.i32()
        base = self.names[idx] if 0 <= idx < len(self.names) else f"<badname {idx}>"
        return base if num == 0 else f"{base}_{num - 1}"

    def read_names(self):
        self.pos = self.name_offset
        self.names = []
        for _ in range(self.name_count):
            self.names.append(self.fstring())
            self.u64()

    def read_imports(self):
        self.pos = self.import_offset
        self.imports = []
        for _ in range(self.import_count):
            cpkg = self.fname()
            cname = self.fname()
            outer = self.i32()
            name = self.fname()
            self.imports.append({"class_package": cpkg, "class": cname, "outer": outer, "name": name})

    def read_exports(self):
        self.pos = self.export_offset
        self.exports = []
        for _ in range(self.export_count):
            e = {}
            e["class_index"] = self.i32()
            e["super_index"] = self.i32()
            e["outer"] = self.i32()
            e["name"] = self.fname()
            e["archetype"] = self.i32()
            e["object_flags"] = self.u64()
            e["serial_size"] = self.i32()
            e["serial_offset"] = self.i32()
            e["export_flags"] = self.u32()
            n = self.i32()
            self.pos += 4 * n
            self.pos += 16  # guid
            e["package_flags"] = self.u32()
            self.exports.append(e)

    def obj_name(self, index):
        if index == 0:
            return "None"
        if index > 0:
            return self.exports[index - 1]["name"]
        return self.imports[-index - 1]["name"]

    def obj_path(self, index):
        parts = []
        guard = 0
        while index != 0 and guard < 64:
            guard += 1
            if index > 0:
                e = self.exports[index - 1]
                parts.append(e["name"])
                index = e["outer"]
            else:
                i = self.imports[-index - 1]
                parts.append(i["name"])
                index = i["outer"]
        return ".".join(reversed(parts))

    def obj_class(self, index):
        if index > 0:
            ci = self.exports[index - 1]["class_index"]
            return "Class" if ci == 0 else self.obj_name(ci)
        if index < 0:
            return self.imports[-index - 1]["class"]
        return "None"


def main():
    p = Pkg(sys.argv[1])
    mode = sys.argv[2]
    if mode == "summary":
        print(f"ver={p.file_version} lic={p.licensee} names={p.name_count} exports={p.export_count} "
              f"imports={p.import_count} flags=0x{p.package_flags:08X}")
        for i in range(5):
            e = p.exports[i]
            print(i + 1, p.obj_class(i + 1), p.obj_path(i + 1), e["serial_size"], hex(e["serial_offset"]))
    elif mode == "exports":
        rx = re.compile(sys.argv[3], re.I)
        for i, e in enumerate(p.exports):
            path = p.obj_path(i + 1)
            if rx.search(path):
                print(f"{i + 1}\t{p.obj_class(i + 1)}\t{path}\tsize={e['serial_size']}\toff=0x{e['serial_offset']:X}"
                      f"\tsuper={p.obj_path(e['super_index']) if e['super_index'] else ''}")
    elif mode == "names":
        rx = re.compile(sys.argv[3], re.I)
        for i, n in enumerate(p.names):
            if rx.search(n):
                print(i, n)


if __name__ == "__main__":
    main()


def read_tagged(p, export_index):
    """Tagged default properties of an export (UE3 ver 575): list of (name, type, array_index, value)."""
    e = p.exports[export_index - 1]
    b = p.b
    pos = e["serial_offset"] + 4  # NetIndex
    end = e["serial_offset"] + e["serial_size"]
    out = []

    def fname(at):
        idx, num = struct.unpack_from(">ii", b, at)
        n = p.names[idx]
        return n if num == 0 else f"{n}_{num - 1}"

    while pos < end:
        name = fname(pos)
        pos += 8
        if name == "None":
            break
        typ = fname(pos)
        pos += 8
        size, arr = struct.unpack_from(">ii", b, pos)
        pos += 8
        extra = None
        if typ == "StructProperty":
            extra = fname(pos)
            pos += 8
        if typ == "BoolProperty":
            (bv,) = struct.unpack_from(">I", b, pos)
            pos += 4
            out.append((name, typ, arr, bool(bv)))
            continue
        raw = b[pos:pos + size]
        pos += size
        if typ == "FloatProperty":
            val = struct.unpack(">f", raw)[0]
        elif typ == "IntProperty":
            val = struct.unpack(">i", raw)[0]
        elif typ == "ByteProperty" and size == 1:
            val = raw[0]
        elif typ == "ByteProperty" and size == 8:
            val = fname_bytes(p, raw)
        elif typ == "NameProperty":
            val = fname_bytes(p, raw)
        elif typ == "ObjectProperty" or typ == "ClassProperty" or typ == "ComponentProperty":
            val = p.obj_path(struct.unpack(">i", raw)[0])
        elif typ == "StructProperty" and extra in ("Vector", "Rotator", "Vector2D") and size in (8, 12):
            fmt = ">fff" if extra == "Vector" else (">iii" if extra == "Rotator" else ">ff")
            val = f"{extra}{struct.unpack(fmt, raw)}"
        elif typ == "StrProperty":
            n = struct.unpack_from(">i", raw)[0]
            val = raw[4:4 + n - 1].decode("latin-1") if n > 0 else raw[4:].decode("utf-16-le", "replace")
        else:
            val = f"<{typ}{'/' + extra if extra else ''} {size} bytes: {raw[:48].hex()}>"
        out.append((name, typ, arr, val))
    return out


def fname_bytes(p, raw):
    idx, num = struct.unpack(">ii", raw[:8])
    n = p.names[idx]
    return n if num == 0 else f"{n}_{num - 1}"
