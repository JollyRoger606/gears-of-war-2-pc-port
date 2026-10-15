"""UnrealScript bytecode decompiler for GoW2's cooked script (UE3 ver 575, big-endian).

Loads Core/Engine/GameFramework/GearGame (decompressed with ue3x360_decompress) so native
indices resolve to their declared names.

python -I tools/ue3/ue3script.py decompile <regex on Class.Function path> [pkgname]
python -I tools/ue3/ue3script.py validate            (decode every function, report failures)
python -I tools/ue3/ue3script.py props <regex on Class.Property path> [pkgname]   (flags + config marker)
"""
import os
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3pkg import Pkg  # noqa: E402

import ue3paths  # noqa: E402
PKG_ORDER = ["Core", "Engine", "GameFramework", "GearGame"]

CPF = {
    0x1: "Edit", 0x2: "Const", 0x4: "Input", 0x8: "ExportObject", 0x10: "OptionalParm",
    0x20: "Net", 0x40: "EditFixedSize", 0x80: "Parm", 0x100: "OutParm", 0x200: "SkipParm",
    0x400: "ReturnParm", 0x800: "CoerceParm", 0x1000: "Native", 0x2000: "Transient",
    0x4000: "Config", 0x8000: "Localized", 0x20000: "EditConst", 0x40000: "GlobalConfig",
    0x80000: "Component", 0x100000: "AlwaysInit", 0x200000: "DuplicateTransient",
    0x400000: "NeedCtorLink", 0x800000: "NoExport", 0x2000000: "NoClear",
    0x4000000: "EditInline", 0x10000000: "EditInlineUse", 0x20000000: "Deprecated",
}

FUNC_FLAGS = {
    0x1: "Final", 0x2: "Defined", 0x4: "Iterator", 0x8: "Latent", 0x10: "PreOperator",
    0x20: "Singular", 0x40: "Net", 0x80: "NetReliable", 0x100: "Simulated", 0x200: "Exec",
    0x400: "Native", 0x800: "Event", 0x1000: "Operator", 0x2000: "Static",
    0x4000: "HasOptionalParms", 0x8000: "Const", 0x20000: "Public", 0x40000: "Private",
    0x80000: "Protected", 0x100000: "Delegate", 0x200000: "NetServer", 0x400000: "HasOutParms",
    0x800000: "HasDefaults", 0x1000000: "NetClient", 0x2000000: "DLLImport",
}


def flag_names(value, table):
    return "|".join(n for b, n in table.items() if value & b) or "0"


class Func:
    pass


class World:
    def __init__(self, pkg_dir=None):
        self.pkgs = {}
        if pkg_dir is None:
            pkg_dir = ue3paths.pkg_dir()
        else:
            pkg_dir = Path(pkg_dir)
        extra = [n for n in __import__("os").environ.get("UE3_EXTRA", "").split(",") if n]
        for name in PKG_ORDER + extra:
            path = pkg_dir / f"{name}.upk"
            if path.exists():
                self.pkgs[name] = Pkg(str(path))
        self.natives = {}
        for pname, p in self.pkgs.items():
            for i, e in enumerate(p.exports):
                if p.obj_class(i + 1) != "Function":
                    continue
                f = self.read_function(p, i + 1, header_only=True)
                if f and f.inative:
                    self.natives.setdefault(f.inative, (f.friendly, f.flags, p.obj_path(i + 1)))

    def read_function(self, p, index, header_only=False):
        e = p.exports[index - 1]
        b = p.b
        off = e["serial_offset"]
        end = off + e["serial_size"]
        f = Func()
        f.pkg, f.index, f.path = p, index, p.obj_path(index)
        pos = off + 4 + 8  # NetIndex, None
        f.super_field, f.next, f.script_text, f.children, f.cpp_text, f.line, f.text_pos, f.script_size = \
            struct.unpack_from(">8i", b, pos)
        pos += 32
        f.code_start = pos
        tail = end - 15
        f.code_end = tail
        f.inative, f.precedence, f.flags = struct.unpack_from(">HBI", b, tail)
        f.friendly = p.names[struct.unpack_from(">i", b, tail + 7)[0]]
        if f.flags & 0x40:  # FUNC_Net carries a RepOffset before FriendlyName
            tail = end - 17
            f.code_end = tail
            f.inative, f.precedence, f.flags = struct.unpack_from(">HBI", b, tail)
            f.friendly = p.names[struct.unpack_from(">i", b, tail + 9)[0]]
        return f


