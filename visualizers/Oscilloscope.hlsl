// name: Oscilloscope
// pixel: 1
//
// The waveform (waveAt) as a 1-dot line. The middle of the pane is 0.
// Each column also covers the vertical gap to its neighbours (halfway to each side),
// so a steep part of the wave is still one unbroken line. Works at any canvas width.

float traceY(float col)
{
    float x = (col + 0.5) / iResolution.x;                 // 0..1 across the canvas
    return (0.5 + 0.45 * waveAt(x)) * iResolution.y;       // -1..1 -> dots from the bottom
}

float4 mainImage(float2 fragCoord)
{
    float col = floor(fragCoord.x);
    float y  = traceY(col);
    float yl = traceY(col - 1.0);
    float yr = traceY(col + 1.0);
    float lo = min(y, min(0.5 * (y + yl), 0.5 * (y + yr)));
    float hi = max(y, max(0.5 * (y + yl), 0.5 * (y + yr)));
    float onLine = (fragCoord.y >= lo - 0.5 && fragCoord.y <= hi + 0.5) ? 1.0 : 0.0;
    float mid = abs(fragCoord.y - 0.5 * iResolution.y) < 0.5 ? 0.12 : 0.0;   // faint center line
    float3 c = float3(1.0, 0.75, 0.2) * onLine + float3(0.3, 0.3, 0.3) * mid * (1.0 - onLine);
    return float4(c, 1);
}
