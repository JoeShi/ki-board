#!/usr/bin/env python3
"""
Generate polished Kiro expression GIFs for the round LCD (160x160).

The script loads the high-res ghost outline PNG, extracts the body silhouette
(eyes are black in the source, so they drop out naturally), resizes it with
anti-aliasing, then draws custom cute expressions and animations on top.

Output: assets/expressions_preview/*.gif
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
OUT_DIR = ASSETS_DIR / "expressions_preview"
BACKUP_DIR = ASSETS_DIR / "expressions_preview_backup"
SRC_PNG = ASSETS_DIR / "AWS_KIRO_Ghost_Outline.png"

CANVAS = 160
BG = (0, 0, 0)
GHOST_COLOR = (255, 255, 255)
GHOST_SIZE = (104, 124)  # fits comfortably in a 160x160 circle

# Accent colors for various states
CYAN = (64, 224, 208)
BLUE = (96, 160, 255)
YELLOW = (255, 220, 96)
PINK = (255, 140, 180)
GREEN = (120, 220, 120)

# Animation timing
FPS = 12


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def load_ghost_body() -> Image.Image:
    """Load the ghost PNG and return a clean white body without eyes."""
    src = Image.open(SRC_PNG).convert("L")
    # The source is a white-filled ghost with black outline/eyes on black bg.
    # We want the white fill as the body. Use grayscale directly as alpha
    # so white areas become opaque and black areas transparent.
    src = src.resize(GHOST_SIZE, Image.Resampling.LANCZOS)

    # Slight contrast boost: the black stroke becomes transparent, white fill opaque
    alpha = src.point(lambda v: 0 if v < 60 else min(255, int((v - 60) / 195 * 255 + 50)))

    body = Image.new("RGBA", GHOST_SIZE, (0, 0, 0, 0))
    for y in range(GHOST_SIZE[1]):
        for x in range(GHOST_SIZE[0]):
            a = alpha.getpixel((x, y))
            if a > 0:
                body.putpixel((x, y), (*GHOST_COLOR, a))
    return body


def paste_ghost(
    canvas: Image.Image,
    body: Image.Image,
    offset: Tuple[int, int] = (0, 0),
    scale: float = 1.0,
    rotate: float = 0.0,
) -> Tuple[int, int]:
    """Paste the ghost body onto canvas, centered + offset. Returns top-left."""
    if scale != 1.0 or rotate != 0.0:
        w, h = body.size
        new_w, new_h = int(w * scale), int(h * scale)
        scaled = body.resize((new_w, new_h), Image.Resampling.LANCZOS)
        if rotate != 0.0:
            scaled = scaled.rotate(rotate, resample=Image.Resampling.BICUBIC, expand=False)
    else:
        scaled = body

    cx, cy = CANVAS // 2, CANVAS // 2
    x = cx - scaled.width // 2 + offset[0]
    y = cy - scaled.height // 2 + offset[1]
    canvas.paste(scaled, (x, y), scaled)
    return (x, y)


def draw_cute_eye(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    width: int,
    height: int,
    look: Tuple[float, float] = (0.0, 0.0),
    blink: float = 0.0,
    happy: bool = False,
    color: Tuple[int, int, int] = (0, 0, 0),
) -> None:
    """
    Draw a cute oval eye.
    blink: 0 = open, 1 = closed
    look: (dx, dy) gaze direction in [-1, 1]
    happy: draw an upward curve instead of an oval
    """
    if happy:
        # Arc as closed happy eye
        draw.arc(
            [cx - width, cy - height, cx + width, cy + height],
            start=200,
            end=340,
            fill=color,
            width=max(2, width // 3),
        )
        return

    if blink > 0.85:
        # Fully closed flat line
        draw.line(
            [(cx - width, cy), (cx + width, cy)],
            fill=color,
            width=max(2, height // 4),
        )
        return

    # Interpolate height for blinking
    h = int(height * (1 - blink))
    if h < 2:
        draw.line(
            [(cx - width, cy), (cx + width, cy)],
            fill=color,
            width=max(2, height // 4),
        )
        return

    # Eye white / background
    draw.ellipse([cx - width, cy - h, cx + width, cy + h], fill=color)

    # Pupil + highlight
    pupil_r = max(2, width // 3)
    px = cx + int(width * 0.35 * look[0])
    py = cy + int(h * 0.25 * look[1])
    draw.ellipse([px - pupil_r, py - pupil_r, px + pupil_r, py + pupil_r], fill=BG)

    highlight_r = max(1, pupil_r // 2)
    hx = cx - width // 3 + int(width * 0.15 * look[0])
    hy = cy - h // 3 + int(h * 0.15 * look[1])
    draw.ellipse([hx - highlight_r, hy - highlight_r, hx + highlight_r, hy + highlight_r], fill=GHOST_COLOR)


def draw_mouth(
    draw: ImageDraw.ImageDraw,
    cx: int,
    cy: int,
    style: str = "small_o",
    color: Tuple[int, int, int] = (0, 0, 0),
) -> None:
    """Draw a small mouth."""
    if style == "small_o":
        draw.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=color)
    elif style == "smile":
        draw.arc([cx - 8, cy - 6, cx + 8, cy + 6], start=200, end=340, fill=color, width=2)
    elif style == "open":
        draw.ellipse([cx - 5, cy - 6, cx + 5, cy + 6], fill=color)
    elif style == "wavy":
        # little wavy mouth for sleep
        pts = [(cx - 8, cy), (cx - 4, cy + 2), (cx, cy), (cx + 4, cy + 2), (cx + 8, cy)]
        draw.line(pts, fill=color, width=2)


def draw_z(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    size: int,
    color: Tuple[int, int, int],
) -> None:
    """Draw a chunky Z shape."""
    w, h = size, int(size * 1.1)
    draw.line([(x, y), (x + w, y)], fill=color, width=max(2, size // 4))
    draw.line([(x + w, y), (x, y + h)], fill=color, width=max(2, size // 4))
    draw.line([(x, y + h), (x + w, y + h)], fill=color, width=max(2, size // 4))


def draw_question_mark(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    size: int,
    color: Tuple[int, int, int],
) -> None:
    """Draw a chunky question mark."""
    r = size // 2
    cx, cy = x + r, y + r
    draw.arc([cx - r, cy - r, cx + r, cy], start=60, end=270, fill=color, width=max(2, size // 5))
    draw.line([(cx - r, cy), (cx - r, cy + r)], fill=color, width=max(2, size // 5))
    dot_y = cy + r + size // 4
    draw.ellipse([cx - size // 6, dot_y - size // 6, cx + size // 6, dot_y + size // 6], fill=color)


def draw_bracket(
    draw: ImageDraw.ImageDraw,
    x: int,
    y: int,
    h: int,
    which: str,
    color: Tuple[int, int, int],
) -> None:
    """Draw a chunky bracket: '<', '>', '[', ']', '{', '}'."""
    w = h // 2
    t = max(2, h // 8)
    if which == "<":
        draw.line([(x + w, y), (x, y + h // 2)], fill=color, width=t)
        draw.line([(x, y + h // 2), (x + w, y + h)], fill=color, width=t)
    elif which == ">":
        draw.line([(x, y), (x + w, y + h // 2)], fill=color, width=t)
        draw.line([(x + w, y + h // 2), (x, y + h)], fill=color, width=t)
    elif which == "[":
        draw.line([(x + w, y), (x, y), (x, y + h), (x + w, y + h)], fill=color, width=t)
    elif which == "]":
        draw.line([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], fill=color, width=t)
    elif which == "{":
        mid = y + h // 2
        draw.line([(x + w, y), (x + w // 2, y), (x + w // 2, mid - h // 8),
                   (x, mid), (x + w // 2, mid + h // 8),
                   (x + w // 2, y + h), (x + w, y + h)], fill=color, width=t)
    elif which == "}":
        mid = y + h // 2
        draw.line([(x, y), (x + w // 2, y), (x + w // 2, mid - h // 8),
                   (x + w, mid), (x + w // 2, mid + h // 8),
                   (x + w // 2, y + h), (x, y + h)], fill=color, width=t)


# ---------------------------------------------------------------------------
# Expression scene renderers
# Each returns a list of RGBA frames.
# ---------------------------------------------------------------------------

def scene_idle(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    eye_cx_left = CANVAS // 2 - 16
    eye_cx_right = CANVAS // 2 + 16
    eye_cy = CANVAS // 2 - 12
    for i in range(n):
        t = i / n
        # Gentle bob
        y_off = int(math.sin(t * 2 * math.pi) * 3)
        # Occasional blink every ~3 seconds
        phase = (t * 2) % 1.0
        blink = 0.0
        if 0.85 < phase < 0.92:
            blink = (phase - 0.85) / 0.07
        elif phase >= 0.92:
            blink = 1.0 - (phase - 0.92) / 0.08
        blink = max(0.0, min(1.0, blink))

        img = Image.new("RGBA", (CANVAS, CANVAS), BG)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        draw_cute_eye(draw, eye_cx_left, eye_cy + y_off, 8, 11, blink=blink)
        draw_cute_eye(draw, eye_cx_right, eye_cy + y_off, 8, 11, blink=blink)
        draw_mouth(draw, CANVAS // 2, CANVAS // 2 + 6 + y_off, style="small_o")
        frames.append(img)
    return frames


def scene_sleep(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 36
    eye_cx_left = CANVAS // 2 - 15
    eye_cx_right = CANVAS // 2 + 15
    eye_cy = CANVAS // 2 - 10
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        draw_cute_eye(draw, eye_cx_left, eye_cy + y_off, 8, 11, happy=True)
        draw_cute_eye(draw, eye_cx_right, eye_cy + y_off, 8, 11, happy=True)
        draw_mouth(draw, CANVAS // 2, CANVAS // 2 + 8 + y_off, style="wavy")

        # Floating Zzz
        for j, phase in enumerate([0.0, 0.30, 0.60]):
            zt = (t + phase) % 1.0
            x = CANVAS // 2 + 26 + int(math.sin(zt * 3 * math.pi) * 4)
            y = CANVAS // 2 - 62 - int(zt * 24)
            size = 14 + int(zt * 8)
            # Fade out near the top
            if zt > 0.85:
                continue
            draw_z(draw, x, y, size, GHOST_COLOR)
        frames.append(img)
    return frames


def scene_work_focus(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    eye_cx_left = CANVAS // 2 - 15
    eye_cx_right = CANVAS // 2 + 15
    eye_cy = CANVAS // 2 - 12
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        # Focused / narrowed eyes looking slightly down
        draw_cute_eye(draw, eye_cx_left, eye_cy + y_off, 8, 7, look=(0.0, 0.3))
        draw_cute_eye(draw, eye_cx_right, eye_cy + y_off, 8, 7, look=(0.0, 0.3))
        draw_mouth(draw, CANVAS // 2, CANVAS // 2 + 8 + y_off, style="small_o")

        # Floating code brackets
        bracket_pairs = [("<", ">"), ("[", "]"), ("{", "}")]
        for idx, (left, right) in enumerate(bracket_pairs):
            phase = (t + idx * 0.22) % 1.0
            y_base = CANVAS // 2 - 22 + idx * 18
            x_left = 10 + int(math.sin(phase * 2 * math.pi) * 5)
            x_right = CANVAS - 24 - int(math.sin(phase * 2 * math.pi) * 5)
            y = y_base + int(math.cos((phase + idx) * 2 * math.pi) * 3)
            fade = 0.6 + 0.4 * math.sin(phase * math.pi)
            c = (int(CYAN[0] * fade), int(CYAN[1] * fade), int(CYAN[2] * fade))
            draw_bracket(draw, x_left, y, 16, left, c)
            draw_bracket(draw, x_right, y, 16, right, c)
        frames.append(img)
    return frames


def scene_work_busy(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 18
    eye_cx_left = CANVAS // 2 - 15
    eye_cx_right = CANVAS // 2 + 15
    eye_cy = CANVAS // 2 - 12
    for i in range(n):
        t = i / n
        # Fast jitter
        jitter_x = int(math.sin(t * 8 * math.pi) * 2)
        jitter_y = int(math.cos(t * 8 * math.pi) * 1)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG)
        paste_ghost(img, body, offset=(jitter_x, jitter_y), rotate=math.sin(t * 4 * math.pi) * 3)
        draw = ImageDraw.Draw(img)
        draw_cute_eye(draw, eye_cx_left + jitter_x, eye_cy + jitter_y, 8, 11)
        draw_cute_eye(draw, eye_cx_right + jitter_x, eye_cy + jitter_y, 8, 11)
        draw_mouth(draw, CANVAS // 2 + jitter_x, CANVAS // 2 + 8 + jitter_y, style="open")

        # Speed lines on the left
        for k in range(3):
            y = 50 + k * 28 + int(math.sin((t + k) * 2 * math.pi) * 4)
            lw = 18 + int(math.sin((t + k * 0.3) * 2 * math.pi) * 6)
            draw.line([(8, y), (8 + lw, y)], fill=(200, 200, 200), width=2)

        # Sweat drop
        sx = CANVAS // 2 + 36
        sy = CANVAS // 2 - 38 + int(math.sin(t * 4 * math.pi) * 2)
        draw.polygon([(sx, sy), (sx + 6, sy + 12), (sx - 6, sy + 12)], fill=BLUE, outline=GHOST_COLOR)
        frames.append(img)
    return frames


def scene_wait_look(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    eye_cx_left = CANVAS // 2 - 15
    eye_cx_right = CANVAS // 2 + 15
    eye_cy = CANVAS // 2 - 12
    for i in range(n):
        t = i / n
        # Smooth look left -> right -> left using cosine
        lx = math.cos(t * 2 * math.pi)  # 1 -> -1 -> 1

        # Body leans slightly in look direction
        lean = lx * 5
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG)
        paste_ghost(img, body, offset=(int(lean), y_off), rotate=lean * 0.5)
        draw = ImageDraw.Draw(img)
        draw_cute_eye(draw, eye_cx_left + int(lean), eye_cy + y_off, 8, 11, look=(lx, 0.0))
        draw_cute_eye(draw, eye_cx_right + int(lean), eye_cy + y_off, 8, 11, look=(lx, 0.0))
        draw_mouth(draw, CANVAS // 2 + int(lean), CANVAS // 2 + 8 + y_off, style="small_o")
        frames.append(img)
    return frames


def scene_wait_expect(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 20
    eye_cx_left = CANVAS // 2 - 15
    eye_cx_right = CANVAS // 2 + 15
    eye_cy = CANVAS // 2 - 12
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        tilt = math.sin(t * 2 * math.pi) * 5
        img = Image.new("RGBA", (CANVAS, CANVAS), BG)
        paste_ghost(img, body, offset=(0, y_off), rotate=tilt)
        draw = ImageDraw.Draw(img)
        # Big curious eyes
        draw_cute_eye(draw, eye_cx_left, eye_cy + y_off, 9, 12, look=(0.0, -0.2))
        draw_cute_eye(draw, eye_cx_right, eye_cy + y_off, 9, 12, look=(0.0, -0.2))
        draw_mouth(draw, CANVAS // 2, CANVAS // 2 + 8 + y_off, style="small_o")

        # Bouncing question mark
        qx = CANVAS // 2 + 30
        bounce = abs(math.sin(t * 2 * math.pi)) * 8
        qy = CANVAS // 2 - 60 - int(bounce)
        draw_question_mark(draw, qx, qy, 20, GHOST_COLOR)
        frames.append(img)
    return frames


def scene_wait_listen(body: Image.Image) -> List[Image.Image]:
    frames: List[Image.Image] = []
    n = 24
    eye_cx_left = CANVAS // 2 - 15
    eye_cx_right = CANVAS // 2 + 15
    eye_cy = CANVAS // 2 - 12
    for i in range(n):
        t = i / n
        y_off = int(math.sin(t * 2 * math.pi) * 2)
        img = Image.new("RGBA", (CANVAS, CANVAS), BG)
        paste_ghost(img, body, offset=(0, y_off))
        draw = ImageDraw.Draw(img)
        draw_cute_eye(draw, eye_cx_left, eye_cy + y_off, 8, 11, look=(0.3, 0.0))
        draw_cute_eye(draw, eye_cx_right, eye_cy + y_off, 8, 11, look=(0.3, 0.0))
        draw_mouth(draw, CANVAS // 2, CANVAS // 2 + 8 + y_off, style="small_o")

        # Sound waves pulsing from right side
        for j in range(3):
            phase = (t + j * 0.15) % 1.0
            r = 18 + int(phase * 18)
            alpha = int((1 - phase) * 255)
            cx = CANVAS // 2 + 44
            cy = CANVAS // 2 - 20 + y_off
            c = (BLUE[0], BLUE[1], BLUE[2])
            draw.arc([cx - r, cy - r, cx + r, cy + r], start=-60, end=60, fill=c, width=2)

        # Little cupped hand / ear shape near right side
        hx = CANVAS // 2 + 36
        hy = CANVAS // 2 - 20 + y_off
        draw.ellipse([hx - 6, hy - 10, hx + 6, hy + 10], outline=GHOST_COLOR, width=2)
        frames.append(img)
    return frames


# ---------------------------------------------------------------------------
# GIF output
# ---------------------------------------------------------------------------

def save_gif(frames: List[Image.Image], path: Path, duration_ms: int = 1000 // FPS) -> None:
    """Save RGBA frames as an animated GIF with a fixed black+white palette."""
    # Convert to P mode with a palette containing black + white + accents
    palette_img = Image.new("P", (1, 1))
    # Build a small palette: black, white, accents, grayscale ramp
    palette = []
    palette.append(BG)
    palette.append(GHOST_COLOR)
    palette.extend([CYAN, BLUE, YELLOW, PINK, GREEN])
    for v in range(0, 256, 8):
        palette.append((v, v, v))
    # Pad to 768 bytes
    flat = []
    for c in palette[:256]:
        flat.extend(c)
    flat += [0] * (768 - len(flat))
    palette_img.putpalette(flat)

    quantized = []
    for f in frames:
        # Composite onto black background, then quantize with palette
        rgb = Image.new("RGB", f.size, BG)
        rgb.paste(f, (0, 0), f)
        q = rgb.quantize(palette=palette_img, dither=Image.Dither.FLOYDSTEINBERG)
        quantized.append(q)

    quantized[0].save(
        path,
        save_all=True,
        append_images=quantized[1:],
        duration=duration_ms,
        loop=0,
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
    BACKUP_DIR.mkdir(parents=True, exist_ok=True)

    # Backup existing GIFs
    for old in OUT_DIR.glob("*.gif"):
        dest = BACKUP_DIR / old.name
        old.replace(dest)
        print(f"Backed up {old.name} -> {BACKUP_DIR.name}")

    body = load_ghost_body()
    print(f"Loaded ghost body: {body.size}")

    for filename, scene_fn in SCENES:
        frames = scene_fn(body)
        out_path = OUT_DIR / filename
        save_gif(frames, out_path)
        print(f"Generated {filename}: {len(frames)} frames, {out_path.stat().st_size // 1024} KB")

    print("Done. Preview at:", OUT_DIR)


if __name__ == "__main__":
    main()
