"""Offline twin of the guest-memory script patches in src/gow2_script_patches.cpp.

It owns the patch rows, prints the C++ table and checks the rows against the
player's own decompressed packages.

python -I tools/script_patch.py --check <dir with Core.upk, Engine.upk, GameFramework.upk, GearGame.upk> [--players N]
python -I tools/script_patch.py --cpp
python -I tools/script_patch.py --cpp --write
"""
import argparse
import copy
import difflib
import re
import sys
import types
from dataclasses import dataclass
from pathlib import Path

PLAYERS = "PLAYERS"  # placeholder for the configured player count (C++ kPlayers)

@dataclass(frozen=True)
class Ref:
    path: str
    cls: str

@dataclass(frozen=True)
class Check:          # bytes that must already be there
    offset: int
    data: bytes

@dataclass(frozen=True)
class Operand:        # 4-byte object operand that must point at this object
    offset: int
    ref: Ref

@dataclass(frozen=True)
class ByteEdit:       # one byte: old -> new (new may be PLAYERS)
    offset: int
    old: int
    new: object

@dataclass(frozen=True)
class OperandEdit:    # 4-byte operand swapped from one object to another
    offset: int
    old: Ref
    new: Ref

@dataclass(frozen=True)
class Raw:            # tail piece: literal bytes
    data: bytes

@dataclass(frozen=True)
class Text:           # tail piece: ASCII string plus a terminating 0 byte (C++ AsciiZ)
    text: str

@dataclass(frozen=True)
class Grow:
    keep: int
    new_size: int
    tail: tuple       # of Raw | Text | Ref

@dataclass(frozen=True)
class ScriptRow:
    id: str
    summary: str
    function: Ref
    script_size: int
    checks: tuple = ()
    operands: tuple = ()
    byte_edits: tuple = ()
    operand_edits: tuple = ()
    grow: Grow | None = None

@dataclass(frozen=True)
class DefaultRow:
    id: str
    summary: str
    object: Ref
    offset: int       # native field offset in guest memory (C++ uses this)
    prop: str         # tagged property name in the package (the offline check uses this)
    old: int
    new: object

NUM_PLAYERS = Ref("Engine.GameInfo.NumPlayers", "IntProperty")
MAX_PLAYERS = Ref("Engine.GameInfo.MaxPlayers", "IntProperty")
MARCUS = Ref("GearGame.GearPawn_COGMarcus", "Class")
DOM = Ref("GearGame.GearPawn_COGDom", "Class")
COG_GEAR = Ref("GearGame.GearPawn_COGGear", "Class")
SUGGEST_RETURN = Ref("GearGame.GearGameSP_Base.SuggestPawnClassFor.ReturnValue", "ClassProperty")
GEAR_PAWN = Ref("GearGame.GearPawn", "Class")
DYNAMIC_LOAD_OBJECT = Ref("Core.Object.DynamicLoadObject", "Function")
CLASS_CLASS = Ref("Core.Class", "Class")

SUGGEST_CHECKS = (Check(0x61, b"\x04\x20"), Check(0x7E, b"\x04\x20"), Check(0x84, b"\x04\x20"),
                  Check(0x8A, b"\x04\x3A"), Check(0x90, b"\x53"))

def load_pawn_class(path):
    # ReturnValue = class<GearPawn>(DynamicLoadObject(path, class'Class', true))
    return (Raw(b"\x0F\x00"), SUGGEST_RETURN, Raw(b"\x13"), GEAR_PAWN, Raw(b"\x1C"),
            DYNAMIC_LOAD_OBJECT, Raw(b"\x1F"), Text(path), Raw(b"\x20"), CLASS_CLASS, Raw(b"\x27\x16"))

SUGGEST_TAIL = (load_pawn_class("GearGameContent.GearPawn_COGBenjaminCarmine")
                + (Raw(b"\x07\x01\x10\x72\x00"), SUGGEST_RETURN, Raw(b"\x2A\x16"))  # if !(ReturnValue == None) goto 0x0110
                + load_pawn_class("GearGameContent.GearPawn_COGRedShirt")
                + (Raw(b"\x04\x3A"), SUGGEST_RETURN, Raw(b"\x53")))

