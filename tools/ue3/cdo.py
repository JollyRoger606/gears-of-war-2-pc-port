"""Prints the tagged properties of a class default object (Default__<Class>) from a decompressed
GoW2 package (UE3 575, big-endian).

python -I tools/ue3/cdo.py <pkg.upk> <ClassName>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upk import Package  # noqa: E402


def read_tags(pk, data, pos):
    out = []
    while pos + 8 <= len(data):
        name = pk.name_at(*struct.unpack_from(">ii", data, pos))
        pos += 8
        if name == "None":
            break
        typ = pk.name_at(*struct.unpack_from(">ii", data, pos))
        pos += 8
        size, idx = struct.unpack_from(">ii", data, pos)
        pos += 8
        extra = ""
        if typ == "StructProperty":
            extra = pk.name_at(*struct.unpack_from(">ii", data, pos))
            pos += 8
        if typ == "BoolProperty":
            # 575 stores the tag's BoolVal as a 4-byte UBOOL
            val = struct.unpack_from(">i", data, pos)[0]
            pos += 4
            out.append((name, idx, typ, bool(val)))
            continue
        raw = data[pos:pos + size]
        pos += size
        if typ == "FloatProperty":
            val = struct.unpack(">f", raw)[0]
        elif typ == "IntProperty":
            val = struct.unpack(">i", raw)[0]
        elif typ == "ByteProperty":
            val = raw[0] if size == 1 else pk.name_at(*struct.unpack(">ii", raw))
        elif typ == "NameProperty":
            val = pk.name_at(*struct.unpack(">ii", raw))
        elif typ == "ObjectProperty":
            val = pk.path(struct.unpack(">i", raw)[0])
        elif typ == "StructProperty" and extra in ("Vector2D",):
            val = struct.unpack(">ff", raw)
        elif typ == "StructProperty" and extra in ("Vector", "Rotator"):
            val = struct.unpack(">fff" if extra == "Vector" else ">iii", raw)
        else:
            val = raw.hex()
        out.append((name, idx, f"{typ}{'<' + extra + '>' if extra else ''}", val))
    return out


if __name__ == "__main__":
    pk = Package(sys.argv[1])
    name = "Default__" + sys.argv[2]
    hits = [e for e in pk.exports if e["name"] == name]
    if not hits:
        print("no CDO", name)
        sys.exit(1)
    e = hits[0]
    data = pk.blob(e)
    # UObject::Serialize: NetIndex (INT) then tagged properties.
    for row in read_tags(pk, data, 4):
        print(row)
