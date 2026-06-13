#!/usr/bin/env python3
"""
Generate Kiro expression GIFs in the style of the reference MP4s:
- Thick black outline, white fill
- Simple oval eyes
- Transparent background
- No watermark
- Minimal, clean animations

Output: assets/kimi_gif/*.gif
"""

from __future__ import annotations

import math
from pathlib import Path
from typing import Callable, List, Tuple

from PIL import Image, ImageDraw, ImageFilter, ImageOps

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
PROJECT_ROOT = Path(__file__).resolve().parent.parent
ASSETS_DIR = PROJECT_ROOT / "assets"
OUT_DIR = ASSETS_DIR / "kimi_gif"
SRC_PNG = ASSETS_DIR / "AWS_KIRO_Ghost_Outline.png"

CANVAS = 160
BG_TRANSPARENT = (0, 0, 0, 0)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

GHOST_TARGET_SIZE = (110, 132)  # fill the 160x160 circle nicely
OUTLINE_THICKNESS = 11  # pixels in the target size
FPS = 12


def load_ghost_mask() -> Image.Image:
    """
    Load the ghost PNG and return a binary mask of the white body fill.
    The source has white fill + black outline + black eyes on black bg.
    Thresholding keeps only the white fill.
    """
    src = Image.open(SRC_PNG).convert("L")
    src = src.resize(GHOST_TARGET_SIZE, Image.Resampling.LANCZOS)
    # White fill -> 255, everything else (outline/eyes/background) -> 0
    mask = src.point(lambda v: 255 if v > 200 else 0)
    return mask


def build_ghost_body(mask: Image.Image) -> Image.Image:
    """
    From the white-fill mask, build a thick-outline ghost body:
    - Dilate the mask to create the black outline
    - Fill original mask area with white
    - Return RGBA image with transparent background
    """
    # Dilate for outline
    dilate_size = OUTLINE_THICKNESS * 2 + 1
    outline_mask = mask.filter(ImageFilter.MaxFilter(dilate_size))

    body = Image.new("RGBA", GHOST_TARGET_SIZE, BG_TRANSPARENT)
    for y in range(GHOST_TARGET_SIZE[1]):
        for x in range(GHOST_TARGET_SIZE[0]):
            is_fill = mask.getpixel((x, y)) > 128
            is_outline = outline_mask.getpixel((x, y)) > 128
            if is_fill:
                body.putpixel((x, y), (*WHITE, 255))
            elif is_outline:
                body.putpixel((x, y), (*BLACK, 255))
    return body


def paste_ghost(
    canvas: Image.Image,
    body: Image.Image,
    offset: Tuple[int, int] = (0, 0),
    scale: float = 1.0,
    rotate: float = 0.0,
) -> Image.Image:
    """Center-paste the ghost body onto canvas."""
    if scale != 1.0:
        w, h = body.size
        body = body.resize((int(w * scale), int(h * scale)), Image.Resampling.LANCZOS)
    if rotate != 0.0:
        body = body.rotate(rotate, resample=Image.Resampling.BICUBIC, expand=False)

    cx, cy = CANVAS // 2, CANVAS // 2
    x = cx - body.width // 2 + offset[0]
    y = cy - body.height // 2 + offset[1]
    canvas.paste(body, (x, y), body)
    return body  # returned just for size info if needed


