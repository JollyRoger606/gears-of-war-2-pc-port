# Android shell

The Android application of the port (step 3 of [docs/ANDROID.md](../docs/ANDROID.md)): an SDL3
activity, the game-folder setup and either a placeholder native library (the default) or the
game's own libraries, built by `scripts/build-android.ps1` (see [Game build](#game-build)).

This directory is part of gow2-recomp and is released under its BSD-3-Clause license
(see [`../LICENSE`](../LICENSE)). The Java files in `app/src/main/java/org/libsdl/app/`,
`app/proguard-rules.pro` and parts of `AndroidManifest.xml` come from SDL3's `android-project`
template (SDL 3.4.14), unchanged, under SDL's zlib license ([`LICENSE.SDL3.txt`](LICENSE.SDL3.txt)).
They must match the SDL3 version that is built: SDL refuses to start on a Java/C version mismatch,
so copy them again from `thirdparty/sdl3/android-project` whenever the SDK's SDL3 changes.

**No game files are ever packaged.** The APK holds only the port's own code. Players bring their
own extracted copy of the game and the app copies it into its storage on the phone.

## What it does

`Gow2Activity` extends `SDLActivity`:

1. Before any native library loads it sets `HOME` and `XDG_DATA_HOME` to the app's files folder
   (`Android/data/com.santiquiroz.gow2recomp/files`; ReXGlue keeps user data in
   `$XDG_DATA_HOME/<app name>`) and `TMPDIR` to its cache folder (Android has no `/tmp`).
2. If `default.xex` is not in `Android/data/com.santiquiroz.gow2recomp/files/game`, it shows a
   setup screen instead of SDL's surface; while that surface is detached SDL never starts its
   native thread. The player picks the folder of their extracted game (Storage Access Framework)
   and the app copies it, with progress, into `files/game.incoming` and then renames that to
   `files/game`. The copy is needed because the runtime memory-maps the game files and a
   `content://` URI cannot be memory-mapped. The player can also copy the folder over USB from a
   PC and press *Check again*. File names from the picker are validated (no `..`, separators or
   NUL) and the free space is checked before copying.
3. It loads the libraries listed in `NativeLibraries.java` and passes
   `--game_data_root=<files/game>` to `SDL_main`. It also sets `REX_EXECUTABLE_FOLDER` to the files
   folder, where the runtime keeps its config and logs (an app's process image is
   `/system/bin/app_process64`).

The placeholder `libgow2shell.so` (`app/src/main/cpp`) opens an SDL3 window with Vulkan, logs the
GPU, its driver, the Vulkan features and extensions the port cares about to logcat (tag
`Gow2Shell`) and clears the screen to a solid dark red.

## Requirements

- JDK 17.
- Android SDK with platform 36, NDK `30.0.14904198` and CMake `4.1.2` (SDK manager).
- An SDL3 source tree: `thirdparty/sdl3` of a ReXGlue SDK checkout, for example
  `out/sdk-android-src/thirdparty/sdl3` once `scripts/build-android.ps1` has run. It is only read;
  the build files go to `app/.cxx`.

## Build (placeholder shell)

Create `android/local.properties` (it is not in git; paths are examples):

```properties
sdk.dir=C:/Users/you/AppData/Local/Android/Sdk
gow2.sdl3.dir=C:/path/to/rexglue-sdk/thirdparty/sdl3
```

Then, from `android/`:

```sh
./gradlew assembleDebug testDebugUnitTest      # gradlew.bat on Windows
```

The APK is `app/build/outputs/apk/debug/app-debug.apk` (arm64-v8a only, minSdk 29, targetSdk 36).
Native libraries are stored uncompressed and 16 KB aligned in the APK (`useLegacyPackaging =
false`, NDK r28+ default linker alignment), so the app also runs on 16 KB-page devices.

## Licenses in the APK

