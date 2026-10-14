#!/usr/bin/env python3
# Find harvested hints that split functions named in codegen unresolved-branch errors.
import argparse
import re
import sys
from pathlib import Path

B_PAT = re.compile(r"Unresolved b target 0x([0-9A-Fa-f]+) from 0x([0-9A-Fa-f]+)")
COND_PAT = re.compile(r"Unresolved conditional branch to 0x([0-9A-Fa-f]+) from 0x([0-9A-Fa-f]+)")
HINT_PAT = re.compile(r"^\s*(0x[0-9A-Fa-f]+)\s*=")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Deny harvested hints that split unresolved branches.")
    p.add_argument("--codegen-log", required=True)
    p.add_argument("--hints", default="hints/harvested.toml")
    p.add_argument("--deny", default="hints/denylist.txt")
    return p.parse_args(argv)


def parse_unresolved(text: str) -> list[tuple[int, int]]:
    pairs: list[tuple[int, int]] = []
    for line in text.splitlines():
        m = B_PAT.search(line)
        if m:
            pairs.append((int(m.group(2), 16), int(m.group(1), 16)))
            continue
        m = COND_PAT.search(line)
        if m:
            pairs.append((int(m.group(2), 16), int(m.group(1), 16)))
    return pairs


def load_hints(path: Path) -> list[int]:
    hints: list[int] = []
    for line in path.read_text().splitlines():
        m = HINT_PAT.match(line)
        if m:
            hints.append(int(m.group(1), 16))
    return hints


def load_denylist(path: Path) -> set[int]:
    try:
        text = path.read_text()
    except FileNotFoundError:
        return set()
    denied: set[int] = set()
    for line in text.splitlines():
        code = line.split("#", 1)[0].strip()
        if not code:
            continue
        try:
            denied.add(int(code.split()[0], 16))
        except ValueError:
            continue
    return denied


def find_culprits(pairs: list[tuple[int, int]], hints: list[int]) -> dict[int, tuple[int, int]]:
    culprits: dict[int, tuple[int, int]] = {}
    for s, t in pairs:
        lo, hi = (s, t) if s < t else (t, s)
        for h in hints:
            if h not in culprits and lo < h <= hi:
                culprits[h] = (s, t)
    return culprits


def format_entry(h: int, s: int, t: int) -> str:
    return f"0x{h:08X} # split 0x{s:08X}->0x{t:08X}"


def append_new(deny_path: Path, culprits: dict[int, tuple[int, int]],
               existing: set[int]) -> list[str]:
    fresh = sorted(h for h in culprits if h not in existing)
    if not fresh:
        return []
    deny_path.parent.mkdir(parents=True, exist_ok=True)
    prefix = ""
    if deny_path.exists():
        old = deny_path.read_text()
        if old and not old.endswith("\n"):
            prefix = "\n"
    lines = [format_entry(h, *culprits[h]) for h in fresh]
    with deny_path.open("a") as f:
        if prefix:
            f.write(prefix)
        for line in lines:
            f.write(line + "\n")
    return lines


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    pairs = parse_unresolved(Path(args.codegen_log).read_text())
    hints = load_hints(Path(args.hints))
    deny_path = Path(args.deny)
    lines = append_new(deny_path, find_culprits(pairs, hints), load_denylist(deny_path))
    for line in lines:
        print(line)
    return 1 if lines else 0


if __name__ == "__main__":
    sys.exit(main())