class Decoder:
    def __init__(self, world, func):
        self.w = world
        self.f = func
        self.p = func.pkg
        self.b = func.pkg.b
        self.pos = func.code_start
        self.base = func.code_start

    # --- primitive reads
    def u8(self):
        v = self.b[self.pos]
        self.pos += 1
        return v

    def u16(self):
        (v,) = struct.unpack_from(">H", self.b, self.pos)
        self.pos += 2
        return v

    def i32(self):
        (v,) = struct.unpack_from(">i", self.b, self.pos)
        self.pos += 4
        return v

    def f32(self):
        (v,) = struct.unpack_from(">f", self.b, self.pos)
        self.pos += 4
        return v

    def obj(self):
        idx = self.i32()
        return self.p.obj_name(idx) if idx else "None", idx

    def name(self):
        idx = self.i32()
        num = self.i32()
        n = self.p.names[idx] if 0 <= idx < len(self.p.names) else f"<name?{idx}>"
        return n if num == 0 else f"{n}_{num - 1}"

    def objpath(self, idx):
        return self.p.obj_path(idx) if idx else "None"

    # --- expressions
    def params(self):
        args = []
        while True:
            if self.b[self.pos] == 0x16:
                self.pos += 1
                return args
            args.append(self.expr())

    def expr(self):
        start = self.pos - self.base
        t = self.u8()
        if t >= 0x70:
            return self.native(t)
        if 0x60 <= t <= 0x6F:
            return self.native(((t - 0x60) << 8) | self.u8())
        h = HANDLERS.get(t)
        if h is None:
            raise ValueError(f"unknown token 0x{t:02X} at code+0x{start:X}")
        return h(self)

    def native(self, idx):
        info = self.w.natives.get(idx)
        args = self.params()
        if not info:
            return f"native{idx}({', '.join(args)})"
        friendly, flags, path = info
        if flags & 0x10 and len(args) == 1:  # pre-operator
            return f"{friendly}({args[0]})"
        if flags & 0x1000 and len(args) == 2:
            return f"({args[0]} {friendly} {args[1]})"
        if flags & 0x1000 and len(args) == 1:  # post-operator
            return f"({args[0]}){friendly}"
        return f"{friendly}({', '.join(args)})"


def h_local(d):
    n, _ = d.obj()
    return n


def h_instance(d):
    n, _ = d.obj()
    return n


def h_default(d):
    n, _ = d.obj()
    return f"default.{n}"


def h_return(d):
    return f"return {d.expr()}"


def h_switch(d):
    d.pos += SWITCH_EXTRA
    return f"switch ({d.expr()})"


SWITCH_EXTRA = 2


def h_jump(d):
    return f"goto 0x{d.u16():X}"


def h_jumpifnot(d):
    off = d.u16()
    return f"if !({d.expr()}) goto 0x{off:X}"


def h_stop(d):
    return "stop"


def h_assert(d):
    d.u16()
    d.u8()
    return f"assert({d.expr()})"


def h_case(d):
    off = d.u16()
    if off == 0xFFFF:
        return "default:"
    return f"case {d.expr()}: (next 0x{off:X})"


def h_nothing(d):
    return "nothing"


def h_labeltable(d):
    labels = []
    while True:
        n = d.name()
        off = d.i32()
        if n == "None":
            break
        labels.append(f"{n}@0x{off:X}")
    return f"labels[{', '.join(labels)}]"


def h_gotolabel(d):
    return f"goto label {d.expr()}"


def h_eatreturn(d):
    d.obj()
    return d.expr()


def h_let(d):
    a = d.expr()
    b = d.expr()
    return f"{a} = {b}"


def h_dynel(d):
    i = d.expr()
    a = d.expr()
    return f"{a}[{i}]"


def h_new(d):
    parts = [d.expr() for _ in range(5)]
    return f"new({', '.join(parts)})"


CONTEXT_EXTRA = 2  # bytes between the u16 skip and the context expression


def h_context(d):
    o = d.expr()
    d.u16()  # skip
    d.pos += CONTEXT_EXTRA
    c = d.expr()
    return f"{o}.{c}"


def h_metacast(d):
    n, _ = d.obj()
    return f"class<{n}>({d.expr()})"


def h_letbool(d):
    a = d.expr()
    b = d.expr()
    return f"{a} = {b}"


def h_endparm(d):
    return "<endparm>"


def h_endfuncparms(d):
    return "<endparms>"


def h_self(d):
    return "self"


