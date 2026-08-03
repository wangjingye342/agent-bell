# -*- coding: utf-8 -*-
"""AgentBell 板级全量审计 v2 — 含手工延长铜条(一等障碍物)
用法: easyeda pcb list --include-bbox --include-pads (json) | python audit_board.py
致过去的自己: 元件清单之外的手工铜(延长条/填充)审计必须显式建模, 否则就是2026-07-29蜂鸣器贴条事故重演。
"""
import json, sys, math, itertools

W, H = 48.0, 24.0
RINGS = [(4, 3.5), (44.5, 3.5), (44.5, 20.5)]          # 定位柱 r2.5
PEGS = [(7.78, 13.59), (12.07, 14.41)]                  # 编码器销孔
ANT = (20.1, 16.6, 29.2, 24.0)                          # 天线窗
# 手工延长铜条(裸铜, 背面): 左列/右列 x 区间; y=各盘中心±0.9 → 简化为两整条
STRIPS = [(14.86, 0.37, 16.96, 19.95), (32.35, 0.37, 34.45, 19.95)]
STRIP_TH_CLR = 1.8      # 穿板孔盘到裸铜条净距(烙铁通道)
STRIP_SMD_CLR = 1.0     # 背面SMD体到裸铜条净距

def gap(a, b):
    dx = max(a[0] - b[2], b[0] - a[2], 0)
    dy = max(a[1] - b[3], b[1] - a[3], 0)
    return math.hypot(dx, dy)

def pt_rect(px, py, b):
    nx = min(max(px, b[0]), b[2]); ny = min(max(py, b[1]), b[3])
    return math.hypot(nx - px, ny - py)

d = json.load(sys.stdin)
r = d.get('result', d)
comps, th = [], []
for c in r.get('components', []):
    bb = c.get('bbox') or {}
    comps.append((c['designator'], c.get('layer'),
                  bb.get('minX', 0) / 39.37, bb.get('minY', 0) / 39.37,
                  bb.get('maxX', 0) / 39.37, bb.get('maxY', 0) / 39.37))
    for p in c.get('pads', []):
        if str(p.get('layer')) == '12':
            th.append((c['designator'], str(p.get('padNumber')),
                       p.get('x', 0) / 39.37, p.get('y', 0) / 39.37))

fails = []
u1 = next(c for c in comps if c[0] == 'U1')

# 1 同层间距
for (n1, l1, *a), (n2, l2, *b) in itertools.combinations(comps, 2):
    if l1 == l2 and 'U1' not in (n1, n2) and gap(a, b) < 0.99:
        fails.append(f'间距 {n1}<->{n2} {gap(a,b):.2f}mm')
# 2 板界
for c in comps:
    if c[0] != 'U1' and (c[2] < -0.05 or c[4] > W + 0.05 or c[3] < -0.05 or c[5] > H + 0.05):
        fails.append(f'出板界 {c[0]}')
# 3 模块安全带(背面)
for c in comps:
    if c[0] != 'U1' and c[1] == 2 and c[4] > u1[2] - 2.3 and c[2] < u1[4] + 2.3:
        fails.append(f'安全带 {c[0]}')
# 4 柱孔净空(实体焊盘判据由人工复核bbox空角, 此处报<2.45)
for des, lay, x0, y0, x1, y1 in comps:
    if des == 'U1':
        continue
    for rc in RINGS:
        if pt_rect(rc[0], rc[1], (x0, y0, x1, y1)) < 2.45:
            fails.append(f'柱孔待核 {des}@{rc}')
# 5 编码器销孔
for des, lay, x0, y0, x1, y1 in comps:
    if des in ('U1', 'SW1'):
        continue
    need = 1.75 if lay == 2 else 1.15
    for pg in PEGS:
        if pt_rect(pg[0], pg[1], (x0, y0, x1, y1)) < need:
            fails.append(f'销孔 {des}@{pg} <{need}')
# 6 背面件 vs 穿板孔盘
for des, lay, x0, y0, x1, y1 in comps:
    if des == 'U1' or lay != 2:
        continue
    for t in th:
        if pt_rect(t[2], t[3], (x0, y0, x1, y1)) < 1.55:
            fails.append(f'背vs孔盘 {des}<->{t[0]}.{t[1]}')
# 7 天线窗
for c in comps:
    if c[0] != 'U1' and c[2] < ANT[2] and c[4] > ANT[0] and c[5] > ANT[1]:
        fails.append(f'天线窗 {c[0]}')
# 8 ★裸铜延长条(本次新增的教训条款)★
for s in STRIPS:
    for t in th:  # 穿板孔盘(接线孔+蜂鸣器针脚)
        if t[0] == 'U1':
            continue
        dd = pt_rect(t[2], t[3], s)
        if dd < STRIP_TH_CLR:
            fails.append(f'★裸铜条 {t[0]}.{t[1]} 距条 {dd:.2f} <{STRIP_TH_CLR}')
    for des, lay, x0, y0, x1, y1 in comps:  # 背面SMD体
        if des == 'U1' or lay != 2:
            continue
        dd = gap((x0, y0, x1, y1), s)
        if dd < STRIP_SMD_CLR:
            fails.append(f'★裸铜条 {des} 距条 {dd:.2f} <{STRIP_SMD_CLR}')

if fails:
    print('FAIL', len(fails))
    for f in sorted(set(fails)):
        print(' ', f)
else:
    print('ALL-PASS (8项含裸铜条条款)')
