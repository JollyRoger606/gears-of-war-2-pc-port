"""Frame-time summary of a PresentMon --v2_metrics CSV: fps, 1% low, GPU time.

Usage: python tools/fps_stats.py out/perf/frames-*.csv
"""
import csv
import statistics
import sys


def frame_times(path: str) -> tuple[list[float], list[float]]:
    frames, gpu = [], []
    with open(path, encoding="utf-8-sig", newline="") as fh:
        for row in csv.DictReader(fh):
            try:
                frames.append(float(row["FrameTime"]))
                gpu.append(float(row["GPUBusy"]))
            except (KeyError, ValueError):
                continue
    return frames, gpu


def summary(frames: list[float], gpu: list[float]) -> str:
    if not frames:
        return "no frames"
    worst = sorted(frames)[-max(1, len(frames) // 100):]
    avg_fps = 1000.0 / statistics.mean(frames)
    low_fps = 1000.0 / statistics.mean(worst)
    return (f"{len(frames)} frames, avg {avg_fps:.1f} fps, 1% low {low_fps:.1f} fps, "
            f"GPU busy avg {statistics.mean(gpu):.2f} ms")


if __name__ == "__main__":
    for path in sys.argv[1:]:
        print(path, "->", summary(*frame_times(path)))
