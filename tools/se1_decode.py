# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Luca Casati
import sys
d=bytearray(open(sys.argv[1],'rb').read()); i=0
while i<len(d):
    a=d[i]; d[i]=a^0x83; i+= 3 if a&1 else 1
open(sys.argv[2],'wb').write(d)