Every APK carries the license texts in `assets/licenses/`: the repository's [`LICENSE`](../LICENSE)
and [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md), and
[`LICENSE.SDL3.txt`](LICENSE.SDL3.txt). The Gradle task `copyLicenseAssets` (`app/build.gradle`)
copies them from the repository at build time for every variant, so there is one copy of each in
git; it stops the build when one of them is missing. `tools/check_android_libs.py` fails an APK
without `assets/licenses/LICENSE` and `assets/licenses/THIRD_PARTY_NOTICES.md`.

## Install and run

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.santiquiroz.gow2recomp/.Gow2Activity
adb logcat -s Gow2Shell Gow2Activity SDL
```

Debug builds also accept two launch extras for unattended runs; release builds ignore both.
`SKIP_GAME_SETUP` starts the native side without game files, which is how the placeholder is
tested, and `SHOW_WHEN_LOCKED` shows the activity over a locked screen and turns the screen on:

```sh
adb shell am start -n com.santiquiroz.gow2recomp/.Gow2Activity \
    --ez com.santiquiroz.gow2recomp.extra.SKIP_GAME_SETUP true \
    --ez com.santiquiroz.gow2recomp.extra.SHOW_WHEN_LOCKED true
```

## Verified on the Galaxy S25 Ultra

October 2026, Android 16, stock Qualcomm driver (build f61dec9117, 03/26/26): the APK installs
(`extractNativeLibs=false`, all libraries 16 KB aligned), the setup screen appears when there are no
game files and keeps the native side from starting, and the placeholder reports `Adreno (TM) 830`,
Vulkan 1.3.284, all 22 core features it checks (among them `vertexPipelineStoresAndAtomics`,
`geometryShader`, `textureCompressionBC`, `shaderStorageImageWriteWithoutFormat`), no
`VK_EXT_fragment_shader_interlock` or `VK_EXT_memory_budget`, and presents frames through SDL's
Vulkan renderer.

## Game build

`scripts/build-android.ps1` builds the APK with the game's libraries from a clean clone of this
repository. It needs, besides the requirements above, Python 3, `git`, `cmake` and `ninja` on
`PATH`, network access the first time (it clones the SDK), and your own `game\default.xex` (only
while `generated\default` does not exist yet, together with the Windows ReXGlue SDK for
`rexglue codegen`, `-Sdk`). `gow2.sdl3.dir` is not used in this mode.

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-android.ps1 -LowPriority
```

The steps, each skipped when its output is current:

0. Clones ReXGlue `923c1a5` with its submodules into `out\sdk-android-src` and applies every
   `sdk\patches\*.patch` in order (0002-0006 like the Windows SDK build, then
   `0007-android-arm64-port.patch`), with the same clone, marker and symlink repair as
   `scripts\build-sdk.ps1` (shared in `scripts\sdk-source.ps1`). `out\sdk-android-patched.txt`
   records the commit and the patch hashes; when a patch changes (or that file is deleted), the
   tree is reset and patched again. `-Clean` leaves the clone alone.
1. `rexglue codegen` when `generated\default` is missing (`-Codegen` forces it).
2. `tools/prepare_android_sources.py` copies `generated\default` to `out\android\generated\default`
   and applies the switch-trap, game and ARM memory-barrier patches to the copy; files whose
   bytes did not change keep their timestamps, so ninja recompiles only the changed ones. The
   Windows tree is never patched for ARM.
