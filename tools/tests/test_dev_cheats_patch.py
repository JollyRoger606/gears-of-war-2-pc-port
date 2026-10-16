import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pytest

import dev_cheats_patch as dcp
import script_patch as sp


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


def make_model():
    model = FakeModel()
    refs = [op.ref for op in dcp.DEV_1.operands]
    handles = {ref: model.add(ref) for ref in refs}
    data = bytearray(31)
    data[0x00] = 0x04
    data[0x01] = 0x9A
    data[0x02:0x06] = b"\x38\x3A\x19\x01"
    data[0x06:0x0A] = model.operand_value(handles[refs[0]]).to_bytes(4, "big")
    data[0x0A:0x0F] = b"\x00\x05\x00\x01\x01"
    data[0x0F:0x13] = model.operand_value(handles[refs[1]]).to_bytes(4, "big")
    data[0x13:0x16] = b"\x38\x3A\x24"
    data[0x16] = 0x00
    data[0x17:0x1A] = b"\x16\x04\x3A"
    data[0x1A:0x1E] = model.operand_value(handles[refs[2]]).to_bytes(4, "big")
    data[0x1E] = 0x53
    before = bytes(data)
    handle = model.add(dcp.DEV_1.function, script=sp.ScriptArray(before, 31, 31))
    return model, handle, before


def test_plan_applies_two_byte_changes():
    model, handle, before = make_model()
    plan = sp.plan_group(model, list(dcp.ROWS), 4)
    out = sp.apply_plan(model, plan)
    after = out[handle]
    assert after[0x01] == 0x98
    assert after[0x16] == 0x02
    for i in range(31):
        if i not in (0x01, 0x16):
            assert after[i] == before[i]
    assert sp.group_lines(plan) == ["DEV-1 Engine.GameInfo.AllowCheats: 2 write(s)"]
    assert model.scripts[handle].data == before


def test_already_applied():
    model, handle, before = make_model()
    patched = bytearray(before)
    patched[0x01] = 0x98
    patched[0x16] = 0x02
    model.scripts[handle] = sp.ScriptArray(bytes(patched), 31, 31)
    plan = sp.plan_group(model, list(dcp.ROWS), 4)
    assert sp.group_lines(plan) == ["DEV-1 Engine.GameInfo.AllowCheats: already applied"]


def test_wrong_old_byte():
    model, handle, before = make_model()
    bad = bytearray(before)
    bad[0x16] = 0x01
    model.scripts[handle] = sp.ScriptArray(bytes(bad), 31, 31)
    with pytest.raises(sp.PatchSkip) as exc:
        sp.plan_group(model, list(dcp.ROWS), 4)
    assert exc.value.row == "DEV-1"


def test_wrong_operand():
    model, handle, before = make_model()
    bad = bytearray(before)
    bad[0x0F:0x13] = (0xDEADBEEF).to_bytes(4, "big")
    model.scripts[handle] = sp.ScriptArray(bytes(bad), 31, 31)
    with pytest.raises(sp.PatchSkip):
        sp.plan_group(model, list(dcp.ROWS), 4)


def test_wrong_num():
    model, handle, before = make_model()
    model.scripts[handle] = sp.ScriptArray(before, 30, 30)
    with pytest.raises(sp.PatchSkip):
        sp.plan_group(model, list(dcp.ROWS), 4)


def test_function_found_twice():
    model, _, _ = make_model()
    model.add(dcp.DEV_1.function)
    with pytest.raises(sp.PatchSkip):
        sp.plan_group(model, list(dcp.ROWS), 4)


def test_render_cpp_matches_file():
    root = Path(__file__).resolve().parents[2]
    with open(root / "src" / "gow2_dev_cheats_table.cpp", "r", encoding="utf-8", newline="") as f:
        assert dcp.render_cpp() == f.read().replace("\r\n", "\n")


def test_write_without_cpp():
    assert dcp.main(["--write"]) == 2
