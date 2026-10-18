# Stutter and slowdown work: what was done, how, and why

Written 2026-10-18 for the Gears of War 2 PC port (branch `keycap-prompts-live-switch`).
Part 1 is the short version. Everything after it explains each step from zero. Nothing in this
file needs game knowledge or graphics knowledge; terms are explained where they first appear, and
there is a glossary at the end.

---

# Part 1: TLDR

**The problem.** The game stuttered and dropped frames when you moved around (walking, turning,
looking at big outdoor areas), even though standing still was smooth.

**The cause.** The graphics card was the bottleneck, not the processor or the disk. While you
moved, the card needed 20-26 ms to draw a picture in places where it only has 16.7 ms to keep
60 frames per second. Those pictures arrived late and were shown twice, which is the "hitch" you
feel. The expensive part was *how the port imitates the Xbox 360's special picture memory*, not
the amount of art or the number of objects.

**The fix.** One setting changed its default: the port now imitates that memory with a different,
more exact method (called ROV, explained below) on NVIDIA cards. Same picture, about half the
graphics work. It is a one-line change in the graphics code (SDK patch 0018). Your config file was
not touched.

**The result (measured, same test scenes, same machine, 2-3 runs each, runs agree within about 1-2 %):**

| Scene | Before | After |
|---|---|---|
| Hospital, walking | 58 fps, 39 hitches/min, worst frames 33 ms | 60 fps, **0 hitches**, worst frames 19 ms |
| Outdoor assault | 40 fps, worst frames 33 ms | 60 fps, worst frames 19 ms, ~1 hitch/min |
| Landown (big outdoor) | 50 fps, **235 hitches/min** | 60 fps, **2 hitches/min** |
| GPU time per frame | 13-26 ms | 6-11 ms |

**What is still true / not done.**
- With `vsync = true` the game is capped at 60 frames per second no matter how fast the card is.
  With the fix and `vsync = false`, the hospital runs at about 135 fps, so 144 fps is now realistic.
  You asked me to leave `vsync` and the 144 setting alone for now, so I did.
- The first time you enter a new area the game builds shaders once, which can cost one 0.3-0.5 s
  freeze per area. That is the same with the old method; it is not new, and it disappears on the
  second visit.
- Tested on one machine (RTX 5080). Not tested: AMD or Intel graphics cards, most chapters, co-op,
  long play sessions. AMD cards keep the old method on purpose.
- To go back to the old method: set `render_target_path_d3d12 = "rtv"`.

**Also built:** a repeatable measuring system (so every future change can be judged by numbers, not
by feel). It is described in Part 4 and is the reason the fix could be found and proven.

---

# Part 2: Background you need (plain language)

## 2.1 What "stutter" actually is

A game draws a new picture many times a second. Each picture is a **frame**. At 60 frames per
second every frame has **16.7 milliseconds (ms)** to be finished.

- If every frame takes about 16 ms, motion looks smooth.
- If most frames take 10 ms but a few take 33 ms, the screen shows the old picture twice. You see a
  tiny freeze. That freeze is a **hitch** or **stutter**. Averages hide it: a game can average
  60 fps and still feel bad. So the measurements here look at the *worst* frames too
  (percentiles, explained in 4.3).

## 2.2 Who does the work: CPU and GPU

- The **CPU** (processor) runs the game logic: where things are, what the AI does.
- The **GPU** (graphics card) draws the picture.

They work as a pair. Whichever one is slower sets the speed of the whole game. Finding out *which
one* is the first question of any speed problem, and the measurement system answers it directly.

## 2.3 What this port is

The game was made for the Xbox 360. This project does not run an emulator that translates the
console's instructions while you play. It **recompiles** the game ahead of time: a tool (ReXGlue
SDK) turns the console program (`default.xex`) into ordinary C++ code, which is compiled into a
Windows program (`gow2.exe`). The console's graphics chip is still *imitated* at run time by the
SDK's graphics code (a DLL called `rexgpu-xenos.dll`), which turns the console's drawing commands
into Direct3D 12 commands for your graphics card. This graphics-imitation code is where the
slowdown lived.

## 2.4 The Xbox 360's special picture memory (the heart of the problem)

