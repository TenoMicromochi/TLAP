// name: Spectrum Terrain
// pixel: 2
//
// The spectrum (bandHist, 128 rows of 64 bands) as a mountain range that flows away from you.
// Across: frequency (log, low on the left). Height: loudness. Wireframe: one line per row, plus a line
// along the depth every 4 bands. Hidden lines: whatever falls behind a nearer ridge is not drawn.
// Colour follows the height: dark teal when low, green, yellow, red when high. Farther is darker.
// The field of view is set by the canvas HEIGHT, so changing the width never stretches the picture.

static const float AMP    = 0.36;     // terrain height at the nearest row, in canvas heights (loudness 1.0)
static const float CAM_H  = 0.42;     // camera height above the floor
static const float D_NEAR = 1.0;
static const float D_FAR  = 7.0;
static const int   LINES  = 16;       // lines along the depth (one every 4 of the 64 bands)

float3 heightColor(float v)
{
    float3 a = float3(0.00, 0.28, 0.38);
    float3 b = float3(0.10, 0.95, 0.45);
    float3 c = float3(1.00, 0.92, 0.10);
    float3 d = float3(1.00, 0.18, 0.10);
    if (v < 0.35) return lerp(a, b, v / 0.35);
    if (v < 0.70) return lerp(b, c, (v - 0.35) / 0.35);
    return lerp(c, d, saturate((v - 0.70) / 0.30));
}

float depthAt(int k) { return D_NEAR + (D_FAR - D_NEAR) * k / (HIST - 1.0); }

float4 mainImage(float2 fragCoord)
{
    float H  = iResolution.y;
    float vx = 0.5 * iResolution.x;
    float vy = 0.64 * H;
    float halfW = 0.5 * iResolution.x / H;
    float px = fragCoord.x, py = fragCoord.y;
    float top = -1e9;
    float3 c = float3(0, 0, 0);

    [loop]
    for (int k = 0; k < HIST; k++)
    {
        float sk = H / depthAt(k);
        float ua = ((px - 0.5 - vx) / sk) / (2.0 * halfW) + 0.5;
        float ub = ((px + 0.5 - vx) / sk) / (2.0 * halfW) + 0.5;
        if (ub < 0.0 || ua > 1.0) continue;
        float fade = 1.0 - 0.75 * k / (HIST - 1.0);
        float uca = saturate(ua), ucb = saturate(ub);
        float ya = vy + (-CAM_H + AMP * bandHist(uca, k)) * sk;
        float yb = vy + (-CAM_H + AMP * bandHist(ucb, k)) * sk;
        float lo = min(ya, yb), hi = max(ya, yb);

        // this row's line
        if (py + 0.5 >= lo && py - 0.5 <= hi && py > top)
        {
            c = heightColor(bandHist(0.5 * (uca + ucb), k)) * fade;
            break;
        }

        // the lines along the depth, from this row to the next one (hidden by this row and the nearer ones)
        float topNow = max(top, hi);
        if (k < HIST - 1 && py > topNow)
        {
            float sn = H / depthAt(k + 1);
            float uk0 = ((px - vx) / sk) / (2.0 * halfW) + 0.5;
            float uk1 = ((px - vx) / sn) / (2.0 * halfW) + 0.5;
            int j0 = (int)floor(uk0 * LINES + 0.5), j1 = (int)floor(uk1 * LINES + 0.5);
            bool hit = false;
            [unroll]
            for (int t = 0; t < 2; t++)
            {
                int j = (t == 0) ? j0 : j1;
                if (t == 1 && j1 == j0) break;
                if (j < 0 || j > LINES) continue;
                float u = j / (float)LINES;
                float va = bandHist(u, k), vb = bandHist(u, k + 1);
                float2 p0 = float2(vx + (u - 0.5) * 2.0 * halfW * sk, vy + (-CAM_H + AMP * va) * sk);
                float2 p1 = float2(vx + (u - 0.5) * 2.0 * halfW * sn, vy + (-CAM_H + AMP * vb) * sn);
                // the part of the segment inside this dot column
                float dx = p1.x - p0.x;
                float t0 = 0, t1 = 1;
                if (abs(dx) < 1e-4) { if (abs(p0.x - px) > 0.5) continue; }
                else
                {
                    float ta = (px - 0.5 - p0.x) / dx, tb = (px + 0.5 - p0.x) / dx;
                    t0 = max(min(ta, tb), 0.0); t1 = min(max(ta, tb), 1.0);
                    if (t0 > t1) continue;
                }
                float y0 = lerp(p0.y, p1.y, t0), y1 = lerp(p0.y, p1.y, t1);
                if (py + 0.5 >= min(y0, y1) && py - 0.5 <= max(y0, y1))
                {
                    c = heightColor(0.5 * (va + vb)) * fade;
                    hit = true; break;
                }
            }
            if (hit) break;
        }
        top = topNow;
        if (top >= py) break;
    }
    return float4(c, 1);
}
