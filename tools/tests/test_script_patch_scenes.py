import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pytest

import script_patch as sp
import script_patch_scenes as scenes


class FakeModel:
    def __init__(self):
        self.objects = {}
        self.values = {}
        self.scripts = {}
        self.ints = {}

    def add(self, ref, script=None, int_value=None):
        handle = len(self.values)
        self.objects.setdefault(ref, []).append(handle)
        self.values[handle] = 0x80001000 + handle
        if script is not None:
            self.scripts[handle] = script
        if int_value is not None:
            self.ints[handle] = int_value
        return handle

    def find(self, ref):
        return list(self.objects.get(ref, []))

    def operand_value(self, handle):
        return self.values[handle]

    def script(self, handle):
        return self.scripts[handle]

    def read_int(self, handle, row):
        return self.ints[handle]


def canonical_tail(row):
    parts = []
    pending = bytearray()
    prefix = row.function.path + "."

    def flush():
        if pending:
            parts.append(pending.hex().upper())
            pending.clear()

    for piece in row.grow.tail:
        if isinstance(piece, sp.Raw):
            pending += piece.data
            continue
        flush()
        if isinstance(piece, sp.Ref):
            if piece.path.startswith(prefix):
                parts.append("{local " + piece.path[len(prefix):] + "}")
            else:
                parts.append("{" + piece.path + "}")
        else:
            parts.append((piece.text.encode("ascii") + b"\0").hex().upper())
    flush()
    return " ".join(parts)


def all_refs(rows):
    refs = []
    seen = set()

    def add(ref):
        if ref not in seen:
            seen.add(ref)
            refs.append(ref)

    for row in rows:
        add(row.function)
        for operand in row.operands:
            add(operand.ref)
        for edit in row.operand_edits:
            add(edit.old)
            add(edit.new)
        if row.grow is not None:
            for piece in row.grow.tail:
                if isinstance(piece, sp.Ref):
                    add(piece)
    return refs


def synthetic_model(rows, skip=()):
    model = FakeModel()
    handles = {}
    for ref in all_refs(rows):
        if ref.path in skip:
            continue
        handles[ref] = model.add(ref)
    for row in rows:
        func_handle = handles.get(row.function)
        if func_handle is None:
            continue
        data = bytearray([0x0B] * row.script_size)
        for check in row.checks:
            data[check.offset:check.offset + len(check.data)] = check.data
        for operand in row.operands:
            handle = handles.get(operand.ref)
            if handle is not None:
                data[operand.offset:operand.offset + 4] = model.operand_value(handle).to_bytes(4, "big")
        for edit in row.byte_edits:
            data[edit.offset] = edit.old
        for edit in row.operand_edits:
            handle = handles.get(edit.old)
            if handle is not None:
                data[edit.offset:edit.offset + 4] = model.operand_value(handle).to_bytes(4, "big")
        model.scripts[func_handle] = sp.ScriptArray(bytes(data), row.script_size, row.script_size)
    return model


def fake_tail(model, row):
    tail = bytearray()
    for piece in row.grow.tail:
        if isinstance(piece, sp.Raw):
            tail += piece.data
        elif isinstance(piece, sp.Text):
            tail += piece.text.encode("ascii") + b"\0"
        else:
            tail += model.operand_value(model.find(piece)[0]).to_bytes(4, "big")
    return bytes(tail)


