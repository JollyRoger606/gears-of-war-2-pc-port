# Android port: plan

Status: research done (October 2026), no Android build yet. Target device for bring-up: Samsung
Galaxy S25 Ultra (Snapdragon 8 Elite, Adreno 830, Vulkan 1.3).

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
   `sdk/patches/` like the others, and offered upstream.
2. **The recompiled game links for arm64** (the generated C++ uses about 42,000 x86 SSE
   intrinsics; simde maps them to NEON).
3. **App shell:** SDL3 activity, game folder chosen by the player (their own extracted disc,
   copied to app storage), controller and touch input, a separate `:game` process.
4. **Bring-up on the S25 Ultra** with the stock driver first, then Turnip through adrenotools;
   record `vulkaninfo` for the device.
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
