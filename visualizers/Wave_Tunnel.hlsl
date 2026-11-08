// name: Wave Tunnel
// pixel: 2
//
// The oscilloscope trace, one line per 1/60 second, flowing away into the distance (waveHist, 128 rows).
// A camera looks down at it from a little above. Lines further away are smaller and gather at the vanishing point.
// Hidden lines: a nearer line hides the part of a farther line that falls below it on the screen.
// Each row is aligned at a rising zero crossing by TLAP, so a steady tone flows straight back without swaying.
// Auto range: every row is divided by the peak at the time it was added (wavePeakHist, floor 1/8 = at most 8x gain),
// so a quiet sound sways as wide as a loud one, and a row keeps its shape while it flows back.
// The camera's field of view is set by the canvas HEIGHT, so changing the width never stretches the picture.

static const float3 GREEN = float3(0.10, 1.00, 0.25);     // the green of Green Bars
static const float3 FRONT = float3(0.60, 1.00, 0.70);     // the nearest line is brighter
static const float  AMP   = 0.25;                          // wave height at the nearest line, in canvas heights
static const float  CAM_H = 0.48;                          // camera height above the floor (canvas heights at the nearest line)
static const float  D_NEAR = 1.0;
static const float  D_FAR  = 7.0;
static const float  MIN_PEAK = 0.125;                      // the gain is limited to 8x (silence stays flat)

float wv(float x, int k)                                   // the row's wave, scaled to its own peak, softly rounded near the edge
{
    float v = waveHist(x, k) / max(wavePeakHist(k), MIN_PEAK);
    return 1.1 * tanh(v / 1.1);
}

float4 mainImage(float2 fragCoord)
{
    float H  = iResolution.y;
    float vx = 0.5 * iResolution.x;                        // vanishing point: horizontally in the middle, above the middle
    float vy = 0.72 * H;
    float halfW = 0.5 * iResolution.x / H;                 // the nearest line spans the full width of the canvas

    float px = fragCoord.x, py = fragCoord.y;
    float top = -1e9;                                      // the highest line so far in this column
    float3 c = float3(0, 0, 0);

    [loop]
    for (int k = 0; k < HIST; k++)
    {
        float d = D_NEAR + (D_FAR - D_NEAR) * k / (HIST - 1.0);
        float s = H / d;                                   // screen size of one unit at this distance
        float ua = ((px - 0.5 - vx) / s) / (2.0 * halfW) + 0.5;      // this column's left and right edge on the line (0..1)
        float ub = ((px + 0.5 - vx) / s) / (2.0 * halfW) + 0.5;
        if (ub < 0.0 || ua > 1.0) continue;                // the line does not reach this column
        ua = saturate(ua); ub = saturate(ub);
        float ya = vy + (-CAM_H + AMP * wv(ua, k)) * s;
        float yb = vy + (-CAM_H + AMP * wv(ub, k)) * s;
        float lo = min(ya, yb), hi = max(ya, yb);
        if (py + 0.5 >= lo && py - 0.5 <= hi && py > top)  // on this line, and above all the nearer lines
        {
            float fade = 0.06 + 0.94 * exp(-k / 36.0);
            c = (k == 0 ? FRONT : GREEN * fade);
            break;
        }
        top = max(top, hi);
        if (top >= py) break;                              // everything further back is hidden here
    }
    return float4(c, 1);
}
