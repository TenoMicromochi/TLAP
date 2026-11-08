# 第2段のテスト用：タグ・ジャケット・並び順・ギャップレス用の音を testdata/ に作る（make_testdata.py の後に実行される）。
#   py tools\make_tagged.py   （先に tools\make_covers.ps1 でジャケット画像、tools\bin\encode.exe を用意）
import math, os, struct, subprocess, sys, wave
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'testdata'); IMG = os.path.join(OUT, '_img'); TMP = os.path.join(OUT, '_tmp2')
ENC = os.path.join(ROOT, 'tools', 'bin', 'encode.exe')
os.makedirs(TMP, exist_ok=True)

def mk(p): os.makedirs(os.path.dirname(p), exist_ok=True); return p
def img(n): return open(os.path.join(IMG, n), 'rb').read()

def sine_wav(path, hz, sec, rate, t_start=0.0, amp=0.5, ch=2):
    """位相がつながる正弦波（t_start 秒目から）。端のフェードなし"""
    w = wave.open(mk(path), 'wb'); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(rate)
    n = int(round(sec * rate)); buf = bytearray()
    for i in range(n):
        v = int(round(32767 * amp * math.sin(2 * math.pi * hz * (t_start + i / rate))))
        buf += struct.pack('<hh', v, v) if ch == 2 else struct.pack('<h', v)
    w.writeframes(bytes(buf)); w.close()

def encode(wav, out, kind):
    if os.path.exists(out): os.remove(out)
    mk(out); r = subprocess.run([ENC, wav, out, kind], capture_output=True, text=True)
    assert r.returncode == 0 and os.path.exists(out), (out, r.stderr)

# ---------------- ID3v2.3
def syncsafe(n): return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])
def frame(fid, data): return fid.encode() + struct.pack('>I', len(data)) + b'\0\0' + data
def text_frame(fid, s, enc=0, raw=None):
    if raw is not None: return frame(fid, bytes([enc]) + raw)
    if enc == 1: return frame(fid, b'\x01' + b'\xff\xfe' + s.encode('utf-16-le'))
    return frame(fid, bytes([enc]) + s.encode('latin1' if enc == 0 else 'utf-8'))
def apic(data, mime='image/jpeg'): return frame('APIC', b'\x00' + mime.encode() + b'\0' + b'\x03' + b'\0' + data)
def id3_tag(frames):
    body = b''.join(frames); return b'ID3\x03\x00\x00' + syncsafe(len(body)) + body
def tag_mp3(path, frames):
    d = open(path, 'rb').read()
    if d[:3] == b'ID3': n = 10 + ((d[6] & 0x7F) << 21 | (d[7] & 0x7F) << 14 | (d[8] & 0x7F) << 7 | (d[9] & 0x7F)); d = d[n:]
    open(path, 'wb').write(id3_tag(frames) + d)

def make_mp3(path, hz, sec, frames):
    w = os.path.join(TMP, 'x.wav'); sine_wav(w, hz, sec, 44100); encode(w, path, 'mp3'); tag_mp3(path, frames)

# ---------------- FLAC（VORBIS_COMMENT と PICTURE を差し込む）
def flac_tag(path, comments, picture=None, mime=b'image/png'):
    d = open(path, 'rb').read(); assert d[:4] == b'fLaC'
    pos = 4; blocks = []
    while True:
        h = d[pos]; last = h & 0x80; t = h & 0x7F; ln = d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3]
        blocks.append((t, d[pos + 4:pos + 4 + ln])); pos += 4 + ln
        if last: break
    audio = d[pos:]
    blocks = [b for b in blocks if b[0] not in (4, 6)]
    vend = b'tlap-test'
    vc = struct.pack('<I', len(vend)) + vend + struct.pack('<I', len(comments)) + b''.join(struct.pack('<I', len(c.encode())) + c.encode() for c in comments)
    blocks.insert(1, (4, vc))
    if picture:
        pic = struct.pack('>II', 3, len(mime)) + mime + struct.pack('>I', 0) + struct.pack('>IIIII', 0, 0, 24, 0, len(picture)) + picture
        blocks.insert(2, (6, pic))
    out = b'fLaC'
    for i, (t, b) in enumerate(blocks):
        out += bytes([(0x80 if i == len(blocks) - 1 else 0) | t]) + struct.pack('>I', len(b))[1:] + b
    open(path, 'wb').write(out + audio)

