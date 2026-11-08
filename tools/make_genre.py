# 第5b段のテスト用：ジャンルのタグ（MP3 TCON・FLAC/Ogg GENRE・M4A ©gen/gnre・WAV IGNR）と、列・並べ替えの確認用の曲を testdata/ に作る。
#   py tools\make_genre.py   （先に tools\bin\encode.exe を用意。Ogg だけ ffmpeg を使う：tools\make_ogg.py と同じ）
# 入力は tools/ で作る試験音だけ、出力は testdata/genre と testdata/columns だけ。Kaname の曲は使わない。
import math, os, struct, subprocess, shutil, wave
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'testdata'); TMP = os.path.join(OUT, '_tmp5b')
ENC = os.path.join(ROOT, 'tools', 'bin', 'encode.exe')
FFMPEG = r'C:\Program Files\Derivative\TouchDesigner\bin\ffmpeg.exe'
os.makedirs(TMP, exist_ok=True)
def mk(p): os.makedirs(os.path.dirname(p), exist_ok=True); return p

def sine_wav(path, hz, sec, rate=44100, amp=0.4):
    w = wave.open(mk(path), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    buf = bytearray()
    for i in range(int(round(sec * rate))):
        v = int(round(32767 * amp * math.sin(2 * math.pi * hz * i / rate))); buf += struct.pack('<hh', v, v)
    w.writeframes(bytes(buf)); w.close()
def encode(wav, out, kind):
    if os.path.exists(out): os.remove(out)
    mk(out); r = subprocess.run([ENC, wav, out, kind], capture_output=True, text=True)
    assert r.returncode == 0 and os.path.exists(out), (out, r.stderr)

# ---- ID3v2.3
def syncsafe(n): return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])
def frame(fid, data): return fid.encode() + struct.pack('>I', len(data)) + b'\0\0' + data
def text_frame(fid, s): return frame(fid, b'\x03' + s.encode('utf-8'))
def tag_mp3(path, frames):
    d = open(path, 'rb').read()
    if d[:3] == b'ID3': n = 10 + ((d[6] & 0x7F) << 21 | (d[7] & 0x7F) << 14 | (d[8] & 0x7F) << 7 | (d[9] & 0x7F)); d = d[n:]
    body = b''.join(frames); open(path, 'wb').write(b'ID3\x03\x00\x00' + syncsafe(len(body)) + body + d)
def make_mp3(path, hz, sec, frames):
    w = os.path.join(TMP, 'x.wav'); sine_wav(w, hz, sec); encode(w, path, 'mp3'); tag_mp3(path, frames)

# ---- FLAC / MP4 / WAV
def flac_tag(path, comments):
    d = open(path, 'rb').read(); assert d[:4] == b'fLaC'
    pos = 4; blocks = []
    while True:
        h = d[pos]; last = h & 0x80; t = h & 0x7F; ln = d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3]
        blocks.append((t, d[pos + 4:pos + 4 + ln])); pos += 4 + ln
        if last: break
    audio = d[pos:]; blocks = [b for b in blocks if b[0] not in (4, 6)]
    vend = b'tlap-test'
    vc = struct.pack('<I', len(vend)) + vend + struct.pack('<I', len(comments)) + b''.join(struct.pack('<I', len(c.encode())) + c.encode() for c in comments)
    blocks.insert(1, (4, vc)); out = b'fLaC'
    for i, (t, b) in enumerate(blocks): out += bytes([(0x80 if i == len(blocks) - 1 else 0) | t]) + struct.pack('>I', len(b))[1:] + b
    open(path, 'wb').write(out + audio)
def box(t, payload): return struct.pack('>I4s', 8 + len(payload), t) + payload
def parse(d):
    out = []; p = 0
    while p + 8 <= len(d):
        sz, t = struct.unpack('>I4s', d[p:p + 8]); out.append((t, d[p + 8:p + sz])); p += sz
    return out
def mp4_tag(path, items):
    d = open(path, 'rb').read(); top = parse(d); ilst = box(b'ilst', b''.join(items))
    def fix(t, payload):
        if t == b'moov': return box(t, b''.join(fix(a, b) for a, b in parse(payload)))
        if t == b'udta': return box(t, b''.join(fix(a, b) for a, b in parse(payload)))
        if t == b'meta':
            head = payload[:4]; kids = [(a, b) for a, b in parse(payload[4:]) if a != b'ilst']
            return box(t, head + b''.join(box(a, b) for a, b in kids) + ilst)
        return box(t, payload)
    open(path, 'wb').write(b''.join(fix(t, p) for t, p in top))
