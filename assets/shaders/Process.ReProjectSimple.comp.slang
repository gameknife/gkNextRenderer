import Common;
import Bindless;

struct PushConsts
{
    // Unified push-constant header (offset 0..15): aliases gpuScene's header so
    // gpuScene.custom_data_0 (active view RT bank base) is valid in this custom pass. Params follow.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_custom_data_0;
    uint _ViewHdr_custom_data_1;
    uint _ViewHdr_custom_data_2;

    uint TemporalFrames;
    uint PrevDiffuseBindlessIdx;
    uint HistoryValid;
    uint TAAEnabled;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_INSTANCE_MASK = 0x07FFFFFFu;
static const uint OBJECT_ID_INVALID_INSTANCE = 0xFFFFFFFFu;
static const float3 kLumaWeights = float3(0.212671f, 0.715160f, 0.072169f);

uint FetchPrimitiveIndex(uint objectId)
{
    return (objectId & OBJECT_ID_FLAG_VALID) != 0
        ? (objectId & OBJECT_ID_INSTANCE_MASK)
        : OBJECT_ID_INVALID_INSTANCE;
}

float3 FireflyClamp(RWTexture2D<float4> src, RWTexture2D<uint> objId, int2 ipos, int2 imageMax,
                    uint centerPrim, float3 center, float k)
{
    float lumSum = 0.0;
    float lumSqSum = 0.0;
    float count = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            const int2 p = clamp(ipos + int2(x, y), int2(0, 0), imageMax);
            if (FetchPrimitiveIndex(objId[p]) != centerPrim)
            {
                continue;
            }

            const float l = dot(src[p].rgb, kLumaWeights);
            lumSum += l;
            lumSqSum += l * l;
            count += 1.0;
        }
    }

    if (count < 2.0)
    {
        return center;
    }

    const float mean = lumSum / count;
    const float sigma = sqrt(max(0.0, lumSqSum / count - mean * mean));
    const float lumCenter = dot(center, kLumaWeights);
    const float lumMax = mean + k * sigma + 1e-4;
    if (lumCenter > lumMax)
    {
        center *= lumMax / max(lumCenter, 1e-5);
    }
    return center;
}

float ClampStrengthFromConfidence(float historyLen, float nMax)
{
    const float histConf = saturate(historyLen / max(nMax, 1.0));
    return saturate(1.0 - histConf);
}

float3 ClampHistorySoft(RWTexture2D<float4> currentImage, int2 ipos, int2 imageMax, float3 history,
                        float3 filteredCur, float strength)
{
    float lumSum = 0.0;
    float lumSqSum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 p = clamp(ipos + int2(x, y), int2(0, 0), imageMax);
            const float l = dot(currentImage[p].rgb, kLumaWeights);
            lumSum += l;
            lumSqSum += l * l;
        }
    }

    const float lumMean = lumSum / 9.0;
    const float sigma = sqrt(max(0.0, lumSqSum / 9.0 - lumMean * lumMean));

    const float3 hy = rgb2ycocg(history);
    const float3 cy = rgb2ycocg(filteredCur);

    const float gammaHiY = lerp(8.0, 2.5, strength);
    const float gammaLoY = lerp(8.0, 5.0, strength);
    const float gammaC = lerp(12.0, 6.0, strength);

    const float3 lo = float3(cy.x - gammaLoY * sigma, cy.yz - gammaC * sigma);
    const float3 hi = float3(cy.x + gammaHiY * sigma, cy.yz + gammaC * sigma);
    float3 clamped = clamp(hy, lo, hi);
    clamped.x = max(clamped.x, cy.x * lerp(0.9, 0.5, strength));

    return ycocg2rgb(lerp(hy, clamped, strength));
}

float calculateWeight(float centerDist, bool sameObject, bool isCenter, float3 normal, float3 centerNormal)
{
    if (isCenter)
    {
        return 0.4;
    }
    if (!sameObject)
    {
        return 0.0;
    }

    float normalDot = clamp(dot(normal, centerNormal), 0.0f, 1.0f);
    const float normalThreshold = 0.98f;
    if (normalDot < normalThreshold)
    {
        return 0.0;
    }

    const float normalWeight = (normalDot - normalThreshold) / (1.0 - normalThreshold);
    return normalWeight * 2.0 / (centerDist * 1.5 + 4.0);
}

