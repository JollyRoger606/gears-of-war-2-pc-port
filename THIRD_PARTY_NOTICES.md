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
- `tools/patch_game_patches.py`, `src/gow2_game_patches.cpp` — adapted from its `tools/patch_game_patches.py` and `src/gowj_game_patches.cpp`; the patched sites and values come from xenia-canary/game-patches (`4D53082D - Gears of War 2.patch.toml`, authors illusion and boma; that repository has no license file, so only the addresses and values are reused, with credit).
- `src/gow2_coalesced.h`, `src/gow2_coalesced.cpp` — adapted from its `src/gowj_coalesced.*` (bundle reader/writer, cached-override plumbing); the edits are GoW2-specific (mouse-look dead zone, key prompts).

## SDL_GameControllerDB

- Project: [mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
- Description: community gamepad mapping database shipped as `config/gamecontrollerdb.txt`.
- License: zlib.

## ReXGlue SDK

- Project: [rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk)
- Description: static recompilation runtime and code generator used by this project. Not vendored in this repository; the release zip includes `rexruntime.dll` and `rexgpu-xenos.dll` built from it with the patches in `sdk/patches/`.
- Copyright: Copyright (c) 2026, Tom Clay; portions Copyright (c) 2022, Ben Vanik and Xenia project contributors
- License: BSD 3-Clause (full text below).

## TinySHA1

- Project: [mohaps/TinySHA1](https://github.com/mohaps/TinySHA1), the copy in the ReXGlue SDK (`thirdparty/crypto/TinySHA1.hpp`, "Modified for use by Xenia"), vendored as `src/third_party/TinySHA1.hpp` to hash the edited Coalesced bundles.
- Copyright: Copyright (c) 2012-22 SAURAV MOHAPATRA
- License: ISC (full text in the file header).

## AMD FidelityFX SDK 1.1.4 (`amd_fidelityfx_dx12.dll`)

- Project: [GPUOpen-LibrariesAndSDKs/FidelityFX-SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), built from source by the ReXGlue SDK for the FSR 1 and CAS present effects.
- License: MIT.

```
This file is part of the FidelityFX SDK.

Copyright (C) 2024 Advanced Micro Devices, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## AMD FSR SDK 2.3.0 signed binaries (`amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll`)

- Project: [GPUOpen-LibrariesAndSDKs/FidelityFX-SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) v2.3.0, the signed DLLs from `Kits/FidelityFX/signedbin`, redistributed unmodified for the temporal FSR path.
- License (from `docs/license.md` of that release; it applies to these binaries):

```
The following license applies to all files except as noted below. 

Copyright (C) Advanced Micro Devices, Inc.
 
REDISTRIBUTION: Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to install, reproduce, copy and distribute copies of the Software, in binary form only, and to permit persons to whom the Software is provided to do the same, provided that the following conditions are met: 
 
No reverse engineering, decompilation, or disassembly of this Software is permitted. 
 
Redistributions must reproduce the above copyright notice, this permission notice, and the following disclaimers and notices in the Software documentation and/or other materials provided with the Software. 
 
DISCLAIMER: THE USE OF THE SOFTWARE IS AT YOUR SOLE RISK.  THE SOFTWARE 
IS PROVIDED "AS IS" AND WITHOUT WARRANTY OF ANY KIND AND COPYRIGHT 
HOLDER AND ITS LICENSORS EXPRESSLY DISCLAIM ALL WARRANTIES, EXPRESS AND 
IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. 
COPYRIGHT HOLDER AND ITS LICENSORS DO NOT WARRANT THAT THE SOFTWARE WILL 
MEET YOUR REQUIREMENTS, OR THAT THE OPERATION OF THE SOFTWARE WILL BE 
UNINTERRUPTED OR ERROR-FREE.  THE ENTIRE RISK ASSOCIATED WITH THE USE OF 
THE SOFTWARE IS ASSUMED BY YOU.  FURTHERMORE, COPYRIGHT HOLDER AND ITS 
LICENSORS DO NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE 
OR THE RESULTS OF THE USE OF THE SOFTWARE IN TERMS OF ITS CORRECTNESS, 
ACCURACY, RELIABILITY, CURRENTNESS, OR OTHERWISE. 
 
DISCLAIMER: UNDER NO CIRCUMSTANCES INCLUDING NEGLIGENCE, SHALL COPYRIGHT HOLDER AND ITS LICENSORS OR ITS DIRECTORS, OFFICERS, EMPLOYEES OR AGENTS ("AUTHORIZED REPRESENTATIVES") BE LIABLE FOR ANY INCIDENTAL, INDIRECT, 
SPECIAL OR CONSEQUENTIAL DAMAGES (INCLUDING DAMAGES FOR LOSS OF BUSINESS 
PROFITS, BUSINESS INTERRUPTION, LOSS OF BUSINESS INFORMATION, AND THE 
LIKE) ARISING OUT OF THE USE, MISUSE OR INABILITY TO USE THE SOFTWARE, 
BREACH OR DEFAULT, INCLUDING THOSE ARISING FROM INFRINGEMENT OR ALLEGED 
INFRINGEMENT OF ANY PATENT, TRADEMARK, COPYRIGHT OR OTHER INTELLECTUAL 
PROPERTY RIGHT EVEN IF COPYRIGHT HOLDER AND ITS AUTHORIZED 
REPRESENTATIVES HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.  IN 
NO EVENT SHALL COPYRIGHT HOLDER OR ITS AUTHORIZED REPRESENTATIVES TOTAL 
LIABILITY FOR ALL DAMAGES, LOSSES, AND CAUSES OF ACTION (WHETHER IN 
CONTRACT, TORT (INCLUDING NEGLIGENCE) OR OTHERWISE) EXCEED THE AMOUNT OF 
US$10.
```

## Microsoft Visual C++ runtime

`msvcp140.dll`, `msvcp140_atomic_wait.dll`, `vcruntime140.dll` and `vcruntime140_1.dll` are Microsoft Visual C++ Redistributable files, included unmodified as Distributable Code under the Microsoft Visual Studio license terms.

## NVIDIA DLSS

Not included in the release. The DLSS SDK license does not allow it to become part of an open-source distribution and asks for NVIDIA trademarks in the game's splash and about screens, which this project cannot do. Players with an NVIDIA GPU who want to try the DLSS path can place `nvngx_dlss.dll` from NVIDIA's DLSS SDK next to `gow2.exe` themselves.

## BSD 3-Clause License text

The following license text applies to gears-judgement-recomp and the ReXGlue SDK:

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
