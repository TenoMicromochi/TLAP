# images/*.png（16x16 パレット PNG、Kaname の絵が正本）を読み、res/ に出力する
#   res/icon_<役割>.png : ツールバー用の RGBA PNG（.rc で埋め込む）
#   res/app.ico         : icon.png を最近傍で整数倍にした 16/32/48/64/256
# 標準ライブラリ（zlib・struct）だけで PNG を読み書きする
import os, struct, zlib

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = os.path.join(root, 'images')
out = os.path.join(root, 'res')
os.makedirs(out, exist_ok=True)

ROLES = {n: n for n in ['play', 'pause', 'prev', 'next', 'open', 'eq', 'visualizer', 'info', 'mute_off', 'mute_on', 'repeat_off', 'repeat_off_folder', 'repeat_all', 'repeat_one', 'fullscreen', 'cover']}

def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    p = 8; idat = b''; plte = None; trns = b''
    while p < len(d):
        n = struct.unpack('>I', d[p:p + 4])[0]; t = d[p + 4:p + 8]; b = d[p + 8:p + 8 + n]
        if t == b'IHDR': w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', b)
        elif t == b'PLTE': plte = [tuple(b[i:i + 3]) for i in range(0, n, 3)]
        elif t == b'tRNS': trns = b
        elif t == b'IDAT': idat += b
        p += 12 + n
    assert (w, h) == (16, 16) and inter == 0, (path, w, h, inter)
    raw = zlib.decompress(idat)
    bpp_bits = {2: 3 * depth, 6: 4 * depth, 3: depth, 0: depth}[ctype] if ctype in (2, 6, 3, 0) else None
    stride = (w * bpp_bits + 7) // 8
    bpx = max(1, bpp_bits // 8)
    rows = []; prev = bytearray(stride); q = 0
    for y in range(h):
        f = raw[q]; line = bytearray(raw[q + 1:q + 1 + stride]); q += 1 + stride
        for i in range(stride):
            a = line[i - bpx] if i >= bpx else 0; b_ = prev[i]; c = prev[i - bpx] if i >= bpx else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b_) & 255
            elif f == 3: line[i] = (line[i] + (a + b_) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b_ - c), abs(a - c), abs(a + b_ - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b_ if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        rows.append(line); prev = line
    px = []
    for line in rows:
        row = []
        for x in range(w):
            if ctype == 3:
                bit = x * depth; v = (line[bit // 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1)
                r, g, bl = plte[v]; a = trns[v] if v < len(trns) else 255
            elif ctype == 6: r, g, bl, a = line[x * 4:x * 4 + 4]
            elif ctype == 2: r, g, bl = line[x * 3:x * 3 + 3]; a = 255
            else: raise SystemExit('unsupported PNG type in ' + path)
            row.append((r, g, bl, a))
        px.append(row)
    return px

def key_out_background(px):
    # アルファの無い PNG（RGB）は、四隅の色とつながった同色の画素を透明にする（中の同じ色は残す）
    h, w = len(px), len(px[0])
    if any(p[3] != 255 for row in px for p in row): return px
    key = px[0][0]
    out = [list(r) for r in px]; seen = set(); stack = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h) or px[y][x] != key: continue
        seen.add((x, y)); out[y][x] = (key[0], key[1], key[2], 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return out

def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

def png(px):
    h, w = len(px), len(px[0])
    raw = b''.join(b'\x00' + b''.join(bytes(p) for p in row) for row in px)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def bmp_entry(px):
    h, w = len(px), len(px[0])
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    xor = b''.join(b''.join(bytes((p[2], p[1], p[0], p[3])) for p in row) for row in reversed(px))
    return hdr + xor + b'\x00' * (((w + 31) // 32) * 4 * h)

def scale(px, n):
    return [[p for p in row for _ in range(n)] for row in px for _ in range(n)]

# 古い生成物を消す
for f in os.listdir(out):
    if f.startswith('icon_') or f.startswith('app_') or f == 'app.ico':
        os.remove(os.path.join(out, f))

for role, name in ROLES.items():
    open(os.path.join(out, 'icon_%s.png' % role), 'wb').write(png(key_out_background(read_png(os.path.join(src, name + '.png')))))

base = key_out_background(read_png(os.path.join(src, 'icon.png')))
entries = []
for s in [16, 32, 48, 64, 256]:
    px = scale(base, s // 16)
    entries.append((s, png(px) if s >= 256 else bmp_entry(px)))
ico = struct.pack('<HHH', 0, 1, len(entries)); off = 6 + 16 * len(entries); body = b''
for s, d in entries:
    ico += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(d), off + len(body)); body += d
open(os.path.join(out, 'app.ico'), 'wb').write(ico + body)
print('icons:', ', '.join(ROLES), '| app.ico 16/32/48/64/256')
