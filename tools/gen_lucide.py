"""gen_lucide.py <lucide icons dir> <license file> <out dir>: Lucide's SVG icons as Cipher's icon data.

Every icon becomes a list of drawing commands on its 24 x 24 grid - move, line, cubic, close - with a flag on each
subpath that is filled rather than stroked (Lucide's dots: fill="currentColor"). SVG arcs, quadratics and the shorthand
curves become cubics, circles, ellipses and rounded rects four cubic quarters, so the library flattens one kind of
curve. Coordinates are 1/256 of a grid unit in int16.

Writes <out>/lucide_icons.hpp (enum class Icon, one name per icon in CamelCase) and <out>/lucide_data.cpp (the commands,
the coordinates and a table by name). Run with python -I: it only reads the files it is given.
"""
import math
import os
import re
import sys

SCALE = 256

# ------------------------------------------------------------------ path data

NUM = re.compile(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')
TOKEN = re.compile(r'[MmLlHhVvCcSsQqTtAaZz]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?')


class Path:
    """Commands in absolute coordinates: ('M', x, y), ('L', x, y), ('C', x1, y1, x2, y2, x, y), ('Z',)."""

    def __init__(self):
        self.cmds = []
        self.filled = []   # per command: the subpath it belongs to is filled

    def move(self, x, y, filled=False):
        self.cmds.append(('M', x, y))
        self.filled.append(filled)

    def line(self, x, y):
        self.cmds.append(('L', x, y))
        self.filled.append(False)

    def cubic(self, x1, y1, x2, y2, x, y):
        self.cmds.append(('C', x1, y1, x2, y2, x, y))
        self.filled.append(False)

    def close(self):
        self.cmds.append(('Z',))
        self.filled.append(False)


def tokenize_path(d):
    out = []
    i = 0
    while i < len(d):
        c = d[i]
        if c in 'MmLlHhVvCcSsQqTtAaZz':
            out.append(c)
            i += 1
        elif c in ' ,\t\r\n':
            i += 1
        else:
            m = NUM.match(d, i)
            if not m:
                raise ValueError('bad path data at %d: %r' % (i, d[i:i + 20]))
            out.append(m.group(0))
            i = m.end()
    return out


def arc_to_cubics(x0, y0, rx, ry, phi_deg, large, sweep, x, y):
    """SVG elliptical arc (endpoint form) as cubic segments of at most 90 degrees (SVG 1.1 appendix F.6)."""
    if x0 == x and y0 == y:
        return []
    rx, ry = abs(rx), abs(ry)
    if rx == 0 or ry == 0:
        return [(x0 + (x - x0) / 3, y0 + (y - y0) / 3, x0 + 2 * (x - x0) / 3, y0 + 2 * (y - y0) / 3, x, y)]
    phi = math.radians(phi_deg % 360)
    cp, sp = math.cos(phi), math.sin(phi)
    dx2, dy2 = (x0 - x) / 2.0, (y0 - y) / 2.0
    x1p = cp * dx2 + sp * dy2
    y1p = -sp * dx2 + cp * dy2
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(max(0.0, num / den)) if den != 0 else 0.0
    if large == sweep:
        coef = -coef
    cxp = coef * (rx * y1p / ry)
    cyp = coef * (-ry * x1p / rx)
    cx = cp * cxp - sp * cyp + (x0 + x) / 2.0
    cy = sp * cxp + cp * cyp + (y0 + y) / 2.0

    def angle(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a

    t1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(1, int(math.ceil(abs(dt) / (math.pi / 2) - 1e-9)))
    seg = dt / n
    k = 4.0 / 3.0 * math.tan(seg / 4)
    out = []
    a = t1
    for _ in range(n):
        b = a + seg
        ca, sa, cb, sb = math.cos(a), math.sin(a), math.cos(b), math.sin(b)
        p1 = (ca - k * sa, sa + k * ca)
        p2 = (cb + k * sb, sb - k * cb)
        p3 = (cb, sb)

        def tr(px, py):
            return (cx + rx * px * cp - ry * py * sp, cy + rx * px * sp + ry * py * cp)

        q1, q2, q3 = tr(*p1), tr(*p2), tr(*p3)
        out.append((q1[0], q1[1], q2[0], q2[1], q3[0], q3[1]))
        a = b
    # the last point exactly the arc's end
    if out:
        last = out[-1]
        out[-1] = (last[0], last[1], last[2], last[3], x, y)
    return out


def parse_path(d, path):
    t = tokenize_path(d)
    i = 0
    cmd = None
    x = y = 0.0
    sx = sy = 0.0           # subpath start
    lcx = lcy = None        # the last cubic's second control point (S)
    lqx = lqy = None        # the last quadratic's control point (T)

    def num():
        nonlocal i
        v = float(t[i])
        i += 1
        return v

    def flag():
        # a flag is one character, and may be glued to what follows ("00.407" = 0, 0, .407): the rest stays a token
        nonlocal i
        tok = t[i]
        if len(tok) > 1:
            t[i] = tok[1:]
        else:
            i += 1
        return tok[0] == '1'

    while i < len(t):
        if t[i] in 'MmLlHhVvCcSsQqTtAaZz':
            cmd = t[i]
            i += 1
            if cmd in 'Zz':
                path.close()
                x, y = sx, sy
                lcx = lqx = None
                continue
        elif cmd is None:
            raise ValueError('path data without a command')
        rel = cmd.islower()
        c = cmd.upper()
        if c == 'M':
            nx, ny = num(), num()
            if rel:
                nx += x
                ny += y
            path.move(nx, ny)
            x, y = sx, sy = nx, ny
            cmd = 'l' if rel else 'L'   # further pairs are lines
            lcx = lqx = None
        elif c == 'L':
            nx, ny = num(), num()
            if rel:
                nx += x
                ny += y
            path.line(nx, ny)
            x, y = nx, ny
            lcx = lqx = None
        elif c == 'H':
            nx = num()
            if rel:
                nx += x
            path.line(nx, y)
            x = nx
            lcx = lqx = None
        elif c == 'V':
            ny = num()
            if rel:
                ny += y
            path.line(x, ny)
            y = ny
            lcx = lqx = None
        elif c == 'C':
            x1, y1, x2, y2, nx, ny = num(), num(), num(), num(), num(), num()
            if rel:
                x1 += x
                y1 += y
                x2 += x
                y2 += y
                nx += x
                ny += y
            path.cubic(x1, y1, x2, y2, nx, ny)
            lcx, lcy = x2, y2
            lqx = None
            x, y = nx, ny
        elif c == 'S':
            x2, y2, nx, ny = num(), num(), num(), num()
            if rel:
                x2 += x
                y2 += y
                nx += x
                ny += y
            x1, y1 = (2 * x - lcx, 2 * y - lcy) if lcx is not None else (x, y)
            path.cubic(x1, y1, x2, y2, nx, ny)
            lcx, lcy = x2, y2
            lqx = None
            x, y = nx, ny
        elif c == 'Q':
            qx, qy, nx, ny = num(), num(), num(), num()
            if rel:
                qx += x
                qy += y
                nx += x
                ny += y
            path.cubic(x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y), nx + 2.0 / 3.0 * (qx - nx), ny + 2.0 / 3.0 * (qy - ny), nx, ny)
            lqx, lqy = qx, qy
            lcx = None
            x, y = nx, ny
        elif c == 'T':
            nx, ny = num(), num()
            if rel:
                nx += x
                ny += y
            qx, qy = (2 * x - lqx, 2 * y - lqy) if lqx is not None else (x, y)
            path.cubic(x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y), nx + 2.0 / 3.0 * (qx - nx), ny + 2.0 / 3.0 * (qy - ny), nx, ny)
            lqx, lqy = qx, qy
            lcx = None
            x, y = nx, ny
        elif c == 'A':
            rx, ry, rot = num(), num(), num()
            large = flag()
            sweep = flag()
            nx, ny = num(), num()
            if rel:
                nx += x
                ny += y
            for seg in arc_to_cubics(x, y, rx, ry, rot, large, sweep, nx, ny):
                path.cubic(*seg)
            x, y = nx, ny
            lcx = lqx = None
        else:
            raise ValueError('unknown command ' + cmd)


