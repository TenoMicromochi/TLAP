// name: Ring Tunnel
// pixel: 2
//
// Each row of the spectrum history (bandHist) is a ring, and the rings rush at you out of the depths of a tunnel
// (age 0 is the nearest). The radius of a ring swells with the spectrum around it: the 64 bands run along half
// the circle and the other half is the mirror image, so there is no seam. Rings are true circles.
// The vanishing point stays in the middle. Farther rings are darker.
// The field of view is set by the canvas HEIGHT, so changing the width never stretches the picture.

static const float R0     = 1.0;      // ring radius with no sound
static const float SWELL  = 0.55;     // extra radius at loudness 1.0
static const float D_NEAR = 0.7;
static const float D_FAR  = 9.0;

float ringR(float phi, int k)
{
    float t = abs(phi) / 3.14159265;                  // 0 (to the right) .. 1 (to the left), mirrored top and bottom
    return R0 + SWELL * bandHist(t, k);
}

float4 mainImage(float2 fragCoord)
{
    float H = iResolution.y;
    float2 vp = 0.5 * iResolution;                    // the vanishing point stays in the middle
    // the nearest ring at its fullest (R0 + SWELL) just fits the canvas height, 1.5 dots inside the top and bottom
    float scale = (0.5 * H - 1.5) / (R0 + SWELL) * D_NEAR;
    float2 p = fragCoord - vp;
    float rho = length(p);
    float phi = atan2(p.y, p.x);
    float dphi = 0.5 / max(rho, 1.0);                 // half a dot, as an angle

    float3 c = float3(0, 0, 0);
    [loop]
    for (int k = 0; k < HIST; k++)
    {
        float d = D_NEAR + (D_FAR - D_NEAR) * k / (HIST - 1.0);
        float s = scale / d;
        float r0 = s * ringR(phi, k);
        float ra = s * ringR(phi - dphi, k), rb = s * ringR(phi + dphi, k);
        float lo = min(r0, min(ra, rb)), hi = max(r0, max(ra, rb));
        if (rho + 0.5 >= lo && rho - 0.5 <= hi)
        {
            float fade = 0.05 + 0.95 * exp(-k / 40.0);
            float v = saturate(bandHist(abs(phi) / 3.14159265, k) * 1.4);
            c = max(c, lerp(float3(0.15, 0.45, 1.0), float3(0.55, 1.0, 1.0), v) * fade);
        }
    }
    return float4(c, 1);
}