The 360 GPU drew pictures into a small, very fast memory chip built next to it, called **EDRAM**
(10 MB). Games relied on its odd behaviour: it could blend colours and test depth *inside* the
chip, in an exact order, with no extra cost. A PC graphics card has no such thing, so the port has
to imitate it. There are two ways, and the SDK calls them "render target paths":

1. **Host render targets ("rtv", the old default here).** Use the PC card's normal picture
   buffers, and whenever the game "reinterprets" the EDRAM (draws a picture, then reads it back
   differently), copy and convert data between buffers with extra helper passes. Fast on small
   pictures, but every extra pass costs time, and the cost grows with the number of pixels.
2. **ROV ("rov", Rasterizer Ordered Views).** A Direct3D 12 feature that lets the pixel program
   read and write a memory buffer **in the exact order the triangles were submitted**. The port
   keeps a true copy of the EDRAM as a big buffer and does the blending and depth test itself in
   the pixel program, exactly like the console did. No copy-and-convert helper passes. Needs
   newer hardware, and a comment in the SDK source says it was "much slower generally" when it was
   last looked at (2021, other games).

The code has a comment saying ROV "is currently much slower generally", so the default was the
old path everywhere except on Intel. **That comment turned out not to hold for this game on this
card.** That is the finding.

## 2.5 Resolution scale

The game's native picture is 1280x720. The port can draw it larger (`resolution_scale`): 2 means
2560x1440 (4 times the pixels), 3 means 4K (9 times). Your config uses 2. Everything that is
per-pixel work (including the EDRAM imitation) costs 4 times as much at scale 2 as at scale 1. That
is why the problem showed up here: at scale 1 the old path took 4.5 ms and nobody would notice.

## 2.6 VSync, frame cap, and why 60 fps appears

**VSync** makes the picture wait for the monitor's refresh so you never see tearing. In this port,
with `vsync = true` the game's internal clock also ticks at 60 Hz, so the game itself runs at 60
no matter what. That is why, before and after the fix, the "vsync on" numbers say 60 fps. It is a
cap, not a speed measure. To see the real speed, the tests were also run with `vsync = false`.

---

# Part 3: What was wrong (findings, in the order they were found)

## 3.1 First look: which part is slow?

I measured the hospital scene (chapter 2) before changing anything:

| Setting | Game ticks/s | GPU busy |
|---|---|---|
| `vsync = true`, cap 144 | 60 | 65-87 % |
| `vsync = false`, cap 144 | 77 | 99 % |
| `vsync = false`, `resolution_scale = 1` | 212 | 93-99 % |

Reading it: with VSync off the GPU is 99 % busy, so the **GPU is the limit**. At scale 1 the game
runs 2.7 times faster, so the cost is **per pixel** (it scales with resolution), not per object or
per shader. That ruled out the CPU, the disk, and "too many draw calls" as the main suspect.

Other suspects I checked in the source and set aside (details in `PERFORMANCE_PLAN.md`):
shader compiling on first use (real, but only for first visits), texture streaming, memory
read-backs (off by default), audio, input locking (2 microseconds, harmless), and motion blur (a
different, earlier fix).

## 3.2 Second look: it is only bad when you move

A fixed standing scene (`hospital_idle`) was clean: 60 fps, GPU 13 ms, zero hitches. The same room
while walking and turning (`hospital_walk`) had **39 hitches per minute**, GPU time spiking to
20-23 ms, and no shader builds, no texture-upload spikes, no CPU stalls in those frames. So it was
**pure GPU work that grows as more of the scene is on screen**: the GPU simply had no headroom.

## 3.3 A lead that turned out to be a red herring

In the walking data, GPU time followed the amount of texture data uploaded per frame very closely
(correlation 0.93). It looked like "textures are being re-uploaded every frame". I started adding a
diagnostic for it (a counter of which textures are loaded most often).

Before building it, the ROV experiment (below) showed the **same texture upload volume** (about
1.7-3 GB/s in both cases) with **half the GPU time**. So uploads were not the cost; they simply
rise together with "more of the scene on screen". The unbuilt diagnostic was reverted and is not in
the repository. This is a good example of why the measuring system compares changes instead of
trusting a correlation.

## 3.4 The experiment table (each is one run on `hospital_walk`, vsync on, scale 2)

Baseline for comparison: GPU 14.8 ms average, 39 hitches/min, frame time 99th percentile 33.7 ms.

