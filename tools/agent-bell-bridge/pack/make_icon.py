# -*- coding: utf-8 -*-
"""make_icon.py — 从 bridge.make_icon_image 生成多尺寸 .ico（打包用，一次性）。"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from bridge import make_icon_image  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "agentbell.ico")
img = make_icon_image(True)
img.save(OUT, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64)])
print("icon ->", OUT)
