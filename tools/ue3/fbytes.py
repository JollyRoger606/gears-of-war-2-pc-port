"""Print each top-level statement of a function with its raw bytecode bytes.
python -I tools/ue3/fbytes.py <regex on Class.Function path> [pkgname]
Also prints the file offset of the function's script start in the decompressed .upk and the
export's serial offset/size, so a patch can be located in the package.
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue3script as U

w = U.World()
rx = re.compile(sys.argv[1])
only = sys.argv[2] if len(sys.argv) > 2 else None
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
        e = p.exports[i]
        print(f"== {pname}.{path} export#{i+1} serial_off=0x{e['serial_offset']:X} size=0x{e['serial_size']:X} "
              f"script_size={f.script_size} code_file_off=0x{f.code_start:X} code_len=0x{f.code_end-f.code_start:X}")
        d = U.Decoder(w, f)
        while d.pos < f.code_end:
            at = d.pos - d.base
            s = d.expr()
            raw = p.b[d.base + at:d.pos]
            print(f"  {at:04X} [{len(raw):3d}] {s}")
            print(f"        {raw.hex(' ')}")
            if s == "<EndOfScript>":
                break