ROWS = {
    "S1-1": ScriptRow("S1-1", "return NumPlayers == 2  ->  return NumPlayers >= N",
        Ref("GearGame.GearGameSP_Base.AtCapacity", "Function"), 17,
        checks=(Check(0x00, b"\x04"), Check(0x02, b"\x01"), Check(0x07, b"\x2C"),
                Check(0x09, b"\x16\x04\x3A"), Check(0x10, b"\x53")),
        operands=(Operand(0x03, NUM_PLAYERS),
                  Operand(0x0C, Ref("GearGame.GearGameSP_Base.AtCapacity.ReturnValue", "BoolProperty"))),
        byte_edits=(ByteEdit(0x01, 0x9A, 0x99), ByteEdit(0x08, 0x02, PLAYERS))),
    "S1-2": ScriptRow("S1-2", "return GearPawn_COGGear  ->  Ben Carmine, else RedShirt, by DynamicLoadObject",
        Ref("GearGame.GearGameSP_Base.SuggestPawnClassFor", "Function"), 145,
        checks=SUGGEST_CHECKS,
        operands=(Operand(0x63, MARCUS), Operand(0x80, DOM), Operand(0x86, COG_GEAR), Operand(0x8C, SUGGEST_RETURN)),
        grow=Grow(0x84, 279, SUGGEST_TAIL)),
    "S1-3": ScriptRow("S1-3", "Coop.NumPublicConnections = 2  ->  N",
        Ref("GearGame.GearMenuGame.ChangeCoopInviteSetting", "Function"), 178,
        checks=(Check(0x47, b"\x0F\x19\x00"), Check(0x4E, b"\x00\x05\x00\x04\x01"), Check(0x57, b"\x2C"),
                Check(0x86, b"\x0F\x01"), Check(0x8C, b"\x92")),
        operands=(Operand(0x4A, Ref("GearGame.GearMenuGame.ChangeCoopInviteSetting.Coop", "ObjectProperty")),
                  Operand(0x53, Ref("Engine.OnlineGameSettings.NumPublicConnections", "IntProperty")),
                  Operand(0x88, MAX_PLAYERS)),
        byte_edits=(ByteEdit(0x58, 0x02, PLAYERS),)),
    "S1-4": DefaultRow("S1-4", "Default__GearCoopGameSettings.NumPublicConnections = 2  ->  N",
        Ref("GearGame.Default__GearCoopGameSettings", "GearCoopGameSettings"), 132, "NumPublicConnections", 2, PLAYERS),
    "S1-5": ScriptRow("S1-5", "MaxPlayers = 2  ->  N",
        Ref("GearGame.GearCampaignLobbyGame_Base.InitGame", "Function"), 169,
        checks=(Check(0x9E, b"\x0F\x01"), Check(0xA4, b"\x2C"), Check(0xA6, b"\x04\x0B\x53")),
        operands=(Operand(0xA0, MAX_PLAYERS),),
        byte_edits=(ByteEdit(0xA5, 0x02, PLAYERS),)),
    "S1-6": ScriptRow("S1-6", "MaxPlayers = 2  ->  N",
        Ref("GearGame.GearGameSP_Base.InitGame", "Function"), 202,
        checks=(Check(0xB4, b"\x0F\x01"), Check(0xBA, b"\x2C"), Check(0xBC, b"\x0F\x01"),
                Check(0xC2, b"\x1D\x00\x00\x1B\x58\x04\x0B\x53")),
        operands=(Operand(0xB6, MAX_PLAYERS), Operand(0xBE, Ref("Engine.GameInfo.MaxDynamicBandwidth", "IntProperty"))),
        byte_edits=(ByteEdit(0xBB, 0x02, PLAYERS),)),
    "D-1": ScriptRow("D-1", "Diagnostic: return GearPawn_COGGear  ->  return GearPawn_COGMarcus (players 3+ are Marcus clones)",
        Ref("GearGame.GearGameSP_Base.SuggestPawnClassFor", "Function"), 145,
        checks=SUGGEST_CHECKS,
        operands=(Operand(0x63, MARCUS), Operand(0x80, DOM), Operand(0x8C, SUGGEST_RETURN)),
        operand_edits=(OperandEdit(0x86, COG_GEAR, MARCUS),)),
}

