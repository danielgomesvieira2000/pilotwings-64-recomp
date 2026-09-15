"""Draw the port's launcher background and Windows icon.

The artwork is original to this project -- flat-shaded low-poly shapes in the
style of the era -- and deliberately takes nothing from the cartridge or from
Nintendo's branding. The launcher background (assets/icons/Logo.svg, a hang
glider over an island at dusk) and the executable's icon (assets/AppIcon.ico, a
propeller plane climbing across the same dusk sky) are both generated here:

    python tools/make_logo.py

The SVG is what recompui draws in the launcher; the ICO holds every standard
icon size, each rasterised from the polygons at that size with supersampling
rather than scaled from one bitmap.
"""

from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent

# The canvas the coordinates are written in.
WIDTH, HEIGHT = 680, 360

# The launcher background: a dusk scene, dark so that recompui's white menu text
# reads over it, with everything bright kept out of the middle column where the
# menu sits (roughly x 240-440, y 130-300 on this canvas).
BACKGROUND = [
    # Sky, from deep blue overhead to a warm band at the horizon.
    ("#0b1630", [(0, 0), (680, 0), (680, 70), (0, 70)]),
    ("#10213f", [(0, 70), (680, 70), (680, 140), (0, 140)]),
    ("#172c50", [(0, 140), (680, 140), (680, 200), (0, 200)]),
    ("#243a5e", [(0, 200), (680, 200), (680, 236), (0, 236)]),
    ("#4a3f5c", [(0, 236), (680, 236), (680, 256), (0, 256)]),
    # Sea.
    ("#0d1c36", [(0, 256), (680, 256), (680, 360), (0, 360)]),
    ("#122647", [(0, 256), (300, 256), (140, 300), (0, 292)]),
    # An island low on the left.
    ("#6b5b43", [(10, 282), (80, 250), (190, 246), (250, 262), (170, 284), (60, 292)]),
    ("#1f5a33", [(40, 268), (95, 236), (150, 214), (170, 250), (120, 266)]),
    ("#17472a", [(150, 214), (215, 240), (230, 262), (170, 262), (170, 250)]),
    # A pale sun setting on the right.
    ("#f2c46a", [(600, 222), (618, 229), (626, 246), (574, 246), (582, 229)]),
    # The hang glider, small, high on the right.
    ("#e8402e", [(500, 74), (620, 40), (600, 68)]),
    ("#ffcf33", [(500, 74), (600, 68), (646, 88)]),
    ("#b82a1d", [(500, 74), (620, 40), (627, 47), (508, 77)]),
    ("#d9dde6", [(570, 73), (574, 73), (581, 100), (577, 100)]),
    ("#d9dde6", [(562, 100), (596, 96), (597, 99), (563, 103)]),
    ("#ffd2a6", [(582, 76), (589, 75), (590, 82), (583, 83)]),
]

# The icon: a flat-shaded low-poly propeller plane climbing across the same dusk
# sky, drawn large and bright so it still reads at a few dozen pixels. The icon
# is square, ICON_SPAN units a side.
ICON_SPAN = 320

ICON_BACKGROUND = [
    # Sky bands, deep overhead to the warm horizon, as in the launcher.
    ("#0b1630", [(0, 0), (320, 0), (320, 90), (0, 90)]),
    ("#10213f", [(0, 90), (320, 90), (320, 160), (0, 160)]),
    ("#172c50", [(0, 160), (320, 160), (320, 215), (0, 215)]),
    ("#243a5e", [(0, 215), (320, 215), (320, 245), (0, 245)]),
    ("#4a3f5c", [(0, 245), (320, 245), (320, 262), (0, 262)]),
    # Sea.
    ("#0d1c36", [(0, 262), (320, 262), (320, 320), (0, 320)]),
    ("#122647", [(0, 262), (170, 262), (80, 300), (0, 294)]),
    # A pale sun on the horizon, and an island.
    ("#f2c46a", [(250, 236), (268, 243), (276, 262), (224, 262), (232, 243)]),
    ("#6b5b43", [(0, 296), (30, 272), (86, 268), (112, 280), (60, 298)]),
    ("#1f5a33", [(10, 286), (40, 262), (68, 250), (78, 272), (50, 284)]),
    ("#17472a", [(68, 250), (98, 266), (104, 278), (78, 278), (78, 272)]),
]

