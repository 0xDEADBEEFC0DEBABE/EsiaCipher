"""The cursive for any text as uniform cubic B-splines: each glyph's control points in x-height units (y up, baseline
0), the letters of a word one control polygon - so the curve is C2 everywhere, the joins included, and runs straight
only where four control points line up. A stroke that goes back over itself is exact: its tip is written three times
and the control points before it mirrored after it (tip, back); a second pass the same way repeats them (same).

A glyph has variants by the way the letter before it left: out of its bottom (LOW: rising at about 27 degrees through
0.15), out of a sag (MID: heading right through 0.20, the w's and the v's), along a bridge (HIGH: heading a little down
through 0.70, the o's, b's and r's), or starting a word. Every exit ends in the same control points (TAIL), so any
letter runs into any other; a variant is the lead into its letter and the letter up to its own exit, its origin the
point it starts from, its advance the point it leaves at.

    python tools/script_bspline.py preview <out dir> "text" ["more text" ...]   preview.png
    python tools/script_bspline.py table <file.inc>                              the C++ table"""
import json
import math
import sys

START, LOW, HIGH, MID = 0, 1, 2, 3
NONE = 0
SLANT = 0.0
SPACE = 0.75      # between words, after the last letter's exit
UNKNOWN = 0.5
SMOOTH = 3        # passes of the fourth-difference smoothing (0.06 a pass)

# the last control points of each exit (x from the advance, y absolute), and what takes their place when the letter ends
# its word (the stroke ends on the last point): out of a bottom a short rise - a variant's own `fin` overrides it
TAIL = {LOW: [(-0.33, 0.0), (-0.165, 0.065), (0.0, 0.15)],
        MID: [(-0.18, 0.24), (0.0, 0.20)],
        HIGH: [(-0.14, 0.745), (0.0, 0.70)]}
END = {LOW: [(-0.33, 0.0), (-0.19, 0.055), (-0.07, 0.16), (0.02, 0.29)],
       MID: [(-0.18, 0.24), (0.0, 0.20), (0.17, 0.225), (0.30, 0.30)],
       HIGH: [(-0.14, 0.745), (0.0, 0.70), (0.12, 0.69), (0.23, 0.72)]}


class V:
    """a variant: control points (P), for each the one it copies (src, -1 none), the advance, its strokes of its own"""

    def __init__(self):
        self.P, self.src = [], []
        self.adv = None
        self.first, self.extra, self.marks = [], [], []   # control polygons: before it, right after it, after its word
        # after a low exit: the control points that take the place of that exit's TAIL (x from this variant's origin):
        # a letter that reaches back over the connector (the e) has the exit run on longer, lower; one that rises at once
        # has it turn up at once
        self.pre = []
        # ending a word: the control points that take the place of its exit's TAIL (none: END's)
        self.fin = []

    def add(self, *pts):
        for p in pts:
            self.P.append((float(p[0]), float(p[1])))
            self.src.append(-1)
        return self

    def start(self, p):
        return self.add(p, p, p)

    def tip(self):
        self.P += [self.P[-1]] * 2
        self.src += [-1, -1]
        return self

    def back(self, k):
        """back over the k control points before the tip: the same curve the other way"""
        t = len(self.P) - 3
        assert self.P[t] == self.P[t + 1] == self.P[t + 2], 'back() needs a tip'
        for i in range(1, k + 1):
            s = t - i
            self.P.append(self.P[s])
            self.src.append(s if self.src[s] < 0 else self.src[s])
        return self

    def same(self, i0, i1):
        """the control points i0..i1 again: a second pass the same way"""
        for i in range(i0, i1 + 1):
            self.P.append(self.P[i])
            self.src.append(i if self.src[i] < 0 else self.src[i])
        return self

    def here(self):
        return len(self.P)

    def exit(self, kind, adv):
        self.add(*[(adv + dx, y) for dx, y in TAIL[kind]])
        self.adv = adv
        return self

    def end(self, adv):
        """the stroke ends in the glyph (no exit)"""
        self.tip()
        self.adv = adv
        return self


def stroke(*pts):
    """a stroke of its own (a cross, a capital's stem): its ends written three times"""
    v = V()
    v.start(pts[0])
    v.add(*pts[1:])
    v.tip()
    return v


def dot(x, y):
    v = V()
    v.add((x, y), (x + 0.004, y + 0.004))
    return v


def sh(pts, dx, dy=0.0):
    return [(x + dx, y + dy) for x, y in pts]


class G:
    def __init__(self, exit, joins):
        self.exit, self.joins, self.var = exit, joins, {}

    def variant(self, kind):
        """the variant for the way the letter before left (kind) or for a word's start; a mid exit falls back on the
        low one, a start on the low one begun at its first point"""
        if kind in self.var:
            return self.var[kind]
        if kind == MID and LOW in self.var:
            return self.var[LOW]
        if kind == HIGH and LOW in self.var:
            return self.var[LOW]
        if kind == START and LOW in self.var:
            lo = self.var[LOW]
            v = V()
            v.P = [lo.P[0], lo.P[0]] + lo.P
            v.src = [-1, -1] + [s + 2 if s >= 0 else -1 for s in lo.src]
            v.adv, v.first, v.extra, v.marks, v.fin = lo.adv, lo.first, lo.extra, lo.marks, lo.fin
            return v
        return self.var[START]


GLYPHS = {}


def glyph(chars, exit=LOW, joins=True, kinds=(START, LOW, MID, HIGH)):
    """the glyph(s) drawn by fn(v, kind[, ch]) for each kind it draws (returning False: none of that kind)"""
    def deco(fn):
        for ch in chars:
            g = G(exit, joins)
            for kind in kinds:
                v = V()
                r = fn(v, kind, ch) if fn.__code__.co_argcount == 3 else fn(v, kind)
                if r is False:
                    continue
                assert v.adv is not None, (ch, kind)
                g.var[kind] = v
            GLYPHS[ch] = g
        return fn
    return deco


# ============================================================== leads
# The leads out of a low exit into a letter that rises at once (a stem, a hump, the overcurve, an ascender) take the
# place of the exit's rise (pre): they turn up right out of the bottom of the letter before, so the space between two
# letters is about the space inside one (the exit's own rise, then the lead's, was twice that). Up to the x-height they
# keep clear of the c's knob, the one letter that reaches over its exit (0.23 before it, at 0.77).
# from a low exit up into the top of a stem (a tip: the stem comes back down over it)
RISE_PRE = [(-0.33, 0.0), (-0.21, 0.05), (-0.10, 0.13)]
RISE = [(0.0, 0.24), (0.08, 0.37), (0.13, 0.53), (0.155, 0.70), (0.17, 0.86), (0.175, 0.96)]
RISE_MID = [(0.17, 0.215), (0.29, 0.27), (0.36, 0.37), (0.40, 0.53), (0.42, 0.70), (0.43, 0.86), (0.435, 0.96)]
# from a bridge over a small round top into a stem coming down
ARCH = [(0.11, 0.672), (0.20, 0.695), (0.265, 0.755), (0.315, 0.84), (0.36, 0.92), (0.425, 0.965), (0.49, 0.955), (0.535, 0.895),
        (0.553, 0.79), (0.556, 0.64)]
# from a low exit over a low hump into a stem (n, m, p, r)
HUMP_PRE = [(-0.33, 0.0), (-0.215, 0.045), (-0.125, 0.14)]
HUMP = [(-0.06, 0.26), (-0.005, 0.385), (0.05, 0.50), (0.115, 0.595), (0.19, 0.65), (0.27, 0.665), (0.33, 0.625), (0.36, 0.545),
        (0.37, 0.44), (0.37, 0.31)]
HUMP_MID = [(0.13, 0.215), (0.215, 0.29), (0.27, 0.41), (0.315, 0.53), (0.375, 0.615), (0.45, 0.66), (0.525, 0.655), (0.575, 0.60),
            (0.598, 0.51), (0.605, 0.40), (0.605, 0.28)]
# from a bridge down into a stem (n, m, p, r after o, b, r, v, w): it comes into the stem at about 0.45 and on down it
DIP = [(0.12, 0.675), (0.24, 0.64), (0.335, 0.57), (0.40, 0.47), (0.43, 0.355), (0.435, 0.24)]
# from a low exit over a round top at the x-height into a stroke coming down (v, x, y, z: the overcurve)
TALL_PRE = [(-0.33, 0.0), (-0.22, 0.05), (-0.135, 0.15)]
TALL = [(-0.07, 0.29), (-0.02, 0.44), (0.03, 0.59), (0.09, 0.73), (0.165, 0.855), (0.255, 0.94), (0.355, 0.98), (0.45, 0.96),
        (0.515, 0.885), (0.545, 0.78), (0.555, 0.66)]
TALL_MID = [(0.13, 0.22), (0.215, 0.30), (0.27, 0.44), (0.32, 0.60), (0.385, 0.75), (0.465, 0.87), (0.56, 0.95), (0.655, 0.975),
            (0.735, 0.925), (0.78, 0.82), (0.795, 0.66)]


