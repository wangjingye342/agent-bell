#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""make_icns.py — 生成 macOS 应用图标 pack/agentbell.icns。

Pillow 的 ICNS 写出在 Windows 上也能跑，所以这一步可以预生成、提交进仓库，
Mac 构建时（pack/build_mac.sh）就不依赖它。图形定义见 icon_art.py。
用法：python pack/make_icns.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import icon_art  # noqa: E402

SIZES = [16, 32, 64, 128, 256, 512, 1024]
OUT = os.path.join(HERE, "agentbell.icns")

imgs = [icon_art.app_icon(n, chassis=(n >= 32)) for n in SIZES]
imgs[-1].save(OUT, format="ICNS", append_images=imgs[:-1])
print("icns ->", OUT, os.path.getsize(OUT), "bytes")
