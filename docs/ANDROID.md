# Android port: plan

Status (October 2026): `scripts/build-android.ps1` builds an APK with the game's own libraries
(SDK, GPU plugin and the recompiled game for arm64) from a clean clone of this repository, the SDK
included, and on the phone the game boots to the PRESS START title screen in landscape with the
intro video and audio. Target device for bring-up: Samsung Galaxy S25 Ultra (Snapdragon 8 Elite,
Adreno 830, Vulkan 1.3).

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
`-SdkSource <tree>` builds your own SDK tree instead (the worktree the port is changed in); the
script first checks that the tree has the current patches and names what is missing.

The APK's `lib/arm64-v8a` holds `libSDL3.so`, `librexruntime.so`, `librexgpu-xenos.so`,
`libgow2.so` and `libc++_shared.so`. SDLActivity loads them in that order and runs `SDL_main` from
`libgow2.so`; there is one SDL in the process (the runtime links the shared `libSDL3.so` the Java
side loads). The check verifies that every library's undefined symbols resolve the way Android's
linker resolves them (own `DT_NEEDED` closure, API-29 system stubs), that only allowed system
libraries are needed, that only `libSDL3.so` defines SDL, and that every segment and every library
in the APK is 16 KB aligned.

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
decoder, and `android/gamelibs/gow2_android_glue.cpp` suppresses the disc-error box.

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
   the S25 Ultra; input mapping and the `:game` process remain.
4. **Bring-up on the S25 Ultra** with the stock driver first, then Turnip through adrenotools;
   record `vulkaninfo` for the device. Started in October 2026: with the stock driver the game
   reaches the PRESS START title screen.
5. **Measure.** If the generic Xenos path cannot hold 30 fps in Act 1, the proven alternative is a
   renderer written for Unreal Engine 3's draw calls, which is a large project of its own.
6. **More players and online** last, through hooks into the engine's session code (shared with
   the PC System Link work).

## Biggest unknowns

- Whether the Xenos emulation (EDRAM resolves, memexport) is fast enough for UE3 on the Adreno 830.
- Which Vulkan features the S25's stock driver exposes, and how mature Turnip is on it.
- CPU headroom for the game's threads on the Snapdragon 8 Elite.
- Legal exposure: several Android ports of other games were taken down in October 2026. This
  project only ever ships its own code; players bring their own disc.
