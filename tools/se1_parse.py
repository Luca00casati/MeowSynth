#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
"""Parse a decoded SynthEdit 1.x project (.se1, MFC CArchive) into a module graph.

Each class reader mirrors the class's Serialize() in the original runtime
(addresses in comments are in MeowSynth.dll). The file version gates fields
exactly like FileVersion() comparisons there.

    python3 tools/se1_parse.py MeowSynth.se1 [--json out.json] [--trace]
"""
import argparse
import json
import struct
import sys


class Desync(Exception):
    pass


class Archive:
    def __init__(self, data, trace=False):
        self.d = data
        self.pos = 0
        self.ver = 0
        self.trace = trace
        self.depth = 0
        # MFC load map: index 0 = NULL; classes and objects share one counter.
        self.map = [None]
        self.objects = []

    # --- primitives -------------------------------------------------------
    def take(self, n):
        if self.pos + n > len(self.d):
            raise Desync(f'read past end at {self.pos:#x}')
        b = self.d[self.pos:self.pos + n]
        self.pos += n
        return b

    def u8(self):  return self.take(1)[0]
    def u16(self): return struct.unpack('<H', self.take(2))[0]
    def i32(self): return struct.unpack('<i', self.take(4))[0]
    def u32(self): return struct.unpack('<I', self.take(4))[0]
    def f32(self): return struct.unpack('<f', self.take(4))[0]
    def f64(self): return struct.unpack('<d', self.take(8))[0]

    def strlen(self):
        """AfxReadStringLength: returns (length, is_unicode)."""
        uni = False
        while True:
            n = self.u8()
            if n < 0xFF:
                return n, uni
            n = self.u16()
            if n == 0xFFFE:
                uni = True
                continue
            if n < 0xFFFF:
                return n, uni
            return self.u32(), uni

    def cstr(self):
        n, uni = self.strlen()
        if n > 0x10000:
            raise Desync(f'string length {n} at {self.pos:#x}')
        return self.take(2 * n).decode('utf-16le') if uni else self.take(n).decode('latin1')

    def count(self):
        """CObList / CArray count: WORD, or 0xFFFF + DWORD."""
        n = self.u16()
        return self.u32() if n == 0xFFFF else n

    # --- objects ----------------------------------------------------------
    def read_object(self):
        at = self.pos
        tag = self.u16()
        if tag == 0x7FFF:
            big = self.u32()
            is_class, idx = bool(big & 0x80000000), big & 0x7FFFFFFF
        elif tag == 0xFFFF:
            schema = self.u16()
            name = self.take(self.u16()).decode('latin1')
            self.map.append(('class', name, schema))
            is_class, idx = True, len(self.map) - 1
        else:
            is_class, idx = bool(tag & 0x8000), tag & 0x7FFF
        if not is_class:
            if idx == 0:
                return None
            ref = self.map[idx] if idx < len(self.map) else None
            if not isinstance(ref, dict):
                raise Desync(f'bad object ref {idx} at {at:#x} (map size {len(self.map)})')
            return {'ref': ref['id']}
        cls = self.map[idx]
        if not (isinstance(cls, tuple) and cls[0] == 'class'):
            raise Desync(f'bad class ref {idx} at {at:#x}')
        name = cls[1]
        obj = {'id': len(self.objects), 'class': name, 'at': at}
        self.map.append(obj)
        self.objects.append(obj)
        reader = READERS.get(name)
        if reader is None:
            raise Desync(f'no reader for class {name} at {at:#x}')
        if self.trace:
            print(f'{"  " * self.depth}{at:#08x} {name} #{obj["id"]}', file=sys.stderr)
        self.depth += 1
        reader(self, obj)
        self.depth -= 1
        obj['end'] = self.pos
        return obj

    def oblist(self):
        return [self.read_object() for _ in range(self.count())]


V = lambda ar: ar.ver


# --- class readers (mirroring Serialize in the original DLL) --------------

def imbedded_file(ar, o):                        # 0x100384f0
    o['name'] = ar.cstr()
    n = ar.u32()
    o['size'] = n
    o['data_at'] = ar.pos
    ar.take(n)


def CDocOb(ar, o):                               # 0x100309d0
    if V(ar) > 7:
        o['type'] = ar.cstr()                    # module type id string


def CUG(ar, o):                                  # 0x1002b790
    if V(ar) > 0x24:
        CDocOb(ar, o)
    o['plugs'] = ar.oblist()
    o['name'] = ar.cstr()
    o['flag'] = ar.u16()
    if V(ar) > 0x24:
        o['rect'] = struct.unpack('<4i', ar.take(16))


def CPlug2(ar, o):                               # 0x100551a0
    o['pin'] = ar.u16()                          # pin index, 0xFFFF = property
    o['value'] = ar.cstr()                       # default value as text
    if V(ar) > 0x23:
        o['lines'] = ar.oblist()                 # member +0x34 (connections)


