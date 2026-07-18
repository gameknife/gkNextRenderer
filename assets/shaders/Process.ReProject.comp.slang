import Shader.Bindless;
import Shader.Math;

struct PushConsts
{
    // Unified push-constant header (offset 0..15): aliases gpuScene's header so
    // gpuScene.CustomData0 (active view RT bank base) is valid in this custom pass. Params follow.
    uint _ViewHdr_SwapChainIndex;
    uint _ViewHdr_CustomData0;
    uint _ViewHdr_CustomData1;
    uint _ViewHdr_CustomData2;

    bool ProgressiveRender;
    int TemporalFrames;

    bool FastReproject;

    // History-clamp tuning (Phase A black-dot fix). gammaHi/gammaLo are the *tight* (low-confidence)
    // YCoCg luma box half-widths in sigmas; floorK is the relative luma floor. See ClampHistorySoft /
    // ClampStrengthFromConfidence.
    float ClampGammaHi;
    float ClampGammaLo;
    float ClampFloor;
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

// Relative firefly suppression (G9), object-aware. Estimate the neighborhood luminance statistics
// from the 8 surrounding taps *excluding the center*, then clamp a bright center down to
// neighborMean + k*sigma. Excluding the center is what makes isolated single-pixel fireflies
// catchable: if the center is included it inflates its own sigma and escapes the clamp. Only ever
// darkens, so genuine multi-pixel highlights (whose neighbors are also bright) survive untouched.
//
// Taps on a different primitive (silhouette edges, geometry next to bright sky) are skipped: mixing
// in a bright background neighbor used to inflate the stats so an edge firefly escaped the clamp,
// leaving the sparse bright dots seen along object edges during camera motion. When too few
// same-object neighbors exist to form a reliable estimate the center is left untouched.
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

// Temporal-confidence-driven clamp strength (Phase A black-dot + camera-motion-noise fix). The old
// fixed-gamma symmetric clamp fought temporal accumulation on every pixel and randomly clamped clean
// bright history to the per-frame 1spp noise floor -> flickering black dots.
//
// Strength is driven *only* by history length: well-accumulated pixels -> ~0 (history trusted, never
// clamped) so it stays clean both when static AND while the camera pans over it; young / freshly
// reset pixels -> ~1 (history unreliable, clamp pulls it toward the spatial estimate). Camera motion
// is deliberately NOT a trigger: panning over well-accumulated static geometry would otherwise clamp
// clean history into the jittery 1spp box every frame and reintroduce salt noise. This means the
// current implementation does not specially reject moving highlights/shadows on long-history
// surfaces. Temporal moments are only a conditional future option; no moments history exists today.
float ClampStrengthFromConfidence(float historyLen, float nMax)
{
    const float histConf = saturate(historyLen / max(nMax, 1.0));
    return saturate(1.0 - histConf);
}

// Soft, asymmetric, luminance-dominated history clamp. The box is centered on a *spatially
// prefiltered* current estimate (geometry-weighted, far less noisy than a raw 1spp neighborhood),
// so its mean is no longer dragged toward zero by dark/occluded taps. The half-width comes from a
// 3x3 neighborhood luma variance. The upper bound is tightened to suppress bright ghost trails
// while the lower bound + relative luma floor stay loose, so a clean bright history is never
// clamped down to a 1spp black dot. strength fades the whole operation toward the untouched history.
float3 ClampHistorySoft(RWTexture2D<float4> currentImage, int2 ipos, int2 imageMax, float3 history,
                        float3 filteredCur, float strength,
                        float gammaHiTight, float gammaLoTight, float floorTight)
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

    // Relaxed end of every gamma is fixed (almost no clamp); the tight end is CVar-driven.
    const float gammaHiY = lerp(8.0, gammaHiTight, strength); // upper bound: tightened to kill ghost trails
    const float gammaLoY = lerp(8.0, gammaLoTight, strength); // lower bound: always looser than upper
    const float gammaC = lerp(12.0, 6.0, strength);           // chroma: very loose, Co/Cg tolerate noise

    const float3 lo = float3(cy.x - gammaLoY * sigma, cy.yz - gammaC * sigma);
    const float3 hi = float3(cy.x + gammaHiY * sigma, cy.yz + gammaC * sigma);
    float3 clamped = clamp(hy, lo, hi);

