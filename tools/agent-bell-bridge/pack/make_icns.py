#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_icns.py — 生成 macOS 应用图标 pack/agentbell.icns（Pillow 跨平台可跑，
Windows 上也能预生成，Mac 构建时就不依赖这一步）。

图形与托盘图标同款金色铃铛（bridge.make_icon_image 的放大参数化版本）。
用法：python pack/make_icns.py
"""
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))


def bell(size):
    """按任意尺寸画铃铛（几何取自 bridge.make_icon_image 的 64px 版 × 比例）。"""
    s = size / 64.0
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    body = (230, 176, 60, 255)                                    # 金色
    d.pieslice((12 * s, 8 * s, 52 * s, 48 * s), 180, 360, fill=body)   # 铃罩上半
    d.polygon([(12 * s, 28 * s), (52 * s, 28 * s),
               (56 * s, 46 * s), (8 * s, 46 * s)], fill=body)          # 铃罩下摆
    d.rectangle((6 * s, 46 * s, 58 * s, 50 * s), fill=body)            # 底沿
    d.ellipse((26 * s, 50 * s, 38 * s, 62 * s), fill=body)             # 铃锤
    return img


def main():
    sizes = [16, 32, 64, 128, 256, 512, 1024]
    imgs = [bell(n) for n in sizes]
    out = os.path.join(HERE, "agentbell.icns")
    imgs[-1].save(out, format="ICNS", append_images=imgs[:-1])
    print("已生成", out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main()