def coop_stage1_rows(marcus_clones=False):
    # Rows of the "coop" group in C++ order: scripts first, then defaults.
    scripts = [ROWS["S1-1"], ROWS["D-1" if marcus_clones else "S1-2"], ROWS["S1-3"], ROWS["S1-5"], ROWS["S1-6"]]
    return scripts + [ROWS["S1-4"]]


@dataclass(frozen=True)
class ScriptArray:
    data: bytes
    num: int
    max: int


class PatchSkip(Exception):
    def __init__(self, row, reason):
        super().__init__(f"{row}: {reason}")
        self.row = row
        self.reason = reason


@dataclass
class ScriptPlan:
    row: ScriptRow
    handle: object
    new: bytes
    writes: int
    grown_size: int


@dataclass
class DefaultPlan:
    row: DefaultRow
    handle: object
    value: int
    writes: int


@dataclass
class Plan:
    entries: list


def _resolve_one(model, ref, row_id):
    hits = model.find(ref)
    if len(hits) != 1:
        raise PatchSkip(row_id, f"{ref.path} ({ref.cls}) found {len(hits)} times")
    return hits[0]


def _skip_old_tail(grown, scope, row_id, offset, size):
    if offset + size <= scope:
        return False
    if grown:  # the entry described the replaced tail
        return True
    raise PatchSkip(row_id, f"+0x{offset:04X} is outside the script")


def _assemble_tail(model, grow, row_id):
    tail = bytearray()
    for piece in grow.tail:
        if isinstance(piece, Raw):
            tail += piece.data
        elif isinstance(piece, Text):
            tail += piece.text.encode("ascii") + b"\0"
        else:
            tail += model.operand_value(_resolve_one(model, piece, row_id)).to_bytes(4, "big")
    if grow.keep + len(tail) != grow.new_size:
        raise PatchSkip(row_id, f"grown script would be {grow.keep + len(tail)} bytes, expected {grow.new_size}")
    return bytes(tail)


def _check_bytes(row, data, in_scope):
    for check in row.checks:
        if not in_scope(check.offset, len(check.data)):
            continue
        for i, expected in enumerate(check.data):
            found = data[check.offset + i]
            if found != expected:
                raise PatchSkip(row.id, f"+0x{check.offset + i:04X} expected {expected:02X}, found {found:02X}")


def _check_operands(model, row, data, in_scope):
    for operand in row.operands:
        if not in_scope(operand.offset, 4):
            continue
        expected = model.operand_value(_resolve_one(model, operand.ref, row.id))
        found = int.from_bytes(data[operand.offset:operand.offset + 4], "big")
        if found != expected:
            raise PatchSkip(row.id, f"+0x{operand.offset:04X} points at 0x{found:08X}, expected "
                            f"{operand.ref.path} ({operand.ref.cls}) at 0x{expected:08X}")


def _require_in_kept_prefix(row, offset, size):
    if offset + size > row.grow.keep:
        raise PatchSkip(row.id, f"+0x{offset:04X} is past the kept prefix +0x{row.grow.keep:04X}")


# A grow keeps only the prefix, so an edit after it would be dropped without a word.
def _check_edits_in_kept_prefix(row):
    if row.grow is None:
        return
    for edit in row.byte_edits:
        _require_in_kept_prefix(row, edit.offset, 1)
    for edit in row.operand_edits:
        _require_in_kept_prefix(row, edit.offset, 4)


def _plan_byte_edits(row, data, new_data, players, in_scope):
    writes = 0
    for edit in row.byte_edits:
        if not in_scope(edit.offset, 1):
            continue
        value = players if edit.new == PLAYERS else edit.new
        if not 0 <= value <= 0xFF:
            raise PatchSkip(row.id, f"value {value} out of range")
        found = data[edit.offset]
        if found == edit.old:
            new_data[edit.offset] = value
            writes += 1
        elif found != value:
            raise PatchSkip(row.id, f"+0x{edit.offset:04X} expected {edit.old:02X} or {value:02X}, found {found:02X}")
    return writes


