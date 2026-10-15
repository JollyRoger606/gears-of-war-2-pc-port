"""Share of the game picture covered by flat black in an Android screenshot.

The phone shows the 16:9 game picture centred between black side bars; only that area is counted.
The black regions of the Act 1 gameplay frames are one flat dark value: pure black in most
screenshots, a flat grey (44) in others where the picture is lifted. So the most common value
among the dark ones (brightest channel below --dark-limit) is taken as the region's value, and
pixels within --tolerance of it are counted. Lit but dark surfaces are textured and add little.

Usage: python tools/black_fraction.py shot.png [more.png ...]
"""
import argparse
import sys

from PIL import Image, ImageChops

GAME_ASPECT = 16 / 9


def game_area(size: tuple[int, int]) -> tuple[int, int, int, int]:
    width, height = size
    game_width = min(width, round(height * GAME_ASPECT))
    left = (width - game_width) // 2
    return left, 0, left + game_width, height


def brightest_channel(image: Image.Image) -> Image.Image:
    red, green, blue = image.convert("RGB").split()
    return ImageChops.lighter(ImageChops.lighter(red, green), blue)


def flat_dark_value(histogram: list[int], dark_limit: int) -> int:
    dark = histogram[:dark_limit]
    return dark.index(max(dark))


def flat_dark_fraction(image: Image.Image, dark_limit: int, tolerance: int) -> tuple[float, int]:
    histogram = brightest_channel(image.crop(game_area(image.size))).histogram()
    value = flat_dark_value(histogram, dark_limit)
    covered = sum(histogram[max(0, value - tolerance): value + tolerance + 1])
    total = sum(histogram)
    return (covered / total if total else 0.0), value


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("screenshots", nargs="+")
    parser.add_argument("--dark-limit", type=int, default=64,
                        help="values below this one are candidates for the flat black (0-255)")
    parser.add_argument("--tolerance", type=int, default=2,
                        help="how far from the flat value a pixel may be and still count")
    args = parser.parse_args(argv)
    for path in args.screenshots:
        with Image.open(path) as image:
            fraction, value = flat_dark_fraction(image, args.dark_limit, args.tolerance)
        print(f"{fraction:6.1%} flat at {value:3d}  {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
