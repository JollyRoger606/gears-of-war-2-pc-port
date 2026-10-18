#!/usr/bin/env python3
"""Reads the per-frame CSV of the port (perf_frame_csv, SDK patch 0017) and reports frame pacing,
GPU time, hitches with their likely cause, and how a run compares with a baseline.

    py tools/perf/analyze.py frames.csv [--from-wall-ms N] [--to-wall-ms N] [--skip N]
                                       [--json out.json] [--baseline base.json] [--max-regress 10]

Columns of the CSV (one row per guest frame): frame, wall_ms, cpu_interval_us (time since the
previous frame closed, on the GPU command thread), gpu_span_us (GPU time of the frame from
timestamp queries), gpu_idle_us (GPU idle since the previous frame), fence_wait_us (CPU blocked
waiting for the GPU), submissions, draws, draws_skipped (draws dropped while a pipeline was still
being built), pipelines, shaders (created/translated during the frame), texture_loads, texture_kb,
shared_memory_kb (uploaded during the frame).

Exit status 0, or 2 when --baseline is given and a metric got worse by more than --max-regress.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
from dataclasses import dataclass

FIELDS = ["frame", "wall_ms", "cpu_interval_us", "gpu_span_us", "gpu_idle_us", "fence_wait_us",
          "submissions", "draws", "draws_skipped", "pipelines", "shaders", "texture_loads",
          "texture_kb", "shared_memory_kb"]


def load_frames(path: str) -> list[dict]:
    """Rows as dicts of ints, in frame order. A row that cannot be read is dropped."""
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for rec in csv.DictReader(f):
            try:
                rows.append({k: int(rec[k]) for k in FIELDS})
            except (KeyError, ValueError, TypeError):
                continue
    rows.sort(key=lambda r: r["frame"])
    return rows


def select(rows: list[dict], from_wall_ms: int | None = None, to_wall_ms: int | None = None,
           skip: int = 0) -> list[dict]:
    """The window to analyze: by wall clock, then dropping the first `skip` rows (the first row of
    a log holds everything counted since start-up)."""
    out = [r for r in rows if (from_wall_ms is None or r["wall_ms"] >= from_wall_ms)
           and (to_wall_ms is None or r["wall_ms"] <= to_wall_ms)]
    return out[skip:]


def percentile(values: list[float], p: float) -> float:
    """Linear-interpolated percentile (p in 0..100) of a non-empty list."""
    s = sorted(values)
    if len(s) == 1:
        return float(s[0])
    k = (len(s) - 1) * p / 100.0
    lo, hi = math.floor(k), math.ceil(k)
    return float(s[lo] + (s[hi] - s[lo]) * (k - lo))


def dist(values: list[float]) -> dict:
    if not values:
        return {}
    return {"avg": statistics.fmean(values), "p50": percentile(values, 50),
            "p95": percentile(values, 95), "p99": percentile(values, 99),
            "p999": percentile(values, 99.9), "max": float(max(values))}


@dataclass
class Hitch:
    frame: int
    at_s: float          # seconds from the first analyzed frame
    interval_ms: float
    cause: str
    detail: str


def classify(row: dict, med: dict) -> tuple[str, str]:
    """Why one slow frame was slow, from what the frame itself did. First match wins."""
    if row["pipelines"] > 0 or row["shaders"] > 0:
        return "shader/pipeline", f"{row['pipelines']} pipelines, {row['shaders']} shaders built"
    big_tex = row["texture_kb"] > 4 * med["texture_kb"] + 1024
    big_mem = row["shared_memory_kb"] > 4 * med["shared_memory_kb"] + 2048
    if big_tex or big_mem:
        return "texture/memory upload", (f"{row['texture_loads']} textures {row['texture_kb']} KB, "
                                         f"{row['shared_memory_kb']} KB shared memory")
    if row["gpu_span_us"] >= 0 and med["gpu_span_us"] > 0 and row["gpu_span_us"] > 1.5 * med["gpu_span_us"]:
        return "gpu", f"gpu {row['gpu_span_us'] / 1000:.1f} ms (typical {med['gpu_span_us'] / 1000:.1f})"
    if row["draws"] > 1.5 * med["draws"] + 50:
        return "heavy frame", f"{row['draws']} draws (typical {med['draws']:.0f})"
    return "cpu/game", "nothing unusual in the GPU or upload counters"


def analyze(rows: list[dict]) -> dict:
    """Metrics and hitches of a window of rows. Needs at least a few frames."""
    n = len(rows)
    if n < 5:
        return {"frames": n, "error": "too few frames"}
    intervals = [r["cpu_interval_us"] for r in rows if r["cpu_interval_us"] > 0]
    gpu = [r["gpu_span_us"] for r in rows if r["gpu_span_us"] >= 0]
    idle = [r["gpu_idle_us"] for r in rows if r["gpu_idle_us"] >= 0]
    duration_s = (rows[-1]["wall_ms"] - rows[0]["wall_ms"]) / 1000.0
    med_interval = statistics.median(intervals)
    med = {"gpu_span_us": statistics.median(gpu) if gpu else -1,
           "draws": statistics.median(r["draws"] for r in rows),
           "texture_kb": statistics.median(r["texture_kb"] for r in rows),
           "shared_memory_kb": statistics.median(r["shared_memory_kb"] for r in rows)}

    hitches: list[Hitch] = []
    t0 = rows[0]["wall_ms"]
    for r in rows:
        iv = r["cpu_interval_us"]
        if iv > 2 * med_interval and iv > med_interval + 3000:
            cause, detail = classify(r, med)
            hitches.append(Hitch(r["frame"], (r["wall_ms"] - t0) / 1000.0, iv / 1000.0, cause, detail))
    by_cause: dict[str, dict] = {}
    for h in hitches:
        c = by_cause.setdefault(h.cause, {"count": 0, "lost_ms": 0.0})
        c["count"] += 1
        c["lost_ms"] += h.interval_ms - med_interval / 1000.0

    # Frames that stay on screen a different time from the typical one (judder).
    off_pace = sum(1 for v in intervals if abs(v - med_interval) > 0.2 * med_interval)

    metrics = {
        "frames": n,
        "duration_s": duration_s,
        "fps_avg": (len(intervals) / (sum(intervals) / 1e6)) if intervals else 0.0,
        "frame_ms": {k: v / 1000.0 for k, v in dist(intervals).items()},
        "gpu_ms": {k: v / 1000.0 for k, v in dist(gpu).items()},
        "gpu_idle_ms_avg": (statistics.fmean(idle) / 1000.0) if idle else None,
        "gpu_busy_pct": (100.0 * sum(gpu) / sum(intervals)) if gpu and intervals else None,
        "cpu_wait_on_gpu_ms_avg": statistics.fmean(r["fence_wait_us"] for r in rows) / 1000.0,
        "off_pace_pct": 100.0 * off_pace / max(1, len(intervals)),
        "hitches": len(hitches),
        "hitches_per_min": (60.0 * len(hitches) / duration_s) if duration_s > 0 else 0.0,
        "hitch_lost_ms_total": sum(c["lost_ms"] for c in by_cause.values()),
        "hitch_causes": by_cause,
        "draws_avg": statistics.fmean(r["draws"] for r in rows),
        "skipped_draws_total": sum(r["draws_skipped"] for r in rows),
        "skipped_draw_frames_pct": 100.0 * sum(1 for r in rows if r["draws_skipped"] > 0) / n,
        "pipelines_built": sum(r["pipelines"] for r in rows),
        "shaders_built": sum(r["shaders"] for r in rows),
        "texture_mb_per_s": (sum(r["texture_kb"] for r in rows) / 1024.0 / duration_s) if duration_s > 0 else 0.0,
        "shared_memory_mb_per_s": (sum(r["shared_memory_kb"] for r in rows) / 1024.0 / duration_s) if duration_s > 0 else 0.0,
    }
    worst = sorted(hitches, key=lambda h: -h.interval_ms)[:10]
    metrics["worst_hitches"] = [h.__dict__ for h in worst]
    return metrics


# Metrics that get worse when they go up, and ones that get worse when they go down.
HIGHER_IS_WORSE = ["frame_ms.p50", "frame_ms.p99", "frame_ms.p999", "gpu_ms.p50", "gpu_ms.p99",
                   "hitches_per_min", "off_pace_pct", "skipped_draw_frames_pct",
                   "cpu_wait_on_gpu_ms_avg"]
LOWER_IS_WORSE = ["fps_avg"]


def get(m: dict, path: str):
    cur = m
    for part in path.split("."):
        if not isinstance(cur, dict) or part not in cur:
            return None
        cur = cur[part]
    return cur


def compare(base: dict, run: dict, max_regress_pct: float) -> list[dict]:
    """Every metric's change, with `regressed` set when it is worse by more than the threshold."""
    out = []
    for path, sign in [(p, 1) for p in HIGHER_IS_WORSE] + [(p, -1) for p in LOWER_IS_WORSE]:
        b, r = get(base, path), get(run, path)
        if b is None or r is None:
            continue
        change = (r - b) / b * 100.0 if b else (0.0 if r == 0 else math.inf)
        # A small absolute move on a tiny number is not a regression (hitches per minute near 0).
        worse = change * sign > max_regress_pct and abs(r - b) > 0.05
        out.append({"metric": path, "baseline": b, "run": r, "change_pct": change, "regressed": worse})
    return out


