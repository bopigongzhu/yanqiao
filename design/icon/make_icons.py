"""Generate 言桥 icon concepts as PNG/ICO assets.

The masters are drawn at 2048 px and downsampled with Lanczos.  The ICO
contains the standard Windows sizes from 16 to 256 px.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent
ALT = ROOT / "alternatives"
S = 2048
K = S / 256.0

INK = (29, 48, 43, 255)
TEAL = (37, 99, 79, 255)
TEAL_TOP = (45, 111, 88, 255)
TEAL_BOTTOM = (24, 65, 51, 255)
IVORY = (247, 245, 238, 255)
MINT = (154, 214, 184, 255)
DEEP = (23, 53, 43, 255)
LIGHT_BG = (245, 246, 243, 255)
DARK_BG = (23, 30, 29, 255)


def px(v):
    return int(round(v * K))


def tile():
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gradient = Image.new("RGB", (S, S))
    draw = ImageDraw.Draw(gradient)
    for y in range(S):
        t = y / (S - 1)
        color = tuple(int(TEAL_TOP[i] * (1 - t) + TEAL_BOTTOM[i] * t) for i in range(3))
        draw.line([(0, y), (S, y)], fill=color)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [px(8), px(8), S - px(8), S - px(8)], radius=px(56), fill=255
    )
    image.paste(gradient, (0, 0), mask)
    return image


def arch_mask(x0, y0, x1, y1, stroke, leg_bottom):
    mask = Image.new("L", (S, S), 0)
    draw = ImageDraw.Draw(mask)
    draw.ellipse([px(x0), px(y0), px(x1), px(y1)], fill=255)
    draw.ellipse(
        [px(x0 + stroke), px(y0 + stroke), px(x1 - stroke), px(y1 - stroke)], fill=0
    )
    spring = (y0 + y1) / 2
    draw.rectangle([0, px(spring), S, S], fill=0)
    draw.rounded_rectangle(
        [px(x0), px(spring - stroke / 2), px(x0 + stroke), px(leg_bottom)],
        radius=px(stroke / 2),
        fill=255,
    )
    draw.rounded_rectangle(
        [px(x1 - stroke), px(spring - stroke / 2), px(x1), px(leg_bottom)],
        radius=px(stroke / 2),
        fill=255,
    )
    return mask


def paste_mask(image, color, mask):
    image.paste(color, (0, 0), mask)


def rounded_bubble(box, radius, tail):
    mask = Image.new("L", (S, S), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle([px(box[0]), px(box[1]), px(box[2]), px(box[3])], radius=px(radius), fill=255)
    draw.polygon([(px(x), px(y)) for x, y in tail], fill=255)
    return mask


def variant_a1():
    image = tile()
    paste_mask(image, IVORY, arch_mask(52, 78, 204, 230, 22, 196))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([px(60), px(199), px(196), px(209)], radius=px(5), fill=MINT)
    return image


def variant_a2():
    image = tile()
    paste_mask(image, IVORY, arch_mask(52, 78, 204, 230, 22, 196))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([px(60), px(199), px(196), px(209)], radius=px(5), fill=MINT)
    draw.polygon(
        [
            (px(128), px(64)),
            (px(143), px(79)),
            (px(128), px(94)),
            (px(113), px(79)),
        ],
        fill=MINT,
    )
    return image


def variant_a3():
    image = tile()
    paste_mask(image, IVORY, arch_mask(52, 78, 204, 230, 22, 196))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([px(60), px(199), px(114), px(209)], radius=px(5), fill=MINT)
    draw.rounded_rectangle([px(142), px(199), px(196), px(209)], radius=px(5), fill=MINT)
    draw.rounded_rectangle([px(123), px(185), px(133), px(221)], radius=px(5), fill=MINT)
    return image


def variant_a4():
    image = tile()
    paste_mask(image, IVORY, arch_mask(38, 96, 218, 232, 22, 200))
    draw = ImageDraw.Draw(image)
    # The deck extends past the piers so the arch reads as a bridge, not a magnet.
    draw.rounded_rectangle([px(26), px(202), px(230), px(212)], radius=px(5), fill=MINT)
    return image


def variant_a5():
    image = tile()
    paste_mask(image, IVORY, arch_mask(38, 96, 218, 232, 22, 200))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([px(26), px(202), px(230), px(212)], radius=px(5), fill=MINT)
    # A mint keystone makes the bridge construction legible without a wordmark.
    draw.rounded_rectangle([px(114), px(90), px(142), px(104)], radius=px(5), fill=MINT)
    return image


def variant_a6():
    image = tile()
    # Same bridge, lifted slightly so the mark is optically centred in the tile.
    paste_mask(image, IVORY, arch_mask(38, 78, 218, 214, 22, 182))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([px(26), px(184), px(230), px(194)], radius=px(5), fill=MINT)
    return image


def variant_b1():
    image = tile()
    bubble = rounded_bubble((36, 62, 220, 198), 40, [(72, 196), (58, 236), (106, 196)])
    cut = arch_mask(76, 110, 180, 198, 18, 198)
    cut = Image.composite(Image.new("L", (S, S), 0), Image.new("L", (S, S), 255), cut)
    bubble = Image.composite(bubble, Image.new("L", (S, S), 0), cut)
    paste_mask(image, IVORY, bubble)
    # Mint tail tip: a small, legible accent at 16 px.
    tail = Image.new("L", (S, S), 0)
    ImageDraw.Draw(tail).polygon(
        [(px(70), px(211)), (px(63), px(230)), (px(88), px(211))], fill=255
    )
    paste_mask(image, MINT, tail)
    return image


def variant_c1():
    image = tile()
    draw = ImageDraw.Draw(image)
    # Deck
    draw.rounded_rectangle([px(56), px(188), px(200), px(197)], radius=px(4.5), fill=MINT)
    # Stem
    draw.rounded_rectangle([px(113), px(148), px(143), px(209)], radius=px(15), fill=MINT)
    # Arms
    for start, end in (((58, 78), (128, 150)), ((198, 78), (128, 150))):
        draw.line([(px(start[0]), px(start[1])), (px(end[0]), px(end[1]))], fill=IVORY, width=px(30))
        for point in (start, end):
            r = px(15)
            draw.ellipse(
                [px(point[0]) - r, px(point[1]) - r, px(point[0]) + r, px(point[1]) + r],
                fill=IVORY,
            )
    return image


VARIANTS = {
    "a1": ("A1 · Bridge + river", variant_a1),
    "a2": ("A2 · Keystone", variant_a2),
    "a3": ("A3 · Banks + caret", variant_a3),
    "a4": ("A4 · Wider bridge", variant_a4),
    "a5": ("A5 · Keystone deck", variant_a5),
    "a6": ("A6 · Centred bridge", variant_a6),
    "b1": ("B1 · Speech-bubble bridge", variant_b1),
    "c1": ("C1 · Y bridge", variant_c1),
}
PREVIEW_ORDER = ["a4", "a6", "a5", "b1", "c1"]


def downsample(image, size):
    return image.resize((size, size), Image.Resampling.LANCZOS)


def font(size, bold=False):
    names = ["seguisb.ttf" if bold else "segoeui.ttf", "segoeuib.ttf", "arial.ttf"]
    for name in names:
        path = Path("C:/Windows/Fonts") / name
        if path.exists():
            return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def preview(path):
    width, height = 1720, 900
    sheet = Image.new("RGBA", (width, height), LIGHT_BG)
    draw = ImageDraw.Draw(sheet)
    title = font(34, True)
    label = font(23, True)
    small = font(18)
    draw.text((40, 28), "YanQiao / E_ShuRu · icon directions", fill=INK, font=title)
    draw.text((40, 70), "Deep teal tile · ivory bridge · mint accent", fill=(97, 115, 107, 255), font=small)
    columns = [(key, VARIANTS[key]) for key in PREVIEW_ORDER]
    col_width = (width - 80) / len(columns)
    for index, (key, (name, function)) in enumerate(columns):
        image = downsample(function(), 256)
        x = int(40 + index * col_width)
        draw.text((x, 130), name, fill=INK, font=label)
        sheet.alpha_composite(image, (x, 170))
        dark_card = Image.new("RGBA", (256, 256), DARK_BG)
        dark_card.alpha_composite(image, (0, 0))
        sheet.alpha_composite(dark_card, (x, 450))
        draw.text((x, 420), "light", fill=(97, 115, 107, 255), font=small)
        draw.text((x, 710), "dark", fill=(97, 115, 107, 255), font=small)
    draw.text(
        (40, 790),
        "Full masters 2048 px · ICO sizes 16, 24, 32, 48, 64, 128, 256",
        fill=(97, 115, 107, 255),
        font=small,
    )
    sheet.convert("RGB").save(path)


def sizes_sheet(master, path):
    sizes = [16, 24, 32, 48, 64, 128, 256]
    width, height = 1260, 620
    sheet = Image.new("RGBA", (width, height), LIGHT_BG)
    draw = ImageDraw.Draw(sheet)
    title = font(30, True)
    label = font(18, True)
    draw.text((32, 24), "Size ladder · actual pixels, small sizes enlarged 4×", fill=INK, font=title)
    x = 40
    for size in sizes:
        icon = downsample(master, size)
        scale = 4 if size <= 48 else 2 if size <= 64 else 1
        shown = icon.resize((size * scale, size * scale), Image.Resampling.NEAREST)
        draw.text((x, 80), f"{size}px", fill=(97, 115, 107, 255), font=label)
        sheet.alpha_composite(shown, (x, 110))
        x += size * scale + 28
    # Dark strip
    y = 400
    x = 40
    for size in sizes:
        icon = downsample(master, size)
        scale = 4 if size <= 48 else 2 if size <= 64 else 1
        shown = icon.resize((size * scale, size * scale), Image.Resampling.NEAREST)
        card = Image.new("RGBA", (size * scale, size * scale), DARK_BG)
        card.alpha_composite(shown, (0, 0))
        sheet.alpha_composite(card, (x, y))
        x += size * scale + 28
    draw.text((32, 560), "dark taskbar / tray background", fill=(97, 115, 107, 255), font=label)
    sheet.convert("RGB").save(path)


def main():
    ALT.mkdir(exist_ok=True)
    generated = {}
    for key, (name, function) in VARIANTS.items():
        master = function()
        master.save(ALT / f"yanqiao-{key}-master.png")
        for size in (512, 256, 128, 64, 48, 32, 24, 16):
            downsample(master, size).save(ALT / f"yanqiao-{key}-{size}.png")
        master.resize((256, 256), Image.Resampling.LANCZOS).save(
            ALT / f"yanqiao-{key}.ico",
            sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
        )
        generated[key] = master
    preview(ROOT / "preview-variants.png")
    sizes_sheet(generated["a6"], ROOT / "preview-sizes-a6.png")
    sizes_sheet(generated["a4"], ALT / "preview-sizes-a4.png")
    sizes_sheet(generated["a1"], ALT / "preview-sizes-a1.png")
    sizes_sheet(generated["b1"], ALT / "preview-sizes-b1.png")
    print("generated", len(generated), "variants")


if __name__ == "__main__":
    main()
