# Test audio for Ogg Vorbis / Opus (stage 2). Run after make_testdata.py / make_covers.ps1:  py tools\make_ogg.py
# Encoder: the already-installed ffmpeg (libvorbis / libopus) - input is only the test tones made by tools/, output only testdata/.
import math, os, struct, subprocess, sys, wave
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'testdata'); IMG = os.path.join(OUT, '_img'); TMP = os.path.join(OUT, '_tmp3')
FFMPEG = r'C:\Program Files\Derivative\TouchDesigner\bin\ffmpeg.exe'
os.makedirs(TMP, exist_ok=True)

def sine(path, hz, sec, rate, t_start=0.0, amp=0.5):
    w = wave.open(path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    n = int(round(sec * rate)); buf = bytearray()
    for i in range(n):
        v = int(round(32767 * amp * math.sin(2 * math.pi * hz * (t_start + i / rate)))); buf += struct.pack('<hh', v, v)
    w.writeframes(bytes(buf)); w.close()

# ---- METADATA_BLOCK_PICTURE を Ogg の comment パケットに足す（ffmpeg の ogg は画像を直接入れられないので、ページを組み直す）
import base64, zlib
def _crc_table():
    t = []
    for i in range(256):
        r = i << 24
        for _ in range(8): r = ((r << 1) ^ 0x04c11db7) & 0xffffffff if r & 0x80000000 else (r << 1) & 0xffffffff
        t.append(r)
    return t
_CRC = _crc_table()
def ogg_crc(b):
    c = 0
    for x in b: c = ((c << 8) & 0xffffffff) ^ _CRC[((c >> 24) & 0xff) ^ x]
    return c
def make_page(flags, gran, serial, seq, segs, payload):
    h = b'OggS' + bytes([0, flags]) + struct.pack('<qIII', gran, serial, seq, 0) + bytes([len(segs)]) + bytes(segs)
    p = bytearray(h + payload); struct.pack_into('<I', p, 22, ogg_crc(bytes(p))); return bytes(p)
def packet_pages(pkt, serial, seq, flags0=0):
    segs = [255] * (len(pkt) // 255) + [len(pkt) % 255]; pages = []; pos = 0; first = True
    while segs:
        chunk, segs = segs[:255], segs[255:]; n = sum(chunk); last = not segs
        pages.append(make_page((flags0 if first else 1), 0 if last else -1, serial, seq + len(pages), chunk, pkt[pos:pos + n])); pos += n; first = False
    return pages
def add_picture(path, img, mime, ptype=3):
    d = open(path, 'rb').read(); pages = []; p = 0
    while p < len(d):
        nseg = d[p + 26]; segs = list(d[p + 27:p + 27 + nseg]); ln = sum(segs)
        pages.append((d[p + 5], struct.unpack_from('<q', d, p + 6)[0], struct.unpack_from('<I', d, p + 14)[0], segs, d[p + 27 + nseg:p + 27 + nseg + ln])); p += 27 + nseg + ln
    serial = pages[0][2]; vorbis = pages[0][4].startswith(b'vorbis'); need = 3 if vorbis else 2
    pk = []; cur = b''; used = 0
    for fl, g, s, segs, pay in pages:
        o = 0
        for sg in segs:
            cur += pay[o:o + sg]; o += sg
            if sg < 255: pk.append(cur); cur = b''
        used += 1
        if len(pk) >= need: break
    ident, comm = pk[0], pk[1]; setup = pk[2] if vorbis else None
    pre = b'vorbis' if vorbis else b'OpusTags'
    assert comm.startswith(pre); q = len(pre); vl = struct.unpack_from('<I', comm, q)[0]; vend = comm[q + 4:q + 4 + vl]; q += 4 + vl
    cnt = struct.unpack_from('<I', comm, q)[0]; q += 4; items = []
    for _ in range(cnt): l = struct.unpack_from('<I', comm, q)[0]; items.append(comm[q + 4:q + 4 + l]); q += 4 + l
    blk = struct.pack('>II', ptype, len(mime)) + mime + struct.pack('>I', 0) + struct.pack('>IIIII', 0, 0, 24, 0, len(img)) + img
    items.append(b'METADATA_BLOCK_PICTURE=' + base64.b64encode(blk))
    ncomm = pre + struct.pack('<I', len(vend)) + vend + struct.pack('<I', len(items)) + b''.join(struct.pack('<I', len(i)) + i for i in items) + (b'' if vorbis else b'')
    out = packet_pages(ident, serial, 0, 2); seq = len(out)
    cp = packet_pages(ncomm, serial, seq); out += cp; seq += len(cp)
    if setup: sp = packet_pages(setup, serial, seq); out += sp; seq += len(sp)
    # 残りのページ（音）：順番号と CRC を振り直す
    for fl, g, s, segs, pay in pages[used:]:
        out.append(make_page(fl, g, s, seq, segs, pay)); seq += 1
    open(path, 'wb').write(b''.join(out))

def enc(wav, out, codec, extra=(), cover=None):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    if os.path.exists(out): os.remove(out)
    cmd = [FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-i', wav]
    cmd += ['-c:a', codec] + list(extra) + [out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0 and os.path.exists(out), (out, r.stderr)
    if cover: add_picture(out, open(cover, 'rb').read(), b'image/png' if cover.endswith('png') else b'image/jpeg')
    print('  ok', os.path.relpath(out, OUT), os.path.getsize(out))

def md(**kw): return sum([['-metadata', '%s=%s' % (k, v)] for k, v in kw.items()], [])

# ---- ogg/ : Vorbis 44.1k (with comment + cover), Opus .ogg (with comment + cover), .opus, Vorbis 96k
O = os.path.join(OUT, 'ogg')
w = os.path.join(TMP, 'a.wav'); sine(w, 523, 2.0, 44100)
enc(w, os.path.join(O, 'o1_vorbis.ogg'), 'libvorbis', ['-q:a', '6'] + md(title='Vorbis title (green png)', artist='Ogg Artist', album='Ogg Album', track='1', date='2024'), os.path.join(IMG, 'green.png'))
w = os.path.join(TMP, 'b.wav'); sine(w, 659, 2.0, 48000)
enc(w, os.path.join(O, 'o2_opus.ogg'), 'libopus', ['-b:a', '128k'] + md(title='Opus in .ogg (blue jpg)', artist='Opus Artist', album='Opus Album', track='2'), os.path.join(IMG, 'blue.jpg'))
w = os.path.join(TMP, 'c.wav'); sine(w, 784, 2.0, 48000)
enc(w, os.path.join(O, 'o3.opus'), 'libopus', ['-b:a', '128k'] + md(title='Opus .opus (red jpg)', artist='Opus Artist', album='Opus Album', track='3'), os.path.join(IMG, 'red.jpg'))
w = os.path.join(TMP, 'd.wav'); sine(w, 1000, 2.0, 96000)
enc(w, os.path.join(O, 'o4_vorbis96k.ogg'), 'libvorbis', ['-q:a', '6'] + md(title='Vorbis 96k, no cover', track='4'))
w = os.path.join(TMP, 'e.wav'); sine(w, 880, 2.0, 44100)
enc(w, os.path.join(O, 'o5_vorbis_notag.ogg'), 'libvorbis', ['-q:a', '6'])

# ---- gapless_ogg/ : Vorbis 44.1k -> Opus 48k -> Vorbis 44.1k (phase-continuous 440 Hz, 3 s each)
G = os.path.join(OUT, 'gapless_ogg')
w = os.path.join(TMP, 'g1.wav'); sine(w, 440, 3.0, 44100, 0.0); enc(w, os.path.join(G, 'g1_vorbis.ogg'), 'libvorbis', ['-q:a', '10'])
w = os.path.join(TMP, 'g2.wav'); sine(w, 440, 3.0, 48000, 3.0); enc(w, os.path.join(G, 'g2_opus.opus'), 'libopus', ['-b:a', '256k'])
w = os.path.join(TMP, 'g3.wav'); sine(w, 440, 3.0, 44100, 6.0); enc(w, os.path.join(G, 'g3_vorbis.ogg'), 'libvorbis', ['-q:a', '10'])
# same set but lossless references to compare noise floor: Vorbis -> Vorbis only
G2 = os.path.join(OUT, 'gapless_vorbis')
for i in range(3):
    w = os.path.join(TMP, 'v%d.wav' % i); sine(w, 440, 3.0, 44100, 3.0 * i); enc(w, os.path.join(G2, 'v%d.ogg' % (i + 1)), 'libvorbis', ['-q:a', '10'])

for f in os.listdir(TMP): os.remove(os.path.join(TMP, f))
os.rmdir(TMP)
print('ogg test data done')
