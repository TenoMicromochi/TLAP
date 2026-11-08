# ギャップレスの確認：TLAP（trace ビルド）が WASAPI へ書いた音（build/TLAP_out.f32、float32 ステレオ）を調べる。
#   py tools\analyze_seam.py <f32> <出力レート> <周波数Hz> <1曲の秒数> <曲数>
# 位相のつながった正弦波が続いているはずなので、全体に正弦波（振幅・位相は最小二乗）を当てて、残り（residual）が継ぎ目で跳ねていないかを見る。
import struct, sys, math
path, R, f, seg, n = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), int(sys.argv[5])
d = open(path, 'rb').read()
x = [struct.unpack_from('<f', d, i * 8)[0] for i in range(len(d) // 8)]   # 左
# 音のある範囲（最初に絶対値が大きくなった所から最後まで）
peak = max(abs(v) for v in x)
i0 = next(i for i, v in enumerate(x) if abs(v) > 0.2 * peak)
i1 = len(x) - 1 - next(i for i, v in enumerate(reversed(x)) if abs(v) > 0.2 * peak)
# 立ち上がり・立ち下がりの音量ランプ（10ms）を避ける
a, b = i0 + int(0.05 * R), i1 - int(0.05 * R)
w = 2 * math.pi * f / R
# 短い窓（10ms）ごとに、振幅・位相を最小二乗で当てはめて残りを見る。周波数が1万分の1ずれていても影響しない
BL = int(R * 0.010)
res = []; amps = []
for s0 in range(a, b - BL, BL):
    ss = cc = sc = ssn = scn = 0.0
    for i in range(s0, s0 + BL):
        s_, c_ = math.sin(w * i), math.cos(w * i); v = x[i]
        ss += s_ * s_; cc += c_ * c_; sc += s_ * c_; ssn += s_ * v; scn += c_ * v
    det = ss * cc - sc * sc
    A = (ssn * cc - scn * sc) / det; B = (scn * ss - ssn * sc) / det
    amps.append(math.hypot(A, B))
    res.append((s0, max(abs(x[i] - (A * math.sin(w * i) + B * math.cos(w * i))) for i in range(s0, s0 + BL))))
amp = sorted(amps)[len(amps) // 2]
print('samples %d, active [%d, %d] = %.3f s, peak %.5f, typical amp %.5f (min %.5f, max %.5f)' % (len(x), i0, i1, (i1 - i0) / R, peak, amp, min(amps), max(amps)))
mxr = max(r for _, r in res)
print('local-fit residual (10 ms blocks): max %.3e = %.4f%% of amp, median %.3e' % (mxr, 100 * mxr / amp, sorted(r for _, r in res)[len(res) // 2]))
print('amplitude spread over blocks: %.3f%%' % (100 * (max(amps) - min(amps)) / amp))
# 継ぎ目の前後（±5ms）で、隣り合う標本の差の最大（理想は amp*w）と、局所当てはめの残りの最大
for sidx in range(1, n):
    c = i0 + int(round(sidx * seg * R)); lo, hi = c - int(0.02 * R), c + int(0.02 * R)
    md = max(abs(x[i + 1] - x[i]) for i in range(lo, hi))
    mr = max([r for s0, r in res if lo - BL <= s0 <= hi] or [0])
    ma = [amps[k] for k, (s0, r) in enumerate(res) if lo - BL <= s0 <= hi]
    print('seam %d near sample %d (+-20ms): max step %.5f (ideal max %.5f), max local residual %.3e (%.4f%% of amp), amp in %.5f..%.5f' % (sidx, c, md, amp * w, mr, 100 * mr / amp, min(ma), max(ma)))
longest = run = 0
for i in range(a, b):
    if abs(x[i]) < amp * 0.01: run += 1; longest = max(longest, run)
    else: run = 0
print('longest run of |x| < 1%% amp: %d samples (a sine zero-crossing alone gives about %d)' % (longest, int(0.02 / w) + 1))
# 継ぎ目の外（±20ms を除く）の残りの最大。非可逆（Vorbis / Opus）では量子化ノイズがこの程度ある。継ぎ目の残りがこれと同程度なら、継ぎ目で余計な段差は出ていない
seams = [i0 + int(round(k * seg * R)) for k in range(1, n)]
out_max = max([r for s0, r in res if all(abs(s0 + BL // 2 - c) > int(0.02 * R) for c in seams)] or [0])
print('residual max outside the +-20ms seam windows: %.3e (%.4f%% of amp)' % (out_max, 100 * out_max / amp))