def h_skip(d):
    d.u16()
    return d.expr()


def h_arrayel(d):
    i = d.expr()
    a = d.expr()
    return f"{a}[{i}]"


def h_virtual(d):
    n = d.name()
    args = d.params()
    return f"{n}({', '.join(args)})"


def h_final(d):
    n, idx = d.obj()
    args = d.params()
    owner = d.objpath(idx).rsplit(".", 1)[0] if idx else ""
    mine = d.f.path.rsplit(".", 1)[0]
    if owner and owner != mine and n == d.f.path.rsplit(".", 1)[1]:
        return f"Super({owner}).{n}({', '.join(args)})"
    return f"{n}({', '.join(args)})"


def h_intconst(d):
    return str(d.i32())


def h_floatconst(d):
    return repr(round(d.f32(), 6))


def h_strconst(d):
    s = bytearray()
    while True:
        c = d.u8()
        if c == 0:
            break
        s.append(c)
    return '"' + s.decode("latin-1") + '"'


def h_objconst(d):
    n, idx = d.obj()
    return f"obj'{d.objpath(idx)}'"


def h_nameconst(d):
    return f"'{d.name()}'"


def h_rotconst(d):
    return f"rot({d.i32()},{d.i32()},{d.i32()})"


def h_vecconst(d):
    return f"vect({d.f32():g},{d.f32():g},{d.f32():g})"


def h_byteconst(d):
    return str(d.u8())


def h_intzero(d):
    return "0"


def h_intone(d):
    return "1"


def h_true(d):
    return "true"


def h_false(d):
    return "false"


def h_nativeparm(d):
    n, _ = d.obj()
    return n


def h_noobject(d):
    return "None"


def h_intconstbyte(d):
    return str(d.u8())


def h_boolvar(d):
    return d.expr()


def h_dyncast(d):
    n, _ = d.obj()
    return f"{n}({d.expr()})"


def h_iterator(d):
    e = d.expr()
    d.u16()
    return f"foreach {e}"


def h_iterpop(d):
    return "iteratorpop"


def h_iternext(d):
    return "iteratornext"


def h_structcmp(op):
    def f(d):
        d.obj()
        a = d.expr()
        b = d.expr()
        return f"({a} {op} {b})"
    return f


def h_unistr(d):
    s = bytearray()
    while True:
        c = d.b[d.pos:d.pos + 2]
        d.pos += 2
        if c == b"\0\0":
            break
        s += c
    return '"' + s.decode("utf-16-be", "replace") + '"'


def h_structmember(d):
    m, _ = d.obj()
    d.obj()  # owning struct
    d.u8()
    d.u8()
    s = d.expr()
    return f"{s}.{m}"


def h_dynlen(d):
    return f"{d.expr()}.Length"


def h_global(d):
    n = d.name()
    args = d.params()
    return f"global.{n}({', '.join(args)})"


CASTS = {0x36: "Vector2Rotator", 0x39: "Rotator2Vector", 0x3A: "ByteToInt", 0x3B: "ByteToBool",
         0x3C: "ByteToFloat", 0x3D: "IntToByte", 0x3E: "IntToBool", 0x3F: "IntToFloat",
         0x40: "BoolToByte", 0x41: "BoolToInt", 0x42: "BoolToFloat", 0x43: "FloatToByte",
         0x44: "FloatToInt", 0x45: "FloatToBool", 0x46: "ObjectToInterface", 0x47: "ObjectToBool",
         0x48: "NameToBool", 0x49: "StringToByte", 0x4A: "StringToInt", 0x4B: "StringToBool",
         0x4C: "StringToFloat", 0x4D: "StringToVector", 0x4E: "StringToRotator",
         0x4F: "VectorToBool", 0x50: "VectorToRotator", 0x51: "RotatorToBool",
         0x52: "ByteToString", 0x53: "IntToString", 0x54: "BoolToString", 0x55: "FloatToString",
         0x56: "ObjectToString", 0x57: "NameToString", 0x58: "VectorToString",
         0x59: "RotatorToString", 0x5A: "DelegateToString", 0x5B: "StringToDelegate",
         0x5C: "StringToName"}


def h_primcast(d):
    c = d.u8()
    return f"{CASTS.get(c, f'cast{c:02X}')}({d.expr()})"


def h_dyninsert(d):
    a = d.expr()
    i = d.expr()
    n = d.expr()
    return f"{a}.Insert({i}, {n})"


def h_returnnothing(d):
    d.obj()
    return "return"