KAPPA = 0.5522847498307936


def ellipse(path, cx, cy, rx, ry, filled):
    path.move(cx + rx, cy, filled)
    path.cubic(cx + rx, cy + KAPPA * ry, cx + KAPPA * rx, cy + ry, cx, cy + ry)
    path.cubic(cx - KAPPA * rx, cy + ry, cx - rx, cy + KAPPA * ry, cx - rx, cy)
    path.cubic(cx - rx, cy - KAPPA * ry, cx - KAPPA * rx, cy - ry, cx, cy - ry)
    path.cubic(cx + KAPPA * rx, cy - ry, cx + rx, cy - KAPPA * ry, cx + rx, cy)
    path.close()


def rect(path, x, y, w, h, rx, ry, filled):
    rx = min(rx, w / 2.0)
    ry = min(ry, h / 2.0)
    if rx <= 0 or ry <= 0:
        path.move(x, y, filled)
        path.line(x + w, y)
        path.line(x + w, y + h)
        path.line(x, y + h)
        path.close()
        return
    path.move(x + rx, y, filled)
    path.line(x + w - rx, y)
    path.cubic(x + w - rx + KAPPA * rx, y, x + w, y + ry - KAPPA * ry, x + w, y + ry)
    path.line(x + w, y + h - ry)
    path.cubic(x + w, y + h - ry + KAPPA * ry, x + w - rx + KAPPA * rx, y + h, x + w - rx, y + h)
    path.line(x + rx, y + h)
    path.cubic(x + rx - KAPPA * rx, y + h, x, y + h - ry + KAPPA * ry, x, y + h - ry)
    path.line(x, y + ry)
    path.cubic(x, y + ry - KAPPA * ry, x + rx - KAPPA * rx, y, x + rx, y)
    path.close()