def into_stem(v, kind):
    """the lead into a first stroke coming down from the x-height (i, j, s, t, u, w): returns the stem's x and whether
    the pen is at the fork the lead leaves the stem at (xs - 0.08, 0.37), having come back down over it, or on the stem
    at about 0.64 coming down"""
    if kind == START:
        # a word's first letter: in from the left a little above the baseline, the low lead begun part of the way up
        v.start((0.0, 0.13))
        v.add(*sh(RISE, 0.10)).tip().back(4)
        return RISE[-1][0] + 0.10, True
    if kind == LOW:
        v.pre = list(RISE_PRE)
        v.add(*RISE).tip().back(4)
        return RISE[-1][0], True
    if kind == MID:
        v.add(*RISE_MID).tip().back(4)
        return 0.435, True
    v.add(*ARCH)
    return ARCH[-1][0], False


def into_hump(v, kind):
    """the lead into a stem that goes down to the baseline and back up (n, m, p, r): returns the stem's x and how many
    of the lead's points the stem goes back up over; the pen on the stem at about 0.35 coming down"""
    if kind == START:
        v.start((0.0, 0.14))
        v.add(*sh(HUMP, 0.125))
        return HUMP[-1][0] + 0.125, 2
    if kind == LOW:
        v.pre = list(HUMP_PRE)
        v.add(*HUMP)
        return HUMP[-1][0], 2
    if kind == MID:
        v.add(*HUMP_MID)
        return HUMP_MID[-1][0], 2
    v.add(*DIP)
    return DIP[-1][0], 2


def into_over(v, kind):
    """the overcurve: up over a round top into a stroke coming down (v, x, y, z): returns its x, the pen there at
    about 0.66"""
    if kind == START:
        v.start((0.0, 0.15))
        v.add(*sh(TALL, 0.135))
        return TALL[-1][0] + 0.135
    if kind == LOW:
        v.pre = list(TALL_PRE)
        v.add(*TALL)
        return TALL[-1][0]
    if kind == MID:
        v.add(*TALL_MID)
        return TALL_MID[-1][0]
    v.add(*ARCH)
    return ARCH[-1][0]


def foot(v, xs, fork, adv=None):
    """from a stem at xs down into its round foot and out (low): the advance xs + 0.62"""
    if fork:
        v.add((xs - 0.065, 0.21), (xs - 0.02, 0.08), (xs + 0.08, 0.01), (xs + 0.21, -0.015))
    else:
        v.add((xs, 0.49), (xs - 0.012, 0.31), (xs, 0.15), (xs + 0.05, 0.045), (xs + 0.15, -0.005))
    v.exit(LOW, adv if adv is not None else xs + 0.62)


def stem_up(v, x, top=0.96):
    """from the bottom of a bowl (heading right at about x - 0.4) up into a stem at x with a tip at its top"""
    v.add((x - 0.26, 0.03), (x - 0.15, 0.13), (x - 0.085, 0.28), (x - 0.045, 0.46), (x - 0.02, 0.65), (x - 0.006, 0.82),
          (x, top))
    v.tip()


# ============================================================== the lowercase
@glyph('i')
def _i(v, kind):
    xs, fork = into_stem(v, kind)
    foot(v, xs, fork)
    v.marks.append(dot(xs + 0.03, 1.42))


@glyph('t')
def _t(v, kind):
    if kind == START:
        xs = 0.30
        v.start((xs, 1.44))
        v.add((xs - 0.005, 1.26), (xs - 0.01, 1.07), (xs - 0.012, 0.88), (xs - 0.012, 0.69))
        foot(v, xs - 0.012, False)
    elif kind == HIGH:
        xs = 0.50
        v.add((0.14, 0.675), (0.27, 0.70), (0.37, 0.78), (0.43, 0.90), (0.465, 1.05), (0.48, 1.21), (0.49, 1.37), (xs, 1.46))
        v.tip().back(3)
        v.add((xs - 0.03, 0.88), (xs - 0.03, 0.71))
        foot(v, xs - 0.03, False)
    elif kind == LOW:
        # the stem's (RISE's) lead, on up
        xs = 0.197
        v.pre = list(RISE_PRE)
        v.add((0.0, 0.24), (0.08, 0.37), (0.135, 0.53), (0.16, 0.70), (0.175, 0.87), (0.185, 1.04), (0.19, 1.21), (0.195, 1.38), (xs, 1.46))
        v.tip().back(6)
        foot(v, xs - 0.03, True, xs + 0.60)
    else:
        xs = 0.46
        v.add((0.17, 0.215), (0.29, 0.27), (0.36, 0.37), (0.40, 0.53), (0.425, 0.70), (0.44, 0.87), (0.45, 1.04), (0.455, 1.21), (0.46, 1.38),
              (0.462, 1.46)).tip().back(6)
        foot(v, xs - 0.03, True, xs + 0.60)
    xb = {START: 0.29, HIGH: 0.47, LOW: 0.167}.get(kind, 0.43)
    if kind == LOW:
        # the stem as near the letter before as a stem's: the cross's left arm shorter, clear of the c's knob
        v.marks.append(stroke((xb - 0.21, 0.93), (xb - 0.06, 0.965), (xb + 0.09, 0.99), (xb + 0.24, 0.98), (xb + 0.38, 0.955)))
        return
    v.marks.append(stroke((xb - 0.30, 0.93), (xb - 0.12, 0.97), (xb + 0.06, 0.99), (xb + 0.24, 0.98), (xb + 0.38, 0.955)))


@glyph('u')
def _u(v, kind):
    xs, fork = into_stem(v, kind)
    if fork:
        v.add((xs - 0.065, 0.21), (xs - 0.02, 0.08), (xs + 0.08, 0.01), (xs + 0.22, -0.015))
    else:
        v.add((xs, 0.49), (xs - 0.012, 0.31), (xs, 0.15), (xs + 0.06, 0.04), (xs + 0.19, -0.01))
    x2 = xs + 0.70
    v.add((x2 - 0.33, 0.01))
    v.add((x2 - 0.20, 0.10), (x2 - 0.11, 0.24), (x2 - 0.06, 0.42), (x2 - 0.03, 0.61), (x2 - 0.01, 0.80), (x2, 0.96)).tip().back(4)
    foot(v, x2, True)


@glyph('w', exit=MID)
def _w(v, kind):
    # a stem into a round bottom, up to a point at the middle (a tip) and back down it, round a second bottom, up to a
    # point at the right and back down it a little, out of the sag
    xs, fork = into_stem(v, kind)
    if fork:
        v.add((xs - 0.065, 0.21), (xs - 0.02, 0.08), (xs + 0.09, 0.0))
    else:
        v.add((xs, 0.49), (xs - 0.012, 0.31), (xs + 0.01, 0.13), (xs + 0.10, 0.02))
    v.add(*sh([(0.235, -0.012), (0.355, 0.045), (0.445, 0.16), (0.50, 0.32), (0.53, 0.50), (0.55, 0.68), (0.56, 0.86)], xs)).tip().back(4)
    v.add(*sh([(0.52, 0.04), (0.64, -0.012), (0.78, 0.015), (0.895, 0.115), (0.975, 0.285), (1.02, 0.48), (1.04, 0.68), (1.045, 0.87)],
              xs)).tip().back(2)
    v.add((xs + 1.09, 0.31))
    v.exit(MID, xs + 1.38)


@glyph('v', exit=MID)
def _v(v, kind):
    xs = into_over(v, kind)
    # down the left arm into a round point, up the right arm to a point (a tip), back down it to the sag
    v.add(*sh([(0.03, 0.47), (0.08, 0.29), (0.155, 0.13), (0.26, 0.025), (0.385, 0.0), (0.50, 0.05), (0.585, 0.18), (0.635, 0.36),
               (0.655, 0.56), (0.66, 0.74), (0.66, 0.90)], xs)).tip().back(2)
    v.add((xs + 0.72, 0.36))
    v.exit(MID, xs + 1.00)


W_ARCH = 0.74   # stem to stem in n, m and h


def _arch_unit(n=2.4, angles=(168, 147, 126, 105, 84, 63, 42, 21)):
    """the arch at width 1: points on the top half of the superellipse |2x - 1|^n + |(y - 0.6) / 0.4|^n = 1"""
    pts = []
    for a in angles:
        c, s_ = math.cos(math.radians(a)), math.sin(math.radians(a))
        pts.append((round(0.5 + 0.5 * math.copysign(abs(c) ** (2 / n), c), 4), round(0.60 + 0.40 * abs(s_) ** (2 / n), 4)))
    return pts


ARCH_UNIT = _arch_unit()


def arch(v, xs, w=W_ARCH):
    """from the foot of a stem at xs (the pen back up it to about 0.4) over a round arch down into the next stem at
    xs + w: the pen there at about 0.77 coming down"""
    v.add(*[(xs + x * w, y) for x, y in ARCH_UNIT])


@glyph('n')
def _n(v, kind):
    xs, k = into_hump(v, kind)
    v.add((xs, 0.13), (xs - 0.009, 0.018)).tip().back(1 + k)
    arch(v, xs)
    foot(v, xs + W_ARCH, False)


