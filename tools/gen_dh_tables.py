#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
"""Generate the DH_MultiFilter2 lookup tables (src/dsp/DhFilterTables.inc) from their formulas,
evaluated like the original's x87 code (tools/x87emu.py). Optionally check them against a memory
dump of the original (tools/dump_module_memory.cpp).

    python3 tools/gen_dh_tables.py [dumpdir] > src/dsp/DhFilterTables.inc

All tables match the original exactly except 5 entries of the Hz table, which the original computes
with the CRT's SSE2 pow (not correctly rounded); see docs/dsp-notes.md.
"""
import math
import os
import struct
import sys
from decimal import Decimal, getcontext
from fractions import Fraction

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import x87emu as X  # noqa: E402

getcontext().prec = 70
LN2_64 = Fraction(0xB17217F7D1CF79AC, 1 << 64)          # fldln2
PI = Decimal('3.141592653589793238462643383279502884197169399375105820974944592307816406286')


def dec(fr):
    fr = Fraction(fr)
    return Decimal(fr.numerator) / Decimal(fr.denominator)


def round_bits(fr, bits):
    fr = Fraction(fr)
    if fr == 0:
        return fr
    s = -1 if fr < 0 else 1
    a = abs(fr)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a:
        e -= 1
    sc = Fraction(2) ** (bits - 1 - e)
    m = a * sc
    q, r = divmod(m.numerator, m.denominator)
    if 2 * r > m.denominator or (2 * r == m.denominator and q & 1):
        q += 1
    return s * Fraction(q) / sc


def series_sin(x):
    s, term, n = Decimal(0), x, 1
    while abs(term) > Decimal(10) ** -60:
        s += term
        term = -term * x * x / ((n + 1) * (n + 2))
        n += 2
    return s


def series_cos(x):
    s, term, n = Decimal(0), Decimal(1), 0
    while abs(term) > Decimal(10) ** -60:
        s += term
        term = -term * x * x / ((n + 1) * (n + 2))
        n += 2
    return s


PI66 = round_bits(Fraction(PI), 66)  # fsin's internal pi


def x87_sin(x):
    """fsin of a double: argument reduction with a 66-bit pi, 64-bit result"""
    x = Fraction(x)
    if x <= PI66 / 4:
        v = series_sin(dec(x))
    else:
        q = round(x / (PI66 / 2))
        r = dec(x - q * (PI66 / 2))
        v = [series_sin(r), series_cos(r), -series_sin(r), -series_cos(r)][q % 4]
    return X.rd64(Fraction(v))


def x87_log2(x):
    """fyl2x with y = 1: 64-bit result"""
    return X.rd64(Fraction(Decimal(Fraction(x).numerator).ln() / Decimal(2).ln()
                           - Decimal(Fraction(x).denominator).ln() / Decimal(2).ln()))


def correctly_rounded_pow2(y):
    return X.rd(Fraction(Decimal(2) ** dec(y)))