ATTR = re.compile(r'([\w:-]+)\s*=\s*"([^"]*)"')
ELEM = re.compile(r'<(path|circle|rect|line|ellipse|polyline|polygon)\b([^>]*)/?>', re.S)


def parse_svg(text):
    path = Path()
    for m in ELEM.finditer(text):
        kind = m.group(1)
        a = dict(ATTR.findall(m.group(2)))
        filled = a.get('fill', 'none') not in ('none', '')
        f = lambda k, default=0.0: float(a.get(k, default))
        if kind == 'path':
            start = len(path.cmds)
            parse_path(a['d'], path)
            if filled:
                for k in range(start, len(path.cmds)):
                    if path.cmds[k][0] == 'M':
                        path.filled[k] = True
        elif kind == 'circle':
            ellipse(path, f('cx'), f('cy'), f('r'), f('r'), filled)
        elif kind == 'ellipse':
            ellipse(path, f('cx'), f('cy'), f('rx'), f('ry'), filled)
        elif kind == 'rect':
            rx = a.get('rx')
            ry = a.get('ry')
            rxv = float(rx) if rx is not None else (float(ry) if ry is not None else 0.0)
            ryv = float(ry) if ry is not None else rxv
            rect(path, f('x'), f('y'), f('width'), f('height'), rxv, ryv, filled)
        elif kind == 'line':
            path.move(f('x1'), f('y1'), filled)
            path.line(f('x2'), f('y2'))
        elif kind in ('polyline', 'polygon'):
            pts = [float(v) for v in NUM.findall(a.get('points', ''))]
            if len(pts) >= 4:
                path.move(pts[0], pts[1], filled)
                for k in range(2, len(pts) - 1, 2):
                    path.line(pts[k], pts[k + 1])
                if kind == 'polygon':
                    path.close()
    return path