# The plane in its own frame: x along the fuselage towards the nose, y across
# the wings (negative is the far wing), and an optional z for height above the
# fuselage (the fin), which is drawn straight up the icon. Back to front.
PLANE = [
    # Far tailplane and far wing, behind the fuselage.
    ("#e0a81f", [(-88, -4), (-110, -4), (-118, -40), (-106, -40)]),
    ("#e0a81f", [(46, -16), (0, -16), (-8, -112), (16, -112)]),
    ("#ffcf33", [(46, -16), (30, -16), (12, -112), (16, -112)]),
    # Fuselage: the lit upper half, the shaded lower half, a band.
    ("#e8402e", [(112, 0), (92, -16), (58, -20), (-24, -19), (-114, -6), (-114, 0)]),
    ("#b82a1d", [(112, 0), (92, 16), (58, 20), (-24, 19), (-114, 6), (-114, 0)]),
    ("#ffcf33", [(-38, -19), (-54, -17), (-54, 17), (-38, 19)]),
    # Cowling and spinner.
    ("#2d2d3a", [(112, 0), (94, -17), (86, -18), (86, 18), (94, 17)]),
    ("#d9dde6", [(124, 0), (112, -8), (112, 8)]),
    # Canopy, with a highlight.
    ("#5fb6f0", [(70, -12), (40, -15), (26, 0), (40, 12), (70, 9)]),
    ("#d9f1ff", [(66, -9), (44, -12), (38, -4), (60, -3)]),
    # The fin, standing up from the tail.
    ("#b82a1d", [(-78, 0, 0), (-114, 0, 0), (-120, 0, 46), (-104, 0, 46)]),
    ("#e8402e", [(-78, 0, 0), (-104, 0, 46), (-96, 0, 46)]),
    # Near tailplane and near wing, in front of the fuselage.
    ("#ffcf33", [(-88, 4), (-110, 4), (-120, 46), (-106, 46)]),
    ("#ffcf33", [(48, 16), (0, 17), (-10, 132), (18, 132)]),
    ("#e0a81f", [(0, 17), (-10, 132), (-3, 132), (9, 17)]),
    ("#fff0b0", [(48, 16), (34, 16), (15, 132), (18, 132)]),
    # The propeller: two pale blades through the spinner.
    ("#d9dde6", [(122, -2), (124, -2), (126, -42), (122, -42)]),
    ("#aeb4c2", [(122, 2), (124, 2), (120, 42), (116, 42)]),
]

# Where the plane sits in the icon: its centre, the heading it climbs at in
# degrees, its scale, and how much the wings are foreshortened by the view.
PLANE_CENTRE = (162, 142)
PLANE_HEADING = 32
PLANE_SCALE = 1.1
PLANE_FORESHORTEN = 0.62


def plane_polygons():
    import math
    a = math.radians(PLANE_HEADING)
    cos_a, sin_a = math.cos(a), math.sin(a)
    cx, cy = PLANE_CENTRE
    out = []
    for colour, points in PLANE:
        placed = []
        for point in points:
            x, y = point[0] * PLANE_SCALE, point[1] * PLANE_SCALE * PLANE_FORESHORTEN
            z = (point[2] if len(point) > 2 else 0) * PLANE_SCALE
            # Screen y grows downwards: the nose climbs up and to the right.
            placed.append((cx + x * cos_a + y * sin_a, cy - x * sin_a + y * cos_a - z))
        out.append((colour, placed))
    return out


ICON_SIZES = [16, 24, 32, 48, 64, 128, 256]


def write_svg(path: Path) -> None:
    lines = [
        f'<svg width="100%" viewBox="0 0 {WIDTH} {HEIGHT}" role="img" xmlns="http://www.w3.org/2000/svg">',
        "<title>Pilotwings 64: Recompiled</title>",
        "<desc>A flat-shaded low-poly hang glider over an island at dusk. Original artwork, "
        "generated by tools/make_logo.py.</desc>",
    ]
    for colour, points in BACKGROUND:
        pts = " ".join(f"{x},{y}" for x, y in points)
        lines.append(f'<polygon points="{pts}" fill="{colour}"/>')
    lines.append("</svg>")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def render(size: int) -> Image.Image:
    """Square icon: the plane over the dusk sea, rasterised at this size."""
    supersample = 8
    canvas = size * supersample
    scale = canvas / ICON_SPAN
    image = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for colour, points in ICON_BACKGROUND + plane_polygons():
        draw.polygon([(x * scale, y * scale) for x, y in points], fill=colour)
    # Round the corners so the icon does not read as a bare square.
    mask = Image.new("L", (canvas, canvas), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, canvas - 1, canvas - 1), radius=canvas // 6, fill=255)
    image.putalpha(mask)
    return image.resize((size, size), Image.LANCZOS)


def main() -> None:
    write_svg(REPO / "assets" / "icons" / "Logo.svg")
    largest = render(ICON_SIZES[-1])
    largest.save(REPO / "assets" / "AppIcon.ico", sizes=[(s, s) for s in ICON_SIZES])
    print("wrote assets/icons/Logo.svg and assets/AppIcon.ico")


if __name__ == "__main__":
    main()
