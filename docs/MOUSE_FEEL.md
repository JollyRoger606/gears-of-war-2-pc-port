# Mouse feel

The native mouse look (`gow2_mouse_aim`) normally passes every mouse movement straight to the camera, which feels like a PC shooter. The original game's weight comes from a
stick: small deflections turn the camera slowly, the turn speeds up and slows down instead of starting and stopping, and the camera never whips round instantly.
`gow2_mouse_feel` (PC Options > Controls > Mouse feel, applied at once) shapes the mouse movement to give some of that back.

| Preset | Lag (time constant) | Curve exponent | Vertical share | What it feels like |
|---|---|---|---|---|
| `off` | none | 1.0 | 100 % | raw, as before |
| `light` | 20 ms | 1.0 | 100 % | the view trails the hand a little |
| `original` | 45 ms | 1.4 | 90 % | stick-like: slow movements are fine and smooth, fast ones catch up at once |
| `heavy` | 80 ms | 1.6 | 80 % | like a slow stick turn; you lead the movement |
| `custom` | `gow2_mouse_smoothing_ms` | `gow2_mouse_curve` | `gow2_mouse_y_scale` | your own values (see below) |

## What each part does

1. **Curve** (`gow2_mouse_curve`, `gow2_mouse_curve_ref`): the gain is `(speed / curve_ref) ^ (curve - 1)`, with speed in mouse counts per second. Above 1, slow movements are damped (finer aim) and fast ones amplified;
   at `curve_ref` the gain is exactly 1; the gain is kept between 0.15 and 4. `curve_ref` depends on your mouse: raise it if the damping seems to reach too far into normal movements.
2. **Vertical share** (`gow2_mouse_y_scale`): vertical movement as a share of horizontal; the console's stick turns slower up and down.
3. **Lag** (`gow2_mouse_smoothing_ms`): the counts go into a reservoir that empties exponentially, so the camera follows the hand slightly behind it. The time constant is this value at rest and shrinks as the hand
   speeds up (halved at about 600 counts per second), so slow aim is smooth like a stick's small deflections while a fast movement is not left behind, and nothing overshoots: when the hand stops, what is left is released
   at once. Nothing is lost. It follows real time, so it feels the same at 60 and 144 fps.
4. **Speed cap** (`gow2_mouse_max_speed`, counts per second, 0 = off): the camera cannot turn faster than this, so a flick is delivered over a moment. The unsent backlog is limited to a quarter of a second of this speed;
   a longer flick is cut, not stretched.

`gow2_mouse_scale` (Mouse sensitivity) is applied after all of this. Menus, the pause screen and the PC Options overlay clear the reservoir, so nothing drifts into them.

## Tuning

Start with `original`. If it feels laggy, switch to `light`; if the aim feels mushy rather than heavy, use `custom` with a lower `gow2_mouse_smoothing_ms` (40 to 60) and keep the curve. `gow2_mouse_aim_log = true` logs the counts
sent once a second. The numbers in the presets are starting points, not measurements of the original game.