def h_delcmp(op):
    def f(d):
        a = d.expr()
        b = d.expr()
        d.u8()  # EX_EndFunctionParms
        return f"({a} {op} {b})"
    return f


def h_emptydelegate(d):
    return "None"


def h_dynremove(d):
    a = d.expr()
    i = d.expr()
    n = d.expr()
    return f"{a}.Remove({i}, {n})"


def h_debuginfo(d):
    d.i32()
    d.i32()
    d.i32()
    d.u8()
    return "debuginfo"


def h_delegatefunction(d):
    d.u8()
    n, _ = d.obj()
    fn = d.name()
    args = d.params()
    return f"{n}<{fn}>({', '.join(args)})"


def h_delegateproperty(d):
    n = d.name()
    return f"delegate {n}"


def h_letdelegate(d):
    a = d.expr()
    b = d.expr()
    return f"{a} = {b}"


def h_conditional(d):
    c = d.expr()
    d.u16()
    a = d.expr()
    d.u16()
    b = d.expr()
    return f"({c} ? {a} : {b})"


def h_dynfind(d):
    a = d.expr()
    d.u16()
    x = d.expr()
    return f"{a}.Find({x})"


def h_dynfindstruct(d):
    a = d.expr()
    d.u16()
    x = d.expr()
    y = d.expr()
    return f"{a}.Find({x}, {y})"


def h_localout(d):
    n, _ = d.obj()
    return n


def h_defaultparm(d):
    d.u16()
    e = d.expr()
    d.u8()  # EX_EndParmValue
    return f"defaultparm {e}"


def h_emptyparm(d):
    return ""


def h_instdelegate(d):
    return f"delegate {d.name()}"


def h_interfacecontext(d):
    return d.expr()


def h_interfacecast(d):
    n, _ = d.obj()
    return f"{n}({d.expr()})"


def h_end(d):
    return "<EndOfScript>"


def h_dynadd(d):
    a = d.expr()
    d.u16()
    n = d.expr()
    return f"{a}.Add({n})"


def h_dynadditem(d):
    a = d.expr()
    d.u16()
    x = d.expr()
    return f"{a}.AddItem({x})"


def h_dynremoveitem(d):
    a = d.expr()
    d.u16()
    x = d.expr()
    return f"{a}.RemoveItem({x})"


def h_dyninsertitem(d):
    a = d.expr()
    d.u16()
    i = d.expr()
    x = d.expr()
    return f"{a}.InsertItem({i}, {x})"


def h_dyniter(d):
    a = d.expr()
    v = d.expr()
    has_index = d.u8()
    i = d.expr()
    d.u16()
    return f"foreach {a}({v}{', ' + i if has_index else ''})"


HANDLERS = {
    0x00: h_local, 0x01: h_instance, 0x02: h_default, 0x04: h_return, 0x05: h_switch,
    0x06: h_jump, 0x07: h_jumpifnot, 0x08: h_stop, 0x09: h_assert, 0x0A: h_case,
    0x0B: h_nothing, 0x0C: h_labeltable, 0x0D: h_gotolabel, 0x0E: h_eatreturn, 0x0F: h_let,
    0x10: h_dynel, 0x11: h_new, 0x12: h_context, 0x13: h_metacast, 0x14: h_letbool,
    0x15: h_endparm, 0x16: h_endfuncparms, 0x17: h_self, 0x18: h_skip, 0x19: h_context,
    0x1A: h_arrayel, 0x1B: h_virtual, 0x1C: h_final, 0x1D: h_intconst, 0x1E: h_floatconst,
    0x1F: h_strconst, 0x20: h_objconst, 0x21: h_nameconst, 0x22: h_rotconst, 0x23: h_vecconst,
    0x24: h_byteconst, 0x25: h_intzero, 0x26: h_intone, 0x27: h_true, 0x28: h_false,
    0x29: h_nativeparm, 0x2A: h_noobject, 0x2C: h_intconstbyte, 0x2D: h_boolvar,
    0x2E: h_dyncast, 0x2F: h_iterator, 0x30: h_iterpop, 0x31: h_iternext,
    0x32: h_structcmp("=="), 0x33: h_structcmp("!="), 0x34: h_unistr, 0x35: h_structmember,
    0x36: h_dynlen, 0x37: h_global, 0x38: h_primcast, 0x39: h_dyninsert, 0x3A: h_returnnothing,
    0x3B: h_delcmp("=="), 0x3C: h_delcmp("!="), 0x3D: h_delcmp("=="), 0x3E: h_delcmp("!="),
    0x3F: h_emptydelegate, 0x40: h_dynremove, 0x41: h_debuginfo, 0x42: h_delegatefunction,
    0x43: h_delegateproperty, 0x44: h_letdelegate, 0x45: h_conditional, 0x46: h_dynfind,
    0x47: h_dynfindstruct, 0x48: h_localout, 0x49: h_defaultparm, 0x4A: h_emptyparm,
    0x4B: h_instdelegate, 0x51: h_interfacecontext, 0x52: h_interfacecast, 0x53: h_end,
    0x54: h_dynadd, 0x55: h_dynadditem, 0x56: h_dynremoveitem, 0x57: h_dyninsertitem,
    0x58: h_dyniter,
}