@glyph('m')
def _m(v, kind):
    xs, k = into_hump(v, kind)
    v.add((xs, 0.133), (xs - 0.009, 0.018)).tip().back(1 + k)
    arch(v, xs)
    x2 = xs + W_ARCH
    v.add((x2, 0.47), (x2, 0.25), (x2 - 0.004, 0.02)).tip().back(2)
    arch(v, x2)
    foot(v, x2 + W_ARCH, False)


@glyph('r', exit=HIGH)
def _r(v, kind):
    xs, k = into_hump(v, kind)
    v.add((xs, 0.13), (xs - 0.009, 0.018)).tip().back(1 + k)
    # the shoulder: the arch's rise, out along its top into a bridge
    v.add(*[(xs + x * W_ARCH, y) for x, y in ARCH_UNIT[:5]])
    v.exit(HIGH, xs + 0.80)


@glyph('p')
def _p(v, kind):
    xs, k = into_hump(v, kind)
    v.add((xs, 0.13), (xs - 0.005, -0.10), (xs - 0.01, -0.33), (xs - 0.015, -0.56), (xs - 0.02, -0.80)).tip().back(4 + k)
    # up from the stem over the bowl (the arch), down its right side, round along its bottom back to the stem (a tip),
    # back along the bottom, out
    arch(v, xs)
    v.add((xs + W_ARCH + 0.005, 0.50), (xs + W_ARCH - 0.04, 0.29), (xs + W_ARCH - 0.13, 0.12), (xs + 0.45, 0.025), (xs + 0.30, 0.01),
          (xs + 0.15, 0.04), (xs - 0.001, 0.10)).tip().back(3)
    v.add((xs + 0.59, -0.025))
    # out far enough past the bowl for a lead that rises at once
    v.exit(LOW, xs + 1.14)


def descender(v, x, adv=None):
    """from a stem at x down into a loop under the baseline, round to the left and back up across the stem, out low"""
    v.add((x, 0.02), (x - 0.01, -0.20), (x - 0.03, -0.42), (x - 0.07, -0.62), (x - 0.15, -0.78), (x - 0.27, -0.86), (x - 0.39, -0.83),
          (x - 0.45, -0.71), (x - 0.43, -0.54), (x - 0.34, -0.36), (x - 0.21, -0.20), (x - 0.06, -0.08), (x + 0.10, 0.0))
    v.exit(LOW, adv if adv is not None else x + 0.56)


@glyph('j')
def _j(v, kind):
    xs, fork = into_stem(v, kind)
    if fork:
        v.add((xs - 0.07, 0.20))
    else:
        v.add((xs, 0.47), (xs - 0.01, 0.26))
    descender(v, xs - 0.075 if fork else xs - 0.015)
    v.marks.append(dot(xs + 0.03, 1.42))


@glyph('y')
def _y(v, kind):
    xs = into_over(v, kind)
    # down into a round bowl, up the right stem (a tip), back down it and on down into the loop
    v.add((xs + 0.01, 0.46), (xs, 0.27), (xs + 0.03, 0.12), (xs + 0.12, 0.02), (xs + 0.26, -0.01))
    x2 = xs + 0.66
    stem_up(v, x2)
    v.back(3)
    v.add((x2 - 0.05, 0.27))
    descender(v, x2 - 0.05)


def on_curve(P, y):
    """where the uniform cubic B-spline of the control points P first rises through height y"""
    for i in range(len(P) - 3):
        p0, p1, p2, p3 = P[i:i + 4]
        prev = None
        for k in range(201):
            t = k / 200
            b = ((1 - t) ** 3 / 6, (3 * t ** 3 - 6 * t ** 2 + 4) / 6, (-3 * t ** 3 + 3 * t ** 2 + 3 * t + 1) / 6, t ** 3 / 6)
            q = (sum(w * p[0] for w, p in zip(b, (p0, p1, p2, p3))), sum(w * p[1] for w, p in zip(b, (p0, p1, p2, p3))))
            if prev is not None and prev[1] <= y <= q[1]:
                f = (y - prev[1]) / (q[1] - prev[1]) if q[1] > prev[1] else 0.0
                return (round(prev[0] + (q[0] - prev[0]) * f, 4), y)
            prev = q
    raise ValueError('the curve never reaches %.3f' % y)


# ---- s: Palmer's - up to a little over the x-height, round over a small top, down the right side swelling out, round
# the bottom back to its upstroke (a point on it: its end on the upstroke's middle line), back along the bottom and out
S_UP = [(0.11, 0.245), (0.18, 0.39), (0.225, 0.56), (0.25, 0.73), (0.27, 0.87)]
S_TOP = [(0.30, 0.98), (0.36, 1.05), (0.44, 1.07), (0.515, 1.03)]
S_BOWL = [(0.565, 0.93), (0.60, 0.78), (0.615, 0.615), (0.61, 0.45), (0.58, 0.30), (0.52, 0.17), (0.43, 0.075), (0.31, 0.025), (0.19, 0.03),
          (0.105, 0.085)]


@glyph('s')
def _s(v, kind):
    if kind == START:
        # in from a little below where the bottom comes back to the upstroke (begun right there, the two meet in a beak)
        start = (-0.09, 0.05)
        v.start(start)
        v.add(*S_UP, *S_TOP, *S_BOWL)
        v.add(on_curve([start] * 3 + S_UP[:3], 0.205)).tip().back(3)
        dx = 0.0
    elif kind == LOW:
        dx = 0.0
        v.add(*S_UP, *S_TOP, *S_BOWL)
        v.add(on_curve(TAIL[LOW][-2:] + S_UP[:3], 0.20)).tip().back(3)
    elif kind == MID:
        dx = 0.08
        up = [(0.12, 0.205), (0.21, 0.265), (0.27, 0.385), (0.305, 0.56), (0.33, 0.73), (0.35, 0.87)]
        v.add(*up, *sh(S_TOP + S_BOWL[:-2], dx), (0.20 + dx, 0.045), (0.14 + dx, 0.11))
        v.add(on_curve(TAIL[MID] + up[:3], 0.25)).tip().back(3)
    else:
        # from a bridge into the top: the bottom ends free at the left, hooking up a little
        dx = 0.05
        v.add((0.12, 0.68), (0.21, 0.70), (0.27, 0.76), (0.31, 0.86), *sh(S_TOP + S_BOWL, dx), (0.11 + dx, 0.17)).tip().back(3)
    v.add((0.43 + dx, 0.0))
    v.exit(LOW, 0.95 + dx)


@glyph('x')
def _x(v, kind):
    # the overcurve, on down into a round foot and out; the cross from the top right after the word
    xs = into_over(v, kind)
    v.add((xs + 0.01, 0.46), (xs + 0.025, 0.27), (xs + 0.08, 0.11), (xs + 0.18, 0.02), (xs + 0.31, -0.01))
    v.exit(LOW, xs + 0.80)
    v.marks.append(stroke((xs + 0.30, 0.94), (xs + 0.162, 0.824), (xs + 0.044, 0.688), (xs - 0.051, 0.535), (xs - 0.124, 0.371),
                          (xs - 0.171, 0.197), (xs - 0.19, 0.018)))


@glyph('z')
def _z(v, kind):
    # the overcurve, down to a point at the middle left (a tip), down into a loop under the baseline, out
    xs = into_over(v, kind)
    v.add((xs - 0.02, 0.52), (xs - 0.10, 0.42), (xs - 0.22, 0.36)).tip()
    v.add((xs - 0.07, 0.31), (xs + 0.04, 0.20))
    descender(v, xs + 0.08, xs + 0.66)


# ---- the downcurve letters (c, a, d, g, q): an upright oval. The lead comes up into its left side at mid-height and
# on up it, over the top to the knob (a tip), back over the top and down the left side (the same curve), round the
# bottom.
OVAL = (0.50, 0.48, 0.34, 0.50)      # its middle, half width, half height


def oval(a, dx=0.0):
    cx, cy, rx, ry = OVAL
    return (round(cx + dx + rx * math.cos(math.radians(a)), 4), round(cy + ry * math.sin(math.radians(a)), 4))


BOWL_UP = [oval(a) for a in (180, 157, 134, 111, 88)]            # up the left side to the top
BOWL_DOWN = [oval(a) for a in (203, 226, 249, 272)]              # from the left side round the bottom


def into_bowl(v, kind, top):
    """into the bowl at the left, over its top to its knob (top: the control points after the top, the last the knob, a
    tip) and back over the top and down its left side round its bottom: returns the bowl's x offset"""
    k = len(top) - 1
    if kind == START:
        v.start(top[-1])
        v.add(*reversed(top[:-1]), *reversed(BOWL_UP), *BOWL_DOWN)
        return 0.0
    if kind == HIGH:
        # from the bridge into the top's left: down the left side after the knob
        dx = 0.10
        v.add((0.12, 0.69), (0.20, 0.72))
        v.add(*sh(BOWL_UP[2:] + top, dx)).tip().back(k + 3)
        v.add(*sh(BOWL_UP[1::-1] + BOWL_DOWN, dx))
        return dx
    dx = 0.0 if kind == LOW else 0.14
    lead = [(0.09, 0.225), (0.14, 0.34)] if kind == LOW else [(0.15, 0.21), (0.245, 0.27), (0.285, 0.36)]
    v.add(*lead, *sh(BOWL_UP + top, dx)).tip().back(k + len(BOWL_UP))
    v.add(*sh(BOWL_DOWN, dx))
    return dx


