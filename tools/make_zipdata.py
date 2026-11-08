# 第5c段（ZIP の中を直接読む）の確認用 ZIP を testdata/zip/ に作る。Kaname の曲は使わない。
# 中身は testdata/ にある確認用の音（tools/make_testdata.py・make_tagged.py・make_ogg.py で作ったもの）。
#   py tools\make_zipdata.py
# 先に testdata/formats, tagged, covers, ogg, gapless_same, _img があること（無ければ上のスクリプトで作る）。
import os, struct, zipfile, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TD = os.path.join(ROOT, 'testdata')
OUT = os.path.join(TD, 'zip')
os.makedirs(OUT, exist_ok=True)


def rd(*p):
    with open(os.path.join(TD, *p), 'rb') as f:
        return f.read()


def deflate_raw(b):
    c = zlib.compressobj(6, zlib.DEFLATED, -15)
    return c.compress(b) + c.flush()


# ---------------------------------------------------------------- 自前の ZIP 書き手（標準の zipfile では作れない形用）
# entries: dict(name=bytes（そのまま書く）, data=bytes（元の中身）, method=0|8, utf8=bool, enc=bool, z64=bool, raw=bytes|None（圧縮後を直接指定）)
def write_raw_zip(path, entries, z64_end=False, comment=b''):
    out = bytearray(); cds = []
    for e in entries:
        data = e['data']; method = e.get('method', 8)
        comp = e.get('raw')
        if comp is None:
            comp = data if method == 0 else deflate_raw(data)
        crc = zlib.crc32(data) & 0xFFFFFFFF
        flags = (0x800 if e.get('utf8') else 0) | (0x1 if e.get('enc') else 0)
        name = e['name']
        z64 = e.get('z64', False)
        off = len(out)
        if z64:
            extra = struct.pack('<HHQQ', 1, 16, len(data), len(comp))
            hdr = struct.pack('<IHHHHHIIIHH', 0x04034b50, 45, flags, method, 0, 0x21, crc, 0xFFFFFFFF, 0xFFFFFFFF, len(name), len(extra))
        else:
            extra = b''
            hdr = struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, flags, method, 0, 0x21, crc, len(comp), len(data), len(name), 0)
        out += hdr + name + extra + comp
        cds.append((e, flags, method, crc, len(comp), len(data), off, z64))
    cd_off = len(out)
    for e, flags, method, crc, cs, us, off, z64 in cds:
        name = e['name']
        if z64:
            extra = struct.pack('<HHQQQ', 1, 24, us, cs, off)
            rec = struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 45, 45, flags, method, 0, 0x21, crc, 0xFFFFFFFF, 0xFFFFFFFF, len(name), len(extra), 0, 0, 0, 0, 0xFFFFFFFF)
        else:
            extra = b''
            rec = struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 20, 20, flags, method, 0, 0x21, crc, cs, us, len(name), 0, 0, 0, 0, 0, off)
        out += rec + name + extra
    cd_size = len(out) - cd_off
    if z64_end:
        z64_pos = len(out)
        out += struct.pack('<IQHHIIQQQQ', 0x06064b50, 44, 45, 45, 0, 0, len(cds), len(cds), cd_size, cd_off)
        out += struct.pack('<IIQI', 0x07064b50, 0, z64_pos, 1)
        out += struct.pack('<IHHHHIIH', 0x06054b50, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFFFFFF, 0xFFFFFFFF, len(comment)) + comment
    else:
        out += struct.pack('<IHHHHIIH', 0x06054b50, 0, 0, len(cds), len(cds), cd_size, cd_off, len(comment)) + comment
    with open(path, 'wb') as f:
        f.write(out)


def zf_write(path, items):
    """標準の zipfile で書く。items: (name, bytes, ZIP_STORED|ZIP_DEFLATED)"""
    with zipfile.ZipFile(path, 'w') as z:
        for name, data, comp in items:
            z.writestr(zipfile.ZipInfo(name, (2026, 11, 8, 12, 0, 0)), data, compress_type=comp)


S, D = zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED

# 1) main.zip：Stored と Deflate、サブフォルダあり。曲のある一番浅いフォルダは Hits/（根には曲が無い。Deep/Sub は2段目）。
#    Hits/ には 6 形式と cover.jpg、Deep/Sub/ にも曲、__MACOSX 風のごみもある
zf_write(os.path.join(OUT, 'main.zip'), [
    ('readme.txt', b'hello\r\n', S),
    ('Deep/Sub/deep_a.wav', rd('formats', 'f5_wav.wav'), D),
    ('Deep/Sub/deep_b.mp3', rd('formats', 'f1_mp3.mp3'), D),
    ('Hits/', b'', S),
    ('Hits/01_wav_stored.wav', rd('formats', 'f5_wav.wav'), S),
    ('Hits/02_flac_deflate.flac', rd('formats', 'f4_flac.flac'), D),
    ('Hits/03_mp3.mp3', rd('formats', 'f1_mp3.mp3'), D),
    ('Hits/04_aac.m4a', rd('formats', 'f2_aac.m4a'), S),
    ('Hits/05_vorbis.ogg', rd('ogg', 'o1_vorbis.ogg'), D),
    ('Hits/06_opus.opus', rd('ogg', 'o3.opus'), S),
    ('Hits/07_alac.m4a', rd('formats', 'f3_alac.m4a'), D),
    ('Hits/cover.jpg', rd('_img', 'blue.jpg'), S),
    ('Hits/notes.txt', b'not audio', D),
])