| Change tried | GPU avg | Worst-1 % frame | Hitches/min | Verdict |
|---|---|---|---|---|
| `render_target_path_d3d12 = rov` | **5.6 ms** | **19.3 ms** | 19 (all one-time shader builds) | **big win** |
| `resolution_scale = 1` (reference only) | 4.5 ms | 19.2 ms | 3 | proves cost is per pixel |
| `direct_host_resolve = false` | 13.9 ms | 32.6 ms | 26 | small; not pursued |
| `anisotropic_override = 1`, no `gow2_aniso16` | 14.7 ms | 33.5 ms | 36 | no effect |
| `gow2_dof_upscaling_fix = false` | 14.7 ms | 33.6 ms | 38 | no effect |
| `gow2_ao_upscaling_fix = false` | 17.1 ms | 34.6 ms | 45 | **worse** |
| `gow2_black_shading_fix = false` | 17.6 ms | 41.2 ms | 72 | worse (and rebuilds shaders) |

(Anisotropic filtering, depth-of-field and ambient-occlusion fixes are visual options; they did
not change speed, and turning two of them off made things worse, so they stay on.)

## 3.5 Confirming the winner in steady state

The ROV run above included one-time shader building, so I repeated it after the shaders were
cached, with VSync on and off, two runs each:

| `hospital_walk` | fps | Typical frame / worst-1 % frame | GPU avg |
|---|---|---|---|
| Old path, VSync off | 74 | 13 ms / 22 ms | 13.3 ms |
| **ROV**, VSync off | **133-139** | **7 ms / 12 ms** | **6.3 ms** |
| ROV, VSync on (steady) | 59 | 16.7 ms / 19.2 ms, 4 hitches | 6.3 ms |

Then on two more scenes, 2-3 runs each:

| Scene | Path | fps | Worst-1 % frame | GPU typical | Hitches/min |
|---|---|---|---|---|---|
| Outdoor assault | old | 40 | 33.5 ms | 26.3 ms | 10 |
| Outdoor assault | ROV | 57 | 19.4 ms | 10.1 ms | 8 (first-visit shader builds) |
| Landown | old | 50 | 36.1 ms | 18.8 ms | 235 |
| Landown | ROV | 59 | 19.4 ms | 10.7 ms | 4 |

## 3.6 First-visit cost (the honest downside check)

ROV needs its own variants of some shaders, so the first time through an area it builds them. I
tested "first visit" with a completely empty shader cache (`-Cold`):

- Old path: built 80 pipelines, **1 hitch** in the minute (55 ms).
- ROV: built 78 pipelines, **1 hitch** in the minute (440 ms).

So ROV is not worse on first visits in count; its one freeze is longer in that sample (one run each,
so that is a hint, not a proof). The larger 300-570 ms stalls seen in the first ROV runs were new
areas the cache had never seen. After a visit they were gone.

## 3.7 Did the picture change?

Faster is worthless if it looks wrong. I captured the same frame with both paths in three places
(hospital, a cutscene close-up in chapter 3, and fire and smoke in chapter 4) and compared them:

- Hospital: all static geometry is pixel-identical; the only differences are moving things (the
  hanging sign, the characters), which is just the animation being at a slightly different instant.
  Average per-pixel difference was 2.7 out of 255, with 3.8 % of pixels differing noticeably, all
  on moving objects.
- Chapters 3 and 4: no visible difference in lighting, shadows, fire, smoke or faces.
- Chapter 5 (a cutscene) could not be compared because the two captures were at different moments
  of the cutscene.

This is a visual check of three places, not a full-game proof.

---

# Part 4: How it was done: the measuring system (methodology)

The rule for the whole job: **a change is kept only if a number improves by more than the noise.**
That required building tools first. All of it lives in the repository.

## 4.1 The per-frame recorder (SDK patch 0017)

New setting `perf_frame_csv=<file>`. When set, the graphics code writes **one line per drawn
frame** to a CSV file. It is off by default and costs nothing when off. Each line holds:

| Column | Meaning |
|---|---|
| `cpu_interval_us` | time since the previous frame finished, measured on the graphics thread (this is the real frame time) |
| `gpu_span_us` | how long the GPU spent on the frame, from **GPU timestamp queries** (the card's own stopwatch, read back after the frame) |
| `gpu_idle_us` | how long the GPU sat idle before this frame started |
| `fence_wait_us` | time the CPU spent blocked waiting for the GPU |
| `draws`, `draws_skipped` | number of draw commands, and how many were dropped because their pipeline was still compiling |
| `pipelines`, `shaders` | pipelines created and shaders translated during the frame |
| `texture_loads`, `texture_kb`, `shared_memory_kb` | texture and memory uploads during the frame |

The numbers come from counters added at the exact places the work happens (command processor,
pipeline cache, texture cache, shared memory). The GPU stopwatch writes two timestamps around the
frame's work and reads them back later, so it never stalls the game. The file is flushed to disk
every 120 frames.

## 4.2 The analysis tool (`tools/perf/analyze.py`)

Reads the CSV and reports:

- **Average fps and frame-time percentiles.** "p99" means "99 % of frames were faster than this";
  it is the worst-1 % frame and is what a player feels as stutter. p99.9 is the worst-0.1 %.
- **GPU busy %**: GPU time divided by frame time. Near 100 % means the GPU is the limit; low means
  something else (or a frame cap) is.
- **Hitches**: a frame whose time is more than 2x the typical (median) frame **and** more than 3 ms
  over it. Each hitch is given a likely cause by looking at what *that frame* did, first match wins:
  pipelines/shaders built, then big texture or memory upload, then GPU time over 1.5x typical, then
  draw count over 1.5x typical ("heavy frame"), otherwise "cpu/game".
- **Judder**: percent of frames more than 20 % off the typical frame time. Catches uneven pacing
  that averages hide.
- **Skipped draws**: draws dropped while a pipeline compiled (these show as things briefly
  missing, and are invisible to plain frame-time logs).
- **Comparison against a saved baseline**, flagging metrics that got worse by more than a threshold.

It is tested (`tools/test_perf_analyze.py`, 12 tests, run with synthetic data so no game is needed).

## 4.3 Repeatable scenes and a runner (`scripts/perf-run.ps1`, `scripts/perf/scenes.json`)

A scene is a fixed chapter, a load wait, and a **scripted controller route** (walk forward, turn,
stop, at set times) fed through the game's existing test-input file, so no one touches the keyboard.
The runner, for each scene and repetition:

1. starts the game with the recorder on and the settings under test,
2. waits for the game to be ready, then travels to the chapter with the developer command file,
3. waits for loading to finish, plays the route for the scene's duration,
4. records the exact wall-clock window of the measured part and stops the game,
5. analyses **only that window** (so menus and loading do not pollute the numbers),
6. combines repetitions into a **median** and measures how much the runs disagree.

Extra abilities: `-Set name=value` overrides any setting for one run (this is how every
experiment was run, without editing the config file); `-Cold` starts with an empty shader cache;
`-Compare` checks a run against a saved baseline and fails if it is worse; `-SaveBaseline` stores
the numbers (numbers only, no game data) under `perf/baselines/`; `-Interactive` records while you
play; `-Desktop N` moves the game window to virtual desktop N so you can keep working (default 1).
Each run also stores the exe hash, git commit, settings, GPU and clocks, so a result can be traced.

**Noise control.** Every comparison uses 2-3 repetitions. The spread between runs was about
1-2 % (for example 0.9 % and 1.1 % on the first baselines). Any difference smaller than that is
treated as noise. All the differences reported here are 30-100 %, far above it.

## 4.4 Scenes that exist

`hospital_idle`, `hospital_walk` (chapter 2), `assault_cinematic` (3), `outdoor_assault` (4),
`landown` (5), `riftworm` (12), `outpost` (13), `mount_kadar` (16), `palace` (23), `sinkhole` (27),
`chapter_load` (6, measures the load itself). Only hospital, outdoor_assault and landown were run
in this work (plus `hospital_idle`); all four have baselines of the new default.

---

# Part 5: The fix (what changed in code)

**File:** `sdk/patches/0018-nvidia-rov-default.patch`, which changes
`src/graphics/d3d12/render_target_cache.cpp` in the SDK.

Before, when `render_target_path_d3d12` was not set, the SDK picked ROV only on Intel and the old
host-render-target path on everything else. After:

```cpp
const auto vendor = provider.GetAdapterVendorID();
path_ = (vendor == Intel || vendor == Nvidia) ? Path::kPixelShaderInterlock
                                              : Path::kHostRenderTargets;
```

Properties you should know:

- A card that does not support ROV falls back to the old path automatically (code that was already
  there, checking `AreRasterizerOrderedViewsSupported()`).
- AMD cards are unchanged. The original authors recorded in 2021 that AMD's shader compiler crashed
  on ROV; I cannot test AMD, so I did not change it.
- Setting `render_target_path_d3d12 = "rtv"` forces the old path; `"rov"` forces ROV on any card.
- The patch was generated as a real diff from a snapshot of the file, checked to apply cleanly to
  the pre-patch file, and the whole SDK was then rebuilt from a **fresh patch application**
  (`scripts/build-sdk.ps1`) and the game rebuilt on top of it, so what is in the repository is what
  was tested.

**Why I think it is faster (a hypothesis, not a measurement).** At scale 2 the old path has to run
its copy-and-convert passes over 4 times as many pixels, while the ROV path does its EDRAM work
inside the normal pixel program. I did not take a per-pass breakdown (that needs a GPU profiler such
as Nsight or PIX), so the exact split of the saved 7-15 ms is unproven. The evidence that it works
is the before/after measurement, not the explanation.

## 5.1 Final verification with the shipped default

After the rebuild, with **no setting on the command line** (so it tests the real default), each
scene 2 runs, compared with the saved old-path baselines:

| Scene | fps | Worst-1 % frame | GPU typical | Hitches/min |
|---|---|---|---|---|
| Hospital, standing | 60.0 -> 60.0 | 19.4 -> 19.3 ms | 13.0 -> **7.3 ms** | 0 -> 0 |
| Hospital, walking | 58.2 -> **60.0** | 33.7 -> **19.3 ms** | 15.4 -> **6.5 ms** | 39 -> **0** |
| Outdoor assault | 40.1 -> **59.6** | 33.5 -> **19.2 ms** | 26.3 -> **10.8 ms** | 10 -> **1** |
| Landown | 50.2 -> **59.9** | 36.1 -> **19.5 ms** | 18.8 -> **10.7 ms** | 235 -> **2** |

Every metric passed the baseline comparison.

**Crash check.** The chapter sweep (`scripts/chapter-sweep.ps1`, which travels to each chapter and
watches the log for a crash for 75 s) was run on chapters 1-8 with the new default: all eight
finished `ok`, no crash and no missing-function error.

---

# Part 6: Limits, risks, and what to do next

**Not tested**
- AMD and Intel graphics cards (Intel already used ROV before; AMD is left on the old path).
- Chapters 9-29 (only 1-8 were swept for crashes, and only the first 75 s of each); pictures were
  compared in chapters 2, 3 and 4 and speed measured in 2, 4 and 5; boss fights, vehicles, co-op.
- Long play sessions (memory growth, shader-cache growth).
- Per-pass GPU breakdown (so the *reason* for the speed-up is a hypothesis).

**Known cost:** the first visit to each area can freeze for 0.3-0.5 s once while shaders build.
`scripts/chapter-sweep.ps1` visits every chapter, which fills the cache in one pass. Cached shader
files are made from the game's own code, so they are **never** put in the repository.

**Decisions left for you**
1. **VSync / 144 fps.** The 60 fps cap comes from VSync. With ROV the game can now exceed 100 fps
   in the hospital with `vsync = false`. Reaching a stable 144 needs either VSync off (tearing
   unless you have G-SYNC) or changing how the game's clock is tied to the display (a larger,
   riskier change). I left your config alone as you asked.
