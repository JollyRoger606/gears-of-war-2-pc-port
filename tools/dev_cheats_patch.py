"""Offline twin of the dev-only devcheats row in src/gow2_dev_cheats.cpp.

It owns the patch row, prints the C++ table and checks the row against the
player's own decompressed packages.

python -I tools/dev_cheats_patch.py --check <dir with Engine.upk>
python -I tools/dev_cheats_patch.py --cpp
python -I tools/dev_cheats_patch.py --cpp --write
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import script_patch as sp

DEV_1 = sp.ScriptRow("DEV-1", "return NetMode == NM_Standalone  ->  return NetMode <= NM_ListenServer",
    sp.Ref("Engine.GameInfo.AllowCheats", "Function"), 31,
    checks=(sp.Check(0x00, b"\x04"), sp.Check(0x02, b"\x38\x3A\x19\x01"),
            sp.Check(0x0A, b"\x00\x05\x00\x01\x01"), sp.Check(0x13, b"\x38\x3A\x24"),
            sp.Check(0x17, b"\x16\x04\x3A"), sp.Check(0x1E, b"\x53")),
    operands=(sp.Operand(0x06, sp.Ref("Engine.Actor.WorldInfo", "ObjectProperty")),
              sp.Operand(0x0F, sp.Ref("Engine.WorldInfo.NetMode", "ByteProperty")),
              sp.Operand(0x1A, sp.Ref("Engine.GameInfo.AllowCheats.ReturnValue", "BoolProperty"))),
    byte_edits=(sp.ByteEdit(0x01, 0x9A, 0x98), sp.ByteEdit(0x16, 0x00, 0x02)))

ROWS = (DEV_1,)


class EnginePackageModel(sp.PackageModel):
    def __init__(self, pkg_dir):
        ue3script, ue3pkg = sp._ue3_modules()
        self._ue3pkg = ue3pkg
        self.world = ue3script.World(pkg_dir)
        if "Engine" not in self.world.pkgs:
            raise FileNotFoundError(f"Engine.upk not found in {pkg_dir}")
        self.pkg = self.world.pkgs["Engine"]
        self.pkg_name = "Engine"


def render_cpp():
    lines = [
        "// gow2 - ReXGlue Recompiled Project",
        "//",
        "// The dev-only devcheats row of docs/TEST_TOOLING.md. Printed by",
        "// `python -I tools/dev_cheats_patch.py --cpp`; edit the row there, not here. Check it against your",
        "// own decompressed packages with `python -I tools/dev_cheats_patch.py --check <folder>`.",
        "",
        '#include "gow2_dev_cheats.h"',
        "",
        "namespace rex::glue::script_patch {",
        "",
        "namespace {",
        "",
    ]
    lines.extend(sp._script_lines(DEV_1))
    lines.append("")
    lines.extend([
        "}  // namespace",
        "",
        "PatchGroup DevCheatsGroup() {",
        "  return {",
        '      .name = "devcheats",',
        "      .scripts = {RowDEV_1()},",
        "      .defaults = {},",
        "  };",
        "}",
        "",
        "}  // namespace rex::glue::script_patch",
    ])
    return "\n".join(lines) + "\n"


def _patched_le_line(ue3script, model, entry, new_script):
    try:
        new_lines, _, _ = sp._decode_spliced(ue3script, model, entry.handle, new_script)
    except Exception as e:
        print(f"   patched decode failed: {e}")
        return False
    for line in new_lines:
        if "<=" in line and "ByteToInt(2)" in line:
            print(f"   patched line: {line.strip()}")
            return True
    print('   expected a decoded line with "<=" and "ByteToInt(2)"')
    return False


def check_package(pkg_dir):
    ue3script, _ = sp._ue3_modules()
    print(f"== dev_cheats_patch --check {pkg_dir}")
    try:
        model = EnginePackageModel(pkg_dir)
    except OSError as e:
        print(f"error: cannot load packages from {pkg_dir}: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"error: cannot load packages from {pkg_dir}: {type(e).__name__}({e})", file=sys.stderr)
        return 1
    try:
        plan = sp.plan_group(model, list(ROWS), 4)
    except sp.PatchSkip as e:
        print(f"SKIP {e.row}: {e.reason}")
        return 1
    patched = sp.apply_plan(model, plan)
    failed = False
    for entry in plan.entries:
        if not sp._check_script_row(ue3script, model, entry, patched[entry.handle]):
            failed = True
        if not _patched_le_line(ue3script, model, entry, patched[entry.handle]):
            failed = True
    for line in sp.group_lines(plan):
        print(line)
    print("== C++ table")
    print(render_cpp(), end="")
    return 1 if failed else 0


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Offline twin of the dev-only devcheats script patch.")
    p.add_argument("--check", metavar="PKGDIR", default=None,
                   help="check the row against decompressed packages in PKGDIR")
    p.add_argument("--cpp", action="store_true", help="print the C++ table file")
    p.add_argument("--write", action="store_true", help="write the C++ table file (with --cpp)")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if args.write and not args.cpp:
        print("error: --write needs --cpp", file=sys.stderr)
        return 2
    if args.cpp:
        text = render_cpp()
        if args.write:
            out = Path(__file__).resolve().parent.parent / "src" / "gow2_dev_cheats_table.cpp"
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print(str(out))
        else:
            sp._write_stdout(text)
        return 0
    if args.check is None:
        print("error: need --check PKGDIR or --cpp", file=sys.stderr)
        return 2
    return check_package(args.check)


if __name__ == "__main__":
    sys.exit(main())
