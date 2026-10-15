"""Static checks for the native libraries of an Android arm64 APK.

A release that only fails on the phone, with UnsatisfiedLinkError, a
missing symbol or a 16 KB page-alignment crash, is slow to debug, so this
script verifies the packaged .so files beforehand with the NDK's
llvm-readelf and llvm-nm, resolving symbols the way Android's linker does.
It also fails when libgow2.so exports a symbol other than SDL_main
(--main-exports lists the allowed ones).

It takes either a folder holding .so files (for example
out/android/jniLibs/arm64-v8a) or an .apk file, in which case the
lib/arm64-v8a/*.so entries are extracted to a temporary directory and the
zip itself is checked for uncompressed, page-aligned storage, and for the
license notices (assets/licenses/THIRD_PARTY_NOTICES.md and
assets/licenses/LICENSE, unless --no-notices is given):

  python tools/check_android_libs.py <folder-or-apk> [--ndk <ndk folder>]
      [--api 29] [--expect NAME ...] [--main libgow2.so]
      [--main-symbol SDL_main] [--main-exports <name> ...] [--sdl libSDL3.so]
      [--allow-system NAME ...] [--no-notices] [--forbid-path PATH ...]
      [--allow-build-paths]

Without --ndk the NDK matching android/app/build.gradle is preferred. The
search order is: <sdk>/ndk/<ndkVersion> for each SDK root, then the
ANDROID_NDK_HOME folder, then the newest NDK under any SDK root. SDK roots
are ANDROID_HOME, ANDROID_SDK_ROOT, sdk.dir in android/local.properties,
then %LOCALAPPDATA%\\Android\\Sdk. Without --api the minSdk from
android/app/build.gradle is used (29 when unset). NDKs older than r29 are
rejected; the pin is compared with the NDK's base revision
(Pkg.BaseRevision), so a pre-release of the pinned NDK
(30.0.14904198-beta1 for 30.0.14904198) does not warn.

It also fails when a library contains an absolute build path: the
repository root, the NDK folder or a --forbid-path (forward or backward
slashes, any case). android/gamelibs/CMakeLists.txt maps them away with
-ffile-prefix-map, so the libraries do not depend on where the checkout
lives; --allow-build-paths skips that check.

Every problem is printed as an 'ERROR <lib>: <problem>' line, followed by
a short summary. The exit code is 0 when all checks pass, 1 when any check
fails and 2 for usage problems such as a missing input or missing tool.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from collections import deque
from pathlib import Path

_PAGE_SIZE = 0x4000
# llvm-nm on a library of the recompiled game takes tens of seconds.
_TOOL_TIMEOUT_S = 900
_MAX_MISSING_SHOWN = 20
_COPY_CHUNK = 1 << 20
_APK_LIB_PREFIX = "lib/arm64-v8a/"
_APK_REQUIRED_ENTRIES = ("assets/licenses/THIRD_PARTY_NOTICES.md", "assets/licenses/LICENSE")
_REPO_ROOT = Path(__file__).resolve().parent.parent
_NEEDED_RE = re.compile(r"\(NEEDED\)\s*Shared library: \[(?P<name>[^\]]+)\]")
_HOSTS = ("windows-x86_64", "linux-x86_64", "darwin-x86_64")
_DEFAULT_EXPECT = (
    "libSDL3.so",
    "librexruntime.so",
    "librexgpu-xenos.so",
    "libgow2.so",
    "libc++_shared.so",
)
_DEFAULT_ALLOW_SYSTEM = (
    "libc.so",
    "libm.so",
    "libdl.so",
    "liblog.so",
    "libandroid.so",
    "libvulkan.so",
    "libz.so",
    "libEGL.so",
    "libGLESv2.so",
    "libGLESv3.so",
    "libOpenSLES.so",
    "libaaudio.so",
    "libmediandk.so",
    "libnativewindow.so",
    "libjnigraphics.so",
)


def strip_version(name: str) -> str:
    return name.split("@", 1)[0]


def parse_needed(readelf_dynamic_text: str) -> list[str]:
    return _NEEDED_RE.findall(readelf_dynamic_text)


def parse_load_alignments(readelf_program_headers_text: str) -> list[int]:
    alignments = []
    for line in readelf_program_headers_text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[0] == "LOAD":
            alignments.append(int(fields[-1], 16))
    return alignments


def parse_elf_header(readelf_header_text: str) -> dict:
    header = {"class": "", "machine": "", "type": ""}
    for line in readelf_header_text.splitlines():
        key, sep, value = line.partition(":")
        if sep and key.strip().lower() in header:
            header[key.strip().lower()] = value.strip().split()[0] if value.strip() else ""
    return header


def parse_nm_dynamic(nm_posix_text: str) -> tuple[set[str], set[str]]:
    defined: set[str] = set()
    undefined_strong: set[str] = set()
    for line in nm_posix_text.splitlines():
        fields = line.split()
        if len(fields) < 2 or len(fields[1]) != 1:
            continue
        name, kind = strip_version(fields[0]), fields[1]
        if kind == "U":
            undefined_strong.add(name)
        elif kind in ("w", "v"):
            continue
        else:
            defined.add(name)
    return defined, undefined_strong


def needed_closure(lib: str, needed_by_lib: dict[str, list[str]]) -> list[str]:
    seen = {lib}
    order = [lib]
    queue = deque([lib])
    while queue:
        for dep in needed_by_lib.get(queue.popleft(), []):
            if dep not in seen:
                seen.add(dep)
                order.append(dep)
                queue.append(dep)
    return order


def unresolved_symbols(lib: str, needed_by_lib: dict[str, list[str]],
                       defined_by_lib: dict[str, set[str]], undefined: set[str]) -> list[str]:
    provided: set[str] = set()
    for name in needed_closure(lib, needed_by_lib):
        provided |= defined_by_lib.get(name, set())
    return sorted(symbol for symbol in undefined if symbol not in provided)


def check_alignment(lib: str, alignments: list[int]) -> list[str]:
    errors = []
    for align in alignments:
        if align < _PAGE_SIZE or align % _PAGE_SIZE != 0:
            errors.append(f"{lib}: PT_LOAD alignment {align:#x} is not a 16 KB multiple")
    return errors


def check_needed(lib: str, needed: list[str], packaged: set[str],
                 allowed_system: set[str]) -> list[str]:
    return [f"{lib}: needs {name}, which is neither packaged nor an allowed system library"
            for name in needed if name not in packaged and name not in allowed_system]


def check_single_sdl(defined_by_lib: dict[str, set[str]], sdl_name: str,
                     main_name: str, main_symbol: str) -> list[str]:
    errors = []
    for lib in sorted(defined_by_lib):
        if lib != sdl_name and "SDL" in lib:
            errors.append(f"{lib}: file name contains 'SDL' but it is not the SDL library ({sdl_name})")
        if lib == sdl_name:
            continue
        for symbol in sorted(defined_by_lib[lib]):
            if symbol == "JNI_OnLoad":
                continue
            if symbol.startswith("SDL_"):
                if lib == main_name and symbol == main_symbol:
                    continue
                errors.append(f"{lib}: defines {symbol} but only {sdl_name} may define SDL_ symbols")
            elif symbol.startswith("Java_org_libsdl_app_"):
                errors.append(f"{lib}: defines {symbol} but only {sdl_name} may define Java_org_libsdl_app_ symbols")
    return errors


def check_main_symbol(nm_posix_text: str, main_symbol: str) -> list[str]:
    for line in nm_posix_text.splitlines():
        fields = line.split()
        if len(fields) >= 2 and strip_version(fields[0]) == main_symbol and fields[1] in ("T", "W", "i"):
            return []
    return [f"{main_symbol}: not exported as a defined function (expected type T, W or i)"]


def path_spellings(path: str) -> list[bytes]:
    full = os.path.abspath(path).rstrip("/\\")
    forward = full.replace("\\", "/").lower().encode("utf-8")
    backward = full.replace("/", "\\").lower().encode("utf-8")
    return sorted({forward, backward})


def count_path_occurrences(data: bytes, spellings: list[bytes]) -> int:
    lowered = data.lower()
    return sum(lowered.count(spelling.lower()) for spelling in spellings)


def check_build_paths(lib_paths: list, forbidden: list[str]) -> list[str]:
    errors: list[str] = []
    for lib_path in lib_paths:
        data = Path(lib_path).read_bytes()
        for path in forbidden:
            count = count_path_occurrences(data, path_spellings(path))
            if count:
                errors.append(f"{Path(lib_path).name}: contains the build path {os.path.abspath(path)} "
                              f"{count} time(s); build with -ffile-prefix-map (android/gamelibs/CMakeLists.txt)")
    return errors


def forbidden_build_paths(extra: list[str] | None, ndk: str) -> list[str]:
    unique: dict[str, str] = {}
    for path in [str(_REPO_ROOT), ndk, *(extra or [])]:
        if path:
            unique.setdefault(os.path.abspath(path).lower(), path)
    return list(unique.values())


def apk_alignment_errors(zip_path) -> list[str]:
    errors = []
    path = str(zip_path)
    with zipfile.ZipFile(path) as archive:
        entries = [info for info in archive.infolist() if is_apk_library(info)]
    with open(path, "rb") as handle:
        for info in entries:
            if info.compress_type != zipfile.ZIP_STORED:
                errors.append(f"{info.filename}: uses compression type {info.compress_type}, "
                              "must be stored uncompressed (ZIP_STORED) for 16 KB page alignment")
            handle.seek(info.header_offset)
            local = handle.read(30)
            if len(local) < 30 or local[:4] != b"PK\x03\x04":
                errors.append(f"{info.filename}: cannot read local file header at offset {info.header_offset}")
                continue
            name_len, extra_len = struct.unpack("<HH", local[26:30])
            offset = info.header_offset + 30 + name_len + extra_len
            if offset % 16384 != 0:
                errors.append(f"{info.filename}: data starts at file offset {offset}, "
                              "which is not a multiple of 16384")
    return errors


def apk_notice_errors(zip_path) -> list[str]:
    with zipfile.ZipFile(str(zip_path)) as archive:
        present = set(archive.namelist())
    return [f"{name}: missing from the APK (third-party notices must ship with the binaries)"
            for name in _APK_REQUIRED_ENTRIES if name not in present]


def _host_order() -> tuple:
    if os.name == "nt":
        first = "windows-x86_64"
    elif sys.platform == "darwin":
        first = "darwin-x86_64"
    else:
        first = "linux-x86_64"
    return (first,) + tuple(host for host in _HOSTS if host != first)


def read_gradle_value(text: str, key: str) -> str:
    match = re.search(r"\b" + re.escape(key) + r"\s*=\s*['\"]?([^'\"\s]+)['\"]?", text)
    return match.group(1) if match else ""


def gradle_text(repo_root: Path | None = None) -> str:
    root = _REPO_ROOT if repo_root is None else repo_root
    try:
        return (root / "android" / "app" / "build.gradle").read_text(encoding="utf-8")
    except OSError:
        return ""


def default_api(repo_root: Path | None = None) -> str:
    return read_gradle_value(gradle_text(repo_root), "minSdk") or "29"


def local_properties_sdk_dir(repo_root: Path | None = None) -> str:
    root = _REPO_ROOT if repo_root is None else repo_root
    try:
        text = (root / "android" / "local.properties").read_text(encoding="utf-8")
    except OSError:
        return ""
    match = re.search(r"(?m)^\s*sdk\.dir\s*=\s*(.+?)\s*$", text)
    return match.group(1).strip() if match else ""


def android_sdk_roots(repo_root: Path | None = None) -> list[Path]:
    candidates = [
        os.environ.get("ANDROID_HOME", "").strip(),
        os.environ.get("ANDROID_SDK_ROOT", "").strip(),
        local_properties_sdk_dir(repo_root),
    ]
    local_app_data = os.environ.get("LOCALAPPDATA", "").strip()
    if local_app_data:
        candidates.append(str(Path(local_app_data) / "Android" / "Sdk"))
    roots: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        if not candidate:
            continue
        path = Path(candidate)
        if not path.is_dir():
            continue
        key = os.path.normcase(str(path.resolve()))
        if key not in seen:
            seen.add(key)
            roots.append(path)
    return roots


def ndk_property(ndk_dir, key: str) -> str:
    try:
        text = (Path(ndk_dir) / "source.properties").read_text(encoding="utf-8")
    except OSError:
        return ""
    match = re.search(r"(?m)^\s*" + re.escape(key) + r"\s*=\s*(.+?)\s*$", text)
    return match.group(1).strip() if match else ""


def ndk_revision(ndk_dir) -> str:
    return ndk_property(ndk_dir, "Pkg.Revision")


def ndk_base_revision(ndk_dir) -> str:
    base = ndk_property(ndk_dir, "Pkg.BaseRevision")
    if base:
        return base
    # Gradle matches a pre-release NDK against the pin without the suffix.
    return ndk_property(ndk_dir, "Pkg.Revision").split("-", 1)[0]


def pinned_ndk_warning(ndk_dir, pinned: str) -> str:
    if not pinned or pinned == ndk_base_revision(ndk_dir):
        return ""
    return (f"warning: using NDK {ndk_revision(ndk_dir)}, "
            f"android/app/build.gradle pins {pinned}")


def ndk_major(ndk_dir) -> int:
    digits = re.findall(r"\d+", ndk_revision(ndk_dir))
    return int(digits[0]) if digits else 0


def default_ndk_dir(repo_root: Path | None = None) -> str:
    roots = android_sdk_roots(repo_root)
    pinned = read_gradle_value(gradle_text(repo_root), "ndkVersion")
    if pinned:
        for root in roots:
            candidate = root / "ndk" / pinned
            if candidate.is_dir():
                return str(candidate)
    env = os.environ.get("ANDROID_NDK_HOME", "").strip()
    if env and Path(env).is_dir():
        return env
    best = None
    best_key: tuple = ()
    for root in roots:
        ndk_root = root / "ndk"
        if not ndk_root.is_dir():
            continue
        for path in ndk_root.iterdir():
            if not path.is_dir():
                continue
            key = tuple(int(part) for part in re.findall(r"\d+", path.name)) or (0,)
            if best is None or key > best_key:
                best, best_key = path, key
    return str(best) if best is not None else ""


def find_tool(ndk_dir: str, host: str, name: str) -> str:
    bindir = Path(ndk_dir) / "toolchains" / "llvm" / "prebuilt" / host / "bin"
    for candidate in (bindir / (name + ".exe"), bindir / name):
        if candidate.is_file():
            return str(candidate)
    return ""


class ToolError(RuntimeError):
    pass


def _run(executable: str, *args: str) -> str:
    try:
        completed = subprocess.run([executable, *args], capture_output=True, text=True,
                                   timeout=_TOOL_TIMEOUT_S)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ToolError(f"{Path(executable).name} {' '.join(args)}: {error}") from error
    if completed.returncode != 0:
        raise ToolError(f"{Path(executable).name} {' '.join(args)} exited with "
                        f"{completed.returncode}: {completed.stderr.strip()[:300]}")
    return completed.stdout


class Tools:
    def __init__(self, readelf: str, nm: str):
        self.readelf = readelf
        self.nm = nm

    def header(self, path) -> dict:
        return parse_elf_header(_run(self.readelf, "-h", str(path)))

    def load_alignments(self, path) -> list[int]:
        return parse_load_alignments(_run(self.readelf, "-lW", str(path)))

    def needed(self, path) -> list[str]:
        return parse_needed(_run(self.readelf, "-d", str(path)))

    def dynamic_symbols(self, path) -> str:
        return _run(self.nm, "-D", "--format=posix", str(path))


class Library:
    def __init__(self, name: str, needed: list[str], nm_text: str):
        self.name = name
        self.needed = needed
        self.nm_text = nm_text
        self.defined, self.undefined = parse_nm_dynamic(nm_text)


def check_elf_kind(lib: str, header: dict) -> list[str]:
    if header["class"] == "ELF64" and header["machine"] == "AArch64" and header["type"] == "DYN":
        return []
    return [f"{lib}: not an ELF64 AArch64 shared object "
            f"(Class={header['class']}, Machine={header['machine']}, Type={header['type']})"]


def read_library(path: Path, tools: Tools, packaged: set[str],
                 allowed_system: set[str]) -> tuple[Library, list[str]]:
    lib = path.name
    errors = check_elf_kind(lib, tools.header(path))
    errors.extend(check_alignment(lib, tools.load_alignments(path)))
    needed = tools.needed(path)
    errors.extend(check_needed(lib, needed, packaged, allowed_system))
    return Library(lib, needed, tools.dynamic_symbols(path)), errors


def check_expected(expect: list[str], packaged: set[str]) -> list[str]:
    return [f"{name}: expected library is not packaged" for name in expect if name not in packaged]


def system_libraries_used(libraries: dict, allowed_system: set[str]) -> list[str]:
    return sorted({dep for library in libraries.values() for dep in library.needed if dep in allowed_system})


def read_system_stubs(names: list[str], stub_dir: Path, tools: Tools) -> tuple[dict, list[str]]:
    defined: dict[str, set[str]] = {}
    errors = []
    for name in names:
        stub = stub_dir / name
        if not stub.is_file():
            errors.append(f"{name}: system stub not found at {stub} (wrong --api or NDK?)")
            defined[name] = set()
            continue
        defined[name], _ = parse_nm_dynamic(tools.dynamic_symbols(stub))
    return defined, errors


def check_resolution(libraries: dict, stub_defined: dict) -> list[str]:
    needed_by = {name: library.needed for name, library in libraries.items()}
    defined_by = {name: library.defined for name, library in libraries.items()}
    defined_by.update(stub_defined)
    errors = []
    for name, library in libraries.items():
        missing = unresolved_symbols(name, needed_by, defined_by, library.undefined)
        errors.extend(f"{name}: undefined symbol {symbol} is not defined by any library it loads"
                      for symbol in missing[:_MAX_MISSING_SHOWN])
        if len(missing) > _MAX_MISSING_SHOWN:
            errors.append(f"{name}: ... and {len(missing) - _MAX_MISSING_SHOWN} more undefined symbols")
    return errors


def check_main_library(libraries: dict, main: str, main_symbol: str) -> list[str]:
    if main not in libraries:
        return [f"{main}: main library is not packaged"]
    return [f"{main}: {problem}" for problem in check_main_symbol(libraries[main].nm_text, main_symbol)]


def check_main_exports(main: str, defined: set[str], allowed: set[str]) -> list[str]:
    extra = sorted(defined - allowed)
    if not extra:
        return []
    return [f"{main}: exports {len(extra)} symbol(s) besides {', '.join(sorted(allowed))} "
            f"(first: {', '.join(extra[:5])}); android/gamelibs/libgow2.map should keep them local"]


def check_sdl_reachable(libraries: dict, sdl: str) -> list[str]:
    needed_by = {name: library.needed for name, library in libraries.items()}
    errors = []
    for name, library in libraries.items():
        if name in ("libc++_shared.so", sdl):
            continue
        uses_sdl = any(symbol.startswith("SDL_") for symbol in library.undefined)
        if uses_sdl and sdl not in needed_closure(name, needed_by):
            errors.append(f"{name}: uses SDL_ symbols but does not load {sdl}")
    return errors


def check_libraries(lib_paths: list, tools: Tools, stub_dir: Path, options) -> list[str]:
    packaged = {Path(path).name for path in lib_paths}
    libraries: dict[str, Library] = {}
    errors: list[str] = []
    for path in lib_paths:
        library, library_errors = read_library(Path(path), tools, packaged, options.allowed)
        libraries[library.name] = library
        errors.extend(library_errors)
    errors.extend(check_expected(options.expect, packaged))
    used = system_libraries_used(libraries, options.allowed)
    stub_defined, stub_errors = read_system_stubs(used, stub_dir, tools)
    errors.extend(stub_errors)
    errors.extend(check_resolution(libraries, stub_defined))
    defined_by = {name: library.defined for name, library in libraries.items()}
    errors.extend(check_single_sdl(defined_by, options.sdl, options.main, options.main_symbol))
    errors.extend(check_main_library(libraries, options.main, options.main_symbol))
    if options.main in libraries:
        errors.extend(check_main_exports(options.main, libraries[options.main].defined, options.main_exports))
    errors.extend(check_sdl_reachable(libraries, options.sdl))
    if not options.allow_build_paths:
        errors.extend(check_build_paths(lib_paths, options.forbidden))
    return errors


def list_libraries(folder: Path) -> list[Path]:
    return sorted(path for path in folder.iterdir() if path.is_file() and path.name.endswith(".so"))


def is_apk_library(info: zipfile.ZipInfo) -> bool:
    return info.filename.startswith(_APK_LIB_PREFIX) and info.filename.endswith(".so") and not info.is_dir()


def extract_apk_libraries(apk: Path, folder: Path) -> None:
    with zipfile.ZipFile(str(apk)) as archive:
        for info in filter(is_apk_library, archive.infolist()):
            with archive.open(info) as src, open(folder / Path(info.filename).name, "wb") as dst:
                shutil.copyfileobj(src, dst, _COPY_CHUNK)


def find_tools(ndk: str):
    for host in _host_order():
        readelf, nm = find_tool(ndk, host, "llvm-readelf"), find_tool(ndk, host, "llvm-nm")
        if readelf and nm:
            return Tools(readelf, nm), host
    return None


def stub_folder(ndk: str, host: str, api: str) -> Path:
    sysroot = Path(ndk) / "toolchains" / "llvm" / "prebuilt" / host / "sysroot"
    return sysroot / "usr" / "lib" / "aarch64-linux-android" / api


def check_input(source: Path, tools: Tools, stub_dir: Path, options) -> tuple[list[str], int]:
    if source.is_dir():
        lib_paths = list_libraries(source)
        return check_libraries(lib_paths, tools, stub_dir, options), len(lib_paths)
    with tempfile.TemporaryDirectory(prefix="apk-libs-") as tmpdir:
        extract_apk_libraries(source, Path(tmpdir))
        lib_paths = list_libraries(Path(tmpdir))
        errors = apk_alignment_errors(str(source))
        if not options.no_notices:
            errors.extend(apk_notice_errors(str(source)))
        errors.extend(check_libraries(lib_paths, tools, stub_dir, options))
        return errors, len(lib_paths)


def parse_options(argv: list[str]):
    parser = argparse.ArgumentParser(prog="check_android_libs")
    parser.add_argument("input")
    parser.add_argument("--ndk", default="")
    parser.add_argument("--api", default=None)
    parser.add_argument("--expect", nargs="*", default=None)
    parser.add_argument("--main", default="libgow2.so")
    parser.add_argument("--main-symbol", default="SDL_main")
    parser.add_argument("--main-exports", nargs="*", default=None)
    parser.add_argument("--sdl", default="libSDL3.so")
    parser.add_argument("--allow-system", nargs="*", default=None)
    parser.add_argument("--no-notices", action="store_true")
    parser.add_argument("--forbid-path", action="append", default=None, metavar="PATH")
    parser.add_argument("--allow-build-paths", action="store_true")
    options = parser.parse_args(argv)
    options.expect = list(_DEFAULT_EXPECT) if options.expect is None else options.expect
    options.allowed = set(_DEFAULT_ALLOW_SYSTEM if options.allow_system is None else options.allow_system)
    options.main_exports = set([options.main_symbol] if options.main_exports is None else options.main_exports)
    options.forbidden = forbidden_build_paths(options.forbid_path, "")
    return options


def usage_error(message: str) -> int:
    print(f"check_android_libs: {message}", file=sys.stderr)
    return 2


def report(errors: list[str], count: int) -> int:
    for problem in errors:
        print(f"ERROR {problem}")
    if errors:
        print(f"check_android_libs: {len(errors)} problem(s)")
        return 1
    print(f"check_android_libs: {count} libraries, all checks passed")
    return 0


def main(argv: list[str]) -> int:
    options = parse_options(argv)
    source = Path(options.input)
    if not source.exists():
        return usage_error(f"input not found: {options.input}")
    ndk = options.ndk or default_ndk_dir()
    if not ndk or not Path(ndk).is_dir():
        return usage_error("NDK not found (use --ndk or set ANDROID_NDK_HOME)")
    revision = ndk_revision(ndk)
    major = ndk_major(ndk)
    if major != 0 and major < 29:
        return usage_error(f"NDK {revision} at {ndk} is too old; NDK r29 or newer is needed")
    pinned = read_gradle_value(gradle_text(), "ndkVersion")
    warning = pinned_ndk_warning(ndk, pinned)
    if warning:
        print(f"check_android_libs: {warning}", file=sys.stderr)
    found = find_tools(ndk)
    if found is None:
        return usage_error(f"llvm-readelf/llvm-nm not found under {ndk}")
    tools, host = found
    options.forbidden = forbidden_build_paths(options.forbid_path, ndk)
    api = options.api or default_api()
    try:
        errors, count = check_input(source, tools, stub_folder(ndk, host, api), options)
    except zipfile.BadZipFile:
        return usage_error(f"not a valid apk/zip: {options.input}")
    except ToolError as error:
        return usage_error(f"cannot inspect the libraries: {error}")
    return report(errors, count)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
