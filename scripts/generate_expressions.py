#!/usr/bin/env python3
"""
Kiro 圆屏表情帧生成管线 (idle / wait / work)
============================================

从 assets/ 下的参考 MP4 提取帧, 处理成 RGB565 (大端) 数据, 生成:
  - src/kiro_expr_idle.cpp
  - src/kiro_expr_wait.cpp
  - src/kiro_expr_work.cpp
  - src/kiro_expr_table.cpp   (指针表)
  - src/kiro_expressions.h     (头文件: 尺寸/帧数/外部声明)

固件按 EXPR_FPS 播放, 圆屏 (GC9D01 160x160) 上居中绘制 120x120。
白色内容显示在黑底上。

依赖: ffmpeg (命令行), Python3 (标准库)

用法:
  python3 scripts/generate_expressions.py              # 生成到 src/
  python3 scripts/generate_expressions.py --out /tmp/x # 生成到指定目录 (用于验证, 不覆盖 src/)
  python3 scripts/generate_expressions.py --keep-raw   # 保留 /tmp 下的原始帧

注意:
  src/ 中现有的 cpp 是已经人工确认过的版本。重新运行本脚本会覆盖它们,
  请确认参数 (尤其 work 的裁切/下移量) 与期望一致后再写入 src/。
"""

import argparse
import os
import subprocess
import sys
from collections import deque

# ---- 全局参数 (须与 kiro_expressions.h 保持一致) ----
FRAME_W = 120
FRAME_H = 120
FRAME_SIZE = FRAME_W * FRAME_H * 2  # RGB565 = 2 字节/像素
FPS = 4
FRAME_COUNT = 40
EXPR_NAMES = ["idle", "wait", "work"]  # 顺序即 currentExpr 索引 0/1/2

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
TMP = "/tmp/kiro_frames"

# 每个表情的源视频与 ffmpeg 滤镜链。
# 裁切/缩放在 ffmpeg 完成, 像素级处理在 Python 完成。
EXPR_SOURCES = {
    "idle": {
        "video": "Kiro形象动图设计 (1).mp4",
        # 720x720 方形裁切, lanczos 缩放填满 120x120
        "vf": "crop=720:720:0:100,fps={fps},scale=120:120:flags=lanczos",
    },
    "wait": {
        "video": "Kiro形象动图设计.mp4",
        "vf": "crop=720:720:0:100,fps={fps},scale=120:120:flags=lanczos",
    },
    "work": {
        "video": "Kiro形象动图设计 (2).mp4",
        # 裁出齿轮+小鬼上半身, 顶部补白把内容整体下移, 缩放填满 120x120
        # (小鬼底部切到画面边缘, 与 idle 取景一致; 底部开口在 Python 中封住)
        "vf": "crop=720:720:0:170,pad=720:840:0:120:white,fps={fps},scale=120:120:flags=lanczos",
    },
}

# work 处理时把底部 N 行当作屏障, 封住被切掉的小鬼底部开口, 防止 flood 泄漏
WORK_BOTTOM_SEAL_ROWS = 3


# ---------------- 像素工具 ----------------
def get_px(d, o, x, y):
    i = o + (y * FRAME_W + x) * 2
    return (d[i] << 8) | d[i + 1]


def set_px(d, o, x, y, v):
    i = o + (y * FRAME_W + x) * 2
    d[i] = (v >> 8) & 0xFF
    d[i + 1] = v & 0xFF


def brightness(p):
    r = ((p >> 11) & 0x1F) * 8
    g = ((p >> 5) & 0x3F) * 4
    b = (p & 0x1F) * 8
    return (r + g + b) // 3


def dilate(mask, n):
    """4-邻域形态学膨胀 n 次。"""
    m = [row[:] for row in mask]
    for _ in range(n):
        nm = [row[:] for row in m]
        for y in range(FRAME_H):
            for x in range(FRAME_W):
                if m[y][x]:
                    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                        nx, ny = x + dx, y + dy
                        if 0 <= nx < FRAME_W and 0 <= ny < FRAME_H:
                            nm[ny][nx] = True
        m = nm
    return m


def flood_bg(sealed):
    """从四条边缘对非屏障像素做 flood fill, 返回背景 mask。"""
    bg = [[False] * FRAME_W for _ in range(FRAME_H)]
    q = deque()

    def seed(x, y):
        if not sealed[y][x] and not bg[y][x]:
            bg[y][x] = True
            q.append((x, y))

    for x in range(FRAME_W):
        seed(x, 0)
        seed(x, FRAME_H - 1)
    for y in range(FRAME_H):
        seed(0, y)
        seed(FRAME_W - 1, y)
    while q:
        cx, cy = q.popleft()
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nx, ny = cx + dx, cy + dy
            if 0 <= nx < FRAME_W and 0 <= ny < FRAME_H and not bg[ny][nx] and not sealed[ny][nx]:
                bg[ny][nx] = True
                q.append((nx, ny))
    return bg


