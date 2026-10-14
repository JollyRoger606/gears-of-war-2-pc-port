# Roadmap

The order matters: a faithful, complete port first; portability second; enhancements last.
Each phase keeps the previous one working.

## Phase 1 - Complete port (retail parity)

- [x] Retail `default.xex` recompiles, builds and boots; title screen, main menu, campaign intro.
- [x] Act 1 loads and runs in real time (input race in the runtime fixed with the XAM input guard).
- [x] First level playable with the keyboard (Act 1, early).
- [ ] The whole campaign (solo and split-screen co-op).
- [ ] Saves and checkpoints next to the executable (portable install).
- [ ] Keyboard and mouse that feel native, following what gears-judgement-recomp does for the same
      engine: mouse movement written straight into the look axes (`aTurn`/`aLookUp`) with the
      thumbstick smoothing and acceleration (`UGearPlayerInput.ViewAcceleration` natives)
      bypassed, real key events handed to the engine's own `InputKey`, and the
      `bUsingGamepad` flag cleared so the game shows keyboard prompts. Today the mouse is
      turned into a right-stick deflection, which goes through the game's dead zone and
      acceleration and feels wrong.
- [ ] Button prompts and icons that follow the device in use (keyboard keys while playing with
      the keyboard, Xbox or generic controller glyphs otherwise), including the button
      pictures inside hints, through input-aware texture variants.
- [ ] Full control mapping for Xbox controllers, keyboard and mouse, and generic controllers
      (anything SDL knows), editable in game.
- [ ] Title Update 6 support (`patch_file_path` for the `.xexp`), which Road to Ruin and
      later multiplayer fixes need.
- [ ] Horde and versus over System Link (LAN). A friend of the project reports he already
      added System Link to a Gears port; find out which one and whether it can be reused.
      ReXGlue 0.10.0.9 stubs most of it (XGI sessions return success without state,
      `XNetGetTitleXnAddr` advertises loopback); GrizzlyOne95's
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
- [ ] Online play beyond LAN (System Link over the internet / relay), more players in
      co-op and versus where the engine allows it.

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
