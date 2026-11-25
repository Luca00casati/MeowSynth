# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
import struct, sys, os
data = open(sys.argv[1],'rb').read()
out = sys.argv[2] if len(sys.argv)>2 else None
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe+6)[0]
optsz = struct.unpack_from('<H', data, pe+20)[0]
opt = pe+24
rsrc_rva, rsrc_sz = struct.unpack_from('<II', data, opt+96+2*8)
secs=[]
for i in range(nsec):
    o = opt+optsz+i*40
    name = data[o:o+8].rstrip(b'\0').decode()
    vsz, va, rsz, raw = struct.unpack_from('<IIII', data, o+8)
    secs.append((name,va,max(vsz,rsz),raw))
def r2o(rva):
    for n,va,sz,raw in secs:
        if va<=rva<va+sz: return rva-va+raw
base = r2o(rsrc_rva)
TYPES={1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',9:'ACCEL',10:'RCDATA',12:'GROUP_CURSOR',14:'GROUP_ICON',16:'VERSION',24:'MANIFEST',23:'HTML'}
def name(e):
    if e & 0x80000000:
        o = base+(e&0x7fffffff); n = struct.unpack_from('<H',data,o)[0]
        return data[o+2:o+2+2*n].decode('utf-16le')
    return e
def walk(off, path):
    _,_,_,_,nn,ni = struct.unpack_from('<IIHHHH', data, base+off)
    for i in range(nn+ni):
        nm, tgt = struct.unpack_from('<II', data, base+off+16+i*8)
        p = path+[name(nm)]
        if tgt & 0x80000000: walk(tgt&0x7fffffff, p)
        else:
            rva, sz = struct.unpack_from('<II', data, base+tgt)
            t = p[0]; t = TYPES.get(t,t)
            blob = data[r2o(rva):r2o(rva)+sz]
            print(f'{str(t):14} {str(p[1]):24} lang={p[2]:<5} size={sz:8}  head={blob[:16]!r}')
            if out:
                d = os.path.join(out, str(t)); os.makedirs(d, exist_ok=True)
                open(os.path.join(d, f'{p[1]}_{p[2]}.bin'),'wb').write(blob)
walk(0, [])
