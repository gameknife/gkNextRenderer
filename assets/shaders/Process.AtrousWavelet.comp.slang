// Variance-guided a-trous wavelet filter (SVGF-style).
//
// This fused pass filters diffuse and specular in the same dispatch. Geometry edge-stop
// inputs (normal/depth/object id) are shared, specular can run fewer iterations than diffuse,
// and the first-iteration variance seed is a cheaper shared 3x3 estimate.

import Shader.Bindless;

struct PushConsts
{
    // Unified push-constant header (offset 0..15): aliases gpuScene's header so
    // gpuScene.custom_data_0 (active view RT bank base) is valid in this custom pass. Params follow.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_custom_data_0;
    uint _ViewHdr_custom_data_1;
    uint _ViewHdr_custom_data_2;

    uint DiffuseInSlot;     // rgb = diffuse, a = variance/historyLen on first iteration
    uint DiffuseOutSlot;    // rgb = filtered diffuse, a = variance
    uint SpecularInSlot;    // rgb = specular, a = variance/historyLen on first iteration
    uint SpecularOutSlot;   // rgb = filtered specular, a = variance

    uint StepSize;          // 1 << iteration
    uint FirstIteration;    // 1 => seed variance spatially instead of reading In.a
    uint FilterDiffuse;     // 1 => write diffuse output this iteration
    uint FilterSpecular;    // 1 => write specular output this iteration

    float SigmaDepth;       // planar depth tolerance (multiples of local depth slope)
    float SigmaLuma;        // luminance edge-stop sigma (variance-normalized)
    float SigmaNormalPower; // normal edge-stop sharpness
    float SpecFootprintScale; // pixels of filter radius per unit roughness
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_INSTANCE_MASK = 0x07FFFFFFu;
static const uint OBJECT_ID_INVALID_INSTANCE = 0xFFFFFFFFu;
static const float3 kLumaWeights = float3(0.212671f, 0.715160f, 0.072169f);

static const float kKernel2D[25] =
{
    0.00390625f, 0.01562500f, 0.02343750f, 0.01562500f, 0.00390625f,
    0.01562500f, 0.06250000f, 0.09375000f, 0.06250000f, 0.01562500f,
    0.02343750f, 0.09375000f, 0.14062500f, 0.09375000f, 0.02343750f,
    0.01562500f, 0.06250000f, 0.09375000f, 0.06250000f, 0.01562500f,
    0.00390625f, 0.01562500f, 0.02343750f, 0.01562500f, 0.00390625f,
};

static const float kOffsetLengthBase[25] =
{
    2.82842712f, 2.23606798f, 2.00000000f, 2.23606798f, 2.82842712f,
    2.23606798f, 1.41421356f, 1.00000000f, 1.41421356f, 2.23606798f,
    2.00000000f, 1.00000000f, 0.00000000f, 1.00000000f, 2.00000000f,
    2.23606798f, 1.41421356f, 1.00000000f, 1.41421356f, 2.23606798f,
    2.82842712f, 2.23606798f, 2.00000000f, 2.23606798f, 2.82842712f,
};

static const float kOffsetDistSqBase[25] =
{
    8.0f, 5.0f, 4.0f, 5.0f, 8.0f,
    5.0f, 2.0f, 1.0f, 2.0f, 5.0f,
    4.0f, 1.0f, 0.0f, 1.0f, 4.0f,
    5.0f, 2.0f, 1.0f, 2.0f, 5.0f,
    8.0f, 5.0f, 4.0f, 5.0f, 8.0f,
};

uint FetchPrimitiveIndex(uint objectId)
{
    return (objectId & OBJECT_ID_FLAG_VALID) != 0 ? (objectId & OBJECT_ID_INSTANCE_MASK) : OBJECT_ID_INVALID_INSTANCE;
}

float Luminance(float3 c)
{
    return dot(c, kLumaWeights);
}

float FastExpNeg(float x)
{
    const float v = max(x, 0.0f);
    const float v2 = v * v;
    return 1.0f / (1.0f + v + 0.48f * v2 + 0.235f * v2 * v);
}

float FastNormalWeight(float dotN, float power)
{
    const float clampedDot = saturate(dotN);
    if (power <= 1.0f)
    {
        return clampedDot;
    }

    const float width = min(1.0f, 2.0f / max(power, 1.0f));
    return smoothstep(max(0.0f, 1.0f - width), 1.0f, clampedDot);
}

float EstimateSpatialVariance3x3(
    RWTexture2D<float4> InColor,
    RWTexture2D<float4> NormalBuffer,
    RWTexture2D<uint> ObjectId0,
    int2 ipos,
    int2 imageMax,
    float3 centerNormal,
    uint centerObj)
{
    float wSum = 0.0f;
    float lumSum = 0.0f;
    float lumSqSum = 0.0f;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 p = clamp(ipos + int2(x, y), int2(0, 0), imageMax);
            const uint obj = FetchPrimitiveIndex(ObjectId0[p].r);
            if (obj != centerObj)
            {
                continue;
            }

            const float nw = max(0.0f, dot(centerNormal, NormalBuffer[p].rgb));
            const float l = Luminance(InColor[p].rgb);
            lumSum += nw * l;
            lumSqSum += nw * l * l;
            wSum += nw;
        }
    }

    const float invW = 1.0f / max(wSum, 1e-5f);
    const float mean = lumSum * invW;
    float centerVar = max(0.0f, lumSqSum * invW - mean * mean);

    const float historyLen = max(InColor[ipos].a, 1.0f);
    centerVar *= max(1.0f, 4.0f / historyLen);
    return centerVar;
}

