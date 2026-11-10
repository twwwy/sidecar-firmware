#!/usr/bin/env python3
"""Regenerate immutable math tables; Python standard library only. MIT."""
import math
from pathlib import Path
out = Path(__file__).resolve().parents[1] / 'fm4_tables.c'
with out.open('w') as f:
    f.write('// Original FM4 tables. SPDX-License-Identifier: MIT\n#include "fm4_tables.h"\n')
    for name, values in [('fm4_sine', [math.sin(2*math.pi*i/4096) for i in range(4097)]),
                         ('fm4_exp2_fraction', [2**(i/1024) for i in range(1025)])]:
        f.write('const float %s[%d] = {\n' % (name, len(values)))
        for i in range(0, len(values), 8):
            f.write('  '+', '.join(f'{v:.9e}f' for v in values[i:i+8])+',\n')
        f.write('};\n')
