# Test sounds for the bundled visualizers (generated, no real music). Writes testdata/vis/*.wav
#   pulse40.wav : 40 Hz sine bursts (0.25 s on, 1.25 s off), 24 s
#   tone1k.wav  : steady 1 kHz sine, 24 s
#   mix.wav     : pink-ish stack of sines (60, 220, 440, 880, 1760, 3520, 7040 Hz) with a slow sweep, 24 s
import wave, struct, math, os
d = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'testdata', 'vis'); os.makedirs(d, exist_ok=True)
R = 44100; N = R * 24
def write(name, f):
    w = wave.open(os.path.join(d, name), 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(R)
    w.writeframes(b''.join(struct.pack('<hh', *[int(max(-1, min(1, v)) * 32000) for v in f(i / R)]) for i in range(N))); w.close()
def pulse(t):
    on = (t % 1.5) < 0.25; env = math.sin(math.pi * (t % 1.5) / 0.25) if on else 0
    v = 0.8 * env * math.sin(2 * math.pi * 40 * t); return v, v
def tone(t):
    v = 0.7 * math.sin(2 * math.pi * 1000 * t); return v, v
def mix(t):
    fs = [60, 220, 440, 880, 1760, 3520, 7040]; s = sum(math.sin(2 * math.pi * f * t) / (1 + i) for i, f in enumerate(fs)) / 2.5
    sw = 0.3 * math.sin(2 * math.pi * (200 + 1500 * ((t / 24) ** 2)) * t)
    return 0.5 * (s + sw), 0.5 * (s * 0.8 - sw)
write('pulse40.wav', pulse); write('tone1k.wav', tone); write('mix.wav', mix)

# Added in stage 4d (XY Scope): steady 24 s tones with a chosen phase difference between the channels
#   xy_mono.wav    : 330 Hz, left = right                -> a 45 degree line
#   xy_circle.wav  : 330 Hz, right a quarter turn behind -> a circle
#   xy_ellipse.wav : 330 Hz, right 45 degrees behind     -> a tilted ellipse
def xy(deg):
    ph = math.radians(deg)
    return lambda t: (0.8 * math.sin(2 * math.pi * 330 * t), 0.8 * math.sin(2 * math.pi * 330 * t - ph))
write('xy_mono.wav', xy(0)); write('xy_circle.wav', xy(90)); write('xy_ellipse.wav', xy(45))
