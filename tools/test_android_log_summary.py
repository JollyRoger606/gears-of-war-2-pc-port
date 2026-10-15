# Tests for tools/android_log_summary.py. All inputs are synthetic log lines in the runtime's format.
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import android_log_summary as summary

FRAMES_OLD = ("[2026-10-15 00:29:22.029] [info] [core] [t23215] FRAMES 5.2 s: 26 frames, 5.0 fps, "
              "longest frame 618.4 ms")
FRAMES_NEW = ("[2026-10-16 10:00:05.000] [info] [core] [t1] FRAMES 5.0 s: 100 frames, 20.0 fps, "
              "longest frame 80.5 ms, gpu 700 MHz of 1200 MHz busy 98% pwrlevel 2 thermal 0 "
              "headroom 0.61")
FRAMES_NO_KGSL = ("[2026-10-16 10:00:10.000] [info] [core] [t1] FRAMES 5.0 s: 50 frames, 10.0 fps, "
                  "longest frame 120.0 ms, gpu n/a of n/a busy n/a pwrlevel n/a thermal 1 "
                  "headroom n/a")
GPUSTATS_LINE = ("[2026-10-16 10:00:04.900] [info] [gpu] [t9] GPUSTATS 5.0 s: swaps 101 unpresented 0 "
                 "| draws 4120 pending 12 no_ps 310 no_ps_color 280 gs 25 d24s8 3900 d24fs8 0 "
                 "missing_textures 7 | color_draw_zfunc never 0 less 0 equal 1200 lequal 2000 greater 0 "
                 "notequal 0 gequal 0 always 10 off 50 | passes 900 submits 45 drains 40 | occlusion "
                 "ends 38 host 36 zero 2 samples 44400 | pipeline_setup 120.5 ms slow 5 max 48.2 ms")
PIPELINE = "[2026-10-16 10:00:03.000] [info] [gpu] [t9] Creating graphics pipeline state with VS 1, PS 2"
STORAGE = ("[2026-10-16 09:59:00.000] [info] [gpu] [t9] Created 812 graphics pipelines from Vulkan "
           "storage (812 requested)")


def test_old_frames_line_has_no_clock():
    windows, _ = summary.read_windows([FRAMES_OLD])
    assert len(windows) == 1
    assert windows[0].fps == 5.0
    assert windows[0].mhz is None
    assert windows[0].mcycles_per_frame() is None


def test_new_frames_line_gives_megacycles_per_frame():
    windows, _ = summary.read_windows([FRAMES_NEW])
    window = windows[0]
    assert (window.mhz, window.busy, window.pwrlevel, window.thermal) == (700, 98, "2", "0")
    assert window.mcycles_per_frame() == pytest.approx(700 * 0.98 / 20.0)


def test_unreadable_kgsl_values_stay_unknown():
    windows, _ = summary.read_windows([FRAMES_NO_KGSL])
    assert windows[0].mhz is None
    assert windows[0].thermal == "1"


def test_pipelines_and_gpustats_attach_to_the_next_window():
    windows, storage = summary.read_windows([STORAGE, PIPELINE, PIPELINE, GPUSTATS_LINE, FRAMES_NEW,
                                             FRAMES_NO_KGSL])
    assert storage == [812]
    assert windows[0].pipelines_created == 2
    assert windows[0].gpustats["draws"] == 4120
    assert windows[0].gpustats["d24s8"] == 3900
    assert windows[0].gpustats["zero"] == 2
    assert windows[0].gpustats["pipeline_setup"] == 120.5
    assert windows[0].gpustats["missing_textures"] == 7
    assert windows[1].pipelines_created == 0
    assert windows[1].gpustats == {}


GPUSTATS_UPLOADS = ("[2026-10-16 10:00:04.900] [info] [gpu] [t9] GPUSTATS 5.1 s: swaps 23 unpresented 0 "
                    "| draws 70150 pending 0 | passes 36525 submits 23 drains 0 | pipeline_setup 16.0 ms "
                    "slow 0 max 0.0 ms | pass_ends barrier 36013 forced 0 switch 512 uploads 42649 "
                    "upload_kb 496348 | upload_kb_by index 40352 texture 30920 vertex 425076 memexport 0")


def test_upload_fields_are_parsed_and_printed():
    windows, _ = summary.read_windows([GPUSTATS_UPLOADS, FRAMES_NEW])
    stats = windows[0].gpustats
    assert (stats["uploads"], stats["upload_kb"], stats["barrier"]) == (42649, 496348, 36013)
    assert stats["vertex"] == 425076
    assert "uploads=42649 upload_kb=496348" in summary.format_window(windows[0], quiet_max=4)


def test_quiet_summary_uses_only_quiet_windows():
    windows, _ = summary.read_windows([PIPELINE] * 9 + [FRAMES_NEW, FRAMES_NO_KGSL])
    text = summary.quiet_summary(windows, quiet_max=4)
    assert text.startswith("1 quiet windows")
    assert "median 10.0 fps" in text


def test_adb_clock_samples_fill_a_window_the_app_could_not_read():
    windows, _ = summary.read_windows([FRAMES_NO_KGSL])
    samples = summary.read_clock_samples([
        "2026-10-16 10:00:04 160000000 99 13",
        "2026-10-16 10:00:06 222000000 99 % 14",
        "2026-10-16 10:00:08 222000000 97 14",
        "2026-10-16 10:00:11 734000000 99 2",
        "not a sample",
    ])
    summary.fill_clock_from_samples(windows[0], samples)
    # The window covers the five seconds up to 10:00:10: the samples at :06 and :08.
    assert windows[0].mhz == pytest.approx(222)
    assert windows[0].busy == pytest.approx(98)
    assert windows[0].pwrlevel == "14"


def test_app_clock_wins_over_adb_samples():
    windows, _ = summary.read_windows([FRAMES_NEW])
    samples = summary.read_clock_samples(["2026-10-16 10:00:04 160000000 99 13"])
    summary.fill_clock_from_samples(windows[0], samples)
    assert windows[0].mhz == 700
