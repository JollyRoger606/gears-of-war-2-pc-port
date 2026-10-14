import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import harvest_hints as hh

RDATA = 0x82001000
TEXT = 0x82220000
PDATA = 0x821D1800
RELOC = 0x83170200


def build_image(img: Path, blocks: list[tuple[str, int, bytes, bool]]) -> None:
    img.mkdir(parents=True, exist_ok=True)
    rows = []
    for name, start, data, exe in blocks:
        (img / f"{start:08X}.bin").write_bytes(data)
        rows.append(f"{name}\t{start:08X}\t{len(data)}\ttrue\tfalse\t{'true' if exe else 'false'}")
    (img / "blocks.tsv").write_text("\n".join(rows) + "\n")


def reloc_block(va: int, entries: list[int]) -> bytes:
    head = struct.pack(">II", va, 8 + 2 * len(entries))
    return head + b"".join(struct.pack(">H", e) for e in entries)


def three_block_reloc(first_entries: list[int]) -> bytes:
    return reloc_block(0x1000, first_entries) + reloc_block(0x2000, []) + reloc_block(0x3000, [])


def std_blocks(rdata: bytes, text: bytes, pdata: bytes = bytes(8),
               reloc: bytes | None = None) -> list[tuple[str, int, bytes, bool]]:
    return [
        (".rdata", RDATA, rdata, False),
        (".text", TEXT, text, True),
        (".pdata", PDATA, pdata, False),
        (".reloc", RELOC, reloc if reloc is not None else three_block_reloc([]), False),
    ]


def build_generated(gen: Path, registered: list[int], labels: list[int]) -> None:
    gen.mkdir(parents=True, exist_ok=True)
    lines = ["void reg() {"]
    lines += [f"  registrar->SetFunction(0x{a:08X}, sub_{a:08X});" for a in registered]
    lines += ["}"]
    (gen / "gow2_register.cpp").write_text("\n".join(lines) + "\n")
    if labels:
        body = "".join(f"loc_{a:08X}:\n" for a in labels)
        (gen / "gow2_recomp.0.cpp").write_text("DEFINE_REX_FUNC(sub_82220000) {\n" + body + "}\n")


def test_reloc_detects_big_endian_and_yields_pointer(tmp_path: Path) -> None:
    rdata = bytearray(0x100)
    struct.pack_into(">I", rdata, 0x10, TEXT + 0x20)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(rdata), bytes(0x100), reloc=three_block_reloc([0x3010])))
    blocks = hh.load_blocks(img)
    reloc = next(b for b in blocks if b.name == ".reloc")
    assert hh.detect_reloc_endian(reloc.data) == "big"
    by_type, ptrs, ignored = hh.harvest_reloc(blocks, "big")
    assert ptrs == [TEXT + 0x20]
    assert by_type == {3: 1}
    assert ignored == 0


def test_thunk_pair_found(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">II", text, 0x20, 0x3863FFBC, 0x48000000)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    assert hh.harvest_thunks(hh.load_blocks(img)) == [TEXT + 0x20]


def test_registered_and_label_rejected(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">II", text, 0x10, 0x3863FFBC, 0x48000000)
    struct.pack_into(">II", text, 0x20, 0x38630000, 0x48000004)
    img, gen = tmp_path / "image", tmp_path / "gen"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    build_generated(gen, [TEXT + 0x10], [TEXT + 0x20])
    blocks = hh.load_blocks(img)
    cands = {TEXT + 0x10: {"thunk"}, TEXT + 0x20: {"thunk"}}
    accepted, rejects = hh.classify(blocks, cands, hh.load_registered(gen), hh.load_labels(gen))
    assert accepted == {}
    assert (rejects["registered"], rejects["label"]) == (1, 1)


def test_reloc_boundary_blr_accepted_mid_function_rejected(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">IIII", text, 0x10, 0x4E800020, 0x7C0802A6, 0x60000000, 0x7C0802A6)
    rdata = bytearray(0x100)
    struct.pack_into(">II", rdata, 0, TEXT + 0x14, TEXT + 0x1C)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(rdata), bytes(text), reloc=three_block_reloc([0x3000, 0x3004])))
    blocks = hh.load_blocks(img)
    _, ptrs, _ = hh.harvest_reloc(blocks, "big")
    cands: dict[int, set[str]] = {}
    hh.add_candidates(cands, ptrs, "reloc")
    accepted, rejects = hh.classify(blocks, cands, set(), set())
    assert set(accepted) == {TEXT + 0x14}
    assert rejects["mid_function"] == 1


def test_datascan_bctr_accepted_mflr_rejected(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">IIII", text, 0x10, 0x4E800420, 0x7C0802A6, 0x7C0802A6, 0x60000000)
    rdata = bytearray(0x100)
    struct.pack_into(">II", rdata, 0, TEXT + 0x14, TEXT + 0x1C)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(rdata), bytes(text)))
    blocks = hh.load_blocks(img)
    ptrs = hh.harvest_datascan(blocks)
    assert TEXT + 0x14 in ptrs and TEXT + 0x1C in ptrs
    cands: dict[int, set[str]] = {}
    hh.add_candidates(cands, ptrs, "datascan")
    accepted, rejects = hh.classify(blocks, cands, set(), set())
    assert accepted == {TEXT + 0x14: {"datascan"}}
    assert rejects["mid_function"] == 1


