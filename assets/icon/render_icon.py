import argparse
import sys
from pathlib import Path

from icon_mark import (
    DESIGN,
    LAYER_BACKGROUND,
    LAYER_DP,
    LEGACY_DP,
    adaptive_foreground,
    adaptive_monochrome,
    check_layout,
    flat_svg,
    full_bleed_frame,
    icon_at,
    legacy_icon,
    mark_at,
    mono_svg,
    on_color,
    render,
    spec_for,
)
from icon_sheets import og_image, write_preview

ICON_DIR = Path(__file__).resolve().parent
REPO_DIR = ICON_DIR.parents[1]
ANDROID_DIR = ICON_DIR / "android"

MASTER_SIZE = 1024
ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
FAVICON_SIZES = (16, 32, 48)
APPLE_TOUCH_SIZE = 180
APPLE_TOUCH_REACH = 0.43
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
PLAYSTORE_SIZE = 512


def save(image, path, written, **options):
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, **options)
    written.append(path)
    return image


def save_text(text, path, written):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    written.append(path)


def save_ico(renders, sizes, path, written):
    largest = renders[max(sizes)]
    largest.save(path, sizes=[(s, s) for s in sizes], append_images=[renders[s] for s in sizes if s != max(sizes)])
    written.append(path)


def write_desktop(renders, written):
    frame = full_bleed_frame(DESIGN, MASTER_SIZE)
    master = save(render(spec_for(DESIGN, frame), frame), ICON_DIR / "gow2-icon-1024.png", written)
    save_ico(renders, ICO_SIZES, ICON_DIR / "gow2.ico", written)
    save_ico(renders, FAVICON_SIZES, ICON_DIR / "favicon.ico", written)
    save(renders[32], ICON_DIR / "favicon-32.png", written)
    apple = on_color(mark_at(APPLE_TOUCH_SIZE, APPLE_TOUCH_REACH * APPLE_TOUCH_SIZE), LAYER_BACKGROUND)
    save(apple.convert("RGB"), ICON_DIR / "favicon-180.png", written)
    return master


ADAPTIVE_XML = """<?xml version="1.0" encoding="utf-8"?>
<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/ic_launcher_background" />
    <foreground android:drawable="@mipmap/ic_launcher_foreground" />
    <monochrome android:drawable="@mipmap/ic_launcher_monochrome" />
</adaptive-icon>
"""


def write_density(name, factor, written):
    folder = ANDROID_DIR / f"mipmap-{name}"
    layer = round(LAYER_DP * factor)
    legacy = round(LEGACY_DP * factor)
    save(adaptive_foreground(layer), folder / "ic_launcher_foreground.png", written)
    save(adaptive_monochrome(layer), folder / "ic_launcher_monochrome.png", written)
    save(legacy_icon(legacy, "square"), folder / "ic_launcher.png", written)
    save(legacy_icon(legacy, "round"), folder / "ic_launcher_round.png", written)


def write_android(written):
    for name, factor in DENSITIES.items():
        write_density(name, factor, written)
    for name in ("ic_launcher.xml", "ic_launcher_round.xml"):
        save_text(ADAPTIVE_XML, ANDROID_DIR / "mipmap-anydpi" / name, written)
    foreground = save(adaptive_foreground(432), ANDROID_DIR / "ic_launcher_foreground_432.png", written)
    save(adaptive_monochrome(432), ANDROID_DIR / "ic_launcher_monochrome_432.png", written)
    store = on_color(adaptive_foreground(PLAYSTORE_SIZE), LAYER_BACKGROUND).convert("RGB")
    save(store, ANDROID_DIR / "ic_launcher_playstore_512.png", written)
    return foreground


def write_vectors(written):
    save_text(flat_svg(DESIGN), ICON_DIR / "gow2-icon-flat.svg", written)
    save_text(mono_svg(DESIGN), ICON_DIR / "gow2-icon-mono.svg", written)


def write_og(master, written):
    save(og_image(master, "en"), ICON_DIR / "og-image-1200x630.png", written, optimize=True)
    save(og_image(master, "es"), ICON_DIR / "og-image-es-1200x630.png", written, optimize=True)


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Render every icon file in assets/icon from scratch.")
    parser.add_argument("--preview", type=Path, help="also write review sheets to this folder (not committed)")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    check_layout()
    written = []
    renders = {size: icon_at(size) for size in ICO_SIZES}
    master = write_desktop(renders, written)
    foreground = write_android(written)
    write_vectors(written)
    write_og(master, written)
    if args.preview:
        write_preview(args.preview, renders, master, foreground)
    for path in written:
        print(path.relative_to(REPO_DIR).as_posix())
    return 0


if __name__ == "__main__":
    sys.exit(main())
