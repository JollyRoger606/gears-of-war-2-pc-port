# PC Options: the in-game settings menu

PC Options is the port's own settings menu. It lives inside the game. It
changes the same settings as `gow2.toml` and the command line.

On Android, the menu and its entry on the game's Options screen are called
"Device Options" ("Opciones del dispositivo" in Spanish). Windows keeps
"PC Options" ("Opciones de PC" in Spanish). Everything else is the same menu.

Verified in game on a PC on 2026-10-16. Android: see the Touch tab below.

## Opening it

- The game's Options screen (main menu, RB) lists "PC Options" on Windows
  or "Device Options" on Android where
  "Storage Location" was. Selecting it opens the menu.
- F10 opens and closes it from anywhere.
- Back + Start, held together on a controller, opens and closes it.
- On Android, the gear in the top-right corner opens and closes it.

Turn the Options entry off with `gow2_pc_options_entry = false`. It takes
effect on the next start.

## Controls

Controller:

- D-pad or left stick: move between rows and change values.
- A: change. B or Esc: back. X: default. Y: save.
- LB / RB: switch tabs.

Keyboard:

- Arrows: move between rows and change values.
- Enter or Space: change. Esc or Backspace: back. Del: default.
- Q / E or PgUp / PgDn: switch tabs. Ctrl+S: save.

Mouse and touch:

- Click or tap tabs, rows, and the arrows beside a value.
- Three buttons at the bottom: Save, Default, Back (Close when clean).

Text rows open an editor. Controllers get an on-screen keyboard there.

## Saving

Only the rows you changed are written into `gow2.toml` next to `gow2.exe`.
On Android the file is
`Android/data/com.santiquiroz.gow2recomp/files/gow2.toml`. Every comment in
the file is kept. Windows paths are written as literal strings.

A build from source copies `config/gow2.toml` over the build folder's file.
The release zip is not affected: your saved settings stay.

Leaving with unsaved changes asks Save / Discard / Cancel. Discard restores
the values the menu opened with.

## Applies now, next level, on restart

Changes apply at once where the setting allows it. Rows that need a restart
say so under the label. A banner lists the changed rows that still need a
restart, with a "Restart now" button on Windows.

The menu language follows the system (English or Spanish), or
`gow2_menu_language`. Advanced rows appear with "Show advanced options" on.
Defaults keep the original game and crossplay working.

## Display

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Window mode | `fullscreen` + `fullscreen_exclusive` | windowed, borderless, exclusive | now | Windows |
| Display | `monitor` | detected monitors | now | Windows |
| Window size | `window_width` + `window_height` | game resolution, 1280x720 ... 3840x2160 | now | Windows |
| Graphics card | `gow2_gpu` | automatic, detected GPUs | restart | Windows |
| VSync | `vsync` | on, off | now | both |
| Unlock frame rate | `gow2_unlock_fps` | on, off | now | both |
| Frame rate limit | `gow2_fps_cap` | off, 30 ... 240 | now | both |
| Display refresh hint | `present_android_frame_rate` | phone default, 30 ... 120 | restart | Android |

## Graphics

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Internal resolution | `resolution_scale` | 720p, 1440p, 4K | restart | both |
| Upscaler | `gow2_temporal` + `gow2_upscaler` | off, FSR 3.1, FSR 4 (DLSS on DLSS builds) | now | Windows |
| Upscaling | info | internal -> screen ratio | now | Windows |
| Final image filter | `present_effect` | bilinear, FSR 1, CAS (SGSR on Android) | restart | both |
| FSR 1 softening | `present_fsr_sharpness_reduction` | 0 ... 2 | restart | both |
| CAS sharpness | `present_cas_additional_sharpness` | 0 ... 1 | restart | both |
| Dithering | `present_dither` | on, off | restart | both |
| Anisotropic filtering | `anisotropic_override` | game default, off, 1x ... 16x | next level | both |
| Texture cache | `texture_cache_memory_limit_soft` + `texture_cache_memory_limit_hard` | 384 MB, 1 GB, 2 GB, 4 GB | now | both |
| Character lighting fix | `gow2_black_shading_fix` | on, off | restart | both |
| Bigger texture pool | `gow2_texture_streaming_fix` | on, off | restart | both |
| 16x filtering patch | `gow2_aniso16` | on, off | restart | both |
| Ambient occlusion fix | `gow2_ao_upscaling_fix` | on, off | now | both |
| Depth of field fix | `gow2_dof_upscaling_fix` | on, off | now | both |
| Texture packs | `texture_replace` | on, off | restart | Windows |
| Dump textures | `texture_dump` | on, off (advanced) | restart | Windows |
| Sustained performance | `gow2_android_sustained_performance` | on, off | restart | Android |
| Engine settings | `gow2_system_settings` | text (advanced) | restart | Android |