# ---------------- MP4（ilst を差し替える）
def box(t, payload): return struct.pack('>I4s', 8 + len(payload), t) + payload
def parse(d):
    out = []; p = 0
    while p + 8 <= len(d):
        sz, t = struct.unpack('>I4s', d[p:p + 8]); out.append((t, d[p + 8:p + sz])); p += sz
    return out
def mp4_tag(path, items):
    d = open(path, 'rb').read(); top = parse(d)
    ilst = box(b'ilst', b''.join(items))
    def fix(t, payload):
        if t == b'moov': return box(t, b''.join(fix(a, b) for a, b in parse(payload)))
        if t == b'udta':
            kids = [fix(a, b) for a, b in parse(payload)]; return box(t, b''.join(kids))
        if t == b'meta':
            head = payload[:4]; kids = [(a, b) for a, b in parse(payload[4:]) if a != b'ilst']
            return box(t, head + b''.join(box(a, b) for a, b in kids) + ilst)
        return box(t, payload)
    open(path, 'wb').write(b''.join(fix(t, p) for t, p in top))
def ilst_text(name, s): return box(name, box(b'data', struct.pack('>II', 1, 0) + s.encode('utf-8')))
def ilst_trkn(n, total): return box(b'trkn', box(b'data', struct.pack('>II', 0, 0) + struct.pack('>HHHH', 0, n, total, 0)))
def ilst_covr(data, fl): return box(b'covr', box(b'data', struct.pack('>II', fl, 0) + data))

# ---------------- WAV LIST INFO
def wav_tag(path, info):
    d = bytearray(open(path, 'rb').read()); body = b'INFO'
    for k, v in info.items():
        b = v.encode('utf-8') + b'\0'; body += k.encode() + struct.pack('<I', len(b)) + b + (b'\0' if len(b) & 1 else b'')
    d += b'LIST' + struct.pack('<I', len(body)) + body
    d[4:8] = struct.pack('<I', len(d) - 8); open(path, 'wb').write(bytes(d))

# ================= tagged/ : 全曲にトラック番号（ファイル名の順と逆）
T = os.path.join(OUT, 'tagged')
for fn, hz, trk, title, artist in [('a_third.mp3', 300, 3, 'Third Song', 'Test Artist'), ('b_first.mp3', 400, 1, 'First Song', 'Test Artist'), ('c_second.mp3', 500, 2, 'Second Song', 'Test Artist')]:
    make_mp3(os.path.join(T, fn), hz, 2.0, [text_frame('TIT2', title), text_frame('TPE1', artist), text_frame('TALB', 'Test Album'),
        text_frame('TPE2', 'Album Artist'), text_frame('TRCK', '%d/3' % trk), text_frame('TPOS', '1/1'), text_frame('TYER', '2026'), apic(img('red.jpg'))])
# ================= discs/ : ディスク番号つき（disc2 の曲がファイル名では先頭）
D = os.path.join(OUT, 'discs')
for fn, hz, disc, trk in [('a_d2t1.mp3', 300, 2, 1), ('b_d1t2.mp3', 400, 1, 2), ('c_d1t1.mp3', 500, 1, 1)]:
    make_mp3(os.path.join(D, fn), hz, 1.5, [text_frame('TIT2', 'D%dT%d' % (disc, trk)), text_frame('TRCK', str(trk)), text_frame('TPOS', '%d/2' % disc)])
# ================= partial/ : 1 曲だけトラック番号が無い → ファイル名順（自然順）
P = os.path.join(OUT, 'partial')
for fn, hz, trk, title in [('a_x.mp3', 300, 2, 'AX two'), ('b_y.mp3', 400, 0, 'BY none'), ('c_z.mp3', 500, 1, 'CZ one')]:
    fr = [text_frame('TIT2', title)] + ([text_frame('TRCK', str(trk))] if trk else [])
    make_mp3(os.path.join(P, fn), hz, 1.5, fr)
