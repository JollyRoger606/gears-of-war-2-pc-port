# Tests for tools/patch_memory_barriers.py. All inputs are synthetic; no game code needed.
import collections
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import patch_memory_barriers as pmb


def test_sync_inserts_seq_cst_fence_before_next_instruction():
    src = "\t// sync \n\t// add r3, r4\n"
    out, counts = pmb.patch_source(src)
    assert out == (
        "\t// sync \n"
        "\tstd::atomic_thread_fence(std::memory_order_seq_cst);  // GOW2 MEMBARRIER sync\n"
        "\t// add r3, r4\n"
    )
    assert counts == {("sync", "insert"): 1}


@pytest.mark.parametrize(
    ("marker", "mnemonic", "operands", "order"),
    [
        ("\t// lwsync \n", "lwsync", "", "acq_rel"),
        ("\t// eieio \n", "eieio", "", "seq_cst"),
        ("\t// ptesync \n", "ptesync", "", "seq_cst"),
        ("\t// sync 1\n", "sync", "1", "acq_rel"),
        ("\t// sync 0\n", "sync", "0", "seq_cst"),
    ],
)
def test_barrier_orders(marker, mnemonic, operands, order):
    assert pmb.fence_order(mnemonic, operands) == order
    out, counts = pmb.patch_source(marker + "\t// add r3, r4\n")
    assert f"\tstd::atomic_thread_fence(std::memory_order_{order});  // GOW2 MEMBARRIER {mnemonic}\n" in out
    assert counts == {(mnemonic, "insert"): 1}


def test_isync_unimplemented_pair_is_replaced():
    src = (
        "\t// isync \n"
        "\t// UNIMPLEMENTED: isync\n"
        '\tREX_UNIMPLEMENTED(0x8222A0B4, "isync");\n'
        "\t// add r3, r4\n"
    )
    out, counts = pmb.patch_source(src)
    assert "REX_UNIMPLEMENTED" not in out
    assert out == (
        "\t// isync \n"
        "\tstd::atomic_thread_fence(std::memory_order_acquire);  // GOW2 MEMBARRIER isync\n"
        "\t// add r3, r4\n"
    )
    assert counts[("isync", "replace")] == 1


def test_isync_without_pair_gets_inserted_fence():
    src = "\t// isync \n\t// add r3, r4\n"
    out, counts = pmb.patch_source(src)
    assert out == (
        "\t// isync \n"
        "\tstd::atomic_thread_fence(std::memory_order_acquire);  // GOW2 MEMBARRIER isync\n"
        "\t// add r3, r4\n"
    )
    assert counts == {("isync", "insert"): 1}


def test_fence_goes_between_marker_and_label():
    src = "\t// lwsync \nloc_82310504:\n"
    out, counts = pmb.patch_source(src)
    assert out == (
        "\t// lwsync \n"
        "\tstd::atomic_thread_fence(std::memory_order_acq_rel);  // GOW2 MEMBARRIER lwsync\n"
        "loc_82310504:\n"
    )
    assert counts == {("lwsync", "insert"): 1}


def test_patching_twice_is_idempotent():
    src = (
        "\t// sync \n"
        "\t// add r3, r4\n"
        "\t// isync \n"
        "\t// UNIMPLEMENTED: isync\n"
        '\tREX_UNIMPLEMENTED(0x1, "isync");\n'
    )
    once, _ = pmb.patch_source(src)
    twice, second = pmb.patch_source(once)
    assert twice == once
    assert len(second) > 0
    assert all(action == "present" for _, action in second)
    assert pmb.verify(once, twice) == []


def test_fence_already_present_without_marker_is_left_alone():
    src = "\t// sync \n\tstd::atomic_thread_fence(std::memory_order_seq_cst);\n"
    out, counts = pmb.patch_source(src)
    assert out == src
    assert counts == {("sync", "present"): 1}


def test_crlf_newlines_are_preserved():
    src = "\t// lwsync \r\n\t// stw r29,8(r30)\r\n"
    out, counts = pmb.patch_source(src)
    assert "\n" not in out.replace("\r\n", "")
    assert "\tstd::atomic_thread_fence(std::memory_order_acq_rel);  // GOW2 MEMBARRIER lwsync\r\n" in out
    assert counts == {("lwsync", "insert"): 1}


def test_crlf_isync_pair_replacement_keeps_crlf():
    src = "\t// isync \r\n\t// UNIMPLEMENTED: isync\r\n\tREX_UNIMPLEMENTED(0x10, \"isync\");\r\n"
    out, counts = pmb.patch_source(src)
    assert "\n" not in out.replace("\r\n", "")
    assert "REX_UNIMPLEMENTED" not in out
    assert out.endswith("// GOW2 MEMBARRIER isync\r\n")
    assert counts == {("isync", "replace"): 1}


def test_marker_as_last_line_without_newline_gets_own_line():
    src = "\t// add r3, r4\n\t// sync "
    out, counts = pmb.patch_source(src)
    assert out == src + "\n\tstd::atomic_thread_fence(std::memory_order_seq_cst);  // GOW2 MEMBARRIER sync"
    assert counts == {("sync", "insert"): 1}


@pytest.mark.parametrize(
    "src",
    [
        "\t// syncfoo\n",
        "\t// lwsyncs r3\n",
        "// sync \n",
        "\t\t// sync \n",
        "\tctx.r3.u64 = 0; // sync \n",
        "\t// UNIMPLEMENTED: sync\n",
    ],
)
def test_lookalikes_are_untouched(src):
    out, counts = pmb.patch_source(src)
    assert out == src
    assert not counts


