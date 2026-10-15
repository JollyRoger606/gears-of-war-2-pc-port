# SDK patches

Both SDK builds apply every patch here in file-name order on top of ReXGlue commit `923c1a5`:
`scripts/build-sdk.ps1` builds `rexruntime.dll` and `rexgpu-xenos.dll` from the result, and
`scripts/build-android.ps1` builds the Android libraries from its own clean clone of the same series
(`out\sdk-android-src`). Keep one change per patch file.

| Patch | What it adds |
|---|---|
| `0002-temporal-upscalers.patch` | Temporal upscaling (FSR 3.1 / FSR 4 through AMD's signed FidelityFX SDK 2.3 DLLs, DLSS on NVIDIA): jitter, motion vectors from the view-projection in the vertex constants, depth. Ported from gears-judgement-recomp; off by default (`gow2_temporal`, `gow2_upscaler`). Notes in `0002-temporal-upscalers.md`. |
| `0003-system-link-trace.patch` | System Link milestone M0/M1: `system_link_trace` logs every network and session call (`[netlink]`), unimplemented calls return defined errors, `getsockopt` works, and `setsockopt` swaps option values to host byte order (a 128 KiB receive buffer used to become 512 bytes). |
| `0004-texture-packs.patch` | Texture dump and replacement (`texture_dump`, `texture_replace`, `texture_pack_dir`): content-hashed DDS dumps in `textures/<TITLEID>/dump`, replacements (same size or larger, full mip chain) from `textures/<TITLEID>/replace`. Video planes and other textures whose content changes on every load are skipped. |
| `0005-pad-deadzone-and-file-overrides.patch` | `GOW2_VFS_OVERRIDES` lets the game read individual files from elsewhere (a cached, edited copy of the player's own Coalesced bundles) without touching the game folder, and `pad_right_stick_deadzone` gives physical controllers a radial right-stick dead zone, so the game's own look dead zone can be turned off for the mouse. |
| `0006-mnk-mouse-stick-switch.patch` | `mnk_mouse_stick` (on by default) lets the game stop the keyboard/mouse driver from turning mouse motion into right-stick deflection while it keeps the mouse captured, so the port can hand the mouse to the game's own PC mouse path instead. |
| `0007-android-arm64-port.patch` | The Android arm64 port of the runtime (NDK r29+, API 29). Build: an `android-arm64` platform, no `-march=armv8-a` that would lower the game's `armv8.2-a`, SDL3 as the shared `libSDL3.so` SDLActivity loads (no X11/Wayland/ALSA), `SDL_main` from `windowed_app_main_sdl.cpp`, Vulkan from the system `libvulkan`, the SDK version read from the SDK's own git history under `add_subdirectory`. Core: `GetAndroidApiLevel()` (`main_android`), guest memory through `ASharedMemory` with a `memfd_create` fallback (no `/dev/ashmem`), Bionic fixes (64-bit `off_t`, no robust mutexes, `clock_cast`, and `"a+"` streams rewound after opening: Bionic starts their reads at the end of the file, so the Vulkan shader and pipeline storage found no header and truncated itself on every launch), an AArch64 fiber switch that keeps the callee-saved x19-x28, fp, lr and d8-d15 (Bionic has no `ucontext`), unclaimed `SIGSEGV`/`SIGBUS`/`SIGILL` chained to the previous handler instead of looping, `REX_EXECUTABLE_FOLDER` for config and logs, `content://` stubs. UI: an `ANativeWindow` surface from the SDL window. The GPU plugin is loaded by name from the APK. Input: one recursive lock across the `InputSystem` entry points and a non-throwing `utf8::next` (the Windows build gets both from the port's own guards). `XnAddrStatus` constants get definitions. Guest sockets: ports 1-1023 bind 30000 higher (apps may not bind them), and `SOL_SOCKET` options and `FIONBIO`/`FIONREAD` with Winsock numbers are translated. Touch: the SDL window delivers finger events as `TouchEvent` and drops the mouse events SDL synthesizes from fingers and pens, an `ImGuiDialog` can say it does not need repainting at the display's refresh rate (`WantsContinuousPaint`), and the ImGui drawer releases a touch whose lift arrived before any frame saw the press one frame later, so a quick tap on the settings overlay is a click. FFmpeg runs its C code: `thirdparty/ffmpeg-android/config.h` comes before FFmpeg's own `config.h` and turns the aarch64 assembly off, so the FFmpeg submodule is not modified. Every change is behind `ANDROID` / `REX_PLATFORM_ANDROID` / `__ANDROID__`, so Windows behaves as before. |

## Working on a patch

Each patch lives in a git worktree of rexglue-sdk based on `923c1a5` plus the patches before it.
After editing, `scripts/verify-sdk-patch.ps1 -Worktree <worktree>` applies the worktree's changes to
the prepared build tree and compiles both DLLs in seconds; then regenerate the patch with
`git diff --binary HEAD > sdk/patches/<name>.patch` and run `scripts/build-sdk.ps1`.

The Android port is worked on in a worktree of `923c1a5` with the whole series applied (committed
or not; only the files count). Build it with `scripts/build-android.ps1 -SdkSource <worktree>`, then regenerate
0007 as the difference between `923c1a5` plus 0002-0006 and the worktree, using a temporary index so
the worktree's own index and commits stay as they are (Git Bash, from the worktree):

```sh
patches=/c/path/to/gow2-recomp/sdk/patches
export GIT_INDEX_FILE=$(mktemp -u)
git read-tree 923c1a5
for p in "$patches"/000[2-6]-*.patch; do git apply --cached "$p"; done
base=$(git write-tree)
git read-tree HEAD && git add -A && port=$(git write-tree)
rm -f "$GIT_INDEX_FILE"; unset GIT_INDEX_FILE
git diff --binary "$base" "$port" > "$patches/0007-android-arm64-port.patch"
```

`scripts/build-android.ps1` runs this comparison itself before every build, for `-SdkSource` and for its own clone, and lists the paths that differ. Check that the series still reproduces the worktree: applying 0002-0007 with `git apply --cached`
to a temporary index read from `923c1a5` must give the same `git write-tree` as `$port`.

`scripts/build-sdk-android.ps1` builds the SDK alone for Android (bring-up); see `docs/ANDROID.md`.
