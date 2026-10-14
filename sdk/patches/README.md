# SDK patches

`scripts/build-sdk.ps1` applies these in file-name order on top of ReXGlue commit `923c1a5` and
builds `rexruntime.dll` and `rexgpu-xenos.dll` from source. Keep one change per patch file.

| Patch | What it adds |
|---|---|
| `0002-temporal-upscalers.patch` | Temporal upscaling (FSR 3.1 / FSR 4 through AMD's signed FidelityFX SDK 2.3 DLLs, DLSS on NVIDIA): jitter, motion vectors from the view-projection in the vertex constants, depth. Ported from gears-judgement-recomp; off by default (`gow2_temporal`, `gow2_upscaler`). Notes in `0002-temporal-upscalers.md`. |
| `0003-system-link-trace.patch` | System Link milestone M0/M1: `system_link_trace` logs every network and session call (`[netlink]`), unimplemented calls return defined errors, `getsockopt` works, and `setsockopt` swaps option values to host byte order (a 128 KiB receive buffer used to become 512 bytes). |
| `0004-texture-packs.patch` | Texture dump and replacement (`texture_dump`, `texture_replace`, `texture_pack_dir`): content-hashed DDS dumps in `textures/<TITLEID>/dump`, replacements (same size or larger, full mip chain) from `textures/<TITLEID>/replace`. Video planes and other textures whose content changes on every load are skipped. |
| `0005-pad-deadzone-and-file-overrides.patch` | `GOW2_VFS_OVERRIDES` lets the game read individual files from elsewhere (a cached, edited copy of the player's own Coalesced bundles) without touching the game folder, and `pad_right_stick_deadzone` gives physical controllers a radial right-stick dead zone, so the game's own look dead zone can be turned off for the mouse. |
| `0006-mnk-mouse-stick-switch.patch` | `mnk_mouse_stick` (on by default) lets the game stop the keyboard/mouse driver from turning mouse motion into right-stick deflection while it keeps the mouse captured, so the port can hand the mouse to the game's own PC mouse path instead. |

## Working on a patch

Each patch lives in a git worktree of rexglue-sdk based on `923c1a5` plus the patches before it.
After editing, `scripts/verify-sdk-patch.ps1 -Worktree <worktree>` applies the worktree's changes to
the prepared build tree and compiles both DLLs in seconds; then regenerate the patch with
`git diff --binary HEAD > sdk/patches/<name>.patch` and run `scripts/build-sdk.ps1`.

Android bring-up uses `scripts/build-sdk-android.ps1` (NDK r29+); see `docs/ANDROID.md`.