float4 FilterAtrousChannel(
    RWTexture2D<float4> InColor,
    RWTexture2D<float4> NormalBuffer,
    RWTexture2D<float> DepthBuffer,
    RWTexture2D<uint> ObjectId0,
    int2 ipos,
    int2 imageMax,
    float3 centerNormal,
    float centerDepth,
    uint centerObj,
    float gradDepth,
    float sharedFirstVariance,
    bool firstIteration,
    bool isSpecular,
    float centerRoughness)
{
    const float4 centerIn = InColor[ipos];
    const float3 centerColor = centerIn.rgb;
    const float centerLum = Luminance(centerColor);
    const float centerVar = firstIteration ? sharedFirstVariance : centerIn.a;
    const float lumPhi = pushConsts.SigmaLuma * sqrt(max(centerVar, 0.0f)) + 1e-4f;

    const float specRadius = isSpecular
        ? max(1.0f, centerRoughness * pushConsts.SpecFootprintScale)
        : 0.0f;
    const float invTwoSpecRadiusSq = (specRadius > 0.0f) ? 1.0f / (2.0f * specRadius * specRadius) : 0.0f;

    float3 sumColor = float3(0, 0, 0);
    float sumWeight = 0.0f;
    float sumVar = 0.0f;

    const int step = int(pushConsts.StepSize);
    const float stepF = float(step);
    const float stepSq = stepF * stepF;

    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            const int kernelIndex = (dy + 2) * 5 + (dx + 2);
            const int2 offset = int2(dx, dy) * step;
            const int2 p = clamp(ipos + offset, int2(0, 0), imageMax);

            const uint objQ = FetchPrimitiveIndex(ObjectId0[p].r);
            if (objQ != centerObj)
            {
                continue;
            }

            const float4 inQ = InColor[p];
            const float3 colorQ = inQ.rgb;
            const float lumQ = Luminance(colorQ);
            const float3 normalQ = NormalBuffer[p].rgb;
            const float depthQ = DepthBuffer[p];

            const float h = kKernel2D[kernelIndex];
            const float wN = FastNormalWeight(dot(centerNormal, normalQ), pushConsts.SigmaNormalPower);
            const float phiDepth = pushConsts.SigmaDepth * gradDepth * kOffsetLengthBase[kernelIndex] * stepF + 1e-5f;
            const float wZ = FastExpNeg(abs(centerDepth - depthQ) / phiDepth);
            const float wL = FastExpNeg(abs(centerLum - lumQ) / lumPhi);
            const float wRough = (invTwoSpecRadiusSq > 0.0f)
                ? FastExpNeg(kOffsetDistSqBase[kernelIndex] * stepSq * invTwoSpecRadiusSq)
                : 1.0f;

            const float w = h * wN * wZ * wL * wRough;
            const float varQ = firstIteration ? centerVar : inQ.a;

            sumColor += w * colorQ;
            sumWeight += w;
            sumVar += w * w * varQ;
        }
    }

    const float invSum = 1.0f / max(sumWeight, 1e-5f);
    const float3 outColor = sumColor * invSum;
    const float outVar = firstIteration
        ? centerVar
        : sumVar / max(sumWeight * sumWeight, 1e-5f);

    return float4(outColor, outVar);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const bool filterDiffuse = pushConsts.FilterDiffuse != 0;
    const bool filterSpecular = pushConsts.FilterSpecular != 0;
    if (!filterDiffuse && !filterSpecular)
    {
        return;
    }

    // Dynamic slot values are absolute screen-space slots; resolve into the active view's bank.
    let vb = pushConsts._ViewHdr_custom_data_0;
    let DiffuseIn = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, int(pushConsts.DiffuseInSlot)));
    let DiffuseOut = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, int(pushConsts.DiffuseOutSlot)));
    let SpecularIn = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, int(pushConsts.SpecularInSlot)));
    let SpecularOut = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, int(pushConsts.SpecularOutSlot)));
    let NormalBuffer = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_NORMAL));
    let DepthBuffer = Bindless.GetStorageTexture<float>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_PREV_DEPTHBUFFER));
    let ObjectId0 = Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_OBJEDCTID_0));

    const uint2 isize = Bindless.GetStorageTextureDimensions<float4>(Bindless.ViewRT(vb, int(filterDiffuse ? pushConsts.DiffuseInSlot : pushConsts.SpecularInSlot)));
    const int2 imageMax = int2(isize) - int2(1, 1);
    const int2 ipos = int2(DTid.xy);
    if (any(ipos > imageMax))
    {
        return;
    }

    const float4 centerNormalRough = NormalBuffer[ipos];
    const float3 centerNormal = centerNormalRough.rgb;
    const float centerDepth = DepthBuffer[ipos];
    const uint centerObj = FetchPrimitiveIndex(ObjectId0[ipos].r);

    const float gradX = abs(DepthBuffer[clamp(ipos + int2(1, 0), int2(0, 0), imageMax)] -
                            DepthBuffer[clamp(ipos - int2(1, 0), int2(0, 0), imageMax)]) * 0.5f;
    const float gradY = abs(DepthBuffer[clamp(ipos + int2(0, 1), int2(0, 0), imageMax)] -
                            DepthBuffer[clamp(ipos - int2(0, 1), int2(0, 0), imageMax)]) * 0.5f;
    const float gradDepth = max(gradX, gradY);

    const bool firstIteration = pushConsts.FirstIteration != 0;
    float sharedFirstVariance = 0.0f;
    if (firstIteration)
    {
        sharedFirstVariance = filterDiffuse
            ? EstimateSpatialVariance3x3(DiffuseIn, NormalBuffer, ObjectId0, ipos, imageMax, centerNormal, centerObj)
            : EstimateSpatialVariance3x3(SpecularIn, NormalBuffer, ObjectId0, ipos, imageMax, centerNormal, centerObj);
    }

    if (filterDiffuse)
    {
        DiffuseOut[ipos] = FilterAtrousChannel(
            DiffuseIn,
            NormalBuffer,
            DepthBuffer,
            ObjectId0,
            ipos,
            imageMax,
            centerNormal,
            centerDepth,
            centerObj,
            gradDepth,
            sharedFirstVariance,
            firstIteration,
            false,
            centerNormalRough.w);
    }

    if (filterSpecular)
    {
        SpecularOut[ipos] = FilterAtrousChannel(
            SpecularIn,
            NormalBuffer,
            DepthBuffer,
            ObjectId0,
            ipos,
            imageMax,
            centerNormal,
            centerDepth,
            centerObj,
            gradDepth,
            sharedFirstVariance,
            firstIteration,
            true,
            centerNormalRough.w);
    }
}