def test_output_format_exact(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">II", text, 0x10, 0x3863FFBC, 0x48000000)
    struct.pack_into(">I", text, 0x20, 0x7C0802A6)
    rdata = bytearray(0x100)
    struct.pack_into(">I", rdata, 0, TEXT + 0x20)
    pdata = struct.pack(">II", TEXT + 0x20, 0) + bytes(8)
    img, gen = tmp_path / "image", tmp_path / "gen"
    build_image(img, std_blocks(bytes(rdata), bytes(text), pdata=pdata,
                                reloc=three_block_reloc([0x3000])))
    build_generated(gen, [], [])
    out = tmp_path / "sub" / "dir" / "hints.toml"
    assert hh.main(["--image", str(img), "--generated", str(gen), "--out", str(out)]) == 0
    assert out.read_text() == (
        "# Generated by tools/harvest_hints.py - do not edit by hand.\n"
        "[functions]\n"
        "0x82220010 = {}  # thunk\n"
        "0x82220020 = {}  # reloc,pdata,datascan\n"
    )


def test_thunk_boundary_beq_rejected_b_accepted(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">III", text, 0x10, 0x419A000C, 0x3863FFBC, 0x48000000)
    struct.pack_into(">IIII", text, 0x20, 0x38630000, 0x48000000, 0x38630000, 0x48000000)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    blocks = hh.load_blocks(img)
    found = hh.harvest_thunks(blocks)
    assert TEXT + 0x14 in found and TEXT + 0x28 in found
    cands = {a: {"thunk"} for a in found}
    accepted, rejects = hh.classify(blocks, cands, set(), set())
    assert TEXT + 0x14 not in accepted
    assert TEXT + 0x28 in accepted
    assert rejects["mid_function"] == 1


def test_denylist_rejects_with_denied(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">II", text, 0x10, 0x4E800020, 0x7C0802A6)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    blocks = hh.load_blocks(img)
    cands = {TEXT + 0x14: {"reloc"}}
    accepted, _ = hh.classify(blocks, cands, set(), set())
    assert TEXT + 0x14 in accepted
    accepted, rejects = hh.classify(blocks, cands, set(), set(), {TEXT + 0x14})
    assert accepted == {}
    assert rejects["denied"] == 1
    deny = tmp_path / "deny.txt"
    deny.write_text("# comment\n\n0x82220014  # trailing\n0X82220020\n")
    assert hh.load_denylist(deny) == {TEXT + 0x14, TEXT + 0x20}
    assert hh.load_denylist(tmp_path / "missing.txt") == set()


def test_hint_fixpoint_appends_culprit_once(tmp_path: Path) -> None:
    import hint_fixpoint as hf

    logf = tmp_path / "codegen.stdout"
    logf.write_text(
        "Unresolved b target 0x82220050 from 0x82220010\n"
        "Unresolved conditional branch to 0x82220060 from 0x82220070\n"
    )
    pairs = hf.parse_unresolved(logf.read_text())
    assert (0x82220010, 0x82220050) in pairs
    assert (0x82220070, 0x82220060) in pairs
    hints = tmp_path / "hints.toml"
    hints.write_text("[functions]\n0x82220020 = {}  # thunk\n0x82220064 = {}  # thunk\n")
    deny = tmp_path / "deny.txt"
    deny.write_text("# denylist\n")
    assert hf.main(["--codegen-log", str(logf), "--hints", str(hints), "--deny", str(deny)]) == 1
    content = deny.read_text()
    assert "0x82220020" in content and "0x82220064" in content
    assert "# split" in content
    assert content.count("0x82220020") == 1
    assert hf.main(["--codegen-log", str(logf), "--hints", str(hints), "--deny", str(deny)]) == 0
    assert deny.read_text().count("0x82220020") == 1


def _lis(rd: int, hi: int) -> int:
    return (15 << 26) | (rd << 21) | (hi & 0xFFFF)


def _addi(rx: int, rd: int, lo: int) -> int:
    return (14 << 26) | (rx << 21) | (rd << 16) | (lo & 0xFFFF)


def _ori(rx: int, rs: int, lo: int) -> int:
    return (24 << 26) | (rs << 21) | (rx << 16) | (lo & 0xFFFF)


def test_codeptr_addi_negative_lo(tmp_path: Path) -> None:
    text = bytearray(0x9000)
    target = TEXT + 0x8040  # 0x82228040: (0x8223 << 16) + sign(0x8040)
    struct.pack_into(">II", text, 0x10, _lis(3, 0x8223), _addi(4, 3, 0x8040))
    struct.pack_into(">II", text, 0x803C, 0x4E800020, 0x7C0802A6)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    blocks = hh.load_blocks(img)
    assert target in hh.harvest_codeptr(blocks)
    accepted, _ = hh.classify(blocks, {target: {"codeptr"}}, set(), set())
    assert target in accepted


