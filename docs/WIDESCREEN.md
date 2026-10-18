# Ultrawide (Hor+) rendering

## TL;DR

It is on by itself (`gow2_widescreen = "auto"`) when the main display is wider than 16:9. Set `"off"`, or an aspect such as `"21:9"` or `"2.39"`, in `gow2.toml` and restart to change that. The
game then draws a really wider picture: you see more to the left and right, nothing is stretched, and the
black bars of a 16:9 picture on a 21:9 monitor are gone. On a 16:9 or narrower display nothing changes.

```toml
gow2_widescreen = "auto"     # auto (default) | off | 21:9 | 16:10 | 2.39 ...
```

| | |
|---|---|
| What you get | A back buffer up to 1680x704 (21:9). The vertical view stays the one of the 16:9 game; the horizontal view grows. |
| Cost | Frame time unchanged (60 fps, p99 19.3-19.5 ms in two test scenes). GPU time per frame about 13-15 % higher. |
| Cap | 1680 pixels wide. A 32:9 screen gets 21:9 with side bars. |
| Side effect | The shadow map is made smaller (800 -> 480 px) so it fits the console's 10 MB EDRAM next to the wider picture. |
| Not covered | Videos (stay 16:9), HUD and menu placement were only partly checked (see "What does not work"). |

Screenshots (3440x1440 monitor, window 21:9): [hospital, chapter 2](screenshots/ultrawide-hospital-21x9.jpg),
[a cutscene, chapter 7](screenshots/ultrawide-cutscene-21x9.jpg).

## How it works

The game was built for 16:9 at 1280x720 (704 lines with the AO upscaling fix). Three things fix it there:

1. **Back buffer size.** `sub_82806A80` computes the width as `720 x aspect` and clamps it to 1280 (the `cr6.lt`
   branch at `0x82806E64`). Patch id 7 turns the aspect into `(W + 0.25) / 720` for the wanted width W and lifts
   the clamp. W is a multiple of 80, because an EDRAM tile is 80 pixels wide at 32 bits per pixel.
2. **Field of view.** The camera code multiplies a field of view by a constant; the per-frame main scene one is
   at `0x8252D750` (site 4). The patch widens the half angle as `atan(tan(half) * W / 1280)`, which keeps the
   vertical angle of the 16:9 game (Hor+). Other sites (cutscene cameras at `0x82634F44`, `0x82634CC8`) use the
   same function. Sites 3, 5, 6, 7 are patched too but never fired in the scenes tested; they are harmless.
3. **Presenter.** `PrepareWidescreen` (called from `OnPreSetup`) sets `video_mode_width/height` to the new
   buffer size unless you set them yourself, so the window shows the frame at the right aspect.

### The EDRAM limit

The Xbox 360 draws into 10 MB of EDRAM and the recompilation emulates that. Scene colour plus depth use
`8 x W x H` bytes: 9.5 MB at 1680x704. The game's 800x800 shadow map (32-bit depth, 2.4 MB) then overlapped
the scene and left dark rectangles on characters. `gow2_system_settings` edits in the Coalesced bundle
(`ResX`, `ResY`, `MaxShadowResolution`) now size the shadow map to what is left: 480 px at 1680x704. Wider than
1680 does not fit, which is why 32:9 is held at 21:9.

## Settings

| Setting | Meaning |
|---|---|
| `"auto"` (default) | The aspect of the main display (Windows only) when it is wider than 16:9; otherwise the game's own 16:9. |
| `"off"` | The game's own 16:9. |
| `"21:9"`, `"16:10"`, `"2.39"` | A fixed aspect, accepted between 1.2 and 4. |

Widths are rounded to a multiple of 80 and held between 960 and 1680. Needs a restart. `video_mode_width` and
`video_mode_height` set by you win. The dev command `!viewinfo` logs the screen size the game uses, the
viewport sizes and which FOV sites ran.

## Performance (hospital_walk and outdoor_assault, 2 runs each, 21:9 vs the 16:9 ROV baselines)

| Scene | GPU p50 16:9 | GPU p50 21:9 | Frame p99 |
|---|---|---|---|
| hospital_walk | 6.45 ms | 7.40 ms (+15 %) | 19.3 ms (unchanged) |
| outdoor_assault | 10.75 ms | 12.15 ms (+13 %) | 19.5 ms (unchanged) |

The pixel count grows by 31 %, the GPU time by about 14 %, and the game stays at 60 fps. The perf tool flags
`gpu_ms.p50` as WORSE (its threshold is 10 %) and 0.5-1.0 hitches per minute in outdoor_assault; the frame pacing
itself did not change.

## What does not work, or was not checked

- **Videos** (Games for Windows logos, pre-rendered cutscenes) are 16:9 and are shown as they are.
- **HUD and menus.** The title screen looked right. The in-game HUD (ammo, health) did not show in my
  captures, and the pause menu was not captured at 21:9, so their placement is **not verified**. I have not looked at where they land.
- **Wider than 1680.** Needs more than the 10 MB of EDRAM; not possible without tiling the scene.
- **Shadows** are lower resolution (480 px instead of 800 px) while widescreen is on.
- **DLSS / upscaler** combinations were not tested with widescreen on. AMD and Intel GPUs untested.
- `auto` was checked on this 3440x1440 monitor (same picture as `21:9`).
- **Other aspects** (16:10, 4:3) are covered by the arithmetic test only, not by screenshots.

## Tests

`tools/tests/widescreen_math_test.cpp` (header `src/gow2_widescreen_math.h`) checks the aspect parser, the
width rounding and caps, the shadow size and the field-of-view arithmetic. It runs with `scripts\test-host.ps1`.
