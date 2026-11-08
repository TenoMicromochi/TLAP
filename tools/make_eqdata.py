# Stage 3 test audio -> testdata/eq/  (py tools\make_eqdata.py)
import math, os, struct, wave
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'testdata', 'eq')
os.makedirs(OUT, exist_ok=True)
def wav(name, f, sec, rate, amp, phase_r=0.0, f_r=None):
    w = wave.open(os.path.join(OUT, name), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    buf = bytearray()
    for i in range(int(rate * sec)):
        t = i / rate
        buf += struct.pack('<hh', int(round(32767 * amp * math.sin(2 * math.pi * f * t))), int(round(32767 * amp * math.sin(2 * math.pi * (f_r or f) * t + phase_r))))
    w.writeframes(bytes(buf)); w.close()
wav('q1k.wav', 1000, 4.0, 48000, 0.02)                  # quiet 1 kHz (48k: no resampling, so the output can be compared bit by bit)
wav('s1k.wav', 1000, 6.0, 48000, 0.5)                   # normal level 1 kHz (spectrum / bars)
wav('liss.wav', 440, 8.0, 48000, 0.5, math.pi / 2)      # L/R 90 degrees apart -> a circle on an XY scope
wav('liss2.wav', 440, 8.0, 48000, 0.5, 0.0, 660)        # 2:3 Lissajous
wav('loud1k.wav', 1000, 3.0, 48000, 0.99)                # near full scale (clipping check with EQ +12 dB)
wav('s100.wav', 100, 3.0, 48000, 0.02)                   # quiet 100 Hz (to see the auto pre-amp lowering the other bands)
print('eq test data done')
