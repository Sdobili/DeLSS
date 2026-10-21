#!/usr/bin/env python3
"""Render the README gallery with the C++ CLI (same shaders as the web demo).

For every preset and input this writes docs/gallery/<input>_<preset>.jpg as a
before/after split (left half original, right half stylized), plus a contact
grid. Requires a build of delss-cli (cmake -S cpp -B build && cmake --build build)
and docs/gallery/input/scene_*.png (tools/make-fixtures.mjs).

Usage: tools/gallery.py [--grid path/to/grid.png]
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build" / "delss-cli"
OUT = ROOT / "docs" / "gallery"
INPUTS = {
    "knight": {"color": ROOT / "web/samples/knight.jpg"},
    "street": {"color": ROOT / "web/samples/street.jpg"},
    "cat": {"color": ROOT / "web/samples/cat.jpg"},
    "scene": {
        "color": OUT / "input/scene_color.png",
        "depth": OUT / "input/scene_depth.png",
        "normal": OUT / "input/scene_normal.png",
    },
}
CELL = (480, 270)
FONT_PATHS = [
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/System/Library/Fonts/PingFang.ttc",
]


def font(size: int) -> ImageFont.ImageFont:
    for path in FONT_PATHS:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def render(preset: str, inputs: dict[str, Path], dest: Path) -> None:
    color = inputs["color"]
    with tempfile.TemporaryDirectory() as tmp:
        if color.suffix != ".png":
            png = Path(tmp) / "in.png"
            Image.open(color).convert("RGB").resize((960, 540), Image.LANCZOS).save(png)
            color = png
        cmd = [str(CLI), "--preset", preset, "--frames", "2"]
        for key in ("depth", "normal"):
            if key in inputs:
                cmd += [f"--{key}", str(inputs[key])]
        subprocess.run([*cmd, str(color), str(dest)], check=True, capture_output=True)


def split(before: Image.Image, after: Image.Image) -> Image.Image:
    out = after.copy()
    w, h = out.size
    out.paste(before.crop((0, 0, w // 2, h)), (0, 0))
    ImageDraw.Draw(out).line([(w // 2, 0), (w // 2, h)], fill=(255, 204, 51), width=3)
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--grid", type=Path, default=OUT / "grid.jpg")
    args = parser.parse_args()
    presets = json.loads((ROOT / "shaders/presets.json").read_text())
    OUT.mkdir(parents=True, exist_ok=True)

    cols = list(INPUTS)
    header = 44
    label_w = 230
    grid = Image.new("RGB", (label_w + CELL[0] * len(cols), header + CELL[1] * (len(presets) + 1)), "#101014")
    draw = ImageDraw.Draw(grid)
    big, small = font(26), font(18)
    draw.text((16, 8), "DeLSS", fill="#ffcc33", font=big)
    for c, name in enumerate(cols):
        draw.text((label_w + c * CELL[0] + 12, 10), name, fill="#e8e6e3", font=small)

    with tempfile.TemporaryDirectory() as tmp:
        originals = {}
        for c, name in enumerate(cols):
            img = Image.open(INPUTS[name]["color"]).convert("RGB").resize((960, 540), Image.LANCZOS)
            originals[name] = img
            grid.paste(img.resize(CELL, Image.LANCZOS), (label_w + c * CELL[0], header))
        draw.text((16, header + CELL[1] // 2 - 14), "原图 Original", fill="#e8e6e3", font=big)

        for r, preset in enumerate(presets, start=1):
            y = header + r * CELL[1]
            draw.text((16, y + CELL[1] // 2 - 30), preset["label"].split(" / ")[-1], fill="#ffcc33", font=big)
            draw.text((16, y + CELL[1] // 2 + 6), preset["id"], fill="#9a98a3", font=small)
            for c, name in enumerate(cols):
                dest = Path(tmp) / f"{name}_{preset['id']}.png"
                render(preset["id"], INPUTS[name], dest)
                styled = Image.open(dest).convert("RGB")
                split(originals[name], styled).save(OUT / f"{name}_{preset['id']}.jpg", quality=80)
                grid.paste(styled.resize(CELL, Image.LANCZOS), (label_w + c * CELL[0], y))
            print(f"rendered {preset['id']}")
    args.grid.parent.mkdir(parents=True, exist_ok=True)
    grid.save(args.grid, quality=82) if args.grid.suffix == ".jpg" else grid.save(args.grid)
    print(f"wrote {args.grid}")


if __name__ == "__main__":
    main()
