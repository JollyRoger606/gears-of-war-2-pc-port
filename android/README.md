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

The scripts look for the NDK pinned in `app/build.gradle` (`ndkVersion`) under `ANDROID_HOME`,
`ANDROID_SDK_ROOT`, `sdk.dir` of `local.properties` or `%LOCALAPPDATA%\Android\Sdk`, then
`ANDROID_NDK_HOME`, then the newest installed NDK; a pre-release NDK whose base revision is the
pinned one (for example `30.0.14904198-beta1`) counts as the pinned one.

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
`THIRD_PARTY_NOTICES.md` has an "Android APK" section with what each of the five libraries
contains, every component's license, and how to get the FFmpeg and libmspack sources (LGPL 2.1,
linked statically into `librexruntime.so`) and rebuild and relink them.

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

With the game's libraries the phone plays Act 1 with touch only: the touch Start button passes the
PRESS START screen, the D-pad, A, B and Y drive the menus, Solo Campaign > New Campaign plays the
intro movie and the opening cutscene of "Tip of the Spear", and in gameplay the floating stick moves
Marcus, a drag on the right side turns the camera, LB opens Tac/Com and RT fires the Lancer (the
ammo count drops). Checkpoints saved there load again from the save slot screen.

Frame rate in that first gameplay area: 2.6 to 5.2 game frames per second (`gow2_frame_log`), with
the GPU held at 160 to 222 MHz of its 1.2 GHz by the phone's thermal limit (thermal status 2 to 3,
skin 42 to 45 C, after hours of testing while charging; `thermal_pwrlevel` 13 to 14 in
`/sys/class/kgsl/kgsl-3d0`). The GPU is 99% busy, so these numbers say little about a cool phone:
after a few minutes' rest (thermal status 2, GPU allowed up to 734 MHz) the same place ran at 13.6
to 16.8 fps for about ten seconds, then fell back to 3 to 6 fps as the cap returned. The menus run
at 30 to 50 fps.
Large parts of the scene render black.

## Game build

`scripts/build-android.ps1` builds the APK with the game's libraries from a clean clone of this
repository. It needs, besides the requirements above, Python 3, `git`, `cmake` and `ninja` on
`PATH`, network access the first time (it clones the SDK), and your own `game\default.xex` (only
while `generated\default` does not exist yet, together with the Windows ReXGlue SDK for
`rexglue codegen`, `-Sdk`). The Windows ReXGlue SDK used for codegen must be the one the Windows
build uses, the Windows x64 build of version 0.10.0.9-dev (commit `923c1a5`, GitHub release tag
`nightly-20260921-923c1a59`, see [`BUILDING.md`](../BUILDING.md)): code generated by another SDK
version does not match the runtime built from `923c1a5`. `gow2.sdl3.dir` is not used in this mode.

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
1. `rexglue codegen` when `generated\default` is missing, when `gow2_manifest.toml` or
   `game\default.xex` is newer than `generated\default\sources.cmake` (only with `-Sdk` and the
   xex; without them the script warns and builds the existing code), or with `-Codegen`.
2. `tools/prepare_android_sources.py` copies `generated\default` to `out\android\generated\default`
   and applies the switch-trap, game and ARM memory-barrier patches to the copy; files whose
   bytes did not change keep their timestamps, so ninja recompiles only the changed ones. The
   Windows tree is never patched for ARM.