C_TOP = [oval(65), oval(43), (0.765, 0.745)]     # on round to the knob, ending down
A_TOP = [oval(65), (0.75, 0.89), (0.835, 0.80)]  # on round into the stem's top
A_KNOB = A_TOP[-1]


@glyph('c')
def _c(v, kind):
    dx = into_bowl(v, kind, C_TOP)
    v.add((dx + 0.65, -0.012))
    v.exit(LOW, dx + 1.08)


def bowl_close(v, dx):
    """round the bowl's bottom up its right side to the knob again (a tip): the pen there"""
    v.add(*sh([oval(295), oval(318), oval(341), (0.835, 0.48), (0.835, 0.64)], dx))


@glyph('a')
def _a(v, kind):
    dx = into_bowl(v, kind, A_TOP)
    bowl_close(v, dx)
    v.add(sh([A_KNOB], dx)[0]).tip().back(2)
    v.add(*sh([(0.84, 0.30), (0.855, 0.13), (0.90, 0.035), (0.99, -0.008)], dx))
    v.exit(LOW, dx + 1.38)


@glyph('d')
def _d(v, kind):
    dx = into_bowl(v, kind, A_TOP)
    bowl_close(v, dx)
    v.add(*sh([A_KNOB, (0.836, 0.97), (0.838, 1.14), (0.84, 1.31), (0.843, 1.48), (0.846, 1.65), (0.849, 1.82), (0.85, 1.94)], dx)).tip().back(8)
    v.add(*sh([(0.84, 0.30), (0.855, 0.13), (0.90, 0.035), (0.99, -0.008)], dx))
    v.exit(LOW, dx + 1.38)


@glyph('g')
def _g(v, kind):
    dx = into_bowl(v, kind, A_TOP)
    bowl_close(v, dx)
    v.add(sh([A_KNOB], dx)[0]).tip().back(2)
    v.add(*sh([(0.835, 0.24)], dx))
    descender(v, dx + 0.835, dx + 1.39)


@glyph('q')
def _q(v, kind):
    dx = into_bowl(v, kind, A_TOP)
    bowl_close(v, dx)
    v.add(sh([A_KNOB], dx)[0]).tip().back(2)
    v.add(*sh([(0.835, 0.24), (0.835, 0.04), (0.83, -0.16), (0.825, -0.38), (0.83, -0.60), (0.865, -0.76), (0.94, -0.85), (1.04, -0.85),
               (1.125, -0.75), (1.16, -0.58), (1.135, -0.39), (1.06, -0.20), (0.95, -0.06), (0.838, 0.03)], dx)).tip()
    v.add(*sh([(0.97, 0.0)], dx))
    v.exit(LOW, dx + 1.44)


# ---- o: written clockwise - in at the lower left, up the left side, over the top, down the right, round the bottom,
# up the left and over the top a second time (the same curve), out of the top right along a bridge
O_OVAL = (0.50, 0.48, 0.355, 0.50)


def opt(a, dx=0.0):
    cx, cy, rx, ry = O_OVAL
    return (round(cx + dx + rx * math.cos(math.radians(a)), 4), round(cy + ry * math.sin(math.radians(a)), 4))


O_UP = [opt(a) for a in (196, 174, 152, 130, 108, 86, 64, 42)]       # up the left side and over the top
O_ROUND = [opt(a) for a in (20, -2, -24, -46, -68, -90, -112, -134)]   # down the right side, round the bottom


@glyph('o', exit=HIGH)
def _o(v, kind):
    n = len(O_UP) - 1
    if kind == START:
        # it starts on the curve of its second pass (the B-spline's point at O_UP[0]): that pass covers its end
        v.start(tuple(round((a + 4 * b + c) / 6, 4) for a, b, c in zip(O_ROUND[-1], O_UP[0], O_UP[1])))
        v.add(*O_UP[1:], *O_ROUND, O_UP[0])
        v.same(3, 3 + n - 1)
        v.exit(HIGH, 1.06)
        return
    if kind == HIGH:
        # from the bridge into its left side at 0.75
        dx = 0.06
        v.add((0.13, 0.665), (0.22, 0.67))
        o1 = v.here()
        v.add(*sh(O_UP[3:] + O_ROUND + O_UP[:3], dx))
        v.same(o1, o1 + n - 3)
        v.exit(HIGH, dx + 1.06)
        return
    dx = 0.0 if kind == LOW else 0.12
    v.add(*([(0.08, 0.205), (0.14, 0.255)] if kind == LOW else [(0.15, 0.205), (0.25, 0.245)]))
    o0 = v.here()
    v.add(*sh(O_UP + O_ROUND, dx))
    v.same(o0, o0 + n)
    v.exit(HIGH, dx + 1.06)


# ---- e: the lead rises to the right as its crossbar, round over the top, down the left side crossing the lead,
# round the bottom and out
E_LOW = [(0.12, 0.21), (0.27, 0.30), (0.41, 0.42), (0.53, 0.565), (0.60, 0.725), (0.595, 0.875), (0.52, 0.97), (0.40, 1.0), (0.275, 0.955),
         (0.17, 0.83), (0.105, 0.65), (0.09, 0.44), (0.13, 0.245), (0.225, 0.095), (0.36, 0.015), (0.52, -0.012)]


@glyph('e')
def _e(v, kind):
    if kind == MID:
        v.add((0.14, 0.20), (0.29, 0.245), (0.44, 0.33), (0.56, 0.46), (0.635, 0.62), (0.645, 0.79), (0.59, 0.93), (0.48, 1.0), (0.355, 0.985),
              (0.24, 0.89), (0.165, 0.73), (0.135, 0.54), (0.155, 0.35), (0.225, 0.18), (0.33, 0.06), (0.47, 0.0), (0.63, -0.012))
        v.exit(LOW, 1.08)
        return
    if kind == HIGH:
        # the bridge on into its crossbar
        v.add((0.14, 0.655), (0.28, 0.60), (0.43, 0.575), (0.57, 0.59), (0.675, 0.66), (0.71, 0.775), (0.67, 0.89), (0.57, 0.97), (0.445, 0.995),
              (0.325, 0.95), (0.24, 0.835), (0.20, 0.67), (0.205, 0.49), (0.245, 0.31), (0.32, 0.15), (0.43, 0.04), (0.57, -0.01), (0.72, -0.01))
        v.exit(LOW, 1.12)
        return
    if kind == START:
        # in from the lower left, clear of where the left side crosses the lead
        v.start((0.0, 0.12))
        v.add(*sh(E_LOW, 0.06))
        v.exit(LOW, 1.04)
        return
    v.add(*E_LOW)
    v.exit(LOW, 0.98)


# ---- the loop letters: up the right of a tall loop, round over its top, down its left side crossing the upstroke
L_UP = [(0.13, 0.245), (0.27, 0.40), (0.39, 0.60), (0.49, 0.84), (0.565, 1.10), (0.605, 1.36), (0.61, 1.58), (0.57, 1.745), (0.48, 1.83),
        (0.375, 1.80), (0.30, 1.67), (0.25, 1.46), (0.215, 1.20), (0.19, 0.92), (0.175, 0.63), (0.17, 0.35)]


def into_loop(v, kind):
    """up into the loop and down its left side to 0.35: returns the x offset of the loop (its stem at 0.17 past it)"""
    if kind == START:
        # in from below where the left side crosses
        v.start((0.0, 0.12))
        v.add(*sh(L_UP, 0.06))
        return 0.06
    if kind == LOW:
        v.add(*L_UP)
        return 0.0
    if kind == MID:
        v.add((0.16, 0.21), (0.27, 0.28))
        v.add(*sh(L_UP[1:], 0.08))
        return 0.08
    # from a bridge: it closes the loop where the left side comes down across it
    v.add((0.13, 0.675), (0.26, 0.70), (0.37, 0.78), (0.46, 0.91))
    v.add(*L_UP[4:])
    return 0.0


# b, h, k: a tall stem the pen goes up and comes back down over (as the d's, the t's) - a loop there, the l's, would
# cross the arch, the bowl, the arm
ASC = [(0.31, 0.71), (0.3197, 0.90), (0.3294, 1.09), (0.3392, 1.28), (0.3489, 1.47), (0.3586, 1.66), (0.3678, 1.84), (0.373, 1.94)]