def test_verify_accepts_correct_patch():
    src = "\t// sync \n\t// add r3, r4\n"
    patched, _ = pmb.patch_source(src)
    assert pmb.verify(src, patched) == []


def test_verify_reports_deleted_fence():
    src = "\t// sync \n\t// add r3, r4\n"
    patched, _ = pmb.patch_source(src)
    broken = patched.replace("\tstd::atomic_thread_fence(std::memory_order_seq_cst);  // GOW2 MEMBARRIER sync\n", "")
    assert broken != patched
    problems = pmb.verify(src, broken)
    assert problems == ["barrier at line 1 has no fence"]


@pytest.mark.parametrize(
    "src",
    [
        "\t// add r3, r4\n\t// sync ",
        "\t// isync \n\t// UNIMPLEMENTED: isync\n\tREX_UNIMPLEMENTED(0x1, \"isync\");",
        "\t// add r3, r4\r\n\t// lwsync ",
    ],
)
def test_verify_accepts_fences_added_at_end_of_file(src):
    patched, _ = pmb.patch_source(src)
    assert patched != src
    assert pmb.verify(src, patched) == []


def test_verify_reports_unrelated_change():
    src = "\t// sync \n\tctx.r3.u64 = 0;\n"
    patched, _ = pmb.patch_source(src)
    assert pmb.verify(src, patched) == []
    altered = patched.replace("ctx.r3", "ctx.r4")
    problems = pmb.verify(src, altered)
    assert "text other than fences changed" in problems


def _write_cpp_files(tmp_path):
    barrier = tmp_path / "with_barriers.cpp"
    plain = tmp_path / "without_barriers.cpp"
    barrier.write_text(
        "\t// lwsync \n\t// stw r29,8(r30)\n\t// sync \n\t// add r3, r4\n",
        encoding="utf-8",
    )
    plain.write_text("\t// add r3, r4\n", encoding="utf-8")
    return barrier, plain


def test_main_dry_run_writes_nothing(tmp_path, capsys):
    barrier, plain = _write_cpp_files(tmp_path)
    before_barrier = barrier.read_bytes()
    before_plain = plain.read_bytes()
    rc = pmb.main(["--dry-run", str(tmp_path / "*.cpp")])
    assert rc == 0
    assert barrier.read_bytes() == before_barrier
    assert plain.read_bytes() == before_plain
    out = capsys.readouterr().out
    assert "dry run, nothing written" in out
    assert "  lwsync       1 site(s): 1 insert, 0 replace, 0 present" in out
    assert "would change" in out


def test_main_writes_then_second_run_is_all_present(tmp_path, capsys):
    barrier, plain = _write_cpp_files(tmp_path)
    before_barrier = barrier.read_text(encoding="utf-8")
    before_plain = plain.read_bytes()
    assert pmb.main([str(tmp_path / "*.cpp")]) == 0
    expected, _ = pmb.patch_source(before_barrier)
    assert barrier.read_text(encoding="utf-8") == expected
    assert plain.read_bytes() == before_plain
    capsys.readouterr()
    assert pmb.main([str(tmp_path / "*.cpp")]) == 0
    out = capsys.readouterr().out
    assert out.count("0 insert") == 5


def test_main_with_empty_glob_returns_1(tmp_path, capsys):
    rc = pmb.main([str(tmp_path / "nothing_matches*.cpp")])
    assert rc == 1
    assert "no sources matched" in capsys.readouterr().err


def test_main_verify_failure_writes_nothing(tmp_path, capsys, monkeypatch):
    target = tmp_path / "broken.cpp"
    target.write_text("\t// sync \n\t// add r3, r4\n", encoding="utf-8")
    before = target.read_bytes()

    def fake_patch_source(text):
        return text + "corrupt\n", collections.Counter()

    monkeypatch.setattr(pmb, "patch_source", fake_patch_source)
    assert pmb.main([str(target)]) == 2
    assert target.read_bytes() == before
    assert "ERROR" in capsys.readouterr().err


def test_verify_and_main_report_ptesync_unimplemented_pair(tmp_path, capsys):
    src = (
        "\t// ptesync \n"
        "\t// UNIMPLEMENTED: ptesync\n"
        '\tREX_UNIMPLEMENTED(0x1, "ptesync");\n'
        "\t// add r3, r4\n"
    )
    patched, _ = pmb.patch_source(src)
    assert "REX_UNIMPLEMENTED" in patched
    problems = pmb.verify(src, patched)
    assert any("ptesync" in problem and "UNIMPLEMENTED" in problem for problem in problems)
    target = tmp_path / "ptesync.cpp"
    target.write_text(src, encoding="utf-8")
    before = target.read_bytes()
    assert pmb.main([str(target)]) == 2
    assert target.read_bytes() == before
    assert "ERROR" in capsys.readouterr().err


def test_verify_and_main_report_isync_pair_separated_by_code(tmp_path, capsys):
    src = (
        "\t// isync \n"
        "\t// UNIMPLEMENTED: isync\n"
        "\tctx.r3.u64 = 0;\n"
        '\tREX_UNIMPLEMENTED(0x1, "isync");\n'
    )
    patched, _ = pmb.patch_source(src)
    assert "REX_UNIMPLEMENTED" in patched
    problems = pmb.verify(src, patched)
    assert any("isync" in problem and "UNIMPLEMENTED" in problem for problem in problems)
    target = tmp_path / "separated.cpp"
    target.write_text(src, encoding="utf-8")
    before = target.read_bytes()
    assert pmb.main([str(target)]) == 2
    assert target.read_bytes() == before
    assert "ERROR" in capsys.readouterr().err
