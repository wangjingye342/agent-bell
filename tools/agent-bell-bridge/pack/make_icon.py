# -*- coding: utf-8 -*-
"""make_icon.py — 生成 Windows 多尺寸图标 pack/agentbell.ico。

分尺寸画法：16/20/24 只留橙铃铛（那种尺寸机身会糊成一团），32 以上是完整的
铝面板 + 深色屏。图形定义在 icon_art.py，托盘与 macOS 菜单栏共用同一套几何。
用法：python pack/make_icon.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import icon_art  # noqa: E402

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
OUT = os.path.join(HERE, "agentbell.ico")

imgs = [icon_art.app_icon(n, chassis=(n >= 32)) for n in SIZES]
imgs[-1].save(OUT, format="ICO", sizes=[(n, n) for n in SIZES],
              append_images=imgs[:-1])
print("icon ->", OUT, os.path.getsize(OUT), "bytes")
