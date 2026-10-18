"""Final icon assets for the chosen Y-mark direction."""
from pathlib import Path
import shutil
from PIL import Image, ImageDraw
from make_icons import downsample, font, LIGHT_BG, DARK_BG, INK
from explore2 import y_mark

ROOT = Path(__file__).resolve().parent
SIZES = (512, 256, 128, 64, 48, 32, 24, 16)


def save_set(name, master):
    master.save(ROOT / f"{name}-master.png")
    for size in SIZES:
        downsample(master, size).save(ROOT / f"{name}-{size}.png")
    master.resize((256, 256), Image.Resampling.LANCZOS).save(
        ROOT / f"{name}.ico",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )


def preview(path):
    light = y_mark(False)
    dark = y_mark(True)
    width, height = 1280, 720
    sheet = Image.new("RGBA", (width, height), (232, 235, 231, 255))
    draw = ImageDraw.Draw(sheet)
    draw.text((40, 30), "YanQiao / E_ShuRu · Y bridge mark", fill=INK, font=font(32, True))
    draw.text((40, 72), "Light tile for the app icon; dark tile as a high-contrast alternate", fill=(97, 115, 107, 255), font=font(18))
    for i, (label, master, bg) in enumerate(
        [("light tile", light, (232, 235, 231, 255)), ("dark tile", dark, (23, 30, 29, 255))]
    ):
        x = 40 + i * 610
        draw.text((x, 126), label, fill=INK, font=font(22, True))
        card = Image.new("RGBA", (300, 300), bg)
        card.alpha_composite(downsample(master, 256), (22, 22))
        sheet.alpha_composite(card, (x, 166))
        # Actual-size ladder; 48/64 are shown at 2× to keep the two columns apart.
        sx = x
        for size in (16, 24, 32, 48, 64):
            icon = downsample(master, size)
            scale = 3 if size <= 32 else 2
            shown = icon.resize((size * scale, size * scale), Image.Resampling.NEAREST)
            draw.text((sx, 492), f"{size}", fill=(97, 115, 107, 255), font=font(14))
            sheet.alpha_composite(shown, (sx, 516))
            sx += size * scale + 22
    draw.text((40, 660), "ICO: 16 / 24 / 32 / 48 / 64 / 128 / 256 · SVG masters included", fill=(97, 115, 107, 255), font=font(17))
    sheet.convert("RGB").save(path)


def main():
    light = y_mark(False)
    dark = y_mark(True)
    save_set("yanqiao", light)
    save_set("yanqiao-dark", dark)
    # Keep the resource copy used by app.rc in sync with the approved asset.
    resource = ROOT.parents[1] / "src" / "app.ico"
    resource.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "yanqiao.ico", resource)
    preview(ROOT / "preview-final.png")
    print("saved yanqiao (light), yanqiao-dark, and src/app.ico")


if __name__ == "__main__":
    main()
