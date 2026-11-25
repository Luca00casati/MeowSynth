#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
"""Rebuild the oscillator wavetables and compare them with a dump from the original
(tools/dump_wavetables.cpp). See docs/dsp-notes.md.

    python3 tools/wavetable_check.py wavetables.bin
"""
import math, struct, sys
def harmonics(kind):
    if kind in ('sine',): return [1.0], [1.0], 1.0
    if kind == 'saw':     return [-1.0 / k for k in range(1, 257)], [float(k) for k in range(1, 257)], 0.64
    if kind == 'tri':
        amps, nums = [], []
        for i, n in enumerate(range(1, 256, 2)):
            amps.append((1.0 if i % 2 == 0 else -1.0) / (n * n)); nums.append(float(n))
        return amps, nums, 0.81
def build(kind, smooth):
    amps, nums, scale = harmonics(kind)
    amps = [a * (scale * 0.5) for a in amps]
    n = len(amps); sqrt2 = 2 ** 0.5
    bands = []   # built low harmonic count first; final order reversed (inserted at front)
    i = 0
    while True:
        if i == n - 1: nxt = i + 1
        else:
            nxt = i
            while True:
                nxt += 1
                if sqrt2 * nums[i] <= nums[nxt]: break
                if not nxt < n - 1: break
        top = i
        maxf = 2147483648.0 / nums[top] * 2 ** (-1 / 6)
        maxinc = int(maxf)
        if smooth:
            a = [(math.cos((nums[k] - 1) * float(struct.unpack('<f', struct.pack('<f', math.pi))[0]) / (top + 1)) * 0.46000000834465027 + 0.54000002145767212) * amps[k] for k in range(top + 1)]
        else:
            a = amps[:top + 1]
        w = 6.2831854820251465 * 0.001953125
        data = []
        for s in range(514):
            ph = s * w
            acc = 0.0
            for k in range(top, -1, -1):
                acc += math.sin(ph * nums[k]) * a[k]
            f = struct.unpack('<f', struct.pack('<f', acc))[0]
            if -2.25346663e-31 < f < 2.25346663e-31: f = 0.0
            data.append(f)
        bands.insert(0, {'max': maxinc, 'maxf': maxf, 'data': data})
        if len(bands) > 1:
            bands[1]['f18'] = maxinc; bands[1]['min'] = int(maxf * 2 ** (-1 / 12))
        i = nxt
        if not i < n: break
    bands[0]['f18'] = 0; bands[0]['min'] = 0
    return bands
def load(path):
    d = open(path, 'rb').read(); o = 0; sets = {}
    while o < len(d):
        va, cnt = struct.unpack_from('<II', d, o); o += 8; bl = []
        for _ in range(cnt):
            size, mn, mx, f18 = struct.unpack_from('<4I', d, o); o += 16
            bl.append({'min': mn, 'max': mx, 'f18': f18, 'data': list(struct.unpack_from(f'<{size}f', d, o))}); o += 4 * size
        sets[va] = bl
    return sets
ref = load(sys.argv[1])
for va, kind, smooth in ((0x1019a3fc, 'sine', True), (0x1019a410, 'saw', True), (0x1019a424, 'saw', False), (0x1019a438, 'tri', False)):
    mine = build(kind, smooth); r = ref[va]
    print(f'{va:#x} {kind} smooth={smooth}: bands {len(mine)} vs {len(r)}')
    for b, (m, o) in enumerate(zip(mine, r)):
        hd = (m['min'] == o['min'], m['max'] == o['max'], m['f18'] == o['f18'])
        diff = [abs(x - y) for x, y in zip(m['data'], o['data'])]
        neq = sum(1 for x, y in zip(m['data'], o['data']) if x != y)
        if not all(hd) or neq: print(f'   band {b}: hdr ok={hd} {m["min"]:#x}/{o["min"]:#x} {m["max"]:#x}/{o["max"]:#x}  samples differing {neq}/514, max abs {max(diff):.3g}')
    print('   ok' if all(m['max']==o['max'] and m['min']==o['min'] for m,o in zip(mine,r)) else '')
