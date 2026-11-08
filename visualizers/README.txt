TLAP visualizers
================

TLAP draws its spectrum / visualizer with an HLSL pixel shader (the same idea as Shadertoy).
Put a .hlsl file in this folder (next to TLAP.exe) and press V in TLAP to switch to it.
V cycles through the shaders in file-name order, then "None" (the pane disappears), then back to the first one.
The selected shader is remembered.

A shader can only compute colors from numbers TLAP gives it. It cannot read files or touch the network.

Live editing
------------
While a shader is shown, save the .hlsl file and TLAP recompiles it within a second.
If the file has an error, the previous picture keeps running and the first line of the error
is shown (in red) under the pane, with the line number of YOUR file ("line 8: error X3000 ...").
Fix the file and save again; the error goes away.
New files in this folder are found automatically.

What you write
--------------
One function, mainImage, plus any helper functions you like. TLAP adds a prelude
(constants, textures, the functions below) and compiles for ps_5_0.

    // name: Bars
    // pixel: 4

    float4 mainImage(float2 fragCoord)
    {
        float2 uv = fragCoord / iResolution;        // 0..1, the origin (0,0) is the BOTTOM-LEFT
        float  v  = band(uv.x);                     // loudness at that position, 0..1
        float3 c  = uv.y < v ? float3(0.2, 1.0, 0.4) : float3(0, 0, 0);
        return float4(c, 1);
    }

- fragCoord is the center of the dot (steps of 0.5). The origin is the bottom-left, like Shadertoy,
  so shaders are easy to port.
- Return a color in 0..1. Alpha is not used.

Comments at the top of the file (the first 30 lines):
    // name: My shader      the name shown in menus (default: the file name without .hlsl)
    // pixel: 4             size of one dot, 1..16 (default 2). See "Dots" below.

What TLAP gives you (already defined, just use them)
----------------------------------------------------
    iResolution   float2   size of the canvas in dots
    iTime         float    seconds since the shader was loaded
    iTimeDelta    float    seconds since the previous frame
    iFrame        int      number of frames since the shader was loaded
    playing       float    1 while a song is playing, 0 when paused / stopped
    level         float    current loudness, 0..1 (RMS)
    bass mid treble  float loudness of low (20-250 Hz), middle (250 Hz-4 kHz), high (4-20 kHz), 0..1
    wavePeak      float    peak of the mono waveform (largest absolute value), 0..1, LINEAR (not dB). Rises instantly, falls to half in about 2 seconds

    band(x)       float    64 bands, log-spaced from 20 Hz to 20 kHz. x is 0..1 (values in between are interpolated linearly)
    fftAt(x)      float    1024 bins, linear in frequency (0 .. half the sample rate). x is 0..1
    fftLR(x)      float2   the same as fftAt, for the left and right channel separately: float2(L, R) (same conversion)
    waveAt(x)     float    mono waveform, -1..1. x is 0..1 (512 samples, oldest to newest)
    waveLR(x)     float2   left and right waveform float2(L, R), -1..1 (for XY oscilloscopes)
    prevFrame(uv) float3   the color of the previous frame at uv (0..1, origin bottom-left).
                           For trails / feedback effects. Outside the canvas it is black.

History (for 3D: what the sound looked like a moment ago)
-------------------------------------------------------
TLAP keeps the last 128 rows of the sound, one row per 1/60 second (so 128 rows = about 2.1 seconds).
The speed is the same on a 60 Hz and a 144 Hz screen: TLAP adds a row for every 1/60 second that passes.
Age 0 is the newest row, age 127 is the oldest. The history is kept while you switch shaders.
It also keeps filling while paused (the picture then shows flat lines, because the values fall to silence).

    HIST                       int      number of rows = 128 (a constant)
    waveHist(x, age)  float    mono waveform of the row that is `age` rows old, -1..1. x is 0..1 (512 samples, linear in between)
    waveLRHist(x, age) float2  the same for left and right, float2(L, R)
    bandHist(x, age)  float    the 64 bands of that row, 0..1 (x is 0..1)
    wavePeakHist(age) float    wavePeak as it was when that row was added, 0..1 linear. Divide waveHist by it (with a floor) to auto-scale quiet sounds

`age` is an integer 0..127 (outside the range it is clamped). The rows of the history waveform are aligned at a rising
zero crossing (the first sample of every row is where the signal goes up through zero), so a steady, repeating sound
flows backwards without swaying left and right. The current waveAt / waveLR are NOT aligned (they are the raw latest samples).
Keep the loop per dot to about 128 steps (e.g. one loop over the rows); a 128 x 512 double loop is too heavy.

Loudness values: -60 dB .. 0 dB is mapped to 0..1 (a full-scale sine wave = 1).
They rise instantly and fall to 0 in one second (they keep falling while paused).
The FFT uses 2048 points with a Hann window on the mono mix (left + right).
The sound is taken after the equalizer and before the volume, so lowering the volume does not shrink the picture.
prevFrame is reset to black when the shader is switched or the canvas size changes.
The picture is drawn 60 times a second (vsync), also while paused.

Dots (pixel-perfect drawing)
----------------------------
The shader draws on a low-resolution canvas. TLAP enlarges it by a whole number, without any smoothing,
so even 3D effects become square dots.
- One dot is "pixel" x (UI scale) screen pixels. UI scale is 1 at 100%, 2 at 200%.
- Canvas size = pane size / (pixel x UI scale), rounded down. The remainder is black, and the canvas is centered.
- The canvas is at most 4096 x 2048 dots.

Rules
-----
- Only the .hlsl text is read. #include is NOT allowed (it is an error).
- A file can be at most 256 KB.
- If a shader is so heavy that Windows resets the GPU, TLAP recreates Direct3D, turns the visualizer off ("None")
  and shows "GPU timeout" under the pane. The music keeps playing.
  A heavy shader can still freeze the whole screen for about 2 seconds until Windows resets the GPU, so keep loops small.
- While TLAP is minimized, or the visualizer is "None", nothing is analyzed or drawn.
- Requires d3dcompiler_47.dll (it is part of Windows).