PINNED = {
    "SC-1": "0702A47201 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 162F1901 {Engine.Actor.WorldInfo} 009200001C {Engine.WorldInfo.AllControllers} 20 {GearGame.GearPC} 00 {local AI} 1602A30702A28282771900 {local AI} 0005000401 {GearGame.GearPC.DefaultPawnClass} 2A16180017771900 {local AI} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 1616180017771900 {local AI} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 16161900 {local AI} 002A00001C {GearGame.GearPC.SetCinematicMode} 2D00 {local bInCinematicMode} 2D00 {local bHidePlayer} 2D00 {local bAffectsHUD} 2D00 {local bAffectsMovement} 2D00 {local bAffectsTurning} 2D00 {local bAffectsButtons} 163130040B53",
    "SC-2": "070960827700 {local HumanPlayer} 2A161800498282771900 {local HumanPlayer} 0005000401 {GearGame.GearPC.DefaultPawnClass} 2A16180017771900 {local HumanPlayer} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 1616180017771900 {local HumanPlayer} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 1616162F1901 {Engine.Actor.WorldInfo} 00B600001C {Engine.WorldInfo.AllControllers} 20 {GearGame.GearAI_Dom} 00 {local C} 1608A10708A08282721900 {local C} 0005000401 {Core.Object.Class} 20 {GearGame.GearAI_Dom} 16180013771900 {local C} 0005000401 {Engine.Controller.Pawn} 2A161618002B72191900 {local C} 0005000401 {Engine.Controller.Pawn} 0005000401 {Core.Object.Class} 1900 {local HumanPlayer} 0005000401 {GearGame.GearPC.DefaultPawnClass} 16160F00 {local CoopPawn} 1900 {local C} 0005000401 {Engine.Controller.Pawn} 1900 {local CoopPawn} 000300046117160708A0811900 {local C} 000600042D01 {Engine.Actor.bDeleteMe} 161900 {local C} 000300046117163130070960771900 {local HumanPlayer} 0005000401 {Engine.Controller.Pawn} 2A162F1901 {Engine.Actor.WorldInfo} 009D00001C {Engine.WorldInfo.AllControllers} 20 {GearGame.GearPC} 00 {local OtherPC} 16095F07095E82721900 {local OtherPC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 161800111900 {local OtherPC} 000600042D01 {Engine.PlayerController.bCinematicMode} 161900 {local HumanPlayer} 005200001C {GearGame.GearPC.SetCinematicMode} 27191900 {local OtherPC} 0005000401 {Engine.Controller.Pawn} 000600042D01 {Engine.Actor.bHidden} 281900 {local OtherPC} 000600042D01 {Engine.PlayerController.bCinemaDisableInputMove} 1900 {local OtherPC} 000600042D01 {Engine.PlayerController.bCinemaDisableInputLook} 1900 {local OtherPC} 000600042D01 {GearGame.GearPC.bCinemaDisableInputButtons} 163130040B53",
    "SC-3": "07040182771900 {local TeleportToPC} 0005000401 {Engine.Controller.Pawn} 2A1618001F9B461900 {local Action} 0005000001 {GearGame.SeqAct_Checkpoint.TeleportClasses} 000520 {GearGame.GearPawn_COGMarcus} 1DFFFFFFFF16162F1901 {Engine.Actor.WorldInfo} 00E900001C {Engine.WorldInfo.AllControllers} 20 {GearGame.GearPC} 00 {local PC} 1604000703FF8282827700 {local PC} 00 {local TeleportToPC} 161800498282771900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 2A16180017771900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 1616180017771900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 161616180018772E {GearGame.GearPawn} 1900 {local PC} 0005000401 {Engine.Controller.Pawn} 2A16161800391900 {local Action} 002E00041C {GearGame.SeqAct_Checkpoint.ShouldTeleport} 1900 {local PC} 0005000401 {Engine.Controller.Pawn} 191900 {local TeleportToPC} 0005000401 {Engine.Controller.Pawn} 0005000C01 {Engine.Actor.Location} 16161900 {local TeleportToPC} 001700041C {GearGame.GearPC.FindAvailableTeleportSpot} 1900 {local PC} 0005000401 {Engine.Controller.Pawn} 4A4A1631300602D253",
    "SC-4": "07025C827700 {local PC} 2A1618003284721900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 16180017721900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 1616160F00 {local Action} 2A2F1901 {Engine.Actor.WorldInfo} 015300001C {Engine.WorldInfo.AllControllers} 20 {GearGame.GearPC} 00 {local PC} 16025B07025A8282771900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 2A16180017771900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 1616180017771900 {local PC} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 16160702458282772E {GearGame.GearPawn} 1900 {local PC} 0005000401 {Engine.Controller.Pawn} 2A1618002AB1E1D8191900 {local PC} 0005000401 {Engine.Controller.Pawn} 0005000C01 {Engine.Actor.Location} 01 {Engine.Actor.Location} 16161E44BB8000161618006EF272192E {GearGame.GearPC} 01 {Engine.Pawn.Controller} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 168284192E {GearGame.GearGame} 1901 {Engine.Actor.WorldInfo} 0005000401 {Engine.WorldInfo.Game} 000600042D01 {GearGame.GearGame.bInCoopSplit} 180020192E {GearGame.GearGame} 1901 {Engine.Actor.WorldInfo} 0005000401 {Engine.WorldInfo.Game} 000600042D01 {GearGame.GearGame.bInSoloSplit} 161800097700 {local Action} 2A16161616192E {GearGame.GearPC} 01 {Engine.Pawn.Controller} 001700041C {GearGame.GearPC.FindAvailableTeleportSpot} 1900 {local PC} 0005000401 {Engine.Controller.Pawn} 4A4A160F00 {local Action} 457200 {local Action} 2A1600011700012A3130040B53",
    "SC-5": "070170847201 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 1618000D7201 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 16160F00 {local PRI} 2A2F1901 {Engine.Actor.WorldInfo} 00A100001C {Engine.WorldInfo.AllControllers} 20 {GearGame.GearPC} 00 {local Team} 16016F07016E8282771900 {local Team} 0005000401 {GearGame.GearPC.DefaultPawnClass} 2A16180017771900 {local Team} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGMarcus} 1616180017771900 {local Team} 0005000401 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 1616070159F27201 {GearGame.GearPC.DefaultPawnClass} 20 {GearGame.GearPawn_COGDom} 167700 {local PRI} 2A16161900 {local Team} 000B00001C {GearGame.GearPC.SetSquadName} 00 {local NewSquadName} 160F00 {local PRI} 457200 {local PRI} 2A1600011700012A3130040B53",
}