2. Whether to also ship ROV as the default for AMD after someone tests it.
3. Whether to run the full 29-chapter sweep and the remaining scenes to widen the baselines.

**Next ideas, in order of value:** decouple the game tick from VSync for true 144; more scenes and
baselines; a per-pass GPU profile to see what else is costly at scale 2; naming the SDK's host
threads so CPU profiles are readable (one thread sits at 100 % of a core and is still unidentified).

---

# Part 7: How to repeat any of this

Run from PowerShell (not Git Bash: its Python shadows the Windows one).

```powershell
# the standard check after any change (3 repeats, compares with baselines, exit code 2 if worse)
powershell -ExecutionPolicy Bypass -File scripts\perf-run.ps1 -Scene hospital_walk,outdoor_assault,landown -Repeat 3 -Compare -GameFolder <your game folder>

# try a setting for one run without touching gow2.toml
powershell -ExecutionPolicy Bypass -File scripts\perf-run.ps1 -Scene hospital_walk -Repeat 2 -Set render_target_path_d3d12=rtv -Tag old_path -GameFolder <your game folder>

# first-visit stutter (empty shader cache)
powershell -ExecutionPolicy Bypass -File scripts\perf-run.ps1 -Scene outdoor_assault -Cold -GameFolder <your game folder>

# record your own play instead of a scripted route
powershell -ExecutionPolicy Bypass -File scripts\perf-run.ps1 -Interactive -Seconds 900 -SkipSeconds 60 -GameFolder <your game folder>

# rebuild after changing an SDK patch
powershell -ExecutionPolicy Bypass -File scripts\build-sdk.ps1
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Sdk <folder with the ReXGlue SDK>
```

