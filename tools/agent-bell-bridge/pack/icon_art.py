#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""icon_art.py — AgentBell 的全套图标画法（一处定义，Windows/macOS/托盘共用）。

设计取自项目自己的设计语言（见仓库根 DESIGN.md），不是通用铃铛素材：

  · 机身：浅暖灰拉丝铝 squircle，边缘一圈机加工高光 + 底部收边阴影
  · 标记：TE 橙铃铛，几何是「半圆 + 外张裙摆 + 收边横条 + 锤点」——
    和菜单栏那枚单色铃铛同源，一家人
  · 质感：铃铛做成丝印效果（极浅的内阴影 + 亚光边缘），像 TE 面板上印的图案，
    不是贴上去的贴纸
  · 全图只有一种强调色，唯一的深色是托盘态的状态点

三个用途、三种画法（都从同一套几何来）：
  app_icon()      —— 应用图标（Dock / 任务栏 / 安装器 / Finder），铝面板 + 橙铃铛
  tray_icon()     —— Windows 托盘，小尺寸下机身会糊，所以只留橙铃铛 + 状态点
  menubar_icon()  —— macOS 菜单栏模板图，纯 alpha 单色；在线=实心，离线=空心

用法：python pack/make_icon.py（出 .ico）、python pack/make_icns.py（出 .icns）
"""
from PIL import Image, ImageDraw, ImageFilter

# —— 调色板（与 DESIGN.md 一致）——
CHASSIS_HI = (235, 234, 230)     # 铝面板顶部
CHASSIS_LO = (216, 215, 210)     # 铝面板底部（竖向微渐变）
EDGE_HI    = (250, 250, 248)     # 机加工内侧高光
EDGE_LO    = (176, 175, 169)     # 外描边 / 底部收边
ORANGE     = (240, 78, 0)        # TE 橙：唯一强调色
ORANGE_DK  = (200, 65, 0)        # 丝印边缘的一点点压深
GREEN      = (63, 185, 80)
RED        = (229, 72, 77)


def _squircle_mask(size, radius_ratio=0.235, ss=4):
    """圆角方形遮罩。半径比例贴近 macOS 图标的连续圆角观感，超采样保证边缘干净。"""
    n = size * ss
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).rounded_rectangle(
        [0, 0, n - 1, n - 1], radius=int(n * radius_ratio), fill=255)
    return m.resize((size, size), Image.LANCZOS)


def _brushed_aluminum(size):
    """竖向拉丝铝：上下微渐变 + 极细的竖纹（纹理要弱到只在大尺寸下看得出）。"""
    img = Image.new("RGB", (size, size), CHASSIS_HI)
    d = ImageDraw.Draw(img)
    for y in range(size):                                  # 竖向渐变
        t = y / max(1, size - 1)
        d.line([(0, y), (size, y)],
               fill=tuple(int(a + (b - a) * t) for a, b in zip(CHASSIS_HI, CHASSIS_LO)))
    step = max(2, size // 44)                              # 拉丝纹
    for x in range(0, size, step):
        d.line([(x, 0), (x, size)], fill=(255, 255, 255), width=1)
    veil = Image.new("RGB", (size, size), CHASSIS_HI)
    return Image.blend(img, veil, 0.72)                     # 纹理压到很淡


def bell_shape(d, size, scale=1.0, dy=0.0, fill=ORANGE, hollow=False, stroke=None):
    """铃铛几何：半圆罩 + 外张裙摆 + 收边横条 + 锤点。坐标用 22 单位网格再缩放。

    这套几何三个用途共用，所以图标家族看起来是一家人。
    """
    u = size / 22.0 * scale
    ox = (size - 22 * u) / 2.0
    oy = (size - 22 * u) / 2.0 + dy * size

    def X(v): return ox + v * u
    def Y(v): return oy + v * u

    if hollow:
        w = max(1, int(round(1.7 * u)))
        d.arc([X(4.4), Y(3.4), X(17.6), Y(15.6)], 180, 360, fill=fill, width=w)
        d.line([(X(4.4), Y(9.5)), (X(2.9), Y(15.0))], fill=fill, width=w)
        d.line([(X(17.6), Y(9.5)), (X(19.1), Y(15.0))], fill=fill, width=w)
        d.line([(X(2.9), Y(15.0)), (X(19.1), Y(15.0))], fill=fill, width=w)
        d.ellipse([X(9.7), Y(16.6), X(12.3), Y(19.4)], outline=fill, width=w)
        d.ellipse([X(10.2), Y(1.5), X(11.8), Y(3.3)], fill=fill)
        return

    d.pieslice([X(4.4), Y(3.4), X(17.6), Y(15.6)], 180, 360, fill=fill)   # 罩顶半圆
    d.polygon([(X(4.4), Y(9.5)), (X(17.6), Y(9.5)),
               (X(19.1), Y(15.0)), (X(2.9), Y(15.0))], fill=fill)          # 外张裙摆
    d.rounded_rectangle([X(2.5), Y(14.3), X(19.5), Y(16.5)],
                        radius=u * 1.0, fill=fill)                          # 收边横条
    d.ellipse([X(10.2), Y(1.5), X(11.8), Y(3.3)], fill=fill)               # 提环
    d.ellipse([X(9.6), Y(17.1), X(12.4), Y(19.9)], fill=fill)              # 锤点
    if stroke:                                                              # 丝印压边
        d.arc([X(4.4), Y(3.4), X(17.6), Y(15.6)], 180, 360,
              fill=stroke, width=max(1, int(round(0.5 * u))))


DISP_BG    = (20, 20, 18)        # 显示屏底（这套设计语言里唯一的深色块）
DISP_EDGE  = (42, 42, 38)


def app_icon(size=512, screen=True, chassis=True):
    """应用图标。

    screen=True  在铝面板上开一块深色「显示屏」，橙铃铛印在屏里 ——
                 深浅对比让它在 32px 也立得住，而且 OLED 屏正是这台设备的视觉锚点。
    chassis=False 只留橙铃铛（16~24px 时机身会糊成一团，那种尺寸只保留标记）。
    """
    ss = 3 if size <= 64 else 2
    n = size * ss

    if not chassis:
        img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        bell_shape(ImageDraw.Draw(img), n, scale=0.95, fill=ORANGE + (255,))
        return img.resize((size, size), Image.LANCZOS)

    panel = _brushed_aluminum(n).convert("RGBA")
    d = ImageDraw.Draw(panel)
    r = int(n * 0.235)
    lw = max(1, n // 110)

    # 机加工内侧高光一圈（面板有厚度的关键，别画成描边）
    d.rounded_rectangle([lw, lw, n - 1 - lw, n - 1 - lw], radius=r - lw,
                        outline=EDGE_HI, width=lw)

    if screen:
        # 深色屏：内凹 + 细屏框；比例参考设备本体（宽扁，不是正方）
        m = n * 0.165
        top, bot = n * 0.245, n * 0.755
        rr = n * 0.058
        d.rounded_rectangle([m - lw, top - lw, n - m + lw, bot + lw],
                            radius=rr + lw, fill=EDGE_LO)          # 屏框
        d.rounded_rectangle([m, top, n - m, bot], radius=rr, fill=DISP_BG)
        d.rounded_rectangle([m, top, n - m, bot], radius=rr,
                            outline=DISP_EDGE, width=max(1, lw))
        # 屏上一道极淡的斜向反光（玻璃感，别抢主体）
        # 玻璃斜向反光 + 屏顶内阴影（内凹感），都裁进屏区，不越界
        gl = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        gd = ImageDraw.Draw(gl)
        gd.polygon([(m, top), (m + (n - 2 * m) * 0.46, top),
                    (m + (n - 2 * m) * 0.14, bot), (m, bot)],
                   fill=(255, 255, 255, 26))
        gd.rounded_rectangle([m, top, n - m, top + (bot - top) * 0.30],
                             radius=rr, fill=(0, 0, 0, 40))
        shade = Image.new("L", (n, n), 0)
        ImageDraw.Draw(shade).rounded_rectangle([m, top, n - m, bot],
                                               radius=rr, fill=255)
        gl = gl.filter(ImageFilter.GaussianBlur(max(0.8, n / 90.0)))   # 反光别留硬边
        gl.putalpha(Image.composite(gl.split()[3], Image.new("L", (n, n), 0), shade))
        panel = Image.alpha_composite(panel, gl)
        bell_fill, bell_scale, bell_dy = ORANGE + (255,), 0.44, 0.0
    else:
        bell_fill, bell_scale, bell_dy = ORANGE + (255,), 0.52, 0.0

    # 丝印铃铛：先一层压深的影再压橙面，像印在面上而不是浮着
    mark = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    bell_shape(ImageDraw.Draw(mark), n, scale=bell_scale, dy=bell_dy + 0.008,
               fill=ORANGE_DK + (200,))
    mark = mark.filter(ImageFilter.GaussianBlur(max(0.6, n / 300.0)))
    top_l = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    bell_shape(ImageDraw.Draw(top_l), n, scale=bell_scale, dy=bell_dy, fill=bell_fill)
    panel = Image.alpha_composite(Image.alpha_composite(panel, mark), top_l)

    panel.putalpha(_squircle_mask(n, 0.235, ss=2))
    return panel.resize((size, size), Image.LANCZOS)


def tray_icon(online, size=64):
    """Windows 托盘：小尺寸下机身会糊成一团，所以只留橙铃铛 + 右下状态点。"""
    ss = 4
    n = size * ss
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    bell_shape(d, n, scale=0.86, dy=-0.03, fill=ORANGE + (255,))
    dot = (GREEN if online else RED) + (255,)
    r = n * 0.19
    cx = cy = n - r - n * 0.04
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 255, 255, 255))
    d.ellipse([cx - r * 0.76, cy - r * 0.76, cx + r * 0.76, cy + r * 0.76], fill=dot)
    return img.resize((size, size), Image.LANCZOS)


def menubar_icon(online, pt=22, scale=2):
    """macOS 菜单栏模板图：只有 alpha 有意义（系统按菜单栏明暗自动上色）。

    在线=实心铃铛，离线=空心铃铛。用形状而不是颜色区分，因为模板图没有颜色；
    实测 22pt 下「实心/空心」比「加斜杠」清楚得多（斜杠会糊成一团）。
    """
    n = pt * scale * 4                                # 先大画再缩，边缘干净
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    bell_shape(ImageDraw.Draw(img), n, scale=0.92, fill=(0, 0, 0, 255),
               hollow=not online)
    return img.resize((pt * scale, pt * scale), Image.LANCZOS)
