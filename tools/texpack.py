#!/usr/bin/env python3
# Texture pack helper: inspect dumps, decode them to PNG and build replacements.
# The DDS layout mirrors src/graphics/d3d12/texture_replacement.cpp in
# sdk/patches/0004-texture-packs.patch: DX10 header, mip 0 only for dumps,
# tight mip pitches (BC: ((w + 3) // 4) * block bytes).
import argparse
import io
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

from PIL import Image

DDS_MAGIC = 0x20534444
DDS_FLAGS = 0x1 | 0x2 | 0x4 | 0x1000
DDS_FLAGS_MIPS = DDS_FLAGS | 0x20000
DDS_CAPS = 0x1000
DDS_CAPS_MIPS = DDS_CAPS | 0x8 | 0x400000
DDS_DIMENSION_TEXTURE2D = 3
DDS_MAX_DIMENSION = 16384
DDS_MAX_MIPS = 16

DXGI_R8G8B8A8_UNORM = 28
# sRGB file variants map to the same linear name, as in the runtime loader.
DXGI_NAMES = {
    28: "R8G8B8A8_UNORM", 29: "R8G8B8A8_UNORM",
    61: "R8_UNORM",
    49: "R8G8_UNORM",
    71: "BC1_UNORM", 72: "BC1_UNORM",
    74: "BC2_UNORM", 75: "BC2_UNORM",
    77: "BC3_UNORM", 78: "BC3_UNORM",
    80: "BC4_UNORM",
    83: "BC5_UNORM",
    87: "B8G8R8A8_UNORM", 91: "B8G8R8A8_UNORM",
    95: "BC6H_UF16", 96: "BC6H_SF16",
    98: "BC7_UNORM", 99: "BC7_UNORM",
}
LEGACY_FOURCC = {
    b"DXT1": "BC1_UNORM",
    b"DXT3": "BC2_UNORM",
    b"DXT5": "BC3_UNORM",
    b"ATI1": "BC4_UNORM", b"BC4U": "BC4_UNORM",
    b"ATI2": "BC5_UNORM", b"BC5U": "BC5_UNORM",
}
BLOCK_BYTES = {
    "BC1_UNORM": 8, "BC4_UNORM": 8,
    "BC2_UNORM": 16, "BC3_UNORM": 16, "BC5_UNORM": 16,
    "BC6H_UF16": 16, "BC6H_SF16": 16, "BC7_UNORM": 16,
}
TEXEL_BYTES = {
    "R8G8B8A8_UNORM": 4, "B8G8R8A8_UNORM": 4,
    "R8G8_UNORM": 2, "R8_UNORM": 1,
}
TEXCONV_NOTE = ("note: RGBA replacements are ~4x larger than compressed ones; "
                "for smaller packs compress with Microsoft texconv, e.g. "
                "`texconv -f BC7_UNORM -m 0 <file.dds>`.")


@dataclass
class Dds:
    format: str
    dxgi: int
    width: int
    height: int
    mips: list
    raw: bytes = field(repr=False, default=b"")


def _fourcc_int(tag: bytes) -> int:
    return struct.unpack("<I", tag)[0]


