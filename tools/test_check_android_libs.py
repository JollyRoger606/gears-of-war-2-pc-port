# Tests for tools/check_android_libs.py. All inputs are synthetic; no NDK needed.
import sys
import zipfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_android_libs as cal

DYNAMIC_SAMPLE = """\
 0x0000000000000001 (NEEDED)             Shared library: [libSDL3.so]
 0x0000000000000001 (NEEDED)             Shared library: [libc++_shared.so]
 0x000000000000000e (SONAME)             Library soname: [librexruntime.so]
"""

HEADERS_SAMPLE = """\
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  PHDR           0x000040 0x0000000000000040 0x0000000000000040 0x000230 0x000230 R   0x8
  LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x865420 0x865420 R E 0x4000
  LOAD           0x865420 0x0000000000869420 0x0000000000869420 0x043130 0x043be0 RW  0x1000
  GNU_STACK      0x000000 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x0
"""

ELF_HEADER_SAMPLE = """\
  Class:                             ELF64
  Type:                              DYN (Shared object file)
  Machine:                           AArch64
"""

NM_SAMPLE = """\
SDL_Init T 1f2a0 3c
SDL_main T 4000 10
malloc@LIBC U
__cxa_finalize@LIBC U
__gmon_start__ w
_ZTVN10__cxxabiv117__class_type_infoE@@ U
rex_thing D 9000 8
"""


def test_parse_needed_ignores_soname():
    assert cal.parse_needed(DYNAMIC_SAMPLE) == ["libSDL3.so", "libc++_shared.so"]


def test_parse_load_alignments_only_load_rows():
    assert cal.parse_load_alignments(HEADERS_SAMPLE) == [0x4000, 0x1000]


def test_check_alignment_flags_small_align_only():
    errors = cal.check_alignment("libx.so", [0x4000, 0x1000])
    assert len(errors) == 1
    assert "0x1000" in errors[0]
    assert cal.check_alignment("libx.so", [0x4000, 0x10000]) == []


def test_parse_elf_header():
    assert cal.parse_elf_header(ELF_HEADER_SAMPLE) == {
        "class": "ELF64", "machine": "AArch64", "type": "DYN"}


def test_parse_nm_dynamic():
    defined, undefined = cal.parse_nm_dynamic(NM_SAMPLE)
    assert {"SDL_Init", "SDL_main", "rex_thing"} <= defined
    assert undefined == {"malloc", "__cxa_finalize",
                         "_ZTVN10__cxxabiv117__class_type_infoE"}
    assert "__gmon_start__" not in defined
    assert "__gmon_start__" not in undefined


def test_strip_version():
    assert cal.strip_version("memcpy@LIBC") == "memcpy"
    assert cal.strip_version("foo@@V1") == "foo"
    assert cal.strip_version("plain") == "plain"


def test_needed_closure_is_bfs_and_handles_cycles():
    graph = {"a": ["b", "c"], "b": ["d"], "c": ["d"], "d": ["a"]}
    assert cal.needed_closure("a", graph) == ["a", "b", "c", "d"]
    assert cal.needed_closure("a", {"a": ["zzz"]}) == ["a", "zzz"]


def test_unresolved_symbols_scope():
    needed = {"app.so": ["mid.so"], "mid.so": ["base.so"]}
    defined = {"mid.so": {"mid_fn"}, "base.so": {"base_fn"}, "outsider.so": {"lost_fn"}}
    assert cal.unresolved_symbols("app.so", needed, defined, {"base_fn"}) == []
    assert cal.unresolved_symbols("app.so", needed, defined, {"lost_fn"}) == ["lost_fn"]


def test_check_needed():
    packaged = {"libSDL3.so", "libgow2.so"}
    allowed = {"libc.so"}
    errors = cal.check_needed("libgow2.so", ["libSDL3.so", "libc.so"], packaged, allowed)
    assert errors == []
    errors = cal.check_needed("libgow2.so", ["libfoo.so"], packaged, allowed)
    assert len(errors) == 1
    assert errors[0] == ("libgow2.so: needs libfoo.so, "
                         "which is neither packaged nor an allowed system library")