3. CMake configures [`gamelibs/`](gamelibs/CMakeLists.txt) with the NDK toolchain (arm64-v8a,
   API 29, `-march=armv8.2-a`, `-fno-emulated-tls`, `-Wl,-z,max-page-size=16384`) into
   `out\android\build`. The SDK source tree is part of that build (its binaries go to
   `out\android\build\sdk`, not to the SDK's own `out\android-arm64`).
4. Builds `libSDL3.so`, `librexruntime.so` and `librexgpu-xenos.so`, then `libgow2.so`, and stages
   them with the NDK's `libc++_shared.so` in `out\android\jniLibs\arm64-v8a`.
5. `tools/check_android_libs.py` checks the staged libraries, then Gradle builds
   `assembleDebug -Pgow2.gameLibraries=true` and the APK is checked again.

Logs are in `out\android\logs`. A first build took 10 minutes on a busy 24-thread PC, plus the SDK
clone; a rebuild after a source change takes about a minute.

### Building your own SDK tree

`-SdkSource <folder>` (or `REXGLUE_SDK_ANDROID`) builds that rexglue-sdk tree instead of
`out\sdk-android-src`, for example the worktree you change the Android port in: ReXGlue `923c1a5`
with all of `sdk\patches` applied. Before anything else the script checks the tree for files and
texts only the current patches add and stops with the list of what is missing:
`include\rex\main_android.h` and `thirdparty\ffmpeg-android\config.h` (0007; an older Android tree
changed FFmpeg's config inside the FFmpeg submodule instead), `GOW2_VFS_OVERRIDES` and
`src\input\stick_deadzone.h` (0005), and `mnk_mouse_stick` (0006). How to turn your changes back
into `0007-android-arm64-port.patch` is in [`sdk/patches/README.md`](../sdk/patches/README.md).

`libgow2.so` holds the recompiled code, the port's code from `src/` and the SDK's SDL entry point
(`windowed_app_main_sdl.cpp`, which exports `SDL_main` on Android). Every file in `src/` is built
except the Windows-only ones listed in `gamelibs/CMakeLists.txt` (`gow2_xam_input_guard.cpp`,
`gow2_utf8_guard.cpp`, `gow2_crash_watch.cpp`); `gamelibs/gow2_android_glue.cpp` stands in for
them: it suppresses the disc-error box itself, logs that the XamInput serialization and the
non-throwing UTF-8 decoder come from patch 0007, and that there is no crash watch on Android.

Gradle alone, once the libraries are staged:

```sh
./gradlew assembleDebug -Pgow2.gameLibraries=true    # gradlew.bat on Windows
```

It packages `../out/android/jniLibs` (or `gow2.gameLibs.dir`, as a `-P` property or in
`local.properties`) and stops with a clear message if one of the five libraries is missing.

### Checking the libraries

```sh
python tools/check_android_libs.py out/android/jniLibs/arm64-v8a
python tools/check_android_libs.py android/app/build/outputs/apk/debug/app-debug.apk
```

It uses the NDK's `llvm-readelf` and `llvm-nm` and fails when: an expected library is missing or is
not an AArch64 shared object; a `PT_LOAD` segment is not 16 KB aligned (and, in an APK, a library is
compressed or not 16 KB aligned in the zip); a `DT_NEEDED` entry is neither packaged nor an allowed
system library; an undefined symbol is not defined by the libraries the linker will search for it
(the library's own `DT_NEEDED` closure, with the NDK's API-29 stubs for system libraries, which is
how Android's linker resolves app libraries); a library other than `libSDL3.so` defines `SDL_*` or
`Java_org_libsdl_app_*` symbols; or `libgow2.so` does not export `SDL_main`; or an APK lacks
`assets/licenses/LICENSE` or `assets/licenses/THIRD_PARTY_NOTICES.md` (`--no-notices` skips that
check).

## Native libraries

`gow2.gameLibraries` in `gradle.properties` (or `-Pgow2.gameLibraries=true`) selects the list in
`NativeLibraries.java`:

| Value | Loaded, in order (the last one holds `SDL_main`) |
|---|---|
| `false` (default) | `libSDL3.so`, `libgow2shell.so` (placeholder) |
| `true` | `libSDL3.so`, `librexruntime.so`, `librexgpu-xenos.so`, `libgow2.so` |

There is exactly one SDL in the process: the SDK's Android build makes SDL3 a shared library
(`SDL_SHARED`), `librexruntime.so` and `librexgpu-xenos.so` link that `libSDL3.so`, and SDLActivity
loads it first, so its `JNI_OnLoad` sets up the JNI state the runtime's SDL calls use. A static SDL
inside the runtime would be a second copy without that state.

## Not done yet

- Gameplay: on the Galaxy S25 Ultra the game reaches the PRESS START title screen in landscape
  (intro video and audio work); nothing past it is verified yet.
- Controller and touch mapping, and a separate `:game` process.
