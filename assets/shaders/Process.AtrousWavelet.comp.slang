// Variance-guided a-trous wavelet filter (SVGF-style), Phase 2 of the denoiser plan.
//
// Replaces the single-pass JBF spatial filter with N iterations of an edge-stopping 5x5
// B-spline wavelet at exponentially increasing stride (1,2,4,8,16). Each iteration filters
// the demodulated diffuse and propagates a per-pixel luminance variance, so converged
// regions are left sharp while noisy GI is aggressively smoothed.
//
// Variance source: estimated spatially on the first iteration (appropriate for demodulated
// lighting, which carries little real high-frequency detail) and propagated thereafter.
// Temporal-moment variance (true SVGF Phase 1) is a later refinement.
//
// Runs as a custom-push-constant compute pass (no camera UBO); all signals come from the
// bindless G-buffer. Diffuse + variance are packed as RGBA16F: rgb = color, a = variance.

import Common;
import Bindless;

struct PushConsts
{
    uint InColorSlot;       // rgb = diffuse, a = variance (variance unused when FirstIteration)
    uint OutSlot;           // rgb = filtered diffuse, a = variance
    uint StepSize;          // 1 << iteration
    uint FirstIteration;    // 1 => seed variance spatially instead of reading In.a

    float SigmaDepth;       // planar depth tolerance (multiples of local depth slope)
    float SigmaLuma;        // luminance edge-stop sigma (variance-normalized)
    float SigmaNormalPower; // normal edge-stop exponent

