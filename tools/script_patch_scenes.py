"""Offline twin of the coop-scenes script patches in src/gow2_coop_scenes.cpp.

python -I tools/script_patch_scenes.py --check <dir with Core.upk, Engine.upk, GameFramework.upk, GearGame.upk> [--players N]
python -I tools/script_patch_scenes.py --cpp
python -I tools/script_patch_scenes.py --cpp --write
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import script_patch as sp


class JumpTarget:
    def __init__(self, label):
        self.label = label


class LabelMarker:
    def __init__(self, label):
        self.label = label


class RelativeSkip:
    def __init__(self, label, start_label):
        self.label = label
        self.start_label = start_label


def piece_size(piece):
    if isinstance(piece, sp.Raw):
        return len(piece.data)
    if isinstance(piece, sp.Ref):
        return 4
    if isinstance(piece, (JumpTarget, RelativeSkip)):
        return 2
    if isinstance(piece, LabelMarker):
        return 0
    raise TypeError(piece)


def pieces(*parts):
    out = []
    for part in parts:
        if isinstance(part, (bytes, bytearray)):
            out.append(sp.Raw(bytes(part)))
        elif isinstance(part, int):
            out.append(sp.Raw(bytes([part])))
        elif isinstance(part, list):
            out.extend(pieces(*part))
        else:
            out.append(part)
    return out


def pieces_size(expr):
    return sum(piece_size(p) for p in expr)


def u16(value):
    return sp.Raw(struct.pack(">H", value))


def int_const(value):
    return pieces(0x1D, struct.pack(">i", value))


def float_const(value):
    return pieces(0x1E, struct.pack(">f", value))


def local(path, cls="ObjectProperty"):
    return pieces(0x00, sp.Ref(path, cls))


def inst(path, cls="ObjectProperty"):
    return pieces(0x01, sp.Ref(path, cls))


def bool_var(expr):
    return pieces(0x2D, expr)


def context(obj, member, result_size):
    return pieces(0x19, obj, u16(pieces_size(member)), u16(result_size), member)


def dyncast(cls_ref, expr):
    return pieces(0x2E, cls_ref, expr)


def class_const(cls_ref):
    return pieces(0x20, cls_ref)


NONE = pieces(0x2A)
SELF = pieces(0x17)
TRUE = pieces(0x27)
FALSE = pieces(0x28)
EMPTY = pieces(0x4A)


def operator_expr(token, *args):
    return pieces(token, *args, 0x16)


def logical_and(first, second):
    return pieces(0x82, first, 0x18, u16(pieces_size(second) + 1), second, 0x16)


def logical_or(first, second):
    return pieces(0x84, first, 0x18, u16(pieces_size(second) + 1), second, 0x16)


def logical_and_all(*exprs):
    acc = exprs[0]
    for expr in exprs[1:]:
        acc = logical_and(acc, expr)
    return acc


def logical_not(expr):
    return operator_expr(0x81, expr)


def eq_object(left, right):
    return operator_expr(0x72, left, right)


def ne_object(left, right):
    return operator_expr(0x77, left, right)


def eq_bool(left, right):
    return operator_expr(0xF2, left, right)


def gt_float(left, right):
    return operator_expr(0xB1, left, right)


def vsize(expr):
    return operator_expr(0xE1, expr)


def vector_sub(left, right):
    return operator_expr(0xD8, left, right)


def array_find(arr, value):
    return pieces(0x46, arr, u16(pieces_size(value)), value)


def ne_int(left, right):
    return operator_expr(0x9B, left, right)


def conditional(cond, when_true, when_false):
    return pieces(0x45, cond, u16(pieces_size(when_true)), when_true,
                 u16(pieces_size(when_false)), when_false)


def final_call(func_path, *args):
    return pieces(0x1C, sp.Ref(func_path, "Function"), *args, 0x16)


def native_call(high, low, *args):
    return pieces(high, low, *args, 0x16)


def let(lhs, rhs):
    return pieces(0x0F, lhs, rhs)


def if_not(label_name, cond):
    return pieces(0x07, JumpTarget(label_name), cond)


def goto_offset(offset):
    return pieces(0x06, u16(offset))


def foreach_all_controllers(cls_ref, var, pop_label, start_label):
    inner = pieces(0x1C, sp.Ref("Engine.WorldInfo.AllControllers", "Function"),
                   class_const(cls_ref), var, 0x16)
    return pieces(0x2F, 0x19, inst("Engine.Actor.WorldInfo"),
                  RelativeSkip(pop_label, start_label), u16(0),
                  LabelMarker(start_label), inner, JumpTarget(pop_label))


iterator_next = pieces(0x31)
iterator_pop = pieces(0x30)
return_nothing = pieces(0x04, 0x0B)
end_of_script = pieces(0x53)


def label(name):
    return [LabelMarker(name)]


def assemble(stmts, base):
    flat = []
    for stmt in stmts:
        flat.extend(stmt)
    pos = base
    labels = {}
    for piece in flat:
        if isinstance(piece, LabelMarker):
            labels[piece.label] = pos
        pos += piece_size(piece)
    out = []
    pos = base
    for piece in flat:
        if isinstance(piece, LabelMarker):
            continue
        if isinstance(piece, JumpTarget):
            out.append(u16(labels[piece.label]))
        elif isinstance(piece, RelativeSkip):
            out.append(u16(labels[piece.label] - labels[piece.start_label]))
        else:
            out.append(piece)
        pos += piece_size(piece)
    merged = []
    for piece in out:
        if isinstance(piece, sp.Raw) and merged and isinstance(merged[-1], sp.Raw):
            merged[-1] = sp.Raw(merged[-1].data + piece.data)
        else:
            merged.append(piece)
    return merged, pos


MARCUS_CLASS = sp.Ref("GearGame.GearPawn_COGMarcus", "Class")
DOM_CLASS = sp.Ref("GearGame.GearPawn_COGDom", "Class")
GEAR_PC_CLASS = sp.Ref("GearGame.GearPC", "Class")
GEAR_PAWN_CLASS = sp.Ref("GearGame.GearPawn", "Class")
GEAR_GAME_CLASS = sp.Ref("GearGame.GearGame", "Class")
GEAR_AI_DOM_CLASS = sp.Ref("GearGame.GearAI_Dom", "Class")
DEFAULT_PAWN_CLASS = sp.Ref("GearGame.GearPC.DefaultPawnClass", "ClassProperty")


def default_pawn_class_of(obj):
    return context(obj, pieces(0x01, DEFAULT_PAWN_CLASS), 4)


def self_default_pawn_class():
    return pieces(0x01, DEFAULT_PAWN_CLASS)


def is_extra(default_pawn):
    return logical_and_all(ne_object(default_pawn, NONE),
                           ne_object(default_pawn, class_const(MARCUS_CLASS)),
                           ne_object(default_pawn, class_const(DOM_CLASS)))


def is_hero(default_pawn):
    return logical_or(eq_object(default_pawn, class_const(MARCUS_CLASS)),
                      eq_object(default_pawn, class_const(DOM_CLASS)))


def pawn_of(obj):
    return context(obj, inst("Engine.Controller.Pawn"), 4)


def game_flag(name):
    game = context(inst("Engine.Actor.WorldInfo"), inst("Engine.WorldInfo.Game"), 4)
    return context(dyncast(GEAR_GAME_CLASS, game),
                   bool_var(inst("GearGame.GearGame." + name, "BoolProperty")), 4)


def in_coop_split():
    return logical_or(game_flag("bInCoopSplit"), game_flag("bInSoloSplit"))


def destroy(obj):
    return context(obj, native_call(0x61, 0x17), 4)


def controller_bool(pc, path):
    return context(pc, bool_var(inst(path, "BoolProperty")), 4)


def row_sc1_set_cinematic_mode():
    func = "GearGame.GearPC.SetCinematicMode"
    ai = local(func + ".AI")
    args = [bool_var(local(func + "." + name, "BoolProperty")) for name in
            ("bInCinematicMode", "bHidePlayer", "bAffectsHUD", "bAffectsMovement",
             "bAffectsTurning", "bAffectsButtons")]
    stmts = [
        if_not("RET", eq_object(self_default_pawn_class(), class_const(MARCUS_CLASS))),
        foreach_all_controllers(GEAR_PC_CLASS, ai, "POP", "IN"),
        if_not("NEXT", is_extra(default_pawn_class_of(ai))),
        context(ai, final_call(func, *args), 0),
        label("NEXT"), iterator_next,
        label("POP"), iterator_pop,
        label("RET"), return_nothing,
        end_of_script,
    ]
    keep = 0x01F7
    tail, end = assemble(stmts, keep)
    return sp.ScriptRow("SC-1", "mirror Marcus's cinematic mode to every extra player",
                        sp.Ref(func, "Function"), 506,
                        checks=(sp.Check(0x0000, bytes.fromhex("07001296383a01")),
                                sp.Check(0x0012, b"\x1C"),
                                sp.Check(0x01F7, b"\x04\x0B\x53")),
                        operands=(sp.Operand(0x0013, sp.Ref("Engine.PlayerController.SetCinematicMode", "Function")),
                                  sp.Operand(0x005F, sp.Ref(func + ".AI", "ObjectProperty"))),
                        grow=sp.Grow(keep, end, tuple(tail)))


def row_sc2_restart_player():
    func = "GearGame.GearGameSP_Base.RestartPlayer"
    human = local(func + ".HumanPlayer")
    candidate = local(func + ".C")
    other = local(func + ".OtherPC")
    coop_pawn = local(func + ".CoopPawn")
    stmts = [
        if_not("RET", logical_and(ne_object(human, NONE),
                                  is_extra(default_pawn_class_of(human)))),
        foreach_all_controllers(GEAR_AI_DOM_CLASS, candidate, "POP1", "IN1"),
        # exact class: AllControllers also yields subclasses
        if_not("NEXT1", logical_and_all(
            eq_object(context(candidate, inst("Core.Object.Class", "ClassProperty"), 4),
                      class_const(GEAR_AI_DOM_CLASS)),
            ne_object(pawn_of(candidate), NONE),
            eq_object(context(pawn_of(candidate), inst("Core.Object.Class", "ClassProperty"), 4),
                      default_pawn_class_of(human)))),
        let(coop_pawn, pawn_of(candidate)),
        destroy(coop_pawn),
        if_not("NEXT1", logical_not(controller_bool(candidate, "Engine.Actor.bDeleteMe"))),
        destroy(candidate),
        label("NEXT1"), iterator_next,
        label("POP1"), iterator_pop,
        if_not("RET", ne_object(pawn_of(human), NONE)),
        foreach_all_controllers(GEAR_PC_CLASS, other, "POP2", "IN2"),
        if_not("NEXT2", logical_and(
            eq_object(default_pawn_class_of(other), class_const(MARCUS_CLASS)),
            controller_bool(other, "Engine.PlayerController.bCinematicMode"))),
        context(human, final_call("GearGame.GearPC.SetCinematicMode",
                                  TRUE,
                                  context(pawn_of(other), bool_var(inst("Engine.Actor.bHidden", "BoolProperty")), 4),
                                  FALSE,
                                  controller_bool(other, "Engine.PlayerController.bCinemaDisableInputMove"),
                                  controller_bool(other, "Engine.PlayerController.bCinemaDisableInputLook"),
                                  controller_bool(other, "GearGame.GearPC.bCinemaDisableInputButtons")), 0),
        label("NEXT2"), iterator_next,
        label("POP2"), iterator_pop,
        label("RET"), return_nothing,
        end_of_script,
    ]
    keep = 0x0788
    tail, end = assemble(stmts, keep)
    return sp.ScriptRow("SC-2", "extras: remove orphaned bodies of their class, then take Marcus's cinematic mode",
                        sp.Ref(func, "Function"), 1931,
                        checks=(sp.Check(0x077A, bytes.fromhex("0607881c")),
                                sp.Check(0x0782, b"\x00"),
                                sp.Check(0x0787, bytes.fromhex("16040b53"))),
                        operands=(sp.Operand(0x077E, sp.Ref("GearGame.GearGame.RestartPlayer", "Function")),
                                  sp.Operand(0x0783, sp.Ref(func + ".NewPlayer", "ObjectProperty"))),
                        grow=sp.Grow(keep, end, tuple(tail)))


def row_sc3_on_checkpoint():
    func = "GearGame.GearPC.OnCheckpoint"
    action = local(func + ".Action")
    teleport_to = local(func + ".TeleportToPC")
    pc = local(func + ".PC")
    teleport_classes = context(action,
                               inst("GearGame.SeqAct_Checkpoint.TeleportClasses", "ArrayProperty"), 0)
    stmts = [
        if_not("END", logical_and(ne_object(pawn_of(teleport_to), NONE),
                                  ne_int(array_find(teleport_classes, class_const(MARCUS_CLASS)),
                                         int_const(-1)))),
        foreach_all_controllers(GEAR_PC_CLASS, pc, "POP", "IN"),
        if_not("NEXT", logical_and_all(
            ne_object(pc, teleport_to),
            is_extra(default_pawn_class_of(pc)),
            ne_object(dyncast(GEAR_PAWN_CLASS, pawn_of(pc)), NONE),
            context(action, final_call("GearGame.SeqAct_Checkpoint.ShouldTeleport",
                                       pawn_of(pc),
                                       context(pawn_of(teleport_to),
                                               inst("Engine.Actor.Location", "StructProperty"), 12)), 4))),
        context(teleport_to, final_call("GearGame.GearPC.FindAvailableTeleportSpot",
                                        pawn_of(pc), EMPTY, EMPTY), 4),
        label("NEXT"), iterator_next,
        label("POP"), iterator_pop,
        label("END"), goto_offset(0x02D2),
        end_of_script,
    ]
    keep = 0x02D4
    tail, end = assemble(stmts, keep)
    return sp.ScriptRow("SC-3", "after the catch-up loop, pull far extras too where TeleportClasses lists Marcus",
                        sp.Ref(func, "Function"), 725,
                        checks=(sp.Check(0x0279, bytes.fromhex("0702a3811900")),
                                sp.Check(0x02A3, bytes.fromhex("31300602")),
                                sp.Check(0x02C8, bytes.fromhex("1b")),
                                sp.Check(0x02D2, bytes.fromhex("040b53"))),
                        operands=(sp.Operand(0x027F, sp.Ref(func + ".TeleportToPC", "ObjectProperty")),),
                        byte_edits=(sp.ByteEdit(0x02A7, 0xD2, 0xD4),),
                        grow=sp.Grow(keep, end, tuple(tail)))


def row_sc4_on_teleport():
    func = "GearGame.GearPawn.OnTeleport"
    pc = local(func + ".PC")
    action = local(func + ".Action")
    hero = dyncast(GEAR_PC_CLASS, inst("Engine.Pawn.Controller"))
    follow = eq_bool(eq_object(default_pawn_class_of(hero), class_const(DOM_CLASS)),
                     logical_and(in_coop_split(), ne_object(action, NONE)))
    far = gt_float(vsize(vector_sub(context(pawn_of(pc),
                                              inst("Engine.Actor.Location", "StructProperty"), 12),
                                    inst("Engine.Actor.Location", "StructProperty"))),
                   float_const(1500.0))
    stmts = [
        if_not("RET", logical_and(ne_object(pc, NONE),
                                  is_hero(default_pawn_class_of(pc)))),
        let(action, NONE),
        foreach_all_controllers(GEAR_PC_CLASS, pc, "POP", "IN"),
        if_not("NEXT", is_extra(default_pawn_class_of(pc))),
        if_not("FLIP", logical_and_all(
            ne_object(dyncast(GEAR_PAWN_CLASS, pawn_of(pc)), NONE), far, follow)),
        context(hero, final_call("GearGame.GearPC.FindAvailableTeleportSpot",
                                 pawn_of(pc), EMPTY, EMPTY), 4),
        # Action reused as a flip-flop to split extras between the heroes
        label("FLIP"), let(action, conditional(eq_object(action, NONE), SELF, NONE)),
        label("NEXT"), iterator_next,
        label("POP"), iterator_pop,
        label("RET"), return_nothing,
        end_of_script,
    ]
    keep = 0x00B5
    tail, end = assemble(stmts, keep)
    return sp.ScriptRow("SC-4", "after a hero's Kismet teleport, place his extras (>1500 uu away) next to him",
                        sp.Ref(func, "Function"), 184,
                        checks=(sp.Check(0x000B, bytes.fromhex("0f00")),
                                sp.Check(0x0011, bytes.fromhex("2e")),
                                sp.Check(0x00AA, b"\x1C"),
                                sp.Check(0x00AF, b"\x00"),
                                sp.Check(0x00B4, bytes.fromhex("16040b53"))),
                        operands=(sp.Operand(0x000D, sp.Ref(func + ".PC", "ObjectProperty")),
                                  sp.Operand(0x00AB, sp.Ref("Engine.Pawn.OnTeleport", "Function")),
                                  sp.Operand(0x00B0, sp.Ref(func + ".Action", "ObjectProperty"))),
                        grow=sp.Grow(keep, end, tuple(tail)))


def row_sc5_set_squad_name():
    func = "GearGame.GearPC.SetSquadName"
    pri = local(func + ".PRI")
    team = local(func + ".Team")
    new_name = local(func + ".NewSquadName", "NameProperty")
    stmts = [
        if_not("RET", is_hero(self_default_pawn_class())),
        let(pri, NONE),
        foreach_all_controllers(GEAR_PC_CLASS, team, "POP", "IN"),
        if_not("NEXT", is_extra(default_pawn_class_of(team))),
        if_not("FLIP", eq_bool(eq_object(self_default_pawn_class(), class_const(DOM_CLASS)),
                               ne_object(pri, NONE))),
        context(team, final_call(func, new_name), 0),
        # PRI reused as a flip-flop to split extras between the heroes
        label("FLIP"), let(pri, conditional(eq_object(pri, NONE), SELF, NONE)),
        label("NEXT"), iterator_next,
        label("POP"), iterator_pop,
        label("RET"), return_nothing,
        end_of_script,
    ]
    keep = 0x009C
    tail, end = assemble(stmts, keep)
    return sp.ScriptRow("SC-5", "a hero's squad change is copied to the extras paired with him",
                        sp.Ref(func, "Function"), 159,
                        checks=(sp.Check(0x0010, bytes.fromhex("07009c77")),
                                sp.Check(0x006B, bytes.fromhex("07009c77")),
                                sp.Check(0x0037, bytes.fromhex("0f19")),
                                sp.Check(0x009C, bytes.fromhex("040b53"))),
                        operands=(sp.Operand(0x0015, sp.Ref(func + ".PRI", "ObjectProperty")),
                                  sp.Operand(0x0070, sp.Ref(func + ".Team", "ObjectProperty"))),
                        grow=sp.Grow(keep, end, tuple(tail)))


def row_sc6_post_login():
    func = "GearGame.GearGameSP_Base.PostLogin"
    return sp.ScriptRow("SC-6", "LocalPlayer(PC.Player) == None  ->  Object(PC.Player) == None: late joins never cancel the cinematic",
                        sp.Ref(func, "Function"), 1690,
                        checks=(sp.Check(0x040D, bytes.fromhex("0704d682722e")),
                                sp.Check(0x0417, bytes.fromhex("1900")),
                                sp.Check(0x041D, bytes.fromhex("0005000401")),
                                sp.Check(0x0426, bytes.fromhex("2a16180017b11901")),
                                sp.Check(0x0432, bytes.fromhex("0005000401")),
                                sp.Check(0x043B, bytes.fromhex("1e417000001616")),
                                sp.Check(0x04C3, b"\x1F" + b"CANCELCINE\x00")),
                        operands=(sp.Operand(0x0419, sp.Ref(func + ".PC", "ObjectProperty")),
                                  sp.Operand(0x0422, sp.Ref("Engine.PlayerController.Player", "ObjectProperty")),
                                  sp.Operand(0x042E, sp.Ref("Engine.Actor.WorldInfo", "ObjectProperty")),
                                  sp.Operand(0x0437, sp.Ref("Engine.WorldInfo.TimeSeconds", "FloatProperty"))),
                        operand_edits=(sp.OperandEdit(0x0413, sp.Ref("Engine.LocalPlayer", "Class"),
                                                      sp.Ref("Core.Object", "Class")),))


SCENES_ROWS = (row_sc1_set_cinematic_mode(), row_sc2_restart_player(),
               row_sc3_on_checkpoint(), row_sc4_on_teleport(),
               row_sc5_set_squad_name(), row_sc6_post_login())


def canonical_tail(row):
    parts = []
    pending = bytearray()
    prefix = row.function.path + "."
    for piece in row.grow.tail:
        if isinstance(piece, sp.Raw):
            pending += piece.data
            continue
        if pending:
            parts.append(pending.hex().upper())
            pending.clear()
        if isinstance(piece, sp.Ref):
            if piece.path.startswith(prefix):
                parts.append("{local " + piece.path[len(prefix):] + "}")
            else:
                parts.append("{" + piece.path + "}")
        else:
            parts.append((piece.text.encode("ascii") + b"\0").hex().upper())
    if pending:
        parts.append(pending.hex().upper())
    return " ".join(parts)


def render_rows_inc():
    lines = [
        "// coop-scenes rows of docs/COOP_STAGE2_SCENES.md, printed by",
        "// `python -I tools/script_patch_scenes.py --cpp`; edit the rows there, not here. Check them",
        "// against your own decompressed packages with `python -I tools/script_patch_scenes.py --check <folder>`.",
        "",
    ]
    for row in SCENES_ROWS:
        lines.extend(sp._script_lines(row))
        lines.append("")
    lines.extend([
        "PatchGroup CoopScenesGroup() {",
        "  return {",
        '      .name = "coop-scenes",',
        "      .scripts = {RowSC_1(), RowSC_2(), RowSC_3(), RowSC_4(), RowSC_5(), RowSC_6()},",
        "      .defaults = {},",
        "  };",
        "}",
    ])
    return "\n".join(lines) + "\n"


def goto_targets_off_statements(lines):
    offsets = set()
    for line in lines:
        text = line.strip()
        if ":" not in text:
            continue
        try:
            offsets.add(int(text.split(":")[0], 16))
        except ValueError:
            continue
    targets = []
    for line in lines:
        for match in re.findall(r"goto 0x([0-9A-Fa-f]+)", line):
            targets.append(int(match, 16))
    return sorted(t for t in targets if t not in offsets)


def check_package(pkg_dir, players):
    ue3script, _ = sp._ue3_modules()
    print(f"== script_patch_scenes --check {pkg_dir} players={players}")
    try:
        model = sp.PackageModel(pkg_dir)
    except OSError as e:
        print(f"error: cannot load packages from {pkg_dir}: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"error: cannot load packages from {pkg_dir}: {type(e).__name__}({e})", file=sys.stderr)
        return 1
    try:
        plan = sp.plan_group(model, SCENES_ROWS, players)
    except sp.PatchSkip as e:
        print(f"SKIP {e.row}: {e.reason}")
        return 1
    patched = sp.apply_plan(model, plan)
    failed = False
    for entry in plan.entries:
        new = patched[entry.handle]
        if not sp._check_script_row(ue3script, model, entry, new):
            failed = True
        try:
            if len(new) != entry.row.script_size:
                before = model.world.read_function(model.pkg, entry.handle)
                new_lines, _, _ = sp._decode_standalone(ue3script, model, entry.handle,
                                                        before.path, new)
            else:
                new_lines, _, _ = sp._decode_spliced(ue3script, model, entry.handle, new)
        except Exception as e:
            print(f"   goto targets off statement starts: decode failed: {e}")
            failed = True
            continue
        off = goto_targets_off_statements(new_lines)
        print(f"   goto targets off statement starts: {off}")
        if off:
            failed = True
    print("== log lines")
    for line in sp.group_lines(plan):
        print(f"   {line}")
    if failed:
        print("== coop-scenes: FAILED")
        return 1
    print("== coop-scenes: 6 rows OK")
    return 0


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Offline twin of the coop-scenes script patches.")
    p.add_argument("--check", metavar="PKGDIR", default=None,
                   help="check the rows against decompressed packages in PKGDIR")
    p.add_argument("--cpp", action="store_true", help="print the C++ rows file")
    p.add_argument("--write", action="store_true", help="write the C++ rows file (with --cpp)")
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
        text = render_rows_inc()
        if args.write:
            out = Path(__file__).resolve().parent.parent / "src" / "gow2_script_patch_scenes_rows.inc"
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print(str(out))
        else:
            sp._write_stdout(text)
        return 0
    if args.check is None:
        print("error: need --check PKGDIR or --cpp", file=sys.stderr)
        return 2
    return check_package(args.check, args.players)


if __name__ == "__main__":
    sys.exit(main())
