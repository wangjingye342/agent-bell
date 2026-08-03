# -*- coding: utf-8 -*-
"""AgentBell PCB 小件打包器: 网格搜索满足全部约束的位置"""
import math, json, itertools

W, H = 40.0, 24.0
GAP = 1.05
rings = [(4, 3.5), (36.5, 3.5), (36.5, 20.5)]  # r2.5 定位柱净空

def in_board(x0, y0, x1, y1):
    if x0 < 0.4 or y0 < 0.4 or x1 > W - 0.4 or y1 > H - 0.4:
        return False
    for cx, cy in [(x0, y1), (x1, y1)]:
        if cx < 10 and cy > 14 and (cx - 10) ** 2 + (cy - 14) ** 2 > 100:
            return False
    return True

def ring_ok(x0, y0, x1, y1, r=2.5):
    for cx, cy in rings:
        nx = min(max(cx, x0), x1)
        ny = min(max(cy, y0), y1)
        if (nx - cx) ** 2 + (ny - cy) ** 2 < r * r:
            return False
    return True

def gap2(a, b):
    dx = max(a[0] - b[2], b[0] - a[2], 0)
    dy = max(a[1] - b[3], b[1] - a[3], 0)
    return math.hypot(dx, dy)

fixed = [
    ('J2', 1, 0.55, 6.35, 3.35, 14.25), ('J3', 1, 31.0, 17.7, 33.8, 23.0), ('J4', 1, 4.4, 6.35, 7.2, 22.25),
    ('SW1', 1, 8.3, 1.2, 24.3, 17.9), ('LS1', 1, 25.33, 7.37, 34.59, 16.63),
    ('J1', 1, 35.7, 6.6, 38.5, 17.4),
    ('Q1', 1, 25.4, 0.55, 28.6, 3.75), ('R4', 1, 25.45, 4.77, 28.35, 6.25), ('R5', 1, 29.45, 4.77, 32.35, 6.25),
    ('U1', 2, 8.0, 1.0, 26.3, 25.5),
    ('C4', 2, 27.4, 3.7, 30.4, 5.3), ('C2', 2, 27.4, 7.7, 30.4, 9.3), ('C1', 2, 27.35, 14.5, 31.45, 16.5),
]
# 背面穿板引脚障碍(蜂鸣器2脚区 + J1/J2/J3/J4 孔列)
th_back = [(26.9, 11.0, 33.0, 13.0), (34.7, 5.6, 39.5, 18.4), (0.0, 5.3, 4.4, 15.3), (30.0, 16.7, 34.8, 24.0), (3.4, 5.3, 8.2, 23.3)]

bad = []
for n, l, x0, y0, x1, y1 in fixed:
    if not in_board(x0, y0, x1, y1):
        bad.append((n, 'board'))
    if n != 'U1' and not ring_ok(x0, y0, x1, y1):
        bad.append((n, 'ring'))
for (n1, l1, *a), (n2, l2, *b) in itertools.combinations(fixed, 2):
    if l1 == l2 and n1 != 'U1' and n2 != 'U1' and gap2(a, b) < GAP:
        bad.append((n1, n2, round(gap2(a, b), 2)))
print('fixed 违规:', bad)

movable = [('D1', 6.4, 3.0), ('Q2', 3.2, 3.2), ('R6', 2.9, 1.48), ('C3', 4.1, 2.0),
           ('R1', 2.9, 1.48), ('R2', 2.9, 1.48), ('R3', 2.9, 1.48), ('R7', 2.9, 1.48)]
placed = list(fixed)
res = {}
for name, w, h in movable:
    done = False
    for layer in (1, 2):
        if done:
            break
        for rot in (0, 90):
            if done:
                break
            ww, hh = (w, h) if rot == 0 else (h, w)
            yy = 0.4
            while yy + hh <= H - 0.4 and not done:
                xx = 0.4
                while xx + ww <= W - 0.4 and not done:
                    box = (xx, yy, xx + ww, yy + hh)
                    if in_board(*box) and ring_ok(*box):
                        ok = True
                        for n, l, x0, y0, x1, y1 in placed:
                            if l != layer:
                                continue
                            if n == 'U1' and layer == 2:
                                if not (box[2] < x0 or box[0] > x1 or box[3] < y0 or box[1] > y1):
                                    ok = False
                                    break
                            elif gap2(box, (x0, y0, x1, y1)) < GAP:
                                ok = False
                                break
                        if ok and layer == 2:
                            for z in th_back:
                                if gap2(box, z) < 0.8:
                                    ok = False
                                    break
                        if ok:
                            placed.append((name, layer, *box))
                            res[name] = (layer, rot, box)
                            done = True
                    xx = round(xx + 0.25, 2)
                yy = round(yy + 0.25, 2)
    if not done:
        res[name] = None

out = {}
for k, v in res.items():
    if v:
        cx = (v[2][0] + v[2][2]) / 2
        cy = (v[2][1] + v[2][3]) / 2
        print(f"{k}: L{v[0]} rot{v[1]} center=({cx:.2f},{cy:.2f})mm")
        out[k] = (v[0], v[1], round(cx, 2), round(cy, 2))
    else:
        print(k, 'NO SLOT!')
json.dump(out, open('hardware/.pack-result.json', 'w'))
