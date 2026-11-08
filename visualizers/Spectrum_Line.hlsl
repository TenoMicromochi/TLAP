// name: Spectrum Line
// pixel: 1
//
// The spectrum as a 1-dot line (log frequency, low notes on the left).
// Each column also covers the vertical gap to its neighbours (halfway to each side),
// so a steep slope is still one unbroken line.

// How loud is dot column `col` (0 .. W-1)? Frequencies are laid out on a log scale (20 Hz .. 20 kHz) over the
// whole width, so the same code fills any canvas width: every column is its own bar.
// fftAt(x) is linear in frequency (0 .. half the sample rate), so a column covers a range of bins:
// take the loudest few samples inside the column's range (a narrow column at the treble end spans many bins).
// 24000 assumes a 48 kHz output (the usual Windows mix rate).
float colLevel(float col)
{
    float x0 = col / iResolution.x;
    float x1 = (col + 1.0) / iResolution.x;
    float f0 = 20.0 * pow(1000.0, x0);      // 20 Hz .. 20 kHz
    float f1 = 20.0 * pow(1000.0, x1);
    float m = 0.0;
    for (int i = 0; i < 6; i++)
    {
        float f = lerp(f0, f1, (i + 0.5) / 6.0);
        m = max(m, fftAt(f / 24000.0));
    }
    return saturate(m);
}

float4 mainImage(float2 fragCoord)
{
    float col = floor(fragCoord.x);
    float H = iResolution.y;
    float y  = colLevel(col) * H;
    float yl = colLevel(col - 1.0) * H;
    float yr = colLevel(col + 1.0) * H;
    float lo = min(y, min(0.5 * (y + yl), 0.5 * (y + yr)));
    float hi = max(y, max(0.5 * (y + yl), 0.5 * (y + yr)));
    float lit = (fragCoord.y >= lo - 0.5 && fragCoord.y <= hi + 0.5) ? 1.0 : 0.0;
    return float4(float3(0.85, 0.95, 1.0) * lit, 1);
}
