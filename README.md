# Gears of War 2 — native PC port (unofficial)

Retail Xbox 360 Gears of War 2 statically recompiled from the player's own `default.xex` to native x64 code with the ReXGlue SDK. This is not emulation. Players must own the game. An earlier fan port ("Hollow") was built from a leaked 2008 development build; this one runs the complete retail game.

Leer en español: [README.es.md](README.es.md)

## Screenshots

![Title screen](docs/screenshots/title.jpg)
![Main menu](docs/screenshots/main-menu.jpg)
![Act 1, Jacinto Med](docs/screenshots/act1-jacintomed.jpg)
![Act 1 with 4K internal rendering (resolution_scale = 3)](docs/screenshots/act1-4k-internal.jpg)

*Act 1 with 4K internal rendering (resolution_scale = 3).*

![Original 720p rendering (left) vs 4K internal rendering (right)](docs/screenshots/scale-1x-vs-3x.jpg)

*Original 720p rendering (left) vs 4K internal rendering (right).*

## Status

- [x] Act 1 playable with keyboard and mouse or a controller
- [x] Native high resolution (`resolution_scale` 2 = 1440p, 3 = 4K internal)
- [x] Community game patches
- [x] FSR 1 upscaling and CAS sharpening (`present_effect`)
- [x] Texture packs: dump the game's textures, load your own higher-resolution versions
- [ ] Mouse look that feels like a PC game (fix in progress, see Known issues)
- [ ] FSR 4 / DLSS temporal upscaling (built in, not active for this game yet)
- [ ] Full campaign play-through
- [ ] System Link (socket layer fixed and traced; sessions not implemented yet)

See [ROADMAP.md](ROADMAP.md) for the full plan.

## Download and play

Get the release zip from the GitHub Releases page.

Requirements:

