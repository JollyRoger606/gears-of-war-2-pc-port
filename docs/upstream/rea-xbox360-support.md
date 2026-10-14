# Support Xbox 360 executables via PowerPC big-endian PE (machine 0x01F2)

## Summary

This is a design-discussion issue asking for a scope decision before any
implementation, per `CONTRIBUTING.md`: add Xbox 360 support to REA without
any XEX decryption in REA.

Proposed scope:

1. Detect the `XEX2` magic and return a clear message telling the user to
   extract the PE basefile themselves (e.g. with XexTool `-b`).
2. Accept the resulting extracted PE whose COFF machine is `0x01F2`
   (`IMAGE_FILE_MACHINE_POWERPCBE`; headers little-endian, code
   big-endian). Target CPU is Xenon: PowerPC 64-bit with 32-bit
   addressing + VMX128.

REA must not decrypt, decompress, or accept keys for XEX. Per
`CONTRIBUTING.md`, DRM / access-control bypass workflows are excluded,
even through plugins.

## Current behavior

- An `XEX2` input matches no branch of `parseExecutableHeader` and fails
  with `"unsupported binary format"` (`src/domain/binaryTarget.ts`).
- A decrypted PPCBE PE basefile then fails `decodePeMachine`
  (`src/domain/peMachine.ts`) with
  `"unsupported PE architecture (COFF machine 0x01f2)"`, and `pe` +
  `powerpc` is not a representable `BinaryTarget` today.

## Verified Ghidra blocker

Ghidra `12.1.3` stock support is also missing:

- `PowerPC.opinion` only lists PE primaries `496`/`497`, little-endian.
- After adding
  `<constraint primary="498" processor="PowerPC" endian="big" size="64" variant="A2ALT-32addr" />`,
  the import still fell back to `Old-style DOS Executable (MZ)` with
  warnings like `Section .rdata is not aligned!`.

So Ghidra's `PeLoader` needs changes too: the `0x01F2` machine constant
plus section-alignment handling for Xbox 360 basefiles.

## Proposed first PR

Small and self-contained, no Ghidra lane changes:

- `XEX2` magic detection with a user-action error message (extract the
  basefile externally; no bypass in REA).
- `0x01F2` mapping in `src/domain/peMachine.ts` plus the minimal
  `pe` + `powerpc` target plumbing.
- Unit tests: synthetic `XEX2` header, truncated header, `0x01F2` PE
  mapping, rejection of other machines.

The full Ghidra analysis lane (pinned `PowerPC:BE:64:A2ALT-32addr`
language, loader selection, real-provider conformance) would be a
follow-up, blocked on upstream Ghidra `PeLoader` support.

## Scope question for maintainers

Is (a) detect-`XEX2`-and-redirect plus (b) accept user-supplied `0x01F2`
PE basefiles an acceptable direction? If yes, we will send the small
first PR above and keep the Ghidra lane separate.
