# Performance: findings, test framework and optimization plan

Status: planning. Numbers below were measured on a Ryzen/RTX 5080 PC, 3440x1440 display, Act 1
chapter 2 (the hospital), `resolution_scale = 2` unless stated. They are one scene, not the game.

## What was measured

| Setting | Game ticks per second | GPU busy |
|---|---|---|
| `vsync = true`, `gow2_fps_cap = 144` | 60 (16.7 ms, p99 19.5) | 65-87 % |
| `vsync = false`, cap 144 | 77 (13.0 ms, p99 15) | 99 % |
| `vsync = false`, no cap | 75 (13.3 ms) | 99 % |
| `vsync = false`, `resolution_scale = 1` | 212 (4.7 ms) | 93-99 % |

Three facts follow.

1. **The cap cannot reach 144 with VSync on.** The guest's vertical blank timer
   (`graphics_system.cpp`, "GPU VSync" thread) runs at the guest video mode's 60 Hz while `vsync` is
   on, and the unlock patch makes the game tick once per blank. `gow2_fps_cap` only slows it down.
   With `vsync = false` that timer runs at 1000 Hz and the cap decides.
2. **60 ticks on a 144 Hz screen is uneven by construction** (a frame shows for 2 or 3 refreshes in
   turn). That alone looks like "wonky frames" even when frame times are perfect.
3. **At `resolution_scale = 2` the GPU is the limit** (99 % busy, 13 ms), at 4x the pixels of scale 1
   (4.7 ms). The cost is per pixel, so it is render-target and post-process work, not draw count or
   the CPU. The render target path is the host-render-target one (`render_target_path_d3d12` unset
   picks it on NVIDIA).

One host thread sits at 100 % of a core in every configuration (it is not named; the SDK does not
name its host threads). Which thread it is, and whether it ever waits on the GPU, is unknown.

## Other possible causes, checked against the source

| Cause | State in this build |
|---|---|
| Shader and pipeline compile on first use | Persistent storage exists (`store_shaders`, shader and pipeline description files) and loads in about 80 ms at boot. The stored set grew from 52 to 833 shaders and from 43 to 909 pipelines while I visited six chapters, so each new area adds hundreds. |
| What a missing pipeline does | `async_shader_compilation` is on: the draw is **skipped** until its pipeline is built on a worker thread (`command_processor.cpp`, "return true"). That shows as objects or effects missing for a few frames, not as a long frame. Frame-time logs cannot see it. |
| Texture cache and streaming | Limits 1024 MB soft, 2048 MB hard; `gow2_texture_streaming_fix` on. Upload hitches not measured. |
| Memexport and resolve readback | The D3D12 build defaults both CPU readbacks off, so no forced GPU/CPU sync from them. |
| Occlusion queries, memory page state refresh | On (defaults). Cost not measured. |
| Audio | `audio_maxqframes = 8`; its help text says lower values can stutter. Not measured. |
| Host threads | `ignore_thread_priorities` and `ignore_thread_affinities` are on, so the GPU thread, the game thread and the audio threads all compete at normal priority. |
| Input lock contention | `XAMINPUTGUARD OVERLAP` warnings appear, a handful per session; the lock hold time is 2 microseconds on average. Not a cause. |
| Built-in perf counters | `perf_log_csv` and a counter list exist, but nothing in the SDK increments the counters, so they log zeros. They need wiring before use. |
| Motion blur | Was a real cause of "blurry when moving"; off by default now. |

## Test framework (built)

One command gives the same numbers on any change, so a change is kept only if a number improves.
Usage is in `docs/TEST_TOOLING.md` ("Performance runs").

| Piece | Where |
|---|---|
| Per-frame log: CPU interval, GPU span and idle (timestamp queries), fence wait, draws, skipped draws, pipelines and shaders built, texture loads and KB, shared-memory KB | SDK patch 0017, `perf_frame_csv` |
| Hitch report: avg, p50/p99/p99.9, frames far over the median, each hitch classified (shader/pipeline, texture/memory upload, gpu, heavy frame, cpu/game), skipped draws | `tools/perf/analyze.py` |
| Median of repeated runs, spread, baseline comparison (exit 2 on regression) | `tools/perf/summarize.py` |
| Fixed scenes with pad routes | `scripts/perf/scenes.json` |
| Runner: start, travel, play the route, stop, analyse; `-Cold`, `-Interactive`, `-Set` | `scripts/perf-run.ps1` |
| Baselines (numbers only) | `perf/baselines/<scene>.json` |
| Tests of the analysis | `tools/test_perf_analyze.py` |

