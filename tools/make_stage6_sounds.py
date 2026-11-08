# Test sounds for the stage 6 (3D) visualizers. Writes testdata/vis6/*.wav (generated, no real music), 60 s each, 44.1 kHz stereo.
#   sine220.wav   : steady 220 Hz sine, mono          (Wave Tunnel must flow straight back, not sway)
#   sweep.wav     : 100 Hz -> 8 kHz exponential sweep, repeating every 8 s   (Spectrum Terrain / Ring Tunnel)
#   phase90.wav   : 220 Hz, right channel a quarter turn behind               (XY Scope 3D: spiral)
#   mono.wav      : 220 Hz, left = right                                      (XY Scope 3D: flat ribbon)
#   pulse40.wav   : 40 Hz bursts every 0.5 s (0.15 s on)                      (Ring Tunnel shake)
import wave, os
import numpy as np
d = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'testdata', 'vis6'); os.makedirs(d, exist_ok=True)
R = 44100; N = R * 60; t = np.arange(N) / R
def write(name, l, r):
    a = np.stack([l, r], 1); a = (np.clip(a, -1, 1) * 30000).astype('<i2')
    w = wave.open(os.path.join(d, name), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(R); w.writeframes(a.tobytes()); w.close()
s = 0.8 * np.sin(2 * np.pi * 220 * t); write('sine220.wav', s, s); write('mono.wav', s, s)
write('phase90.wav', s, 0.8 * np.sin(2 * np.pi * 220 * t - np.pi / 2))
tp = t % 8.0; f0, f1 = 100.0, 8000.0; k = np.log(f1 / f0) / 8.0
ph = 2 * np.pi * f0 * (np.exp(k * tp) - 1) / k; sw = 0.7 * np.sin(ph); write('sweep.wav', sw, sw)
tt = t % 0.5; env = np.where(tt < 0.15, np.sin(np.pi * tt / 0.15), 0.0); p = 0.9 * env * np.sin(2 * np.pi * 40 * t); write('pulse40.wav', p, p)

# Stage 6b additions
#   left_only.wav / right_only.wav : a stack of sines (110, 220, 440, 880, 1760, 3520 Hz) in one channel only (Mirror Bars)
#   loud.wav : full-scale 100 Hz -> 8 kHz sweep plus 60 Hz, as loud as it gets (Ring Tunnel must still fit the pane)
st = sum(np.sin(2 * np.pi * f * t) / (1 + i * 0.5) for i, f in enumerate([110, 220, 440, 880, 1760, 3520])) * 0.3
z = np.zeros(N); write('left_only.wav', st, z); write('right_only.wav', z, st)
lo = np.clip(0.7 * np.sin(ph) + 0.6 * np.sin(2 * np.pi * 60 * t), -1, 1); write('loud.wav', lo, lo)

# Stage 6c additions
#   sine_m20db.wav   : 220 Hz sine at -20 dB (peak 0.1)         (Wave Tunnel auto range: as wide as sine220.wav)
#   quiet_to_loud.wav: -30 dB 220 Hz, jumps to 0 dB every 6 s (3 s loud)  (the nearest line must not overflow the pane)
#   sines_sum.wav    : sum of 6 sines, 150 Hz .. 4.8 kHz doubling (Spectrogram: horizontal lines)
write('sine_m20db.wav', 0.1 / 0.8 * s, 0.1 / 0.8 * s)
g = np.where((t % 6.0) >= 3.0, 0.8, 0.8 * 10 ** (-30 / 20)); q = g * np.sin(2 * np.pi * 220 * t); write('quiet_to_loud.wav', q, q)
ss = sum(np.sin(2 * np.pi * f * t) * a for f, a in zip([150, 300, 600, 1200, 2400, 4800], [0.18, 0.16, 0.14, 0.12, 0.10, 0.08])); write('sines_sum.wav', ss, ss)
