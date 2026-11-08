// name: Mirror Bars
// pixel: 1
//
// Green Bars mirrored around the middle line, one bar per dot column: the RIGHT channel grows UP in red,
// the LEFT channel grows DOWN in white (the colours of the RCA plugs). Any window width is filled edge to edge.
// The middle line itself is always drawn. Uses fftLR (left and right spectra).

// How loud is dot column `col` (0 .. W-1) in the left and right channel? float2(L, R).
// Frequencies are laid out on a log scale (20 Hz .. 20 kHz) over the whole width.
// fftLR(x) is linear in frequency (0 .. half the sample rate), so a column covers a range of bins:
// take the loudest few samples inside the column's range (a narrow column at the treble end spans many bins).
// 24000 assumes a 48 kHz output (the usual Windows mix rate).
float2 colLevel(float col)
{
    float x0 = col / iResolution.x;
    float x1 = (col + 1.0) / iResolution.x;
    float f0 = 20.0 * pow(1000.0, x0);      // 20 Hz .. 20 kHz
    float f1 = 20.0 * pow(1000.0, x1);
    float2 m = float2(0, 0);
    for (int i = 0; i < 6; i++)
    {
        float f = lerp(f0, f1, (i + 0.5) / 6.0);
        m = max(m, fftLR(f / 24000.0));
    }
    return saturate(m);
}

float4 mainImage(float2 fragCoord)
{
    float2 lv = colLevel(floor(fragCoord.x));
    float half_ = 0.5 * iResolution.y;
    float dy = fragCoord.y - half_;                          // + above the middle line, - below
    float3 c = float3(0, 0, 0);
    if (abs(dy) <= 0.5) c = float3(1, 1, 1);                 // the middle line is always drawn
    else if (dy > 0 && dy <= lv.y * half_) c = float3(1.0, 0.15, 0.12);   // R, up, red
    else if (dy < 0 && -dy <= lv.x * half_) c = float3(1, 1, 1);           // L, down, white
    return float4(c, 1);
}
