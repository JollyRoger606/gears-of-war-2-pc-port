# Adapted from gears-judgement-recomp (BSD-3-Clause).
"""Post-codegen port of the xenia-canary game patches for Gears of War 2.

Source: xenia-canary/game-patches, patches/4D53082D base file
(title 4D53082D, module hash BDB97753374D520A = this exact retail XEX).
Verified that the file targets this exact image: every patched word below
matches the original instruction at that address in the decrypted image
(file offset == address - 0x82220000, big-endian) and the generated
code's comment there.

A patch in a recompile has to land on the C++ that implements the
instruction, not on bytes. Each site keeps the original behaviour behind a
host switch, `Gow2GamePatch(id)` (src/gow2_game_patches.cpp), so every
patch is a cvar and can be A/B'd without a rebuild:

  id 1 Black Shading Fix      82806A44 stw r11,11940(r6) -> li r11,0
                              (GUseTilingMode=FALSE; GameFixes/gow2_black_shading_fix=true)
  id 2 Texture Streaming Fix  8280D534 lwz r4,80(r1)     -> li r4,196 (PoolSize=196)
                              8280DDC8 lis r10,-31980    -> li r3,0 \
                              8280DDCC mr r11,r3         -> blr     / early "return 0"
                              (RHIGetTextureMemoryStats=FALSE; .../gow2_texture_streaming_fix=true)
  id 3 16x Anisotropic Filtering 8280E7FC lwz r5,408(r30) -> li r5,16
                              (GameFixes/gow2_aniso16=true)

The id 2 blr part is portable only because 0x8280DDC8/0x8280DDCC are the
first two guest instructions of sub_8280DDC8 (no prologue); the patch is
therefore "return 0 immediately", emitted as an early return.

    python tools/patch_game_patches.py generated/default/*.cpp

Idempotent (marker GOW2 GAMEPATCH). Fails closed if any site's instruction text or
C++ body differs from what is asserted below.
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "GOW2 GAMEPATCH"
DECL = f'extern "C" int Gow2GamePatch(int id);  // {MARK}\n'

# (address, function start, expected comment, expected C++ body, patched C++ body, id)
SITES = [
    (0x82806A44, 0x828069F8, "stw r11,11940(r6)",
     ["REX_STORE_U32(ctx.r6.u32 + 11940, ctx.r11.u32);"],
     "if (Gow2GamePatch(1)) { ctx.r11.s64 = 0; } else { "
     "REX_STORE_U32(ctx.r6.u32 + 11940, ctx.r11.u32); }", 1),
    (0x8280D534, 0x8280D4C8, "lwz r4,80(r1)",
     ["ctx.r4.u64 = REX_LOAD_U32(ctx.r1.u32 + 80);"],
     "ctx.r4.u64 = Gow2GamePatch(2) ? 196 : REX_LOAD_U32(ctx.r1.u32 + 80);", 2),
    (0x8280DDC8, 0x8280DDC8, "lis r10,-31980",
     ["ctx.r10.s64 = -2095841280;"],
     "if (Gow2GamePatch(2)) { ctx.r3.s64 = 0; return; } ctx.r10.s64 = -2095841280;", 2),
    (0x8280DDCC, 0x8280DDC8, "mr r11,r3",
     ["ctx.r11.u64 = ctx.r3.u64;"],
     "if (!Gow2GamePatch(2)) { ctx.r11.u64 = ctx.r3.u64; }", 2),
    (0x8280E7FC, 0x8280E740, "lwz r5,408(r30)",
     ["ctx.r5.u64 = REX_LOAD_U32(ctx.r30.u32 + 408);"],
     "ctx.r5.u64 = Gow2GamePatch(3) ? 16 : REX_LOAD_U32(ctx.r30.u32 + 408);", 3),
]

INSN = re.compile(r"^\s*// [a-z]")


def main(argv: list[str]) -> int:
    files: list[str] = []
    for a in argv:
        files.extend(glob.glob(a) if any(c in a for c in "*?[") else [a])
    by_func: dict[int, str] = {}
    head = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)")
    wanted = {s[1] for s in SITES}
    for f in files:
        with open(f, errors="replace") as fh:
            for line in fh:
                m = head.match(line)
                if m and int(m.group(1), 16) in wanted:
                    by_func[int(m.group(1), 16)] = f
    missing = wanted - by_func.keys()
    if missing:
        print(f"patch_game_patches: functions not found: {sorted(hex(x) for x in missing)}")
        return 1
    edits: dict[str, list] = {}
    for site in SITES:
        edits.setdefault(by_func[site[1]], []).append(site)
    total = 0
    for f, sites in edits.items():
        with open(f, errors="replace", newline="") as fh:
            lines = fh.readlines()
        changed = False
        for addr, fstart, want, body, new, pid in sites:
            start = next(i for i, l in enumerate(lines)
                         if l.startswith(f"DEFINE_REX_FUNC(sub_{fstart:08X})"))
            k = (addr - fstart) // 4
            seen = -1
            for j in range(start + 1, len(lines)):
                if lines[j].startswith("DEFINE_REX_FUNC"):
                    raise SystemExit(f"{addr:08X}: ran off sub_{fstart:08X}")
                if INSN.match(lines[j]):
                    seen += 1
                    if seen == k:
                        break
            comment = lines[j].strip()
            if MARK in lines[j + 1]:
                continue  # already patched
            if comment != f"// {want}":
                raise SystemExit(f"{addr:08X}: expected '// {want}', found '{comment}'")
            got = [lines[j + 1 + n].strip() for n in range(len(body))]
            if got != body:
                raise SystemExit(f"{addr:08X}: body mismatch {got}")
            indent = re.match(r"\s*", lines[j + 1]).group(0)
            nl = "\r\n" if lines[j + 1].endswith("\r\n") else "\n"
            lines[j + 1:j + 1 + len(body)] = [f"{indent}{new}  // {MARK} {pid}{nl}"]
            changed = True
            total += 1
        if changed:
            text = "".join(lines)
            if DECL not in text:
                text = DECL + text
            tmp = f + ".gp.tmp"
            with open(tmp, "w", errors="replace", newline="") as fh:
                fh.write(text)
            os.replace(tmp, f)
    print(f"patch_game_patches: {total} site(s) patched")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
