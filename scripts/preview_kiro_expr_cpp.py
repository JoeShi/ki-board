#!/usr/bin/env python3
"""
Preview the compiled RGB565 frame data embedded in src/kiro_expr_*.cpp.

Usage:
    python scripts/preview_kiro_expr_cpp.py [idle|wait|work]

Output:
    assets/preview_from_cpp/<expr>/frame_00.png ... frame_39.png
    assets/preview_from_cpp/<expr>/preview.gif
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import List, Tuple

from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = PROJECT_ROOT / "src"
OUT_DIR = PROJECT_ROOT / "assets" / "preview_from_cpp"

FRAME_W = 120
FRAME_H = 120
FRAME_COUNT = 40


def rgb565_to_rgb888(value: int) -> Tuple[int, int, int]:
    """Convert a 16-bit RGB565 value to an (R, G, B) tuple."""
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    # Scale to 8-bit
    r = (r * 255 + 15) // 31
    g = (g * 255 + 31) // 63
    b = (b * 255 + 15) // 31
    return (r, g, b)


def parse_cpp_frames(path: Path) -> List[List[int]]:
    """Parse the big uint16_t array in a kiro_expr_*.cpp file."""
    text = path.read_text(encoding="utf-8")
    # Remove C comments
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    # Find all hex literals
    values = [int(v, 16) for v in re.findall(r"\b0x([0-9A-Fa-f]+)\b", text)]

    pixels_per_frame = FRAME_W * FRAME_H
    expected = FRAME_COUNT * pixels_per_frame
    if len(values) != expected:
        print(
            f"Warning: expected {expected} pixel values, found {len(values)}",
            file=sys.stderr,
        )

    frames: List[List[int]] = []
    for i in range(FRAME_COUNT):
        start = i * pixels_per_frame
        end = start + pixels_per_frame
        frames.append(values[start:end])
    return frames


def render_frame(pixels: List[int]) -> Image.Image:
    """Render one frame's raw pixels to an RGB image."""
    img = Image.new("RGB", (FRAME_W, FRAME_H))
    rgb_pixels = [rgb565_to_rgb888(v) for v in pixels]
    img.putdata(rgb_pixels)
    return img


def save_preview(frames: List[Image.Image], out_dir: Path, fps: int = 4) -> None:
    """Save PNG frames and an animated GIF preview."""
    out_dir.mkdir(parents=True, exist_ok=True)

    for i, frame in enumerate(frames):
        frame_path = out_dir / f"frame_{i:02d}.png"
        frame.save(frame_path)

    gif_path = out_dir / "preview.gif"
    duration_ms = int(1000 / fps)
    frames[0].save(
        gif_path,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=True,
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Preview kiro_expr_*.cpp frame data")
    parser.add_argument(
        "expression",
        choices=["idle", "wait", "work"],
        default="idle",
        nargs="?",
        help="Which expression file to preview",
    )
    args = parser.parse_args()

    src_file = SRC_DIR / f"kiro_expr_{args.expression}.cpp"
    if not src_file.exists():
        print(f"Source file not found: {src_file}", file=sys.stderr)
        sys.exit(1)

    print(f"Parsing {src_file} ...")
    raw_frames = parse_cpp_frames(src_file)
    frames = [render_frame(pixels) for pixels in raw_frames]

    output_dir = OUT_DIR / args.expression
    save_preview(frames, output_dir)
    print(f"Saved {len(frames)} frames to {output_dir}")
    print(f"Animated preview: {output_dir / 'preview.gif'}")


if __name__ == "__main__":
    main()
