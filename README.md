# Gears of War 2 — native PC recompilation (unofficial)

Static recompilation of the retail Xbox 360 version of Gears of War 2
(Epic Games, 2008, Unreal Engine 3) to a native Windows x64 binary, using the
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk). The game's PowerPC code
from its own `default.xex` is recompiled to C++ and built for PC.

An earlier fan port ("Gears of War 2: Hollow", July 2026) was built from a
leaked, incomplete 2008 development build shipped with Windows binaries. This
project instead recompiles the complete retail game from the player's own disc.

## Status

Early work in progress: code generation from the retail default.xex succeeds
and the project is being brought up to boot. Not playable yet.

- [x] codegen of retail XEX
- [ ] first successful build
- [ ] boots to menus
- [ ] campaign playable
- [ ] keyboard & mouse
- [ ] multiplayer/Horde

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