def test_inc_file_matches_render():
    path = Path(__file__).resolve().parents[2] / "src" / "gow2_script_patch_scenes_rows.inc"

    with open(path, encoding="utf-8", newline="") as f:
        text = f.read().replace("\r\n", "\n")  # a core.autocrlf checkout has CRLF

    assert scenes.render_rows_inc() == text
    assert "namespace" not in text
    assert '.name = "coop-scenes"' in text


def test_pinned_tails():
    rows = {row.id: row for row in scenes.SCENES_ROWS}

    tails = {row_id: canonical_tail(rows[row_id]) for row_id in ("SC-1", "SC-2", "SC-3", "SC-4", "SC-5")}

    for row_id, tail in tails.items():
        assert tail == PINNED[row_id]
    assert rows["SC-6"].grow is None


def test_tail_sizes_and_endings():
    rows = {row.id: row for row in scenes.SCENES_ROWS}

    raws = {}
    for row in rows.values():
        if row.grow is None:
            continue
        size = 0
        merged = bytearray()
        for piece in row.grow.tail:
            if isinstance(piece, sp.Raw):
                size += len(piece.data)
                merged += piece.data
            elif isinstance(piece, sp.Text):
                size += len(piece.text.encode("ascii")) + 1
            else:
                size += 4
        assert row.grow.keep + size == row.grow.new_size
        raws[row.id] = bytes(merged)

    for row_id in ("SC-1", "SC-2", "SC-4", "SC-5"):
        assert raws[row_id].endswith(b"\x04\x0B\x53")
    assert raws["SC-3"].endswith(b"\x06\x02\xD2\x53")