## Controls

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Keyboard and mouse | `mnk_mode` | on, off | now | both |
| Native mouse aim | `gow2_mouse_aim` | on, off | restart | both |
| Mouse sensitivity | `gow2_mouse_scale` or `mnk_sensitivity` | 0.05x ... 10x | now | both |
| Invert mouse Y | `gow2_mouse_invert_y` | on, off | now | both |
| Aim assist with the mouse | `gow2_mouse_aim_assist` | on, off | now | both |
| Button prompts | `gow2_prompts` | automatic, keyboard, controller | restart | both |
| Controller look dead zone | `pad_right_stick_deadzone` | 0 ... 0.9 | now | both |
| Key bindings | opens the page below | | now | both |
| Controller layout | info | in the game's own Options | now | both |

The Key bindings page holds one row per action (`keybind_*`, 24 rows).
Accept, then press the new key. Default restores the original keys. Menu
prompts update after a restart.

## Touch (Android)

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Touch controls | `gow2_touch` | automatic, on, off | restart | Android |
| Opacity | `gow2_touch_opacity` | 0% ... 100% | now | Android |
| Size | `gow2_touch_scale` | 0.5x ... 2x | now | Android |
| Look sensitivity | `gow2_touch_look_gain` | 0.1x ... 5x | now | Android |
| Aim: tap to toggle | `gow2_touch_aim_toggle` | on, off | now | Android |
| Hide after keyboard or mouse | `gow2_touch_hide_seconds` | 0 ... 600 s | now | Android |

## Multiplayer

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Your name | `user_name` | text, up to 15 characters | restart | both |
| System Link (LAN and internet) | `net_link_active` | on, off | restart | both |
| Co-op players | `gow2_coop_players` | 2, 3, 4 | restart | both |
| Co-op check code | info | the combined hash | now | both |
| Join a host (name or IP) | `net_peers` | text | restart | both |
| Host over the internet | opens the page below | | now | Windows |
| This device's LAN address | `net_local_ip` | text (advanced) | restart | both |
| Port offset | `net_port_offset` | 1024 ... 64512 | restart | both |
| Co-op check | `net_beacon_gate` | on, off (advanced) | restart | both |
| Network log | `system_link_trace` | on, off (advanced) | now | both |

The "Host over the internet" page holds: Status (info), Keep open for
(`gow2_host_lease_hours`: 2, 6, 12, 24 h), Open ports now (action), Close
ports (action), Address friends use (`net_public_address`: text), Friends
allowed (`net_allow_peers`: text), Close ports when the game exits
(`gow2_host_close_on_exit`: toggle). All Windows. See below.

## Saves

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Save location | `gow2_save_dir` | default, portable, detected cloud folders | restart | both |
| Choose a folder | picks `gow2_save_dir` | folder picker | now | Windows |
| Folder path | `gow2_save_dir` | text (advanced) | restart | both |
| Saves are in | info | the folder this session uses | now | both |
| Open the saves folder | action | opens File Explorer | now | Windows |

## Audio

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Volume | `gow2_audio_volume` | 0% ... 100% | now | both |
| Mute | `audio_mute` | on, off | now | both |
| Audio buffer | `audio_maxqframes` | 4 ... 64 (advanced) | restart | both |

## System

| Row | Key(s) | Values | Applies | Platforms |
|---|---|---|---|---|
| Menu language | `gow2_menu_language` | automatic, English, Spanish | now | both |
| Show advanced options | `gow2_menu_advanced` | on, off | now | both |
| All runtime settings | action | opens the F4 list | now | both |
| Restart the game | action | save first: it saves nothing | now | Windows |
| Quit the game | action | quit, then open the game again | now | Android |
| Settings file | info | where the menu saves | now | both |
| Version | info | this build of the port | now | both |
| Open this menu | info | F10, Back + Start | now | both |

## New settings this release

`gow2_fps_cap`, `gow2_audio_volume`, `gow2_menu_language`,
`gow2_menu_advanced`, `gow2_host_lease_hours`, `gow2_host_close_on_exit`,
`gow2_pc_options_entry`, `bind_pc_options` (F10).

## Host over the internet

On Windows this page runs the bundled `scripts\host-online.ps1` hidden with
`-Json`. It opens UDP 14001 and 41000 by UPnP on every router on the way,
with a lease. It fills `net_public_address` when empty. It closes the ports
when the game exits if "Close ports when the game exits" is on. The
scheduled `-Stop` still covers a crash. See [ONLINE.md](ONLINE.md).

## Co-op check code

With 3 or 4 players every player must see the same code: the same build and
the same co-op settings. With 2 players it is not used. See
[NETWORK_PLAY.md](NETWORK_PLAY.md).

## All runtime settings

The raw runtime list stays on F4 ("All runtime settings"). It is also a row
in PC Options > System.

## How it works inside

SDK patch 0013 adds the hooks. `xam_input_block` gives the game a neutral
pad and no keystrokes while a host menu is open. The Coalesced layer renames
the game's Options > Storage Location button to PC Options. The device
selector call with the UI flag (0x200) runs the command cvar
`gow2_pc_options_modal`, which opens this menu and waits; the boot-time call
(flags 0) stays headless. The game is told through the system-UI
notification (XN_SYS_UI) while the menu is open.
