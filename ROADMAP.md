# Roadmap

The order matters: a faithful, complete port first; portability second; enhancements last.
Each phase keeps the previous one working.

## Phase 1 - Complete port (retail parity)

- [x] Retail `default.xex` recompiles, builds and boots; title screen, main menu, campaign intro.
- [x] Act 1 loads and runs in real time (input race in the runtime fixed with the XAM input guard).
- [x] First level playable with the keyboard (Act 1, early).
- [ ] The whole campaign (solo and split-screen co-op).
- [x] Saves, profile settings and achievements in any folder: `gow2_save_dir` (`saves` next to
      the executable for a portable install, or a cloud-synced folder; [docs/SAVES.md](docs/SAVES.md)).
- [ ] Keyboard and mouse that feel native. Mouse aiming now goes through the engine's own PC
      mouse axes (`MouseX`/`MouseY` into `aMouseX`/`aMouseY`), with no stick emulation and no
      aim friction or adhesion for the mouse (`src/gow2_mouse_aim.cpp`, SDK patch 0006); the
      controller keeps the game's own aiming. Still to do: real key events through the engine's
      `InputKey`, so keys stop going through the emulated controller.
- [ ] Button prompts and icons that follow the device in use (keyboard keys while playing with
      the keyboard, Xbox or generic controller glyphs otherwise), including the button
      pictures inside hints, through input-aware texture variants.
- [ ] Full control mapping for Xbox controllers, keyboard and mouse, and generic controllers
      (anything SDL knows), editable in game.
- [ ] Title Update 6 support (`patch_file_path` for the `.xexp`), which Road to Ruin and
      later multiplayer fixes need.
- [ ] Horde and versus over System Link (LAN). A friend of the project reports he already
      added System Link to a Gears port; find out which one and whether it can be reused.
      System Link itself works for campaign co-op: ReXGlue 0.10.0.9 stubbed it (XGI sessions
      returned success without state, `XNetGetTitleXnAddr` advertised loopback), and SDK patches
      0010 and 0011 add sessions, real XNet addresses and the port remap
      ([docs/NETWORK_PLAY.md](docs/NETWORK_PLAY.md)). Horde and versus have not been tried. GrizzlyOne95's
      [LAN co-op audit](https://github.com/GrizzlyOne95/gears-judgement-recomp/blob/main/docs/LAN_COOP_NETWORK_AUDIT.md)
      for Judgment lays out the trace -> session service -> replication milestones.
- [ ] Stability: no unregistered-function FATALs in a full playthrough (hints complete),
      known Xenia-era GPU issues for this title fixed (character shading, lens flares,
      occlusion queries).

## Phase 2 - Portable base

Goal: the same recompiled game on other systems, the way the Halo: Combat Evolved community
port reached more platforms and added features.

- [ ] Keep everything platform-neutral: game-specific code only in `src/` and `hints/`, no
      Windows-only assumptions outside `#ifdef _WIN32` (ReXGlue already builds for Linux
      x64/ARM64 with Vulkan; the Mass Effect port runs on ARM64).
- [ ] Linux x64 build (Vulkan) as the first non-Windows target.
- [ ] Android ARM64 (Vulkan): packaging, touch and gamepad input, storage access to the
      user's own game files, memory limits. Plan, precedents (two ReXGlue games already run on
      Android) and the SDK changes it needs: [docs/ANDROID.md](docs/ANDROID.md).
- [ ] Touch controls on Android in the style of mobile shooters (left virtual stick, drag to
      look through the same native mouse path, buttons for fire, aim, cover, reload, melee and
      weapon swap), plus controllers and keyboard with or without a mouse.
- [x] Cross-play between PC and Android: a PC and a phone joined each other's System Link
      campaign on one LAN on 2026-10-15 (patch 0011, [docs/NETWORK_PLAY.md](docs/NETWORK_PLAY.md));
      in game the phone runs at about 30 fps for the first minutes from a cool start, falling to
      about 10-12.5 fps once warm ([docs/ANDROID.md](docs/ANDROID.md)).
- [ ] Campaign co-op for more than two players. The cap of two is in the game's script
      (`GearGameSP_Base.InitGame` sets `MaxPlayers = 2`, the co-op menu
      `NumPublicConnections = 2`), while versus already takes 10. Extra players would join
      as Carmine-style COG soldiers (`SuggestPawnClassFor` already falls back to
      `GearPawn_COGGear` for a third player). Target four first, then as many as the
      cinematics, checkpoints and bandwidth allow. Stage 1 is done: four players on one PC played
      Act 1 chapter 2 on 2026-10-15 ([docs/COOP_N_PLAYERS.md](docs/COOP_N_PLAYERS.md)); Stage 2
      is implemented and ran with four copies on one PC on 2026-10-16 (chapter changes, deaths,
      leave and rejoin, checkpoints, cinematics, players 3+ on foot unhurt while a hero rides);
      co-op splits, vehicle exit, the Ride-Reaver section, four PCs over the internet, Android
      with 3+ players and Stage 3 (8-10 players, bandwidth) remain.
- [ ] Online play beyond LAN (System Link over the internet / relay). Built in patch 0011
      (`net_peers`, `net_public_address`, `net_allow_peers`; [docs/ONLINE.md](docs/ONLINE.md)),
      not tested over the internet yet.

## Phase 3 - Modern enhancements

- [ ] Native resolution above 720p (the guest render size becomes a setting; Judgment
      patches its hardcoded 720p the same way).
- [ ] Unlocked frame rate with correct game timing.
- [ ] Upscalers: FSR (and DLSS on NVIDIA) through the GPU plugin, as gears-judgement-recomp
      does.
- [ ] Higher quality textures (4K packs) through a texture replacement path that loads
      user-supplied files; no original assets are redistributed.
- [ ] Graphics options (shadows, anisotropic filtering, AA, film grain, motion blur).
- [ ] The new options inside the game: graphics (render resolution, upscaler, texture packs)
      and controls (mapping, sensitivity), from the game's Options screen or the runtime's
      overlay, instead of only `gow2.toml`.

## Rules that apply to every phase

- The repository never contains game code, generated C++ or game assets: players bring their
  own retail copy.
- Findings that help other ports go upstream (ReXGlue, REA, Ghidra) as issues or pull
  requests.