def _plan_operand_edits(model, row, data, new_data, in_scope):
    writes = 0
    for edit in row.operand_edits:
        if not in_scope(edit.offset, 4):
            continue
        old_value = model.operand_value(_resolve_one(model, edit.old, row.id))
        new_value = model.operand_value(_resolve_one(model, edit.new, row.id))
        found = int.from_bytes(data[edit.offset:edit.offset + 4], "big")
        if found == old_value:
            new_data[edit.offset:edit.offset + 4] = new_value.to_bytes(4, "big")
            writes += 1
        elif found != new_value:
            raise PatchSkip(row.id, f"+0x{edit.offset:04X} points at 0x{found:08X}, expected "
                            f"{edit.old.path} ({edit.old.cls})")
    return writes


# The new script bytes, or None when the row grows nothing or the tail is already there.
def _plan_grow(model, row, script, grown, new_data):
    if row.grow is None:
        return None
    tail = _assemble_tail(model, row.grow, row.id)
    if grown:
        if script.data[row.grow.keep:] != tail:
            raise PatchSkip(row.id, f"Script.Num is already {script.num} but the tail differs")
        return None
    if script.max != script.num:
        raise PatchSkip(row.id, f"Script.Max is {script.max}, Num {script.num}")
    return bytes(new_data[:row.grow.keep]) + tail


def _plan_script(model, row, players):
    handle = _resolve_one(model, row.function, row.id)
    script = model.script(handle)
    grown = row.grow is not None and script.num == row.grow.new_size
    if not grown and script.num != row.script_size:
        raise PatchSkip(row.id, f"Script.Num is {script.num}, expected {row.script_size}")
    scope = row.grow.keep if grown else script.num

    def in_scope(offset, size):
        return not _skip_old_tail(grown, scope, row.id, offset, size)

    data = script.data
    _check_bytes(row, data, in_scope)
    _check_operands(model, row, data, in_scope)
    _check_edits_in_kept_prefix(row)
    new_data = bytearray(data)
    writes = _plan_byte_edits(row, data, new_data, players, in_scope)
    writes += _plan_operand_edits(model, row, data, new_data, in_scope)
    grown_script = _plan_grow(model, row, script, grown, new_data)
    if grown_script is not None:
        return ScriptPlan(row, handle, bytes(grown_script), writes, row.grow.new_size)
    return ScriptPlan(row, handle, bytes(new_data), writes, 0)


def _plan_default(model, row, players):
    handle = _resolve_one(model, row.object, row.id)
    value = players if row.new == PLAYERS else row.new
    if value < 0 or value > 0xFFFFFFFF:
        raise PatchSkip(row.id, f"value {value} out of range")
    found = model.read_int(handle, row)
    writes = 0
    if found == row.old:
        writes = 1
    elif found != value:
        raise PatchSkip(row.id, f"+0x{row.offset:04X} holds {found}, expected {row.old}")
    return DefaultPlan(row, handle, value, writes)


def plan_group(model, rows, players):
    entries = []
    for row in rows:
        if isinstance(row, ScriptRow):
            entries.append(_plan_script(model, row, players))
        else:
            entries.append(_plan_default(model, row, players))
    return Plan(entries)


def apply_plan(model, plan):
    out = {}
    for entry in plan.entries:
        if isinstance(entry, ScriptPlan):
            out[entry.handle] = entry.new
        else:
            out[entry.handle] = entry.value
    return out


def group_lines(plan):
    lines = []
    for entry in plan.entries:
        row = entry.row
        if isinstance(entry, ScriptPlan):
            head = f"{row.id} {row.function.path}: "
            if entry.grown_size:
                line = head + f"script grown to {entry.grown_size} bytes"
                if entry.writes > 0:
                    line += f", with {entry.writes} write(s)"
                lines.append(line)
            elif entry.writes == 0:
                lines.append(head + "already applied")
            else:
                lines.append(head + f"{entry.writes} write(s)")
        else:
            head = f"{row.id} {row.object.path}: "
            if entry.writes == 0:
                lines.append(head + "already applied")
            else:
                lines.append(head + f"{entry.writes} write(s)")
    return lines


ROW_ORDER = ("S1-1", "S1-2", "S1-3", "S1-4", "S1-5", "S1-6", "D-1")


def _ref_cpp(ref):
    return f'{{"{ref.path}", "{ref.cls}"}}'


def _bytes_cpp(data):
    return ", ".join(f"0x{b:02X}" for b in data)


