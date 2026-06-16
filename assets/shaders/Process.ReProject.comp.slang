import Common;
import Bindless;

struct PushConsts
{
    bool ProgressiveRender;
    int TemporalFrames;
    
    uint PrevDiffuseBindlessIdx;
    uint PrevSpecularBindlessIdx;
    uint PrevAlbedoBindlessIdx;

    bool FastReproject;
};

[[vk::push_constant]]
ConstantBuffer<PushConsts> pushConsts;

// Helper functions
static const uint OBJECT_ID_FLAG_VALID = 0x80000000u;
static const uint OBJECT_ID_INSTANCE_MASK = 0x07FFFFFFu;
static const uint OBJECT_ID_INVALID_INSTANCE = 0xFFFFFFFFu;

uint FetchPrimitiveIndex(uint objectId)
{
    return (objectId & OBJECT_ID_FLAG_VALID) != 0 ? (objectId & OBJECT_ID_INSTANCE_MASK) : OBJECT_ID_INVALID_INSTANCE;
}

static const float3 kLumaWeights = float3(0.212671f, 0.715160f, 0.072169f);

// Relative firefly suppression (G9). Estimate the neighborhood luminance statistics from the
// 8 surrounding taps *excluding the center*, then clamp a bright center down to neighborMean +
// k*sigma. Excluding the center is what makes isolated single-pixel fireflies catchable: if the
// center is included it inflates its own sigma and escapes the clamp. Only ever darkens, so
// genuine multi-pixel highlights (whose neighbors are also bright) survive untouched.
float3 FireflyClamp(RWTexture2D<float4> src, int2 ipos, int2 imageMax, float3 center, float k)
{
    float lumSum = 0.0;
    float lumSqSum = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }
            const int2 p = clamp(ipos + int2(x, y), int2(0, 0), imageMax);
            const float l = dot(src[p].rgb, kLumaWeights);
            lumSum += l;
            lumSqSum += l * l;
        }
    }
    const float mean = lumSum / 8.0;
    const float sigma = sqrt(max(0.0, lumSqSum / 8.0 - mean * mean));
    const float lumCenter = dot(center, kLumaWeights);
    const float lumMax = mean + k * sigma + 1e-4;
    if (lumCenter > lumMax)
    {
        center *= lumMax / max(lumCenter, 1e-5);
    }
    return center;
}

// Variance-based YCoCg history clipping (G5). Unlike the strict min/max AABB used by the
// SoftwareModern path, a 1spp path-traced neighborhood is too noisy for hard clamping, so we
// build a loose [mean - gamma*sigma, mean + gamma*sigma] box that only catches gross ghosting
// (moving highlights/shadows) while leaving temporal accumulation intact.
float3 ClampHistoryVariance(RWTexture2D<float4> currentImage, int2 ipos, int2 imageMax, float3 history, float gamma)
{
    float3 m1 = float3(0, 0, 0);
    float3 m2 = float3(0, 0, 0);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 p = clamp(ipos + int2(x, y), int2(0, 0), imageMax);
            const float3 c = rgb2ycocg(currentImage[p].rgb);
            m1 += c;
            m2 += c * c;
        }
    }
    const float3 mean = m1 / 9.0;
    const float3 sigma = sqrt(max(float3(0, 0, 0), m2 / 9.0 - mean * mean));
    const float3 lo = mean - gamma * sigma;
    const float3 hi = mean + gamma * sigma;
    return ycocg2rgb(clamp(rgb2ycocg(history), lo, hi));
}

float calculateWeight(float centerDist, bool sameObject, bool isCenter, float3 Normal, float3 CenterNormal)
{
    if (isCenter) {
        return 0.4;
    } else if (!sameObject) {
        return 0.0;
    } else {
        float normalDot = clamp(dot(Normal, CenterNormal), 0.0f, 1.0f);
        const float normalThreshold = 0.98f;
        if (normalDot < normalThreshold) {
            return 0.0;
        }
        const float normalWeight = (normalDot - normalThreshold) / (1.0 - normalThreshold);
        return normalWeight * 2.0 / (centerDist * 1.5 + 4.0);
    }
}