def camel(name):
    return ''.join(p[:1].upper() + p[1:] for p in name.split('-'))


def fixed(v):
    q = int(round(v * SCALE))
    if not -32768 <= q <= 32767:
        raise ValueError('coordinate out of range: %f' % v)
    return q


def main():
    src, license_file, out = sys.argv[1], sys.argv[2], sys.argv[3]
    names = sorted(f[:-4] for f in os.listdir(src) if f.endswith('.svg'))
    ops, coords, table = [], [], []
    for name in names:
        with open(os.path.join(src, name + '.svg'), encoding='utf-8') as fh:
            path = parse_svg(fh.read())
        op0, c0 = len(ops), len(coords)
        for cmd, filled in zip(path.cmds, path.filled):
            kind = cmd[0]
            if kind == 'M':
                ops.append(4 if filled else 0)
            else:
                ops.append({'L': 1, 'C': 2, 'Z': 3}[kind])
            coords.extend(fixed(v) for v in cmd[1:])
        table.append((name, op0, len(ops) - op0, c0))
    license_text = open(license_file, encoding='utf-8').read().strip()
    header_license = '\n'.join(('// ' + line).rstrip() for line in license_text.splitlines())

    enum_lines = []
    for k, name in enumerate(names):
        enum_lines.append('        %s = %d,' % (camel(name), k + 1))
    with open(os.path.join(out, 'lucide_icons.hpp'), 'w', encoding='utf-8', newline='\n') as h:
        h.write('// Cipher - the Lucide icons (lucide.dev, %d icons) by name. Generated by tools/gen_lucide.py: do not edit.\n' % len(names))
        h.write('//\n// Lucide\'s license:\n//\n' + header_license + '\n')
        h.write('#pragma once\n#include <cstdint>\n\nnamespace cipher\n{\n')
        h.write('    enum class Icon : std::uint16_t\n    {\n        None = 0,\n')
        h.write('\n'.join(enum_lines) + '\n    };\n')
        h.write('    inline constexpr int kIconCount = %d;\n}\n' % len(names))

    with open(os.path.join(out, 'lucide_data.cpp'), 'w', encoding='utf-8', newline='\n') as c:
        c.write('// Cipher - the Lucide icons as drawing commands (icons.hpp). Generated by tools/gen_lucide.py: do not edit.\n')
        c.write('// Lucide\'s license: lucide_icons.hpp.\n')
        c.write('#include "icons.hpp"\n\nnamespace cipher::lucide\n{\n')
        c.write('    // 0 move, 1 line, 2 cubic, 3 close, 4 move to a filled subpath\n')
        c.write('    extern const std::uint8_t kOps[] = {\n')
        for k in range(0, len(ops), 64):
            c.write('        ' + ','.join(str(v) for v in ops[k:k + 64]) + ',\n')
        c.write('    };\n')
        c.write('    // 1/%d of a grid unit\n' % SCALE)
        c.write('    extern const std::int16_t kCoords[] = {\n')
        for k in range(0, len(coords), 32):
            c.write('        ' + ','.join(str(v) for v in coords[k:k + 32]) + ',\n')
        c.write('    };\n')
        c.write('    // by Icon - 1: name, first command, commands, first coordinate\n')
        c.write('    extern const IconData kIcons[] = {\n')
        for name, op0, n, c0 in table:
            c.write('        {"%s", %d, %d, %d},\n' % (name, op0, n, c0))
        c.write('    };\n')
        c.write('    extern const int kIconTotal = %d;\n}\n' % len(names))
    print('%d icons, %d commands, %d coordinates' % (len(names), len(ops), len(coords)))


if __name__ == '__main__':
    main()