def test_check_single_sdl():
    defined = {"libSDL3.so": {"SDL_Init", "JNI_OnLoad"},
               "libgow2.so": {"SDL_main", "JNI_OnLoad"},
               "librexruntime.so": {"rex_run"}}
    assert cal.check_single_sdl(defined, "libSDL3.so", "libgow2.so", "SDL_main") == []
    bad = dict(defined)
    bad["librexruntime.so"] = {"SDL_Init"}
    errors = cal.check_single_sdl(bad, "libSDL3.so", "libgow2.so", "SDL_main")
    assert any("librexruntime.so" in error and "SDL_Init" in error for error in errors)
    named = dict(defined)
    named["libSDL2compat.so"] = {"helper"}
    assert any("libSDL2compat.so" in error
               for error in cal.check_single_sdl(named, "libSDL3.so", "libgow2.so", "SDL_main"))
    java = dict(defined)
    java["librexruntime.so"] = {"Java_org_libsdl_app_SDLActivity_nativeInit"}
    assert cal.check_single_sdl(java, "libSDL3.so", "libgow2.so", "SDL_main") != []


def test_check_main_symbol():
    assert cal.check_main_symbol("SDL_main T 4000 10\n", "SDL_main") == []
    assert cal.check_main_symbol("SDL_Init T 1f2a0 3c\n", "SDL_main") != []
    assert cal.check_main_symbol("SDL_main U\n", "SDL_main") != []


def test_apk_flags_unaligned_stored_entry(tmp_path):
    apk = tmp_path / "app.apk"
    with zipfile.ZipFile(apk, "w") as archive:
        archive.writestr("lib/arm64-v8a/liba.so", b"\x7fELF" + b"\x00" * 100,
                         compress_type=zipfile.ZIP_STORED)
    errors = cal.apk_alignment_errors(apk)
    assert len(errors) == 1
    assert "liba.so" in errors[0]


def test_apk_flags_deflated_entry(tmp_path):
    apk = tmp_path / "app.apk"
    with zipfile.ZipFile(apk, "w") as archive:
        archive.writestr("lib/arm64-v8a/liba.so", b"\x7fELF" + b"\x00" * 100,
                         compress_type=zipfile.ZIP_DEFLATED)
    errors = cal.apk_alignment_errors(apk)
    assert any("ZIP_STORED" in error for error in errors)


def test_apk_accepts_aligned_stored_entry(tmp_path):
    apk = tmp_path / "app.apk"
    name = "lib/arm64-v8a/liba.so"
    # First entry lives at header offset 0, so pad the extra field until the
    # data offset itself is a 16384 multiple.
    info = zipfile.ZipInfo(name)
    info.compress_type = zipfile.ZIP_STORED
    info.extra = b"\x00" * (16384 - 30 - len(name.encode()))
    with zipfile.ZipFile(apk, "w") as archive:
        archive.writestr(info, b"\x7fELF" + b"\x00" * 100)
        archive.writestr("lib/armeabi-v7a/other.so", b"ignored",
                         compress_type=zipfile.ZIP_DEFLATED)
    assert cal.apk_alignment_errors(apk) == []


def test_a_failing_tool_is_an_error_not_an_empty_result():
    with pytest.raises(cal.ToolError, match="exited with 4"):
        cal._run(sys.executable, "-c", "import sys; sys.exit(4)")


def test_a_missing_tool_is_an_error():
    with pytest.raises(cal.ToolError):
        cal._run("no-such-llvm-nm-anywhere", "-D")


def _write_build_gradle(repo, ndk_version="30.0.14904198", min_sdk="29"):
    gradle = repo / "android" / "app" / "build.gradle"
    gradle.parent.mkdir(parents=True, exist_ok=True)
    gradle.write_text("android {\n"
                      f"    ndkVersion = '{ndk_version}'\n"
                      "    defaultConfig {\n"
                      f"        minSdk = {min_sdk}\n"
                      "    }\n"
                      "}\n", encoding="utf-8")
    return repo


