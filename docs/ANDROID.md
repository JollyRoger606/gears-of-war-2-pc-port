# Android port: plan

Status (October 2026): `scripts/build-android.ps1` builds an APK with the game's own libraries
(SDK, GPU plugin and the recompiled game for arm64) from a clean clone of this repository, the SDK
included. On the phone the game plays the start of Act 1 with on-screen touch controls (also
controllers, and a keyboard and mouse attached at launch): menus, the campaign intro, the opening
cutscene and gameplay, where Marcus moves, turns the camera and fires. From a cool start (thermal
status 0, skin 37.1 C), the current build (commit `373c1e8` and later docs) ran at the game's 30 fps
cap (29.7 fps at 734 MHz), 27.9 at 607 MHz, 25.0 at 525 MHz and 23.2 at 443 MHz in the first
minutes, then 15.8-12.7 fps once the phone had settled at a 342-222 MHz cap (after about five
minutes, on USB power). GPU cost was 19.5 Mcycles per game frame (median of quiet windows), against
24.2 for the October 15 morning build, which still had the black regions. At the same 222 MHz cap,
the current build draws 17 % more frames than that build. Boot builds
852 stored pipelines in 2.7 s instead of 27.5 s, and the panel runs at 30 Hz. On October 15, the
black regions were traced to a depth mismatch between the prepass and base-pass vertex shaders on
Adreno and fixed with the Android color depth bias default of 16 (step 4). Light environment
shadows caused the white lamp objects and hanging banner; the current build's Android default
`LightEnvironmentShadows=0` removes them while keeping the world's dynamic shadows. The cause in
the Vulkan backend or driver and the colour grade difference from the PC remain open.
Target device for bring-up: Samsung Galaxy S25 Ultra
(Snapdragon 8 Elite, Adreno 830, Vulkan 1.3). Controls:
[`android/README.md`](../android/README.md#controls). [`ANDROID_EXPERIMENTS.md`](ANDROID_EXPERIMENTS.md)
has the experiments, their results and what comes next.
On October 15 (evening), the current build (commit `d65fe7d`) joined a co-op campaign hosted by
the v0.1.1 PC build over the home LAN; both lobbies listed both players, both loaded Act 1 chapter 2,
and each player saw the other in game. As the client in the hospital, the phone ran at 30 fps at
443-660 MHz for about 70 s, then 27-30 fps at 342-389 MHz (a short run, not a sustained rate, and
a different scene from the courtyard measurements above). See [NETWORK_PLAY.md](NETWORK_PLAY.md)
for the setup.

## Building the APK

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-android.ps1 -LowPriority
```

From a clean clone the script runs the whole chain, skipping what is current: a clone of ReXGlue
`923c1a5` in `out\sdk-android-src` with every `sdk\patches\*.patch` applied (the same series as the
Windows SDK build, ending in `0007-android-arm64-port.patch`), `rexglue codegen` if
`generated\default` is missing (needs `-Sdk` and your own `game\default.xex`), an Android copy of
the generated code in `out\android\generated\default` with the ARM memory barriers
(`tools/patch_memory_barriers.py` never touches `generated\default`), one CMake build of the SDK and
the game from [`android/gamelibs`](../android/gamelibs/CMakeLists.txt) in `out\android\build`, the
staged libraries in `out\android\jniLibs\arm64-v8a`, `tools/check_android_libs.py`, and Gradle with
`-Pgow2.gameLibraries=true`. Details and requirements: [`android/README.md`](../android/README.md#game-build).
`-SdkSource <tree>` builds your own SDK tree instead (the worktree the port is changed in). Either
tree is first compared with `923c1a5` plus every patch: changes beyond the series are listed, and a
tree that lacks part of the series, or changes lines it adds, stops the build with the paths that
differ. The libraries are compiled with `-ffile-prefix-map`, so they hold no absolute paths of the
checkout, the SDK tree or the NDK, and the check fails when one appears.

The APK's `lib/arm64-v8a` holds `libSDL3.so`, `librexruntime.so`, `librexgpu-xenos.so`,
`libgow2.so` and `libc++_shared.so`. SDLActivity loads them in that order and runs `SDL_main` from
`libgow2.so`; there is one SDL in the process (the runtime links the shared `libSDL3.so` the Java
side loads). The check verifies that every library's undefined symbols resolve the way Android's
linker resolves them (own `DT_NEEDED` closure, API-29 system stubs), that only allowed system
libraries are needed, that only `libSDL3.so` defines SDL, and that every segment and every library
in the APK is 16 KB aligned.

`libgow2.so` exports only `SDL_main`, which `SDLActivity` finds with `dlsym`; the version script
`android/gamelibs/libgow2.map` keeps the recompiled code's ~98,000 symbols local. That shrank
`libgow2.so` in the APK from 62.2 MB to 52.0 MB (the whole APK, packed from scratch, is 67.6 MB)
and its load time from 10-15 ms to 2-7 ms. The check fails if it exports anything besides `SDL_main` (`--main-exports` lists the
allowed names).

The SDK side of the port is [`sdk/patches/0007-android-arm64-port.patch`](../sdk/patches/README.md),
applied after 0002-0006, so the Android runtime also has the Coalesced file overrides
(`GOW2_VFS_OVERRIDES`), `pad_right_stick_deadzone` and `mnk_mouse_stick`. Every change in it is
Android-only (desktop builds unchanged). Besides the fixes listed under
[What breaks](#what-breaks-in-our-sdk-today-rexglue-923c1a5), it changes the build:

- SDL3 is built as `libSDL3.so` (`SDL_SHARED`) instead of being linked statically into the runtime.
- `windowed_app_main_sdl.cpp` includes `SDL_main.h`, so `main` becomes the exported `SDL_main`.
- Android uses one app per library like the desktop (`XE_UI_WINDOWED_APPS_IN_LIBRARY` off), so
  `REX_DEFINE_APP` defines the `GetWindowedAppCreator()` the SDL entry point calls.
- `rexglue_apply_target_settings` no longer adds `-march=armv8-a` on Android, which lowered the
  `-march=armv8.2-a` baseline (LSE atomics) of the game code.
- FFmpeg runs its C code: `thirdparty/ffmpeg-android/config.h` comes before FFmpeg's own
  `config.h` on the include path and turns the aarch64 assembly off, so the FFmpeg submodule
  itself is not modified.

The port's Windows-only guards (`gow2_xam_input_guard.cpp`, `gow2_utf8_guard.cpp`,
`gow2_crash_watch.cpp`) are not built for Android. Patch 0007 gives the Android runtime the
XamInput serialization (one lock across the `InputSystem` entry points) and the non-throwing UTF-8
decoder, and `android/gamelibs/gow2_android_glue.cpp` suppresses the disc-error box. An invalid
byte decodes as a one-byte code point, as on Windows, not as U+FFFD. The guest XamInput entry points
all go through the locked `InputSystem` methods. `scripts/test-android-device.ps1` checks both on
the phone (18 checks, 0 failed on the S25 Ultra).

## What already exists

Several Xbox 360 recompilations run natively on Android since October 2026 (GTA IV, Skate 3, Need
for Speed, DMC4 and others; most activity is on r/AndroidNativePorts). Two of them are built on
ReXGlue, the same SDK as this port:

- **LibertyRecompAndroid** (GTA IV, https://github.com/vaduur/LibertyRecompAndroid). Its
  `docs/android/PORTING.md` is the best single reference: NDK r29+ (r27's libc++ lacks
  `std::jthread`), minSdk 29, `-march=armv8.2-a` for LSE atomics, `-fno-emulated-tls`, ThinLTO
  with `-Bsymbolic-functions`, 64-byte TLS alignment for Bionic, an `SDLActivity` subclass that
  sets `HOME`/`XDG_DATA_HOME` to the app's files folder, an installer that needs all-files access
  (game files behind `content://` URIs cannot be memory-mapped), `dladdr` instead of
  `/proc/self/exe`, audio threads at nice -16 (`SCHED_FIFO` is denied), vblank from
  `AChoreographer`, and the game in a separate `:game` process. It went from 11 to 30 fps at
  720p on a Snapdragon 865 after replacing the generic GPU path with a renderer for that game.
  **It is GPL-3.0: use it to learn what breaks, never copy its code into this BSD-3 repository
  or into the ReXGlue patches.**
- **skate3-android** (https://github.com/andrewnakas/skate3-android): 7.7 million generated lines
  built outside Gradle; the author reports that most hard bugs were Qualcomm driver bugs.

Both avoid emulating the Xbox 360 GPU (Xenos) during gameplay and draw with renderers written for
their game. No playable Android port on ReXGlue's generic GPU path is known. ReXGlue PR #346
(Kameo on an Adreno 740 with Turnip, desktop Linux) was closed unmerged.

The Halo: Combat Evolved port the project was inspired by is a decompilation (OpenCE), not a
recompilation: its added players come from rewritten netcode in readable source. Here the same
features need reverse engineering of Unreal Engine 3's session and network code.

## What breaks in our SDK today (ReXGlue 923c1a5)

Items 1 and 3 are fixed by patch 0007, item 2 by `tools/patch_memory_barriers.py` on the Android
copy of the generated code.

1. **It does not compile for Android.** `memory_posix.cpp` and `threading_posix.cpp` include
   `rex/main_android.h` and call `GetAndroidApiLevel()`, which do not exist, and nothing calls
   `memory::AndroidInitialize()`, so guest memory falls back to `/dev/ashmem`, which apps targeting
   API 29+ cannot open. Needs `ASharedMemory` with a `memfd_create` fallback.
2. **No memory barriers on ARM.** The code generator emits nothing for `sync`/`lwsync`/`eieio`
   (`src/codegen/builders/system.cpp`). x86 hides this; ARM's weaker memory model will not
   (upstream PR #463 was closed unmerged, issue #462). Floating-point issues #381, #476 and #477
   are open too.
3. **Unclaimed faults loop forever.** `exception_handler_posix.cpp` returns when no handler claims
   a SIGSEGV, so a real crash becomes a frozen screen; SIGBUS asserts. It must chain to the
   previous `sigaction` so Android's crash reporting sees real crashes.
4. **16 KB pages.** Guest 4 KB pages inside 16 KB host pages are already handled (Apple Silicon
   work); check the device with `getconf PAGE_SIZE`.
5. **Vulkan features.** The command processor requires `vertexPipelineStoresAndAtomics`; Adreno
   has no fragment shader interlock, so the host render-target path is used.
6. **Drivers.** Turnip (Mesa) on the Adreno 830 is early ("sysmem-only, gmem causes GPU hangs");
   the stock Qualcomm driver has known bugs with fragment-less pipelines and fences.

## Plan

1. **SDK builds for Android arm64.** An `android-arm64` preset on NDK 30 (minSdk 29,
   armv8.2-a, `-fno-emulated-tls`), a `main_android` shim, `ASharedMemory`/`memfd` guest memory,
   chained signal handlers, and memory barriers in the code generator. Kept as a patch in
   `sdk/patches/` like the others, and offered upstream. Done in October 2026 as
   `0007-android-arm64-port.patch`; the memory barriers are added to the generated code instead.
2. **The recompiled game links for arm64** (the generated C++ uses about 42,000 x86 SSE
   intrinsics; simde maps them to NEON). Done in October 2026: `libgow2.so` builds and links with
   `-Wl,--no-undefined`, and the APK passes `tools/check_android_libs.py`.
3. **App shell:** SDL3 activity, game folder chosen by the player (their own extracted disc,
   copied to app storage), controller and touch input, a separate `:game` process.
   The shell exists in [`android/`](../android/README.md), packages the game libraries with
   `-Pgow2.gameLibraries=true` and the license texts in `assets/licenses/`, and runs the game on
   the S25 Ultra. Input: on-screen touch controls (a virtual pad merged with real pads), SDL
   controllers, and the keyboard/mouse driver when a keyboard or mouse is attached at launch (not
   yet tried with a real keyboard or mouse). The `:game` process remains. On Android `gow2_prompts=auto` keeps the controller glyphs unless
   a physical keyboard drives the game.
4. **Bring-up on the S25 Ultra** with the stock driver first, then Turnip through adrenotools;
   record `vulkaninfo` for the device. Started in October 2026: with the stock driver the game
   reaches gameplay in Act 1 once the app may open sockets (`INTERNET`) and patch 0007 maps the
   guest's privileged port and Winsock socket options and ioctls. Large parts of the scene render
   black: about a third of the picture at the start of the first checkpoint (buildings and sky
   behind the first facade). The runtime logs no GPU error. On October 15 every GPU-side candidate
   was switched off in turn on the phone (fragment-less pipelines, the game's shadows and other
   effects, position invariance, sparse binding, render passes, float24 depth in the shader,
   geometry shaders, depth test, depth prepass, stencil, scissor, culling, texture limits, invalid
   fetch constants) and the black shape stayed the same; pipelines built late and missing or
   all-zero textures are ruled out too. With every scene draw painted a constant color, the region
   stays black: no scene draw reaches it, so those objects are either never drawn or drawn
   off-screen. A frame capture and a comparison with the PC at the same checkpoint come next
   ([`ANDROID_EXPERIMENTS.md`](ANDROID_EXPERIMENTS.md#results-of-the-october-15-session)).
   The second October 15 session found that some Adreno base-pass meshes produce depth up to about
   one float24 step farther than their prepass and fail GEQUAL; the earlier switches had not
   tested the intended draws. `vulkan_android_color_depth_bias` now defaults to 16 on Android,
   fixing the black regions while keeping the prepass and early depth rejection. The evidence
   and remaining white objects are in
   [`ANDROID_EXPERIMENTS.md`](ANDROID_EXPERIMENTS.md#black-scene-found-october-15-second-session).
5. **Measure.** If the generic Xenos path cannot hold 30 fps in Act 1, the proven alternative is a
   renderer written for Unreal Engine 3's draw calls, which is a large project of its own. First
   numbers (`gow2_frame_log`): 3 to 5 game frames per second in the first gameplay area with the
   GPU 99% busy but thermally capped at 160-222 MHz of 1.2 GHz, and 13.6 to 16.8 for the first
   seconds after the phone had cooled a little (GPU up to 734 MHz). Part of the slowest windows was
   not the clock but pipelines built during play: Bionic starts reading an `"a+"` stream at its
   end, so the Vulkan shader storage found no header and truncated itself at every launch. Patch
   0007 rewinds those streams; on the phone the storage now loads at boot (`Created 295 graphics
   pipelines from Vulkan storage` after the first gameplay). The larger cost was the SDK's
   `clear_memory_page_state`: at every frame end it marks every page the CPU uploaded as stale, so
   the whole working set (about 6,000 pages, 21 MB) was copied to the GPU again each frame in about
   1,850 uploads, each a transfer that ends the Vulkan render pass (about 1,600 render passes a
   frame for 3,000 draws). Patch 0007 turns it off on Android (the memory watches still catch CPU
   writes): at the same 222 MHz cap the game went from 4.6 to 9.4 fps (47.8 to 23.4 GPU
   megacycles per game frame), with 155 render passes and about 46 uploads a frame. From a cool
   start (thermal status 0) the first 40 s of gameplay now run at 26-29 fps with the GPU at
   734 MHz, close to the game's own 30 fps cap, and the rate follows the thermal cap down to
   9-13 fps at 222 MHz after about four minutes. `FRAMES` lines carry the GPU clock and thermal
   state, and `GPUSTATS` lines the command processor's work, so numbers can be read per clock.
   On October 15 afternoon, C8 measured ScreenPercentage 50 at 8.5/10.3 fps at 160/222 MHz and
   ScreenPercentage 75 at 9.8-10.2 fps at 222 MHz, while `resolution_scale=2` raised the cost to
   73.3-75.4 Mcycles/frame:
   most cost scales with the runtime's render-target work, shadows and full-resolution passes,
   so ScreenPercentage tiers are not a frame-rate lever.
   Disabling only light environment shadows removed the white objects and reached 12.3 fps at
   222 MHz, keeping the world's dynamic shadows; the current build makes this the Android default
   and measured 10.1 fps at 160 MHz and 12.5 fps at 222 MHz on a warm phone.
   From a cool start (thermal status 0, skin 37.1 C), the current build ran at the game's 30 fps cap
   (29.7 fps at 734 MHz), 27.9 at 607 MHz, 25.0 at 525 MHz and 23.2 at 443 MHz in the first minutes,
   then 15.8-12.7 fps at a 342-222 MHz cap after about five minutes on USB power, with a median
   quiet-window GPU cost of 19.5 Mcycles/frame against 24.2 for the October 15 morning build,
   which still had the black regions.
   With only the depth bias (before the shadow default), a cool start peaked at 23.6 fps at 734 MHz,
   but on USB power the phone went from thermal status 0 (skin 35.8 C) to status 3 about 90 s into
   gameplay.
   The phone cannot stay cool on USB power, so warm-phone A/B comparisons use Mcycles/frame and
   fps at the same capped clock.
6. **More players and online** last, through hooks into the engine's session code (shared with
   the PC System Link work).

## Next steps

Trace the root cause of the Adreno depth mismatch with per-shader readback of vertex 0 depth
(plan B1(c)). Investigate light environment shadows in the Vulkan backend or Adreno driver, and
fix the presenter semaphore error with per-swapchain-image semaphores.

Frame generation (plan E1) would use depth plus camera reprojection in the presenter between guest
frames, using the per-frame view-projection and camera position captured by patch 0002 and the
scene depth. Warp the last frame to t + 0.5 frame with the camera extrapolated from the last two
matrices, keep near-camera pixels (the third-person character) screen-locked, fill disocclusions
by edge stretch, and composite the HUD from a pre-HUD snapshot. Pace with FIFO and
`VK_GOOGLE_display_timing` (or Swappy, Apache-2.0), enable above 28 fps sustained for 2 s and disable
below 24 fps; the panel request must then be 60 Hz. The guest vblank is a fixed timer, so extra
host presents never reach game timing and remain cross-play safe. This is not worth starting
until the sustained base rate is near 28 fps; today it settles at 12.7-15.8 fps.

The codegen options `skip_msr` (plan C5) and register-as-local (C10) also change the PC build and
need a PC/System Link regression.

## Biggest unknowns

- Whether the Xenos emulation (EDRAM resolves, memexport) is fast enough for UE3 on the Adreno 830.
- Which Vulkan features the S25's stock driver exposes, and how mature Turnip is on it.
- CPU headroom for the game's threads on the Snapdragon 8 Elite.
- Legal exposure: several Android ports of other games were taken down in October 2026. This
  project only ever ships its own code; players bring their own disc.
