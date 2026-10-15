import math
from dataclasses import dataclass, replace

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# Android adaptive icon layer: 108 dp wide, 72 dp visible, 66 dp safe circle.
LAYER_DP, VISIBLE_DP, SAFE_DP = 108, 72, 66
SAFE_RADIUS = SAFE_DP / 2 / LAYER_DP
MARK_REACH = 0.30
LEGACY_DP, LEGACY_SHAPE_DP = 48, 46

MIN_CLEARANCE = 2.5
STENCIL_MIN_CUT_PX = 2.0
# Pixels per design unit: wear starts on a ~128 px icon and is at full strength from ~512 px.
WEAR_MIN_SCALE = 1.0
WEAR_FULL_SCALE = 4.0
WEAR_SEED = 2008
CHIPS_PER_PART = 3
SCRATCHES = 14
SPRAY_DENSITY = 0.2
RIDGE_ALPHA = 0.4
CHIP_ALPHA = 0.7

OUTLINE = (10, 12, 14)
STEEL_LIT = (232, 236, 239)
STEEL_MID = (150, 158, 165)
STEEL_SHADE = (86, 94, 101)
SEAM = (14, 16, 18)
FIELD_TOP = (54, 60, 66)
FIELD_BOTTOM = (23, 26, 30)
FIELD_FLAT = (38, 43, 48)
HOT_TOP = (255, 78, 40)
HOT_BOTTOM = (206, 24, 20)
HOT_FLAT = (232, 52, 30)
HOT_RIDGE = (255, 150, 112)
CHIP_METAL = (80, 86, 92)
RIVET_TOP = (240, 243, 245)
RIVET_BOTTOM = (122, 131, 138)
LAYER_BACKGROUND = (27, 29, 33)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
LIGHT = (-0.45, -0.893)


@dataclass(frozen=True)
class Spec:
    width: float
    chamfer: float
    notch: float
    shoulder: float
    apex: float
    outline: float
    rim: float
    seam: float
    ii_top: float
    ii_bottom: float
    serif_w: float
    serif_h: float
    stem: float
    gap: float
    cut: float
    serif_chamfer: float
    rivet: float
    shadow: float
    overspray: float
    grain: float
    wear: float
    bevel: str


@dataclass(frozen=True)
class Frame:
    size: int
    scale: float
    ox: float
    oy: float


DESIGN = Spec(
    width=100, chamfer=9, notch=9, shoulder=92, apex=120,
    outline=2.6, rim=7.0, seam=1.3,
    ii_top=22, ii_bottom=86, serif_w=31, serif_h=10, stem=16, gap=8, cut=2.6, serif_chamfer=2.2,
    rivet=2.4, shadow=3.2, overspray=1.1, grain=0.05, wear=1.0, bevel="split",
)


def pixel_spec(**overrides):
    flat = dict(seam=0, cut=0, serif_chamfer=0, rivet=0, shadow=0, overspray=0, grain=0, wear=0, bevel="flat")
    return replace(DESIGN, **{**flat, **overrides})


# Pixel-exact layouts for small sizes: units are pixels, so stems, serifs and gaps land on whole pixels.
# The stencil cuts stay closed here; a 1 px cut reads as a rendering fault, not as a stencil.
HINTED = {
    16: (0, pixel_spec(width=16, chamfer=2, notch=0, shoulder=13, apex=16, outline=1, rim=1,
                       ii_top=3, ii_bottom=11, serif_w=4, serif_h=1, stem=2, gap=2)),
    20: (1, pixel_spec(width=18, chamfer=2, notch=0, shoulder=15, apex=20, outline=1, rim=1,
                       ii_top=3, ii_bottom=13, serif_w=5, serif_h=1, stem=3, gap=2)),
    24: (1, pixel_spec(width=22, chamfer=2, notch=0, shoulder=18, apex=24, outline=1, rim=1,
                       ii_top=3, ii_bottom=16, serif_w=7, serif_h=2, stem=3, gap=2)),
    32: (2, pixel_spec(width=28, chamfer=3, notch=2, shoulder=25, apex=32, outline=1, rim=2,
                       ii_top=6, ii_bottom=22, serif_w=9, serif_h=2, stem=5, gap=2, shadow=1)),
    40: (3, pixel_spec(width=34, chamfer=3, notch=3, shoulder=31, apex=40, outline=1, rim=3,
                       ii_top=8, ii_bottom=28, serif_w=11, serif_h=3, stem=5, gap=2, shadow=1)),
    48: (4, pixel_spec(width=40, chamfer=4, notch=4, shoulder=37, apex=48, outline=1, rim=3, seam=1,
                       ii_top=10, ii_bottom=34, serif_w=12, serif_h=4, stem=6, gap=2, serif_chamfer=1,
                       shadow=1)),
}


