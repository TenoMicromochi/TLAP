# EQ check: py tools\analyze_eq.py <TLAP_out.f32> <input.wav> <volume%>
# - prints the output peak (converted back to input scale by the volume gain), the ratio to the input peak, and whether the output equals input*gain bit by bit
import struct, sys, wave
f32, wavp, vol = sys.argv[1], sys.argv[2], int(sys.argv[3])
gain = struct.unpack('<f', struct.pack('<f', (vol / 100.0) ** 2))[0]
d = open(f32, 'rb').read(); n = len(d) // 8
out = [struct.unpack_from('<ff', d, i * 8) for i in range(n)]
w = wave.open(wavp, 'rb'); raw = w.readframes(w.getnframes()); w.close()
inp = struct.unpack('<%dh' % (len(raw) // 2), raw); L_in = inp[0::2]; R_in = inp[1::2]
def f32r(x): return struct.unpack('<f', struct.pack('<f', x))[0]
# the first non-zero output sample = the first input sample (the 10 ms volume ramp is over once it is at a steady gain; compare the middle part)
start = next(i for i, (l, r) in enumerate(out) if l != 0 or r != 0)
peak_in = max(abs(v) for v in L_in) / 32768.0
a, b = start + 4800, min(n, start + len(L_in)) - 4800          # skip the 0.1 s edges (ramp-in / drain)
peak_out = max(abs(out[i][0]) for i in range(a, b)) / gain
print('input peak %.5f, output peak (converted by the volume gain %.6f) %.5f, ratio %.4f (%.2f dB)' % (peak_in, gain, peak_out, peak_out / peak_in, 20 * __import__('math').log10(peak_out / peak_in)))
off = start    # alignment: output frame i <-> input frame i - start (+ maybe a few frames of pad); find the offset by matching
best = None
for sh in range(-2, 3):
    ok = all(out[i][0] == f32r(f32r(L_in[i - start + sh] / 32768.0) * gain) and out[i][1] == f32r(f32r(R_in[i - start + sh] / 32768.0) * gain) for i in range(a, b))
    if ok: best = sh; break
print('bit-exact against input*gain over %d frames: %s' % (b - a, 'YES (offset %d)' % best if best is not None else 'no'))
