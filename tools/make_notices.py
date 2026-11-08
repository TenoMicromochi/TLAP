# Builds THIRD_PARTY_NOTICES.txt from the license texts inside third_party/ (run again if a library is updated)
import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TP = os.path.join(ROOT, 'third_party')
def rd(p): return open(os.path.join(TP, p), encoding='utf-8', errors='replace').read().strip('\n')
def stb_license():
    s = rd('stb_vorbis-1.22/stb_vorbis.c'); i = s.rindex('------------------------------------------------------------------------------\nThis software is available under 2 licenses')
    return s[i:].rsplit('*/', 1)[0].strip('\n')
parts = [
    ('stb_vorbis 1.22 (Ogg Vorbis decoder)', 'https://github.com/nothings/stb  -  Sean Barrett. MIT License or Public Domain (either).', stb_license()),
    ('libogg 1.3.6', 'https://xiph.org/ogg/  -  BSD-3-Clause style license.', rd('libogg-1.3.6/COPYING')),
    ('Opus (libopus) 1.6.1', 'https://opus-codec.org/  -  BSD-3-Clause style license (decoder part only is used by TLAP).', rd('opus-1.6.1/COPYING')),
    ('opusfile 0.12', 'https://opus-codec.org/  -  BSD-3-Clause style license.', rd('opusfile-0.12/COPYING')),
]
out = ['TLAP uses the following third-party libraries (compiled from source and linked statically).', 'Place this text after LICENSE in the distribution.', '']
for name, url, text in parts:
    out += ['=' * 78, name, url, '=' * 78, text, '']
open(os.path.join(ROOT, 'THIRD_PARTY_NOTICES.txt'), 'w', encoding='utf-8', newline='\r\n').write('\n'.join(out))
print('THIRD_PARTY_NOTICES.txt', sum(len(p[2]) for p in parts), 'chars of license text')