def ilst_text(name, s): return box(name, box(b'data', struct.pack('>II', 1, 0) + s.encode('utf-8')))
def ilst_gnre(n): return box(b'gnre', box(b'data', struct.pack('>II', 0, 0) + struct.pack('>H', n)))   # 1 始まり：ID3v1 の番号 + 1
def wav_tag(path, info):
    d = bytearray(open(path, 'rb').read()); body = b'INFO'
    for k, v in info.items():
        b = v.encode('utf-8') + b'\0'; body += k.encode() + struct.pack('<I', len(b)) + b + (b'\0' if len(b) & 1 else b'')
    d += b'LIST' + struct.pack('<I', len(body)) + body; d[4:8] = struct.pack('<I', len(d) - 8); open(path, 'wb').write(bytes(d))

# ================= genre/ : 形式ごとのジャンル
G = os.path.join(OUT, 'genre')
make_mp3(os.path.join(G, 'g1_mp3_num.mp3'), 330, 1.5, [text_frame('TIT2', 'TCON (17) -> Rock'), text_frame('TCON', '(17)')])
make_mp3(os.path.join(G, 'g2_mp3_numtext.mp3'), 350, 1.5, [text_frame('TIT2', 'TCON (13)Synth -> Synth'), text_frame('TCON', '(13)Synth')])
make_mp3(os.path.join(G, 'g3_mp3_text.mp3'), 370, 1.5, [text_frame('TIT2', 'TCON Ambient'), text_frame('TCON', 'Ambient')])
make_mp3(os.path.join(G, 'g3b_mp3_bare.mp3'), 390, 1.5, [text_frame('TIT2', 'TCON 8 -> Jazz'), text_frame('TCON', '8')])
w = os.path.join(TMP, 'f.wav'); sine_wav(w, 410, 1.5); fp = os.path.join(G, 'g4_flac.flac'); encode(w, fp, 'flac')
flac_tag(fp, ['TITLE=FLAC GENRE Electronic', 'GENRE=Electronic'])
w = os.path.join(TMP, 'a.wav'); sine_wav(w, 430, 1.5); mp = os.path.join(G, 'g5_m4a_gen.m4a'); encode(w, mp, 'aac')
mp4_tag(mp, [ilst_text(b'\xa9nam', 'M4A (c)gen Folk'), ilst_text(b'\xa9gen', 'Folk')])
mp = os.path.join(G, 'g6_m4a_gnre.m4a'); encode(w, mp, 'aac')
mp4_tag(mp, [ilst_text(b'\xa9nam', 'M4A gnre 18 -> Rock'), ilst_gnre(18)])
sine_wav(os.path.join(G, 'g7_wav.wav'), 450, 1.5); wav_tag(os.path.join(G, 'g7_wav.wav'), {'INAM': 'WAV IGNR Soundtrack', 'IGNR': 'Soundtrack'})
if os.path.exists(FFMPEG):
    w = os.path.join(TMP, 'o.wav'); sine_wav(w, 470, 1.5); op = os.path.join(G, 'g8_ogg.ogg')
    r = subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-y', '-i', w, '-c:a', 'libvorbis', '-q:a', '6', '-metadata', 'title=Ogg GENRE Classical', '-metadata', 'genre=Classical', op], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
# WMA はテスト用の作り方が無い（Shell のプロパティ読みだけ。コードは PKEY_Music_Genre を読む）

# ================= columns/ : 列・並べ替えの確認用（トラック番号が全曲には無い → # はファイル名の順）
C = os.path.join(OUT, 'columns')
rows = [  # file, hz, sec, title, artist, album, year, genre
    ('01_delta.mp3', 300, 1.0, 'Delta', 'Zebra Band', 'Alpha Album', '1999', 'Rock'),
    ('02_alpha.mp3', 340, 3.0, 'alpha', 'Yankee', 'Zulu Album', '2005', 'Jazz'),
    ('03_charlie.mp3', 380, 2.0, 'Charlie', '', '', '', ''),
    ('04_bravo.mp3', 420, 4.0, 'Bravo', 'Xray', 'Alpha Album', '2012', 'Pop'),
    ('05_echo.mp3', 460, 1.5, 'Echo', 'Walter', 'Mike Album', '1987', 'Ambient'),
    ('10_foxtrot.mp3', 500, 2.5, 'Foxtrot', 'Victor', 'Mike Album', '2020', 'Folk'),
]
for fn, hz, sec, title, artist, album, year, genre in rows:
    fr = [text_frame('TIT2', title)]
    for fid, v in (('TPE1', artist), ('TALB', album), ('TYER', year), ('TCON', genre)):
        if v: fr.append(text_frame(fid, v))
    make_mp3(os.path.join(C, fn), hz, sec, fr)

for f in os.listdir(TMP): os.remove(os.path.join(TMP, f))
os.rmdir(TMP)
print('genre / columns test data done')
