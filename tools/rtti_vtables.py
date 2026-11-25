# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
# MSVC RTTI: TypeDescriptor(name at +8) <- COL(+12, offset@+4==0) <- vtable[-1]. Print class -> vtable, Serialize (slot 2).
import pefile, sys, struct, re
pe = pefile.PE(sys.argv[1]); B = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
u32 = lambda va: struct.unpack_from('<I', img, va - B)[0]
p32 = lambda v: struct.pack('<I', v)
def allrefs(v): return [m.start() + B for m in re.finditer(re.escape(p32(v)), img)]
for name in sys.argv[2].split(','):
    m = img.find(b'.?AV' + name.encode() + b'@@\0')
    if m < 0: print(f'{name:26} no RTTI'); continue
    td = m + B - 8
    vts = []
    for col in allrefs(td):
        col -= 12
        if u32(col) != 0 or u32(col + 4) != 0: continue
        vts += [r + 4 for r in allrefs(col)]
    out = ' '.join(f'vt={v:#x} ser={u32(v+8):#x}' for v in vts)
    print(f'{name:26} td={td:#x} {out}')