    // Relative luma floor: history luma is never pulled below floorK * filtered mean -> no black dots.
    clamped.x = max(clamped.x, cy.x * lerp(0.9, floorTight, strength));

    // Soft blend: fade between untouched history and the hard clamp by strength, removing hard edges.
    return ycocg2rgb(lerp(hy, clamped, strength));
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
    history = clamp(history, 0.0, 1600.0); // kill the negative values
    return history;
}

// A simple accumulation shader, reproject can be implemented here later.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int2 ipos = int2(DTid.xy);
    uint2 isize = Bindless.GetStorageTextureDimensions<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_DIFFUSE));
    if (any(DTid.xy >= isize))
    {
        return;
    }
    int2 imageSize = int2(isize);
    int2 imageMax = imageSize - int2(1, 1);

    let SourceDiffuse = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_DIFFUSE));
    let SourceSpecular = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_SINGLE_SPECULAR));
    let SourceAlbedo = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_ALBEDO));

    let vb = pushConsts._ViewHdr_CustomData0;
    let PrevDiffuse = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, Bindless.RT_SINGLE_PREV_DIFFUSE));
    let PrevSpecular = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, Bindless.RT_SINGLE_PREV_SPECULAR));
    let PrevAlbedo = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(vb, Bindless.RT_SINGLE_PREV_ALBEDO));

    let AccumulateDiffuse = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_ACCUMULATE_DIFFUSE));
    let AccumulateSpecular = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_ACCUMULATE_SPECULAR));
    let AccumulateAlbedo = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_ACCUMULATE_ALBEDO));

    let ObjectId0 = Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_OBJECTID_0));
    let ObjectId1 = Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_OBJECTID_1));
    let MotionVector = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_MOTIONVECTOR));
    let NormalBuffer = Bindless.GetStorageTexture<float4>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_NORMAL));

    if (pushConsts.ProgressiveRender)
    {
        if (pushConsts.TemporalFrames >= 1024)
        {
            const float nMax = float(max(1, pushConsts.TemporalFrames));
            const float prevCount = clamp(PrevDiffuse[ipos].a, 0.0, nMax);
            if (prevCount >= nMax)
            {
                AccumulateDiffuse[ipos] = PrevDiffuse[ipos];
                AccumulateSpecular[ipos] = PrevSpecular[ipos];
                AccumulateAlbedo[ipos] = PrevAlbedo[ipos];
                return;
            }

            const float newCount = min(prevCount + 1.0, nMax);
            const float currWeight = clamp(1.0 / newCount, 0.0, 1.0);

            AccumulateDiffuse[ipos] = float4(lerp(PrevDiffuse[ipos].rgb, SourceDiffuse[ipos].rgb, currWeight), newCount);
            AccumulateSpecular[ipos] = float4(lerp(PrevSpecular[ipos].rgb, SourceSpecular[ipos].rgb, currWeight), newCount);
            AccumulateAlbedo[ipos] = float4(lerp(PrevAlbedo[ipos].rgb, SourceAlbedo[ipos].rgb, currWeight), newCount);
        }
        else
        {
            AccumulateDiffuse[ipos] = lerp(PrevDiffuse[ipos], SourceDiffuse[ipos], clamp(1.0 / 512.0, 0.0, 1.0));
            AccumulateSpecular[ipos] = lerp(PrevSpecular[ipos], SourceSpecular[ipos], clamp(1.0 / 512.0, 0.0, 1.0));
            AccumulateAlbedo[ipos] = lerp(PrevAlbedo[ipos], SourceAlbedo[ipos], clamp(1.0 / 512.0, 0.0, 1.0));
        }
        return;
    }

    float4 currDiffuseSample = SourceDiffuse[ipos];
    float4 currSpecularSample = SourceSpecular[ipos];
    float4 currAlbedoSample = SourceAlbedo[ipos];

    uint current_primitive_index0 = FetchPrimitiveIndex(ObjectId0[ipos]);

    // Firefly pre-clamp on the freshly traced lighting before it enters temporal accumulation.
    // Diffuse is clamped loosely (the a-trous pass recovers any over-suppression spatially);
    // specular is clamped tighter since it has no spatial denoiser yet (specular is Phase 4).
    // Stats are restricted to same-primitive neighbors so edge fireflies can't hide behind a bright
    // background tap.
    currDiffuseSample.rgb = FireflyClamp(SourceDiffuse, ObjectId0, ipos, imageMax, current_primitive_index0, currDiffuseSample.rgb, 3.0f);
    currSpecularSample.rgb = FireflyClamp(SourceSpecular, ObjectId0, ipos, imageMax, current_primitive_index0, currSpecularSample.rgb, 1.5f);

    bool useHistory = true;
    float2 motion = MotionVector[ipos].rg;
    int2 previpos = int2(floor(float2(ipos) + motion));
    const bool inside = all(previpos >= int2(0, 0)) &&
                        all(previpos + int2(1, 1) < imageSize);
    uint prev_primitive_index0 = OBJECT_ID_INVALID_INSTANCE;
    if (inside)
    {
        prev_primitive_index0 = FetchPrimitiveIndex(ObjectId1[previpos]);
    }
    const bool miss = current_primitive_index0 == OBJECT_ID_INVALID_INSTANCE;
    
    uint moment = Bindless.GetStorageTexture<uint>(Bindless.ViewRT(pushConsts._ViewHdr_CustomData0, Bindless.RT_MOTIONMOMENT))[ipos];

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

        // Adaptive temporal blend (Phase 3): historyLen is carried in the diffuse accumulation
        // alpha. Young / freshly-disoccluded pixels (small historyLen) use a large alpha to
        // converge fast; once historyLen saturates at TemporalFrames the blend matches the old
        // fixed 1/TemporalFrames behaviour. historyLen resets when the primary reprojected tap
        // crosses an object boundary. Computed before the clamp so it can drive clamp strength.
        const float nMax = float(max(1u, pushConsts.TemporalFrames));
        const float prevHistlen = clamp(PrevDiffuse[previpos].a, 0.0, nMax);
        const float historyLen = (prev_primitive_index0 == current_primitive_index0)
            ? min(prevHistlen + 1.0, nMax)
            : 1.0;
        const float currKeep = clamp(1.0 / historyLen, 0.0, 1.0);

        // Adaptive soft clipping. Box is centered on the geometry-weighted spatial estimate; strength
        // is driven purely by history confidence so well-accumulated pixels are never clamped (stays
        // clean while static AND while the camera pans). Albedo is a stable demodulated material
        // signal, so only the lighting channels are clipped.
        const float clampStrength = ClampStrengthFromConfidence(historyLen, nMax);
        historyDiffuse = ClampHistorySoft(SourceDiffuse, ipos, imageMax, historyDiffuse,
            spatialDiffuseSample.rgb, clampStrength,
            pushConsts.ClampGammaHi, pushConsts.ClampGammaLo, pushConsts.ClampFloor);
        historySpecular = ClampHistorySoft(SourceSpecular, ipos, imageMax, historySpecular,
            spatialSpecularSample.rgb, clampStrength,
            pushConsts.ClampGammaHi, pushConsts.ClampGammaLo, pushConsts.ClampFloor);

        // Spatial fallback for young pixels (camera-motion noise fix). With only reproject and no
        // a-trous, a pixel whose history just reset (reprojection crossed an object boundary while
        // the camera moved) would blend in the raw 1spp center -> visible salt noise. Lean on the
        // geometry-weighted spatial estimate while history is short, then fade back to the raw sample
        // as temporal integration takes over so well-accumulated pixels keep their full sharpness.
        const float kSpatialFallbackFrames = 6.0;
        const float spatialFallback = saturate(1.0 - (historyLen - 1.0) / kSpatialFallbackFrames);
        const float3 currDiffuseEst = lerp(currDiffuseSample.rgb, spatialDiffuseSample.rgb, spatialFallback);
        const float3 currSpecularEst = lerp(currSpecularSample.rgb, spatialSpecularSample.rgb, spatialFallback);

        AccumulateDiffuse[ipos] = float4(lerp(historyDiffuse, currDiffuseEst, currKeep), historyLen);
        AccumulateSpecular[ipos] = float4(lerp(historySpecular, currSpecularEst, currKeep), historyLen);
        AccumulateAlbedo[ipos].rgb = lerp(historyAlbedo, currAlbedoSample.rgb, currKeep);
    }
    else
    {
        // Disocclusion: drop history, restart the accumulation counter (carried in alpha).
        AccumulateDiffuse[ipos] = float4(currDiffuseSample.rgb, 1.0);
        AccumulateSpecular[ipos] = float4(currSpecularSample.rgb, 1.0);
        AccumulateAlbedo[ipos] = currAlbedoSample;
    }
}
