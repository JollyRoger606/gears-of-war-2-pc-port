import collections
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import patch_spin_hints as psh


@pytest.mark.parametrize("newline", ["\n", "\r\n"])
def test_db16cyc_inserts_pause_with_original_line_endings(newline):
    source = f"\t// db16cyc {newline}\t// add r3,r4{newline}"
    patched, counts = psh.patch_source(source)
    assert patched == (
        f"\t// db16cyc {newline}"
        f"{psh.pause_line('db16cyc', newline)}"
        f"\t// add r3,r4{newline}"
    )
    assert patched.index("#if defined(__aarch64__)") < patched.index("__builtin_arm_isb(15);")
    assert patched.index("__builtin_arm_isb(15);") < patched.index("#endif")
    assert counts == {("db16cyc", "insert"): 1}
    assert psh.verify(source, patched) == []


@pytest.mark.parametrize("mnemonic", ["db8cyc", "db10cyc", "db12cyc", "db14cyc", "db16cyc"])
def test_every_delay_hint_is_patched_before_following_label(mnemonic):
    source = f"\t// {mnemonic} \nloc_1234:\n"
    patched, counts = psh.patch_source(source)
    assert patched == (
        f"\t// {mnemonic} \n"
        + psh.pause_line(mnemonic, "\n")
        + "loc_1234:\n"
    )
    assert counts == {(mnemonic, "insert"): 1}


@pytest.mark.parametrize("pause", [
    "\t__builtin_arm_isb(15);  // GOW2 SPINHINT db16cyc\n",
    "\t__builtin_arm_isb(15);\n",
])
def test_existing_pause_is_unchanged_and_counted_present(pause):
    source = "\t// db16cyc \n" + pause
    patched, counts = psh.patch_source(source)
    assert patched == source
    assert counts == {("db16cyc", "present"): 1}
    assert psh.verify(source, patched) == []


def test_legacy_pause_is_present_and_not_wrapped():
    source = "\t// db16cyc \n\t__builtin_arm_isb(15);  // GOW2 SPINHINT db16cyc\n"
    patched, counts = psh.patch_source(source)
    assert patched == source
    assert counts == {("db16cyc", "present"): 1}


@pytest.mark.parametrize("newline", ["\n", "\r\n"])
def test_guarded_pause_is_idempotent(newline):
    source = f"\t// db16cyc {newline}"
    patched, first_counts = psh.patch_source(source)
    twice, second_counts = psh.patch_source(patched)
    assert twice == patched
    assert first_counts == {("db16cyc", "insert"): 1}
    assert second_counts == {("db16cyc", "present"): 1}


@pytest.mark.parametrize("prefix,newline", [("", "\n"), ("int x;\n", "\n"), ("int x;\r\n", "\r\n")])
def test_final_unterminated_marker_gets_pause_and_verifies(prefix, newline):
    source = prefix + "\t// db16cyc "
    patched, counts = psh.patch_source(source)
    block = psh.pause_line("db16cyc", newline)[:-len(newline)]
    assert patched == source + newline + block
    assert counts == {("db16cyc", "insert"): 1}
    assert psh.verify(source, patched) == []
    twice, second_counts = psh.patch_source(patched)
    assert twice == patched
    assert second_counts == {("db16cyc", "present"): 1}


@pytest.mark.parametrize("source", [
    "\t// db16cycx\n", "\t// cctpl \n", "// db16cyc \n",
    "\t\t// db16cyc \n", "\tctx.r3.u64 = 0; // db16cyc \n",
])
def test_unrelated_comments_are_untouched(source):
    patched, counts = psh.patch_source(source)
    assert patched == source
    assert not counts


def test_verify_reports_removed_pause():
    source = "\t// db16cyc \n\tctx.r3.u64 = 0;\n"
    patched, _ = psh.patch_source(source)
    broken = patched.replace(psh.pause_line("db16cyc", "\n"), "")
    assert psh.verify(source, broken) == ["delay hint at line 1 has no pause"]


def test_verify_reports_changed_non_pause_line():
    source = "\t// db16cyc \n\tctx.r3.u64 = 0;\n"
    patched, _ = psh.patch_source(source)
    altered = patched.replace("ctx.r3", "ctx.r4")
    assert psh.verify(source, altered) == ["text other than spin pauses changed"]


def test_verify_reports_changed_guarded_pause():
    source = "\t// db16cyc \n"
    patched, _ = psh.patch_source(source)
    broken = patched.replace("#endif\n", "")
    assert psh.verify(source, broken) == ["text other than spin pauses changed"]