def into_ascender(v, kind):
    """up to the top of a tall stem and back down it: returns the stem's x, the pen on it at 0.71"""
    if kind == START:
        v.start(ASC[-1])
        v.add(*reversed(ASC[:-1]))
        return 0.31
    if kind == HIGH:
        dx = 0.12
        v.add((0.14, 0.672), (0.26, 0.695), (0.35, 0.76), (0.42, 0.85))
        v.add(*sh(ASC[1:], dx)).tip().back(len(ASC) - 2)
        v.add(sh(ASC, dx)[0])
        return 0.31 + dx
    if kind == LOW:
        dx = -0.13
        v.pre = list(RISE_PRE)
        v.add((0.0, 0.24), (0.08, 0.37), (0.14, 0.53))
    else:
        dx = 0.145
        v.add((0.17, 0.215), (0.29, 0.28), (0.37, 0.40), (0.42, 0.55))
    v.add(*sh(ASC, dx)).tip().back(len(ASC) - 1)
    return 0.31 + dx


@glyph('l')
def _l(v, kind):
    dx = into_loop(v, kind)
    v.add(*sh([(0.175, 0.15), (0.235, 0.035), (0.355, -0.015), (0.50, -0.012)], dx))
    v.exit(LOW, dx + 1.0)


@glyph('h')
def _h(v, kind):
    xs = into_ascender(v, kind)
    v.add((xs - 0.01, 0.52), (xs - 0.02, 0.33), (xs - 0.028, 0.16), (xs - 0.035, 0.02)).tip().back(2)
    xs -= 0.02
    arch(v, xs)
    foot(v, xs + W_ARCH, False)


@glyph('k')
def _k(v, kind):
    xs = into_ascender(v, kind)
    v.add((xs - 0.01, 0.52), (xs - 0.02, 0.33), (xs - 0.028, 0.16), (xs - 0.035, 0.02)).tip().back(2)
    xs -= 0.02
    # out of the stem up into a small round loop at the right (its buckle), back to the stem (a point), the leg down
    # to the right and out
    v.add((xs + 0.03, 0.53), (xs + 0.10, 0.72), (xs + 0.22, 0.87), (xs + 0.37, 0.94), (xs + 0.50, 0.90), (xs + 0.555, 0.78), (xs + 0.51, 0.65),
          (xs + 0.39, 0.55), (xs + 0.22, 0.49)).tip()
    v.add((xs + 0.36, 0.43), (xs + 0.47, 0.33), (xs + 0.53, 0.19), (xs + 0.585, 0.07), (xs + 0.68, 0.005), (xs + 0.81, -0.01))
    v.exit(LOW, xs + 1.30)


@glyph('b', exit=HIGH)
def _b(v, kind):
    xs = into_ascender(v, kind)
    # down round the bottom, up the bowl's right side, over its top back to the stem (a tip), back over the top, out
    v.add((xs - 0.01, 0.52), (xs - 0.02, 0.33), (xs - 0.02, 0.16), (xs + 0.02, 0.05), (xs + 0.13, -0.01), (xs + 0.28, -0.02), (xs + 0.43, 0.04),
          (xs + 0.55, 0.16), (xs + 0.61, 0.33), (xs + 0.62, 0.52), (xs + 0.58, 0.69), (xs + 0.49, 0.80), (xs + 0.36, 0.855), (xs + 0.22, 0.835),
          (xs + 0.10, 0.785), (xs + 0.001, 0.73)).tip().back(4)
    v.exit(HIGH, xs + 0.76)


@glyph('f')
def _f(v, kind):
    dx = into_loop(v, kind)
    xs = dx + 0.17
    # on down the stem under the baseline, round to the right and back up to the stem (a point), out
    v.add((xs + 0.0, 0.14), (xs - 0.005, -0.08), (xs - 0.01, -0.30), (xs - 0.01, -0.52), (xs + 0.01, -0.71), (xs + 0.08, -0.84),
          (xs + 0.20, -0.87), (xs + 0.30, -0.78), (xs + 0.32, -0.60), (xs + 0.27, -0.40), (xs + 0.18, -0.21), (xs + 0.08, -0.08), (xs - 0.004, -0.04)).tip()
    v.add((xs + 0.13, -0.035), (xs + 0.25, -0.025))
    v.exit(LOW, xs + 0.70)


# ============================================================== capitals, digits, marks (cap height about 1.75)
def ell(cx, cy, rx, ry, a0, a1, step=0.17):
    """points on an ellipse from angle a0 to a1 (degrees, counter-clockwise when a1 > a0), about `step` apart"""
    n = max(2, int(math.ceil(abs(math.radians(a1 - a0)) * max(rx, ry) / step)))
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * k / n)), cy + ry * math.sin(math.radians(a0 + (a1 - a0) * k / n)))
            for k in range(n + 1)]


def bow(p0, p1, sag, step=0.17):
    """points along a circular arc from p0 to p1 bowing `sag` to the left of the way (negative: to the right)"""
    (x0, y0), (x1, y1) = p0, p1
    c = math.dist(p0, p1)
    n = max(2, int(math.ceil(c / step)))
    nx, ny = -(y1 - y0) / c, (x1 - x0) / c
    out = []
    for k in range(n + 1):
        t = k / n
        h = 4 * sag * t * (1 - t)          # a parabola: near enough an arc for a small bow
        out.append((x0 + (x1 - x0) * t + nx * h, y0 + (y1 - y0) * t + ny * h))
    return out


def closed(v, pts, run_on):
    """a closed round shape: pts once round and on over its first `run_on` points again; it starts on the curve of
    that second pass, which covers its end"""
    v.start(tuple((a + 4 * b + c) / 6 for a, b, c in zip(pts[-1], pts[0], pts[1])))
    v.add(*pts[1:], pts[0])
    v.same(3, 3 + run_on - 2)


CAP = 1.75
STEM = [(0.25, 1.72), (0.25, 1.30), (0.245, 0.86), (0.24, 0.43), (0.24, 0.02)]


def cap(chars, exit=LOW):
    return glyph(chars, exit=exit, joins=False, kinds=(START,))


@cap('A')
def _A(v, kind):
    # a large a: from the knob round over the top, down the left, round the bottom, up the right side to the knob (a
    # tip), back down it, out
    knob = (1.10, 1.30)
    v.start(knob)
    v.add(*ell(0.64, 0.86, 0.50, 0.88, 40, 330)[1:])
    v.add((1.115, 0.66), (1.12, 0.98), knob).tip().back(2)
    v.add((1.12, 0.40), (1.13, 0.18), (1.18, 0.05), (1.29, -0.005))
    v.exit(LOW, 1.76)


@cap('B', exit=NONE)
def _B(v, kind):
    v.first.append(stroke(*STEM))
    v.start(STEM[0])
    v.add((0.50, 1.79), (0.78, 1.74), (0.92, 1.55), (0.88, 1.30), (0.70, 1.10), (0.48, 0.99), (0.247, 0.95)).tip()
    v.add((0.50, 0.93), (0.76, 0.88), (0.98, 0.72), (1.05, 0.46), (0.93, 0.18), (0.70, 0.03), (0.46, -0.005), STEM[-1])
    v.end(1.26)


@cap('C')
def _C(v, kind):
    v.start((1.06, 1.42))
    v.add((1.00, 1.60), *ell(0.64, 0.86, 0.52, 0.88, 65, 285)[1:])
    v.add((0.96, -0.01))
    v.exit(LOW, 1.47)


@cap('D', exit=NONE)
def _D(v, kind):
    # the stem, then from its top round the bowl to its foot (a tip), back along the bottom and out
    v.first.append(stroke(*STEM))
    v.start(STEM[0])
    v.add(*ell(0.25, 0.87, 0.90, 0.85, 82, -82)[1:], STEM[-1])
    v.end(1.38)


@cap('E')
def _E(v, kind):
    v.start((0.96, 1.50))
    v.add((0.86, 1.70), (0.62, 1.78), (0.40, 1.72), (0.28, 1.55), (0.30, 1.33), (0.42, 1.15), (0.60, 1.02)).tip()
    v.add((0.40, 0.95), (0.22, 0.80), (0.12, 0.55), (0.14, 0.30), (0.27, 0.10), (0.48, 0.0), (0.72, -0.01))
    v.exit(LOW, 1.25)


@cap('F')
def _F(v, kind):
    v.first.append(stroke((0.10, 1.52), (0.26, 1.68), (0.46, 1.755), (0.68, 1.73), (0.88, 1.68), (1.06, 1.70), (1.20, 1.79)))
    v.start((0.68, 1.7234))
    v.add((0.675, 1.30), (0.665, 0.85), (0.655, 0.42), (0.665, 0.16), (0.71, 0.04), (0.82, -0.01))
    v.exit(LOW, 1.32)
    v.marks.append(stroke((0.40, 0.90), (0.62, 0.95), (0.86, 0.93)))


@cap('G')
def _G(v, kind):
    v.start((1.06, 1.42))
    v.add((1.00, 1.60), *ell(0.64, 0.86, 0.52, 0.88, 65, 300)[1:])
    v.add((1.08, 0.26), (1.12, 0.52), (1.12, 0.80)).tip().back(2)
    v.add((1.12, 0.30), (1.13, 0.14), (1.18, 0.04), (1.29, -0.005))
    v.exit(LOW, 1.76)
    v.marks.append(stroke((0.84, 0.80), (0.98, 0.815), (1.12, 0.80)))


