from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from icon_mark import (
    HOT_TOP,
    LAYER_BACKGROUND,
    LAYER_DP,
    SAFE_RADIUS,
    VISIBLE_DP,
    adaptive_monochrome,
    icon_at,
    legacy_icon,
    mark_reach,
    on_color,
    shape_mask,
)

REPO_DIR = Path(__file__).resolve().parents[2]
SCREENSHOT = REPO_DIR / "docs" / "screenshots" / "act1-4k-internal.jpg"
FONT_PATH = Path(r"C:\Windows\Fonts\bahnschrift.ttf")
OG_SIZE = (1200, 630)
OG_TEXT = {
    "en": {
        "tag": "UNOFFICIAL FAN PORT",
        "subtitle": "— native PC port",
        "body": ("Static recompilation to native x64 code.", "Not an emulator.", "Bring your own copy of the Xbox 360 game."),
        "notice": "Not affiliated with or endorsed by Microsoft, Xbox Game Studios, The Coalition or Epic Games.",
    },
    "es": {
        "tag": "PORT NO OFICIAL",
        "subtitle": "— port nativo para PC",
        "body": ("Recompilación estática a código nativo x64.", "No es un emulador.", "Usa tu propia copia del juego de Xbox 360."),
        "notice": "Sin afiliación ni respaldo de Microsoft, Xbox Game Studios, The Coalition o Epic Games.",
    },
}
OG_NOTICE_BAND = 64
OG_NOTICE_SIZES = (24, 23, 22)


# ---- Open Graph image ----

def font(size, variation="Regular"):
    if not FONT_PATH.exists():
        raise SystemExit(f"font not found: {FONT_PATH} (Bahnschrift ships with Windows 10/11)")
    face = ImageFont.truetype(str(FONT_PATH), size)
    face.set_variation_by_name(variation)
    return face


def horizontal_ramp(width, height, start, end):
    xs = np.clip((np.arange(width, dtype=np.float32) - start) / (end - start), 0.0, 1.0)
    ramp = xs * xs * (3 - 2 * xs)
    return Image.fromarray((np.broadcast_to(ramp, (height, width)) * 255).astype(np.uint8), "L")