# ---- geometry (design units) ----

def shield_outline(spec):
    w, c, cx = spec.width, spec.chamfer, spec.width / 2
    return [(0, c), (c, 0), (cx, spec.notch), (w - c, 0), (w, c), (w, spec.shoulder), (cx, spec.apex), (0, spec.shoulder)]


def edges(points):
    return [(points[i], points[(i + 1) % len(points)]) for i in range(len(points))]


def signed_area(points):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1] for a, b in edges(points))


def orientation_of(points):
    return 1.0 if signed_area(points) > 0 else -1.0


def inward_normal(a, b, orientation):
    ex, ey = b[0] - a[0], b[1] - a[1]
    length = math.hypot(ex, ey)
    return (-ey / length * orientation, ex / length * orientation)


def offset_line(a, b, orientation, d):
    nx, ny = inward_normal(a, b, orientation)
    return (a[0] + nx * d, a[1] + ny * d), (b[0] - a[0], b[1] - a[1])


def intersect(line_a, line_b):
    (p, d), (q, e) = line_a, line_b
    cross = d[0] * e[1] - d[1] * e[0]
    if abs(cross) < 1e-9:
        return q
    t = ((q[0] - p[0]) * e[1] - (q[1] - p[1]) * e[0]) / cross
    return (p[0] + t * d[0], p[1] + t * d[1])


def inset(points, d):
    orientation = orientation_of(points)
    lines = [offset_line(a, b, orientation, d) for a, b in edges(points)]
    return [intersect(lines[i - 1], lines[i]) for i in range(len(lines))]


def mirror(points, cx):
    return [(2 * cx - x, y) for x, y in reversed(points)]


def midpoint(a, b):
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)


def top_serif(x0, x1, top, height, c):
    if c <= 0:
        return [(x0, top), (x1, top), (x1, top + height), (x0, top + height)]
    return [(x0, top + c), (x0 + c, top), (x1 - c, top), (x1, top + c), (x1, top + height), (x0, top + height)]


def bottom_serif(x0, x1, bottom, height, c):
    y0 = bottom - height
    if c <= 0:
        return [(x0, y0), (x1, y0), (x1, bottom), (x0, bottom)]
    return [(x0, y0), (x1, y0), (x1, bottom - c), (x1 - c, bottom), (x0 + c, bottom), (x0, bottom - c)]


def stem(xc, half, top, bottom):
    return [(xc - half, top), (xc + half, top), (xc + half, bottom), (xc - half, bottom)]


def left_numeral(spec):
    x1 = spec.width / 2 - spec.gap / 2
    x0 = x1 - spec.serif_w
    stem_top = spec.ii_top + spec.serif_h + spec.cut
    stem_bottom = spec.ii_bottom - spec.serif_h - spec.cut
    return [
        top_serif(x0, x1, spec.ii_top, spec.serif_h, spec.serif_chamfer),
        stem((x0 + x1) / 2, spec.stem / 2, stem_top, stem_bottom),
        bottom_serif(x0, x1, spec.ii_bottom, spec.serif_h, spec.serif_chamfer),
    ]


def numeral_parts(spec):
    left = left_numeral(spec)
    return left + [mirror(part, spec.width / 2) for part in left]


def rivet_centers(spec):
    ring = inset(shield_outline(spec), spec.outline + spec.rim / 2)
    return [midpoint(ring[0], ring[1]), midpoint(ring[3], ring[4]), ring[5], ring[7], ring[6]]


def field_polygon(spec):
    return inset(shield_outline(spec), spec.outline + spec.rim + spec.seam)


def point_on_edges(points, rng):
    sides = edges(points)
    lengths = np.array([math.dist(a, b) for a, b in sides])
    a, b = sides[rng.choice(len(sides), p=lengths / lengths.sum())]
    t = rng.uniform(0.12, 0.88)
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t), inward_normal(a, b, orientation_of(points)), (a, b)


def chip(part, rng):
    (px, py), (nx, ny), _ = point_on_edges(part, rng)
    depth, radius, corners = rng.uniform(0.0, 0.5), rng.uniform(0.8, 1.9), int(rng.integers(5, 9))
    cx, cy = px + nx * depth, py + ny * depth
    angles = np.sort(rng.uniform(0, 2 * math.pi, corners))
    radii = radius * rng.uniform(0.55, 1.0, corners)
    return [(cx + r * math.cos(t), cy + r * math.sin(t)) for t, r in zip(angles, radii)]


