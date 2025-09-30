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
uint FetchPrimitiveIndex(uint InstanceID)
{
    return InstanceID;
}

static const float2 offsets5x5[25] = {
    float2(-2.0, -2.0), float2(-1.0, -2.0), float2(0.0, -2.0), float2(1.0, -2.0), float2(2.0, -2.0),
    float2(-2.0, -1.0), float2(-1.0, -1.0), float2(0.0, -1.0), float2(1.0, -1.0), float2(2.0, -1.0),
    float2(-2.0, 0.0), float2(-1.0, 0.0), float2(0.0, 0.0), float2(1.0, 0.0), float2(2.0, 0.0),
    float2(-2.0, 1.0), float2(-1.0, 1.0), float2(0.0, 1.0), float2(1.0, 1.0), float2(2.0, 1.0),
    float2(-2.0, 2.0), float2(-1.0, 2.0), float2(0.0, 2.0), float2(1.0, 2.0), float2(2.0, 2.0)
};

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

    bool useHistory = true;
    float2 motion = MotionVector[ipos].rg;
    int2 previpos = int2(floor(float2(ipos) + motion));
    const bool inside = all(previpos < isize) &&
                        all(previpos >= int2(-1, -1));
    uint current_primitive_index0 = FetchPrimitiveIndex(ObjectId0[ipos]);
    uint prev_primitive_index0 = FetchPrimitiveIndex(ObjectId1[previpos]);
    const bool miss = current_primitive_index0 == 65535;
    
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
                const int2 samplePos = ipos + offset;
                const uint primitiveIdx = FetchPrimitiveIndex(ObjectId0[samplePos]);
                const float3 worldNormal = NormalBuffer[samplePos].rgb;

                const bool isCenter = (x == 0 && y == 0);
                const bool sameObject = (primitiveIdx == current_primitive_index0);
                const float centerDist = length(float2(x, y));
                const float weight = calculateWeight(centerDist, sameObject, isCenter, worldNormal, CenterNormal);

                spatialDiffuseSample += SourceDiffuse[samplePos] * weight;
                spatialSpecularSample += SourceSpecular[samplePos] * weight;
                totalWeight += weight;

                motionInRange += length(MotionVector[ipos + offset * 2].rg);
            }
        }
        spatialDiffuseSample /= totalWeight;
        spatialSpecularSample /= totalWeight;

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

        // if (!Camera.ProgressiveRender && false)
        // {
        //     float3 currentColor[25];
        //     for (int i = 0; i < 25; i++)
        //     {
        //         currentColor[i] = SourceDiffuse[ipos + int2(offsets5x5[i])].rgb;
        //     }
        //     float3 AABBMax = rgb2ycocg(src.rgb);
        //     float3 AABBMin = AABBMax;
        //     for (int k = 0; k < 25; k++)
        //     {
        //         AABBMin = min(AABBMin, rgb2ycocg(currentColor[k]));
        //         AABBMax = max(AABBMax, rgb2ycocg(currentColor[k]));
        //     }
        //     float3 HistoryYCoCg = rgb2ycocg(history);
        //     float3 ResultYCoCg = clamp(HistoryYCoCg, AABBMin, AABBMax);
        //     history = ycocg2rgb(ResultYCoCg);
        // }

        float currKeep = 1.0f / max(1, pushConsts.TemporalFrames);
        AccumlateDiffuse[ipos].rgb = lerp(historyDiffuse, currDiffuseSample.rgb, clamp(currKeep, 0.0, 1.0));
        AccumlateSpecular[ipos].rgb = lerp(historySpecular, currSpecularSample.rgb, clamp(currKeep, 0.0, 1.0));
        AccumlateAlbedo[ipos].rgb = lerp(historyAlbedo, currAlbedoSample.rgb, clamp(currKeep, 0.0, 1.0));
    }
    else
    {
        AccumlateDiffuse[ipos] = currDiffuseSample;
        AccumlateSpecular[ipos] = currSpecularSample;
        AccumlateAlbedo[ipos] = currAlbedoSample;
    }
}