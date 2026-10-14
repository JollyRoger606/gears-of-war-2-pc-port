# Codegen: catch funclets that rethrow fall through into the next funclet

## Summary

MSVC catch funclets that end in a rethrow (`throw;`) are not terminated
during discovery. The trailing call to `_CxxThrowException` is treated as
returning, so the funclet's body runs on into the next funclet, duplicates
its blocks, and emits `goto loc_X` references to labels owned by the next
function. The generated C++ then fails to compile with
`use of undeclared label`.

## Environment

- ReXGlue SDK `0.10.0.9-dev`, commit `923c1a5`, Windows x64.
- Input: retail Gears of War 2 `default.xex` (title `4D53082D`).
- In this XEX, `_CxxThrowException` is at `0x82CD54A0`. It copies an
  exception record template and calls `RaiseException`; it never returns.

## Steps to reproduce

1. Use any title with C++ EH rethrow funclets (we hit this on the retail
   Gears of War 2 `default.xex`).
2. Look for a catch funclet ending in the rethrow sequence:
   `li r4,0 ; li r3,0 ; bl _CxxThrowException`.
3. Observe that discovery treats the `bl` as returning and continues
   disassembly straight into the next funclet, which starts with:
   `addi r31,r12,-N ; mflr r12 ; stw r12,-8(r1) ; stwu r1,-96(r1)`.
4. Build the generated code with clang.

Observed pairs (funclet / next funclet, next start is always `+0x9C`):

- `sub_82AE14C8` vs `sub_82AE1564`
- `0x82AEFF8C` / `0x82AF0028`
- `0x82AFC28C` / `0x82AFC328`
- `0x82AFCE0C` / `0x82AFCEA8`
- `0x82B01290` / `0x82B0132C`

Related symptom: a parent function's tail block placed after its funclets
is reached by a plain `b` but reported as `target not in any function`
(`0x82B3EB88` from `0x82B3EAC0`, parent `0x82B3E728`).

## Expected / Actual

- Expected: the funclet ends at the noreturn `bl _CxxThrowException`; the
  next funclet is discovered as its own function; no cross-function gotos
  are emitted.
- Actual: the funclet falls through into the next funclet, duplicating its
  blocks, and the generated body contains `goto loc_X` to labels defined
  in the next function. Clang fails with `use of undeclared label`.

## Workaround

Bound each affected funclet explicitly in the manifest and re-parent the
orphaned tail chunk:

- `0xF = { end = 0xNEXT }` for each rethrow funclet, where `0xNEXT` is the
  next funclet's start.
- `0xTAIL = { parent = 0xPARENT }` for the orphaned tail chunk.

We find them with a small script that scans generated bodies for gotos
whose label is not defined in the same body:
`tools/find_orphan_gotos.py` in
`github.com/santiquiroz/gears-of-war-2-pc-port`.

## Suggested fix

- Treat calls to the detected `_CxxThrowException` (or, more generally,
  any function whose body unconditionally ends in `RaiseException` with
  no `blr`) as noreturn during discovery, so the funclet block ends at
  the `bl`.
- And/or bound a funclet at the next `.pdata` / `FuncInfo` funclet start
  so discovery never runs one funclet into the next.