- Windows 10/11 x64.
- A DirectX 12 GPU (developed on an AMD Radeon RX 7800 XT).
- Your own retail Xbox 360 copy of Gears of War 2 (title ID `4D53082D`), dumped to a folder with `default.xex` and `GearGame`, e.g. extract your ISO with [xdvdfs](https://github.com/antangelo/xdvdfs):

  ```text
  xdvdfs unpack "Gears of War 2.iso" C:\Games\GoW2
  ```

Unzip anywhere and run `gow2.exe` — the first launch asks for the game folder and remembers it in `gow2_game_path.txt`. Alternatively, pass the folder directly:

```text
gow2.exe --game_data_root="C:\path\to\game"
```

Nothing of the game is included in the release. Do not pirate the game.

## Controls

| Action | Key |
|---|---|
| Move | W / A / S / D |
| Look | Mouse (or arrow keys) |
| Fire | Left mouse |
| Aim | Right mouse |
| Cover / roadie run / roll | Space |
| Melee / chainsaw (hold) | F or V |
| Use / pick up / revive | E |
| Point of interest | Q |
| Reload | R |
| Tactical display | G |
| Zoom | Middle mouse or Z |
| Weapons | 1–4 |
| Pause | Enter or Esc |
| Back | Tab |

A controller works at the same time. All bindings are in `gow2.toml`.

## Settings

`gow2.toml` sits next to the exe. It is read at start-up; command-line flags (`--name=value`) override it. Main keys:

- `fullscreen` — run fullscreen or windowed.
- `resolution_scale` — internal render resolution as a multiple of 720p: 1 = 720p, 2 = 1440p, 3 = 4K.
- `present_effect` — final image scaling: `bilinear` with the stock runtime; `fsr`, `cas` and experimental temporal paths with a source-built runtime (see below).
- `vsync` — vertical sync.
- `gow2_unlock_fps` — lifts the game's 30 fps cap.
- `gow2_black_shading_fix` — correct character lighting.
- `gow2_texture_streaming_fix` — bigger texture pool, less pop-in.
- `gow2_aniso16` — 16x anisotropic filtering.
- `gow2_ao_upscaling_fix` — ambient occlusion without artifacts when scaled.
- `gow2_dof_upscaling_fix` — depth of field without artifacts when scaled.
- `mnk_sensitivity` — mouse sensitivity.

The community patches come from [xenia-canary/game-patches](https://github.com/xenia-canary/game-patches) (authors illusion and boma); the patched sites and values for this XEX are applied to the generated code, each behind its `gow2_*` setting.

## Performance

Measured on a Ryzen 9 7900X3D + Radeon RX 7800 XT, Act 1, `resolution_scale = 2` (1440p internal): about 90 presented frames per second on average, 1% low 55, GPU about 6 ms per frame (PresentMon 2.6, 30 s capture). These numbers are presented frames.

## Modern enhancements

- Native resolution via `resolution_scale` (see Settings).
- FSR: the release runtime is built with AMD FidelityFX. `present_effect = "fsr"` renders at a lower `resolution_scale` and upscales with FSR 1 plus sharpening; `"cas"` only sharpens. Measured with `resolution_scale = 1` and FSR 1 to 1440p: about 85 fps in Act 1.
- Temporal upscaling: FSR 4.1 (Radeon RX 7000/9000, through AMD's signed FidelityFX SDK 2.3 DLLs), FSR 3.1 on other GPUs and DLSS on NVIDIA (bring your own `nvngx_dlss.dll`, see THIRD_PARTY_NOTICES.md) are built in, ported from the Gears of War: Judgment recompilation. It does not switch on for this game yet: its scene detection still expects Judgment's frame layout and is being calibrated. Off by default (`gow2_temporal`, `gow2_upscaler`).
- Texture packs: see the next section. Tested with a texture replaced by a version twice its size:

  ![A title-screen texture replaced by a recoloured 2048x2048 version](docs/screenshots/texture-pack-test.jpg)

## Texture packs

Replace game textures with higher-resolution versions (for example upscaled 4x). The game ships no textures, so each player builds packs from their own copy — packs must never include files from the game, only your own reworked DDS files under their hash names.

1. Set `texture_dump = true` in `gow2.toml` and play. Dumps land in `textures\4D53082D\dump` next to `gow2.exe`, one `.dds` file per unique texture, named `<hash>_<format>_<width>x<height>.dds`. Videos and other textures that change every frame are skipped.
2. Convert the dumps to PNG with `tools\texpack.py` (included in the release; needs Python 3 and Pillow, `pip install pillow`): `python tools\texpack.py to-png textures\4D53082D\dump work` converts the whole folder.
3. Upscale the PNG with any tool (Upflow on AMD GPUs, chaiNNer, Upscayl).
4. Convert back with a full mip chain: `python tools\texpack.py from-png work textures\4D53082D\replace`. File names are kept; the game matches each file by the hash at the start of its name. This writes uncompressed DDS; for files about 4x smaller compress with Microsoft texconv instead: `texconv -f BC7_UNORM -m 0 file.dds`.
5. Set `texture_replace = true` in `gow2.toml` and play. Before upscaling everything, check that replacement works with a solid color: `python tools/texpack.py tint <dump.dds> <replace.dds>` and look for the tinted texture in game.

A replacement is picked up when it has at least as many mip levels as the game texture (a full chain down to 1x1, which `from-png` writes), keeps the same aspect ratio, and is at least as large as the original.

## System Link

LAN play for Horde and Versus is being implemented in the runtime. Done so far: `system_link_trace = true` logs every network and session call the game makes (`[netlink]` lines), unimplemented calls return proper errors, and socket options reach Windows in the right byte order (the game's 128 KiB receive buffer used to become 512 bytes; reported upstream as rexglue-sdk#512). Next: real XNet addresses and the session service, so two PCs (or two instances on one PC) find each other. See [ROADMAP.md](ROADMAP.md).

## Known issues

- **Mouse look feels wrong.** The mouse is turned into a right-stick movement, and the game ignores any stick deflection under 30% (its `DeadZone=0.3` binding), so slow mouse movement is lost and fast movement jumps. A fix that removes that dead zone for the mouse while keeping it for controllers is in progress.
- **Button prompts show Xbox buttons** even when you play with the keyboard. Keyboard labels are in progress.
- **Green-looking lighting** has been reported on skin, lights and the menu flames; it has not been reproduced on the development PC yet. Screenshots are welcome in an issue.
- Only Act 1 has been played so far.

## Building from source

See [BUILDING.md](BUILDING.md). The main scripts are `scripts/build.ps1`, `scripts/build-sdk.ps1` (optional — adds FidelityFX/FSR, temporal upscalers and system link tracing) and `scripts/package.ps1`.

## Finding the functions codegen misses

`tools/harvest_hints.py` scans the decrypted image (exported from Ghidra with `tools/ghidra/ExportImage.java`) against a baseline codegen without hints (`scripts/codegen-baseline.sh`) and emits `hints/harvested.toml`: adjustor thunks, `.pdata` entries, code pointers stored in `.rdata`/`.data` and addresses built in code with `lis`+`addi`/`ori`. Hints that split an existing function are detected from the codegen log by `tools/hint_fixpoint.py` and kept out through `hints/denylist.txt`. Anything still missing is found at runtime by `scripts/run-harvest-loop.sh` (boot, read the "unregistered function" FATAL, add it to `hints/runtime.toml`, rebuild).

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (BSD-3-Clause, derived from Xenia) — runtime and code generator.
- [OverkillLabs3/gears-judgement-recomp](https://github.com/OverkillLabs3/gears-judgement-recomp) (Gears of War: Judgment on ReXGlue, BSD-3-Clause) — this repo copies `tools/patch_switch_traps.py` and `tools/check_objects_fresh.py` from it and adapts `scripts/build.ps1` from its `scripts/build.ps1`. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- [XenonRecomp](https://github.com/hedge-dev/xenonrecomp) (hedge-dev).
- [Xenia](https://github.com/xenia-project/xenia).
- AMD FidelityFX SDK (MIT).
- NVIDIA DLSS SDK.
- [PresentMon](https://github.com/GameTechDev/PresentMon) (used for measurements).

## Legal

Unofficial project. Not affiliated with or endorsed by Microsoft, Xbox Game Studios, The Coalition, or Epic Games. All trademarks belong to their owners. This repository's own code is licensed under the BSD-3-Clause License — see [LICENSE](LICENSE).