def _tail_lines(tail):
    lines = []
    pending = bytearray()
    for piece in tail:
        if isinstance(piece, Raw):
            pending += piece.data
            continue
        if pending:  # consecutive Raw pieces merge into one line
            lines.append(f"              {{.bytes = {{{_bytes_cpp(pending)}}}}},")
            pending.clear()
        if isinstance(piece, Text):  # a Text piece ends the merge
            lines.append(f'              {{.bytes = AsciiZ("{piece.text}")}},')
        else:
            lines.append(f"              {{.object = {_ref_cpp(piece)}}},")
    if pending:
        lines.append(f"              {{.bytes = {{{_bytes_cpp(pending)}}}}},")
    return lines


def _script_lines(row):
    name = "Row" + row.id.replace("-", "_")
    lines = [f"// {row.summary}", f"ScriptRow {name}() {{", "  return {"]
    lines.append(f'      .id = "{row.id}",')
    lines.append(f"      .function = {_ref_cpp(row.function)},")
    lines.append(f"      .script_size = {row.script_size},")
    if row.checks:
        lines.append("      .checks = {")
        for check in row.checks:
            lines.append(f"          {{0x{check.offset:04X}, {{{_bytes_cpp(check.data)}}}}},")
        lines.append("      },")
    if row.operands:
        lines.append("      .operands = {")
        for operand in row.operands:
            lines.append(f"          {{0x{operand.offset:04X}, {_ref_cpp(operand.ref)}}},")
        lines.append("      },")
    if row.byte_edits:
        lines.append("      .byte_edits = {")
        for edit in row.byte_edits:
            new = "kPlayers" if edit.new == PLAYERS else f"0x{edit.new:02X}"
            lines.append(f"          {{0x{edit.offset:04X}, 0x{edit.old:02X}, {new}}},")
        lines.append("      },")
    if row.operand_edits:
        lines.append("      .operand_edits = {")
        for edit in row.operand_edits:
            lines.append(f"          {{0x{edit.offset:04X}, {_ref_cpp(edit.old)}, {_ref_cpp(edit.new)}}},")
        lines.append("      },")
    if row.grow is not None:
        lines.append("      .grow = GrowEdit{")
        lines.append(f"          .keep = 0x{row.grow.keep:04X},")
        lines.append(f"          .new_size = {row.grow.new_size},")
        lines.append("          .tail = {")
        lines.extend(_tail_lines(row.grow.tail))
        lines.append("          },")
        lines.append("      },")
    lines.append("  };")
    lines.append("}")
    return lines


def _default_lines(row):
    name = "Row" + row.id.replace("-", "_")
    new = "kPlayers" if row.new == PLAYERS else f"{row.new}"
    return [
        f"// {row.summary}",
        f"DefaultRow {name}() {{",
        "  return {",
        f'      .id = "{row.id}",',
        f"      .object = {_ref_cpp(row.object)},",
        f"      .offset = {row.offset},",
        f"      .old_value = {row.old},",
        f"      .new_value = {new},",
        "  };",
        "}",
    ]


def check_table():
    for row in ROWS.values():
        if not isinstance(row, ScriptRow):
            continue
        try:
            _check_edits_in_kept_prefix(row)
        except PatchSkip as e:
            raise ValueError(f"{e.row}: {e.reason}") from None


def render_cpp():
    check_table()
    lines = [
        "// gow2 - ReXGlue Recompiled Project",
        "//",
        "// The Stage 1 co-op rows of docs/COOP_N_PLAYERS.md section 4. Printed by",
        "// `python -I tools/script_patch.py --cpp`; edit the rows there, not here. Check them against your",
        "// own decompressed packages with `python -I tools/script_patch.py --check <folder>`.",
        "",
        '#include "gow2_script_patches.h"',
        "",
        "namespace rex::glue::script_patch {",
        "",
        "namespace {",
        "",
    ]
    for rid in ROW_ORDER:
        row = ROWS[rid]
        if isinstance(row, ScriptRow):
            lines.extend(_script_lines(row))
        else:
            lines.extend(_default_lines(row))
        lines.append("")
    lines.extend([
        "}  // namespace",
        "",
        "PatchGroup CoopStage1Group(bool marcus_clones) {",
        "  return {",
        '      .name = "coop",',
        "      .scripts = {RowS1_1(), marcus_clones ? RowD_1() : RowS1_2(), RowS1_3(), RowS1_5(), RowS1_6()},",
        "      .defaults = {RowS1_4()},",
        "  };",
        "}",
        "",
        "}  // namespace rex::glue::script_patch",
    ])
    return "\n".join(lines) + "\n"


