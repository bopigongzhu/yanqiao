"""Second icon exploration: light tile and bridge/monogram alternatives."""
from pathlib import Path
from PIL import Image, ImageDraw
from make_icons import (
    S,
    px,
    downsample,
    font,
    arch_mask,
    paste_mask,
    sizes_sheet,
    IVORY,
    MINT,
    INK,
    DARK_BG,
    LIGHT_BG,
)

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "explore2"
DEEP = (31, 77, 61, 255)
ACCENT_LIGHT = (79, 155, 122, 255)
WHITE = (251, 250, 245, 255)
WARM = (239, 236, 226, 255)


def tile(dark=True):
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gradient = Image.new("RGB", (S, S))
    draw = ImageDraw.Draw(gradient)
    top = (45, 111, 88) if dark else WHITE
    bottom = (24, 65, 51) if dark else WARM
    for y in range(S):
        t = y / (S - 1)
        color = tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3))
        draw.line([(0, y), (S, y)], fill=color)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [px(8), px(8), S - px(8), S - px(8)], radius=px(58), fill=255
    )
    image.paste(gradient, (0, 0), mask)
    if not dark:
        border = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(border).rounded_rectangle(
            [px(8), px(8), S - px(8), S - px(8)],
            radius=px(58),
            outline=(29, 48, 43, 36),
            width=px(1.5),
        )
        image.alpha_composite(border)
    return image


def rounded(draw, box, radius, color):
    draw.rounded_rectangle([px(box[0]), px(box[1]), px(box[2]), px(box[3])], radius=px(radius), fill=color)


def line(draw, points, width, color):
    pts = [(px(x), px(y)) for x, y in points]
    draw.line(pts, fill=color, width=px(width), joint="curve")
    r = px(width / 2)
    for x, y in pts:
        draw.ellipse([x - r, y - r, x + r, y + r], fill=color)