def largest_cc(mask):
    """返回 mask 中最大的 4-连通分量 (像素坐标列表)。"""
    seen = [[False] * FRAME_W for _ in range(FRAME_H)]
    best = []
    for sy in range(FRAME_H):
        for sx in range(FRAME_W):
            if mask[sy][sx] and not seen[sy][sx]:
                comp = []
                q = deque([(sx, sy)])
                seen[sy][sx] = True
                while q:
                    cx, cy = q.popleft()
                    comp.append((cx, cy))
                    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < FRAME_W and 0 <= ny < FRAME_H and mask[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            q.append((nx, ny))
                if len(comp) > len(best):
                    best = comp
    return best


# ---------------- 各表情处理算法 ----------------
def process_idle(d, o):
    """
    idle: 灰底小鬼。
    从边缘 flood fill 穿过 "中等亮度" 的灰背景 (60<b<240) -> 涂黑;
    其余像素 (白身体 b>=240 / 黑轮廓 b<60) 保留原始颜色。
    """
    passable = [[60 < brightness(get_px(d, o, x, y)) < 240 for x in range(FRAME_W)]
                for y in range(FRAME_H)]
    sealed = [[not passable[y][x] for x in range(FRAME_W)] for y in range(FRAME_H)]
    bg = flood_bg(sealed)
    for y in range(FRAME_H):
        for x in range(FRAME_W):
            if bg[y][x]:
                set_px(d, o, x, y, 0x0000)
            # else: 保留原值


def process_wait(d, o):
    """
    wait: 灰底 + 灰色问号。
    flood 阈值收紧 (196<b<245), 让灰色问号 (b~96) 不被当成背景而保留;
    非背景像素二值化: b>=45 -> 白, 否则 -> 黑。
    """
    passable = [[196 < brightness(get_px(d, o, x, y)) < 245 for x in range(FRAME_W)]
                for y in range(FRAME_H)]
    sealed = [[not passable[y][x] for x in range(FRAME_W)] for y in range(FRAME_H)]
    bg = flood_bg(sealed)
    for y in range(FRAME_H):
        for x in range(FRAME_W):
            if bg[y][x]:
                set_px(d, o, x, y, 0x0000)
            else:
                b = brightness(get_px(d, o, x, y))
                set_px(d, o, x, y, 0xFFFF if b >= 45 else 0x0000)


def process_work(d, o):
    """
    work: 白底, 小鬼 + 齿轮 + 闪电。区域感知处理:
      1. 屏障 = 亮度<120 的墨迹; 底部数行也设为屏障 (封住被切掉的小鬼底部)。
      2. 膨胀屏障 2 次封住小缝隙, 再 flood 出背景。
      3. 被围住的亮像素中取最大连通域 = 小鬼主体。
      4. 把主体膨胀 3 次得到 "小鬼区"。
      5. 渲染:
         - 小鬼区内: b>=120 -> 白 (身体), 否则 -> 黑 (轮廓/眼睛)  => 保留细节, 与 idle 一致
         - 小鬼区外: b<120 -> 白 (符号墨迹反色), 否则 -> 黑 (纸面)  => 齿轮/闪电显示为白
    """
    barrier = [[brightness(get_px(d, o, x, y)) < 120 for x in range(FRAME_W)]
               for y in range(FRAME_H)]
    for y in range(FRAME_H - WORK_BOTTOM_SEAL_ROWS, FRAME_H):
        for x in range(FRAME_W):
            barrier[y][x] = True
    sealed = dilate(barrier, 2)
    bg = flood_bg(sealed)

    enclosed = [[(brightness(get_px(d, o, x, y)) >= 120 and not bg[y][x])
                 for x in range(FRAME_W)] for y in range(FRAME_H)]
    comp = largest_cc(enclosed)
    body = [[False] * FRAME_W for _ in range(FRAME_H)]
    for x, y in comp:
        body[y][x] = True
    zone = dilate(body, 3)

    for y in range(FRAME_H):
        for x in range(FRAME_W):
            b = brightness(get_px(d, o, x, y))
            if zone[y][x]:
                set_px(d, o, x, y, 0xFFFF if b >= 120 else 0x0000)
            else:
                set_px(d, o, x, y, 0xFFFF if b < 120 else 0x0000)


PROCESSORS = {"idle": process_idle, "wait": process_wait, "work": process_work}


# ---------------- ffmpeg 提取 ----------------
def extract(name):
    src = EXPR_SOURCES[name]
    video = os.path.join(ASSETS, src["video"])
    if not os.path.isfile(video):
        sys.exit(f"[ERR] 源视频不存在: {video}")
    out_dir = os.path.join(TMP, name)
    os.makedirs(out_dir, exist_ok=True)
    raw = os.path.join(out_dir, "frames.raw")
    vf = src["vf"].format(fps=FPS)
    cmd = ["ffmpeg", "-y", "-i", video, "-vf", vf,
           "-pix_fmt", "rgb565be", "-f", "rawvideo", raw]
    print(f"[{name}] ffmpeg -vf \"{vf}\"")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return raw


# ---------------- 写出 cpp / 头文件 ----------------
def write_expr_cpp(out_dir, name, data, nframes):
    path = os.path.join(out_dir, f"kiro_expr_{name}.cpp")
    with open(path, "w") as f:
        f.write('#include "kiro_expressions.h"\n\n')
        f.write(f"const uint16_t kiro_{name}_frames[{FRAME_COUNT}][{FRAME_W * FRAME_H}] PROGMEM = {{\n")
        for fi in range(nframes):
            o = fi * FRAME_SIZE
            f.write(f"  {{ // Frame {fi}\n    ")
            px = [f"0x{((data[i] << 8) | data[i + 1]):04X}" for i in range(o, o + FRAME_SIZE, 2)]
            for i in range(0, len(px), 16):
                f.write(",".join(px[i:i + 16]) + (",\n    " if i + 16 < len(px) else "\n"))
            f.write("  },\n" if fi < nframes - 1 else "  }\n")
        f.write("};\n")
    print(f"  -> {path}")


def write_table_cpp(out_dir):
    path = os.path.join(out_dir, "kiro_expr_table.cpp")
    with open(path, "w") as f:
        f.write('#include "kiro_expressions.h"\n\n')
        f.write(f"const uint16_t (*kiro_expressions[EXPR_COUNT])[{FRAME_W * FRAME_H}] = {{\n")
        for n in EXPR_NAMES:
            f.write(f"  kiro_{n}_frames,\n")
        f.write("};\n")
    print(f"  -> {path}")


def write_header(out_dir):
    path = os.path.join(out_dir, "kiro_expressions.h")
    cells = FRAME_W * FRAME_H
    with open(path, "w") as f:
        f.write("/**\n")
        f.write(" * kiro_expressions.h - Auto-generated RGB565 frame data for round LCD\n")
        f.write(" *\n")
        f.write(f" * {len(EXPR_NAMES)} expressions x {FRAME_COUNT} frames @ {FPS}fps, {FRAME_W}x{FRAME_H} pixels\n")
        f.write(" * Total size: ~3.3MB (stored in PROGMEM/Flash)\n")
        f.write(" */\n\n")
        f.write("#ifndef KIRO_EXPRESSIONS_H\n#define KIRO_EXPRESSIONS_H\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"#define EXPR_FRAME_W {FRAME_W}\n")
        f.write(f"#define EXPR_FRAME_H {FRAME_H}\n")
        f.write(f"#define EXPR_FPS {FPS}\n")
        f.write(f"#define EXPR_FRAME_COUNT {FRAME_COUNT}\n")
        f.write(f"#define EXPR_COUNT {len(EXPR_NAMES)}\n\n")
        for n in EXPR_NAMES:
            f.write(f"extern const uint16_t kiro_{n}_frames[{FRAME_COUNT}][{cells}] PROGMEM;\n")
        f.write("\n// Array of pointers to expression frame arrays\n")
        f.write(f"extern const uint16_t (*kiro_expressions[EXPR_COUNT])[{cells}];\n\n")
        f.write("#endif // KIRO_EXPRESSIONS_H\n")
    print(f"  -> {path}")


# ---------------- 主流程 ----------------
def main():
    ap = argparse.ArgumentParser(description="生成 Kiro 圆屏表情帧数据")
    ap.add_argument("--out", default=os.path.join(ROOT, "src"),
                    help="输出目录 (默认 src/)")
    ap.add_argument("--only", choices=EXPR_NAMES, help="只生成某一个表情")
    ap.add_argument("--no-header", action="store_true", help="不写头文件/指针表")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    names = [args.only] if args.only else EXPR_NAMES

    for name in names:
        raw = extract(name)
        with open(raw, "rb") as f:
            data = bytearray(f.read())
        nframes = min(len(data) // FRAME_SIZE, FRAME_COUNT)
        proc = PROCESSORS[name]
        for fi in range(nframes):
            proc(data, fi * FRAME_SIZE)
        write_expr_cpp(args.out, name, data, nframes)

    if not args.no_header and not args.only:
        write_table_cpp(args.out)
        write_header(args.out)

    print("完成。")


if __name__ == "__main__":
    main()