def _ue3_modules():
    sys.path.insert(0, str(Path(__file__).resolve().parent / "ue3"))
    import ue3script
    import ue3pkg
    return ue3script, ue3pkg


class PackageModel:
    def __init__(self, pkg_dir):
        ue3script, ue3pkg = _ue3_modules()
        self._ue3pkg = ue3pkg
        self.world = ue3script.World(pkg_dir)
        if "GearGame" not in self.world.pkgs:
            raise FileNotFoundError(f"GearGame.upk not found in {pkg_dir}")
        self.pkg = self.world.pkgs["GearGame"]
        self.pkg_name = "GearGame"

    def _full_path(self, index):
        if index > 0:
            return f"{self.pkg_name}." + self.pkg.obj_path(index)
        return self.pkg.obj_path(index)  # imports already start with their package name

    def find(self, ref):
        hits = [i + 1 for i in range(len(self.pkg.exports))
                if self._full_path(i + 1) == ref.path and self.pkg.obj_class(i + 1) == ref.cls]
        if hits:
            return hits
        return [-(j + 1) for j in range(len(self.pkg.imports))
                if self._full_path(-(j + 1)) == ref.path and self.pkg.obj_class(-(j + 1)) == ref.cls]

    def operand_value(self, handle):
        return handle & 0xFFFFFFFF  # the disk bytecode stores the package reference big-endian

    def script(self, handle):
        f = self.world.read_function(self.pkg, handle)
        data = bytes(self.pkg.b[f.code_start:f.code_end])
        if len(data) != f.script_size:
            raise PatchSkip("-", "disk script length differs from ScriptSize")
        return ScriptArray(data, f.script_size, f.script_size)

    def read_int(self, handle, row):
        for name, typ, _arr, value in self._ue3pkg.read_tagged(self.pkg, handle):
            if name == row.prop and typ == "IntProperty":
                return value
        raise PatchSkip(row.id, f"{row.prop} (IntProperty) not found")


def _decode_spliced(ue3script, model, handle, new_script):
    before = model.world.read_function(model.pkg, handle)
    patched = bytearray(model.pkg.b)
    patched[before.code_start:before.code_end] = new_script
    q = copy.copy(model.pkg)
    q.b = bytes(patched)
    after = model.world.read_function(q, handle)
    return ue3script.decompile(model.world, after)


def _decode_standalone(ue3script, model, handle, path, new_script):
    f = types.SimpleNamespace(pkg=model.pkg, index=handle, path=path,
                              code_start=0, code_end=len(new_script))
    d = ue3script.Decoder.__new__(ue3script.Decoder)
    d.w = model.world
    d.f = f
    d.p = model.pkg
    d.b = new_script
    d.pos = 0
    d.base = 0
    lines = []
    while d.pos < len(new_script):
        at = d.pos - d.base
        text = d.expr()
        lines.append(f"  {at:04X}: {text}")
        if text == "<EndOfScript>":
            break
    return lines, d.pos == len(new_script), d.pos - d.base


def _show_diff(old_lines, new_lines):
    matcher = difflib.SequenceMatcher(None, old_lines, new_lines)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            continue
        for line in old_lines[i1:i2]:
            print(f"   - {line}")
        for line in new_lines[j1:j2]:
            print(f"   + {line}")


def _kept_targets(old_lines, keep):
    targets = []
    for line in old_lines:
        if int(line.strip().split(":")[0], 16) >= keep:
            continue
        targets += [int(m, 16) for m in re.findall(r"goto 0x([0-9A-Fa-f]+)", line)]
        targets += [int(m, 16) for m in re.findall(r"\(next 0x([0-9A-Fa-f]+)\)", line)]
    return targets


