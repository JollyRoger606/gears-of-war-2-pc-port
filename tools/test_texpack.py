# Tests for tools/texpack.py. All inputs are generated; no game files needed.
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import texpack
from PIL import Image


def _gradient_rgba(width, height):
    data = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            o = (y * width + x) * 4
            data[o:o + 4] = bytes((x * 255 // max(width - 1, 1),
                                   y * 255 // max(height - 1, 1),
                                   (x + y) * 255 // max(width + height - 2, 1), 255))
    return bytes(data)


def test_from_png_info_to_png_roundtrip(tmp_path, capsys):
    src = tmp_path / "upscaled.png"
    Image.frombytes("RGBA", (64, 32), _gradient_rgba(64, 32)).save(src)
    dds = tmp_path / "0123456789abcdef.dds"
    assert texpack.main(["from-png", str(src), str(dds)]) == 0
    info = texpack.read_dds(dds)
    assert info.format == "R8G8B8A8_UNORM"
    assert (info.width, info.height) == (64, 32)
    assert len(info.mips) == 7  # 64x32 down to 1x1
    assert texpack.main(["info", str(dds)]) == 0
    out = capsys.readouterr().out
    assert "mips: 7" in out and "width: 64" in out and "height: 32" in out
    assert "R8G8B8A8_UNORM" in out
    back = tmp_path / "back.png"
    assert texpack.main(["to-png", str(dds), str(back)]) == 0
    with Image.open(back) as im:
        assert im.size == (64, 32)
        assert im.convert("RGBA").tobytes() == _gradient_rgba(64, 32)


def _bc1_block_bytes(c0, c1, indices):
    return struct.pack("<HHI", c0, c1, indices)


def test_bc1_decode_expected_colors(tmp_path):
    # 8x4: opaque red block, then a transparent-mode block that is all clear.
    red = _bc1_block_bytes(0xF800, 0x07E0, 0x00000000)
    clear = _bc1_block_bytes(0x07E0, 0xF800, 0xFFFFFFFF)
    dds = tmp_path / "bc1.dds"
    texpack.write_dds(dds, 71, 8, 4, [red + clear])
    # The hand decoder pins the exact palette math, whatever Pillow supports.
    tile = texpack.decode_block_mip("BC1_UNORM", 4, 4,
                                    _bc1_block_bytes(0xF800, 0x07E0, 0x00000002))
    assert tile[0:4] == bytes((170, 85, 0, 255))  # (2*red + green) / 3
    assert tile[4:8] == bytes((255, 0, 0, 255))
    out = tmp_path / "bc1.png"
    assert texpack.main(["to-png", str(dds), str(out)]) == 0
    with Image.open(out) as im:
        px = im.convert("RGBA")
        assert px.getpixel((0, 0)) == (255, 0, 0, 255)
        assert px.getpixel((3, 3)) == (255, 0, 0, 255)
        assert px.getpixel((4, 0)) == (0, 0, 0, 0)
        assert px.getpixel((7, 3)) == (0, 0, 0, 0)


def test_tint_full_chain_size_and_color(tmp_path):
    src = tmp_path / "src.png"
    Image.frombytes("RGBA", (16, 8), bytes((10, 20, 30, 128)) * (16 * 8)).save(src)
    dump = tmp_path / "abcdef0123456789.dds"
    assert texpack.main(["from-png", str(src), str(dump)]) == 0
    tinted = tmp_path / "tinted.dds"
    assert texpack.main(["tint", str(dump), str(tinted), "--color", "00ff00"]) == 0
    info = texpack.read_dds(tinted)
    assert info.format == "R8G8B8A8_UNORM"
    assert (info.width, info.height) == (16, 8)
    assert len(info.mips) == 5  # 16x8 down to 1x1
    w, h = 16, 8
    for mip in info.mips:
        assert len(mip) == w * h * 4
        assert mip[0::4] == bytes([0]) * (w * h)
        assert mip[1::4] == bytes([255]) * (w * h)
        assert mip[2::4] == bytes([0]) * (w * h)
        # Constant source alpha survives the Lanczos chain unchanged.
        assert set(mip[3::4]) <= {127, 128, 129}
        w, h = max(w // 2, 1), max(h // 2, 1)
