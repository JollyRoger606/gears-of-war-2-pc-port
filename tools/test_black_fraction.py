# Tests for tools/black_fraction.py with synthetic screenshots in the phone's 3120x1440 layout.
import sys
from pathlib import Path

import pytest
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))

import black_fraction as bf

WIDTH, HEIGHT = 3120, 1440
GAME_LEFT, GAME_RIGHT = 280, 2840


def phone_shot(fill, black_box=None, black_value=0):
    image = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rectangle((GAME_LEFT, 0, GAME_RIGHT - 1, HEIGHT - 1), fill=fill)
    if black_box:
        draw.rectangle(black_box, fill=(black_value,) * 3)
    return image


def test_game_area_is_the_centred_16_by_9_picture():
    assert bf.game_area((WIDTH, HEIGHT)) == (GAME_LEFT, 0, GAME_RIGHT, HEIGHT)


def test_side_bars_do_not_count():
    fraction, _ = bf.flat_dark_fraction(phone_shot((120, 110, 100)), 64, 2)
    assert fraction == 0.0


def test_quarter_of_the_picture_black():
    box = (GAME_LEFT, 0, GAME_LEFT + 1279, 719)
    fraction, value = bf.flat_dark_fraction(phone_shot((120, 110, 100), box), 64, 2)
    assert value == 0
    assert fraction == pytest.approx(0.25)


def test_lifted_flat_grey_counts_as_the_black_region():
    box = (GAME_LEFT, 0, GAME_LEFT + 1279, 719)
    fraction, value = bf.flat_dark_fraction(phone_shot((120, 110, 100), box, 44), 64, 2)
    assert value == 44
    assert fraction == pytest.approx(0.25)