def CPlugNameable(ar, o):                        # 0x10015b60
    CPlug2(ar, o)
    o['label'] = ar.cstr()


def CPlugIO2(ar, o):                             # 0x100147f0
    CPlugNameable(ar, o)
    o['io_index'] = ar.u16()
    o['partner'] = ar.read_object()
    if V(ar) > 0x172F1:
        o['io_flags'] = ar.u16()


def CPlugFeature(ar, o):                         # 0x10013950
    CPlug2(ar, o)
    o['datatype'] = ar.u32()
    o['f68'] = ar.u32()
    o['f5c'] = ar.u32()
    o['label'] = ar.cstr()
    o['meta'] = ar.cstr()
    if V(ar) > 0x173B9:
        o['f70'] = ar.u32()


def CPlugFeature3(ar, o):                        # 0x100560a0
    CPlugFeature(ar, o)
    o['f74'] = ar.u8()


def CLine2(ar, o):                               # 0x1000aef0
    if V(ar) > 0x24:
        CDocOb(ar, o)
    o['from'] = ar.read_object()
    o['to'] = ar.read_object()


def CPlugin2(ar, o):                             # 0x100573e0 -> 0x10014470
    CUG(ar, o)
    o['sem'] = ar.cstr()
    o['guid2'] = ar.take(16).hex()


def control_ob(ar, o):                           # 0x1000fb20 -> 0x100104b0
    CUG(ar, o)
    o['bitmap'] = ar.read_object()
    o['ctl_rect'] = struct.unpack('<4i', ar.take(16))
    pairs = []
    k = ar.i32()
    while k != -1:
        pairs.append((k, ar.i32(), ar.i32()))
        k = ar.i32()
    o['pairs'] = pairs


def ctl_value(ar, o):                            # 0x1000bca0 (Ctl_Slider, Ctl_Combo)
    CUG(ar, o)
    if V(ar) > 0x12:
        if V(ar) > 0x53:
            o['f10c'] = ar.u32()
        n = ar.u32()
        kind = CTL_VALUE_KIND[o['class']]
        if kind == 0:
            o['values'] = [ar.i32() for _ in range(n)]
        elif kind == 3:
            o['values'] = [ar.f64() for _ in range(n)]
        else:
            raise Desync(f'ctl_value kind {kind}')
    o['ctl_rect'] = struct.unpack('<4i', ar.take(16))
    if V(ar) > 0x3C:
        o['ctl_name'] = ar.cstr()


CTL_VALUE_KIND = {'Ctl_Slider': 3, 'Ctl_Combo': 0}


def Ctl_PatchSel(ar, o):                         # 0x1001f860
    CUG(ar, o)


def CPatchManager(ar, o):                        # 0x1004bb30
    o['f8'] = ar.u32()
    o['names'] = [(ar.u32(), ar.cstr()) for _ in range(ar.u32())]
    o['mems'] = []
    if V(ar) > 0x1744E:
        while True:
            t = ar.i32()
            if t < 0:
                break
            o['mems'].append(patch_mem(ar, t))


PATCH_TYPES = {0: 'enum', 1: 'filename', 4: 'bool', 6: 'float', 8: 'int'}
PATCH_VALUE = {'enum': lambda ar: ar.u16(), 'filename': lambda ar: ar.cstr(),
               'bool': lambda ar: ar.u8(), 'float': lambda ar: ar.f32(), 'int': lambda ar: ar.i32()}


def patch_mem(ar, t):                            # PatchParameter<T>::Serialize (vtable +0x20)
    kind = PATCH_TYPES.get(t)
    if kind is None:
        raise Desync(f'patch memory type {t} not implemented (at {ar.pos:#x})')
    m = {'kind': kind}                           # PatchParameter_base 0x10050f30
    m['handle'] = ar.u32()
    m['plug'] = ar.read_object()
    m['name'] = ar.cstr()
    m['private'] = ar.u8()
    m['automation'] = ar.i32()
    m['ignore_pc'] = ar.u8()
    m['value_type'] = ar.u32()                   # float 0x1004d0d0, bool 0x1004d820, ...
    n = ar.i32()
    m['values'] = [PATCH_VALUE[kind](ar) for _ in range(n)]
    return m


def CContainer(ar, o):                           # 0x100069f0
    CUG(ar, o)
    if V(ar) > 0x17425:
        o['patch_manager'] = ar.read_object()
    o['children'] = []
    while True:
        c = ar.read_object()
        if c is None:
            break
        o['children'].append(c)
    o['r_fc'] = ar.take(16).hex()
    o['r_dc'] = ar.take(16).hex()
    o['r_ec'] = ar.take(16).hex()
    o['r_10c'] = ar.take(8).hex()
    o['r_114'] = ar.take(8).hex()
    o['fd4'] = ar.u32()
    o['skin'] = ar.cstr()
    o['f13c'] = ar.u8()


