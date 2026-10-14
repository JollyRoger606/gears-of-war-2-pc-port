# Roadmap

The order matters: a faithful, complete port first; portability second; enhancements last.
Each phase keeps the previous one working.

## Phase 1 - Complete port (retail parity)

- [x] Retail `default.xex` recompiles, builds and boots; title screen, main menu, campaign intro.
- [ ] First level playable; then the whole campaign (solo and split-screen co-op).
- [ ] Saves and checkpoints next to the executable (portable install).
- [ ] Keyboard and mouse with real mouse look, rebinding, PC button prompts.
- [ ] Title Update 6 support (`patch_file_path` for the `.xexp`), which Road to Ruin and
      later multiplayer fixes need.
- [ ] Horde and versus over System Link (LAN). A friend of the project reports he already
      added System Link to a Gears port; find out which one and whether it can be reused.
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
      user's own game files, memory limits.
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

## Rules that apply to every phase

- The repository never contains game code, generated C++ or game assets: players bring their
  own retail copy.
- Findings that help other ports go upstream (ReXGlue, REA, Ghidra) as issues or pull
  requests.