# Constants of the original SEM used by the formulas (address in dh_multifilter2.sem: value).
CONSTANTS = {
    0x1000f300: 3.2535051481941695e-86,
    0x1000f320: 1000.0,
    0x1000f330: 0.001953125,
    0x1000f3a0: 0.0,
    0x1000f3b8: 0.5,
    0x1000f3c0: 0.001,
    0x1000f438: 4.0,
    0x1000f440: 0.2759,
    0x1000f448: 0.2461,
    0x1000f450: 0.0144,
    0x1000f458: -0.0041,
    0x1000f460: 0.1748046875,
    0x1000f468: 0.6,
    0x1000f470: 0.05,
    0x1000f478: 4.5,
    0x1000f480: 4.5e-14,
    0x1000f488: 4.5e-12,
    0x1000f490: 7e-10,
    0x1000f498: 4.6e-08,
    0x1000f4a0: 4.2e-06,
    0x1000f4a8: 0.0003,
    0x1000f4b0: 0.022,
    0x1000f4b8: 1.12,
    0x1000f4c0: 2.5e-11,
    0x1000f4c8: 1e-09,
    0x1000f4d0: 3.2e-08,
    0x1000f4d8: 5e-05,
    0x1000f4e0: 0.0017,
    0x1000f4e8: 0.043,
    0x1000f4f0: 0.872,
    0x1000f4f8: 5e-10,
    0x1000f500: 1e-08,
    0x1000f508: 5.2e-07,
    0x1000f510: 0.00025,
    0x1000f518: 0.07,
    0x1000f520: 0.89,
    0x1000f528: 3e-08,
    0x1000f530: 2e-07,
    0x1000f538: 5e-06,
    0x1000f540: 7e-05,
    0x1000f548: 0.013,
    0x1000f550: 0.13,
    0x1000f558: 0.853,
    0x1000f560: 1.5e-06,
    0x1000f568: 1.3e-05,
    0x1000f570: 8e-05,
    0x1000f578: 0.0007,
    0x1000f580: 0.005,
    0x1000f588: 0.032,
    0x1000f590: 0.2,
    0x1000f598: 7.5e-05,
    0x1000f5a0: 0.00027,
    0x1000f5a8: 0.0013,
    0x1000f5b0: 0.0045,
    0x1000f5b8: 0.024,
    0x1000f5c0: 0.08,
    0x1000f5c8: 0.35,
    0x1000f5d0: 0.0055,
    0x1000f5d8: 0.015,
    0x1000f5e0: 0.04,
    0x1000f5e8: 0.1,
    0x1000f5f0: 0.55,
    0x1000f5f8: 0.055,
    0x1000f600: 0.09,
    0x1000f608: 0.17,
    0x1000f610: 0.25,
    0x1000f618: 0.37,
    0x1000f620: 0.52,
    0x1000f628: 0.7,
    0x1000f630: 0.85,
    0x1000f638: 0.725,
    0x1000f640: 0.735,
    0x1000f648: 0.785,
    0x1000f650: 0.795,
    0x1000f658: 0.845,
    0x1000f660: 0.815,
    0x1000f668: 0.81,
    0x1000f670: 0.8,
    0x1000f680: 0.99,
    0x1000f688: 440.0,
    0x1000f690: 5.0,
    0x1000f698: 0.002,
    0x1000f6a0: 2.0,
}

# Addresses of the gain-compensation knots, [knot][stage] (9 knots at L = 0, 0.5, ... 4; the
# compiler merged equal constants, so they are not consecutive).
KNOT_ADDRS = [
    0x1000f670, 0x1000f668, 0x1000f660, 0x1000f658, 0x1000f650, 0x1000f648, 0x1000f640, 0x1000f638,
    0x1000f630, 0x1000f628, 0x1000f620, 0x1000f618, 0x1000f610, 0x1000f608, 0x1000f600, 0x1000f5f8,
    0x1000f630, 0x1000f5f0, 0x1000f610, 0x1000f5e8, 0x1000f5e0, 0x1000f5d8, 0x1000f5d0, 0x1000f698,
    0x1000f630, 0x1000f5c8, 0x1000f5c0, 0x1000f5b8, 0x1000f5b0, 0x1000f5a8, 0x1000f5a0, 0x1000f598,
    0x1000f630, 0x1000f590, 0x1000f588, 0x1000f580, 0x1000f578, 0x1000f570, 0x1000f568, 0x1000f560,
    0x1000f558, 0x1000f550, 0x1000f548, 0x1000f3c0, 0x1000f540, 0x1000f538, 0x1000f530, 0x1000f528,
    0x1000f520, 0x1000f518, 0x1000f580, 0x1000f510, 0x1000f568, 0x1000f508, 0x1000f500, 0x1000f4f8,
    0x1000f4f0, 0x1000f4e8, 0x1000f4e0, 0x1000f4d8, 0x1000f560, 0x1000f4d0, 0x1000f4c8, 0x1000f4c0,
    0x1000f4b8, 0x1000f4b0, 0x1000f4a8, 0x1000f4a0, 0x1000f498, 0x1000f490, 0x1000f488, 0x1000f480,
]


def add(a, b):
    return X.rd(Fraction(a) + Fraction(b))


def sub(a, b):
    return X.rd(Fraction(a) - Fraction(b))


