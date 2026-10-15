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
| `0007-android-arm64-port.patch` | The Android arm64 port of the runtime (NDK r29+, API 29). Build: an `android-arm64` platform, no `-march=armv8-a` that would lower the game's `armv8.2-a`, SDL3 as the shared `libSDL3.so` SDLActivity loads (no X11/Wayland/ALSA), `SDL_main` from `windowed_app_main_sdl.cpp`, Vulkan from the system `libvulkan`, the SDK version read from the SDK's own git history under `add_subdirectory`. Core: `GetAndroidApiLevel()` (`main_android`), guest memory through `ASharedMemory` with a `memfd_create` fallback (no `/dev/ashmem`), Bionic fixes (64-bit `off_t`, no robust mutexes, `clock_cast`), an AArch64 fiber switch (Bionic has no `ucontext`), unclaimed `SIGSEGV`/`SIGBUS`/`SIGILL` chained to the previous handler instead of looping, `REX_EXECUTABLE_FOLDER` for config and logs, `content://` stubs. UI: an `ANativeWindow` surface from the SDL window. The GPU plugin is loaded by name from the APK. Input: one recursive lock across the `InputSystem` entry points and a non-throwing `utf8::next` (the Windows build gets both from the port's own guards). `XnAddrStatus` constants get definitions. FFmpeg runs its C code: `thirdparty/ffmpeg-android/config.h` comes before FFmpeg's own `config.h` and turns the aarch64 assembly off, so the FFmpeg submodule is not modified. Every change is behind `ANDROID` / `REX_PLATFORM_ANDROID` / `__ANDROID__`, so Windows behaves as before. |
| `0010-system-link-sessions.patch` | System Link milestone M2a (`docs/COOP_N_PLAYERS.md`, Stage 1, item 2). Sockets: `sendto`/`recvfrom` addresses in network order (port 1000 no longer leaves as 59395), `from` written only on success, guest WSA error codes from `sendto`, `setsockopt` and `bind`, `SIO_UDP_CONNRESET` off on Windows UDP sockets. XNet: the link reports active (`net_link_active`), `XNetGetTitleXnAddr` gives `net_local_ip` and a locally administered MAC per instance, `XNetXnAddrToInAddr` returns the peer's own address, `XNetRegisterKey`/`UnregisterKey`/`UnregisterInAddr` succeed, `XNetRandom` is random (`net_random_seed` makes it a fixed stream). Sessions (`include/rex/kernel/xam/xsession.h`): one handle and object per session, a host's `XSessionCreate` fills `XSESSION_INFO` (system-link XNKID, XNADDR, XNKEY) and the nonce, joins and leaves keep members, `XSessionGetDetails` is filled. Identity: the game sees `user_xuid` (default: random per install, kept in `system_link_xuid.txt` in the user data folder) and `user_name`, while saves stay under the old fixed XUID and name. One PC, several instances: `net_bind_ip` and `net_broadcast_targets`. Unit tests in `tests/unit/system/system_link_test.cpp` and `tests/unit/kernel/xsession_test.cpp`. |
| `0011-network-crossplay.patch` | System Link milestone M2b (`docs/COOP_N_PLAYERS.md`, Stage 4 networking). Port remap: guest ports below 1024 use port + `net_port_offset` on the host (default 40000, so the game's UDP 1000 becomes 41000; beacon port 14001 is unchanged), applied symmetrically in bind, connect, sendto, recvfrom, accept, getsockname and getpeername; `getsockname` and `getpeername` are real exports now (they were stubs returning WSAEOPNOTSUPP). POSIX sockets (Android, Linux): Winsock option numbers map to the host's through one table that `setsockopt` and `getsockopt` share; SO_LINGER reads as two big-endian 16-bit fields (this also fixes Windows, where the old 32-bit swap exchanged on/off and the timeout); `ioctlsocket` FIONBIO sets O_NONBLOCK and FIONREAD returns the count big-endian (Windows returned it little-endian before); `select` computes nfds from the host descriptors (the game passes 0); errno maps to WSA codes, and every socket call keeps its own error in a per-thread slot right after the host call fails. Internet play: `net_peers` (a comma-separated host[:port] list, IPv4 or DNS names) gets a unicast copy of every broadcast; `net_public_address` is advertised as XNADDR inaOnline/wPortOnline; `net_allow_peers` filters inbound datagrams (loopback always passes); `XNetXnAddrToInAddr` returns ina for local peers and inaOnline for the rest. Every broadcast also goes as a unicast copy to each address that sent a datagram to that socket in the last 10 seconds, with a 250 ms duplicate filter; on Android and Linux 255.255.255.255 goes out as the subnet's directed broadcast, while Windows keeps it. `net_local_ip` accepts a.b.c.d or a.b.c.d/prefix (prefix 24 by default); empty means `REX_DETECTED_LOCAL_IP`, then `net_bind_ip`, then the default-route address, then 127.0.0.1. `[netlink]` logging covers every remap, drop, fan-out and DNS pass. Unit tests in `tests/unit/system/system_link_test.cpp`. |

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

Check that the series still reproduces the worktree: applying 0002-0007 with `git apply --cached`
to a temporary index read from `923c1a5` must give the same `git write-tree` as `$port`.

`scripts/build-sdk-android.ps1` builds the SDK alone for Android (bring-up); see `docs/ANDROID.md`.
