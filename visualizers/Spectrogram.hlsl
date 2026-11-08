// name: Spectrogram
// pixel: 1
//
// A sound print: frequency up the side (low notes at the bottom, 20 Hz .. 20 kHz on a log scale),
// time across (the right edge is now, older sound flows left), colour is loudness:
// black - navy - purple - red - orange - yellow - white.
// One column = 1/60 second, whatever the refresh rate: the picture is shifted left with prevFrame,
// and the number of new columns this frame comes from iTime and iTimeDelta
// (TLAP adds iTimeDelta to iTime after drawing, so floor(iTime*60) steps by exactly the columns that pass).

float rowLevel(float row)
{
    float y0 = row / iResolution.y;
    float y1 = (row + 1.0) / iResolution.y;
    float f0 = 20.0 * pow(1000.0, y0);      // 20 Hz .. 20 kHz, like Spectrum Line but vertical
    float f1 = 20.0 * pow(1000.0, y1);
    float m = 0.0;
    for (int i = 0; i < 6; i++)             // the loudest few samples inside this row's range of bins
    {
        float f = lerp(f0, f1, (i + 0.5) / 6.0);
        m = max(m, fftAt(f / 24000.0));     // 24000 assumes a 48 kHz output
    }
    return saturate((m - 0.25) / 0.75);     // -45 dB and below is black, so the 1 s fall-off of the values does not smear the print
}

float3 ramp(float v)
{
    const float3 C0 = float3(0.00, 0.00, 0.00);   // black
    const float3 C1 = float3(0.00, 0.05, 0.45);   // navy
    const float3 C2 = float3(0.45, 0.00, 0.60);   // purple
    const float3 C3 = float3(0.90, 0.08, 0.10);   // red
    const float3 C4 = float3(1.00, 0.55, 0.00);   // orange
    const float3 C5 = float3(1.00, 1.00, 0.20);   // yellow
    const float3 C6 = float3(1.00, 1.00, 1.00);   // white
    float t = saturate(v) * 6.0;
    if (t < 1.0) return lerp(C0, C1, t);
    if (t < 2.0) return lerp(C1, C2, t - 1.0);
    if (t < 3.0) return lerp(C2, C3, t - 2.0);
    if (t < 4.0) return lerp(C3, C4, t - 3.0);
    if (t < 5.0) return lerp(C4, C5, t - 4.0);
    return lerp(C5, C6, t - 5.0);
}

float4 mainImage(float2 fragCoord)
{
    float W = iResolution.x;
    float x = floor(fragCoord.x);
    float n = min(floor((iTime + iTimeDelta) * 60.0) - floor(iTime * 60.0), 16.0);   // columns that pass this frame
    if (iFrame == 0) n = 0.0;
    if (x < W - n)
    {
        float2 uv = float2((fragCoord.x + n) / W, fragCoord.y / iResolution.y);      // the dot n columns to the right, last frame
        return float4(prevFrame(uv), 1);
    }
    return float4(ramp(rowLevel(floor(fragCoord.y))), 1);                            // the new columns at the right edge
}
