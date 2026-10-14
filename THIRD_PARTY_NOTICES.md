# Third-Party Notices

This file lists third-party projects from which this repository derives code
or on which it depends at build time.

## gears-judgement-recomp

- Project: [OverkillLabs3/gears-judgement-recomp](https://github.com/OverkillLabs3/gears-judgement-recomp)
- Description: Gears of War: Judgment static recompilation on ReXGlue (BSD-3-Clause).
- Copyright: Copyright (c) 2026, the contributors to this project
- License: BSD 3-Clause (full text below).

Files in this repository derived from it:

- `tools/patch_switch_traps.py` — copied from gears-judgement-recomp.
- `tools/check_objects_fresh.py` — copied from gears-judgement-recomp.
- `scripts/build.ps1` — adapted from its `scripts/build.ps1`.
- `src/gow2_app.h` — WithXex, AppHome, ResolveContentRoot, SetIfUnset,
  PickFastestAdapter, and OnConfigurePaths adapted from its `src/gowj_app.h`.
- `config/gamecontrollerdb.txt` — copied from its `config/gamecontrollerdb.txt`.
- `CMakeLists.txt` — gamecontrollerdb.txt install step adapted from its `CMakeLists.txt`.
- `src/gow2_xam_input_guard.h` — adapted from its `src/gowj_xam_input_guard.h`.
- `src/gow2_xam_input_guard.cpp` — adapted from its `src/gowj_xam_input_guard.cpp`.
- `src/gow2_utf8_guard.h`, `src/gow2_utf8_guard.cpp` — adapted from its `src/gowj_utf8_guard.*`.
- `src/gow2_crash_watch.h`, `src/gow2_crash_watch.cpp` — adapted from its `src/gowj_crash_watch.*`.

## SDL_GameControllerDB

- Project: [mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
- Description: community gamepad mapping database shipped as `config/gamecontrollerdb.txt`.
- License: zlib.

## ReXGlue SDK

- Project: [rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk)
- Description: static recompilation runtime and code generator used by this project. Not vendored; obtained separately (see BUILDING.md).
- Copyright: Copyright (c) 2026, Tom Clay; portions Copyright (c) 2022, Ben Vanik and Xenia project contributors
- License: BSD 3-Clause (full text below).

## BSD 3-Clause License text

The following license text applies to both projects listed above:

```
BSD 3-Clause License

Copyright (c) 2026, the contributors to this project (gears-judgement-recomp)
Copyright (c) 2026, Tom Clay (ReXGlue SDK)
Portions Copyright (c) 2022, Ben Vanik and Xenia project contributors (ReXGlue SDK)
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