float3 FilterHistoryColor(RWTexture2D<float4> prevFrameTexture, float2 subpixel, int2 previpos,
                          uint currentPrimitiveIndex,
                          uint prevPrimitiveIndex0,
                          uint prevPrimitiveIndex1,
                          uint prevPrimitiveIndex2,
                          uint prevPrimitiveIndex3,
                          float4 currFrameSample)
{
    float3 historyColor[4];
    historyColor[0] = currentPrimitiveIndex == prevPrimitiveIndex0 ? prevFrameTexture[previpos].rgb : currFrameSample.rgb;
    historyColor[1] = currentPrimitiveIndex == prevPrimitiveIndex1 ? prevFrameTexture[previpos + int2(1, 0)].rgb : currFrameSample.rgb;
    historyColor[2] = currentPrimitiveIndex == prevPrimitiveIndex2 ? prevFrameTexture[previpos + int2(0, 1)].rgb : currFrameSample.rgb;
    historyColor[3] = currentPrimitiveIndex == prevPrimitiveIndex3 ? prevFrameTexture[previpos + int2(1, 1)].rgb : currFrameSample.rgb;

    float3 history = lerp(lerp(historyColor[0], historyColor[1], subpixel.x),
                          lerp(historyColor[2], historyColor[3], subpixel.x),
                          subpixel.y);
    return clamp(history, 0.0, 1600.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 ipos = int2(DTid.xy);
    uint2 isize = Bindless.GetStorageTextureDimensions<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_SINGLE_DIFFUSE));
    int2 imageSize = int2(isize);
    int2 imageMax = imageSize - int2(1, 1);
    if (ipos.x >= imageSize.x || ipos.y >= imageSize.y)
    {
        return;
    }

    let SourceDiffuse = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_SINGLE_DIFFUSE));
    let PrevDiffuse = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, int(pushConsts.PrevDiffuseBindlessIdx)));
    let AccumulateDiffuse = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_ACCUMLATE_DIFFUSE));
    let ObjectId0 = Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_OBJEDCTID_0));
    let ObjectId1 = Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_OBJEDCTID_1));
    let MotionVector = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_MOTIONVECTOR));
    let MotionMoment = Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_MOTIONMOMENT));
    let NormalBuffer = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_custom_data_0, Bindless.RT_NORMAL));

    float4 currDiffuseSample = SourceDiffuse[ipos];
    uint currentPrimitiveIndex = FetchPrimitiveIndex(ObjectId0[ipos]);
    currDiffuseSample.rgb = FireflyClamp(SourceDiffuse, ObjectId0, ipos, imageMax,
                                         currentPrimitiveIndex, currDiffuseSample.rgb, 3.0f);

    float2 motion = MotionVector[ipos].rg;
    int2 previpos = int2(floor(float2(ipos) + motion));
    const bool inside = all(previpos >= int2(0, 0)) &&
                        all(previpos + int2(1, 1) < imageSize);
    uint prevPrimitiveIndex0 = OBJECT_ID_INVALID_INSTANCE;
    if (inside)
    {
        prevPrimitiveIndex0 = FetchPrimitiveIndex(ObjectId1[previpos]);
    }

    const bool miss = currentPrimitiveIndex == OBJECT_ID_INVALID_INSTANCE;
    bool useHistory = pushConsts.HistoryValid != 0 &&
                      pushConsts.TAAEnabled != 0 &&
                      !miss &&
                      inside &&
                      MotionMoment[ipos] == 0;

    if (useHistory)
    {
        const int sampleRange = 2;
        const float3 centerNormal = NormalBuffer[ipos].rgb;

        float4 spatialDiffuseSample = float4(0, 0, 0, 0);
        float totalWeight = 0.0;
        float motionInRange = 0.0;

        for (int y = -sampleRange; y <= sampleRange; ++y)
        {
            for (int x = -sampleRange; x <= sampleRange; ++x)
            {
                const int2 offset = int2(x, y);
                const int2 samplePos = clamp(ipos + offset, int2(0, 0), imageMax);
                const int2 motionSamplePos = clamp(ipos + offset * 2, int2(0, 0), imageMax);
                const uint primitiveIdx = FetchPrimitiveIndex(ObjectId0[samplePos]);
                const float3 worldNormal = NormalBuffer[samplePos].rgb;

                const bool isCenter = x == 0 && y == 0;
                const bool sameObject = primitiveIdx == currentPrimitiveIndex;
                const float centerDist = length(float2(x, y));
                const float weight = calculateWeight(centerDist, sameObject, isCenter, worldNormal, centerNormal);

                spatialDiffuseSample += SourceDiffuse[samplePos] * weight;
                totalWeight += weight;
                motionInRange += length(MotionVector[motionSamplePos].rg);
            }
        }
        spatialDiffuseSample /= max(totalWeight, 1e-5);

        uint prevPrimitiveIndex1 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 0)]);
        uint prevPrimitiveIndex2 = FetchPrimitiveIndex(ObjectId1[previpos + int2(0, 1)]);
        uint prevPrimitiveIndex3 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 1)]);

        if (motionInRange < 0.1)
        {
            prevPrimitiveIndex0 = currentPrimitiveIndex;
            prevPrimitiveIndex1 = currentPrimitiveIndex;
            prevPrimitiveIndex2 = currentPrimitiveIndex;
            prevPrimitiveIndex3 = currentPrimitiveIndex;
        }

        float2 subpixel = frac(float2(ipos) + motion);
        float3 historyDiffuse = FilterHistoryColor(PrevDiffuse, subpixel, previpos,
                                                   currentPrimitiveIndex,
                                                   prevPrimitiveIndex0,
                                                   prevPrimitiveIndex1,
                                                   prevPrimitiveIndex2,
                                                   prevPrimitiveIndex3,
                                                   spatialDiffuseSample);

        const float nMax = float(max(1u, pushConsts.TemporalFrames));
        const float prevHistlen = clamp(PrevDiffuse[previpos].a, 0.0, nMax);
        const float historyLen = prevPrimitiveIndex0 == currentPrimitiveIndex
            ? min(prevHistlen + 1.0, nMax)
            : 1.0;
        const float currKeep = clamp(1.0 / historyLen, 0.0, 1.0);

        const float clampStrength = ClampStrengthFromConfidence(historyLen, nMax);
        historyDiffuse = ClampHistorySoft(SourceDiffuse, ipos, imageMax, historyDiffuse,
                                          spatialDiffuseSample.rgb, clampStrength);

        const float kSpatialFallbackFrames = 6.0;
        const float spatialFallback = saturate(1.0 - (historyLen - 1.0) / kSpatialFallbackFrames);
        const float3 currDiffuseEst = lerp(currDiffuseSample.rgb, spatialDiffuseSample.rgb, spatialFallback);

        AccumulateDiffuse[ipos] = float4(lerp(historyDiffuse, currDiffuseEst, currKeep), historyLen);
    }
    else
    {
        AccumulateDiffuse[ipos] = float4(currDiffuseSample.rgb, 1.0);
    }
}