float3 FilterHistoryColor( RWTexture2D<float4> prevFrameTexture, float2 subpixel, int2 previpos,
                          uint current_primitive_index0,
                          uint prev_primitive_index0,
                          uint prev_primitive_index1,
                          uint prev_primitive_index2,
                          uint prev_primitive_index3,
                          float4 currFrameSample)
{
    float3 historyColor[4];
    historyColor[0] = current_primitive_index0 == prev_primitive_index0 ? prevFrameTexture[previpos].rgb : currFrameSample.rgb;
    historyColor[1] = current_primitive_index0 == prev_primitive_index1 ? prevFrameTexture[previpos + int2(1, 0)].rgb : currFrameSample.rgb;
    historyColor[2] = current_primitive_index0 == prev_primitive_index2 ? prevFrameTexture[previpos + int2(0, 1)].rgb : currFrameSample.rgb;
    historyColor[3] = current_primitive_index0 == prev_primitive_index3 ? prevFrameTexture[previpos + int2(1, 1)].rgb : currFrameSample.rgb;

    float3 history = lerp(lerp(historyColor[0], historyColor[1], subpixel.x),
                            lerp(historyColor[2], historyColor[3], subpixel.x),
                            subpixel.y);
    history = clamp(history, 0.0, 1600.0); // kill the negative valuse
    return history;
}