3. CMake configures [`gamelibs/`](gamelibs/CMakeLists.txt) with the NDK toolchain (arm64-v8a,
   API = `minSdk` of `app/build.gradle` (29; `-Api` must agree with it),
   `-march=armv8.2-a`, `-fno-emulated-tls`, `-Wl,-z,max-page-size=16384`) into
   `out\android\build`. The SDK source tree is part of that build (its binaries go to
   `out\android\build\sdk`, not to the SDK's own `out\android-arm64`). The script prints the
   SDK version the configure step derived from the SDK tree's git history (`0.10.0.9-dev.g923c1a5`
   for the clean clone) and warns when it names a commit other than that tree's `HEAD`.
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
(`windowed_app_main_sdl.cpp`, which exports `SDL_main` on Android). The version script
`gamelibs/libgow2.map` keeps the recompiled code's ~98,000 symbols local, so the library exports
only `SDL_main` (which `SDLActivity` finds with `dlsym`). That shrank `libgow2.so` in the APK from
62.2 MB to 52.0 MB (APK 79.2 MB to 71.6 MB) and its load time from 10-15 ms to 2-7 ms. Every file
in `src/` is built except the Windows-only ones listed in `gamelibs/CMakeLists.txt`
(`gow2_xam_input_guard.cpp`, `gow2_utf8_guard.cpp`, `gow2_crash_watch.cpp`);
`gamelibs/gow2_android_glue.cpp` stands in for them: it suppresses the disc-error box itself, logs
that the XamInput serialization and the non-throwing UTF-8 decoder (an invalid byte decodes as a
one-byte code point instead of throwing) come from patch 0007, and that there is no crash watch on
Android.

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
`Java_org_libsdl_app_*` symbols; or `libgow2.so` does not export `SDL_main`; or `libgow2.so` exports
anything besides `SDL_main` (`--main-exports` lists the allowed names); or an APK lacks
`assets/licenses/LICENSE` or `assets/licenses/THIRD_PARTY_NOTICES.md` (`--no-notices` skips that
check).

### Self-test on the phone

`scripts/test-android-device.ps1` (`-Serial`, `-SkipBuild`, `-LowPriority`) builds
`gow2_runtime_selftest` (`gamelibs/tests/runtime_selftest.cpp`, not part of the APK) in
`out\android\build` (configured by `scripts\build-android.ps1` first), pushes it with `libSDL3.so`,
`librexruntime.so` and `libc++_shared.so` to `/data/local/tmp/gow2-selftest` over adb, runs it there
and exits with its exit code; the app and its data are untouched. It checks patch 0007 through the
runtime's public API: the UTF-8 helpers (`utf8_count`, `to_utf16`, `utf8_hash_fnv1a`,
`utf8_lower_ascii`, `utf8_equal_case`, `utf8_starts_with_case`) never throw on invalid, truncated,
overlong, surrogate and out-of-range sequences and decode each invalid byte as a one-byte code point
(`0xFF` becomes U+00FF, as in `src/gow2_utf8_guard.cpp`, not U+FFFD, so names hash alike on both
platforms). 8 threads calling `InputSystem`'s `GetState`, `GetCapabilities`, `SetState` and
`GetKeystroke` over a counting driver never have more than one thread inside the driver, while the
same driver called without `InputSystem` does see overlapping threads (so the check can detect the
race). The guest `XamInputGetState`, `XamInputGetCapabilities(Ex)`, `XamInputSetState` and
`XamInputGetKeystroke(Ex)` call exactly those four methods. On the S25 Ultra it prints
`runtime_selftest: 18 checks, 0 failed`. The script then runs `gow2_touch_model_test`
(`gamelibs/tests/touch_model_test.cpp`, pure C++): the touch layout, finger ownership, stick, look
window, tap latch and aim toggle (`touch_model_test: 50 checks, 0 failed`).

```powershell
powershell -ExecutionPolicy Bypass -File scripts\test-android-device.ps1 -LowPriority
```

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

## Controls

### Touch

`gow2_touch=auto` (the default on Android) turns the on-screen controls on when the phone has a
touchscreen (`Configuration.touchscreen`). Fingers drive a virtual Xbox 360 pad
(`src/gow2_touch_controls.cpp`, layout and pointer logic in `src/gow2_touch_model.cpp`) that the
SDK merges with real pads for the first player, so a controller and the touch controls work at the
same time.

