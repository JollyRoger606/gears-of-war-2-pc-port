# Gears of War 2 — native PC recompilation (unofficial)

Static recompilation of the retail Xbox 360 version of Gears of War 2
(Epic Games, 2008, Unreal Engine 3) to a native Windows x64 binary, using the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The game's PowerPC code
from its own `default.xex` is recompiled to C++ and built for PC.

An earlier fan port ("Gears of War 2: Hollow", July 2026) was built from a
leaked, incomplete 2008 development build shipped with Windows binaries. This
project instead recompiles the complete retail game from the player's own disc.

## Status

Early work in progress. The retail `default.xex` recompiles, builds and boots on a
Radeon RX 7800 XT: the game initializes its kernel, audio, file system and Direct3D 12,
reaches the title screen and the main menu (keyboard: `--mnk_mode=true`, Enter = Start,
Space = A), starts Solo Campaign, plays the campaign intro cinematic and reaches the
level loading screen. Not playable yet: gameplay past the first load is untested.

- [x] codegen of retail XEX
- [x] first successful build
- [x] boots and plays the opening cinematic
- [x] title screen and main menu
- [x] Solo Campaign: intro cinematic and loading screen
- [ ] first level playable
- [ ] full campaign playable
- [ ] keyboard & mouse
- [ ] multiplayer/Horde

### Finding the functions codegen misses

`tools/harvest_hints.py` scans the decrypted image (exported from Ghidra with
`tools/ghidra/ExportImage.java`) against a baseline codegen without hints
(`scripts/codegen-baseline.sh`) and emits `hints/harvested.toml`: adjustor thunks,
`.pdata` entries, code pointers stored in `.rdata`/`.data` and addresses built in code
with `lis`+`addi`/`ori`. Hints that split an existing function are detected from the
codegen log by `tools/hint_fixpoint.py` and kept out through `hints/denylist.txt`.
Anything still missing is found at runtime by `scripts/run-harvest-loop.sh`
(boot, read the "unregistered function" FATAL, add it to `hints/runtime.toml`, rebuild).

## You need your own copy

- You must provide your own legally-dumped copy of the retail Xbox 360 disc.
  Supported executable: title ID `4D53082D`, media ID `63AF211E`, XEX version
  `0.0.0.5`, file time 2008-09-23. Title-update builds are not supported.
- This repository contains no game code, no generated C++, and no assets.
- Do not pirate the game.

## Building

See [BUILDING.md](BUILDING.md).

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (BSD-3-Clause, derived
  from Xenia) — runtime and code generator.
- [OverkillLabs3/gears-judgement-recomp](https://github.com/OverkillLabs3/gears-judgement-recomp)
  (Gears of War: Judgment on ReXGlue, BSD-3-Clause) — this repo copies
  `tools/patch_switch_traps.py` and `tools/check_objects_fresh.py` from it and
  adapts `scripts/build.ps1` from its `scripts/build.ps1`. See
  [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- [XenonRecomp](https://github.com/hedge-dev/xenonrecomp) (hedge-dev).
- [Xenia](https://github.com/xenia-project/xenia).

## Legal

Unofficial project. Not affiliated with or endorsed by Microsoft, Xbox Game
Studios, The Coalition, or Epic Games. All trademarks belong to their owners.
This repository's own code is licensed under the BSD-3-Clause License — see
[LICENSE](LICENSE).