def test_synthetic_plan_applies_all_rows():
    model = synthetic_model(scenes.SCENES_ROWS)
    originals = {}
    for row in scenes.SCENES_ROWS:
        handle = model.find(row.function)[0]
        originals[row.id] = (handle, model.scripts[handle].data)

    plan = sp.plan_group(model, scenes.SCENES_ROWS, 4)

    out = sp.apply_plan(model, plan)
    for row in scenes.SCENES_ROWS:
        handle, original = originals[row.id]
        new = out[handle]
        if row.grow is not None:
            assert len(new) == row.grow.new_size
            if row.id == "SC-3":
                assert new[:0x02A7] == original[:0x02A7]
                assert new[0x02A7] == 0xD4
                assert original[0x02A7] == 0xD2
                assert model.scripts[handle].data[0x02A7] == 0xD2
                assert new[0x02A8:row.grow.keep] == original[0x02A8:row.grow.keep]
            else:
                assert new[:row.grow.keep] == original[:row.grow.keep]
            assert new[row.grow.keep:] == fake_tail(model, row)
        else:
            assert row.id == "SC-6"
            assert len(new) == 1690
            core = model.find(sp.Ref("Core.Object", "Class"))[0]
            assert new[0x0413:0x0417] == model.operand_value(core).to_bytes(4, "big")
            assert new[:0x0413] == original[:0x0413]
            assert new[0x0417:] == original[0x0417:]
    assert sp.group_lines(plan) == [
        "SC-1 GearGame.GearPC.SetCinematicMode: script grown to 679 bytes",
        "SC-2 GearGame.GearGameSP_Base.RestartPlayer: script grown to 2403 bytes",
        "SC-3 GearGame.GearPC.OnCheckpoint: script grown to 1029 bytes, with 1 write(s)",
        "SC-4 GearGame.GearPawn.OnTeleport: script grown to 607 bytes",
        "SC-5 GearGame.GearPC.SetSquadName: script grown to 371 bytes",
        "SC-6 GearGame.GearGameSP_Base.PostLogin: 1 write(s)",
    ]


def test_flipped_check_byte_skips_group():
    model = synthetic_model(scenes.SCENES_ROWS)
    row = {r.id: r for r in scenes.SCENES_ROWS}["SC-4"]
    handle = model.find(row.function)[0]
    data = bytearray(model.scripts[handle].data)
    assert data[0x00B5] == 0x04
    data[0x00B5] = 0xFB
    model.scripts[handle] = sp.ScriptArray(bytes(data), row.script_size, row.script_size)

    with pytest.raises(sp.PatchSkip) as exc:
        sp.plan_group(model, scenes.SCENES_ROWS, 4)

    assert exc.value.row == "SC-4"
    assert "+0x00B5" in exc.value.reason


def test_replan_on_patched_scripts_is_already_applied():
    model = synthetic_model(scenes.SCENES_ROWS)
    out = sp.apply_plan(model, sp.plan_group(model, scenes.SCENES_ROWS, 4))
    for row in scenes.SCENES_ROWS:
        handle = model.find(row.function)[0]
        new = out[handle]
        model.scripts[handle] = sp.ScriptArray(new, len(new), len(new))

    plan = sp.plan_group(model, scenes.SCENES_ROWS, 4)

    for line in sp.group_lines(plan):
        assert line.endswith("already applied")


def test_grown_size_with_other_tail_skips():
    model = synthetic_model(scenes.SCENES_ROWS)
    row = {r.id: r for r in scenes.SCENES_ROWS}["SC-1"]
    handle = model.find(row.function)[0]
    prefix = model.scripts[handle].data[:0x01F7]
    model.scripts[handle] = sp.ScriptArray(prefix + bytes(679 - 0x01F7), 679, 679)

    with pytest.raises(sp.PatchSkip) as exc:
        sp.plan_group(model, scenes.SCENES_ROWS, 4)

    assert exc.value.row == "SC-1"
    assert "tail differs" in exc.value.reason


def test_missing_class_skips_group():
    model = synthetic_model(scenes.SCENES_ROWS, skip={"GearGame.GearAI_Dom"})

    with pytest.raises(sp.PatchSkip) as exc:
        sp.plan_group(model, scenes.SCENES_ROWS, 4)

    assert exc.value.row == "SC-2"
    assert "GearGame.GearAI_Dom" in exc.value.reason
    assert "found 0 times" in exc.value.reason


def test_cli_usage_errors():
    argsets = [["--check", "x", "--players", "2"], ["--write"], []]

    codes = [scenes.main(args) for args in argsets]

    assert codes == [2, 2, 2]