def _clear_android_env(monkeypatch):
    for name in ("ANDROID_HOME", "ANDROID_SDK_ROOT", "ANDROID_NDK_HOME", "LOCALAPPDATA"):
        monkeypatch.delenv(name, raising=False)


def test_read_gradle_value():
    assert cal.read_gradle_value("    ndkVersion = '30.0.14904198'\n", "ndkVersion") == "30.0.14904198"
    assert cal.read_gradle_value('    ndkVersion = "30.0.14904198"\n', "ndkVersion") == "30.0.14904198"
    assert cal.read_gradle_value("        minSdk = 29\n", "minSdk") == "29"
    assert cal.read_gradle_value("        minSdk = 29\n", "ndkVersion") == ""
    assert cal.read_gradle_value("", "minSdk") == ""


def test_default_api_reads_min_sdk(tmp_path):
    _write_build_gradle(tmp_path, min_sdk="30")
    assert cal.default_api(tmp_path) == "30"


def test_default_api_falls_back_without_gradle(tmp_path):
    assert cal.default_api(tmp_path) == "29"


def test_ndk_revision_and_major(tmp_path):
    ndk = tmp_path / "ndk" / "30.0.14904198"
    ndk.mkdir(parents=True)
    (ndk / "source.properties").write_text("Pkg.Desc = Android NDK\nPkg.Revision = 30.0.14904198\n",
                                            encoding="utf-8")
    assert cal.ndk_revision(ndk) == "30.0.14904198"
    assert cal.ndk_major(ndk) == 30
    assert cal.ndk_revision(tmp_path / "missing") == ""
    assert cal.ndk_major(tmp_path / "missing") == 0


def _write_ndk(ndk, text):
    ndk.mkdir(parents=True)
    (ndk / "source.properties").write_text(text, encoding="utf-8")
    return ndk


def test_ndk_base_revision_prefers_base_revision(tmp_path):
    ndk = _write_ndk(tmp_path / "ndk",
                     "Pkg.Desc = Android NDK\n"
                     "Pkg.Revision = 30.0.14904198-beta1\n"
                     "Pkg.BaseRevision = 30.0.14904198\n"
                     "Pkg.ReleaseName = r30-beta1\n")
    assert cal.ndk_base_revision(ndk) == "30.0.14904198"


def test_ndk_base_revision_strips_suffix_without_base(tmp_path):
    ndk = _write_ndk(tmp_path / "ndk", "Pkg.Revision = 30.0.14904198-beta1\n")
    assert cal.ndk_base_revision(ndk) == "30.0.14904198"


def test_ndk_base_revision_missing_file(tmp_path):
    assert cal.ndk_base_revision(tmp_path / "missing") == ""


def test_pinned_ndk_warning_silent_for_prerelease_of_pin(tmp_path):
    ndk = _write_ndk(tmp_path / "ndk",
                     "Pkg.Desc = Android NDK\n"
                     "Pkg.Revision = 30.0.14904198-beta1\n"
                     "Pkg.BaseRevision = 30.0.14904198\n"
                     "Pkg.ReleaseName = r30-beta1\n")
    assert cal.pinned_ndk_warning(ndk, "30.0.14904198") == ""


def test_pinned_ndk_warning_for_other_ndk(tmp_path):
    ndk = _write_ndk(tmp_path / "ndk", "Pkg.Revision = 29.0.13113456\n")
    message = cal.pinned_ndk_warning(ndk, "30.0.14904198")
    assert "29.0.13113456" in message
    assert "30.0.14904198" in message


def test_pinned_ndk_warning_without_pin(tmp_path):
    ndk = _write_ndk(tmp_path / "ndk", "Pkg.Revision = 29.0.13113456\n")
    assert cal.pinned_ndk_warning(ndk, "") == ""


