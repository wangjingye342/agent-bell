# -*- coding: utf-8 -*-
"""AgentBell PCB v2 全局打包器: TH/SMD 分层约束 + 板宽扫描"""
import math, json, itertools

H = 24.0
GAP = 1.05

def solve(W):
    rings = [(4, 3.5), (W - 3.5, 3.5), (W - 3.5, 20.5)]
    U1cx = 24.65
    U1 = (U1cx - 9.15, -2.4, U1cx + 9.15, 22.1)          # 背面模块
    ANT = (U1cx - 4.5, 16.6, U1cx + 4.5, 23.6)           # 天线窗(双面禁件)
    SW1 = (2.0, 5.65, 18.0, 22.35)                        # 正面编码器(锁定)

    def in_board(b):
        x0, y0, x1, y1 = b
        if x0 < 0.4 or y0 < 0.4 or x1 > W - 0.4 or y1 > H - 0.4:
            return False
        for cx, cy in [(x0, y1), (x1, y1)]:
            if cx < 10 and cy > 14 and (cx - 10) ** 2 + (cy - 14) ** 2 > 100:
                return False
        return True

    def ring_ok(b):
        for cx, cy in rings:
            nx = min(max(cx, b[0]), b[2]); ny = min(max(cy, b[1]), b[3])
            if (nx - cx) ** 2 + (ny - cy) ** 2 < 2.5 ** 2:
                return False
        return True

    def gap2(a, b):
        dx = max(a[0] - b[2], b[0] - a[2], 0)
        dy = max(a[1] - b[3], b[1] - a[3], 0)
        return math.hypot(dx, dy)

    def ov(a, b):
        return not (a[2] <= b[0] or a[0] >= b[2] or a[3] <= b[1] or a[1] >= b[3])

    # (name, w, h, kind)  kind: TH / F(front SMD) / B(back SMD) / FB(任一面)
    movable = [
        ('LS1', 9.26, 9.26, 'TH'), ('J1', 2.8, 10.8, 'TH'), ('J2', 2.8, 7.9, 'TH'),
        ('J4', 2.8, 7.9, 'TH'), ('J3', 2.8, 5.3, 'TH'), ('J5', 2.8, 5.3, 'TH'),
        ('U2', 3.2, 3.2, 'F'), ('D2', 6.4, 3.0, 'FB'), ('C5', 4.1, 2.0, 'FB'),
        ('Q1', 3.2, 3.2, 'F'), ('Q2', 3.2, 3.2, 'F'),
        ('D1', 6.4, 3.0, 'FB'), ('C3', 4.1, 2.0, 'FB'), ('C1', 4.1, 2.0, 'B'),
        ('C2', 3.0, 1.7, 'B'), ('C4', 3.0, 1.7, 'B'),
        ('R4', 2.9, 1.48, 'FB'), ('R5', 2.9, 1.48, 'FB'), ('R6', 2.9, 1.48, 'FB'),
        ('R7', 2.9, 1.48, 'FB'), ('R8', 2.9, 1.48, 'FB'), ('R9', 2.9, 1.48, 'FB'),
        ('LED1', 3.2, 2.0, 'F'), ('R1', 2.9, 1.48, 'FB'), ('R2', 2.9, 1.48, 'FB'),
        ('R3', 2.9, 1.48, 'FB'),
    ]
    front = [('SW1', SW1)]
    back = []
    th_zones = [U1]           # TH 件禁入区(背面模块)
    res = {}
    for name, w, h, kind in movable:
        layers = {'TH': ('T',), 'F': ('F',), 'B': ('B',), 'FB': ('F', 'B')}[kind]
        done = False
        for lay in layers:
            if done: break
            for rot in (0, 90):
                if done: break
                ww, hh = (w, h) if rot == 0 else (h, w)
                yy = 0.4
                while yy + hh <= H - 0.4 and not done:
                    xx = 0.4
                    while xx + ww <= W - 0.8 and not done:
                        b = (xx, yy, xx + ww, yy + hh)
                        okc = in_board(b) and ring_ok(b) and not ov(b, ANT)
                        if okc and lay in ('T', 'F'):
                            for n2, b2 in front:
                                if gap2(b, b2) < GAP: okc = False; break
                        if okc and lay in ('T', 'B'):
                            for n2, b2 in back:
                                if gap2(b, b2) < GAP: okc = False; break
                        if okc and lay == 'B':
                            for z in th_zones:
                                if z is U1:
                                    if ov(b, U1): okc = False; break
                                elif gap2(b, z) < 0.6: okc = False; break
                        if okc and lay == 'T':
                            for z in [U1]:
                                if gap2(b, z) < 0.65: okc = False; break
                        if okc:
                            res[name] = (lay, rot, b)
                            if lay in ('T', 'F'): front.append((name, b))
                            if lay in ('T', 'B'): back.append((name, b))
                            if lay == 'T': th_zones.append(b)
                            done = True
                        xx = round(xx + 0.25, 2)
                    yy = round(yy + 0.25, 2)
        if not done:
            return None, name
    return res, None

for W in (46.0, 46.5, 47.0, 47.5, 48.0):
    res, fail = solve(W)
    if res:
        print(f'=== W={W} 可行 ===')
        for k, (lay, rot, b) in res.items():
            cx = round((b[0] + b[2]) / 2, 2); cy = round((b[1] + b[3]) / 2, 2)
            print(f'{k}: {lay} rot{rot} c=({cx},{cy})')
        json.dump({k: (v[0], v[1], round((v[2][0]+v[2][2])/2, 2), round((v[2][1]+v[2][3])/2, 2))
                   for k, v in res.items()}, open('hardware/.pack2-result.json', 'w'))
        json.dump({'W': W, 'U1cx': 24.65}, open('hardware/.pack2-board.json', 'w'))
        break
    else:
        print(f'W={W} 不可行, 卡在 {fail}')
