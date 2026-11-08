// name: XY Scope
// pixel: 1
//
// An XY oscilloscope: the left channel moves the beam sideways, the right channel moves it up (waveLR).
// The 512 samples are joined by a 1-dot line. A mono sound (left = right) is a 45 degree line;
// left and right a quarter turn apart make a circle; other shifts make ellipses.
// The plot area is a square in the middle of the pane (not stretched to the pane's shape), drawn with a
// faint frame and cross. A light afterimage (prevFrame) fades behind the beam.

static const float3 BEAM  = float3(0.30, 1.00, 0.50);
static const float3 GUIDE = float3(0.12, 0.12, 0.12);
static const float  FADE  = 0.82;     // how much of the previous frame is kept (afterimage)

// distance from point p to the segment a-b (in dots)
float distSeg(float2 p, float2 a, float2 b)
{
    float2 ab = b - a;
    float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
    return length(p - (a + ab * t));
}

float4 mainImage(float2 fragCoord)
{
    float side = min(iResolution.x, iResolution.y);
    float2 mid = floor(0.5 * iResolution) + 0.5;       // the center dot (so the cross and the 45 degree line are exactly 1 dot wide)
    float H = floor((side - 1.0) * 0.5);               // the frame is H dots from the center dot
    float R = H - 1.0;                                 // the beam stays inside the frame
    float2 p = fragCoord - mid;                        // this dot, measured from the center dot
    float edge = max(abs(p.x), abs(p.y));              // distance to the center in the square's own (max) norm

    // afterimage: last frame, faded. The small subtraction makes it reach exact black (8-bit rounding)
    float3 c = max(prevFrame(fragCoord / iResolution) * FADE - 1.5 / 255.0, 0.0);

    // faint frame and cross of the square
    if ((edge > R && edge <= H) || (edge <= H && (abs(p.x) <= 0.5 || abs(p.y) <= 0.5)))
        c = max(c, GUIDE);

    // the beam: only dots inside the square can be touched, so skip the loop for the others
    if (edge <= H)
    {
        float2 a = waveLR(0.0) * R;
        float dmin = 1e9;
        [loop]
        for (int i = 1; i < 512; i++)
        {
            float2 b = waveLR(i / 511.0) * R;          // sample i: x = left, y = right (the origin is bottom-left, so +y is up)
            dmin = min(dmin, distSeg(p, a, b));
            a = b;
        }
        if (dmin < 0.6) c = BEAM;
    }
    return float4(c, 1);
}