# 2) tags.zip：タグと埋め込みジャケット（MP3・FLAC・M4A）。同じフォルダの cover.jpg は無い
zf_write(os.path.join(OUT, 'tags.zip'), [
    ('Album/c1_mp3.mp3', rd('covers', 'c1_mp3.mp3'), D),
    ('Album/c2_flac.flac', rd('covers', 'c2_flac.flac'), D),
    ('Album/c3_aac.m4a', rd('covers', 'c3_aac.m4a'), S),
    ('Album/b_first.mp3', rd('tagged', 'b_first.mp3'), D),
    ('Album/c_second.mp3', rd('tagged', 'c_second.mp3'), D),
    ('Album/a_third.mp3', rd('tagged', 'a_third.mp3'), D),
])

# 3) gapless.zip：位相のつながった 440Hz の 44.1k wav 3 本（3 秒ずつ）。継ぎ目の確認用
zf_write(os.path.join(OUT, 'gapless.zip'), [
    ('g1.wav', rd('gapless_same', 'g1.wav'), D),
    ('g2.wav', rd('gapless_same', 'g2.wav'), S),
    ('g3.wav', rd('gapless_same', 'g3.wav'), D),
])

# 4) 日本語名：UTF-8（汎用フラグ bit 11 あり）と CP932（フラグなし）
JP_DIR = '効果音'
JP_FILES = ['ヒット音01.wav', '爆発02.wav', 'ｱｲｳ_半角カナ03.wav']
jp_data = [rd('formats', 'f5_wav.wav'), rd('gapless_same', 'g1.wav'), rd('formats', 'f5_wav.wav')]
zf_write(os.path.join(OUT, 'jp_utf8.zip'), [(JP_DIR + '/' + n, d, D) for n, d in zip(JP_FILES, jp_data)])
write_raw_zip(os.path.join(OUT, 'jp_cp932.zip'),
              [dict(name=(JP_DIR + '/' + n).encode('cp932'), data=d, method=8, utf8=False) for n, d in zip(JP_FILES, jp_data)])

# 5) パスワード付き風（暗号化の旗を立てたもの：中身は ZIP の暗号ではなく、ただの別のバイト列）と、ふつうの曲
write_raw_zip(os.path.join(OUT, 'password.zip'), [
    dict(name=b'a_locked.wav', data=rd('formats', 'f5_wav.wav'), method=8, enc=True, raw=os.urandom(5000)),
    dict(name=b'b_ok.wav', data=rd('formats', 'f5_wav.wav'), method=8),
    dict(name=b'c_locked.mp3', data=rd('formats', 'f1_mp3.mp3'), method=0, enc=True, raw=os.urandom(2000)),
])

# 6) 壊れた ZIP：(a) 目次の消えた（途中で切れた）もの、(b) ZIP ではないバイト列、(c) 曲のデータが壊れていて CRC が合わないもの
good = open(os.path.join(OUT, 'main.zip'), 'rb').read()
with open(os.path.join(OUT, 'broken_truncated.zip'), 'wb') as f:
    f.write(good[:len(good) // 2])
with open(os.path.join(OUT, 'broken_garbage.zip'), 'wb') as f:
    f.write(os.urandom(4000))
wav = rd('formats', 'f5_wav.wav')
bad = bytearray(deflate_raw(wav))
bad[len(bad) // 2] ^= 0xFF   # 中を1バイト壊す（Deflate が通っても CRC が合わない／途中で壊れる）
write_raw_zip(os.path.join(OUT, 'crc_bad.zip'), [
    dict(name=b'a_corrupt.wav', data=wav, method=8, raw=bytes(bad)),
    dict(name=b'b_good.wav', data=wav, method=8),
])

# 7) ZIP64：ローカル・目次・終端のすべてを ZIP64 の形で書いたもの
write_raw_zip(os.path.join(OUT, 'zip64.zip'), [
    dict(name=b'Z64/one.wav', data=rd('formats', 'f5_wav.wav'), method=8, z64=True),
    dict(name=b'Z64/two.mp3', data=rd('formats', 'f1_mp3.mp3'), method=0, z64=True),
], z64_end=True)

# 8) 曲が1つも無い ZIP（No audio files）
zf_write(os.path.join(OUT, 'noaudio.zip'), [('a.txt', b'x', D), ('dir/b.txt', b'y', D)])

# 9) 同じ深さの 2 フォルダ（自然順で先のほう＝ B2 より B10 でなく B2 が先）。ZIP の中の ZIP は出さない
inner = os.path.join(OUT, '_inner.zip')
zf_write(inner, [('x.wav', rd('formats', 'f5_wav.wav'), D)])
zf_write(os.path.join(OUT, 'depth.zip'), [
    ('B10/ten.wav', rd('formats', 'f5_wav.wav'), D),
    ('B2/two.wav', rd('formats', 'f5_wav.wav'), D),
    ('nested.zip', open(inner, 'rb').read(), S),
])
os.remove(inner)

# 10) 実フォルダの中に ZIP を置いたもの（エクスプローラで「フォルダ → ZIP → 音のファイル」の順になるか）
os.makedirs(os.path.join(OUT, 'tree', 'sub_folder'), exist_ok=True)
import shutil
shutil.copy(os.path.join(OUT, 'main.zip'), os.path.join(OUT, 'tree', 'pack.zip'))
shutil.copy(os.path.join(TD, 'formats', 'f5_wav.wav'), os.path.join(OUT, 'tree', 'loose.wav'))
shutil.copy(os.path.join(TD, 'formats', 'f1_mp3.mp3'), os.path.join(OUT, 'tree', 'sub_folder', 'inner.mp3'))

print('done:', sorted(os.listdir(OUT)))