def test_default_ndk_dir_prefers_pinned_over_newer_and_env(tmp_path, monkeypatch):
    _clear_android_env(monkeypatch)
    _write_build_gradle(tmp_path)
    sdk = tmp_path / "sdk"
    pinned = sdk / "ndk" / "30.0.14904198"
    pinned.mkdir(parents=True)
    (sdk / "ndk" / "31.0.12345678").mkdir(parents=True)
    ndk_home = tmp_path / "ndk-home"
    ndk_home.mkdir()
    monkeypatch.setenv("ANDROID_HOME", str(sdk))
    monkeypatch.setenv("ANDROID_NDK_HOME", str(ndk_home))
    assert cal.default_ndk_dir(tmp_path) == str(pinned)


def test_default_ndk_dir_falls_back_to_ndk_home(tmp_path, monkeypatch):
    _clear_android_env(monkeypatch)
    _write_build_gradle(tmp_path)
    sdk = tmp_path / "sdk"
    (sdk / "ndk" / "31.0.12345678").mkdir(parents=True)
    ndk_home = tmp_path / "ndk-home"
    ndk_home.mkdir()
    monkeypatch.setenv("ANDROID_HOME", str(sdk))
    monkeypatch.setenv("ANDROID_NDK_HOME", str(ndk_home))
    assert cal.default_ndk_dir(tmp_path) == str(ndk_home)


def test_default_ndk_dir_falls_back_to_newest(tmp_path, monkeypatch):
    _clear_android_env(monkeypatch)
    _write_build_gradle(tmp_path)
    sdk = tmp_path / "sdk"
    (sdk / "ndk" / "29.0.13599879").mkdir(parents=True)
    newest = sdk / "ndk" / "31.0.12345678"
    newest.mkdir(parents=True)
    monkeypatch.setenv("ANDROID_HOME", str(sdk))
    assert cal.default_ndk_dir(tmp_path) == str(newest)


def test_default_ndk_dir_empty_without_sdks(tmp_path, monkeypatch):
    _clear_android_env(monkeypatch)
    assert cal.default_ndk_dir(tmp_path) == ""


def test_android_sdk_roots_order_and_dedup(tmp_path, monkeypatch):
    _clear_android_env(monkeypatch)
    home = tmp_path / "home-sdk"
    home.mkdir()
    props_sdk = tmp_path / "props-sdk"
    props_sdk.mkdir()
    local_base = tmp_path / "localappdata"
    local_sdk = local_base / "Android" / "Sdk"
    local_sdk.mkdir(parents=True)
    (tmp_path / "android").mkdir()
    (tmp_path / "android" / "local.properties").write_text(f"sdk.dir={props_sdk}\n", encoding="utf-8")
    monkeypatch.setenv("ANDROID_HOME", str(home))
    monkeypatch.setenv("ANDROID_SDK_ROOT", str(home))
    monkeypatch.setenv("LOCALAPPDATA", str(local_base))
    assert cal.android_sdk_roots(tmp_path) == [home, props_sdk, local_sdk]


def test_android_sdk_roots_skips_missing(tmp_path, monkeypatch):
    _clear_android_env(monkeypatch)
    monkeypatch.setenv("ANDROID_HOME", str(tmp_path / "does-not-exist"))
    assert cal.android_sdk_roots(tmp_path) == []


def test_apk_notice_errors_present(tmp_path):
    apk = tmp_path / "app.apk"
    with zipfile.ZipFile(apk, "w") as archive:
        archive.writestr("assets/licenses/THIRD_PARTY_NOTICES.md", "notices")
        archive.writestr("assets/licenses/LICENSE", "license")
    assert cal.apk_notice_errors(apk) == []


def test_apk_notice_errors_missing(tmp_path):
    apk = tmp_path / "app.apk"
    with zipfile.ZipFile(apk, "w") as archive:
        archive.writestr("lib/arm64-v8a/liba.so", b"data")
    errors = cal.apk_notice_errors(apk)
    assert len(errors) == 2
    assert any("THIRD_PARTY_NOTICES.md" in error for error in errors)
    assert any("assets/licenses/LICENSE" in error for error in errors)
