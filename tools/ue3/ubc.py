"""UnrealScript bytecode lister for GoW2 cooked packages (UE3 file version 575, big-endian).

Token formats were checked against the GearGame package itself (e.g. EX_Context carries a
4-byte header between its object and member expressions in this version).

python -I tools/ue3/ubc.py <pkg.upk> <Class> <Function> [more packages for name/obj resolution]
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from upk import Package  # noqa: E402

NATIVE_NAMES = {
    112: "$", 113: "GotoState", 114: "==(obj)", 119: "!=(obj)", 129: "!", 130: "&&", 131: "^^", 132: "||",
    133: "*=(byte)", 134: "/=(byte)", 135: "+=(byte)", 136: "-=(byte)", 137: "++(byte)", 138: "--(byte)",
    139: "(byte)++", 140: "(byte)--", 141: "~(int)", 142: "==(rot)", 143: "-(int)", 144: "*(int)",
    145: "/(int)", 146: "+(int)", 147: "-(int,int)", 148: "<<", 149: ">>", 150: "<(int)", 151: ">(int)",
    152: "<=(int)", 153: ">=(int)", 154: "==(int)", 155: "!=(int)", 156: "&", 157: "^", 158: "|",
    159: "*=(int)", 160: "/=(int)", 161: "+=(int)", 162: "-=(int)", 163: "++(int)", 164: "--(int)",
    165: "(int)++", 166: "(int)--", 167: "Rand", 168: "@", 169: "-(float)", 170: "**", 171: "*",
    172: "/", 173: "%", 174: "+", 175: "-", 176: "<", 177: ">", 178: "<=", 179: ">=", 180: "==",
    181: "!=", 182: "*=", 183: "/=", 184: "+=", 185: "-=", 186: "Abs", 187: "Sin", 188: "Cos",
    189: "Tan", 190: "Atan", 191: "Exp", 192: "Loge", 193: "Sqrt", 194: "Square", 195: "FRand",
    196: ">>>", 197: "IsA", 203: "!=(rot)", 210: "~=", 211: "-(vec)", 212: "*(vec,float)",
    213: "*(float,vec)", 214: "/(vec,float)", 215: "+(vec)", 216: "-(vec,vec)", 217: "==(vec)",
    218: "!=(vec)", 219: "Dot", 220: "Cross", 221: "*=(vec)", 222: "/=(vec)", 223: "+=(vec)",
    224: "-=(vec)", 225: "VSize", 226: "Normal", 229: "GetAxes", 230: "GetUnAxes", 231: "LogInternal",
    232: "WarnInternal", 242: "==(bool)", 243: "!=(bool)", 244: "FMin", 245: "FMax", 246: "FClamp",
    247: "Lerp", 249: "Min", 250: "Max", 251: "Clamp", 252: "VRand", 254: "==(name)", 255: "!=(name)",
    258: "ClassIsChildOf", 275: "<<(vec,rot)", 276: ">>(vec,rot)", 281: "IsInState", 284: "GetStateName",
    287: "*(rot,float)", 288: "*(float,rot)", 289: "/(rot,float)", 290: "*=(rot)", 291: "/=(rot)",
    296: "*(vec,vec)", 297: "*=(vec,vec)", 298: "/=(vec,vec)", 316: "+(rot)", 317: "-(rot)",
    318: "+=(rot)", 319: "-=(rot)", 320: "RotRand", 321: "Mirror",
}


class Lister:
    def __init__(self, pk: Package):
        self.pk = pk

    def ref(self, v: int) -> str:
        if v == 0:
            return "None"
        try:
            return self.pk.path(v)
        except IndexError:
            return f"<ref {v}>"

    def disasm(self, code: bytes):
        self.c = code
        self.p = 0
        lines = []
        while self.p < len(code):
            start = self.p
            try:
                s = self.stmt()
            except Exception as ex:  # keep going with a marker
                lines.append(f"{start:04X}: !! {ex} at {self.p:04X}: {code[start:start+16].hex(' ')}")
                break
            lines.append(f"{start:04X}: {s}")
            if s == "EndOfScript":
                break
        return lines

    def u8(self):
        v = self.c[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from(">H", self.c, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        v = struct.unpack_from(">i", self.c, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from(">f", self.c, self.p)[0]
        self.p += 4
        return v

    def name(self):
        idx = self.i32()
        num = self.i32()
        return self.pk.name_at(idx, num)

    def params(self):
        out = []
        while True:
            if self.c[self.p] == 0x16:
                self.p += 1
                return out
            out.append(self.expr())

    def stmt(self):
        return self.expr()

    def expr(self) -> str:
        t = self.u8()
        if t == 0x00:
            return f"local:{self.ref(self.i32()).split('.')[-1]}"
        if t == 0x01:
            return f"{self.short(self.ref(self.i32()))}"
        if t == 0x02:
            return f"default.{self.short(self.ref(self.i32()))}"
        if t == 0x04:
            return f"return {self.expr()}"
        if t == 0x05:
            # 575: two bytes (unknown, value size) then the switched expression
            self.u8()
            size = self.u8()
            return f"switch<{size}> ({self.expr()})"
        if t == 0x06:
            return f"jump {self.u16():04X}"
        if t == 0x07:
            off = self.u16()
            return f"if !({self.expr()}) jump {off:04X}"
        if t == 0x08:
            return "stop"
        if t == 0x09:
            line = self.u16()
            dbg = self.u8()
            return f"assert(line {line}) {self.expr()}"
        if t == 0x0A:
            off = self.u16()
            if off == 0xFFFF:
                return "default:"
            return f"case {self.expr()}: (next {off:04X})"
        if t == 0x0B:
            return "nothing"
        if t == 0x0C:
            items = []
            while True:
                n = self.name()
                o = self.i32()
                items.append(f"{n}@{o:04X}")
                if n == "None":
                    break
            return f"labeltable {items}"
        if t == 0x0D:
            return f"goto {self.expr()}"
        if t == 0x0E:
            r = self.i32()
            return f"eat({self.expr()})"
        if t == 0x0F:
            a = self.expr()
            b = self.expr()
            return f"{a} = {b}"
        if t == 0x10:
            i = self.expr()
            a = self.expr()
            return f"{a}[{i}]"
        if t == 0x11:
            parts = [self.expr() for _ in range(4)]
            return f"new({parts})"
        if t in (0x12, 0x19):
            obj = self.expr()
            hdr = self.c[self.p:self.p + 4].hex()
            self.p += 4
            mem = self.expr()
            kw = "classctx" if t == 0x12 else ""
            return f"{kw}{obj}.{mem}"
        if t == 0x13:
            r = self.i32()
            return f"class<{self.short(self.ref(r))}>({self.expr()})"
        if t == 0x14:
            a = self.expr()
            b = self.expr()
            return f"{a} = {b}"
        if t == 0x15:
            return "endparmvalue"
        if t == 0x16:
            return ")"
        if t == 0x17:
            return "self"
        if t == 0x18:
            sk = self.u16()
            return f"{self.expr()}"
        if t == 0x1A:
            i = self.expr()
            a = self.expr()
            return f"{a}[{i}]"
        if t == 0x1B:
            n = self.name()
            return f"{n}({', '.join(self.params())})"
        if t == 0x1C:
            r = self.ref(self.i32())
            return f"{self.short(r)}({', '.join(self.params())})"
        if t == 0x1D:
            return str(self.i32())
        if t == 0x1E:
            return f"{self.f32():g}"
        if t == 0x1F:
            end = self.c.index(b"\0", self.p)
            s = self.c[self.p:end].decode("latin-1")
            self.p = end + 1
            return repr(s)
        if t == 0x20:
            return f"obj:{self.ref(self.i32())}"
        if t == 0x21:
            return f"'{self.name()}'"
        if t == 0x22:
            v = struct.unpack_from(">iii", self.c, self.p)
            self.p += 12
            return f"rot{v}"
        if t == 0x23:
            v = struct.unpack_from(">fff", self.c, self.p)
            self.p += 12
            return f"vect{tuple(round(x, 4) for x in v)}"
        if t == 0x24:
            return f"{self.u8()}b"
        if t == 0x25:
            return "0"
        if t == 0x26:
            return "1"
        if t == 0x27:
            return "true"
        if t == 0x28:
            return "false"
        if t == 0x29:
            return f"nativeparm:{self.ref(self.i32())}"
        if t == 0x2A:
            return "None"
        if t == 0x2C:
            return str(self.u8())
        if t == 0x2D:
            return self.expr()
        if t == 0x2E:
            r = self.i32()
            return f"{self.short(self.ref(r))}({self.expr()})"
        if t == 0x2F:
            e = self.expr()
            off = self.u16()
            return f"foreach {e} (end {off:04X})"
        if t == 0x30:
            return "iteratorpop"
        if t == 0x31:
            return "iteratornext"
        if t in (0x32, 0x33):
            r = self.i32()
            a = self.expr()
            b = self.expr()
            return f"({a} {'==' if t == 0x32 else '!='} {b})"
        if t == 0x34:
            out = []
            while True:
                ch = struct.unpack_from(">H", self.c, self.p)[0]
                self.p += 2
                if ch == 0:
                    break
                out.append(chr(ch))
            return repr("".join(out))
        if t == 0x35:
            prop = self.ref(self.i32())
            sref = self.ref(self.i32())
            b1 = self.u8()
            b2 = self.u8()
            inner = self.expr()
            return f"{inner}.{prop.split('.')[-1]}"
        if t == 0x36:
            return f"{self.expr()}.Length"
        if t == 0x37:
            n = self.name()
            return f"global.{n}({', '.join(self.params())})"
        if t == 0x38:
            k = self.u8()
            return f"cast{k}({self.expr()})"
        if t == 0x39:
            a = self.expr()
            b = self.expr()
            c = self.expr()
            return f"{a}.Insert({b},{c})"
        if t == 0x3A:
            return f"returnnothing<{self.short(self.ref(self.i32()))}>"
        if t in (0x3B, 0x3C):
            a = self.expr()
            b = self.expr()
            self.expect16()
            return f"({a} {'==' if t == 0x3B else '!='} {b})"
        if t in (0x3D, 0x3E):
            a = self.expr()
            b = self.expr()
            self.expect16()
            return f"({a} {'==' if t == 0x3D else '!='} {b})"
        if t == 0x3F:
            return "EmptyDelegate"
        if t == 0x40:
            a = self.expr()
            b = self.expr()
            c = self.expr()
            return f"{a}.Remove({b},{c})"
        if t == 0x42:
            local = self.u8()
            r = self.ref(self.i32())
            n = self.name()
            return f"delegate.{n}({', '.join(self.params())})"
        if t == 0x43:
            n = self.name()
            return f"delegateprop:{n}"
        if t == 0x44:
            a = self.expr()
            b = self.expr()
            return f"{a} = {b}"
        if t == 0x45:
            c = self.expr()
            s1 = self.u16()
            a = self.expr()
            s2 = self.u16()
            b = self.expr()
            return f"({c} ? {a} : {b})"
        if t == 0x46:
            a = self.expr()
            sk = self.u16()
            v = self.expr()
            self.expect16()
            return f"{a}.Find({v})"
        if t == 0x47:
            a = self.expr()
            sk = self.u16()
            n = self.expr()
            v = self.expr()
            self.expect16()
            return f"{a}.Find({n},{v})"
        if t == 0x48:
            return f"out:{self.ref(self.i32()).split('.')[-1]}"
        if t == 0x49:
            sz = self.u16()
            e = self.expr()
            if self.c[self.p] == 0x15:
                self.p += 1
            return f"defaultparm({e})"
        if t == 0x4A:
            return "<empty>"
        if t == 0x4B:
            return f"instancedelegate:{self.name()}"
        if t == 0x51:
            return f"iface({self.expr()})"
        if t == 0x52:
            r = self.i32()
            return f"ifacecast<{self.short(self.ref(r))}>({self.expr()})"
        if t == 0x53:
            return "EndOfScript"
        if t == 0x54:
            a = self.expr()
            sk = self.u16()
            b = self.expr()
            self.expect16()
            return f"{a}.Add({b})"
        if t == 0x55:
            a = self.expr()
            sk = self.u16()
            b = self.expr()
            self.expect16()
            return f"{a}.AddItem({b})"
        if t == 0x56:
            a = self.expr()
            sk = self.u16()
            b = self.expr()
            self.expect16()
            return f"{a}.RemoveItem({b})"
        if t == 0x57:
            a = self.expr()
            sk = self.u16()
            b = self.expr()
            c = self.expr()
            self.expect16()
            return f"{a}.InsertItem({b},{c})"
        if t == 0x58:
            a = self.expr()
            b = self.expr()
            has_idx = self.u8()
            c = self.expr()
            off = self.u16()
            return f"foreach {a}({b},{c}) (end {off:04X})"
        if 0x60 <= t <= 0x6F:
            idx = ((t & 0x0F) << 8) | self.u8()
            return f"{NATIVE_NAMES.get(idx, f'native{idx}')}({', '.join(self.params())})"
        if t >= 0x70:
            return f"{NATIVE_NAMES.get(t, f'native{t}')}({', '.join(self.params())})"
        raise ValueError(f"unknown token {t:02X}")

    def expect16(self):
        if self.p < len(self.c) and self.c[self.p] == 0x16:
            self.p += 1

    @staticmethod
    def short(path: str) -> str:
        parts = path.split(".")
        return ".".join(parts[-2:]) if len(parts) > 2 else path


def function_code(pk: Package, cls: str, fn: str):
    c = pk.find(cls, "Class") or pk.find(cls, "State")
    if not c:
        return None, None
    kids = {e["name"]: e for e in pk.children_of(c[0]["index"] + 1)}
    e = kids.get(fn)
    if e is None:
        return None, None
    b = pk.blob(e)
    n = struct.unpack_from(">i", b, 40)[0]
    return e, b[44:44 + n]


if __name__ == "__main__":
    pk = Package(sys.argv[1])
    e, code = function_code(pk, sys.argv[2], sys.argv[3])
    if code is None:
        print("not found")
        sys.exit(1)
    print(f"; {sys.argv[2]}.{sys.argv[3]} export {e['index'] + 1} bytecode {len(code)} bytes")
    print("; params/locals:", [x["name"] for x in pk.children_of(e["index"] + 1)])
    for line in Lister(pk).disasm(code):
        print(line)


def export_code(pk: Package, index1: int):
    e = pk.exports[index1 - 1]
    b = pk.blob(e)
    n = struct.unpack_from(">i", b, 40)[0]
    return e, b[44:44 + n]


def dump_export(pk: Package, index1: int):
    e, code = export_code(pk, index1)
    print(f"; {pk.path(index1)} export {index1} bytecode {len(code)} bytes")
    print("; params/locals:", [x["name"] for x in pk.children_of(index1)])
    for line in Lister(pk).disasm(code):
        print(line)
