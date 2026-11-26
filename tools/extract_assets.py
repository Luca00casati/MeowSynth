#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
"""Extract the interface images from your own copy of the original MeowSynth.dll.

    python3 tools/extract_assets.py MeowSynth.dll [assets/interface]

Writes the 'meow' skin's PNGs (panel, knob strip, cat animation, selectors), skin.json with
each image's frame size and frame count, and the 24-bit BMPs the plugin embeds: the background and
one frame strip per control, pre-blended over the panel at the control's position (DamSDK draws
BMPs without alpha). The images are not in the repository; the plugin build embeds them from
assets/interface when they are present.
"""
import json
import os
import struct
import sys
import zlib

import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import se1_parse  # noqa: E402


def se1_resource(dll_path):
    pe = pefile.PE(dll_path)
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.name is None or str(t.name) != 'SE1':
            continue
        for e in t.directory.entries:
            if e.id == 165:
                d = e.directory.entries[0].data.struct
                return bytearray(pe.get_memory_mapped_image()[d.OffsetToData:d.OffsetToData + d.Size])
    raise SystemExit('no SE1 #165 resource: is this MeowSynth.dll?')


def decode(d):  # FUN_10067f70
    i = 0
    while i < len(d):
        a = d[i]
        d[i] = a ^ 0x83
        i += 3 if a & 1 else 1
    return bytes(d)


def png_rgba(data):
    """Decode an 8-bit RGBA, non-interlaced PNG (what SynthEdit skins use)."""
    p, idat = 8, b''
    while p < len(data):
        n, t = struct.unpack('>I4s', data[p:p + 8])
        b = data[p + 8:p + 8 + n]
        p += 12 + n
        if t == b'IHDR':
            w, h, depth, ctype = struct.unpack('>IIBB', b[:10])
        elif t == b'IDAT':
            idat += b
    if depth != 8 or ctype != 6:
        raise SystemExit('unexpected PNG format')
    raw, stride = zlib.decompress(idat), w * 4
    out, prev, i = bytearray(), bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b, c = prev[x], (prev[x - 4] if x >= 4 else 0)
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    return w, h, out


class Canvas:
    """An RGB image; images are alpha-composited onto it like SynthEdit draws its panel."""

    def __init__(self, w, h, rgb=(0, 0, 0)):
        self.w, self.h = w, h
        self.px = bytearray(bytes(rgb) * (w * h))

    def draw(self, img, x0, y0, src_y=0, src_h=None):
        w, h, d = img
        src_h = h - src_y if src_h is None else src_h
        for y in range(src_h):
            ty = y0 + y
            if not 0 <= ty < self.h:
                continue
            for x in range(w):
                tx = x0 + x
                if not 0 <= tx < self.w:
                    continue
                s = ((src_y + y) * w + x) * 4
                a = d[s + 3]
                t = (ty * self.w + tx) * 3
                if a == 0:  # transparent pixels leave the panel as is
                    continue
                for k in range(3):  # SynthEdit's blend (fits its panel pixel for pixel)
                    self.px[t + k] = (d[s + k] * (a + 1) + self.px[t + k] * (255 - a)) >> 8

    def crop(self, x, y, w, h):
        c = Canvas(w, h)
        for yy in range(h):
            c.px[yy * w * 3:(yy + 1) * w * 3] = self.px[((y + yy) * self.w + x) * 3:((y + yy) * self.w + x + w) * 3]
        return c

    def save_bmp(self, path):
        stride = (self.w * 3 + 3) & ~3
        body = bytearray()
        for y in range(self.h - 1, -1, -1):
            row = self.px[y * self.w * 3:(y + 1) * self.w * 3]
            row = bytes(b for i in range(0, len(row), 3) for b in (row[i + 2], row[i + 1], row[i]))
            body += row + b'\0' * (stride - len(row))
        head = struct.pack('<2sIHHI', b'BM', 54 + len(body), 0, 0, 54)
        info = struct.pack('<IiiHHIIiiII', 40, self.w, self.h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
        open(path, 'wb').write(head + info + body)


# The panel as the original draws it (positions measured on its editor, see docs/gui-notes.md):
# image, position, and for strips the parameter that picks the frame.
PANEL_SIZE = (335, 228)
PANEL_AT = (-2, 0)
STRIPS = {  # output name: (skin image, x, y)
    'knob_m': ('knob_med', 200, 28), 'knob_o': ('knob_med', 230, 28), 'knob_e': ('knob_med', 260, 28),
    'knob_w': ('knob_med', 290, 28), 'knob_vibamount': ('knob_med', 200, 91),
    'knob_vibattack': ('knob_med', 230, 91), 'knob_porta': ('knob_med', 200, 146),
    'knob_noise': ('knob_med', 230, 146), 'knob_chorus': ('knob_med', 260, 146),
    'lfo': ('LFO', 266, 96), 'pitchrange': ('pitchrange', 294, 151), 'polycat': ('polycat', 199, 195),
    'url': ('URL', 15, 188), 'cat': ('baks', 18, 20),
}


def composite(out, images, info):
    """Background and per-control frame strips pre-blended over the panel, as 24-bit BMPs."""
    bg = Canvas(*PANEL_SIZE)
    bg.draw(images['panels'], *PANEL_AT)
    bg.save_bmp(os.path.join(out, 'background.bmp'))
    for name, (img, x, y) in STRIPS.items():
        fw, fh, n = info[img]['frame_width'], info[img]['frame_height'], info[img]['frames']
        strip = Canvas(fw, fh * n)
        under = bg.crop(x, y, fw, fh)
        for f in range(n):
            frame = Canvas(fw, fh)
            frame.px[:] = under.px
            frame.draw(images[img], 0, 0, f * fh, fh)
            strip.px[f * fw * fh * 3:(f + 1) * fw * fh * 3] = frame.px
        strip.save_bmp(os.path.join(out, name + '.bmp'))
        print(f'{name + ".bmp":18} {n} frames at ({x}, {y})')


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'assets', 'interface')
    os.makedirs(out, exist_ok=True)
    data = decode(se1_resource(sys.argv[1]))
    doc = se1_parse.document(se1_parse.Archive(data))
    skin = next(s for s in doc['skins'] if s['name'] == 'meow')
    info, images = {}, {}
    for it in skin['items']:
        name = it['file'].replace('\\', '/').split('/')[-1]
        png = data[it['png_at']:it['png_at'] + it['png_size']]
        open(os.path.join(out, name + '.png'), 'wb').write(png)
        images[name] = png_rgba(png)
        fw, fh = it['anim_pt']
        info[name] = {'frame_width': fw, 'frame_height': fh, 'frames': it['anim2'][0] + 1}
        print(f'{name + ".png":18} frames {info[name]["frames"]:3} of {fw}x{fh}')
    json.dump(info, open(os.path.join(out, 'skin.json'), 'w'), indent=1)
    composite(out, images, info)


if __name__ == '__main__':
    main()
