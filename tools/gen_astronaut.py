#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_astronaut.py — 生成"绕头脚中轴自转"的太空人逐帧位图（黑底白线）。
把太空人建成 3D（头盔/躯干在轴上，手臂/腿/背包分布在轴周围），绕纵轴(Y=头脚线)旋转
角度 φ，正交投影到 2D；按前后深度排序、前面的图元用黑填充"抹掉"后面再描白边 → 实现遮挡。
输出：① 预览 PNG（网格）② agent_bell/astronaut_frames.h（U8g2 XBM 帧）。纯标准库。
改顶部 PARTS 调形状，重跑即可。
"""
import math, os, zlib, struct

W = H = 48
N = 24
CX = CY = (W - 1) / 2.0
TH = 2.6                          # 轮廓线宽（粗一点，小屏更清晰）
TILT = -math.pi / 4               # 整体左倾 45°（头朝左上、脚朝右下）；只倾斜，不改自转/遮挡

# 3D 图元（x右 / y下[头在 -y] / z前+后-）。capsule:(x1,y1,z1,x2,y2,z2,r)  sphere:(x,y,z,r)
TORSO = ("cap", 0, -8, 0,   0, 8, 0,   6.0)
HELMET = ("sph", 0, -15, 0, 7.0)
PACK  = ("cap", 0, -6, -5.5, 0, 5, -5.5, 3.4)      # 背包（轴后方，转到背面才可见）
LARM  = ("cap", -6, -7, 0,  -6.5, 6, 0,  3.0)
RARM  = ("cap", 6, -7, 0,   6.5, 6, 0,   3.0)
LLEG  = ("cap", -3, 8, 0,   -3.5, 17, 0, 3.2)
RLEG  = ("cap", 3, 8, 0,    3.5, 17, 0,  3.2)
LBOOT = ("cap", -3.5, 17, 0, -6, 18.5, 0, 3.0)
RBOOT = ("cap", 3.5, 17, 0,  6, 18.5, 0,  3.0)
PARTS = [TORSO, HELMET, PACK, LARM, RARM, LLEG, RLEG, LBOOT, RBOOT]
VISOR = (0, -15, 6.0, 2.8)        # 面罩高光（头盔前方一点）


def seg_dist(px, py, x1, y1, x2, y2):
    vx, vy = x2 - x1, y2 - y1
    L2 = vx * vx + vy * vy
    t = 0.0 if L2 == 0 else max(0.0, min(1.0, ((px - x1) * vx + (py - y1) * vy) / L2))
    return math.hypot(px - (x1 + t * vx), py - (y1 + t * vy))


def roty(x, y, z, phi):                 # 绕 Y(头脚)轴自转，再整体左倾投影 → (屏幕x, 屏幕y, 深度z)
    c, s = math.cos(phi), math.sin(phi)
    xr = x * c + z * s                  # 自转后的横向
    zr = -x * s + z * c                 # 深度（前后，倾斜不影响）
    tc, ts = math.cos(TILT), math.sin(TILT)
    return (CX + xr * tc - y * ts, CY + xr * ts + y * tc, zr)


def depth(e, phi):
    if e[0] == "sph": return roty(e[1], e[2], e[3], phi)[2]
    a = roty(e[1], e[2], e[3], phi)[2]; b = roty(e[4], e[5], e[6], phi)[2]
    return (a + b) / 2.0


def stamp(g, e, phi):                   # 先黑填充(抹掉后面)，再描白边
    if e[0] == "sph":
        sx, sy, _ = roty(e[1], e[2], e[3], phi); r = e[4]
        for y in range(H):
            for x in range(W):
                d = math.hypot(x - sx, y - sy)
                if d <= r: g[y][x] = 0
                if r - TH <= d <= r: g[y][x] = 1
    else:
        x1, y1, _ = roty(e[1], e[2], e[3], phi)
        x2, y2, _ = roty(e[4], e[5], e[6], phi); r = e[7]
        for y in range(H):
            for x in range(W):
                d = seg_dist(x, y, x1, y1, x2, y2)
                if d <= r: g[y][x] = 0
                if r - TH <= d <= r: g[y][x] = 1


def frame(phi):
    g = [[0] * W for _ in range(H)]
    for e in sorted(PARTS, key=lambda e: depth(e, phi)):   # 后→前
        stamp(g, e, phi)
    vx, vy, vz = VISOR[0], VISOR[1], VISOR[2]
    sx, sy, dz = roty(vx, vy, vz, phi)
    if dz > 0.6:                         # 面罩朝前时画白色高光月牙
        for y in range(H):
            for x in range(W):
                if math.hypot(x - sx, y - sy) <= VISOR[3] and \
                   math.hypot(x - (sx + 1.4), y - (sy - 0.6)) > VISOR[3] - 0.2:
                    g[y][x] = 1
    return g


def write_png(path, w, h, pix):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    raw = bytearray()
    for y in range(h):
        raw.append(0); raw += pix[y * w:(y + 1) * w]
    open(path, "wb").write(b'\x89PNG\r\n\x1a\n'
        + chunk(b'IHDR', struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
        + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def preview(frames, scale=4, cols=6, gap=3):
    rows = (N + cols - 1) // cols
    PW, PH = cols * (W * scale + gap) + gap, rows * (H * scale + gap) + gap
    img = bytearray([40] * (PW * PH))
    for i, g in enumerate(frames):
        ox = gap + (i % cols) * (W * scale + gap); oy = gap + (i // cols) * (H * scale + gap)
        for y in range(H):
            for x in range(W):
                v = 255 if g[y][x] else 0
                for sy in range(scale):
                    row = (oy + y * scale + sy) * PW
                    for sx in range(scale):
                        img[row + ox + x * scale + sx] = v
    write_png(os.path.join(os.path.dirname(__file__), "astro_preview.png"), PW, PH, img)


def emit_header(frames):
    bpr = (W + 7) // 8
    out = ["// 自动生成（tools/gen_astronaut.py）：绕头脚中轴自转太空人，%d 帧 %dx%d，U8g2 XBM 位序" % (N, W, H),
           "#pragma once", "#include <Arduino.h>",
           "#define ASTRO_W %d" % W, "#define ASTRO_H %d" % H, "#define ASTRO_N %d" % N,
           "const uint8_t ASTRO[%d][%d] PROGMEM = {" % (N, bpr * H)]
    for g in frames:
        b = []
        for y in range(H):
            for by in range(bpr):
                v = 0
                for bit in range(8):
                    x = by * 8 + bit
                    if x < W and g[y][x]: v |= (1 << bit)
                b.append("0x%02x" % v)
        out.append("  {" + ",".join(b) + "},")
    out.append("};")
    p = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "agent_bell", "astronaut_frames.h"))
    open(p, "w", encoding="utf-8").write("\n".join(out) + "\n")


frames = [frame(2 * math.pi * i / N) for i in range(N)]
preview(frames)
emit_header(frames)
print("OK: tools/astro_preview.png + agent_bell/astronaut_frames.h  (%d 帧 %dx%d)" % (N, W, H))