@cap('H')
def _H(v, kind):
    v.first.append(stroke(*STEM))
    v.start((0.25, 0.88))
    v.add((0.45, 0.92), (0.68, 0.94), (0.88, 0.97), (1.00, 1.04), (1.045, 1.18), (1.055, 1.36), (1.06, 1.54), (1.06, 1.72)).tip().back(3)
    v.add((1.05, 0.95), (1.05, 0.70), (1.05, 0.45), (1.055, 0.22), (1.09, 0.07), (1.19, 0.0))
    v.exit(LOW, 1.70)


@cap('I')
def _I(v, kind):
    v.start((0.12, 1.52))
    v.add((0.26, 1.70), (0.46, 1.76), (0.60, 1.66), (0.625, 1.32), (0.62, 0.88), (0.615, 0.44), (0.625, 0.16), (0.67, 0.04), (0.78, -0.01))
    v.exit(LOW, 1.28)


@cap('J')
def _J(v, kind):
    v.start((0.22, 1.52))
    v.add((0.38, 1.70), (0.58, 1.76), (0.72, 1.66), (0.74, 1.32), (0.72, 0.90), (0.68, 0.46))
    descender(v, 0.66)


@cap('K')
def _K(v, kind):
    v.first.append(stroke(*STEM))
    v.start((1.06, 1.70))
    v.add((0.96, 1.52), (0.84, 1.34), (0.70, 1.17), (0.55, 1.03), (0.40, 0.93), (0.25, 0.875)).tip()
    v.add((0.52, 0.80), (0.68, 0.62), (0.79, 0.40), (0.87, 0.18), (0.96, 0.04), (1.08, -0.005))
    v.exit(LOW, 1.58)


@cap('L')
def _L(v, kind):
    v.start((0.66, 1.47))
    v.add((0.78, 1.66), (0.72, 1.80), (0.55, 1.73), (0.46, 1.46), (0.42, 1.05), (0.38, 0.62), (0.31, 0.26), (0.21, 0.07), (0.10, 0.02)).tip()
    v.add((0.30, 0.05), (0.52, 0.02), (0.74, -0.01))
    v.exit(LOW, 1.28)


@cap('M')
def _M(v, kind):
    v.start((0.10, 0.04))
    v.add((0.13, 0.42), (0.16, 0.82), (0.20, 1.20), (0.26, 1.52), (0.38, 1.72), (0.52, 1.69), (0.60, 1.46), (0.62, 1.10), (0.62, 0.70),
          (0.62, 0.32), (0.615, 0.02)).tip().back(3)
    v.add((0.66, 1.42), (0.76, 1.66), (0.92, 1.74), (1.06, 1.62), (1.12, 1.35), (1.13, 0.95), (1.13, 0.55), (1.14, 0.22), (1.19, 0.05),
          (1.30, -0.005))
    v.exit(LOW, 1.80)


@cap('N')
def _N(v, kind):
    v.start((0.10, 0.04))
    v.add((0.13, 0.42), (0.16, 0.82), (0.20, 1.20), (0.26, 1.52), (0.36, 1.72), (0.48, 1.66), (0.56, 1.36), (0.65, 0.96), (0.75, 0.55),
          (0.85, 0.18), (0.96, 0.02), (1.06, 0.10), (1.11, 0.30), (1.13, 0.60), (1.145, 0.95), (1.155, 1.35), (1.16, 1.74)).tip().back(5)
    v.add((1.15, 0.02), (1.28, -0.01))
    v.exit(LOW, 1.76)


@cap('O', exit=NONE)
def _O(v, kind):
    closed(v, ell(0.64, 0.88, 0.52, 0.88, 100, 460)[:-1], 4)
    v.end(1.40)


@cap('P', exit=HIGH)
def _P(v, kind):
    v.first.append(stroke(*STEM))
    v.start(STEM[0])
    v.add((0.52, 1.79), (0.80, 1.72), (0.96, 1.52), (0.95, 1.25), (0.80, 1.04), (0.56, 0.95), (0.38, 0.94), (0.247, 0.95)).tip().back(2)
    v.add((0.73, 0.85))
    v.exit(HIGH, 1.02)


@cap('Q')
def _Q(v, kind):
    o = V()
    closed(o, ell(0.64, 0.88, 0.52, 0.88, 100, 460)[:-1], 4)
    o.tip()
    v.first.append(o)
    v.start((0.60, 0.34))
    v.add((0.74, 0.16), (0.90, 0.05), (1.06, 0.0))
    v.exit(LOW, 1.54)


@cap('R')
def _R(v, kind):
    v.first.append(stroke(*STEM))
    v.start(STEM[0])
    v.add((0.52, 1.79), (0.80, 1.72), (0.96, 1.52), (0.95, 1.25), (0.80, 1.04), (0.56, 0.95), (0.38, 0.94), (0.247, 0.95)).tip()
    v.add((0.50, 0.88), (0.67, 0.68), (0.79, 0.42), (0.87, 0.18), (0.96, 0.04), (1.08, -0.005))
    v.exit(LOW, 1.58)


@cap('S', exit=NONE)
def _S(v, kind):
    v.start((1.00, 1.52))
    v.add((0.88, 1.70), (0.62, 1.78), (0.38, 1.70), (0.25, 1.50), (0.30, 1.25), (0.50, 1.05), (0.75, 0.85), (0.92, 0.60), (0.92, 0.32),
          (0.76, 0.08), (0.50, -0.01), (0.25, 0.04), (0.10, 0.20))
    v.end(1.16)


@cap('T')
def _T(v, kind):
    v.first.append(stroke((0.10, 1.52), (0.26, 1.68), (0.46, 1.755), (0.68, 1.73), (0.88, 1.68), (1.06, 1.70), (1.20, 1.79)))
    v.start((0.68, 1.7234))
    v.add((0.675, 1.30), (0.665, 0.85), (0.655, 0.42), (0.665, 0.16), (0.71, 0.04), (0.82, -0.01))
    v.exit(LOW, 1.32)


@cap('U')
def _U(v, kind):
    v.start((0.08, 1.50))
    v.add((0.18, 1.70), (0.34, 1.72), (0.38, 1.50), (0.36, 1.10), (0.34, 0.65), (0.37, 0.25), (0.50, 0.04), (0.70, -0.01), (0.90, 0.08),
          (1.03, 0.30), (1.09, 0.70), (1.11, 1.15), (1.12, 1.72)).tip().back(2)
    v.add((1.095, 0.40), (1.11, 0.18), (1.16, 0.05), (1.27, 0.0))
    v.exit(LOW, 1.73)


@cap('V', exit=MID)
def _V(v, kind):
    v.start((0.06, 1.50))
    v.add((0.16, 1.70), (0.30, 1.70), (0.38, 1.45), (0.46, 1.05), (0.55, 0.62), (0.64, 0.25), (0.74, 0.03), (0.86, 0.02), (0.98, 0.12),
          (1.06, 0.36), (1.12, 0.70), (1.17, 1.10), (1.22, 1.45), (1.28, 1.76)).tip().back(4)
    v.add((1.12, 0.26))
    v.exit(MID, 1.48)


@cap('W', exit=MID)
def _W(v, kind):
    v.start((0.06, 1.50))
    v.add((0.16, 1.70), (0.28, 1.66), (0.32, 1.30), (0.35, 0.80), (0.40, 0.30), (0.48, 0.03), (0.60, 0.05), (0.70, 0.40), (0.78, 0.85),
          (0.84, 1.20)).tip()
    v.add((0.90, 0.80), (0.98, 0.35), (1.08, 0.04), (1.19, 0.03), (1.28, 0.12), (1.33, 0.30), (1.37, 0.55), (1.43, 0.90), (1.50, 1.25),
          (1.57, 1.60), (1.61, 1.76)).tip().back(5)
    v.add((1.40, 0.22))
    v.exit(MID, 1.74)


@cap('X')
def _X(v, kind):
    v.start((0.10, 1.70))
    v.add(*bow((0.10, 1.70), (0.80, 0.16), -0.12)[1:], (0.88, 0.04), (1.0, -0.005))
    v.exit(LOW, 1.50)
    v.marks.append(stroke(*bow((1.04, 1.70), (0.12, 0.04), -0.10)))


@cap('Y')
def _Y(v, kind):
    v.start((0.08, 1.50))
    v.add((0.18, 1.70), (0.32, 1.70), (0.37, 1.45), (0.36, 1.05), (0.38, 0.68), (0.48, 0.42), (0.66, 0.35), (0.84, 0.44), (0.95, 0.72),
          (1.01, 1.10), (1.04, 1.45), (1.06, 1.74)).tip().back(3)
    v.add((1.00, 0.70), (0.985, 0.35))
    descender(v, 0.975)