def fmt_dist(label: str, d: dict, unit: str = "ms") -> str:
    if not d:
        return f"{label}: no data"
    return (f"{label}: avg {d['avg']:.2f} {unit}  p50 {d['p50']:.2f}  p95 {d['p95']:.2f}  "
            f"p99 {d['p99']:.2f}  p99.9 {d['p999']:.2f}  max {d['max']:.2f}")


def report(m: dict) -> str:
    if "error" in m:
        return f"{m['frames']} frames: {m['error']}"
    lines = [
        f"{m['frames']} frames over {m['duration_s']:.1f} s, {m['fps_avg']:.1f} fps average",
        fmt_dist("frame time", m["frame_ms"]),
        fmt_dist("GPU time  ", m["gpu_ms"]),
    ]
    if m["gpu_busy_pct"] is not None:
        lines.append(f"GPU busy {m['gpu_busy_pct']:.0f} % of frame time, idle between frames "
                     f"{m['gpu_idle_ms_avg']:.2f} ms; CPU waits on the GPU {m['cpu_wait_on_gpu_ms_avg']:.2f} ms/frame")
    lines.append(f"{m['off_pace_pct']:.1f} % of frames more than 20 % off the typical frame time (judder)")
    lines.append(f"{m['hitches']} hitches ({m['hitches_per_min']:.1f}/min), {m['hitch_lost_ms_total']:.0f} ms lost in total")
    for cause, c in sorted(m["hitch_causes"].items(), key=lambda kv: -kv[1]["lost_ms"]):
        lines.append(f"   {cause}: {c['count']} hitches, {c['lost_ms']:.0f} ms lost")
    lines.append(f"{m['draws_avg']:.0f} draws/frame; skipped draws {m['skipped_draws_total']} "
                 f"({m['skipped_draw_frames_pct']:.1f} % of frames)")
    lines.append(f"built {m['pipelines_built']} pipelines, {m['shaders_built']} shaders; "
                 f"uploads {m['texture_mb_per_s']:.1f} MB/s textures, {m['shared_memory_mb_per_s']:.1f} MB/s shared memory")
    if m["worst_hitches"]:
        lines.append("worst hitches:")
        for h in m["worst_hitches"][:5]:
            lines.append(f"   frame {h['frame']} at {h['at_s']:.1f} s: {h['interval_ms']:.1f} ms, "
                         f"{h['cause']} ({h['detail']})")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv")
    ap.add_argument("--from-wall-ms", type=int)
    ap.add_argument("--to-wall-ms", type=int)
    ap.add_argument("--skip", type=int, default=2, help="drop this many first rows (default 2)")
    ap.add_argument("--json", help="write the metrics here")
    ap.add_argument("--baseline", help="metrics JSON of an earlier run to compare with")
    ap.add_argument("--max-regress", type=float, default=10.0, help="percent (default 10)")
    args = ap.parse_args(argv)

    rows = select(load_frames(args.csv), args.from_wall_ms, args.to_wall_ms, args.skip)
    metrics = analyze(rows)
    print(report(metrics))
    if args.json:
        with open(args.json, "w", encoding="utf-8", newline="\n") as f:
            json.dump(metrics, f, indent=1)
            f.write("\n")
    if args.baseline and "error" not in metrics:
        with open(args.baseline, encoding="utf-8") as f:
            base = json.load(f)
        changes = compare(base, metrics, args.max_regress)
        print(f"\nagainst {args.baseline} (regression threshold {args.max_regress:g} %):")
        bad = 0
        for c in changes:
            flag = "WORSE" if c["regressed"] else "ok"
            bad += c["regressed"]
            print(f"   {c['metric']:<26} {c['baseline']:>10.2f} -> {c['run']:>10.2f}  {c['change_pct']:+7.1f} %  {flag}")
        return 2 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
