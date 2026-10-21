"""Draw the DeLSS app icon and menu-bar template icon (deterministic, PIL only).

Motif: one scene split down a diagonal. The left half is smooth and
photoreal-ish, the right half is the same scene pixelated to a 4-colour
palette: "reverse DLSS" in one glyph. Writes assets/icons/:
  AppIcon.icns (macOS), delss.ico (Windows), delss-<n>.png (Linux),
  MenuBarTemplate.png / MenuBarTemplate@2x.png (macOS menu bar, 18 pt).
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

OUT = Path(__file__).resolve().parent.parent / "assets" / "icons"
S = 1024


def lerp(a: tuple[int, ...], b: tuple[int, ...], t: float) -> tuple[int, ...]:
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def scene(size: int) -> Image.Image:
    """Sunset sky, sun, two mountain ridges, a lake: a stand-in 'photo'."""
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        t = y / size
        sky = lerp((40, 46, 120), (255, 140, 90), min(1.0, t * 1.6))
        for x in range(size):
            px[x, y] = sky
    d = ImageDraw.Draw(img)
    r = size * 0.16
    cx, cy = size * 0.62, size * 0.47
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 226, 140))

    def ridge(base: float, amp: float, freq: float, phase: float, color: tuple[int, int, int]) -> None:
        pts = [(0, size)]
        for i in range(65):
            x = i / 64 * size
            y = base + amp * (math.sin(x / size * freq + phase) + 0.5 * math.sin(x / size * freq * 2.7 + phase))
            pts.append((x, y))
        pts.append((size, size))
        d.polygon(pts, fill=color)

    ridge(size * 0.58, size * 0.07, 7.0, 0.4, (96, 60, 110))
    ridge(size * 0.68, size * 0.05, 11.0, 2.0, (44, 30, 70))
    d.rectangle([0, size * 0.8, size, size], fill=(30, 40, 90))
    for i in range(6):  # sun glints on the lake
        y = size * (0.83 + i * 0.025)
        w = size * (0.12 - i * 0.015)
        d.rectangle([cx - w, y, cx + w, y + size * 0.008], fill=(255, 200, 130))
    return img.filter(ImageFilter.GaussianBlur(size / 300))


PALETTE = [(26, 28, 44), (44, 46, 110), (93, 39, 93), (177, 62, 83), (239, 125, 87), (255, 205, 117)]


def pixelate(img: Image.Image, cell: int) -> Image.Image:
    small = img.resize((img.width // cell, img.height // cell), Image.BOX)
    pal = Image.new("P", (1, 1))
    flat = [c for rgb in PALETTE for c in rgb]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    q = small.quantize(palette=pal, dither=Image.Dither.NONE).convert("RGB")
    return q.resize(img.size, Image.NEAREST)


def app_icon() -> Image.Image:
    base = scene(S)
    pix = pixelate(base, 48)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).polygon([(S * 0.58, 0), (S, 0), (S, S), (S * 0.42, S)], fill=255)
    art = Image.composite(pix, base, mask)
    d = ImageDraw.Draw(art)
    d.line([(S * 0.58, 0), (S * 0.42, S)], fill=(255, 255, 255), width=S // 64)
    # macOS-style rounded square with margin.
    margin = S * 0.1
    inner = int(S - 2 * margin)
    art = art.resize((inner, inner), Image.LANCZOS)
    rmask = Image.new("L", (inner, inner), 0)
    ImageDraw.Draw(rmask).rounded_rectangle([0, 0, inner - 1, inner - 1], radius=inner * 0.225, fill=255)
    icon = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(
        [margin, margin + S * 0.015, S - margin, S - margin + S * 0.015], radius=inner * 0.225, fill=(0, 0, 0, 110)
    )
    icon.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(S * 0.012)))
    icon.paste(art, (int(margin), int(margin)), rmask)
    return icon


def template_icon(pt: int, scale: int) -> Image.Image:
    """Monochrome template: black + alpha only, macOS tints it for the menu bar."""
    n = pt * scale * 8  # draw big, downsample
    img = Image.new("LA", (n, n), (0, 0))
    d = ImageDraw.Draw(img)
    pad = n * 0.08
    w = n * 0.07
    d.rounded_rectangle([pad, pad, n - pad, n - pad], radius=n * 0.18, outline=(0, 255), width=int(w))
    # Left: smooth circle (the "photo").  Right: same circle as chunky pixels.
    cx, cy, r = n * 0.5, n * 0.5, n * 0.26
    left = Image.new("L", (n, n), 0)
    ImageDraw.Draw(left).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    cell = n // 9
    right = left.resize((n // cell, n // cell), Image.BOX).point(lambda v: 255 if v > 110 else 0)
    right = right.resize((n, n), Image.NEAREST)
    half = Image.new("L", (n, n), 0)
    ImageDraw.Draw(half).rectangle([n // 2, 0, n, n], fill=255)
    disc = Image.composite(right, left, half)
    gap = Image.new("L", (n, n), 255)
    ImageDraw.Draw(gap).rectangle([n // 2 - w * 0.4, 0, n // 2 + w * 0.4, n], fill=0)
    alpha = Image.composite(disc, Image.new("L", (n, n), 0), gap)
    img.paste((0, 255), mask=alpha)
    size = pt * scale
    return img.resize((size, size), Image.LANCZOS).convert("RGBA")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    icon = app_icon()
    icon.save(OUT / "AppIcon.icns", sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])
    icon.save(OUT / "delss.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    for n in (16, 32, 48, 64, 128, 256, 512):
        icon.resize((n, n), Image.LANCZOS).save(OUT / f"delss-{n}.png")
    template_icon(18, 1).save(OUT / "MenuBarTemplate.png")
    template_icon(18, 2).save(OUT / "MenuBarTemplate@2x.png")


if __name__ == "__main__":
    main()