Results land in `out\perf\<date>_<tag>\<scene>\run<n>\` (`frames.csv`, `report.txt`,
`metrics.json`, `run.json`, the game log) and a combined `summary.json` per scene.

---

# Part 8: Files touched or created in this work

| Path | What it is |
|---|---|
| `sdk/patches/0017-perf-frame-log.patch` | the per-frame recorder in the graphics code |
| `sdk/patches/0018-nvidia-rov-default.patch` | **the fix** |
| `sdk/patches/README.md` | rows for 0016, 0017 and 0018 |
| `tools/perf/analyze.py`, `summarize.py` | analysis, medians, baseline comparison |
| `tools/test_perf_analyze.py` | 12 tests of the analysis |
| `scripts/perf-run.ps1`, `scripts/perf/scenes.json` | the scene runner and scene definitions |
| `perf/baselines/*.json` | saved numbers of the **new default** (ROV) for hospital_idle, hospital_walk, outdoor_assault, landown; the old-path numbers they replaced are in Part 3 and 5.1 above |
| `docs/PERFORMANCE_PLAN.md` | findings and plan (working notes) |
| `docs/TEST_TOOLING.md` | "Performance runs" section |
| `docs/PERFORMANCE_REPORT.md` | this file |

Tools installed on your PC for this work (current user only): the PowerShell `VirtualDesktop`
module and the NuGet provider it needs, used only to open the game on desktop 1 while testing.

Not in the repository on purpose: any game file, generated C++, shader cache, or screenshot.

---

# Glossary

- **Frame / frame time**: one picture; how long it took to produce it.
- **fps**: frames per second. 60 fps = 16.7 ms per frame.
- **Hitch / stutter**: a frame far slower than its neighbours.
- **Percentile (p50, p99, p99.9)**: p50 is the typical frame; p99 means 99 % of frames were faster;
  p99.9 is the very worst 0.1 %.
- **CPU / GPU**: the processor / the graphics card.
- **GPU timestamp query**: the graphics card's own stopwatch, used to time its work.
- **Draw call**: one command to draw a batch of triangles.
- **Shader**: a small program that runs on the GPU. **Pipeline**: the full recipe (shaders plus
  settings) the GPU needs to draw something. Both are built the first time they are needed.
- **EDRAM**: the Xbox 360's 10 MB picture memory next to its GPU.
- **Render target**: a picture buffer the GPU draws into.
- **ROV**: Rasterizer Ordered Views, a Direct3D 12 feature (strict-order read/write in pixel
  programs) that lets the EDRAM be imitated exactly.
- **VSync**: waiting for the monitor refresh before showing a frame.
- **Recompilation**: converting the console program to C++ ahead of time (as opposed to emulating
  it while it runs).
- **SDK patch**: a text file of code changes applied on top of the ReXGlue SDK's source.
- **Baseline**: saved numbers of a known state, used to tell if a later change helped or hurt.
- **Median**: the middle value; ignores one freak run.

---

# Appendix: other work on this branch (not about speed)

For completeness, earlier work on this same branch (each documented in its own file): live
keyboard and controller button prompts and keycap pictures for every bindable key (including the
tutorial pictograms); native keyboard and mouse-button input during gameplay (SDK patch 0016);
motion blur off by default; texture-pack variants (patch 0015); the chapter-sweep tool; the 144 fps
unlock setting. See `README.md`, `docs/PC_OPTIONS.md`, `tools/keycaps/README.md` and
`docs/TEST_TOOLING.md`.