def quadratic(p0, p1, p2, steps=32):
    out = []
    for i in range(steps + 1):
        t = i / steps
        x = (1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t**2 * p2[0]
        y = (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t**2 * p2[1]
        out.append((x, y))
    return out


def suspension(dark=True, simple=False):
    image = tile(dark)
    mark = IVORY if dark else DEEP
    accent = MINT if dark else ACCENT_LIGHT
    draw = ImageDraw.Draw(image)
    # deck
    rounded(draw, (26, 170, 230, 184), 7, mark)
    # towers
    rounded(draw, (70, 82, 90, 194), 10, mark)
    rounded(draw, (166, 82, 186, 194), 10, mark)
    # side spans
    line(draw, [(26, 136), (80, 84)], 10, accent)
    line(draw, [(176, 84), (230, 136)], 10, accent)
    # main cable
    line(draw, [(80, 84)] + quadratic((80, 84), (128, 150), (176, 84)) + [(176, 84)], 10, accent)
    if not simple:
        # hangers
        for x in (104, 128, 152):
            t = (x - 80) / 96
            y = (1 - t) ** 2 * 84 + 2 * (1 - t) * t * 150 + t**2 * 84
            line(draw, [(x, y), (x, 170)], 4, accent)
        # water hint
        draw.rounded_rectangle([px(56), px(204), px(200), px(210)], radius=px(3), fill=(*accent[:3], 70))
    return image


def y_bridge(dark=True):
    image = tile(dark)
    mark = IVORY if dark else DEEP
    accent = MINT if dark else ACCENT_LIGHT
    draw = ImageDraw.Draw(image)
    # deck
    rounded(draw, (52, 188, 204, 198), 5, accent)
    # stem
    rounded(draw, (113, 146, 143, 210), 15, accent)
    # arms
    for start, end in (((58, 76), (128, 150)), ((198, 76), (128, 150))):
        line(draw, [start, end], 32, mark)
    return image


def y_mark(dark=True):
    """A Y tower sitting on a bridge deck: monogram and bridge in one mark."""
    image = tile(dark)
    mark = IVORY if dark else DEEP
    accent = MINT if dark else ACCENT_LIGHT
    draw = ImageDraw.Draw(image)
    rounded(draw, (40, 188, 216, 199), 5.5, accent)
    # stem stops at the deck, so the mark reads as a tower/cable, not a glass stem.
    rounded(draw, (112, 146, 144, 190), 16, mark)
    for start, end in (((56, 74), (128, 150)), ((200, 74), (128, 150))):
        line(draw, [start, end], 33, mark)
    return image


def a_bridge(dark=True):
    """An A-shaped cable-stayed bridge: A for English, bridge for the product."""
    image = tile(dark)
    mark = IVORY if dark else DEEP
    accent = MINT if dark else ACCENT_LIGHT
    draw = ImageDraw.Draw(image)
    rounded(draw, (34, 188, 222, 199), 5.5, accent)
    # Two cables and a central tower meet at the apex.
    for end in ((48, 188), (208, 188)):
        line(draw, [(128, 72), end], 30, mark)
    rounded(draw, (113, 72, 143, 190), 15, mark)
    return image


def a6_light():
    image = tile(False)
    paste_mask(image, DEEP, arch_mask(38, 78, 218, 214, 22, 182))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([px(26), px(184), px(230), px(194)], radius=px(5), fill=ACCENT_LIGHT)
    return image


def viaduct(dark=True):
    image = tile(dark)
    mark = IVORY if dark else DEEP
    accent = MINT if dark else ACCENT_LIGHT
    body = Image.new("L", (S, S), 0)
    draw = ImageDraw.Draw(body)
    # deck
    draw.rounded_rectangle([px(24), px(86), px(232), px(112)], radius=px(8), fill=255)
    # three piers
    for x0, x1 in ((34, 68), (116, 140), (188, 222)):
        draw.rounded_rectangle([px(x0), px(86), px(x1), px(208)], radius=px(10), fill=255)
    # two arched openings
    openings = Image.new("L", (S, S), 0)
    od = ImageDraw.Draw(openings)
    for x0, x1 in ((68, 116), (140, 188)):
        od.ellipse([px(x0), px(124), px(x1), px(172)], fill=255)
        od.rectangle([px(x0), px(148), px(x1), px(208)], fill=255)
    from PIL import ImageChops

    body = ImageChops.subtract(body, openings)
    paste_mask(image, mark, body)
    draw = ImageDraw.Draw(image)
    # road / water accents
    draw.rounded_rectangle([px(30), px(90), px(226), px(99)], radius=px(4), fill=accent)
    for x0, x1 in ((72, 112), (144, 184)):
        draw.rounded_rectangle([px(x0), px(198), px(x1), px(204)], radius=px(3), fill=accent)
    return image


def preview(path):
    variants = [
        ("A1 · Light A bridge", lambda: a_bridge(False)),
        ("L6 · Light Y mark", lambda: y_mark(False)),
        ("A2 · Dark A bridge", lambda: a_bridge(True)),
        ("D5 · Dark Y mark", lambda: y_mark(True)),
        ("L4 · Clean suspension", lambda: suspension(False, True)),
    ]
    width, height = 1800, 760
    sheet = Image.new("RGBA", (width, height), LIGHT_BG)
    draw = ImageDraw.Draw(sheet)
    draw.text((40, 28), "Second pass · A bridge / Y mark", fill=INK, font=font(34, True))
    draw.text((40, 70), "A for English, bridge for the product; light and dark tiles", fill=(97, 115, 107, 255), font=font(18))
    col = (width - 80) / len(variants)
    for i, (name, fn) in enumerate(variants):
        image = downsample(fn(), 256)
        x = int(40 + i * col)
        draw.text((x, 120), name, fill=INK, font=font(20, True))
        background = Image.new("RGBA", (256, 256), DARK_BG if i in (2, 3) else LIGHT_BG)
        background.alpha_composite(image, (0, 0))
        sheet.alpha_composite(background, (x, 160))
        # 32 px on a taskbar-like strip
        small = downsample(fn(), 32)
        strip = Image.new("RGBA", (256, 48), (32, 32, 32, 255))
        strip.alpha_composite(small, (12, 8))
        sheet.alpha_composite(strip, (x, 450))
        draw.text((x, 510), "32 px on dark strip", fill=(97, 115, 107, 255), font=font(16))
    sheet.convert("RGB").save(path)


def main():
    OUT.mkdir(exist_ok=True)
    for name, fn in [
        ("l1-light-bridge", a6_light),
        ("l2-light-suspension", lambda: suspension(False)),
        ("l3-light-y", lambda: y_bridge(False)),
        ("l4-light-suspension-clean", lambda: suspension(False, True)),
        ("d1-dark-suspension", lambda: suspension(True)),
        ("d2-dark-y", lambda: y_bridge(True)),
        ("d3-dark-suspension-clean", lambda: suspension(True, True)),
        ("l5-light-viaduct", lambda: viaduct(False)),
        ("d4-dark-viaduct", lambda: viaduct(True)),
        ("l6-light-y-mark", lambda: y_mark(False)),
        ("d5-dark-y-mark", lambda: y_mark(True)),
        ("a1-light-a-bridge", lambda: a_bridge(False)),
        ("a2-dark-a-bridge", lambda: a_bridge(True)),
    ]:
        master = fn()
        master.save(OUT / f"{name}-master.png")
        for size in (512, 256, 128, 64, 48, 32, 24, 16):
            downsample(master, size).save(OUT / f"{name}-{size}.png")
    preview(OUT / "preview.png")
    sizes_sheet(a6_light(), OUT / "sizes-l1.png")
    sizes_sheet(y_bridge(False), OUT / "sizes-l3.png")
    sizes_sheet(suspension(False, True), OUT / "sizes-l4.png")
    sizes_sheet(suspension(True, True), OUT / "sizes-d3.png")
    sizes_sheet(viaduct(False), OUT / "sizes-l5.png")
    sizes_sheet(viaduct(True), OUT / "sizes-d4.png")
    sizes_sheet(y_mark(False), OUT / "sizes-l6.png")
    sizes_sheet(y_mark(True), OUT / "sizes-d5.png")
    sizes_sheet(a_bridge(False), OUT / "sizes-a1.png")
    sizes_sheet(a_bridge(True), OUT / "sizes-a2.png")
    print("generated explore2")


if __name__ == "__main__":
    main()