def _check_script_row(ue3script, model, entry, new_script):
    row = entry.row
    print(f"== {row.id} {row.function.path} ScriptSize={row.script_size}")
    try:
        before = model.world.read_function(model.pkg, entry.handle)
        old_lines, _, _ = ue3script.decompile(model.world, before)
    except Exception as e:
        print(f"   original decode failed: {e}")
        return False
    try:
        if len(new_script) == before.code_end - before.code_start:
            new_lines, ok, consumed = _decode_spliced(ue3script, model, entry.handle, new_script)
        else:  # grown scripts no longer fit in the package
            new_lines, ok, consumed = _decode_standalone(ue3script, model, entry.handle, before.path, new_script)
    except Exception as e:
        print(f"   patched decode failed: {e}")
        return False
    _show_diff(old_lines, new_lines)
    good = ok and consumed == len(new_script)
    print(f"   decodes to its end: {good} at 0x{consumed:X} of 0x{len(new_script):X}")
    if row.grow is not None:
        targets = _kept_targets(old_lines, row.grow.keep)
        print(f"   jumps in the kept prefix: {[f'0x{t:X}' for t in sorted(targets)]}")
        bad = [t for t in targets if t > row.grow.keep]
        if bad:
            print(f"   jumps into the replaced tail: {[f'0x{t:X}' for t in sorted(bad)]}")
            return False
    return good


def _check_default_row(model, entry):
    row = entry.row
    value = model.read_int(entry.handle, row)
    print(f"== {row.id} {row.object.path}.{row.prop} = {value}")
    if value != row.old:
        print(f"   expected the old value {row.old}")
        return False
    return True


GROUP_TITLES = ("coop", "coop with D-1 (gow2_coop_marcus_clones), its other rows as above")


def check_package(pkg_dir, players):
    ue3script, _ = _ue3_modules()
    print(f"== script_patch --check {pkg_dir} players={players}")
    try:
        model = PackageModel(pkg_dir)
    except OSError as e:
        print(f"error: cannot load packages from {pkg_dir}: {e}", file=sys.stderr)
        return 1
    except Exception as e:  # corrupt package header: name the failure
        print(f"error: cannot load packages from {pkg_dir}: {type(e).__name__}({e})", file=sys.stderr)
        return 1
    try:
        plans = [plan_group(model, coop_stage1_rows(False), players),
                 plan_group(model, coop_stage1_rows(True), players)]
    except PatchSkip as e:
        print(f"SKIP {e.row}: {e.reason}")
        return 1
    failed = False
    checked = set()
    for title, plan in zip(GROUP_TITLES, plans):
        print(f"== group {title}")
        patched = apply_plan(model, plan)
        for entry in plan.entries:
            if isinstance(entry, ScriptPlan) and entry.row.id not in checked:
                checked.add(entry.row.id)
                if not _check_script_row(ue3script, model, entry, patched[entry.handle]):
                    failed = True
    for entry in plans[0].entries:
        if isinstance(entry, DefaultPlan):
            if not _check_default_row(model, entry):
                failed = True
    print("== C++ table")
    print(render_cpp(), end="")
    return 1 if failed else 0


def _write_stdout(text):
    # Binary write so piped output keeps LF endings on every platform.
    buf = getattr(sys.stdout, "buffer", None)
    if buf is None:
        sys.stdout.write(text)
    else:
        buf.write(text.encode("utf-8"))
        buf.flush()


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Offline twin of the guest-memory script patches.")
    p.add_argument("--check", metavar="PKGDIR", default=None,
                   help="check the rows against decompressed packages in PKGDIR")
    p.add_argument("--cpp", action="store_true", help="print the C++ table file")
    p.add_argument("--write", action="store_true", help="write the C++ table file (with --cpp)")
    p.add_argument("--players", type=int, default=4, help="player count 3..255 (default 4)")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if not 3 <= args.players <= 255:
        print(f"error: --players must be 3..255, got {args.players}", file=sys.stderr)
        return 2
    if args.write and not args.cpp:
        print("error: --write needs --cpp", file=sys.stderr)
        return 2
    if args.cpp:
        text = render_cpp()
        if args.write:
            out = Path(__file__).resolve().parent.parent / "src" / "gow2_script_patch_table.cpp"
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print(str(out))
        else:
            _write_stdout(text)
        return 0
    if args.check is None:
        print("error: need --check PKGDIR or --cpp", file=sys.stderr)
        return 2
    return check_package(args.check, args.players)


if __name__ == "__main__":
    sys.exit(main())
