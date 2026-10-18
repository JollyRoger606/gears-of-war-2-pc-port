"""Tests of tools/perf/analyze.py on synthetic frame logs (no game, no GPU)."""
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "perf"))

import analyze  # noqa: E402


def frame(i, interval=13000, gpu=12000, **over):
    row = {"frame": i, "wall_ms": 1_000_000 + i * 13, "cpu_interval_us": interval, "gpu_span_us": gpu,
           "gpu_idle_us": 100, "fence_wait_us": 5000, "submissions": 1, "draws": 1000, "draws_skipped": 0,
           "pipelines": 0, "shaders": 0, "texture_loads": 2, "texture_kb": 200, "shared_memory_kb": 500}
    row.update(over)
    return row


def steady(n=300, **kw):
    return [frame(i, **kw) for i in range(n)]


def test_percentile_and_distribution():
    assert analyze.percentile([1, 2, 3, 4, 5], 50) == 3
    assert analyze.percentile([10], 99) == 10
    assert analyze.percentile([1, 2], 50) == 1.5
    d = analyze.dist([1.0, 2.0, 3.0, 4.0])
    assert d["max"] == 4.0 and d["p50"] == 2.5


def test_steady_run_has_no_hitches():
    m = analyze.analyze(steady())
    assert m["hitches"] == 0 and m["off_pace_pct"] == 0
    assert abs(m["frame_ms"]["p50"] - 13.0) < 1e-6
    assert abs(m["fps_avg"] - 1000 / 13) < 0.01
    assert 90 < m["gpu_busy_pct"] < 95


def test_hitch_causes():
    rows = steady()
    rows[50] = frame(50, interval=60000, pipelines=3, shaders=4)
    rows[100] = frame(100, interval=50000, texture_kb=40000, texture_loads=30)
    rows[150] = frame(150, interval=45000, gpu=40000)
    rows[200] = frame(200, interval=45000, draws=4000)
    rows[250] = frame(250, interval=45000)
    m = analyze.analyze(rows)
    assert m["hitches"] == 5
    assert set(m["hitch_causes"]) == {"shader/pipeline", "texture/memory upload", "gpu", "heavy frame", "cpu/game"}
    assert m["worst_hitches"][0]["frame"] == 50


def test_small_wobble_is_not_a_hitch():
    rows = steady()
    rows[40] = frame(40, interval=17000)  # 4 ms over, under 2x the median
    assert analyze.analyze(rows)["hitches"] == 0
    # a 2x frame at a very high frame rate is under the 3 ms floor
    fast = [frame(i, interval=3000) for i in range(200)]
    fast[10] = frame(10, interval=7000)
    assert analyze.analyze(fast)["hitches"] == 1  # 4 ms over the median and over 2x


def test_judder_counts_frames_off_the_typical_time():
    rows = [frame(i, interval=16000 if i % 2 else 28000) for i in range(100)]
    m = analyze.analyze(rows)
    assert m["off_pace_pct"] > 40


def test_skipped_draws_and_pipelines_are_counted():
    rows = steady()
    for i in range(10, 20):
        rows[i] = frame(i, draws_skipped=5)
    rows[30] = frame(30, pipelines=2, shaders=2)
    m = analyze.analyze(rows)
    assert m["skipped_draws_total"] == 50
    assert abs(m["skipped_draw_frames_pct"] - 100 * 10 / 300) < 1e-6
    assert m["pipelines_built"] == 2 and m["shaders_built"] == 2


def test_missing_gpu_timing_is_tolerated():
    rows = steady(gpu=-1)
    for r in rows:
        r["gpu_idle_us"] = -1
    m = analyze.analyze(rows)
    assert m["gpu_ms"] == {} and m["gpu_busy_pct"] is None


def test_too_few_frames():
    assert "error" in analyze.analyze(steady(3))


def test_select_window_and_skip():
    rows = steady(100)
    sel = analyze.select(rows, from_wall_ms=1_000_000 + 13 * 10, to_wall_ms=1_000_000 + 13 * 20, skip=2)
    assert [r["frame"] for r in sel][0] == 12 and sel[-1]["frame"] == 20


def test_compare_flags_regressions_only():
    base = analyze.analyze(steady())
    same = analyze.compare(base, analyze.analyze(steady()), 10)
    assert not any(c["regressed"] for c in same)
    slow = analyze.analyze(steady(interval=20000))
    changes = analyze.compare(base, slow, 10)
    worse = {c["metric"] for c in changes if c["regressed"]}
    assert "frame_ms.p50" in worse and "fps_avg" in worse
    faster = analyze.compare(slow, base, 10)
    assert not any(c["regressed"] for c in faster)


def test_csv_round_trip(tmp_path):
    p = tmp_path / "f.csv"
    rows = steady(10)
    with open(p, "w", newline="") as f:
        f.write(",".join(analyze.FIELDS) + "\n")
        for r in rows:
            f.write(",".join(str(r[k]) for k in analyze.FIELDS) + "\n")
        f.write("garbage,row\n")
    loaded = analyze.load_frames(str(p))
    assert len(loaded) == 10 and loaded[3] == rows[3]
    out = tmp_path / "m.json"
    assert analyze.main([str(p), "--skip", "0", "--json", str(out)]) == 0
    assert json.loads(out.read_text())["frames"] == 10


def test_summarize_medians_and_baseline(tmp_path):
    import summarize

    runs = [analyze.analyze(steady(interval=iv)) for iv in (13000, 14000, 13500)]
    s = summarize.aggregate(runs)
    assert s["runs"] == 3 and abs(s["frame_ms"]["p50"] - 13.5) < 1e-6
    assert s["frame_ms_p99_spread_pct"] > 0
    assert "worst_hitches" in s
    bad = summarize.aggregate([analyze.analyze(steady(3))])
    assert "error" in bad
    paths = []
    for i, r in enumerate(runs):
        p = tmp_path / f"m{i}.json"
        p.write_text(json.dumps(r))
        paths.append(str(p))
    base = tmp_path / "base.json"
    assert summarize.main(paths + ["--json", str(base)]) == 0
    assert summarize.main(paths + ["--baseline", str(base)]) == 0
    slow = tmp_path / "slow.json"
    slow.write_text(json.dumps(analyze.analyze(steady(interval=30000))))
    assert summarize.main([str(slow), "--baseline", str(base)]) == 2
    assert summarize.main([str(slow), "--baseline", str(tmp_path / "none.json")]) == 0