# ================= sjis/ : Shift_JIS（Latin-1 と書かれた ID3）と UTF-16 の日本語
S = os.path.join(OUT, 'sjis')
make_mp3(os.path.join(S, 's1_sjis.mp3'), 440, 2.0, [text_frame('TIT2', '', 0, raw='テスト曲'.encode('cp932')), text_frame('TPE1', '', 0, raw='てのくん'.encode('cp932')), text_frame('TALB', '', 0, raw='日本語アルバム'.encode('cp932')), text_frame('TRCK', '1')])
make_mp3(os.path.join(S, 's2_utf16.mp3'), 523, 2.0, [text_frame('TIT2', 'ユニコードの曲', 1), text_frame('TPE1', 'アーティスト', 1), text_frame('TRCK', '2')])
# ================= covers/ : 形式ごとの埋め込みジャケット（MP3 は jpg、FLAC は png、M4A は jpg）。ほかは単色
C = os.path.join(OUT, 'covers')
make_mp3(os.path.join(C, 'c1_mp3.mp3'), 440, 2.0, [text_frame('TIT2', 'MP3 cover (red jpg)'), text_frame('TRCK', '1'), apic(img('red.jpg'))])
w = os.path.join(TMP, 'f.wav'); sine_wav(w, 523, 2.0, 44100); fp = os.path.join(C, 'c2_flac.flac'); encode(w, fp, 'flac')
flac_tag(fp, ['TITLE=FLAC cover (green png)', 'ARTIST=Flac Artist', 'ALBUM=Flac Album', 'TRACKNUMBER=2', 'DATE=2025'], img('green.png'))
w = os.path.join(TMP, 'a.wav'); sine_wav(w, 659, 2.0, 44100); mp = os.path.join(C, 'c3_aac.m4a'); encode(w, mp, 'aac')
mp4_tag(mp, [ilst_text(b'\xa9nam', 'M4A cover (blue jpg)'), ilst_text(b'\xa9ART', 'M4A Artist'), ilst_text(b'\xa9alb', 'M4A Album'), ilst_trkn(3, 5), ilst_covr(img('blue.jpg'), 13)])
sine_wav(os.path.join(C, 'c4_wav.wav'), 784, 2.0, 44100); wav_tag(os.path.join(C, 'c4_wav.wav'), {'INAM': 'WAV info title', 'IART': 'Wav Artist', 'IPRD': 'Wav Album', 'ITRK': '4'})
make_mp3(os.path.join(C, 'c5_big.mp3'), 880, 2.0, [text_frame('TIT2', 'MP3 big cover (2400px png)'), text_frame('TRCK', '5'), apic(img('big.png'), 'image/png')])
# ================= foldercover/ : 埋め込み無し + cover.jpg（小さい画像は整数倍）、nocover/ : どちらも無し
F = os.path.join(OUT, 'foldercover'); sine_wav(os.path.join(F, 'f1.wav'), 440, 3.0, 44100); sine_wav(os.path.join(F, 'f2.wav'), 520, 3.0, 44100)
open(mk(os.path.join(F, 'Cover.JPG')), 'wb').write(img('small.jpg'))
N = os.path.join(OUT, 'nocover'); sine_wav(os.path.join(N, 'n1.wav'), 440, 3.0, 44100)
# ================= gapless_same/ : 44.1k → 44.1k → 44.1k。位相のつながった 440Hz（3 秒ずつ）
G = os.path.join(OUT, 'gapless_same')
for i in range(3): sine_wav(os.path.join(G, 'g%d.wav' % (i + 1)), 440, 3.0, 44100, t_start=3.0 * i)
# ================= gapless_mixed/ : 44.1k → 96k → 44.1k（同じく位相がつながる 440Hz）
M = os.path.join(OUT, 'gapless_mixed')
sine_wav(os.path.join(M, 'm1_44k.wav'), 440, 3.0, 44100, t_start=0.0)
sine_wav(os.path.join(M, 'm2_96k.wav'), 440, 3.0, 96000, t_start=3.0)
sine_wav(os.path.join(M, 'm3_44k.wav'), 440, 3.0, 44100, t_start=6.0)
for f in os.listdir(TMP): os.remove(os.path.join(TMP, f))
os.rmdir(TMP)
print('tagged test data done')
