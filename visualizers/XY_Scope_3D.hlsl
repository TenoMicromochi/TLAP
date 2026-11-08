// name: XY Scope 3D
// pixel: 2
//
// The XY oscilloscope (left = across, right = up) flowing away into the distance like Wave Tunnel.
// Every few rows of the waveLRHist history are drawn as an XY figure in a square, at the same depths as Spectrum
// Terrain (1..7 over 128 rows, about 2.1 seconds): the newest is biggest and brightest, older ones shrink toward the
// vanishing point in the middle of the canvas and get darker and bluer. No hidden lines (the lines just overlap).
// A mono sound makes a 45 degree line, a quarter-turn phase shift makes a circle, and the figures form a tunnel.
// Figure squares are never stretched: the size is set by the canvas HEIGHT.
// Cost: one row in 8 is drawn (16 figures), each with 12..96 points, and a figure is skipped for dots outside its square.
// Rows are picked by their absolute number (_histHead - age is a multiple of 8) so they glide smoothly instead of jumping.

static const float3 BEAM   = float3(0.30, 1.00, 0.50);
static const float3 OLD    = float3(0.10, 0.45, 0.90);
static const float  D_NEAR = 1.0;
static const float  D_FAR  = 7.0;
static const int    STRIDE = 8;

float distSeg(float2 p, float2 a, float2 b)
{
    float2 ab = b - a;
    float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
    return length(p - (a + ab * t));
}

float4 mainImage(float2 fragCoord)
{
    float2 mid = floor(0.5 * iResolution) + 0.5;
    float2 p = fragCoord - mid;
    float Q = 0.5 * iResolution.y - 2.0;                      // half size of the nearest figure's square
    float e = max(abs(p.x), abs(p.y));
    int first = _histHead & (STRIDE - 1);                     // the rows whose number is a multiple of STRIDE

    float3 c = float3(0, 0, 0);
    [loop]
    for (int a = first; a < HIST; a += STRIDE)
    {
        float d = D_NEAR + (D_FAR - D_NEAR) * a / (HIST - 1.0);
        float S = Q / d;
        if (e > S + 1.0) continue;                            // outside this figure's square
        int n = (int)clamp(100.0 / d, 12.0, 96.0);
        float2 prev = waveLRHist(0.0, a) * S;
        float dmin = 1e9;
        [loop]
        for (int i = 1; i < n; i++)
        {
            float2 q = waveLRHist(i / (n - 1.0), a) * S;
            dmin = min(dmin, distSeg(p, prev, q));
            prev = q;
        }
        if (dmin < 0.6)
        {
            float f = exp(-a / 40.0);
            c = lerp(OLD, BEAM, f) * (0.10 + 0.90 * f);
            break;                                            // nearer figures come first, so this is the brightest
        }
    }
    return float4(c, 1);
}
