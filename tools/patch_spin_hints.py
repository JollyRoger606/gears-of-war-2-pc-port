"""Post-codegen patch: ARM spin pauses for Xenon delay hints on the Android copy.

The recompiler emits nothing for the Xenon's delay hints, so the game's GPU and
lock wait loops spin at full speed on the phone. ISB is the usual AArch64
spin-loop pause. This patch runs only on the Android copy of the generated code.

    python tools/patch_spin_hints.py [--dry-run] [generated/default/*.cpp ...]

Idempotent (marker GOW2 SPINHINT): hints already followed by an ARM ISB are left
alone. Writes nothing unless every file verifies. Counts include each mnemonic.
"""
import argparse
import collections
import glob
import os
import re
import sys

_MNEMONICS = ("db8cyc", "db10cyc", "db12cyc", "db14cyc", "db16cyc")
_MARKER_RE = re.compile(
    r'^\t// (?P<mn>db8cyc|db10cyc|db12cyc|db14cyc|db16cyc)'
    r'(?: [^\r\n]*)?(?P<nl>\r?\n|\Z)', re.M
)
_PAUSE_AHEAD_RE = re.compile(r'[ \t]*__builtin_arm_isb\(')
_GOW2_LINE_RE = re.compile(
    r'^\t__builtin_arm_isb\(15\);  // GOW2 SPINHINT '
    r'(?:db8cyc|db10cyc|db12cyc|db14cyc|db16cyc)(?:\r?\n|\Z)', re.M
)


def file_newline(text: str) -> str:
    match = re.search(r'\r?\n', text)
    return match.group(0) if match is not None else "\n"


def pause_line(mnemonic: str, newline: str) -> str:
    return f"\t__builtin_arm_isb(15);  // GOW2 SPINHINT {mnemonic}{newline}"


def _after_marker(text: str, marker: re.Match[str]) -> tuple[str, str]:
    if _PAUSE_AHEAD_RE.match(text, marker.end()) is not None:
        return "", "present"
    newline = marker.group("nl")
    if newline == "":
        # An unterminated final marker borrows the file's newline; its pause stays unterminated.
        return file_newline(text) + pause_line(marker.group("mn"), ""), "insert"
    return pause_line(marker.group("mn"), newline), "insert"


def patch_source(text: str) -> tuple[str, collections.Counter]:
    counts: collections.Counter = collections.Counter()
    parts: list[str] = []
    cursor = 0
    for marker in _MARKER_RE.finditer(text):
        parts.append(text[cursor:marker.end()])
        snippet, action = _after_marker(text, marker)
        parts.append(snippet)
        counts[(marker.group("mn"), action)] += 1
        cursor = marker.end()
    parts.append(text[cursor:])
    return "".join(parts), counts


def unpaused_lines(text: str) -> list[int]:
    return [
        text.count("\n", 0, marker.start()) + 1
        for marker in _MARKER_RE.finditer(text)
        if _PAUSE_AHEAD_RE.match(text, marker.end()) is None
    ]


def strip_patch(text: str) -> str:
    return _GOW2_LINE_RE.sub("", text) if "GOW2 SPINHINT" in text else text


def verify(original: str, patched: str) -> list[str]:
    problems = []
    # Appending the original line ending accounts for a final marker borrowing a newline.
    newline = file_newline(original)
    if strip_patch(patched + newline) != strip_patch(original + newline):
        problems.append("text other than spin pauses changed")
    for number in unpaused_lines(patched):
        problems.append(f"delay hint at line {number} has no pause")
    return problems


def expand_args(args: list[str]) -> list[str]:
    paths: list[str] = []
    for arg in args:
        if any(wild in arg for wild in "*?["):
            paths.extend(glob.glob(arg))
        else:
            paths.append(arg)
    return sorted(set(paths))


def summary(counts: collections.Counter, files_total: int, files_changed: int, dry_run: bool) -> str:
    head = "patch_spin_hints: dry run, nothing written" if dry_run else f"patch_spin_hints: {files_changed} file(s) written"
    lines = [head]
    for mnemonic in _MNEMONICS:
        inserts = counts[(mnemonic, "insert")]
        presents = counts[(mnemonic, "present")]
        lines.append(f"  {mnemonic:<8}{inserts + presents:>6} site(s): {inserts} insert, {presents} present")
    tail = "would change" if dry_run else "changed"
    lines.append(f"  {'total':<8}{sum(counts.values()):>6} site(s) in {files_changed} of {files_total} file(s) {tail}")
    return "\n".join(lines)


def _read_and_patch(path: str) -> tuple[str, str, collections.Counter, list[str]]:
    with open(path, encoding="utf-8", newline="") as handle:
        original = handle.read()
    patched, counts = patch_source(original)
    return original, patched, counts, verify(original, patched)


def _write_if_changed(path: str, original: str, patched: str, dry_run: bool) -> bool:
    if patched == original:
        return False
    if not dry_run:
        tmp = path + ".spinhint.tmp"
        with open(tmp, "w", encoding="utf-8", newline="") as handle:
            handle.write(patched)
        os.replace(tmp, path)
    return True


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="patch_spin_hints")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("paths", nargs="*", default=["generated/default/*.cpp"])
    options = parser.parse_args(argv)
    files = expand_args(options.paths)
    if not files:
        print("patch_spin_hints: no sources matched " + " ".join(options.paths), file=sys.stderr)
        return 1
    if any(not path.endswith(".cpp") for path in files):
        print("patch_spin_hints: only .cpp sources are accepted", file=sys.stderr)
        return 1
    results = [(path, *_read_and_patch(path)) for path in files]
    if any(problems for _, _, _, _, problems in results):
        for path, _, _, _, problems in results:
            for problem in problems:
                print(f"ERROR {path}: {problem}", file=sys.stderr)
        return 2
    total: collections.Counter = collections.Counter()
    changed = 0
    for path, original, patched, counts, _ in results:
        total += counts
        changed += _write_if_changed(path, original, patched, options.dry_run)
    print(summary(total, len(files), changed, options.dry_run))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