def draw_oval_eye(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    w: int,
    h: int,
    style: str = "open",
) -> None:
    """Draw simple oval eyes matching the reference style."""
    if style == "open":
        draw.ellipse([cx - w, cy - h, cx + w, cy + h], fill=BLACK)
    elif style == "closed":
        # Flat horizontal line
        draw.line([(cx - w, cy), (cx + w, cy)], fill=BLACK, width=max(2, h // 2))
    elif style == "wink":
        # Small upward curve / closed eye
        draw.arc([cx - w, cy - h, cx + w, cy + h], start=200, end=340, fill=BLACK, width=max(2, h // 2))
    elif style == "tired":
        # Half-closed droopy eye: black oval with a white eyelid covering the top half
        draw.ellipse([cx - w, cy - h, cx + w, cy + h], fill=BLACK)
        draw.pieslice([cx - w - 1, cy - h - 4, cx + w + 1, cy + h - 2],
                      start=0, end=180, fill=WHITE)


def draw_question_mark(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    size: int,
) -> None:
    """Draw a chunky, clearly readable question mark."""
    t = max(4, size // 4)
    cx = x + size // 2
    # Top hook: thick arc, leaving a gap at the bottom-left
    hook_r = size // 2 - t
    hook_cy = y + hook_r + t // 2
    bbox = [cx - hook_r, hook_cy - hook_r, cx + hook_r, hook_cy + hook_r]
    draw.arc(bbox, start=40, end=280, fill=BLACK, width=t)
    # Stem: vertical bar from hook down
    stem_top = hook_cy + int(hook_r * 0.6)
    stem_bottom = y + size - t * 2
    draw.line([(cx, stem_top), (cx, stem_bottom)], fill=BLACK, width=t)
    # Dot
    dot_y = y + size - t
    draw.ellipse([cx - t, dot_y - t, cx + t, dot_y + t], fill=BLACK)


def draw_z(draw: ImageDraw.ImageDraw, x: int, y: int, size: int) -> None:
    """Draw a chunky Z."""
    w, h = size, int(size * 1.1)
    t = max(2, size // 4)
    draw.line([(x, y), (x + w, y)], fill=BLACK, width=t)
    draw.line([(x + w, y), (x, y + h)], fill=BLACK, width=t)
    draw.line([(x, y + h), (x + w, y + h)], fill=BLACK, width=t)


def draw_gear(draw: ImageDraw.ImageDraw, cx: int, cy: int, r: int, teeth: int = 6) -> None:
    """Draw a simple gear icon."""
    outer = r
    inner = r - max(2, r // 4)
    pts = []
    for i in range(teeth * 2):
        angle = i * math.pi / teeth
        rad = outer if i % 2 == 0 else inner
        pts.append((cx + int(rad * math.cos(angle)), cy + int(rad * math.sin(angle))))
    draw.polygon(pts, outline=BLACK, fill=WHITE)
    draw.ellipse([cx - r // 3, cy - r // 3, cx + r // 3, cy + r // 3], outline=BLACK, width=max(1, r // 5))


def draw_lightning(draw: ImageDraw.ImageDraw, x: int, y: int, size: int) -> None:
    """Draw a simple lightning bolt."""
    pts = [(x, y), (x + size // 2, y), (x, y + size), (x + size, y + size),
           (x + size // 2, y + size), (x + size, y)]
    draw.polygon(pts, fill=BLACK)


# ---------------------------------------------------------------------------
# Scenes
# ---------------------------------------------------------------------------
EYE_LX = -16
EYE_RX = 16
EYE_Y = -10
EYE_W = 7
EYE_H = 10


def scene_idle(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 3)
        # Blink once per loop
        phase = (t * 2) % 1.0
        blink = 0.0
        if 0.82 < phase < 0.92:
            blink = (phase - 0.82) / 0.10
        elif phase >= 0.92:
            blink = 1.0 - (phase - 0.92) / 0.08
        blink = max(0.0, min(1.0, blink))

        img = Image.new("RGBA", (CANVAS, CANVAS), BG_TRANSPARENT)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        style = "closed" if blink > 0.5 else "open"
        draw_oval_eye(draw, CANVAS // 2 + EYE_LX, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, style)
        draw_oval_eye(draw, CANVAS // 2 + EYE_RX, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, style)
        frames.append(img)
    return frames


def scene_sleep(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 30
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG_TRANSPARENT)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        draw_oval_eye(draw, CANVAS // 2 + EYE_LX, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, "closed")
        draw_oval_eye(draw, CANVAS // 2 + EYE_RX, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, "closed")

        # Floating Zzz
        for j, phase in enumerate([0.0, 0.30, 0.60]):
            zt = (t + phase) % 1.0
            x = CANVAS // 2 + 28 + int(math.sin(zt * 3 * math.pi) * 3)
            y = CANVAS // 2 - 60 - int(zt * 24)
            size = 12 + int(zt * 6)
            if zt > 0.85:
                continue
            draw_z(draw, x, y, size)
        frames.append(img)
    return frames


def scene_work_focus(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG_TRANSPARENT)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        # narrowed focused eyes
        draw_oval_eye(draw, CANVAS // 2 + EYE_LX, CANVAS // 2 + EYE_Y + y_off, EYE_W, 6, "open")
        draw_oval_eye(draw, CANVAS // 2 + EYE_RX, CANVAS // 2 + EYE_Y + y_off, EYE_W, 6, "open")

        # Brackets floating
        pairs = [("<", ">"), ("[", "]")]
        for idx, (left, right) in enumerate(pairs):
            phase = (t + idx * 0.25) % 1.0
            y_base = CANVAS // 2 - 18 + idx * 16
            x_l = 12 + int(math.sin(phase * 2 * math.pi) * 4)
            x_r = CANVAS - 22 - int(math.sin(phase * 2 * math.pi) * 4)
            y = y_base + int(math.cos((phase + idx) * 2 * math.pi) * 2)
            # Draw chunky brackets with lines
            h = 14
            w = 7
            t_w = max(2, h // 6)
            if left == "<":
                draw.line([(x_l + w, y), (x_l, y + h // 2)], fill=BLACK, width=t_w)
                draw.line([(x_l, y + h // 2), (x_l + w, y + h)], fill=BLACK, width=t_w)
            if right == ">":
                draw.line([(x_r, y), (x_r + w, y + h // 2)], fill=BLACK, width=t_w)
                draw.line([(x_r + w, y + h // 2), (x_r, y + h)], fill=BLACK, width=t_w)
            if left == "[":
                draw.line([(x_l + w, y), (x_l, y), (x_l, y + h), (x_l + w, y + h)], fill=BLACK, width=t_w)
            if right == "]":
                draw.line([(x_r, y), (x_r + w, y), (x_r + w, y + h), (x_r, y + h)], fill=BLACK, width=t_w)
        frames.append(img)
    return frames


def scene_work_busy(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    for i in range(n):
        t = i / n
        jitter_x = int(math.sin(t * 6 * math.pi) * 1.5)
        jitter_y = int(math.cos(t * 6 * math.pi) * 1)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG_TRANSPARENT)
        paste_ghost(img, body, offset=(jitter_x, jitter_y))
        draw = ImageDraw.Draw(img)
        # Tired eyes
        draw_oval_eye(draw, CANVAS // 2 + EYE_LX + jitter_x, CANVAS // 2 + EYE_Y + jitter_y, EYE_W, EYE_H, "tired")
        draw_oval_eye(draw, CANVAS // 2 + EYE_RX + jitter_x, CANVAS // 2 + EYE_Y + jitter_y, EYE_W, EYE_H, "tired")

        # Gears and lightning around
        positions = [
            (CANVAS // 2 - 52, CANVAS // 2 - 45, 10),
            (CANVAS // 2 + 50, CANVAS // 2 - 40, 8),
            (CANVAS // 2 - 48, CANVAS // 2 + 42, 8),
            (CANVAS // 2 + 52, CANVAS // 2 + 38, 10),
        ]
        for idx, (gx, gy, gr) in enumerate(positions):
            phase = (t + idx * 0.2) % 1.0
            angle = phase * 2 * math.pi
            r_offset = 4
            ox = int(r_offset * math.cos(angle))
            oy = int(r_offset * math.sin(angle))
            draw_gear(draw, gx + ox, gy + oy, gr)
            # lightning between gears and ghost
            lx = gx - int(8 * math.cos(angle))
            ly = gy - int(8 * math.sin(angle))
            draw_lightning(draw, lx, ly, 8)
        frames.append(img)
    return frames


def scene_wait_look(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    for i in range(n):
        t = i / n
        lx = math.cos(t * 2 * math.pi)
        lean = lx * 5
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG_TRANSPARENT)
        paste_ghost(img, body, offset=(int(lean), y_off), rotate=lean * 0.5)
        draw = ImageDraw.Draw(img)
        # Eyes look in direction
        eye_y_offset = int(EYE_H * 0.35 * 0)  # keep vertical center
        eye_x_offset = int(EYE_W * 0.4 * lx)
        draw_oval_eye(draw, CANVAS // 2 + EYE_LX + int(lean) + eye_x_offset, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, "open")
        draw_oval_eye(draw, CANVAS // 2 + EYE_RX + int(lean) + eye_x_offset, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, "open")
        frames.append(img)
    return frames


def scene_wait_expect(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 20
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        tilt = math.sin(t * 2 * math.pi) * 5
        img = Image.new("RGBA", (CANVAS, CANVAS), BG_TRANSPARENT)
        paste_ghost(img, body, offset=(0, y_off), rotate=tilt)
        draw = ImageDraw.Draw(img)
        draw_oval_eye(draw, CANVAS // 2 + EYE_LX, CANVAS // 2 + EYE_Y + y_off, EYE_W + 1, EYE_H + 1, "open")
        draw_oval_eye(draw, CANVAS // 2 + EYE_RX, CANVAS // 2 + EYE_Y + y_off, EYE_W + 1, EYE_H + 1, "open")

        # Bouncing question mark centered above head
        size = 28
        qx = CANVAS // 2 - size // 2
        bounce = abs(math.sin(t * 2 * math.pi)) * 8
        qy = CANVAS // 2 - 66 - int(bounce)
        draw_question_mark(draw, qx, qy, size)
        frames.append(img)
    return frames


def scene_wait_listen(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG_TRANSPARENT)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        # Eyes look right
        draw_oval_eye(draw, CANVAS // 2 + EYE_LX + 3, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, "open")
        draw_oval_eye(draw, CANVAS // 2 + EYE_RX + 3, CANVAS // 2 + EYE_Y + y_off, EYE_W, EYE_H, "open")

        # Sound waves on the right
        for j in range(3):
            phase = (t + j * 0.15) % 1.0
            r = 16 + int(phase * 16)
            cx = CANVAS // 2 + 46
            cy = CANVAS // 2 - 18 + y_off
            draw.arc([cx - r, cy - r, cx + r, cy + r], start=-60, end=60, fill=BLACK, width=2)
        # Little ear cup
        draw.ellipse([CANVAS // 2 + 38, CANVAS // 2 - 28 + y_off,
                      CANVAS // 2 + 50, CANVAS // 2 - 8 + y_off], outline=BLACK, width=2)
        frames.append(img)
    return frames


# ---------------------------------------------------------------------------
# GIF output with transparency
# ---------------------------------------------------------------------------
def save_transparent_gif(frames: List[Image.Image], path: Path, duration_ms: int = 1000 // FPS) -> None:
    """Save RGBA frames as an animated GIF with transparent background."""
    # Palette: index 0 = transparent (magenta placeholder), 1 = black, 2 = white
    palette = [255, 0, 255, 0, 0, 0, 255, 255, 255]
    palette += [0] * (768 - len(palette))

    def convert_frame(f: Image.Image) -> Image.Image:
        rgba = f.convert("RGBA")
        out = Image.new("P", f.size, 0)
        for y in range(f.size[1]):
            for x in range(f.size[0]):
                r, g, b, a = rgba.getpixel((x, y))
                if a < 128:
                    idx = 0
                elif r > 200 and g > 200 and b > 200:
                    idx = 2
                else:
                    idx = 1
                out.putpixel((x, y), idx)
        out.putpalette(palette)
        return out

    quantized = [convert_frame(f) for f in frames]

    quantized[0].save(
        path,
        save_all=True,
        append_images=quantized[1:],
        duration=duration_ms,
        loop=0,
        transparency=0,
        disposal=2,
        optimize=True,
    )


SCENES: List[Tuple[str, Callable[[Image.Image], List[Image.Image]]]] = [
    ("1_idle.gif", scene_idle),
    ("2_sleep.gif", scene_sleep),
    ("3_work_focus.gif", scene_work_focus),
    ("4_work_busy.gif", scene_work_busy),
    ("5_wait_look.gif", scene_wait_look),
    ("6_wait_expect.gif", scene_wait_expect),
    ("7_wait_listen.gif", scene_wait_listen),
]


def main() -> None:
    if not SRC_PNG.exists():
        raise FileNotFoundError(f"Source PNG not found: {SRC_PNG}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)

    mask = load_ghost_mask()
    body = build_ghost_body(mask)
    print(f"Loaded ghost body: {body.size}, outline thickness: {OUTLINE_THICKNESS}px")

    for filename, scene_fn in SCENES:
        frames = scene_fn(body)
        out_path = OUT_DIR / filename
        save_transparent_gif(frames, out_path)
        print(f"Generated {filename}: {len(frames)} frames, {out_path.stat().st_size // 1024} KB")

    print("Done. Output at:", OUT_DIR)


if __name__ == "__main__":
    main()
