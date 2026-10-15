"""Computes UE3 script property offsets for a class from the decompressed package.

Follows the UField::Next chain (property export blob: NetIndex, None-tag, SuperField, Next,
ArrayDim, PropertyFlags(u64), Category(FName), ...) and applies UProperty::Link rules for the
Xbox 360 build: bools merge into one DWORD bitfield with masks 0x80000000 >> n; bytes align 1;
names 8; strings/arrays/delegates 12; structs use the struct's computed size.

python -I tools/ue3/layout.py <pkg.upk> <ClassName> <parent_size> [extra.upk ...]
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upk import Package  # noqa: E402

KNOWN_STRUCTS = {"Vector": (12, 4), "Rotator": (12, 4), "Vector2D": (8, 4), "Color": (4, 4),
                 "LinearColor": (16, 4), "Guid": (16, 4), "Plane": (16, 16), "Quat": (16, 16),
                 "Matrix": (64, 16), "Box": (28, 4), "Vector4": (16, 16), "IntPoint": (8, 4),
                 "TwoVectors": (24, 4), "Pointer": (4, 4), "Double": (8, 8), "QWord": (8, 4)}
SIZES = {"IntProperty": 4, "FloatProperty": 4, "ObjectProperty": 4, "ClassProperty": 4,
         "ComponentProperty": 4, "InterfaceProperty": 8, "NameProperty": 8, "StrProperty": 12,
         "ArrayProperty": 12, "DelegateProperty": 12, "MapProperty": 20, "ByteProperty": 1}


def prop_fields(pk, e):
    b = pk.blob(e)
    nxt = struct.unpack_from(">i", b, 16)[0]
    dim = struct.unpack_from(">i", b, 20)[0]
    flags = struct.unpack_from(">Q", b, 24)[0]
    extra = struct.unpack_from(">i", b, 40)[0] if len(b) >= 44 else 0
    # CPF_Net properties carry a 2-byte RepOffset before the class-specific data.
    data_at = 44 + (2 if flags & 0x20 else 0)
    if pk.class_name(e) == "StructProperty" and len(b) >= data_at + 4:
        extra = struct.unpack_from(">i", b, data_at)[0]
    return nxt, dim, flags, extra


def chain(pk, cls_e):
    kids = [e for e in pk.children_of(cls_e["index"] + 1) if pk.class_name(e).endswith("Property")]
    by_ref = {e["index"] + 1: e for e in kids}
    nexts = set()
    for e in kids:
        nxt, *_ = prop_fields(pk, e)
        nexts.add(nxt)
    heads = [e for e in kids if e["index"] + 1 not in nexts]
    out = []
    seen = set()
    for h in heads:
        cur = h
        while cur is not None and cur["index"] not in seen:
            seen.add(cur["index"])
            out.append(cur)
            nxt, *_ = prop_fields(pk, cur)
            cur = by_ref.get(nxt)
    return out, heads


def struct_size(pks, name):
    if name in KNOWN_STRUCTS:
        return KNOWN_STRUCTS[name]
    for pk in pks:
        for e in pk.find(name, "ScriptStruct"):
            props, _ = chain(pk, e)
            size, align = layout(pks, pk, props, 0, quiet=True)
            return (size + align - 1) // align * align, align
    return None, 4


def layout(pks, pk, props, start, quiet=False):
    off = start
    last_bool = None
    max_align = 4
    for e in props:
        cls = pk.class_name(e)
        nxt, dim, flags, extra = prop_fields(pk, e)
        if cls == "BoolProperty":
            if last_bool is not None and last_bool[1] > 1:
                o, mask = last_bool[0], last_bool[1] >> 1
            else:
                o = (off + 3) & ~3
                mask = 0x80000000
                off = o + 4
            last_bool = (o, mask)
            if not quiet:
                print(f"+{o:4d} (0x{o:03X}) mask 0x{mask:08X} {cls:18s} {e['name']} flags=0x{flags:X}")
            continue
        last_bool = None
        if cls == "StructProperty":
            sname = pk.obj_name(extra)
            size, align = struct_size(pks, sname)
            if size is None:
                print(f"!! unknown struct {sname} for {e['name']}; stopping")
                return off, max_align
            max_align = max(max_align, align)
        else:
            size = SIZES.get(cls)
            align = 1 if cls == "ByteProperty" else 4
            if size is None:
                print(f"!! unknown property class {cls} for {e['name']}; stopping")
                return off, max_align
        o = (off + align - 1) // align * align
        if not quiet:
            note = f" [{dim}]" if dim != 1 else ""
            sname = f" <{pk.obj_name(extra)}>" if cls == "StructProperty" else ""
            print(f"+{o:4d} (0x{o:03X})               {cls:18s} {e['name']}{note}{sname} flags=0x{flags:X}")
        off = o + size * dim
    return off, max_align


if __name__ == "__main__":
    pk = Package(sys.argv[1])
    extras = [Package(p) for p in sys.argv[4:]]
    cls_e = pk.find(sys.argv[2], "Class")[0]
    props, heads = chain(pk, cls_e)
    print(f"{sys.argv[2]}: {len(props)} properties, chain heads {[h['name'] for h in heads]}")
    end, _ = layout([pk] + extras, pk, props, int(sys.argv[3]))
    print(f"end = {end}")