@cap('Z')
def _Z(v, kind):
    v.start((0.12, 1.58))
    v.add((0.28, 1.70), (0.50, 1.72), (0.72, 1.70), (0.95, 1.72)).tip()
    v.add(*bow((0.95, 1.72), (0.12, 0.08), 0.08)[1:]).tip()
    v.add((0.32, 0.06), (0.55, 0.02), (0.78, 0.0))
    v.exit(LOW, 1.30)


def digit(chars, exit=NONE):
    return glyph(chars, exit=exit, joins=False, kinds=(START,))


@digit('0')
def _0(v, kind):
    closed(v, ell(0.46, 0.80, 0.36, 0.80, 100, 460)[:-1], 4)
    v.end(1.0)


@digit('1')
def _1(v, kind):
    v.start((0.12, 1.30))
    v.add((0.26, 1.42), (0.40, 1.55), (0.48, 1.64)).tip()
    v.add((0.46, 1.25), (0.44, 0.85), (0.42, 0.45), (0.40, 0.02))
    v.end(0.80)


@digit('2')
def _2(v, kind):
    v.start((0.12, 1.22))
    v.add((0.20, 1.45), (0.40, 1.62), (0.62, 1.60), (0.76, 1.42), (0.74, 1.15), (0.58, 0.85), (0.40, 0.58), (0.24, 0.32), (0.12, 0.08)).tip()
    v.add((0.30, 0.06), (0.55, 0.03), (0.84, 0.06))
    v.end(1.05)


@digit('3')
def _3(v, kind):
    v.start((0.14, 1.38))
    v.add((0.28, 1.56), (0.50, 1.63), (0.70, 1.52), (0.74, 1.30), (0.62, 1.08), (0.42, 0.93)).tip()
    v.add((0.62, 0.88), (0.78, 0.68), (0.80, 0.40), (0.66, 0.12), (0.42, 0.0), (0.20, 0.05), (0.08, 0.18))
    v.end(1.0)


@digit('4')
def _4(v, kind):
    v.start((0.62, 1.62))
    v.add((0.48, 1.34), (0.34, 1.04), (0.20, 0.74), (0.10, 0.52)).tip()
    v.add((0.32, 0.52), (0.56, 0.53), (0.84, 0.56))
    v.end(1.02)
    v.extra.append(stroke((0.68, 1.10), (0.66, 0.80), (0.64, 0.50), (0.62, 0.22), (0.61, 0.0)))


@digit('5')
def _5(v, kind):
    v.start((0.84, 1.62))
    v.add((0.62, 1.62), (0.42, 1.60), (0.34, 1.58)).tip()
    v.add((0.32, 1.32), (0.30, 1.06)).tip()
    v.add((0.48, 1.08), (0.68, 1.02), (0.82, 0.80), (0.82, 0.45), (0.66, 0.15), (0.42, 0.0), (0.20, 0.05), (0.08, 0.18))
    v.end(1.02)


@digit('6')
def _6(v, kind):
    v.start((0.72, 1.58))
    v.add((0.48, 1.48), (0.28, 1.22), (0.14, 0.85), (0.10, 0.45), (0.20, 0.12), (0.42, 0.0), (0.64, 0.10), (0.74, 0.35), (0.68, 0.62),
          (0.48, 0.74), (0.28, 0.66), (0.132, 0.478))
    v.end(0.98)


@digit('7')
def _7(v, kind):
    v.start((0.08, 1.55))
    v.add((0.28, 1.60), (0.52, 1.58), (0.80, 1.62)).tip()
    v.add(*bow((0.80, 1.62), (0.38, 0.02), 0.07)[1:])
    v.end(0.98)


@digit('8')
def _8(v, kind):
    v.start((0.66, 1.42))
    v.add((0.55, 1.58), (0.38, 1.62), (0.22, 1.50), (0.20, 1.30), (0.34, 1.10), (0.52, 0.92), (0.70, 0.70), (0.76, 0.42), (0.66, 0.14),
          (0.44, 0.0), (0.22, 0.08), (0.12, 0.32), (0.20, 0.60), (0.38, 0.82), (0.54, 1.0), (0.66, 1.20), (0.66, 1.42))
    v.end(0.98)


@digit('9')
def _9(v, kind):
    k = (0.74, 1.30)
    v.start(k)
    v.add((0.66, 1.52), (0.44, 1.62), (0.24, 1.52), (0.14, 1.30), (0.20, 1.06), (0.40, 0.96), (0.60, 1.02), (0.72, 1.16), k).tip()
    v.add((0.72, 0.95), (0.66, 0.55), (0.58, 0.20), (0.50, 0.0))
    v.end(0.98)


@digit('.')
def _dot(v, kind):
    v.add((0.12, 0.04), (0.124, 0.044))
    v.adv = 0.38


@digit(',')
def _comma(v, kind):
    v.start((0.16, 0.08))
    v.add((0.14, -0.06), (0.06, -0.22))
    v.end(0.38)


@digit('!')
def _excl(v, kind):
    v.start((0.30, 1.62))
    v.add((0.28, 1.25), (0.26, 0.85), (0.24, 0.45))
    v.end(0.52)
    v.extra.append(dot(0.22, 0.04))


@digit('?')
def _quest(v, kind):
    v.start((0.12, 1.30))
    v.add((0.24, 1.52), (0.45, 1.62), (0.64, 1.52), (0.68, 1.30), (0.56, 1.10), (0.40, 0.95), (0.34, 0.75), (0.34, 0.50))
    v.end(0.88)
    v.extra.append(dot(0.34, 0.04))


@digit("'")
def _apos(v, kind):
    v.start((0.22, 1.70))
    v.add((0.20, 1.52), (0.16, 1.36))
    v.end(0.36)


@digit('-')
def _dash(v, kind):
    v.start((0.10, 0.56))
    v.add((0.30, 0.60), (0.52, 0.58))
    v.end(0.64)


@digit(':')
def _colon(v, kind):
    v.add((0.12, 0.04), (0.124, 0.044))
    v.adv = 0.40
    v.extra.append(dot(0.14, 0.70))


@digit('&')
def _amp(v, kind):
    v.start((0.96, 0.05))
    v.add((0.75, 0.25), (0.52, 0.55), (0.32, 0.85), (0.22, 1.15), (0.26, 1.42), (0.44, 1.55), (0.60, 1.45), (0.60, 1.22), (0.44, 1.0),
          (0.24, 0.75), (0.10, 0.45), (0.14, 0.15), (0.36, 0.0), (0.60, 0.06), (0.80, 0.28), (0.92, 0.55))
    v.end(1.12)


# ============================================================== words
def layout(text):
    """the strokes of `text` in writing order: control polygons (points, copy sources), x-height units; handwriting.cpp's
    WriteScript does the same"""
    strokes = []
    x, y = 0.0, 0.0
    for line in text.split('\n'):
        for word in line.split(' '):
            cur, src, exit_, marks, after, last = None, None, NONE, [], [], None

            def end():
                nonlocal cur, src
                if cur is not None:
                    if exit_:
                        # the word's last letter: its exit's TAIL becomes its end (the last point three times)
                        hx = cur[-1][0]
                        n = len(TAIL[exit_])
                        del cur[-n:], src[-n:]
                        for dx, py in last.fin or END[exit_]:
                            cur.append((hx + dx, py + y))
                            src.append(-1)
                        cur += [cur[-1]] * 2
                        src += [-1, -1]
                    strokes.append((cur, src))
                    cur = src = None
                strokes.extend(after)
                after.clear()

            def place(vv, at):
                return [(px + at, py + y) for px, py in vv.P]
            for ch in word:
                g = GLYPHS.get(ch)
                if g is None:
                    end()
                    exit_ = NONE
                    x += UNKNOWN
                    continue
                v = g.variant(exit_ if cur is not None and exit_ and g.joins else START)
                for f in v.first:
                    end()
                    strokes.append((place(f, x), list(f.src)))
                if cur is not None and exit_ and g.joins:
                    if exit_ == LOW and v.pre:
                        del cur[-len(TAIL[LOW]):], src[-len(TAIL[LOW]):]
                        cur += [(px + x, py + y) for px, py in v.pre]
                        src += [-1] * len(v.pre)
                    base = len(cur)
                    cur += place(v, x)
                    src += [s + base if s >= 0 else -1 for s in v.src]
                else:
                    end()
                    cur, src = place(v, x), list(v.src)
                after.extend((place(e, x), list(e.src)) for e in v.extra)
                marks.extend((place(m, x), list(m.src)) for m in v.marks)
                exit_, last = g.exit, v
                x += v.adv
                if not exit_:
                    end()
            end()
            strokes.extend(marks)
            x += SPACE
        x = 0.0
        y -= 3.2
    return strokes