def decompile(world, f):
    d = Decoder(world, f)
    lines = []
    while d.pos < f.code_end:
        at = d.pos - d.base
        s = d.expr()
        lines.append(f"  {at:04X}: {s}")
        if s == "<EndOfScript>":
            break
    ok = d.pos == f.code_end
    return lines, ok, d.pos - d.base


def locals_of(f):
    out = []
    idx = f.children
    guard = 0
    while idx > 0 and guard < 256:
        guard += 1
        e = f.pkg.exports[idx - 1]
        cls = f.pkg.obj_class(idx)
        flags = prop_flags(f.pkg, idx)
        kind = "param" if flags & 0x80 else "local"
        if flags & 0x400:
            kind = "return"
        if flags & 0x100:
            kind = "out " + kind
        out.append(f"{kind} {cls} {e['name']}")
        idx = struct.unpack_from(">i", f.pkg.b, e["serial_offset"] + 4 + 8 + 4)[0]  # Next
    return out


def prop_flags(p, idx):
    e = p.exports[idx - 1]
    return struct.unpack_from(">Q", p.b, e["serial_offset"] + 4 + 8 + 4 + 4 + 4)[0]


def main():
    w = World()
    mode = sys.argv[1]
    if mode == "decompile":
        rx = re.compile(sys.argv[2])
        only = sys.argv[3] if len(sys.argv) > 3 else None
        for pname, p in w.pkgs.items():
            if only and pname != only:
                continue
            for i in range(len(p.exports)):
                if p.obj_class(i + 1) != "Function":
                    continue
                path = p.obj_path(i + 1)
                if not rx.search(path):
                    continue
                f = w.read_function(p, i + 1)
                print(f"== {pname}.{path}  flags={flag_names(f.flags, FUNC_FLAGS)} native={f.inative} "
                      f"script={f.script_size}")
                for l in locals_of(f):
                    print("   " + l)
                try:
                    lines, ok, consumed = decompile(w, f)
                    print("\n".join(lines))
                    if not ok:
                        print(f"  !! decode ended at 0x{consumed:X}, code size 0x{f.code_end - f.code_start:X}")
                except Exception as ex:  # noqa: BLE001
                    print(f"  !! {ex}")
    elif mode == "validate":
        total = bad = 0
        for pname, p in w.pkgs.items():
            for i in range(len(p.exports)):
                if p.obj_class(i + 1) != "Function":
                    continue
                f = w.read_function(p, i + 1)
                if f.flags & 0x400 and f.code_end - f.code_start <= 1:
                    continue
                total += 1
                try:
                    _, ok, consumed = decompile(w, f)
                    if not ok:
                        bad += 1
                        if bad <= 15:
                            print(f"MISMATCH {pname}.{f.path} ended 0x{consumed:X} of 0x{f.code_end - f.code_start:X}")
                except Exception as ex:  # noqa: BLE001
                    bad += 1
                    if bad <= 15:
                        print(f"FAIL {pname}.{f.path}: {ex}")
        print(f"functions={total} failed={bad}")
    elif mode == "props":
        rx = re.compile(sys.argv[2])
        only = sys.argv[3] if len(sys.argv) > 3 else None
        for pname, p in w.pkgs.items():
            if only and pname != only:
                continue
            for i in range(len(p.exports)):
                cls = p.obj_class(i + 1)
                if not cls.endswith("Property"):
                    continue
                path = p.obj_path(i + 1)
                if not rx.search(path):
                    continue
                fl = prop_flags(p, i + 1)
                print(f"{pname}.{path}\t{cls}\t0x{fl:X}\t{flag_names(fl, CPF)}")


if __name__ == "__main__":
    main()
