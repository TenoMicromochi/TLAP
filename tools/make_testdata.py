# テスト用の音を testdata/ に作る（Kaname の曲は使わない）。
#   py tools\make_testdata.py
# サイン波の WAV を Python で作り、mp3 / m4a(AAC・ALAC) / flac / wma は tools\bin\encode.exe（Media Foundation の変換）で作る。
# encode.exe は tools\build_tools.bat で作る。作れなかった形式は最後に一覧で出す。
import math, os, struct, subprocess, sys, wave, random
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from alac_writer import write_alac

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'testdata')
ENC = os.path.join(ROOT, 'tools', 'bin', 'encode.exe')
failed = []

def mk(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return path

def write_wav(path, segs, rate, ch=2, amp=0.5, phase_r=0.0):
    """segs: [(周波数Hz, 秒)]。左右の位相差 phase_r（ラジアン）"""
    w = wave.open(mk(path), 'wb'); w.setnchannels(ch); w.setsampwidth(2); w.setframerate(rate)
    t0 = 0
    buf = bytearray()
    for hz, sec in segs:
        n = int(rate * sec)
        for i in range(n):
            t = (t0 + i) / rate
            fade = min(1.0, i / (rate * 0.005), (n - i) / (rate * 0.005))   # 区切りのプチ音を避ける 5ms
            l = int(32767 * amp * fade * math.sin(2 * math.pi * hz * t))
            if ch == 2:
                r = int(32767 * amp * fade * math.sin(2 * math.pi * hz * t + phase_r))
                buf += struct.pack('<hh', l, r)
            else:
                buf += struct.pack('<h', l)
        t0 += n
    w.writeframes(bytes(buf)); w.close()

def enc(wav, out, kind):
    mk(out)
    if os.path.exists(out): os.remove(out)
    r = subprocess.run([ENC, wav, out, kind], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        failed.append((out, kind, (r.stderr or r.stdout).strip()))
        print('  FAILED', os.path.basename(out), kind, (r.stderr or r.stdout).strip())
        return False
    print('  ok', os.path.relpath(out, OUT))
    return True

tmp = os.path.join(OUT, '_tmp'); os.makedirs(tmp, exist_ok=True)

# ---- formats/ : 形式ごと（各 2 秒、周波数が違う）。44.1kHz
F = os.path.join(OUT, 'formats')
for name, hz, kind, ext in [('f1_mp3', 440, 'mp3', 'mp3'), ('f2_aac', 523, 'aac', 'm4a'), ('f3_alac', 659, 'alac', 'm4a'),
                            ('f4_flac', 784, 'flac', 'flac'), ('f5_wav', 880, None, 'wav'), ('f6_wma', 988, 'wma', 'wma')]:
    src = os.path.join(tmp, name + '.wav')
    write_wav(src, [(hz, 2.0)], 44100)
    if kind == 'alac':   # MF の変換は ALAC の m4a を仕上げられない。自前の書き出し（tools/alac_writer.py）を使う
        w = wave.open(src, 'rb'); raw = w.readframes(w.getnframes()); w.close()
        smp = struct.unpack('<%dh' % (len(raw) // 2), raw)
        write_alac(mk(os.path.join(F, name + '.m4a')), list(smp[0::2]), list(smp[1::2]), 44100); print('  ok formats/' + name + '.m4a (alac_writer)')
    elif kind is None: write_wav(os.path.join(F, name + '.wav'), [(hz, 2.0)], 44100); print('  ok formats/' + name + '.wav')
    else: enc(src, os.path.join(F, name + '.' + ext), kind)

# ---- rates/ : 44.1kHz と 96kHz（どちらも 1000Hz、3 秒）
R = os.path.join(OUT, 'rates')
write_wav(os.path.join(R, 'r1_44k.wav'), [(1000, 3.0)], 44100)
write_wav(os.path.join(R, 'r2_96k.wav'), [(1000, 3.0)], 96000)
src = os.path.join(tmp, 'r3_96k.wav'); write_wav(src, [(1000, 3.0)], 96000)
enc(src, os.path.join(R, 'r3_96k.flac'), 'flac')
write_wav(os.path.join(R, 'r4_mono44k.wav'), [(1000, 3.0)], 44100, ch=1)

# ---- natural/ : track2 < track10。サブフォルダと非対応ファイルは一覧に入らない
N = os.path.join(OUT, 'natural')
for name, hz in [('track1', 300), ('track2', 400), ('track10', 500)]:
    src = os.path.join(tmp, name + '.wav'); write_wav(src, [(hz, 2.0)], 44100)
    enc(src, os.path.join(N, name + '.mp3'), 'mp3')
write_wav(os.path.join(N, 'sub', 'inner.wav'), [(900, 2.0)], 44100)
open(mk(os.path.join(N, 'notes.txt')), 'w').write('not audio')

# ---- repeat/ : 3 曲、各 1.5 秒（300 / 400 / 500 Hz）
P = os.path.join(OUT, 'repeat')
for name, hz in [('p1', 300), ('p2', 400), ('p3', 500)]:
    write_wav(os.path.join(P, name + '.wav'), [(hz, 1.5)], 44100)

# ---- long/ : 60 秒。10 秒ごとに周波数が変わる（シークの位置を周波数で確かめる）。300,400,500,600,700,800 Hz
L = os.path.join(OUT, 'long')
write_wav(os.path.join(L, 'long.wav'), [(300 + 100 * i, 10.0) for i in range(6)], 44100)
write_wav(os.path.join(L, 'z_after.wav'), [(1200, 2.0)], 44100)

# ---- broken/ : 中身を壊した mp3 が間にある。allbroken/ : すべて壊れている
B = os.path.join(OUT, 'broken')
write_wav(os.path.join(B, 'a_good.wav'), [(400, 1.5)], 44100)
random.seed(1)
open(mk(os.path.join(B, 'b_broken.mp3')), 'wb').write(bytes(random.randrange(256) for _ in range(40000)))
write_wav(os.path.join(B, 'c_good.wav'), [(600, 1.5)], 44100)
A = os.path.join(OUT, 'allbroken')
for n in ('x.mp3', 'y.mp3', 'z.flac'):
    open(mk(os.path.join(A, n)), 'wb').write(bytes(random.randrange(256) for _ in range(30000)))

# ---- many/ : 70 曲（一覧のスクロール確認用、各 3 秒・モノラル 22.05kHz。名前の長さはまちまち）
M = os.path.join(OUT, 'many')
for i in range(1, 71):
    write_wav(os.path.join(M, 'song_%02d_%s.wav' % (i, 'long name for list ' * (1 + i % 4))), [(200 + 10 * i, 3.0)], 22050, ch=1)

for f in os.listdir(tmp): os.remove(os.path.join(tmp, f))
os.rmdir(tmp)
print()
print('failed:', failed if failed else 'none')
