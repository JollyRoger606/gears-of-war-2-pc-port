#!/usr/bin/env python3
"""Combines the metrics of several runs of one scene (median per metric), writes the summary and
compares it with a baseline.

    py tools/perf/summarize.py run1/metrics.json run2/metrics.json ... --json summary.json
                               [--baseline perf/baselines/hospital_idle.json] [--max-regress 10]

Exit status 2 when a metric is worse than the baseline by more than --max-regress percent.
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

SKIP_KEYS = {"worst_hitches", "hitch_causes"}


def median_of(values: list):
    nums = [v for v in values if isinstance(v, (int, float)) and not isinstance(v, bool)]
    return statistics.median(nums) if nums else None


def aggregate(runs: list[dict]) -> dict:
    """Median of every numeric metric across runs; dicts are combined key by key. The worst hitches
    and the hitch causes of the run with the median frame time p99 are kept as they are."""
    good = [r for r in runs if "error" not in r]
    if not good:
        return {"error": "no usable run", "runs": len(runs)}

    def merge(items: list[dict]) -> dict:
        out = {}
        for key in items[0]:
            if key in SKIP_KEYS:
                continue
            vals = [i.get(key) for i in items]
            if all(isinstance(v, dict) for v in vals):
                out[key] = merge(vals)
            else:
                out[key] = median_of(vals)
        return out

    summary = merge(good)
    ranked = sorted(good, key=lambda r: r["frame_ms"].get("p99", 0))
    typical = ranked[len(ranked) // 2]
    summary["hitch_causes"] = typical["hitch_causes"]
    summary["worst_hitches"] = typical["worst_hitches"]
    summary["runs"] = len(good)
    # How much the runs disagree: a change smaller than this is not a result.
    p99s = [r["frame_ms"]["p99"] for r in good]
    summary["frame_ms_p99_spread_pct"] = (100.0 * (max(p99s) - min(p99s)) / statistics.median(p99s)) if len(p99s) > 1 else 0.0
    return summary


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("metrics", nargs="+")
    ap.add_argument("--json")
    ap.add_argument("--baseline")
    ap.add_argument("--max-regress", type=float, default=10.0)
    args = ap.parse_args(argv)

    runs = []
    for path in args.metrics:
        with open(path, encoding="utf-8") as f:
            runs.append(json.load(f))
    summary = aggregate(runs)
    print(f"{summary.get('runs', 0)} of {len(runs)} runs usable")
    if "error" in summary:
        print(summary["error"])
        return 1
    print(analyze.report(summary))
    if summary["runs"] > 1:
        print(f"frame time p99 differs by {summary['frame_ms_p99_spread_pct']:.1f} % between runs")
    if args.json:
        with open(args.json, "w", encoding="utf-8", newline="\n") as f:
            json.dump(summary, f, indent=1)
            f.write("\n")
    if args.baseline and os.path.exists(args.baseline):
        with open(args.baseline, encoding="utf-8") as f:
            base = json.load(f)
        threshold = max(args.max_regress, summary["frame_ms_p99_spread_pct"])
        print(f"\nagainst {args.baseline} (threshold {threshold:.1f} %, the larger of --max-regress "
              f"and the spread between runs):")
        bad = 0
        for c in analyze.compare(base, summary, threshold):
            flag = "WORSE" if c["regressed"] else "ok"
            bad += c["regressed"]
            print(f"   {c['metric']:<26} {c['baseline']:>10.2f} -> {c['run']:>10.2f}  {c['change_pct']:+7.1f} %  {flag}")
        return 2 if bad else 0
    if args.baseline:
        print(f"\nno baseline at {args.baseline} yet (save this summary there to make one)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
