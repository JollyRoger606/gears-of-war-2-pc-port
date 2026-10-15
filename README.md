# Gears of War 2 — native PC port (unofficial)

[![Latest release](https://img.shields.io/github/v/release/santiquiroz/gears-of-war-2-pc-port?label=release)](https://github.com/santiquiroz/gears-of-war-2-pc-port/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/santiquiroz/gears-of-war-2-pc-port/total)](https://github.com/santiquiroz/gears-of-war-2-pc-port/releases)
[![License: BSD-3-Clause](https://img.shields.io/github/license/santiquiroz/gears-of-war-2-pc-port)](LICENSE)
[![GitHub stars](https://img.shields.io/github/stars/santiquiroz/gears-of-war-2-pc-port?style=social)](https://github.com/santiquiroz/gears-of-war-2-pc-port/stargazers)

**Website:** [santiquiroz.github.io/gears-of-war-2-pc-port](https://santiquiroz.github.io/gears-of-war-2-pc-port/)

Retail Xbox 360 Gears of War 2 statically recompiled from the player's own `default.xex` to native x64 code with the ReXGlue SDK. This is not emulation. Players must own the game. An earlier fan port ("Hollow") was built from a leaked 2008 development build; this one runs the complete retail game.

The project's short name, `gow2-recomp`, names the release zip; the repository is `gears-of-war-2-pc-port`.

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
- [x] Mouse aim through the game's own PC mouse axes, with no stick dead zone (`gow2_mouse_aim`, on by default)
- [x] FSR 4 and FSR 3.1 temporal upscaling (`gow2_temporal`, `gow2_upscaler`; off by default). The release has no DLSS
- [ ] Keyboard keys handled as on a PC (they still pass through an emulated controller)
- [ ] Full campaign play-through
- [x] System Link campaign co-op for up to four players (`gow2_coop_players`, off by default): four copies on one PC (loopback), and a PC with an Android phone on one LAN for two players, verified 2026-10-14 and 2026-10-15
- [x] Internet play, experimental (`net_peers`, `net_public_address`, `net_allow_peers`): a phone on mobile data joined a PC-hosted campaign through ports opened by UPnP, verified 2026-10-15; a PC friend outside the network has not been tried yet
- [ ] System Link Horde and Versus
- [ ] Android: a build from the repository reaches gameplay and joins a PC's System Link campaign, but runs at a few fps with parts of the scene black; not playable yet, no APK published ([docs/ANDROID.md](docs/ANDROID.md))

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
- `gow2_temporal`, `gow2_upscaler` — temporal upscaling: `gow2_temporal = true` with `gow2_upscaler = "fsr4"` (Radeon RX 7000/9000) or `"fsr3"`; off by default.
- `mnk_sensitivity` — mouse sensitivity.
- `gow2_save_dir` — folder for saves, profile settings and achievements; point it at a OneDrive, Google Drive or Dropbox folder to keep the campaign in the cloud ([docs/SAVES.md](docs/SAVES.md)).

The community patches come from [xenia-canary/game-patches](https://github.com/xenia-canary/game-patches) (authors illusion and boma); the patched sites and values for this XEX are applied to the generated code, each behind its `gow2_*` setting.

## Performance

Measured on a Ryzen 9 7900X3D + Radeon RX 7800 XT, Act 1, `resolution_scale = 2` (1440p internal): about 90 presented frames per second on average, 1% low 55, GPU about 6 ms per frame (PresentMon 2.6, 30 s capture). These numbers are presented frames.

## Modern enhancements

- Native resolution via `resolution_scale` (see Settings).
- FSR: the release runtime is built with AMD FidelityFX. `present_effect = "fsr"` renders at a lower `resolution_scale` and upscales with FSR 1 plus sharpening; `"cas"` only sharpens. Measured with `resolution_scale = 1` and FSR 1 to 1440p: about 85 fps in Act 1.
- Temporal upscaling: FSR 4.1 on Radeon RX 7000/9000 and FSR 3.1 on other GPUs, through AMD's signed FidelityFX SDK 2.3 DLLs, ported from the Gears of War: Judgment recompilation. Off by default: set `gow2_temporal = true` and `gow2_upscaler = "fsr4"` or `"fsr3"`, and lower `resolution_scale` so the upscaler has room to work. FSR 4 was checked in game on a Radeon RX 7800 XT (1280x704 upscaled to 2560x1408).
- No DLSS in the release: NVIDIA's DLSS SDK license does not allow it in an open-source release (see THIRD_PARTY_NOTICES.md). Only builds made from source with your own copy of NVIDIA's DLSS SDK can use it.
- Texture packs: see the next section. Tested with a texture replaced by a version twice its size:

  ![A title-screen texture replaced by a recoloured 2048x2048 version](docs/screenshots/texture-pack-test.jpg)

## Texture packs

Replace game textures with higher-resolution versions (for example upscaled 4x) for your own use. Each player builds packs from their own copy. An upscaled or recoloured texture is still derived from the game's copyrighted art, so do not share packs made from the game's textures; this project does not host or link them.

1. Set `texture_dump = true` in `gow2.toml` and play. Dumps land in `textures\4D53082D\dump` next to `gow2.exe`, one `.dds` file per unique texture, named `<hash>_<format>_<width>x<height>.dds`. Videos and other textures that change every frame are skipped.
2. Convert the dumps to PNG with `tools\texpack.py` (included in the release; needs Python 3 and Pillow, `pip install pillow`): `python tools\texpack.py to-png textures\4D53082D\dump work` converts the whole folder.
3. Upscale the PNG with any tool (Upflow on AMD GPUs, chaiNNer, Upscayl).
4. Convert back with a full mip chain: `python tools\texpack.py from-png work textures\4D53082D\replace`. File names are kept; the game matches each file by the hash at the start of its name. This writes uncompressed DDS; for files about 4x smaller compress with Microsoft texconv instead: `texconv -f BC7_UNORM -m 0 file.dds`.
5. Set `texture_replace = true` in `gow2.toml` and play. Before upscaling everything, check that replacement works with a solid color: `python tools/texpack.py tint <dump.dds> <replace.dds>` and look for the tinted texture in game.

A replacement is picked up when it has at least as many mip levels as the game texture (a full chain down to 1x1, which `from-png` writes), keeps the same aspect ratio, and is at least as large as the original.

## System Link

Campaign co-op over System Link works between PCs and between several copies on one PC; an Android build joined a PC for two-player co-op. It is off by default: set `net_link_active = true` in `gow2.toml` (or pass `--net_link_active=true`) on every player's copy, and use the same build everywhere; `gow2_coop_players` 3 or more turns it on by itself. The game's UDP port 1000 becomes 41000 on every platform (`net_port_offset`, because Android cannot bind ports below 1024) and the System Link beacon uses UDP 14001, so a PC that hosts needs a Windows Firewall rule for those two ports. `system_link_trace = true` logs every network and session call the game makes (`[netlink]` lines).

- [docs/NETWORK_PLAY.md](docs/NETWORK_PLAY.md): a PC and a phone on one network, the settings, several copies on one PC, troubleshooting.
- [docs/COOP_N_PLAYERS.md](docs/COOP_N_PLAYERS.md): co-op with more than two players (`gow2_coop_players`; four verified on one PC) and what is still missing, such as chapter travel with extra players.
- [docs/ONLINE.md](docs/ONLINE.md): playing over the internet. From the repository (not in the release zip), [`scripts/host-online.ps1`](https://github.com/santiquiroz/gears-of-war-2-pc-port/blob/master/scripts/host-online.ps1) `-Start` opens UDP 14001 and 41000 through UPnP on every router between the PC and the internet, with a lease (6 h by default, 24 h at most), and schedules an automatic `-Stop` for the end of the lease; `-Stop` closes them. Run `-Stop` after every session: some routers ignore the lease. Without UPnP: forward by hand; with no open ports: Tailscale or ZeroTier. Verified on 2026-10-15: a phone on mobile data joined a PC host behind two routers in a row.

Horde and Versus over System Link have not been tried. See [ROADMAP.md](ROADMAP.md).

## Known issues

- **Mouse aim changed in 0.1.1.** In 0.1.0 the mouse was turned into a right-stick movement, and the game ignored any stick deflection under 30% (its `DeadZone=0.3` binding). Now the mouse goes through the game's own PC mouse axes (`gow2_mouse_aim`), with no dead zone, while controllers keep the game's own aiming. Keys still pass through an emulated controller. Reports of how it feels are welcome in an issue.
- **Hints show Xbox button pictures.** Menu prompts show keyboard keys ([Space], [E]...) unless a controller is connected at launch (`gow2_prompts` = `auto`, `keyboard` or `controller`); the button pictures inside in-game hints still show Xbox buttons.
- **Green-looking lighting** has been reported on skin, lights and the menu flames; it has not been reproduced on the development PC yet. Screenshots are welcome in an issue.
- **Co-op with three or four players is early.** A chapter change, the death of any player, a player leaving, cinematics and vehicles can break it ([docs/COOP_N_PLAYERS.md](docs/COOP_N_PLAYERS.md)).
- **Every player needs the same build.** There is no version check yet, and a build without patch 0011 (such as 0.1.0) cannot see a 0.1.1 host.
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
- AMD FidelityFX SDK: FSR 1 and CAS (MIT). The FSR 4 and FSR 3.1 DLLs in the release are AMD's signed binaries, redistributed under AMD's license, not MIT (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
- NVIDIA DLSS SDK (builds from source only; not in the release).
- [PresentMon](https://github.com/GameTechDev/PresentMon) (used for measurements).

## Legal

Unofficial project. Not affiliated with or endorsed by Microsoft, Xbox Game Studios, The Coalition, or Epic Games. All trademarks belong to their owners. This repository's own code is licensed under the BSD-3-Clause License — see [LICENSE](LICENSE).

The screenshots (`docs/screenshots`, the `docs/assets/img` files other than `icon-320.webp`, `docs/assets/og-image*.jpg` and `assets/icon/og-image*.png`) show Gears of War 2 content, © Microsoft Corporation (originally © 2008 Epic Games, Inc.), captured from the game running in this port. They are not covered by the BSD-3-Clause license. The release zip also carries AMD and Microsoft runtime DLLs under their own licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
