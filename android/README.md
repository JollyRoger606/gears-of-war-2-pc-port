# Android shell

The Android application shell of the port (step 3 of [docs/ANDROID.md](../docs/ANDROID.md)): an
SDL3 activity, the game-folder setup and, for now, a placeholder native library in place of the
game, so the app can be built and run on a phone before the SDK and the recompiled game are
packaged for Android.

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
   `--game_data_root=<files/game>` to `SDL_main`.

The placeholder `libgow2shell.so` (`app/src/main/cpp`) opens an SDL3 window with Vulkan, logs the
GPU, its driver, the Vulkan features and extensions the port cares about to logcat (tag
`Gow2Shell`) and clears the screen to a solid dark red.

## Requirements

- JDK 17.
- Android SDK with platform 36, NDK `30.0.14904198` and CMake `4.1.2` (SDK manager).
- An SDL3 source tree: `thirdparty/sdl3` of a ReXGlue SDK checkout. It is only read; the build
  files go to `app/.cxx`.

## Build

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

## Native libraries

`gow2.gameLibraries` in `gradle.properties` selects the list in `NativeLibraries.java`:

| Value | Loaded, in order (the last one holds `SDL_main`) |
|---|---|
| `false` (today) | `libSDL3.so`, `libgow2shell.so` (placeholder) |
| `true` | `librexruntime.so`, `librexgpu-xenos.so`, `libgow2.so` |

The game list has no `libSDL3.so` on purpose: the SDK's Android build links SDL3 statically into
`librexruntime.so` (it forces `SDL_STATIC`), and that library exports SDL's JNI entry points. A
second, shared SDL would leave one of the two copies without its JNI state. If the SDK switches to
`SDL_SHARED`, put `SDL3` back at the front of that list. Packaging the game libraries (as
`jniLibs` or as CMake targets) is part of the next step.

## Not done yet

- Controller and touch mapping, and a separate `:game` process.
- Packaging the SDK and game libraries (`gow2.gameLibraries=true`).
