# ALAC（Apple Lossless）の .m4a を自分で書く（Media Foundation の変換は ALAC を m4a に仕上げられないため）。
# 圧縮せずに「素の PCM を ALAC のフレームに入れる」書き方（escape フレーム）。形式としては正しい ALAC。
import math, struct

def _box(t, payload): return struct.pack('>I4s', 8 + len(payload), t) + payload
def _full(t, payload, ver=0, flags=0): return _box(t, struct.pack('>I', (ver << 24) | flags) + payload)

class _Bits:
    def __init__(self): self.v = 0; self.n = 0
    def put(self, value, nbits): self.v = (self.v << nbits) | (value & ((1 << nbits) - 1)); self.n += nbits
    def bytes(self):
        pad = (-self.n) % 8
        v = self.v << pad
        return v.to_bytes((self.n + pad) // 8, 'big')

def write_alac(path, left, right, rate):
    """left / right: -32768..32767 の整数のリスト（同じ長さ）。16bit ステレオ。"""
    total = len(left); FL = 4096
    frames = []
    for s in range(0, total, FL):
        n = min(FL, total - s)
        b = _Bits()
        b.put(1, 3); b.put(0, 4); b.put(0, 12)            # CPE、instance 0、unused
        partial = n != FL
        b.put((8 if partial else 0) | 1, 4)               # partialFrame ビット + escape(= 圧縮なし)
        if partial: b.put(n, 32)
        for i in range(s, s + n): b.put(left[i], 16); b.put(right[i], 16)
        b.put(7, 3)                                       # end tag
        frames.append(b.bytes())
    mdat_payload = b''.join(frames)
    ftyp = _box(b'ftyp', b'M4A ' + struct.pack('>I', 0) + b'M4A mp42isom')
    mdat = _box(b'mdat', mdat_payload)
    offset = len(ftyp) + 8
    nfull, rem = divmod(total, FL)
    stts_entries = [(nfull, FL)] + ([(1, rem)] if rem else [])
    stts = _full(b'stts', struct.pack('>I', len(stts_entries)) + b''.join(struct.pack('>II', c, d) for c, d in stts_entries))
    stsc = _full(b'stsc', struct.pack('>IIII', 1, 1, len(frames), 1))
    stsz = _full(b'stsz', struct.pack('>II', 0, len(frames)) + b''.join(struct.pack('>I', len(f)) for f in frames))
    stco = _full(b'stco', struct.pack('>II', 1, offset))
    cookie = struct.pack('>IBBBBBBHIII', FL, 0, 16, 40, 10, 14, 2, 255, max(len(f) for f in frames), int(rate * 32), rate)
    alac_in = _full(b'alac', cookie)
    entry = struct.pack('>6xH', 1) + struct.pack('>HHIHHHHI', 0, 0, 0, 2, 16, 0, 0, rate << 16) + alac_in
    stsd = _full(b'stsd', struct.pack('>I', 1) + _box(b'alac', entry))
    stbl = _box(b'stbl', stsd + stts + stsc + stsz + stco)
    dinf = _box(b'dinf', _full(b'dref', struct.pack('>I', 1) + _full(b'url ', b'', 0, 1)))
    minf = _box(b'minf', _full(b'smhd', struct.pack('>HH', 0, 0)) + dinf + stbl)
    mdhd = _full(b'mdhd', struct.pack('>IIIIHH', 0, 0, rate, total, 0x55C4, 0))
    hdlr = _full(b'hdlr', struct.pack('>I4s12x', 0, b'soun') + b'SoundHandler\0')
    mdia = _box(b'mdia', mdhd + hdlr + minf)
    matrix = struct.pack('>9I', 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000)
    tkhd = _full(b'tkhd', struct.pack('>IIIII8xhhhh', 0, 0, 1, 0, total, 0, 0, 0x0100, 0) + matrix + struct.pack('>II', 0, 0), 0, 7)
    trak = _box(b'trak', tkhd + mdia)
    mvhd = _full(b'mvhd', struct.pack('>IIII', 0, 0, rate, total) + struct.pack('>IH', 0x10000, 0x0100) + b'\0' * 10 + matrix + b'\0' * 24 + struct.pack('>I', 2))
    moov = _box(b'moov', mvhd + trak)
    open(path, 'wb').write(ftyp + mdat + moov)
