# Rebuilds docs/assets (the website's images and icons) from docs/screenshots and assets/icon.
# Run after render_icon.py or after a screenshot changes: python tools/site_assets.py
import shutil
import sys
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[1]
SHOT_WIDTHS = {
    "act1-4k-internal": (1600, 960),
    "act1-jacintomed": (1600, 800),
    "main-menu": (1200, 640),
    "title": (1200, 640),
    "scale-1x-vs-3x": (1416, 760),
    "texture-pack-test": (1280, 640),
    "pc-options": (1600, 800),
    "device-options": (1600, 800),
}
OG_CARDS = {"og-image-1200x630.png": "og-image.jpg", "og-image-es-1200x630.png": "og-image-es.jpg"}


def resized(img, width):
    if img.width <= width:
        return img
    height = round(img.height * width / img.width)
    return img.resize((width, height), Image.LANCZOS)


def write_webp(img, path, quality):
    img.save(path, "WEBP", quality=quality, method=6)
    return path


def build_screenshots(repo):
    shots = repo / "docs" / "screenshots"
    img_dir = repo / "docs" / "assets" / "img"
    for name, widths in SHOT_WIDTHS.items():
        src = Image.open(shots / f"{name}.jpg").convert("RGB")
        for width in widths:
            img = resized(src, width)
            write_webp(img, img_dir / f"{name}-{img.width}.webp", 80)


def build_icons(repo):
    icon = repo / "assets" / "icon"
    out = repo / "docs" / "assets"
    shutil.copyfile(icon / "favicon.ico", out / "favicon.ico")
    shutil.copyfile(icon / "favicon-180.png", out / "apple-touch-icon.png")
    shutil.copyfile(icon / "gow2-icon-flat.svg", out / "icon.svg")
    master = Image.open(icon / "gow2-icon-1024.png").convert("RGBA")
    write_webp(master.resize((320, 320), Image.LANCZOS), out / "img" / "icon-320.webp", 88)


def build_og_images(repo):
    for card, jpg in OG_CARDS.items():
        og = Image.open(repo / "assets" / "icon" / card).convert("RGB")
        og.save(repo / "docs" / "assets" / jpg, "JPEG", quality=86, optimize=True, progressive=True)


def list_outputs(repo):
    for f in sorted((repo / "docs" / "assets").rglob("*")):
        if f.is_file():
            print(f"{f.relative_to(repo).as_posix()}  {f.stat().st_size // 1024} KB")


def main(argv):
    repo = Path(argv[0]).resolve() if argv else REPO
    build_screenshots(repo)
    build_icons(repo)
    build_og_images(repo)
    list_outputs(repo)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
