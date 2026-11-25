#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
"""Read a module trace written by tools/trace_modules.cpp.

    from trace_read import Trace
    t = Trace('voice128.trace')
    t.find('ug_oscillator2')            -> list of instances
    t.samples(inst, plug)               -> all samples of that plug (one float per frame)
    t.producer(inst, plug)              -> (instance, plug) of the output feeding an input

Command line: python3 tools/trace_read.py file.trace   (lists instances with their wiring)
"""
import array
import sys


class Trace:
    def __init__(self, path):
        raw = open(path, 'rb').read()
        head, _, rest = raw.partition(b'DATA ')
        nl = rest.index(b'\n')
        self.block = int(rest[:nl])
        data = rest[nl + 1:]
        self.inst = []
        for line in head.decode('latin1').splitlines():
            parts = line.split()
            if not parts or parts[0] != 'INST':
                continue
            plugs = []
            for p in parts[4:]:
                idx, d, buf = p.split(':')
                plugs.append({'index': int(idx), 'dir': d, 'buf': int(buf, 16)})
            self.inst.append({'n': int(parts[1]), 'addr': int(parts[2], 16), 'cls': parts[3], 'plugs': plugs})
        self.nplugs = sum(len(i['plugs']) for i in self.inst)
        self.f = array.array('f', data)
        self.blocks = len(self.f) // (self.nplugs * self.block)
        # column of each (instance, plug) in a block record
        col = 0
        self.col = {}
        for i in self.inst:
            for p in i['plugs']:
                self.col[(i['n'], p['index'])] = col
                col += 1
        # producers: output buffer -> (instance, plug)
        self.out_by_buf = {}
        for i in self.inst:
            for p in i['plugs']:
                if p['dir'] == 'out' and p['buf']:
                    self.out_by_buf[p['buf']] = (i, p['index'])

    def find(self, cls):
        return [i for i in self.inst if i['cls'] == cls]

    def by_n(self, n):
        return next(i for i in self.inst if i['n'] == n)

    def plug(self, inst, index):
        return next(p for p in inst['plugs'] if p['index'] == index)

    def producer(self, inst, index):
        return self.out_by_buf.get(self.plug(inst, index)['buf'])

    def consumers(self, inst, index):
        buf = self.plug(inst, index)['buf']
        return [(i, p['index']) for i in self.inst for p in i['plugs'] if p['dir'] == 'in' and p['buf'] == buf]

    def samples(self, inst, index):
        c = self.col[(inst['n'], index)]
        out = array.array('f')
        stride = self.nplugs * self.block
        for b in range(self.blocks):
            o = b * stride + c * self.block
            out.extend(self.f[o:o + self.block])
        return out

    def describe(self, inst):
        s = f'#{inst["n"]} {inst["cls"]}'
        for p in inst['plugs']:
            if p['dir'] == 'in':
                src = self.out_by_buf.get(p['buf'])
                s += f' {p["index"]}<-' + (f'#{src[0]["n"]}.{src[1]}' if src else ('-' if not p['buf'] else 'own'))
            else:
                s += f' {p["index"]}->[' + ','.join(f'#{i["n"]}.{k}' for i, k in self.consumers(inst, p['index'])) + ']'
        return s


if __name__ == '__main__':
    t = Trace(sys.argv[1])
    print(f'{len(t.inst)} instances, {t.nplugs} plugs, {t.blocks} blocks of {t.block}')
    for i in t.inst:
        print(t.describe(i))