Still open: PresentMon as an outside check of present-to-present and display timings (needs admin
or the Performance Log Users group); Nsight or PIX for one frame's pass breakdown; naming the SDK's
host threads so CPU profiles say what is hot.

## First baselines (vsync on, 144 cap, scale 2, hospital, 3 runs each)

| Scene | fps | frame p99 | GPU avg / p95 | Hitches | Spread between runs |
|---|---|---|---|---|---|
| `hospital_idle` | 60.0 | 19.4 ms | 13.1 / 13.7 ms | 0 | 1.1 % |
| `hospital_walk` | 58.2 | 33.7 ms | 14.8 / 20.5 ms | 39 per minute, 712 ms lost (all "heavy frame") | 0.9 % |

Standing still is clean. Walking and turning is where the stutter is: the GPU time rises from 13 ms
to 20-23 ms in the worst frames, which is over the 16.7 ms a 60 Hz frame allows, so those frames are
shown for two refreshes (33 ms). No hitch was a shader build, a texture upload or a CPU stall: no
pipelines were built, no draws skipped. It is GPU work that grows with what is on screen, so the fix
is in the per-pixel and per-pass cost (step 2 below), not in caching or threads. The numbers repeat
to about 1 %, so a change of more than that is real.

## Optimization plan, in order

1. **Timing/pacing (cheap, large visual effect).** Decouple the guest blank rate from the host
   present: give the guest a vblank at the display's refresh (or a chosen rate, 120/144) while the
   host presents with proper sync (or VRR). Test that game logic stays correct at the higher tick
   (animation, physics, AI, timers that assume 30/60). Fallback: `vsync = false` with a cap just
   under the refresh rate.
2. **Per-pixel GPU cost.** With GPU timestamps and one Nsight capture, split the 13 ms by pass:
   resolves and EDRAM ownership transfers at 2x, depth conversion, AO, depth of field, shadows,
   post effects. Then: skip or cheapen passes the port already patches (AO and DoF "upscaling fixes"
   are cvars, A/B them), reduce transfer shader cost, try `render_target_path_d3d12 = rov` only as a
   comparison, consider a temporal upscaler at `resolution_scale = 1` as the quality/cost trade
   (FSR/DLSS code already exists in patch 0002).
3. **Pipeline stalls and missing draws.** Count skipped draws; if frequent, pre-warm: run the chapter
   sweep (`scripts/chapter-sweep.ps1`) once so the pipeline files hold the whole game, and consider
   building pipelines for known combinations at boot. Compare `async_shader_compilation` on and off
   for hitch length versus visual pop-in.
4. **Texture and memory hitches.** Measure upload bytes per frame; tune cache limits; check that
   textures are not rebuilt after the memory watch fires (`clear_memory_page_state`).
5. **CPU and threads.** Name threads, find the 100 % thread, check lock contention and busy waits;
   try normal-above priority for the GPU command thread; compile flags for the generated code (LTO,
   profile-guided optimization) measured on a CPU-bound scene.
6. **Physics and engine code.** Only if scenes show the game thread, not the GPU, as the limit
   (large fights). UE3's physics runs in the recompiled guest code; look for hot functions with a
   sampling profiler before touching any.

## Rust

Not part of the plan. The game code is generated C++ and the GPU backend is C++; a rewrite only
helps once a profile shows one specific CPU-bound host function. The measured limit is the GPU.

## Decisions needed

- Whether a tearing-free 144 Hz needs VRR on your monitor (G-SYNC) or whether you want the guest
  blank decoupled as in step 1.
- Which resolution trade is acceptable if the 13 ms per-pixel cost does not come down: scale 1 with
  an upscaler, or 2 at around 75 fps.