def test_codeptr_ori(tmp_path: Path) -> None:
    text = bytearray(0x9000)
    target = TEXT + 0x8040  # (0x8222 << 16) | 0x8040 (unsigned LO)
    struct.pack_into(">I", text, 0x10, _lis(5, 0x8222))
    struct.pack_into(">I", text, 0x1C, _ori(6, 5, 0x8040))
    struct.pack_into(">II", text, 0x803C, 0x4E800420, 0x7C0802A6)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    blocks = hh.load_blocks(img)
    assert target in hh.harvest_codeptr(blocks)
    accepted, _ = hh.classify(blocks, {target: {"codeptr"}}, set(), set())
    assert target in accepted


def test_codeptr_register_mismatch_ignored(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">III", text, 0x10, _lis(3, 0x8222),
                     _addi(4, 5, 0x0040), _ori(6, 5, 0x0040))
    struct.pack_into(">II", text, 0x3C, 0x4E800020, 0x7C0802A6)
    img = tmp_path / "image"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    assert hh.harvest_codeptr(hh.load_blocks(img)) == []


def test_hint_fixpoint_owner_rule(tmp_path: Path) -> None:
    import hint_fixpoint as hf

    gen = tmp_path / "gen"
    build_generated(gen, [0x82230368, 0x82230390], [])
    hints = tmp_path / "hints.toml"
    hints.write_text("[functions]\n0x82230390 = {}  # codeptr\n")
    logf = tmp_path / "codegen.stdout"
    logf.write_text("Unresolved conditional branch to 0x82230560 from 0x82230554\n")
    pairs = hf.parse_unresolved(logf.read_text())
    assert pairs == [(0x82230554, 0x82230560)]
    assert hf.find_culprits(pairs, [0x82230390]) == {}
    funcs = hf.load_registered(gen)
    assert funcs == [0x82230368, 0x82230390]
    assert hf.find_owner_culprits(pairs, {0x82230390}, funcs) == {
        0x82230390: (0x82230554, 0x82230560)}
    deny = tmp_path / "deny.txt"
    deny.write_text("# denylist\n")
    argv = ["--codegen-log", str(logf), "--hints", str(hints),
           "--deny", str(deny), "--generated", str(gen)]
    assert hf.main(argv) == 1
    content = deny.read_text()
    assert "0x82230390 # owns 0x82230554->0x82230560" in content
    assert hf.main(argv) == 0
    assert deny.read_text().count("0x82230390") == 1


def test_inside_span_rejected(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">II", text, 0x1C, 0x4E800020, 0x7C0802A6)
    img, gen = tmp_path / "image", tmp_path / "gen"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    build_generated(gen, [], [TEXT + 0x40])
    blocks = hh.load_blocks(img)
    spans = hh.load_spans(gen)
    assert spans == [(TEXT, TEXT + 0x40)]
    accepted, rejects = hh.classify(blocks, {TEXT + 0x20: {"codeptr"}}, set(), set(), spans=spans)
    assert accepted == {}
    assert rejects["inside_span"] == 1
    accepted, _ = hh.classify(blocks, {TEXT + 0x20: {"pdata"}}, set(), set(), spans=spans)
    assert TEXT + 0x20 in accepted


def test_after_last_label_accepted(tmp_path: Path) -> None:
    text = bytearray(0x100)
    struct.pack_into(">II", text, 0x5C, 0x4E800020, 0x7C0802A6)
    img, gen = tmp_path / "image", tmp_path / "gen"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    build_generated(gen, [], [TEXT + 0x40])
    blocks = hh.load_blocks(img)
    spans = hh.load_spans(gen)
    accepted, rejects = hh.classify(blocks, {TEXT + 0x60: {"codeptr"}}, set(), set(), spans=spans)
    assert accepted == {TEXT + 0x60: {"codeptr"}}
    assert rejects["inside_span"] == 0


def test_sources_excluding_codeptr_drops_codeptr_only(tmp_path: Path) -> None:
    assert hh.parse_args([]).sources == "thunk,pdata,datascan,codeptr,reloc"
    text = bytearray(0x9000)
    target = TEXT + 0x8040
    struct.pack_into(">II", text, 0x10, _lis(3, 0x8223), _addi(4, 3, 0x8040))
    struct.pack_into(">II", text, 0x803C, 0x4E800020, 0x7C0802A6)
    img, gen = tmp_path / "image", tmp_path / "gen"
    build_image(img, std_blocks(bytes(0x100), bytes(text)))
    build_generated(gen, [], [])
    out_all = tmp_path / "all.toml"
    assert hh.main(["--image", str(img), "--generated", str(gen), "--out", str(out_all)]) == 0
    assert f"0x{target:08X}" in out_all.read_text()
    out_filtered = tmp_path / "nocodeptr.toml"
    argv = ["--image", str(img), "--generated", str(gen), "--out", str(out_filtered),
           "--sources", "thunk,pdata,datascan,reloc"]
    assert hh.main(argv) == 0
    assert f"0x{target:08X}" not in out_filtered.read_text()