def build(fs=44100.0):
    c = CONSTANTS.__getitem__
    # frequency: 440 * 2^((i - 1000) * 0.002 - 5), capped at 0.99 * fs / 2
    hz = []
    for i in range(7001):
        y = sub(X.mul(sub(i, c(0x1000f320)), c(0x1000f698)), c(0x1000f690))
        v = X.mul(correctly_rounded_pow2(y), c(0x1000f688))  # CRT SSE2 pow: 5 entries differ, see notes
        hz.append(v)  # uncapped; the filter applies min(v, 0.99 * fs / 2) at its sample rate
    # sine: sin(k * 0.001)
    sine = [X.rd(x87_sin(X.mul(k, c(0x1000f3c0)))) for k in range(4714)]
    # Q: exp(exp(i/512 * 4.5) * 0.05) - 0.6
    q = []
    for i in range(513):
        e1 = X.x87_exp2_ext(X.mul(X.L2E, X.mul(i * c(0x1000f330), c(0x1000f478))))
        q.append(X.x87_exp2_ext(X.mul(X.L2E, X.mul(e1, c(0x1000f470)))) - c(0x1000f468))
    # gain compensation, 8 stage counts x 513
    knots = [c(a) for a in KNOT_ADDRS]                            # [knot][stage]
    low = [c(0x1000f670 - 8 * s) for s in range(8)]
    high = [c(0x1000f4b8 - 8 * s) for s in range(8)]
    first = c(0x1000f478)
    gain = [[first] * 513 for _ in range(8)]
    for i in range(1, 513):
        # fldln2; fyl2x gives ln(x) at 64-bit precision in one step; the first multiply uses that
        # register value, later ones the double copy L.
        x = Fraction(X.mul(i, c(0x1000f460)))
        lnx = X.rd64(Fraction(Decimal(x.numerator).ln() - Decimal(x.denominator).ln()))
        L = X.rd(lnx)
        # ((L * -0.0041 - 0.0144) * L - 0.2461) * L + 0.2759, each step rounded to double
        poly = sub(X.rd(lnx * Fraction(c(0x1000f458))), c(0x1000f450))
        poly = sub(X.mul(poly, L), c(0x1000f448))
        poly = add(X.mul(poly, L), c(0x1000f440))
        g = X.x87_exp2_ext(X.mul(X.L2E, poly))
        for s in range(8):
            if L < c(0x1000f3a0):
                f = low[s]
            elif L <= c(0x1000f438):
                t = X.mul(L, c(0x1000f6a0))
                j = int(t)                                        # fistp in chop mode
                fr = sub(t, j)
                a, b = knots[j * 8 + s], knots[j * 8 + 8 + s]
                f = add(X.mul(sub(b, a), fr), a)
            else:
                f = high[s]
            gain[s][i] = X.mul(f, g)
    return hz, sine, q, gain


def check(name, mine, dump):
    bad = [i for i, (a, b) in enumerate(zip(mine, dump)) if a != b]
    print(f'{name}: {len(bad)} of {len(mine)} differ {bad[:8]}', file=sys.stderr)


def emit(name, values):
    out = [f'static const double {name}[{len(values)}] = {{']
    for k in range(0, len(values), 4):
        out.append('    ' + ' '.join(f'{float(v)!r},' for v in values[k:k + 4]))
    out.append('};')
    return '\n'.join(out)


def main():
    hz, sine, q, gain = build()
    if len(sys.argv) > 1:
        d = sys.argv[1]
        rd = lambda f, n: struct.unpack(f'<{n}d', open(os.path.join(d, f), 'rb').read()[:8 * n])
        check('hz', [min(v, 21829.5) for v in hz], rd('124856.bin', 7001))
        check('sine', sine, rd('87120.bin', 4715)[1:])
        check('Q', q, rd('213768.bin', 513))
        g = rd('180872.bin', 4112)
        for s in range(8):
            check(f'gain[{s}]', gain[s], g[s * 514:s * 514 + 513])
    print('// SPDX-License-Identifier: LGPL-2.1-or-later')
    print('// Copyright (C) 2026 Luca Casati')
    print('// Generated by tools/gen_dh_tables.py: DH_MultiFilter2 lookup tables. kDhHz is uncapped.')
    print(emit('kDhHz', hz))
    print(emit('kDhSine', sine))
    print(emit('kDhQ', q))
    for s in range(8):
        print(emit(f'kDhGain{s + 1}', gain[s]))


if __name__ == '__main__':
    main()