def level_pitch(fmt: str, width: int) -> int:
    # Tight row pitch, same as the runtime loader.
    if fmt in BLOCK_BYTES:
        return ((width + 3) // 4) * BLOCK_BYTES[fmt]
    return width * TEXEL_BYTES[fmt]


def level_rows(fmt: str, height: int) -> int:
    if fmt in BLOCK_BYTES:
        return (height + 3) // 4
    return height


def level_size(fmt: str, width: int, height: int) -> int:
    return level_pitch(fmt, width) * level_rows(fmt, height)


def read_dds(path) -> Dds:
    raw = Path(path).read_bytes()
    if len(raw) < 4 + 124:
        raise ValueError(f"{path}: truncated DDS header")
    (magic,) = struct.unpack_from("<I", raw, 0)
    if magic != DDS_MAGIC:
        raise ValueError(f"{path}: bad DDS magic")
    off = 4
    size, _flags, height, width, _pitch, _depth, mip_count = struct.unpack_from("<7I", raw, off)
    off += 28 + 44  # header fields above, then reserved1
    pf_size, pf_flags, fourcc, rgb_bits, r_mask, g_mask, b_mask, a_mask = \
        struct.unpack_from("<8I", raw, off)
    off += 32
    _caps, caps2, _c3, _c4, _r2 = struct.unpack_from("<5I", raw, off)
    off += 20
    if size != 124 or pf_size != 32:
        raise ValueError(f"{path}: bad DDS header size")
    if caps2 & (0x200 | 0x200000):
        raise ValueError(f"{path}: cube/volume maps are not supported")
    if not (1 <= width <= DDS_MAX_DIMENSION and 1 <= height <= DDS_MAX_DIMENSION):
        raise ValueError(f"{path}: bad dimensions {width}x{height}")
    tag = struct.pack("<I", fourcc)
    dxgi = 0
    swap_rb = False
    force_opaque = False
    if pf_flags & 0x4:
        if tag == b"DX10":
            if len(raw) < off + 20:
                raise ValueError(f"{path}: truncated DX10 header")
            dxgi, dim, _misc, array, _misc2 = struct.unpack_from("<5I", raw, off)
            off += 20
            if dim != DDS_DIMENSION_TEXTURE2D or array != 1:
                raise ValueError(f"{path}: only 2D single images are supported")
            fmt = DXGI_NAMES.get(dxgi)
            if fmt is None:
                raise ValueError(f"{path}: unsupported DXGI format {dxgi}")
        else:
            fmt = LEGACY_FOURCC.get(tag)
            if fmt is None:
                raise ValueError(f"{path}: unsupported FourCC {tag!r}")
    elif pf_flags & 0x40:
        # Uncompressed masks, same cases as the runtime (A8R8G8B8 needs a swap).
        if (rgb_bits == 32 and r_mask == 0x00FF0000 and g_mask == 0x0000FF00 and
                b_mask == 0x000000FF and a_mask in (0xFF000000, 0)):
            fmt, swap_rb, force_opaque = "R8G8B8A8_UNORM", True, a_mask == 0
        elif (rgb_bits == 32 and r_mask == 0x000000FF and g_mask == 0x0000FF00 and
                b_mask == 0x00FF0000 and a_mask == 0xFF000000):
            fmt = "R8G8B8A8_UNORM"
        elif rgb_bits == 16 and r_mask == 0x00FF and g_mask == 0xFF00:
            fmt = "R8G8_UNORM"
        elif rgb_bits == 8 and r_mask == 0xFF:
            fmt = "R8_UNORM"
        else:
            raise ValueError(f"{path}: unsupported RGB masks")
    else:
        raise ValueError(f"{path}: unsupported pixel format")
    levels = mip_count if mip_count != 0 else 1
    levels = min(levels, DDS_MAX_MIPS)
    mips = []
    for level in range(levels):
        w, h = max(width >> level, 1), max(height >> level, 1)
        size = level_size(fmt, w, h)
        if off + size > len(raw):
            break  # truncated file: keep the mips read so far
        mips.append(bytes(raw[off:off + size]))
        off += size
    if not mips:
        raise ValueError(f"{path}: truncated mip data")
    if swap_rb:
        swapped = []
        for mip in mips:
            data = bytearray(mip)
            for i in range(0, len(data), 4):
                data[i], data[i + 2] = data[i + 2], data[i]
                if force_opaque:
                    data[i + 3] = 0xFF
            swapped.append(bytes(data))
        mips = swapped
    return Dds(fmt, dxgi, width, height, mips, raw)


def write_dds(path, dxgi: int, width: int, height: int, mips) -> None:
    # Same DX10 header as the runtime dumper; mips add the mip-count flag/caps.
    fmt = DXGI_NAMES.get(dxgi)
    if fmt is None:
        raise ValueError(f"unsupported DXGI format {dxgi}")
    if not mips:
        raise ValueError("need at least one mip level")
    n = len(mips)
    flags = DDS_FLAGS_MIPS if n > 1 else DDS_FLAGS
    caps = DDS_CAPS_MIPS if n > 1 else DDS_CAPS
    pitch = level_pitch(fmt, width)
    header = struct.pack("<7I11I", 124, flags, height, width, pitch, 1, n, *([0] * 11))
    pixfmt = struct.pack("<8I", 32, 0x4, _fourcc_int(b"DX10"), 0, 0, 0, 0, 0)
    capsblk = struct.pack("<5I", caps, 0, 0, 0, 0)
    dx10 = struct.pack("<5I", dxgi, DDS_DIMENSION_TEXTURE2D, 0, 1, 0)
    Path(path).write_bytes(b"DDS " + header + pixfmt + capsblk + dx10 + b"".join(mips))


def rgba_mip_chain(img) -> list:
    # Full chain down to 1x1; the game requires at least as many mips as the
    # guest texture, and the dump does not record that count.
    chain = []
    w, h = img.size
    tw, th = w, h
    while True:
        level = img if (tw, th) == (w, h) else img.resize((tw, th), Image.LANCZOS)
        chain.append(level.tobytes())
        if tw == 1 and th == 1:
            return chain
        tw, th = max(tw // 2, 1), max(th // 2, 1)


def _expand565(value: int) -> tuple:
    return (((value >> 11) & 31) * 255 + 15) // 31, \
           (((value >> 5) & 63) * 255 + 31) // 63, \
           ((value & 31) * 255 + 15) // 31


def _bc1_block(blk: bytes, opaque: bool) -> bytes:
    c0, c1 = struct.unpack_from("<HH", blk, 0)
    p0 = (*_expand565(c0), 255)
    p1 = (*_expand565(c1), 255)
    if c0 > c1:
        pal = (p0, p1,
               ((2 * p0[0] + p1[0]) // 3, (2 * p0[1] + p1[1]) // 3,
                (2 * p0[2] + p1[2]) // 3, 255),
               ((p0[0] + 2 * p1[0]) // 3, (p0[1] + 2 * p1[1]) // 3,
                (p0[2] + 2 * p1[2]) // 3, 255))
    elif opaque:  # BC3 color part: 4 opaque colors even when c0 <= c1
        pal = (p0, p1,
               ((p0[0] + p1[0]) // 2, (p0[1] + p1[1]) // 2, (p0[2] + p1[2]) // 2, 255),
               ((p0[0] + 2 * p1[0]) // 3, (p0[1] + 2 * p1[1]) // 3,
                (p0[2] + 2 * p1[2]) // 3, 255))
    else:
        pal = (p0, p1,
               ((p0[0] + p1[0]) // 2, (p0[1] + p1[1]) // 2, (p0[2] + p1[2]) // 2, 255),
               (0, 0, 0, 0))
    idx = int.from_bytes(blk[4:8], "little")
    out = bytearray(64)
    for i in range(16):
        out[4 * i:4 * i + 4] = bytes(pal[(idx >> (2 * i)) & 3])
    return bytes(out)


def _bc_alpha_values(blk: bytes) -> list:
    # 8-byte alpha (BC3) or single-channel (BC4) block -> 16 values.
    a0, a1 = blk[0], blk[1]
    bits = int.from_bytes(blk[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((8 - i) * a0 + (i - 1) * a1) // 7 for i in range(2, 8)]
    else:
        pal = [a0, a1] + [((6 - i) * a0 + (i - 1) * a1) // 5 for i in range(2, 6)] + [0, 255]
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def _bc_block_to_rgba(fmt: str, blk: bytes) -> bytes:
    if fmt == "BC1_UNORM":
        return _bc1_block(blk, opaque=False)
    if fmt == "BC3_UNORM":
        alpha = _bc_alpha_values(blk[:8])
        color = _bc1_block(blk[8:], opaque=True)
        out = bytearray(64)
        for i in range(16):
            out[4 * i:4 * i + 3] = color[4 * i:4 * i + 3]
            out[4 * i + 3] = alpha[i]
        return bytes(out)
    if fmt == "BC4_UNORM":
        out = bytearray(64)
        for i, v in enumerate(_bc_alpha_values(blk[:8])):
            out[4 * i:4 * i + 4] = bytes((v, v, v, 255))
        return bytes(out)
    if fmt == "BC5_UNORM":
        out = bytearray(64)
        for i, (r, g) in enumerate(zip(_bc_alpha_values(blk[:8]),
                                       _bc_alpha_values(blk[8:]))):
            out[4 * i:4 * i + 4] = bytes((r, g, 0, 255))
        return bytes(out)
    raise ValueError(f"{fmt} has no pure-Python decoder")


def decode_block_mip(fmt: str, width: int, height: int, data: bytes) -> bytes:
    bb = BLOCK_BYTES[fmt]
    bw, bh = (width + 3) // 4, (height + 3) // 4
    if len(data) != bw * bh * bb:
        raise ValueError(f"{fmt} mip size mismatch for {width}x{height}")
    out = bytearray(width * height * 4)
    for by in range(bh):
        for bx in range(bw):
            tile = _bc_block_to_rgba(fmt, data[(by * bw + bx) * bb:(by * bw + bx + 1) * bb])
            for py in range(4):
                y = by * 4 + py
                if y >= height:
                    break
                for px in range(4):
                    x = bx * 4 + px
                    if x >= width:
                        break
                    o, t = (y * width + x) * 4, (py * 4 + px) * 4
                    out[o:o + 4] = tile[t:t + 4]
    return bytes(out)


def _pillow_mip0(dds: Dds) -> bytes:
    with Image.open(io.BytesIO(dds.raw)) as im:
        im.load()
        if im.size != (dds.width, dds.height):
            raise ValueError("Pillow decoded an unexpected size")
        return im.convert("RGBA").tobytes()


def decode_rgba(dds: Dds) -> bytes:
    # Mip 0 as RGBA bytes.
    fmt = dds.format
    if fmt == "R8G8B8A8_UNORM":
        return dds.mips[0]
    if fmt == "B8G8R8A8_UNORM":
        data = bytearray(dds.mips[0])
        for i in range(0, len(data), 4):
            data[i], data[i + 2] = data[i + 2], data[i]
        return bytes(data)
    if fmt in ("R8_UNORM", "R8G8_UNORM"):
        src = dds.mips[0]
        step = TEXEL_BYTES[fmt]
        out = bytearray(dds.width * dds.height * 4)
        for i in range(dds.width * dds.height):
            r = src[i * step]
            g = src[i * step + 1] if step == 2 else r
            out[4 * i:4 * i + 4] = bytes((r, g, 0 if step == 2 else r, 255))
        return bytes(out)
    if fmt in ("BC1_UNORM", "BC2_UNORM", "BC3_UNORM"):
        try:
            return _pillow_mip0(dds)
        except Exception:
            pass  # fall through to the decoders below
    if fmt in ("BC1_UNORM", "BC3_UNORM", "BC4_UNORM", "BC5_UNORM"):
        return decode_block_mip(fmt, dds.width, dds.height, dds.mips[0])
    if fmt == "BC2_UNORM":
        raise ValueError("BC2 (DXT3) explicit-alpha decoding is not implemented; "
                         "re-save the texture without explicit alpha or use from-png")
    raise ValueError(f"{fmt} decoding is not supported by this tool")


def cmd_info(path) -> Dds:
    dds = read_dds(path)
    print(f"format: {dds.format}")
    print(f"width: {dds.width}")
    print(f"height: {dds.height}")
    print(f"mips: {len(dds.mips)}")
    return dds


def convert_dds_to_png(src, dst) -> None:
    dds = read_dds(src)
    Image.frombytes("RGBA", (dds.width, dds.height), decode_rgba(dds)).save(dst)
    print(f"{src} -> {dst} ({dds.format} {dds.width}x{dds.height})")


def cmd_to_png(src, dst) -> None:
    s, d = Path(src), Path(dst)
    if s.is_dir():
        d.mkdir(parents=True, exist_ok=True)
        found = sorted(f for f in s.iterdir() if f.is_file() and f.suffix.lower() == ".dds")
        if not found:
            raise ValueError(f"no .dds files in {s}")
        for f in found:
            convert_dds_to_png(f, d / (f.stem + ".png"))
    else:
        if d.is_dir():
            d = d / (s.stem + ".png")
        d.parent.mkdir(parents=True, exist_ok=True)
        convert_dds_to_png(s, d)


def convert_png_to_dds(src, dst) -> None:
    with Image.open(src) as im:
        img = im.convert("RGBA")
    write_dds(dst, DXGI_R8G8B8A8_UNORM, img.width, img.height, rgba_mip_chain(img))
    print(f"{src} -> {dst} (R8G8B8A8_UNORM {img.width}x{img.height})")


def _replace_stem(stem: str) -> str:
    # Dumps carry a "<hash>_<tag>_<WxH>" suffix; replacements need the bare hash.
    if len(stem) >= 16 and all(c in "0123456789abcdefABCDEF" for c in stem[:16]):
        return stem[:16]
    return stem


def cmd_from_png(src, dst) -> None:
    s, d = Path(src), Path(dst)
    if s.is_dir():
        d.mkdir(parents=True, exist_ok=True)
        found = sorted(f for f in s.iterdir() if f.is_file() and f.suffix.lower() == ".png")
        if not found:
            raise ValueError(f"no .png files in {s}")
        for f in found:
            convert_png_to_dds(f, d / (_replace_stem(f.stem) + ".dds"))
    else:
        if d.is_dir():
            d = d / (_replace_stem(s.stem) + ".dds")
        d.parent.mkdir(parents=True, exist_ok=True)
        convert_png_to_dds(s, d)
    print(TEXCONV_NOTE)


def parse_color(text: str) -> tuple:
    s = text.strip().lstrip("#")
    if len(s) not in (6, 8) or any(c not in "0123456789abcdefABCDEF" for c in s):
        raise ValueError("--color must be hex like ff00ff or ff00ff80")
    alpha = int(s[6:8], 16) if len(s) == 8 else None
    return int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), alpha


def cmd_tint(src, dst, color: str) -> None:
    dds = read_dds(src)
    r, g, b, alpha_override = parse_color(color)
    n = dds.width * dds.height
    try:
        alpha = decode_rgba(dds)[3::4]  # keep the source alpha when decodable
    except ValueError:
        alpha = None
    if alpha_override is not None:
        alpha = bytes([alpha_override]) * n
    elif alpha is None or len(alpha) != n:
        alpha = bytes([255]) * n
    base = bytearray(n * 4)
    base[0::4] = bytes([r]) * n
    base[1::4] = bytes([g]) * n
    base[2::4] = bytes([b]) * n
    base[3::4] = alpha
    img = Image.frombytes("RGBA", (dds.width, dds.height), bytes(base))
    write_dds(dst, DXGI_R8G8B8A8_UNORM, dds.width, dds.height, rgba_mip_chain(img))
    print(f"{src} -> {dst} (tint {r:02x}{g:02x}{b:02x} {dds.width}x{dds.height})")


def parse_args(argv=None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Texture pack helper: inspect dumps and build replacements.")
    sub = p.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("info", help="print format, size and mip count of a DDS file")
    a.add_argument("dds")
    b = sub.add_parser("to-png", help="decode a dump (file or directory) to PNG")
    b.add_argument("src")
    b.add_argument("dst")
    c = sub.add_parser("from-png", help="build a replacement DDS with a full mip chain from PNG")
    c.add_argument("src")
    c.add_argument("dst")
    d = sub.add_parser("tint", help="solid-color replacement to test loading in game")
    d.add_argument("src")
    d.add_argument("dst")
    d.add_argument("--color", default="ff00ff")
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    try:
        if args.cmd == "info":
            cmd_info(args.dds)
        elif args.cmd == "to-png":
            cmd_to_png(args.src, args.dst)
        elif args.cmd == "from-png":
            cmd_from_png(args.src, args.dst)
        elif args.cmd == "tint":
            cmd_tint(args.src, args.dst, args.color)
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
