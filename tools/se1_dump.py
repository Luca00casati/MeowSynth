#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
"""Readable dump of a decoded SynthEdit 1.x project: containers, modules, pin values,
wiring (by pin name) and patch-memory (preset) values.

    python3 tools/se1_dump.py MeowSynth.se1 > patch.txt
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import se1_parse  # noqa: E402

PINS = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'se_pins.json')))
GUI_CLASSES = {'control_ob_bitmap', 'control_ob_bitmap_tinted', 'control_ob_text', 'Ctl_Slider', 'Ctl_Combo',
               'Ctl_PatchSel'}


class Graph:
    def __init__(self, doc):
        self.doc = doc
        self.objs = {}
        self.owner = {}          # plug id -> module object
        self._index(doc)
        for o in self.objs.values():
            for p in o.get('plugs', []):
                self.owner[self.ref(p)['id']] = o

    def _index(self, o):
        if isinstance(o, dict):
            if 'id' in o and 'class' in o:
                self.objs[o['id']] = o
            for v in o.values():
                self._index(v)
        elif isinstance(o, list):
            for v in o:
                self._index(v)

    def ref(self, o):
        return self.objs[o['ref']] if o and 'ref' in o else o

    def mod_label(self, m):
        n = m.get('name') or ''
        t = m.get('type') or m['class']
        return f'#{m["id"]} {t}' + (f' "{n}"' if n and n != t else '')

    def pin_name(self, m, plug):
        plug = self.ref(plug)
        if plug.get('label'):
            return plug['label']
        table = PINS.get(m.get('type'))
        if table and plug['pin'] != 0xFFFF and plug['pin'] < len(table['pins']):
            return table['pins'][plug['pin']]['name']
        return f'pin{plug["pin"]}' if plug['pin'] != 0xFFFF else 'prop'

    def endpoint(self, plug):
        plug = self.ref(plug)
        m = self.owner.get(plug['id'])
        if m is None:
            return f'<plug #{plug["id"]}>'
        return f'{self.mod_label(m)}.{self.pin_name(m, plug)}'


def dump_container(g, c, depth, out):
    ind = '  ' * depth
    out.append(f'{ind}== Container {g.mod_label(c)} ({len(c["children"])} objects)')
    mods = [g.ref(x) for x in c['children']]
    lines = [m for m in mods if m['class'] == 'CLine2']
    for m in mods:
        if m['class'] in ('CLine2', 'CContainer'):
            continue
        kind = 'GUI ' if m['class'] in GUI_CLASSES or m['class'].startswith('ui_') else ''
        vals = []
        for p in m.get('plugs', []):
            p = g.ref(p)
            if not p.get('lines') and p.get('value') not in ('', None):
                vals.append(f'{g.pin_name(m, p)}={p["value"]}')
        extra = ''
        if m['class'] == 'Ctl_Slider' or m['class'] == 'Ctl_Combo':
            vs = [round(x, 4) if isinstance(x, float) else x for x in m.get('values', [])[:6]]
            extra = f' presets={vs}'
        out.append(f'{ind}  {kind}{g.mod_label(m)}' + (f'  [{", ".join(vals)}]' if vals else '') + extra)
    if lines:
        out.append(f'{ind}  -- wiring')
        for ln in lines:
            out.append(f'{ind}    {g.endpoint(ln["from"])}  ->  {g.endpoint(ln["to"])}')
    pm = c.get('patch_manager')
    if pm and pm['mems']:
        out.append(f'{ind}  -- patch memory (presets: {["(0)"] + [n for _, n in pm["names"]]})')
        for mem in pm['mems']:
            v = mem['values'][:1 + len(pm['names'])]
            v = [round(x, 5) if isinstance(x, float) else x for x in v]
            out.append(f'{ind}    {mem["name"]!r:28} {mem["kind"]:6} -> {g.endpoint(mem["plug"]) if mem["plug"] else "-"}  {v}')
    for m in mods:
        if m['class'] == 'CContainer':
            dump_container(g, m, depth + 1, out)


def main():
    ar = se1_parse.Archive(open(sys.argv[1], 'rb').read())
    doc = se1_parse.document(ar)
    g = Graph(doc)
    out = [f'SynthEdit file version {doc["version"]}, plugin {doc.get("s6")!r} ({doc.get("s2")}), skin {doc["root"]["skin"]!r}',
           f'about: {doc.get("s3")!r}', '']
    dump_container(g, doc['root'], 0, out)
    print('\n'.join(out))


if __name__ == '__main__':
    main()
