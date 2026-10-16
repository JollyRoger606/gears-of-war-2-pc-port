"""python -I tools/script_patch_stage2.py --check <dir with Core.upk, Engine.upk, GameFramework.upk, GearGame.upk> [--players N]"""
import argparse
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import script_patch as sp
import script_patch_survival as sv
import script_patch_body as spb
import script_patch_scenes as scn
import dev_cheats_patch as dcp


class StackModel(sv.SurvivalPackageModel, spb.BodyModel):
    pass


@dataclass(frozen=True)
class StackGroup:
    name: str
    rows: tuple


class StackSkip(Exception):
    def __init__(self, group, row, reason):
        super().__init__(f"{group}: {row}: {reason}")
        self.group = group
        self.row = row
        self.reason = reason


def stack_groups(marcus_clones=False, carmine=False, split_revive=False):
    groups = [
        StackGroup("coop", tuple(sp.coop_stage1_rows(marcus_clones))),
        StackGroup("survival", tuple(sv.survival_rows())),
    ]
    if split_revive:
        groups.append(StackGroup("split-revive", tuple(sv.split_revive_rows())))
    groups.append(StackGroup("coop-body", tuple(spb.coop_body_rows(carmine=carmine, marcus_clones=marcus_clones))))
    groups.append(StackGroup("coop-ride", tuple(spb.coop_ride_rows())))
    groups.append(StackGroup("coop-scenes", tuple(scn.SCENES_ROWS)))
    return groups


VARIANTS = (
    ("defaults", {}),
    ("gow2_coop_split_revive", {"split_revive": True}),
    ("gow2_coop_extra_carmine", {"carmine": True}),
    ("gow2_coop_marcus_clones", {"marcus_clones": True}),
)


def plan_group_rows(model, rows, players):
    plans = []
    for row in rows:
        if isinstance(row, sp.ScriptRow):
            plans.append(sp._plan_script(model, row, players))
        elif isinstance(row, sp.DefaultRow):
            plans.append(sp._plan_default(model, row, players))
        else:
            plans.append(spb.plan_object_field(model, row))
    return plans


def plan_stack(model, groups, players):
    overlay = {}
    changed = {}
    grouped = []
    for group in groups:
        try:
            plans = plan_group_rows(spb.CoopOverlay(model, overlay), group.rows, players)
        except sp.PatchSkip as e:
            raise StackSkip(group.name, e.row, e.reason) from e
        for entry in plans:
            if isinstance(entry, sp.ScriptPlan):
                overlay[entry.handle] = entry.new
                changed.setdefault(entry.handle, []).append(group.name)
        grouped.append((group, plans))
    return grouped, overlay, changed


def check_shared_function(ue3script, model, handle, final_bytes):
    try:
        path = model.world.read_function(model.pkg, handle).path
        lines, ok, consumed = sp._decode_standalone(ue3script, model, handle, path, final_bytes)
    except Exception as e:
        print(f"decode failed: {e}")
        return False
    for line in lines:
        print(f"   {line}")
    good = ok and consumed == len(final_bytes)
    print(f"   decodes to its end: {good} at 0x{consumed:X} of 0x{len(final_bytes):X}")
    off = scn.goto_targets_off_statements(lines)
    print(f"   goto targets off statement starts: {off}")
    return good and not off


def check_variant(ue3script, model, title, kwargs, players):
    print(f"== stack {title}")
    groups = stack_groups(**kwargs)
    try:
        grouped, overlay, changed = plan_stack(model, groups, players)
    except StackSkip as e:
        print(f"   SKIP {e.group} {e.row}: {e.reason}")
        return False
    for group, plans in grouped:
        for line in spb.row_lines(plans):
            print(f"   SCRIPTPATCH {group.name} {line}")
        print(f"   SCRIPTPATCH {group.name}: {len(plans)} rows applied for {players} players")
    ok = True
    for handle, names in changed.items():
        if len(names) < 2:
            continue
        path = model.world.read_function(model.pkg, handle).path
        print(f"== shared {path} by {' + '.join(names)}")
        if not check_shared_function(ue3script, model, handle, overlay[handle]):
            ok = False
    return ok


def check_stack(pkg_dir, players):
    print(f"== script_patch_stage2 --check {pkg_dir} players={players}")
    try:
        model = StackModel(pkg_dir)
    except OSError as e:
        print(f"error: cannot load packages from {pkg_dir}: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"error: cannot load packages from {pkg_dir}: {type(e).__name__}({e})", file=sys.stderr)
        return 1
    ue3script, _ = sp._ue3_modules()
    failed = False
    for title, kwargs in VARIANTS:
        if not check_variant(ue3script, model, title, kwargs, players):
            failed = True
    if dcp.check_package(pkg_dir) != 0:
        failed = True
    print("== stage2 stack: FAILED" if failed else "== stage2 stack: OK")
    return 1 if failed else 0


def parse_args(argv=None):
    p = argparse.ArgumentParser(description="Stacked check of every co-op script patch group.")
    p.add_argument("--check", metavar="PKGDIR", default=None,
                   help="check the stacked rows against decompressed packages in PKGDIR")
    p.add_argument("--players", type=int, default=4, help="player count 3..255 (default 4)")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    if not 3 <= args.players <= 255:
        print(f"error: --players must be 3..255, got {args.players}", file=sys.stderr)
        return 2
    if args.check is None:
        print("error: need --check PKGDIR", file=sys.stderr)
        return 2
    return check_stack(args.check, args.players)


if __name__ == "__main__":
    sys.exit(main())