    uint IsSpecular;            // 1 => apply the roughness-scaled footprint below
    float SpecFootprintScale;   // pixels of filter radius per unit roughness
    float Pad0;
    float Pad1;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_INSTANCE_MASK = 0x07FFFFFFu;
static const uint OBJECT_ID_INVALID_INSTANCE = 0xFFFFFFFFu;
static const float3 kLumaWeights = float3(0.212671f, 0.715160f, 0.072169f);

// B3-spline a-trous kernel (1D), outer-producted into a 5x5 footprint.
static const float kKernel[5] = { 1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };

uint FetchPrimitiveIndex(uint objectId)
{
    return (objectId & OBJECT_ID_FLAG_VALID) != 0 ? (objectId & OBJECT_ID_INSTANCE_MASK) : OBJECT_ID_INVALID_INSTANCE;
}

float Luminance(float3 c)
{
    return dot(c, kLumaWeights);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let InColor = Bindless.GetStorageTexture<float4>(pushConsts.InColorSlot);
    let OutColor = Bindless.GetStorageTexture<float4>(pushConsts.OutSlot);
    let NormalBuffer = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL);
    let DepthBuffer = Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER);
    let ObjectId0 = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);

    const uint2 isize = Bindless.GetStorageTextureDimensions<float4>(pushConsts.InColorSlot);
    const int2 imageMax = int2(isize) - int2(1, 1);
    const int2 ipos = int2(DTid.xy);
    if (any(ipos > imageMax))
    {
        return;
    }

    const float4 centerNormalRough = NormalBuffer[ipos];
    const float3 centerColor = InColor[ipos].rgb;
    const float3 centerNormal = centerNormalRough.rgb;
    const float centerDepth = DepthBuffer[ipos];
    const uint centerObj = FetchPrimitiveIndex(ObjectId0[ipos].r);
    const float centerLum = Luminance(centerColor);

    // Specular footprint (Phase 4): the normal buffer alpha carries roughness. Glossy/near-mirror
    // surfaces must keep a tight filter radius or coherent reflections get smeared; rough surfaces
    // get the full diffuse-style footprint. Radius is in pixels and scales linearly with roughness.
    const float specRadius = (pushConsts.IsSpecular != 0)
        ? max(1.0, centerNormalRough.w * pushConsts.SpecFootprintScale)
        : 0.0;
    const float invTwoSpecRadiusSq = (specRadius > 0.0) ? 1.0 / (2.0 * specRadius * specRadius) : 0.0;

    // Local screen-space depth slope, used to keep slanted surfaces tolerant (SVGF phiDepth).
    const float gradX = abs(DepthBuffer[clamp(ipos + int2(1, 0), int2(0, 0), imageMax)] -
                            DepthBuffer[clamp(ipos - int2(1, 0), int2(0, 0), imageMax)]) * 0.5;
    const float gradY = abs(DepthBuffer[clamp(ipos + int2(0, 1), int2(0, 0), imageMax)] -
                            DepthBuffer[clamp(ipos - int2(0, 1), int2(0, 0), imageMax)]) * 0.5;
    const float gradDepth = max(gradX, gradY);

    // Variance for the luminance edge-stop. First iteration estimates it spatially over a
    // geometry-weighted 7x7 window; later iterations read the propagated value.
    float centerVar;
    if (pushConsts.FirstIteration != 0)
    {
        float wSum = 0.0;
        float lumSum = 0.0;
        float lumSqSum = 0.0;
        for (int y = -3; y <= 3; ++y)
        {
            for (int x = -3; x <= 3; ++x)
            {
                const int2 p = clamp(ipos + int2(x, y), int2(0, 0), imageMax);
                const uint obj = FetchPrimitiveIndex(ObjectId0[p].r);
                if (obj != centerObj)
                {
                    continue;
                }
                const float nw = max(0.0, dot(centerNormal, NormalBuffer[p].rgb));
                const float l = Luminance(InColor[p].rgb);
                lumSum += nw * l;
                lumSqSum += nw * l * l;
                wSum += nw;
            }
        }
        const float invW = 1.0 / max(wSum, 1e-5);
        const float mean = lumSum * invW;
        centerVar = max(0.0, lumSqSum * invW - mean * mean);

        // Disocclusion recovery (Phase 3): the diffuse accumulation alpha carries historyLen.
        // Young pixels (<4 frames of temporal averaging) inflate their variance to force a wider
        // effective filter footprint until accumulation catches up; converged pixels are untouched.
        const float historyLen = max(InColor[ipos].a, 1.0);
        centerVar *= max(1.0, 4.0 / historyLen);
    }
    else
    {
        centerVar = InColor[ipos].a;
    }

    const float lumPhi = pushConsts.SigmaLuma * sqrt(max(centerVar, 0.0)) + 1e-4;

    float3 sumColor = float3(0, 0, 0);
    float sumWeight = 0.0;
    float sumVar = 0.0;

    const int step = int(pushConsts.StepSize);
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            const int2 offset = int2(dx, dy) * step;
            const int2 p = clamp(ipos + offset, int2(0, 0), imageMax);

            const uint objQ = FetchPrimitiveIndex(ObjectId0[p].r);
            if (objQ != centerObj)
            {
                continue; // hard object-id rejection
            }

            const float4 inQ = InColor[p];
            const float3 colorQ = inQ.rgb;
            const float lumQ = Luminance(colorQ);
            const float3 normalQ = NormalBuffer[p].rgb;
            const float depthQ = DepthBuffer[p];

            const float h = kKernel[dx + 2] * kKernel[dy + 2];
            const float wN = pow(max(0.0, dot(centerNormal, normalQ)), pushConsts.SigmaNormalPower);
            const float phiDepth = pushConsts.SigmaDepth * gradDepth * length(float2(offset)) + 1e-5;
            const float wZ = exp(-abs(centerDepth - depthQ) / phiDepth);
            const float wL = exp(-abs(centerLum - lumQ) / lumPhi);
            // Roughness-limited spatial footprint for specular (1.0 for diffuse).
            const float wRough = (invTwoSpecRadiusSq > 0.0)
                ? exp(-dot(float2(offset), float2(offset)) * invTwoSpecRadiusSq)
                : 1.0;

            const float w = h * wN * wZ * wL * wRough;
            const float varQ = (pushConsts.FirstIteration != 0) ? centerVar : inQ.a;

            sumColor += w * colorQ;
            sumWeight += w;
            sumVar += w * w * varQ;
        }
    }

    const float invSum = 1.0 / max(sumWeight, 1e-5);
    const float3 outColor = sumColor * invSum;
    // Variance propagates with the squared weights (SVGF). On the first iteration we keep the
    // per-pixel spatial estimate so neighbours have a variance field to propagate next pass.
    const float outVar = (pushConsts.FirstIteration != 0)
        ? centerVar
        : sumVar / max(sumWeight * sumWeight, 1e-5);

    OutColor[ipos] = float4(outColor, outVar);
}
