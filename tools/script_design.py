"""Monoline cursive for the intro's title, designed as centerline points (y up, baseline 0, x-height 1) through which a
centripetal Catmull-Rom spline runs; slanted, sampled, written out for a preview (render_preview.ps1) and as a C++ table.

    python tools/script_design.py <out dir> [cipher/boot_script.inc]   strokes.json (and the table) for a change
    powershell tools/render_preview.ps1 -Dir <out dir>                  <out dir>/preview.png: how it reads"""
import json
import math
import sys

out = sys.argv[1]
SLANT = 0.22   # x += SLANT * y

# Each stroke: a list of (x, y). Words are single connected strokes; i dots are their own short strokes.
WORDS = {
    'Esia': [
        [
            # E: the upper bowl from its top right, round to the left, the waist curling right, the larger lower
            # bowl, the tail rising into s
            (0.98, 1.50), (0.84, 1.70), (0.58, 1.74), (0.38, 1.60), (0.36, 1.40), (0.50, 1.24), (0.74, 1.16),
            (0.56, 1.10), (0.32, 1.00), (0.14, 0.78), (0.10, 0.46), (0.22, 0.16), (0.46, 0.01), (0.76, 0.00),
            (1.02, 0.12),
            # s: up to its peak, the belly down to the right, round the bottom, out to the right
            (1.30, 0.50), (1.48, 0.98), (1.56, 0.84), (1.74, 0.56), (1.76, 0.26), (1.62, 0.04), (1.40, 0.02),
            (1.36, 0.14), (1.52, 0.10), (1.76, 0.10),
            # i: up, a rounded turn, down, out
            (1.96, 0.46), (2.10, 0.98), (2.06, 0.62), (2.02, 0.22), (2.12, 0.02), (2.34, 0.10),
            # a: up to the bowl's top right, round counter-clockwise, back up, down the stem, the tail
            (2.62, 0.62), (2.80, 0.94), (2.62, 1.02), (2.42, 0.86), (2.34, 0.50), (2.40, 0.16), (2.56, 0.02),
            (2.76, 0.12), (2.88, 0.44), (2.96, 0.96), (2.92, 0.54), (2.94, 0.16), (3.06, 0.00), (3.30, 0.14),
        ],
        [(2.00, 1.40), (2.01, 1.41)],
    ],
    'UI': [
        [
            # U: a small hook in at the top, down, round the bottom, up the right arm, down again, out
            (0.02, 1.42), (0.18, 1.70), (0.40, 1.70), (0.46, 1.42), (0.40, 0.84), (0.38, 0.32), (0.52, 0.04),
            (0.82, 0.00), (1.06, 0.24), (1.20, 0.80), (1.32, 1.66), (1.28, 1.00), (1.28, 0.32), (1.40, 0.04),
            (1.62, 0.10),
            # I: up to its cap, the cap out to the right, back into the stem, down, the foot out to the right
            (1.80, 0.70), (1.94, 1.46), (2.02, 1.72), (2.24, 1.80), (2.48, 1.76), (2.44, 1.66), (2.30, 1.58),
            (2.24, 1.10), (2.18, 0.48), (2.10, 0.10), (1.96, 0.00), (1.84, 0.06), (1.90, 0.16), (2.10, 0.14),
            (2.40, 0.10), (2.56, 0.20),
        ],
    ],
    'Lib': [
        [
            # L: up into a loop at its top, the stem down, a small loop at its foot, along the baseline, out
            (0.36, 1.24), (0.62, 1.64), (0.80, 1.82), (0.70, 1.88), (0.58, 1.74), (0.56, 1.20), (0.52, 0.52),
            (0.44, 0.10), (0.30, 0.00), (0.22, 0.08), (0.30, 0.16), (0.56, 0.10), (0.86, 0.02), (1.06, 0.12),
            # i
            (1.18, 0.66), (1.26, 0.98), (1.20, 0.58), (1.18, 0.18), (1.28, 0.02), (1.48, 0.12),
            # b: up into the ascender loop, the stem down, the bowl, the little tail at its top
            (1.72, 0.84), (1.92, 1.66), (1.90, 1.94), (1.74, 1.90), (1.70, 1.50), (1.68, 0.80), (1.70, 0.18),
            (1.84, 0.00), (2.08, 0.14), (2.16, 0.48), (2.04, 0.80), (1.88, 0.84), (1.96, 0.70), (2.18, 0.72),
            (2.42, 0.86),
        ],
        [(1.16, 1.40), (1.17, 1.41)],
    ],
}
ORDER = [('Esia', 0.0), ('UI', 3.75), ('Lib', 6.65)]


def catmull(points, per=6):
    """centripetal Catmull-Rom through `points`, `per` samples per span"""
    if len(points) < 3:
        return points
    pts = [points[0]] + points + [points[-1]]
    res = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        def tj(ti, a, b):
            return ti + max(math.dist(a, b), 1e-6) ** 0.5
        t0 = 0.0
        t1 = tj(t0, p0, p1)
        t2 = tj(t1, p1, p2)
        t3 = tj(t2, p2, p3)
        for k in range(per):
            t = t1 + (t2 - t1) * k / per
            def lerp(a, b, ta, tb):
                if tb - ta < 1e-9:
                    return a
                return ((tb - t) / (tb - ta) * a[0] + (t - ta) / (tb - ta) * b[0], (tb - t) / (tb - ta) * a[1] + (t - ta) / (tb - ta) * b[1])
            a1 = lerp(p0, p1, t0, t1)
            a2 = lerp(p1, p2, t1, t2)
            a3 = lerp(p2, p3, t2, t3)
            b1 = lerp(a1, a2, t0, t2)
            b2 = lerp(a2, a3, t1, t3)
            res.append(lerp(b1, b2, t1, t2))
    res.append(points[-1])
    return res


strokes = []
for word, dx in ORDER:
    for s in WORDS[word]:
        sampled = catmull(s)
        strokes.append([(x + dx + SLANT * y, y) for x, y in sampled])
json.dump(strokes, open(out + '/strokes.json', 'w'))
print(len(strokes), 'strokes', sum(len(s) for s in strokes), 'points')

if len(sys.argv) > 2:
    # the C++ table: the strokes in the order they are written, points in x-height units (y up)
    lines = ['// generated by script_design.py: the intro title\'s cursive, its strokes in writing order (x-height units, y up)',
             'const Vec2 kScriptPoints[] = {']
    starts = []
    n = 0
    for s in strokes:
        starts.append(n)
        row = []
        for x, y in s:
            row.append('{%.4ff, %.4ff}' % (x, y))
            n += 1
        for i in range(0, len(row), 6):
            lines.append('    ' + ', '.join(row[i:i + 6]) + ',')
    lines.append('};')
    lines.append('const int kScriptStrokeStart[] = {' + ', '.join(str(s) for s in starts) + ', %d};' % n)
    open(sys.argv[2], 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print('wrote', sys.argv[2])
