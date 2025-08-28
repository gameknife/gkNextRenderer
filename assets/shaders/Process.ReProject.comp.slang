import Common;
import Bindless;

// Texture bindings
// [[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> Camera;
// [[vk::binding(1, 0)]] RWTexture2D<float4> SourceImage;
// [[vk::binding(2, 0)]] RWTexture2D<float4> AccumulateImage;
// [[vk::binding(3, 0)]] RWTexture2D<float4> FinalImage;

// [[vk::binding(1, 1)]] RWTexture2D<float4> OutImage;
// [[vk::binding(2, 1)]] RWTexture2D<uint> MiniGBuffer;
// [[vk::binding(3, 1)]] RWTexture2D<uint> ObjectId0;
// [[vk::binding(4, 1)]] RWTexture2D<uint> ObjectId1;
// [[vk::binding(5, 1)]] RWTexture2D<float4> OutMotionVector;
// [[vk::binding(6, 1)]] RWTexture2D<float4> OutAlbedoBuffer;
// [[vk::binding(7, 1)]] RWTexture2D<float4> OutNormalBuffer;

// struct PushConsts
// {
//     bool needClamp;
//     bool needSpatio;
// };

// [[vk::push_constant]]
// ConstantBuffer<PushConsts> pushConsts;

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

// A simple accumulation shader, reproject can be implemented here later.
[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    UniformBufferObject Camera = Bindless.GetGpuscene().Camera[0];

    int2 ipos = int2(DTid.xy) + int2(Camera.ViewportRect.xy);

    let SourceDiffuse = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE);
    let SourceSpecular = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR);
    let SourceAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO);

    let PrevDiffuse = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_PREV_DIFFUSE);
    let PrevSpecular = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_PREV_SPECULAR);
    let PrevAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_PREV_ALBEDO);

    let AccumlateDiffuse = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_DIFFUSE);
    let AccumlateSpecular = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_SPECULAR);
    let AccumlateAlbedo = Bindless.GetStorageTexture<float4>(Bindless.RT_ACCUMLATE_ALBEDO);

    let ObjectId0 = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0);
    let ObjectId1 = Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_1);
    let MotionVector = Bindless.GetStorageTexture<float4>(Bindless.RT_MOTIONVECTOR);
    let NormalBuffer = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL);


    float4 src = SourceDiffuse[ipos];
    
    bool useHistory = true;
    float2 motion = MotionVector[ipos].rg;
    int2 previpos = int2(floor(float2(ipos) + motion));
    const bool inside = all(previpos < int2(Camera.ViewportRect.xy + Camera.ViewportRect.zw)) &&
                        all(previpos >= int2(Camera.ViewportRect.xy) + int2(-1, -1));
    uint current_primitive_index0 = FetchPrimitiveIndex(ObjectId0[ipos]);
    uint prev_primitive_index0 = FetchPrimitiveIndex(ObjectId1[previpos]);

    float4 final = src;

    if (Camera.ProgressiveRender)
    {
        float4 history = PrevDiffuse[ipos];
        final = lerp(history, src, clamp(1.0 / Camera.TemporalFrames, 0.0, 1.0));
        AccumlateDiffuse[ipos] = float4(final.rgb, 1.0);
        return;
    }
    
    // fetch visibility to validate the history
    const bool miss = current_primitive_index0 == 65535 ? true : false;
    if (miss || Camera.TotalFrames == 0 || !inside)
    {
        useHistory = false;
    }

    if (useHistory)
    {
        const int SAMPLE_RANGE = Camera.DisableSpatialReuse ? 0 : 2;

        float3 CenterNormal = NormalBuffer[ipos].rgb;

        float4 spatialSample = float4(0, 0, 0, 0);
        float totalWeight = 0.0;
        float motionInRange = 0; // 

        for (int y = -SAMPLE_RANGE; y <= SAMPLE_RANGE; y++) {
            for (int x = -SAMPLE_RANGE; x <= SAMPLE_RANGE; x++) {
                int2 offset = int2(x, y);
                int2 samplePos = ipos + offset;
                float4 samplePixel = SourceDiffuse[samplePos];
                uint primitiveIdx = FetchPrimitiveIndex(ObjectId0[samplePos]);
                float3 worldNormal = NormalBuffer[samplePos].rgb;

                bool isCenter = (x == 0 && y == 0) ? true : false;
                bool sameObject = (primitiveIdx == current_primitive_index0) ? true : false;
                float centerDist = length(float2(x, y));

                // Calculate the weight for this sample
                float weight = calculateWeight(centerDist, sameObject, isCenter, worldNormal, CenterNormal);

                spatialSample += samplePixel * weight;
                totalWeight += weight;

                motionInRange += length(MotionVector[ipos + offset * 2].rg);
            }
        }

        // Normalize
        spatialSample /= totalWeight;

        // prev 9
        uint prev_primitive_index1 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 0)]);
        uint prev_primitive_index2 = FetchPrimitiveIndex(ObjectId1[previpos + int2(0, 1)]);
        uint prev_primitive_index3 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 1)]);

        float3 historyColor[4];

        // todo: off focus dont judge, need fix later
        // motion value should write to a buffer that cool down by time
        if (length(motionInRange) < 0.1)
        {
            prev_primitive_index0 = current_primitive_index0;
            prev_primitive_index1 = current_primitive_index0;
            prev_primitive_index2 = current_primitive_index0;
            prev_primitive_index3 = current_primitive_index0;
        }

        historyColor[0] = current_primitive_index0 == prev_primitive_index0 ? PrevDiffuse[previpos].rgb : spatialSample.rgb;
        historyColor[1] = current_primitive_index0 == prev_primitive_index1 ? PrevDiffuse[previpos + int2(1, 0)].rgb : spatialSample.rgb;
        historyColor[2] = current_primitive_index0 == prev_primitive_index2 ? PrevDiffuse[previpos + int2(0, 1)].rgb : spatialSample.rgb;
        historyColor[3] = current_primitive_index0 == prev_primitive_index3 ? PrevDiffuse[previpos + int2(1, 1)].rgb : spatialSample.rgb;

        float2 subpixel = frac(float2(ipos) + motion);
        float3 history = lerp(lerp(historyColor[0], historyColor[1], subpixel.x),
                                lerp(historyColor[2], historyColor[3], subpixel.x),
                                subpixel.y);

        history = clamp(history, 0.0, 1600.0); // kill the negative valuse

        if (!Camera.ProgressiveRender && false)
        {
            float3 currentColor[25];

            for (int i = 0; i < 25; i++)
            {
                currentColor[i] = SourceDiffuse[ipos + int2(offsets5x5[i])].rgb;
            }

            float3 AABBMax = rgb2ycocg(src.rgb);
            float3 AABBMin = AABBMax;
  
            for (int k = 0; k < 25; k++)
            {
                AABBMin = min(AABBMin, rgb2ycocg(currentColor[k]));
                AABBMax = max(AABBMax, rgb2ycocg(currentColor[k]));
            }

            float3 HistoryYCoCg = rgb2ycocg(history);

            float3 ResultYCoCg = clamp(HistoryYCoCg, AABBMin, AABBMax);

            history = ycocg2rgb(ResultYCoCg);
        }

        float currKeep = 1.0f / max(1, Camera.TemporalFrames);
        final.rgb = lerp(history, src.rgb, clamp(currKeep, 0.0, 1.0));
    }

    AccumlateDiffuse[ipos] = float4(final.rgb, 1.0);
    AccumlateSpecular[ipos] = SourceSpecular[ipos];
    AccumlateAlbedo[ipos] = SourceAlbedo[ipos];
}