// A simple accumulation shader, reproject can be implemented here later.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 ipos = int2(DTid.xy);
    uint2 isize = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    int2 imageSize = int2(isize);
    int2 imageMax = imageSize - int2(1, 1);

    let SourceDiffuse = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
    let SourceSpecular = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR);
    let SourceAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO);

    let PrevDiffuse = Bindless.GetStorageTexture<float4>(pushConsts.PrevDiffuseBindlessIdx);
    let PrevSpecular = Bindless.GetStorageTexture<float4>(pushConsts.PrevSpecularBindlessIdx);
    let PrevAlbedo = Bindless.GetStorageTexture<float4>(pushConsts.PrevAlbedoBindlessIdx);

    let AccumlateDiffuse = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_DIFFUSE);
    let AccumlateSpecular = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_SPECULAR);
    let AccumlateAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_ALBEDO);

    let ObjectId0 = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);
    let ObjectId1 = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_1);
    let MotionVector = Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR);
    let NormalBuffer = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL);

    if (pushConsts.ProgressiveRender)
    {
        AccumlateDiffuse[ipos] = lerp(PrevDiffuse[ipos], SourceDiffuse[ipos], clamp(1.0 / 512.0, 0.0, 1.0));
        AccumlateSpecular[ipos] = lerp(PrevSpecular[ipos], SourceSpecular[ipos], clamp(1.0 / 512.0, 0.0, 1.0));
        AccumlateAlbedo[ipos] = lerp(PrevAlbedo[ipos], SourceAlbedo[ipos], clamp(1.0 / 512.0, 0.0, 1.0));
        return;
    }

    float4 currDiffuseSample = SourceDiffuse[ipos];
    float4 currSpecularSample = SourceSpecular[ipos];
    float4 currAlbedoSample = SourceAlbedo[ipos];

    // Firefly pre-clamp on the freshly traced lighting before it enters temporal accumulation.
    // Diffuse is clamped loosely (the a-trous pass recovers any over-suppression spatially);
    // specular is clamped tighter since it has no spatial denoiser yet (specular is Phase 4).
    currDiffuseSample.rgb = FireflyClamp(SourceDiffuse, ipos, imageMax, currDiffuseSample.rgb, 3.0f);
    currSpecularSample.rgb = FireflyClamp(SourceSpecular, ipos, imageMax, currSpecularSample.rgb, 1.5f);

    bool useHistory = true;
    float2 motion = MotionVector[ipos].rg;
    int2 previpos = int2(floor(float2(ipos) + motion));
    const bool inside = all(previpos >= int2(0, 0)) &&
                        all(previpos + int2(1, 1) < imageSize);
    uint current_primitive_index0 = FetchPrimitiveIndex(ObjectId0[ipos]);
    uint prev_primitive_index0 = OBJECT_ID_INVALID_INSTANCE;
    if (inside)
    {
        prev_primitive_index0 = FetchPrimitiveIndex(ObjectId1[previpos]);
    }
    const bool miss = current_primitive_index0 == OBJECT_ID_INVALID_INSTANCE;
    
    uint moment = Bindless.GetStorageTexture<uint>(Bindless.RT_MOTIONMOMENT)[ipos];

    if (miss || !inside || moment > 0)
    {
        useHistory = false;
    }

    if (useHistory)
    {
        const int SAMPLE_RANGE = pushConsts.FastReproject ? 0 : 2;
        const float3 CenterNormal = NormalBuffer[ipos].rgb;

        float4 spatialDiffuseSample = float4(0, 0, 0, 0);
        float4 spatialSpecularSample = float4(0, 0, 0, 0);

        float totalWeight = 0.0;
        float motionInRange = 0;

        for (int y = -SAMPLE_RANGE; y <= SAMPLE_RANGE; y++) {
            for (int x = -SAMPLE_RANGE; x <= SAMPLE_RANGE; x++) {
                const int2 offset = int2(x, y);
                const int2 samplePos = clamp(ipos + offset, int2(0, 0), imageMax);
                const int2 motionSamplePos = clamp(ipos + offset * 2, int2(0, 0), imageMax);
                const uint primitiveIdx = FetchPrimitiveIndex(ObjectId0[samplePos]);
                const float3 worldNormal = NormalBuffer[samplePos].rgb;

                const bool isCenter = (x == 0 && y == 0);
                const bool sameObject = (primitiveIdx == current_primitive_index0);
                const float centerDist = length(float2(x, y));
                const float weight = calculateWeight(centerDist, sameObject, isCenter, worldNormal, CenterNormal);

                spatialDiffuseSample += SourceDiffuse[samplePos] * weight;
                spatialSpecularSample += SourceSpecular[samplePos] * weight;
                totalWeight += weight;

                motionInRange += length(MotionVector[motionSamplePos].rg);
            }
        }
        spatialDiffuseSample /= max(totalWeight, 1e-5);
        spatialSpecularSample /= max(totalWeight, 1e-5);

        uint prev_primitive_index1 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 0)]);
        uint prev_primitive_index2 = FetchPrimitiveIndex(ObjectId1[previpos + int2(0, 1)]);
        uint prev_primitive_index3 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 1)]);

        if (length(motionInRange) < 0.1)
        {
            prev_primitive_index0 = current_primitive_index0;
            prev_primitive_index1 = current_primitive_index0;
            prev_primitive_index2 = current_primitive_index0;
            prev_primitive_index3 = current_primitive_index0;
        }

        float2 subpixel = frac(float2(ipos) + motion);
        float3 historyDiffuse = FilterHistoryColor( PrevDiffuse, subpixel, previpos,
                          current_primitive_index0,
                          prev_primitive_index0,
                          prev_primitive_index1,
                          prev_primitive_index2,
                          prev_primitive_index3,
                          spatialDiffuseSample);

        float3 historySpecular = FilterHistoryColor( PrevSpecular, subpixel, previpos,
                          current_primitive_index0,
                          prev_primitive_index0,
                          prev_primitive_index1,
                          prev_primitive_index2,
                          prev_primitive_index3,
                          spatialSpecularSample);

        float3 historyAlbedo = FilterHistoryColor( PrevAlbedo, subpixel, previpos,
                          current_primitive_index0,
                          prev_primitive_index0,
                          prev_primitive_index1,
                          prev_primitive_index2,
                          prev_primitive_index3,
                          currAlbedoSample);

        // Neighborhood clipping to fight ghosting of moving highlights/shadows. Albedo is a stable
        // demodulated material signal, so only the lighting channels are clipped.
        const float kHistoryClampGamma = 4.0f;
        historyDiffuse = ClampHistoryVariance(SourceDiffuse, ipos, imageMax, historyDiffuse, kHistoryClampGamma);
        historySpecular = ClampHistoryVariance(SourceSpecular, ipos, imageMax, historySpecular, kHistoryClampGamma);

        // Adaptive temporal blend (Phase 3): historyLen is carried in the diffuse accumulation
        // alpha. Young / freshly-disoccluded pixels (small historyLen) use a large alpha to
        // converge fast; once historyLen saturates at TemporalFrames the blend matches the old
        // fixed 1/TemporalFrames behaviour. historyLen resets when the primary reprojected tap
        // crosses an object boundary.
        const float nMax = float(max(1u, pushConsts.TemporalFrames));
        const float prevHistlen = clamp(PrevDiffuse[previpos].a, 0.0, nMax);
        const float historyLen = (prev_primitive_index0 == current_primitive_index0)
            ? min(prevHistlen + 1.0, nMax)
            : 1.0;
        const float currKeep = clamp(1.0 / historyLen, 0.0, 1.0);

        AccumlateDiffuse[ipos] = float4(lerp(historyDiffuse, currDiffuseSample.rgb, currKeep), historyLen);
        AccumlateSpecular[ipos] = float4(lerp(historySpecular, currSpecularSample.rgb, currKeep), historyLen);
        AccumlateAlbedo[ipos].rgb = lerp(historyAlbedo, currAlbedoSample.rgb, currKeep);
    }
    else
    {
        // Disocclusion: drop history, restart the accumulation counter (carried in alpha).
        AccumlateDiffuse[ipos] = float4(currDiffuseSample.rgb, 1.0);
        AccumlateSpecular[ipos] = float4(currSpecularSample.rgb, 1.0);
        AccumlateAlbedo[ipos] = currAlbedoSample;
    }
}
