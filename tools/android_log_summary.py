"""Per-window summary of an Android runtime log (files/logs/gow2_NNN.log).

Joins the FRAMES lines of gow2_frame_log (game fps, plus the GPU clock and thermal state the app
could read), the pipeline creations logged in each window and the GPUSTATS lines of
vulkan_android_stats_log, and prints one row per FRAMES window. GPU megacycles per game frame are
clock x busy / fps, the number that stays put while the thermal limit moves the clock.

When the app could not read the GPU clock ("gpu n/a" in FRAMES), --clocks takes samples taken over
adb, one per line: "YYYY-MM-DD HH:MM:SS <gpuclk Hz> <busy %> <thermal_pwrlevel>" in the phone's
local time (docs/ANDROID_EXPERIMENTS.md has the sampling loop).

Usage: python tools/android_log_summary.py <gow2_NNN.log> [--clocks clocks.txt] [--quiet-max 4]
"""
import argparse
import re
import statistics
import sys
from dataclasses import dataclass, field
from datetime import datetime, timedelta

TIMESTAMP = re.compile(r"^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]")
FRAMES = re.compile(
    r"FRAMES (?P<seconds>[\d.]+) s: (?P<frames>\d+) frames, (?P<fps>[\d.]+) fps, "
    r"longest frame (?P<longest>[\d.]+) ms"
    r"(?:, gpu (?P<mhz>\d+|n/a)(?: MHz)? of (?P<max_mhz>\d+|n/a)(?: MHz)? "
    r"busy (?P<busy>\d+|n/a)%? pwrlevel (?P<pwrlevel>\d+|n/a) thermal (?P<thermal>-?\d+|n/a) "
    r"headroom (?P<headroom>[\d.]+|n/a))?")
PIPELINE_CREATED = "Creating graphics pipeline state"
STORAGE_LOADED = re.compile(r"Created (\d+) graphics pipelines from Vulkan storage")
GPUSTATS = re.compile(r"GPUSTATS [\d.]+ s: (?P<body>.*)$")
GPUSTATS_FIELD = re.compile(r"([a-z_][a-z0-9_]*) (\d+(?:\.\d+)?)")
GPUSTATS_COLUMNS = ("draws", "passes", "submits", "drains", "ends", "zero", "pipeline_setup",
                    "unpresented", "missing_textures")
CLOCK_SAMPLE = re.compile(
    r"^(?P<time>\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) (?P<hz>\d+) (?P<busy>\d+)\s*%?\s+(?P<pwrlevel>\d+)")
HZ_PER_MHZ = 1_000_000


@dataclass
class Window:
    time: datetime
    seconds: float
    fps: float
    longest_ms: float
    mhz: float | None
    busy: float | None
    pwrlevel: str
    thermal: str
    headroom: str
    pipelines_created: int = 0
    gpustats: dict[str, float] = field(default_factory=dict)

    def mcycles_per_frame(self) -> float | None:
        if self.mhz is None or self.busy is None or self.fps <= 0:
            return None
        return self.mhz * self.busy / 100.0 / self.fps


@dataclass
class ClockSample:
    time: datetime
    mhz: float
    busy: float
    pwrlevel: int


def parse_time(line: str) -> datetime | None:
    match = TIMESTAMP.match(line)
    return datetime.strptime(match.group(1), "%Y-%m-%d %H:%M:%S.%f") if match else None


def optional_number(text: str | None) -> float | None:
    if text is None or text == "n/a":
        return None
    return float(text)


def parse_window(time: datetime, match: re.Match) -> Window:
    return Window(time=time, seconds=float(match["seconds"]), fps=float(match["fps"]),
                  longest_ms=float(match["longest"]), mhz=optional_number(match["mhz"]),
                  busy=optional_number(match["busy"]), pwrlevel=match["pwrlevel"] or "n/a",
                  thermal=match["thermal"] or "n/a", headroom=match["headroom"] or "n/a")


def parse_gpustats(body: str) -> dict[str, float]:
    # "occlusion ends 3 host 3 zero 0" keeps the last word before each number as its name.
    return {name: float(value) for name, value in GPUSTATS_FIELD.findall(body)}