| Where (landscape) | Control |
|---|---|
| Left 40% of the screen, below the top 15% | Floating left stick: it is centred where the finger lands, full tilt at 10% of the screen height |
| Rest of the right side | Look: the right stick follows the finger's travel over the last 200 ms, full tilt at one screen height per second (`gow2_touch_look_gain` scales it), with a square root and a 0.3 minimum so slow drags get past the game's look dead zone. The game's own aim assist stays on |
| Right side, large | RT (fire); LT (aim, held, or toggled with `gow2_touch_aim_toggle`) |
| Bottom right | A (cover, run), B (melee), X (reload, use) |
| Right side | Y, RB (active reload) |
| Top left | LB (Tac/Com) |
| Left, above the stick | D-pad (weapons, menus) |
| Top centre | Back, Start |

Each finger owns the control it touched first until it lifts; a tap shorter than a game frame stays
pressed for 100 ms; losing focus releases everything. The controls are a translucent ImGui overlay
(`gow2_touch_opacity`, `gow2_touch_scale`) drawn with each game frame, not at the display's refresh
rate (patch 0007's `ImGuiDialog::WantsContinuousPaint`). A controller button, a key or a mouse hides
them for `gow2_touch_hide_seconds` (10); the next touch shows them again.

Patch 0007 makes the SDK's window deliver Android finger events to its listeners and drop the mouse
events SDL makes up from fingers and pens. On Windows the SDK does not deliver finger events, the
touch files are not built and `gow2_touch` defaults to `off`.

### Controllers, keyboard and mouse

Controllers (USB or Bluetooth) go through the SDK's SDL driver as before. A physical keyboard or an
external mouse attached when the game starts turns on the keyboard/mouse driver (`mnk_mode`, with
the same key binds as on Windows); a mouse also sets `mnk_mouse`, so the native mouse look of
`src/gow2_mouse_aim.cpp` installs. The runtime log records the choice in an `INPUTDEV` line. The
devices are found through JNI (`src/gow2_android_devices.cpp`): the keyboard from
`Configuration.keyboard`, the mouse from `InputDevice` entries that are external, not virtual and
have `SOURCE_MOUSE` (the S25 Ultra's built-in `sec_touchpad` reports `SOURCE_MOUSE` too). A keyboard
or mouse plugged in after the start is not picked up. Only the keyboard path was exercised on the
phone, with `adb shell input keyevent` and `mnk_mode=true` in `gow2.toml`; no real keyboard or mouse
was at hand.

### Network

Solo campaign opens a UDP socket for its session layer, so the app asks for `INTERNET` (without it
the campaign stops at "Error initializing network layer"). Apps may not bind ports below 1024: patch
0007 binds the guest's port 1000 as 31000 (a `XSocket: guest port ...` log line) and translates the
socket options and `FIONBIO`/`FIONREAD` the game passes with Winsock numbers.

### Frame rate log

`gow2_frame_log=true` (in `gow2.toml` beside the logs) logs the game's own frame rate every five
seconds as `FRAMES` lines, counted at the player's input step. Presented frames do not measure it on
a phone: Android lowers the display's refresh rate, and runtime overlays repaint without new game
frames.

## Menu prompts

With `gow2_prompts=auto` the menus show the controller glyphs unless a physical keyboard is attached
and `mnk_mode` is on, in which case they show the keyboard labels. The keyboard is detected through
SDL's JNI bridge (`Configuration.keyboard` of the activity), because SDL 3.4.14 keeps no keyboard
list on Android. The runtime log records the choice in a `PROMPTS` line. On Windows the rule is
unchanged: keyboard labels unless an XInput controller is connected at launch.

## Not done yet

- Speed: 3 to 5 game frames per second on a thermally capped GPU (see above); a measurement on a cool
  phone, and the Xenos path's cost on the Adreno 830, are next.
- Rendering: large parts of the scene are black in gameplay.
- A checkpoint written by an older build (before the network fixes) crashes on load: the main
  thread's guest stack overflows in `sub_8255E4A8`, which calls itself through an object listed as
  its own child (`Unhandled guest access violation: write of guest 0x7014FFD0`). New campaigns and
  their checkpoints load.
- Keyboard and mouse hot-plug, a test with a real keyboard and mouse, and a separate `:game`
  process.
