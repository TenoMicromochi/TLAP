// name: Green Bars
// pixel: 1
//
// One bar per dot column, growing up from the bottom. The number of bars is the canvas width,
// so nothing is fixed: any window width is filled edge to edge.

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
    float h = colLevel(floor(fragCoord.x)) * iResolution.y;
    float lit = fragCoord.y <= h ? 1.0 : 0.0;
    return float4(float3(0.1, 1.0, 0.25) * lit, 1);
}