def test_main_dry_run_writes_nothing(tmp_path, capsys):
    target = tmp_path / "delay.cpp"
    target.write_bytes(b"\t// db16cyc \r\nloc_1234:\r\n")
    before = target.read_bytes()
    assert psh.main(["--dry-run", str(tmp_path / "*.cpp")], allowed_root=tmp_path) == 0
    assert target.read_bytes() == before
    assert not list(tmp_path.glob("*.tmp"))
    output = capsys.readouterr().out
    assert "dry run, nothing written" in output
    assert "db16cyc      1 site(s): 1 insert, 0 present" in output


def test_main_writes_then_second_run_changes_nothing(tmp_path, capsys):
    target = tmp_path / "delay.cpp"
    plain = tmp_path / "plain.cpp"
    source = "\t// db16cyc \r\nloc_1234:\r\n"
    target.write_bytes(source.encode())
    plain.write_bytes(b"int x;\n")
    assert psh.main([str(tmp_path / "*.cpp")], allowed_root=tmp_path) == 0
    assert target.read_bytes() == psh.patch_source(source)[0].encode()
    assert plain.read_bytes() == b"int x;\n"
    capsys.readouterr()
    before = target.read_bytes()
    timestamp = target.stat().st_mtime_ns
    assert psh.main([str(tmp_path / "*.cpp")], allowed_root=tmp_path) == 0
    assert target.read_bytes() == before
    assert target.stat().st_mtime_ns == timestamp
    output = capsys.readouterr().out
    assert "0 file(s) written" in output
    assert "db16cyc      1 site(s): 0 insert, 1 present" in output


def test_main_verify_failure_does_not_write_any_files(tmp_path, capsys, monkeypatch):
    clean = tmp_path / "a.cpp"
    broken = tmp_path / "b.cpp"
    clean.write_bytes(b"\t// db16cyc \n")
    broken.write_bytes(b"int broken;\n")
    before = {path: path.read_bytes() for path in (clean, broken)}
    patch_source = psh.patch_source

    def corrupt_second_source(text):
        if "broken" in text:
            return text + "corrupt\n", collections.Counter()
        return patch_source(text)

    monkeypatch.setattr(psh, "patch_source", corrupt_second_source)
    assert psh.main([str(tmp_path / "*.cpp")], allowed_root=tmp_path) == 2
    assert all(path.read_bytes() == text for path, text in before.items())
    assert "ERROR" in capsys.readouterr().err


def test_main_with_empty_glob_reports_no_sources(tmp_path, capsys):
    assert psh.main([str(tmp_path / "*.cpp")], allowed_root=tmp_path) == 1
    assert "no sources matched" in capsys.readouterr().err


def test_main_rejects_non_cpp_arguments_before_writing(tmp_path, capsys):
    target = tmp_path / "delay.h"
    target.write_bytes(b"\t// db16cyc \n")
    before = target.read_bytes()
    assert psh.main([str(target)], allowed_root=tmp_path) == 1
    assert target.read_bytes() == before
    assert "only .cpp sources" in capsys.readouterr().err


def test_main_without_arguments_uses_argparse_and_writes_nothing(tmp_path, capsys):
    with pytest.raises(SystemExit) as error:
        psh.main([], allowed_root=tmp_path)
    assert error.value.code == 2
    assert "usage:" in capsys.readouterr().err
    assert list(tmp_path.iterdir()) == []


def test_main_refuses_path_outside_allowed_root(tmp_path, capsys):
    target = tmp_path / "delay.cpp"
    allowed_root = tmp_path / "android"
    allowed_root.mkdir()
    target.write_bytes(b"\t// db16cyc \n")
    before = target.read_bytes()
    assert psh.main([str(target)], allowed_root=allowed_root) == 3
    assert target.read_bytes() == before
    assert "refusing" in capsys.readouterr().err


@pytest.mark.skipif(shutil.which("clang++") is None and shutil.which("clang") is None,
                    reason="clang is not available")
def test_guarded_pause_compiles_for_x64(tmp_path):
    compiler = shutil.which("clang++") or shutil.which("clang")
    source = "\t// db16cyc \n"
    patched, _ = psh.patch_source(source)
    target = tmp_path / "spin_hint.cpp"
    target.write_text("void spin_hint() {\n" + patched + "}\n", encoding="utf-8", newline="")
    result = subprocess.run(
        [
            compiler,
            "-fsyntax-only",
            "-x",
            "c++",
            "--target=x86_64-pc-windows-msvc",
            str(target),
        ],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