def read_windows(lines: list[str]) -> tuple[list[Window], list[int]]:
    windows: list[Window] = []
    storage_loads: list[int] = []
    pending_pipelines = 0
    last_stats: dict[str, float] = {}
    for line in lines:
        time = parse_time(line)
        if PIPELINE_CREATED in line:
            pending_pipelines += 1
        elif (stats := GPUSTATS.search(line)) is not None:
            last_stats = parse_gpustats(stats["body"])
        elif (loaded := STORAGE_LOADED.search(line)) is not None:
            storage_loads.append(int(loaded.group(1)))
        elif time is not None and (frames := FRAMES.search(line)) is not None:
            window = parse_window(time, frames)
            window.pipelines_created = pending_pipelines
            window.gpustats = last_stats
            windows.append(window)
            pending_pipelines = 0
            last_stats = {}
    return windows, storage_loads


def read_clock_samples(lines: list[str]) -> list[ClockSample]:
    samples = []
    for line in lines:
        match = CLOCK_SAMPLE.match(line.strip())
        if match is None:
            continue
        samples.append(ClockSample(time=datetime.strptime(match["time"], "%Y-%m-%d %H:%M:%S"),
                                   mhz=int(match["hz"]) / HZ_PER_MHZ, busy=float(match["busy"]),
                                   pwrlevel=int(match["pwrlevel"])))
    return samples


def samples_in_window(window: Window, samples: list[ClockSample]) -> list[ClockSample]:
    start = window.time - timedelta(seconds=window.seconds)
    return [s for s in samples if start <= s.time <= window.time]


def fill_clock_from_samples(window: Window, samples: list[ClockSample]) -> None:
    inside = samples_in_window(window, samples)
    if window.mhz is not None or not inside:
        return
    window.mhz = statistics.mean(s.mhz for s in inside)
    window.busy = statistics.mean(s.busy for s in inside)
    window.pwrlevel = str(max(s.pwrlevel for s in inside))


def format_optional(value: float | None, digits: int = 1) -> str:
    return "n/a" if value is None else f"{value:.{digits}f}"


def format_window(window: Window, quiet_max: int) -> str:
    quiet = "quiet" if window.pipelines_created <= quiet_max else "compile"
    stats = " ".join(f"{name}={window.gpustats[name]:g}" for name in GPUSTATS_COLUMNS
                     if name in window.gpustats)
    return (f"{window.time:%H:%M:%S} {window.fps:6.1f} fps  longest {window.longest_ms:7.1f} ms  "
            f"pipelines {window.pipelines_created:3d} {quiet:7s}  "
            f"gpu {format_optional(window.mhz, 0)} MHz busy {format_optional(window.busy, 0)}% "
            f"pwr {window.pwrlevel} thermal {window.thermal} headroom {window.headroom}  "
            f"Mcycles/frame {format_optional(window.mcycles_per_frame())}  {stats}").rstrip()


def quiet_summary(windows: list[Window], quiet_max: int) -> str:
    quiet = [w for w in windows if w.pipelines_created <= quiet_max]
    if not quiet:
        return "no quiet windows"
    cycles = [c for c in (w.mcycles_per_frame() for w in quiet) if c is not None]
    cycles_text = f", median {statistics.median(cycles):.1f} Mcycles/frame" if cycles else ""
    return (f"{len(quiet)} quiet windows (<= {quiet_max} pipeline creations): median "
            f"{statistics.median(w.fps for w in quiet):.1f} fps{cycles_text}")


def read_lines(path: str) -> list[str]:
    with open(path, encoding="utf-8", errors="replace") as fh:
        return fh.read().splitlines()


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logs", nargs="+")
    parser.add_argument("--clocks", help="GPU clock samples taken over adb (see the docstring)")
    parser.add_argument("--quiet-max", type=int, default=4,
                        help="most pipeline creations a window may have and still count as quiet")
    args = parser.parse_args(argv)
    samples = read_clock_samples(read_lines(args.clocks)) if args.clocks else []
    for path in args.logs:
        windows, storage_loads = read_windows(read_lines(path))
        for window in windows:
            fill_clock_from_samples(window, samples)
        print(f"== {path}")
        print("pipelines from Vulkan storage: " +
              (", ".join(map(str, storage_loads)) if storage_loads else "none logged"))
        for window in windows:
            print(format_window(window, args.quiet_max))
        print(quiet_summary(windows, args.quiet_max))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
