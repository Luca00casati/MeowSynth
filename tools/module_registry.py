# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
# Map SynthEdit module type names -> factory -> ug_* class, using the static descriptor table
# initializer (mov [mem], imm stores) and RTTI vtables written by the factory's constructor chain.
import pefile, sys, re, struct, subprocess, json
dll = sys.argv[1]
pe = pefile.PE(dll); B = pe.OPTIONAL_HEADER.ImageBase; img = pe.get_memory_mapped_image()
u32 = lambda va: struct.unpack_from('<I', img, va - B)[0]
refs = lambda v: [m.start() + B for m in re.finditer(re.escape(struct.pack('<I', v)), img)]
def cstr(va):
    o = va - B; e = img.find(b'\0', o)
    s = img[o:e]
    return s.decode() if 0 < len(s) < 60 and all(32 <= c < 127 for c in s) else None
# vtable -> class name via RTTI
vt2cls = {}
for m in re.finditer(rb'\.\?AV([A-Za-z0-9_]+)@@\0', img):
    td = m.start() + B - 8
    for col in refs(td):
        col -= 12
        if u32(col) == 0 and u32(col + 4) == 0:
            for r in refs(col): vt2cls[r + 4] = m.group(1).decode()
# descriptor stores: C7 05 <addr> <imm>
TEXT_LO, TEXT_HI = B + 0x1000, B + 0x13e000
stores = {}
for m in re.finditer(rb'\xc7\x05', img[0x1000:0x13e000]):
    o = m.start() + 0x1000
    addr, imm = struct.unpack_from('<II', img, o + 2)
    if 0x10190000 <= addr < 0x101a0000: stores[addr] = imm
def dis(fn, n=0x120):
    out = subprocess.run(['objdump', '-d', '-M', 'intel', f'--start-address={fn:#x}', f'--stop-address={fn+n:#x}', dll],
                         capture_output=True, text=True).stdout
    lines = []
    for l in out.splitlines():
        if '\tret' in l or 'ret ' in l.split('\t')[-1]: lines.append(l); break
        lines.append(l)
    return lines
def cls_of(fn, depth=0):
    last = None
    for l in dis(fn):
        ins = l.split('\t')[-1]
        mv = re.match(r'mov\s+DWORD PTR \[(e\w\w)\],(0x[0-9a-f]+)', ins)
        if mv and int(mv.group(2), 16) in vt2cls: last = vt2cls[int(mv.group(2), 16)]
        c = re.match(r'call\s+(0x[0-9a-f]+)', ins)
        if c and depth < 2:
            r = cls_of(int(c.group(1), 16), depth + 1)
            if r: last = r if (last is None or depth == 0) else last
    return last
reg = {}
for addr in sorted(stores):
    name = cstr(stores[addr]) if TEXT_HI <= stores[addr] < B + 0x18f000 else None
    if not name: continue
    for k in range(4, 0x24, 4):
        f = stores.get(addr + k)
        if f and TEXT_LO <= f < TEXT_HI:
            reg[name] = (hex(f), cls_of(f)); break
want = set(sys.argv[2].split(','))
for n in sorted(want): print(f'{n:28} {reg.get(n)}')
json.dump(reg, open(sys.argv[3], 'w'), indent=1)