def chip_polygons(spec):
    rng = np.random.default_rng(WEAR_SEED)
    return [chip(part, rng) for part in numeral_parts(spec) for _ in range(CHIPS_PER_PART)]


def scratch(ring, rng):
    (px, py), _, (a, b) = point_on_edges(ring, rng)
    heading = math.atan2(b[1] - a[1], b[0] - a[0]) + rng.uniform(-0.3, 0.3)
    half, width = rng.uniform(1.2, 3.4), rng.uniform(0.14, 0.24)
    dx, dy = math.cos(heading), math.sin(heading)
    nx, ny = -dy * width / 2, dx * width / 2
    p0, p1 = (px - dx * half, py - dy * half), (px + dx * half, py + dy * half)
    return [(p0[0] + nx, p0[1] + ny), (p1[0] + nx, p1[1] + ny), (p1[0] - nx, p1[1] - ny), (p0[0] - nx, p0[1] - ny)]


def scratch_quads(spec):
    rng = np.random.default_rng(WEAR_SEED + 1)
    ring = inset(shield_outline(spec), spec.outline + spec.rim * rng.uniform(0.3, 0.7))
    return [scratch(ring, rng) for _ in range(SCRATCHES)]


# ---- layout checks ----

def point_segment_distance(p, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
    return math.hypot(p[0] - a[0] - t * dx, p[1] - a[1] - t * dy)


def contains(points, p):
    inside = False
    for a, b in edges(points):
        if (a[1] > p[1]) != (b[1] > p[1]) and p[0] < a[0] + (p[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1]):
            inside = not inside
    return inside


def clearance(part, field):
    if not all(contains(field, p) for p in part) or any(contains(part, q) for q in field):
        return -1.0
    to_field = min(point_segment_distance(p, a, b) for p in part for a, b in edges(field))
    to_part = min(point_segment_distance(q, a, b) for q in field for a, b in edges(part))
    return min(to_field, to_part)


def numeral_touches_rim(spec, frame):
    letters = Image.fromarray((coverage(frame, numeral_parts(spec)) * 255).astype(np.uint8), "L")
    grown = np.asarray(letters.filter(ImageFilter.MaxFilter(3)), dtype=np.float32) / 255.0
    outside = 1.0 - coverage(frame, [field_polygon(spec)])
    return float((grown * outside).max()) > 0.5


def check_layout():
    field = field_polygon(DESIGN)
    worst = min(clearance(part, field) for part in numeral_parts(DESIGN))
    if worst < MIN_CLEARANCE:
        raise SystemExit(f"numerals are {worst:.2f} units from the rim, need {MIN_CLEARANCE}")
    for size, (offset, spec) in HINTED.items():
        if numeral_touches_rim(spec, hinted_frame(size, offset)):
            raise SystemExit(f"{size}px numeral has no 1px clearance from the rim")


def mark_reach(image):
    ys, xs = np.nonzero(np.asarray(image.getchannel("A")))
    centre = image.width / 2
    return float(np.hypot(xs + 0.5 - centre, ys + 0.5 - centre).max()) / image.width


def checked_safe(image, label):
    reach = mark_reach(image)
    if reach > SAFE_RADIUS:
        raise SystemExit(f"{label}: mark reaches {reach:.4f} of the width, outside the 66 dp safe circle ({SAFE_RADIUS:.4f})")
    return image


# ---- rasterising ----

def to_pixels(points, frame):
    return [(frame.ox + x * frame.scale, frame.oy + y * frame.scale) for x, y in points]


def supersampling(size):
    return max(4, 1024 // size)


def coverage(frame, polygons=(), circles=()):
    ss = supersampling(frame.size)
    big = Image.new("L", (frame.size * ss, frame.size * ss), 0)
    draw = ImageDraw.Draw(big)
    for poly in polygons:
        draw.polygon([(x * ss, y * ss) for x, y in to_pixels(poly, frame)], fill=255)
    for (x, y), r in circles:
        px, py = to_pixels([(x, y)], frame)[0]
        pr = r * frame.scale
        draw.ellipse([(px - pr) * ss, (py - pr) * ss, (px + pr) * ss, (py + pr) * ss], fill=255)
    return np.asarray(big.resize((frame.size, frame.size), Image.BOX), dtype=np.float32) / 255.0


def shifted(polygon, dx, dy):
    return [(x + dx, y + dy) for x, y in polygon]


def solid(size, color):
    return np.broadcast_to(np.array(color, dtype=np.float32) / 255.0, (size, size, 3))


def vertical_gradient(frame, y0, y1, c0, c1):
    ys = (np.arange(frame.size, dtype=np.float32) + 0.5 - frame.oy) / frame.scale
    t = np.clip((ys - y0) / max(y1 - y0, 1e-6), 0.0, 1.0)[:, None, None]
    a, b = np.array(c0, dtype=np.float32) / 255.0, np.array(c1, dtype=np.float32) / 255.0
    return np.broadcast_to(a + (b - a) * t, (frame.size, frame.size, 3))


def mix(c0, c1, t):
    return tuple(a + (b - a) * t for a, b in zip(c0, c1))


def steel_tone(light):
    if light < 0.5:
        return mix(STEEL_SHADE, STEEL_MID, light * 2)
    return mix(STEEL_MID, STEEL_LIT, (light - 0.5) * 2)


def paint(canvas, rgb, alpha):
    a = alpha[..., None]
    canvas[..., :3] = rgb * a + canvas[..., :3] * (1.0 - a)
    canvas[..., 3:] = a + canvas[..., 3:] * (1.0 - a)


def blank(size):
    return np.zeros((size, size, 4), dtype=np.float32)


def to_image(canvas):
    alpha = canvas[..., 3:]
    rgb = np.where(alpha > 1e-6, canvas[..., :3] / np.maximum(alpha, 1e-6), 0.0)
    pixels = np.concatenate([rgb, alpha], axis=-1)
    return Image.fromarray((np.clip(pixels, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGBA")


def grain_for(spec, frame):
    # Brushed texture turns into speckle on small renders, so it fades out with scale.
    return spec.grain * min(1.0, frame.scale / 4.2)


def brushed_grain(size, amplitude, seed):
    if amplitude <= 0:
        return np.ones((size, size), dtype=np.float32)
    rng = np.random.default_rng(seed)
    kernel = max(3, size // 12)
    noise = np.cumsum(rng.standard_normal((size, size + kernel)).astype(np.float32), axis=1)
    streaks = noise[:, kernel:] - noise[:, :-kernel]
    return 1.0 + amplitude * streaks / (streaks.std() + 1e-6)


def grained(rgb, grain):
    return np.clip(rgb * grain[..., None], 0.0, 1.0)


def blurred(mask, radius):
    image = Image.fromarray((np.clip(mask, 0, 1) * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(radius))
    return np.asarray(image, dtype=np.float32) / 255.0


# ---- painting ----

def facet_light(a, b, orientation, outward):
    nx, ny = inward_normal(a, b, orientation)
    sign = -1.0 if outward else 1.0
    return 0.5 + 0.5 * sign * (nx * LIGHT[0] + ny * LIGHT[1])


def ring_facets(outer_ring, inner_ring, outward):
    n = len(outer_ring)
    orientation = orientation_of(outer_ring)
    return [
        ([outer_ring[i], outer_ring[(i + 1) % n], inner_ring[(i + 1) % n], inner_ring[i]],
         facet_light(outer_ring[i], outer_ring[(i + 1) % n], orientation, outward))
        for i in range(n)
    ]


def rim_facets(spec):
    outer = shield_outline(spec)
    o = inset(outer, spec.outline)
    i = inset(outer, spec.outline + spec.rim)
    if spec.bevel == "flat":
        return [(quad, 0.5 + (light - 0.5) * 0.7) for quad, light in ring_facets(o, i, outward=True)]
    m = inset(outer, spec.outline + spec.rim * 0.5)
    return ring_facets(o, m, outward=True) + ring_facets(m, i, outward=False)


def paint_body(canvas, spec, frame):
    outer = shield_outline(spec)
    paint(canvas, solid(frame.size, OUTLINE), coverage(frame, [outer]))
    grain = brushed_grain(frame.size, grain_for(spec, frame), seed=7)
    paint(canvas, grained(solid(frame.size, STEEL_MID), grain), coverage(frame, [inset(outer, spec.outline)]))
    for quad, light in rim_facets(spec):
        paint(canvas, grained(solid(frame.size, steel_tone(light)), grain), coverage(frame, [quad]))


def paint_scratches(canvas, spec, frame):
    if spec.wear <= 0:
        return
    outer = shield_outline(spec)
    rim = coverage(frame, [inset(outer, spec.outline)]) * (1.0 - coverage(frame, [inset(outer, spec.outline + spec.rim)]))
    quads = scratch_quads(spec)
    paint(canvas, solid(frame.size, OUTLINE), coverage(frame, [shifted(q, 0.1, 0.18) for q in quads]) * rim * 0.35 * spec.wear)
    paint(canvas, solid(frame.size, STEEL_LIT), coverage(frame, quads) * rim * 0.6 * spec.wear)


def paint_field(canvas, spec, frame):
    outer = shield_outline(spec)
    if spec.seam > 0:
        paint(canvas, solid(frame.size, SEAM), coverage(frame, [inset(outer, spec.outline + spec.rim)]))
    field = field_polygon(spec)
    gradient = vertical_gradient(frame, spec.notch, spec.apex, FIELD_TOP, FIELD_BOTTOM)
    paint(canvas, grained(gradient, brushed_grain(frame.size, grain_for(spec, frame) * 0.5, seed=11)), coverage(frame, [field]))
    if spec.shadow > 0:
        inner = coverage(frame, [field]) * (1.0 - coverage(frame, [shifted(field, spec.shadow * 0.45, spec.shadow)]))
        paint(canvas, solid(frame.size, BLACK), inner * 0.5)


def overspray(letters, field, spec, frame):
    # Paint sprayed through a stencil leaves a speckled fringe, not a smooth glow.
    fringe = blurred(letters, spec.overspray * frame.scale) * (1.0 - letters) * field
    dots = np.random.default_rng(WEAR_SEED + 2).random(letters.shape) < SPRAY_DENSITY
    return fringe * (0.1 + 0.55 * dots)


def paint_numeral(canvas, spec, frame):
    parts = numeral_parts(spec)
    letters = coverage(frame, parts)
    if spec.overspray > 0:
        paint(canvas, solid(frame.size, HOT_BOTTOM), overspray(letters, coverage(frame, [field_polygon(spec)]), spec, frame))
    chips = coverage(frame, chip_polygons(spec)) * letters * spec.wear if spec.wear > 0 else np.zeros_like(letters)
    paint(canvas, vertical_gradient(frame, spec.ii_top, spec.ii_bottom, HOT_TOP, HOT_BOTTOM), letters * (1.0 - chips))
    if spec.bevel == "split":
        ridge = letters * (1.0 - coverage(frame, [shifted(p, 0, spec.serif_h * 0.12) for p in parts])) * (1.0 - chips)
        paint(canvas, solid(frame.size, HOT_RIDGE), ridge * RIDGE_ALPHA)
    if spec.wear > 0:
        paint(canvas, solid(frame.size, CHIP_METAL), chips * CHIP_ALPHA)


def paint_rivets(canvas, spec, frame):
    if spec.rivet <= 0:
        return
    r = spec.rivet
    centers = rivet_centers(spec)
    paint(canvas, solid(frame.size, OUTLINE), coverage(frame, circles=[((x + r * 0.18, y + r * 0.28), r) for x, y in centers]) * 0.8)
    heads = coverage(frame, circles=[((x, y), r * 0.84) for x, y in centers])
    paint(canvas, vertical_gradient(frame, 0, spec.apex, RIVET_TOP, RIVET_BOTTOM), heads)
    glints = coverage(frame, circles=[((x - r * 0.26, y - r * 0.3), r * 0.3) for x, y in centers])
    paint(canvas, solid(frame.size, WHITE), glints * 0.85)


def render(spec, frame):
    canvas = blank(frame.size)
    paint_body(canvas, spec, frame)
    paint_scratches(canvas, spec, frame)
    paint_field(canvas, spec, frame)
    paint_numeral(canvas, spec, frame)
    paint_rivets(canvas, spec, frame)
    return to_image(canvas)


def render_silhouette(spec, frame, color):
    flat = replace(spec, cut=0)
    alpha = coverage(frame, [shield_outline(flat)]) * (1.0 - coverage(frame, numeral_parts(flat)))
    canvas = blank(frame.size)
    paint(canvas, solid(frame.size, color), alpha)
    return to_image(canvas)


# ---- frames ----

def enclosing_radius(spec, yc):
    cx = spec.width / 2
    return max(math.hypot(x - cx, y - yc) for x, y in shield_outline(spec))


def enclosing_center(spec):
    lo, hi = 0.0, spec.apex
    for _ in range(80):
        m1, m2 = lo + (hi - lo) / 3, hi - (hi - lo) / 3
        if enclosing_radius(spec, m1) < enclosing_radius(spec, m2):
            hi = m2
        else:
            lo = m1
    return (lo + hi) / 2


def centered_frame(spec, size, reach_px):
    yc = enclosing_center(spec)
    scale = reach_px / enclosing_radius(spec, yc)
    return Frame(size, scale, size / 2 - spec.width / 2 * scale, size / 2 - yc * scale)


def full_bleed_frame(spec, size):
    pad = max(0.5, size * 0.02)
    scale = (size - 2 * pad) / spec.apex
    return Frame(size, scale, (size - spec.width * scale) / 2, pad)


def hinted_frame(size, offset):
    return Frame(size, 1.0, float(offset), 0.0)


def wear_at(scale):
    return min(1.0, max(0.0, (scale - WEAR_MIN_SCALE) / (WEAR_FULL_SCALE - WEAR_MIN_SCALE)))


def spec_for(spec, frame):
    if spec.cut * frame.scale < STENCIL_MIN_CUT_PX:
        spec = replace(spec, cut=0)
    if frame.scale < WEAR_MIN_SCALE:
        return replace(spec, wear=0, overspray=0)
    return replace(spec, wear=spec.wear * wear_at(frame.scale))


def icon_at(size):
    if size in HINTED:
        offset, spec = HINTED[size]
        return render(spec, hinted_frame(size, offset))
    frame = full_bleed_frame(DESIGN, size)
    return render(spec_for(DESIGN, frame), frame)


def mark_at(size, reach_px):
    frame = centered_frame(DESIGN, size, reach_px)
    return render(spec_for(DESIGN, frame), frame)


def adaptive_foreground(size):
    return checked_safe(mark_at(size, MARK_REACH * size), f"foreground {size}px")


def adaptive_monochrome(size):
    frame = centered_frame(DESIGN, size, MARK_REACH * size)
    return checked_safe(render_silhouette(DESIGN, frame, WHITE), f"monochrome {size}px")


def on_color(image, color):
    tile = Image.new("RGBA", image.size, color + (255,) if len(color) == 3 else color)
    tile.alpha_composite(image)
    return tile


def shape_mask(size, extent, kind):
    ss = 8
    mask = Image.new("L", (size * ss, size * ss), 0)
    draw = ImageDraw.Draw(mask)
    lo, hi = (size - extent) / 2 * ss, (size + extent) / 2 * ss - 1
    if kind == "round":
        draw.ellipse([lo, lo, hi, hi], fill=255)
    else:
        draw.rounded_rectangle([lo, lo, hi, hi], radius=extent * ss * 0.18, fill=255)
    return mask.resize((size, size), Image.BOX)


def legacy_icon(size, kind):
    shape = size * LEGACY_SHAPE_DP / LEGACY_DP
    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    icon.paste(Image.new("RGBA", (size, size), LAYER_BACKGROUND + (255,)), (0, 0), shape_mask(size, shape, kind))
    icon.alpha_composite(mark_at(size, MARK_REACH * LAYER_DP / VISIBLE_DP * shape))
    return icon


def hex_color(color):
    return "#{:02X}{:02X}{:02X}".format(*(round(c) for c in color))


def svg_points(points):
    return " ".join(f"{x:.2f},{y:.2f}" for x, y in points)


def svg_document(spec, body):
    pad = (spec.apex - spec.width) / 2
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{-pad:g} 0 {spec.apex:g} {spec.apex:g}">\n'
            + "".join(f"  {line}\n" for line in body) + "</svg>\n")


def flat_svg(spec):
    flat = replace(spec, cut=0)
    outer = shield_outline(flat)
    layers = [(outer, OUTLINE), (inset(outer, flat.outline), STEEL_MID), (inset(outer, flat.outline + flat.rim), SEAM),
              (field_polygon(flat), FIELD_FLAT)] + [(part, HOT_FLAT) for part in numeral_parts(flat)]
    return svg_document(flat, [f'<polygon points="{svg_points(p)}" fill="{hex_color(c)}"/>' for p, c in layers])


def mono_svg(spec):
    flat = replace(spec, cut=0)
    rings = [shield_outline(flat)] + numeral_parts(flat)
    path = " ".join(f"M{svg_points(r)}Z" for r in rings)
    return svg_document(flat, [f'<path fill-rule="evenodd" fill="{hex_color(STEEL_LIT)}" d="{path}"/>'])