def CSkinBitmap(ar, o):                          # 0x1005f830
    o['w'] = ar.u32()
    o['h'] = ar.u32()
    o['file'] = ar.cstr()
    if V(ar) > 0x173BA:
        o['fmt'] = ar.u32()
    n = ar.u32()
    o['png_at'], o['png_size'] = ar.pos, n
    ar.take(n)


def CAnimBitmap(ar, o):                          # 0x100012c0
    CSkinBitmap(ar, o)
    o['anim'] = [ar.i32() for _ in range(4)]
    o['anim_rect'] = struct.unpack('<4i', ar.take(16))
    o['anim_pt'] = struct.unpack('<2i', ar.take(8))
    o['anim2'] = [ar.i32() for _ in range(2)]
    if V(ar) > 0x17354:
        o['anim3'] = [ar.i32() for _ in range(3)]
    o['anim4'] = ar.i32()                        # TODO: locate in decompile


def SkinFontInfo(ar, o):                         # 0x100615d0
    o['style'] = ar.cstr()
    o['face'] = ar.cstr()
    o['size'] = ar.i32()
    o['color'] = ar.u32()
    if V(ar) > 0x17399:
        o['f14'] = ar.u32()
        o['f18'] = ar.u32()


READERS = {
    'imbedded_file': imbedded_file,
    'CUG': CUG, 'CSwitch': CUG, 'CPlug2': CPlug2, 'CPlugNameable': CPlugNameable,
    'CPlugIO2': CPlugIO2, 'CPlugFeature': CPlugFeature, 'CPlugFeature2': CPlugFeature,
    'CPlugFeature4': CPlugFeature, 'CPlugFeature3': CPlugFeature3, 'CLine2': CLine2,
    'CPlugin2': CPlugin2, 'CContainer': CContainer, 'CPatchManager': CPatchManager,
    'control_ob_bitmap': control_ob, 'control_ob_bitmap_tinted': control_ob,
    'control_ob_text': control_ob,
    'Ctl_Slider': ctl_value, 'Ctl_Combo': ctl_value, 'Ctl_PatchSel': Ctl_PatchSel,
    'CAnimBitmap': CAnimBitmap, 'SkinFontInfo': SkinFontInfo,
}
for _ui in ('ui_param_enum_in2', 'ui_bitmap_frame', 'ui_param_float_in2', 'ui_text_to_float',
            'ui_float_scaler', 'ui_param_float_out2', 'ui_param_text_in', 'ui_float_to_bool',
            'ui_system_command'):
    READERS[_ui] = CUG


def skin(ar):                                    # skin record (0x100617c0 -> vfunc +8)
    s = {'name': ar.cstr()}
    s['items'] = []
    while (o := ar.read_object()) is not None:
        s['items'].append(o)
    s['fonts'] = ar.oblist()
    return s


def document(ar):                                # 0x100685b0
    doc = {}
    ar.ver = doc['version'] = ar.u32()
    if ar.ver > 0x52:
        doc['files'] = ar.oblist()
    doc['title'] = ar.cstr()
    if ar.ver >= 9:
        doc['d1'] = ar.i32()
    if ar.ver > 0x2C:
        doc['d2'] = ar.i32()
    if ar.ver > 0x37:
        doc['s1'] = ar.cstr()
        if ar.ver > 0x3D: doc['s2'] = ar.cstr()
        if ar.ver > 0x39: doc['d3'] = ar.i32(); doc['s3'] = ar.cstr()
        if ar.ver > 0x3F: doc['d4'] = ar.i32()
        if ar.ver > 0x38: doc['d5'] = ar.take(8).hex()
        if ar.ver > 0x44:
            doc['d6'] = ar.i32(); doc['d7'] = ar.i32()
            if ar.ver > 0x46: doc['s4'] = ar.cstr()
            if ar.ver > 0x55: doc['s5'] = ar.cstr()
            doc['s6'] = ar.cstr()
        doc['has_skins'] = ar.i32()
        if doc['has_skins']:
            doc['skins'] = [skin(ar) for _ in range(ar.i32() if ar.ver > 0x3A else 1)]
    if ar.ver > 0x17428:
        doc['registered'] = ar.u8()
    doc['root'] = ar.read_object()
    return doc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('se1')
    ap.add_argument('--json')
    ap.add_argument('--trace', action='store_true')
    a = ap.parse_args()
    ar = Archive(open(a.se1, 'rb').read(), a.trace)
    try:
        doc = document(ar)
    except Desync as e:
        print(f'DESYNC: {e}', file=sys.stderr)
        p = ar.pos
        print(f'context @ {p:#x}: {ar.d[p - 32:p].hex(" ")} | {ar.d[p:p + 48].hex(" ")}', file=sys.stderr)
        sys.exit(1)
    print(f'parsed {ar.pos:#x} / {len(ar.d):#x} bytes, {len(ar.objects)} objects', file=sys.stderr)
    if a.json:
        json.dump(doc, open(a.json, 'w'), indent=1, default=str)


if __name__ == '__main__':
    main()