def smooth(P, src, passes=SMOOTH):
    """a little smoothing of the control points: fourth differences (they leave arcs alone, take out wobbles); the tips
    (a point written more than once), their neighbours and the ends stay; the copies follow their originals"""
    n = len(P)
    tips = {i for i in range(n) if (i > 0 and P[i] == P[i - 1]) or (i + 1 < n and P[i] == P[i + 1])}
    P = [list(p) for p in P]
    for _ in range(passes):
        Q = [p[:] for p in P]
        for i in range(2, n - 2):
            if i in tips or i - 1 in tips or i + 1 in tips:
                continue
            for a in range(2):
                d4 = P[i - 2][a] - 4 * P[i - 1][a] + 6 * P[i][a] - 4 * P[i + 1][a] + P[i + 2][a]
                Q[i][a] = P[i][a] - 0.06 * d4
        for i, s in enumerate(src):
            if s >= 0:
                Q[i] = Q[s][:]
        P = Q
    return [tuple(p) for p in P]


STEP = 0.02     # x-heights between the curve's samples


def bspline(P, step=STEP):
    out = []
    for i in range(len(P) - 3):
        p0, p1, p2, p3 = P[i], P[i + 1], P[i + 2], P[i + 3]
        n = max(int(math.ceil(math.dist(p1, p2) / step)), 1)
        for k in range(n):
            t = k / n
            b0, b1, b2, b3 = (1 - t) ** 3 / 6, (3 * t ** 3 - 6 * t ** 2 + 4) / 6, (-3 * t ** 3 + 3 * t ** 2 + 3 * t + 1) / 6, t ** 3 / 6
            out.append((b0 * p0[0] + b1 * p1[0] + b2 * p2[0] + b3 * p3[0], b0 * p0[1] + b1 * p1[1] + b2 * p2[1] + b3 * p3[1]))
    a, b, c = P[-3], P[-2], P[-1]
    out.append(((a[0] + 4 * b[0] + c[0]) / 6, (a[1] + 4 * b[1] + c[1]) / 6))
    return out


def curves(text, step=STEP):
    res = []
    for P, src in layout(text):
        if len(P) <= 2:
            res.append(P)
            continue
        res.append([(px + SLANT * py, py) for px, py in bspline(smooth(P, src), step)])
    return res


def render(lines, pen=0.18, scale=90.0, pad=40, polygons=False, bg=(128, 128, 128), ink=(248, 245, 240), guides=True):
    from PIL import Image, ImageDraw
    strokes, polys = [], []
    for k, text in enumerate(lines):
        for c in curves(text):
            strokes.append([(x, y - 3.2 * k) for x, y in c])
        if polygons:
            for P, src in layout(text):
                polys.append([(x, y - 3.2 * k) for x, y in P])
    xs = [p[0] for s in strokes for p in s]
    ys = [p[1] for s in strokes for p in s]
    minx, maxx, miny, maxy = min(xs) - pen, max(xs) + pen, min(ys) - pen, max(ys) + pen
    w, h = int((maxx - minx) * scale + 2 * pad), int((maxy - miny) * scale + 2 * pad)
    ss = 3
    img = Image.new('RGB', (w * ss, h * ss), bg)
    d = ImageDraw.Draw(img)

    def to(p):
        return ((pad + (p[0] - minx) * scale) * ss, (pad + (maxy - p[1]) * scale) * ss)
    if guides:
        for k in range(len(lines)):
            for yy in (0.0, 1.0):
                py = to((0, yy - 3.2 * k))[1]
                d.line([(0, py), (w * ss, py)], fill=tuple(max(c - 14, 0) for c in bg), width=ss)
    r = pen * scale * ss / 2
    for s in strokes:
        q = [to(p) for p in s]
        if len(s) <= 2:
            x0, y0 = q[0]
            d.ellipse([x0 - r * 1.1, y0 - r * 1.1, x0 + r * 1.1, y0 + r * 1.1], fill=ink)
            continue
        d.line(q, fill=ink, width=max(int(round(2 * r)), 1))
        for x0, y0 in q:
            d.ellipse([x0 - r, y0 - r, x0 + r, y0 + r], fill=ink)
    if polygons:
        for P in polys:
            q = [to(p) for p in P]
            d.line(q, fill=(220, 60, 60), width=ss)
            for x0, y0 in q:
                d.ellipse([x0 - 2 * ss, y0 - 2 * ss, x0 + 2 * ss, y0 + 2 * ss], outline=(220, 60, 60), width=ss)
    return img.resize((w, h), Image.LANCZOS)


def preview(out, lines, polygons=False):
    import os
    os.makedirs(out, exist_ok=True)
    img = render(lines, polygons=polygons)
    img.save(os.path.join(out, 'preview.png'))
    json.dump([curves(t) for t in lines], open(os.path.join(out, 'strokes.json'), 'w'))
    print('preview', img.size)


def table(path):
    """the glyphs as the C++ table cipher/handwriting_glyphs.inc: control points with the point each copies, runs of
    them, the variants, the glyphs"""
    points, copies, runs, variants, glyphs, seen = [], [], [], [], [], {}
    none = 0xFFFF

    def run(P, src):
        if not P:
            return none
        runs.append((len(points), len(P)))
        points.extend(P)
        copies.extend(src)
        return len(runs) - 1

    def runs_of(vs):
        first = len(runs)
        for w in vs:
            run(w.P, w.src)
        return first, len(vs)

    def variant(v):
        if id(v) in seen:
            return seen[id(v)]
        body = run(v.P, v.src)
        pre = run(v.pre, [-1] * len(v.pre))
        fin = run(v.fin, [-1] * len(v.fin))
        f, e, m = runs_of(v.first), runs_of(v.extra), runs_of(v.marks)
        variants.append('    {%d, %d, %d, %.4ff, %d, %d, %d, %d, %d, %d},' % ((body, pre, fin, v.adv) + f + e + m))
        seen[id(v)] = len(variants) - 1
        return seen[id(v)]
    for ch in sorted(GLYPHS, key=ord):
        g = GLYPHS[ch]
        ids = [variant(g.variant(k)) for k in (START, LOW, HIGH, MID)]
        glyphs.append('    {0x%04X, %d, %s, {%d, %d, %d, %d}},  // %s' % ((ord(ch), g.exit, 'true' if g.joins else 'false') + tuple(ids) + (ch,)))
    nl = chr(10)
    out = ['// generated by tools/script_bspline.py: the cursive as uniform cubic B-splines - each glyph by the way the letter before',
           '// it left (its lead into it and the letter), control points in x-height units, y up, baseline 0; handwriting.cpp joins',
           '// a word\'s into one control polygon, smooths it a little and samples the curve',
           'constexpr float kHandSlant = %.2ff, kHandSpace = %.2ff, kHandUnknown = %.2ff;' % (SLANT, SPACE, UNKNOWN),
           '// by exit (none, low, high, mid): how many of its last control points the next letter\'s pre, or the word\'s end, takes',
           '// the place of; what ends a word in their place unless the last letter has its own (x from its advance, y from the',
           '// baseline; the stroke ends on the last point)',
           'constexpr int kHandTail[4] = {0, %d, %d, %d};' % (len(TAIL[LOW]), len(TAIL[HIGH]), len(TAIL[MID])),
           'constexpr int kHandEndCount[4] = {0, %d, %d, %d};' % (len(END[LOW]), len(END[HIGH]), len(END[MID])),
           'const Vec2 kHandEnd[4][4] = {{}, %s};' % ', '.join('{%s}' % ', '.join('{%.3ff, %.3ff}' % p for p in END[k]) for k in (LOW, HIGH, MID)),
           'const Vec2 kHandPoints[] = {']
    row = ['{%.4ff, %.4ff}' % p for p in points]
    for i in range(0, len(row), 6):
        out.append('    ' + ', '.join(row[i:i + 6]) + ',')
    out.append('};')
    out.append('// for each point the one of its run it copies (-1 none): a pass back over a stroke, or a second pass the same way')
    out.append('const std::int16_t kHandCopy[] = {')
    cc = ['%d' % c for c in copies]
    for i in range(0, len(cc), 24):
        out.append('    ' + ', '.join(cc[i:i + 24]) + ',')
    out.append('};')
    out.append('// a run of points: the first, how many')
    out.append('const HandRun kHandRuns[] = {')
    rr = ['{%d, %d}' % r for r in runs]
    for i in range(0, len(rr), 10):
        out.append('    ' + ', '.join(rr[i:i + 10]) + ',')
    out.append('};')
    out.append('// body, pre, fin (runs; 0xFFFF none), advance, its strokes before it, right after it, after its word (first run, count)')
    out.append('const HandVariant kHandVariants[] = {')
    out.extend(variants)
    out.append('};')
    out.append('// code, exit (0 none, 1 low, 2 high, 3 mid), joined from the letter before, its variants: starting a word, after a')
    out.append('// low, a high, a mid exit')
    out.append('const HandGlyph kHandGlyphs[] = {')
    out.extend(glyphs)
    out.append('};')
    open(path, 'w', encoding='utf-8', newline=nl).write(nl.join(out) + nl)
    print('wrote', path, len(glyphs), 'glyphs', len(variants), 'variants', len(points), 'control points')


if __name__ == '__main__':
    if sys.argv[1] == 'preview':
        preview(sys.argv[2], [a for a in sys.argv[3:] if a != '--poly'], polygons='--poly' in sys.argv)
    elif sys.argv[1] == 'table':
        table(sys.argv[2])
