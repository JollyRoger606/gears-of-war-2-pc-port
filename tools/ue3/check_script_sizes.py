"""Checks that every Function export's on-disk bytecode length equals its serialized ScriptSize
(i.e. disk layout == in-memory layout on the 32-bit 360), and summarizes package flags.

python -I tools/ue3/check_script_sizes.py [GearGame]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ue3script import World

w = World()
for pname in (sys.argv[1:] or ["GearGame"]):
    p = w.pkgs[pname]
    print(f"{pname}: ver={p.file_version} lic={p.licensee} pkgflags=0x{p.package_flags:08X} "
          f"names={p.name_count} exports={p.export_count} imports={p.import_count}")
    ok = bad = 0
    examples = []
    for i, e in enumerate(p.exports):
        if p.obj_class(i + 1) != "Function":
            continue
        f = w.read_function(p, i + 1)
        disk = f.code_end - f.code_start
        if disk == f.script_size:
            ok += 1
        else:
            bad += 1
            if len(examples) < 5:
                examples.append((f.path, disk, f.script_size))
    print(f"  functions: {ok} disk==ScriptSize, {bad} differ; examples {examples}")