def screenshot_panel(width, height):
    shot = Image.open(SCREENSHOT).convert("RGB")
    crop_w = round(shot.height * width / height)
    left = min(max(0, 990 - crop_w // 2), shot.width - crop_w)
    return shot.crop((left, 0, left + crop_w, shot.height)).resize((width, height), Image.LANCZOS)


def draw_tag(draw, xy, text):
    face = font(28, "SemiBold SemiCondensed")
    x, y = xy
    width = face.getlength(text) + 36
    draw.rounded_rectangle([x, y, x + width, y + 44], radius=22, outline=HOT_TOP, width=3)
    draw.text((x + 18, y + 22), text, font=face, fill=(255, 122, 92), anchor="lm")


def og_text(draw, left, text):
    draw_tag(draw, (left, 204), text["tag"])
    draw.text((left, 260), "Gears of War 2", font=font(66, "SemiBold"), fill=(238, 240, 242))
    draw.text((left, 334), text["subtitle"], font=font(42, "Light"), fill=(178, 184, 190))
    y = 406
    for line in text["body"]:
        draw.text((left, y), line, font=font(21), fill=(150, 157, 164))
        y += 29
    draw.text((left, y + 12), "github.com/santiquiroz/gears-of-war-2-pc-port", font=font(19, "SemiBold"), fill=(214, 218, 222))


def notice_font(notice, max_width):
    for size in OG_NOTICE_SIZES:
        face = font(size)
        if face.getlength(notice) <= max_width:
            return face
    raise SystemExit(f"social card notice does not fit in {max_width} px at {OG_NOTICE_SIZES[-1]} px: {notice}")


# The notice stays clear of the bottom 15 px, which a feed may crop when it fits 1.91:1 into 2:1.
def og_footer(image, notice):
    w, h = image.size
    band = Image.new("RGBA", (w, OG_NOTICE_BAND), (10, 11, 13, 235))
    image.alpha_composite(band, (0, h - OG_NOTICE_BAND))
    face = notice_font(notice, w - 2 * 64)
    ImageDraw.Draw(image).text((64, h - 38), notice, font=face, fill=(169, 175, 182), anchor="lm")


def og_image(master, lang="en"):
    text = OG_TEXT[lang]
    w, h = OG_SIZE
    image = Image.new("RGBA", OG_SIZE, (20, 20, 22, 255))
    panel_x = 470
    panel = screenshot_panel(w - panel_x, h).convert("RGBA")
    image.paste(panel, (panel_x, 0), horizontal_ramp(w - panel_x, h, 0, 230))
    image.alpha_composite(master.resize((150, 150), Image.LANCZOS), (55, 36))
    og_text(ImageDraw.Draw(image), 64, text)
    og_footer(image, text["notice"])
    return image.convert("RGB")


# ---- preview sheet (not committed) ----

def magnified(image, color, factor):
    return on_color(image, color).resize((image.width * factor, image.height * factor), Image.NEAREST)


def size_row(renders, color, text_color, width):
    row = Image.new("RGBA", (width, 330), color)
    draw = ImageDraw.Draw(row)
    x = 30
    for size in (256, 128, 64, 48, 40, 32, 24, 20, 16):
        row.alpha_composite(renders[size], (x, 160 - size // 2))
        draw.text((x, 300), f"{size}", font=font(15), fill=text_color)
        x += size + 26
    for size, factor in ((48, 4), (32, 6), (16, 10)):
        zoom = magnified(renders[size], color[:3], factor)
        row.alpha_composite(zoom, (x, 160 - zoom.height // 2))
        draw.text((x, 300), f"{size} x{factor}", font=font(15), fill=text_color)
        x += zoom.width + 26
    return row


def squircle(size):
    ys, xs = np.mgrid[0:size, 0:size].astype(np.float32)
    u, v = (xs + 0.5) / size * 2 - 1, (ys + 0.5) / size * 2 - 1
    return Image.fromarray(((np.abs(u) ** 4 + np.abs(v) ** 4 <= 1) * 255).astype(np.uint8), "L")


def teardrop(size):
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    draw.ellipse([0, 0, size - 1, size - 1], fill=255)
    draw.rounded_rectangle([size // 2, 0, size - 1, size // 2], radius=size * 0.09, fill=255, corners=(False, True, False, False))
    return mask


def launcher_mask(size, kind):
    big = size * 4
    if kind == "circle":
        mask = shape_mask(big, big, "round")
    elif kind == "squircle":
        mask = squircle(big)
    else:
        mask = teardrop(big)
    return mask.resize((size, size), Image.LANCZOS)


def launcher_tile(layer, kind, size=176):
    crop = round(layer.width * (LAYER_DP - VISIBLE_DP) / LAYER_DP / 2)
    visible = layer.crop((crop, crop, layer.width - crop, layer.height - crop)).resize((size, size), Image.LANCZOS)
    tile = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    tile.paste(visible, (0, 0), launcher_mask(size, kind))
    return tile


def tinted(mono, ink, paper):
    alpha = mono.getchannel("A")
    layer = Image.new("RGBA", mono.size, paper + (255,))
    layer.paste(Image.new("RGBA", mono.size, ink + (255,)), (0, 0), alpha)
    return layer


def safe_guide(image):
    guide = image.copy()
    draw = ImageDraw.Draw(guide)
    c, r = image.width / 2, image.width * SAFE_RADIUS
    draw.ellipse([c - r, c - r, c + r, c + r], outline=(90, 200, 255, 255), width=1)
    v = image.width * VISIBLE_DP / LAYER_DP / 2
    draw.rectangle([c - v, c - v, c + v, c + v], outline=(120, 120, 120, 255), width=1)
    return guide


def android_row(foreground, monochrome, width):
    row = Image.new("RGBA", (width, 270), (34, 36, 38, 255))
    draw = ImageDraw.Draw(row)
    row.alpha_composite(safe_guide(on_color(foreground, (60, 60, 60)).resize((216, 216), Image.LANCZOS)), (30, 20))
    draw.text((30, 244), f"foreground, reach {mark_reach(foreground):.3f} (safe {SAFE_RADIUS:.4f})", font=font(14), fill=(200, 204, 208))
    full = on_color(foreground, LAYER_BACKGROUND)
    x = 290
    for kind in ("circle", "squircle", "teardrop"):
        row.alpha_composite(launcher_tile(full, kind), (x, 40))
        draw.text((x, 244), kind, font=font(14), fill=(200, 204, 208))
        x += 200
    row.alpha_composite(launcher_tile(tinted(monochrome, (31, 58, 95), (214, 227, 247)), "circle"), (x, 40))
    draw.text((x, 244), "themed (monochrome)", font=font(14), fill=(200, 204, 208))
    x += 200
    for kind in ("square", "round"):
        row.alpha_composite(legacy_icon(144, kind), (x, 56))
        draw.text((x, 244), f"legacy {kind}", font=font(14), fill=(200, 204, 208))
        x += 170
    return row


def preview_sheet(renders, master, foreground, monochrome):
    width = 1700
    rows = [size_row(renders, (17, 17, 17, 255), (170, 170, 170), width),
            size_row(renders, (238, 238, 238, 255), (70, 70, 70), width),
            android_row(foreground, monochrome, width)]
    sheet = Image.new("RGBA", (width, sum(r.height for r in rows)), (0, 0, 0, 255))
    y = 0
    for row in rows:
        sheet.alpha_composite(row, (0, y))
        y += row.height
    return sheet.convert("RGB")


def write_preview(folder, renders, master, foreground):
    folder.mkdir(parents=True, exist_ok=True)
    renders = {**renders, **{s: icon_at(s) for s in (256, 128, 64)}}
    preview_sheet(renders, master, foreground, adaptive_monochrome(432)).save(folder / "preview.png")
    on_color(master, (17, 17, 17)).crop((200, 120, 824, 744)).save(folder / "master_detail.png")
