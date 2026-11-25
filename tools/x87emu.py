# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
"""Exact emulation of x87 arithmetic at 53-bit precision control (MSVC default).

Precision control rounds fadd/fsub/fmul/fdiv/fsqrt to 53 bits, but transcendental
instructions (f2xm1, fsin, fyl2x...) keep 64-bit results. exp(x) compiled as
fldl2e/fmul/frndint/f2xm1/fld1/fadd/fscale is therefore x87_exp2_ext(mul(L2E, x)).
"""
from fractions import Fraction
from decimal import Decimal, getcontext
import math
getcontext().prec = 60
L2E = Fraction(0xB8AA3B295C17F0BC, 1 << 63)  # fldl2e (64-bit significand)

def rd(fr):
    """round a Fraction to the nearest double (ties to even)"""
    return float(fr) if isinstance(fr, Fraction) else fr  # float(Fraction) is correctly rounded

def x87_exp(x):
    t = rd(L2E * Fraction(x))                    # fmul by l2e, rounded to 53 bits
    n = round(t)                                  # frndint (nearest even)
    f = t - n                                     # exact
    p = Decimal(2) ** Decimal(f) - 1              # f2xm1 (accurate)
    p = rd(Fraction(p))
    one = rd(Fraction(1) + Fraction(p))
    return math.ldexp(one, n)                     # fscale

def x87_exp2_of(t):
    """2^t for an already-rounded double t (frndint/f2xm1/fscale)"""
    n = round(t)
    f = t - n
    p = rd(Fraction(Decimal(2) ** Decimal(f) - 1))
    one = rd(Fraction(1) + Fraction(p))
    return math.ldexp(one, n)

def mul(a, b):
    return rd(Fraction(a) * Fraction(b))

def rd64(fr):
    """round a Fraction to a 64-bit-significand (x87 extended) value, as a Fraction"""
    fr = Fraction(fr)
    if fr == 0: return fr
    s = -1 if fr < 0 else 1
    a = abs(fr)
    e = a.numerator.bit_length() - a.denominator.bit_length()
    if Fraction(2) ** e > a: e -= 1
    scale = Fraction(2) ** (63 - e)
    m = a * scale
    q, r = divmod(m.numerator, m.denominator)
    if 2 * r > m.denominator or (2 * r == m.denominator and q & 1): q += 1
    return s * Fraction(q) / scale

def x87_exp2_ext(t):
    """2^t, t a double: f2xm1 result kept at 64 bits, 1 + p rounded to 53 (PC = 53)"""
    n = round(t)
    f = t - n
    p = rd64(Fraction(Decimal(2) ** Decimal(f) - 1))
    one = rd(Fraction(1) + p)
    return math.ldexp(one, n)
