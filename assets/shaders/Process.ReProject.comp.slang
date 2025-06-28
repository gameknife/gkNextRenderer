import Common;

// Texture bindings
[[vk::binding(0, 0)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(1, 0)]] RWTexture2D<float4> AccumulateImage;

[[vk::binding(0, 1)]] RWTexture2D<float4> FinalImage;
[[vk::binding(1, 1)]] RWTexture2D<float4> OutImage;
[[vk::binding(2, 1)]] RWTexture2D<uint16_t2> MiniGBuffer;
[[vk::binding(3, 1)]] RWTexture2D<uint> ObjectId0;
[[vk::binding(4, 1)]] RWTexture2D<uint> ObjectId1;
[[vk::binding(5, 1)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(6, 1)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(7, 1)]] RWTexture2D<float4> OutNormalBuffer;

// Helper functions
uint FetchPrimitiveIndex(uint InstanceID)
{
    return InstanceID;
}

bool EdgeDetect(uint current_primitive_index, int2 ipos)
{
    uint center = FetchPrimitiveIndex(current_primitive_index);

    uint prev_primitive_index0 = FetchPrimitiveIndex(ObjectId0[ipos + int2(2, 0)]);
    uint prev_primitive_index1 = FetchPrimitiveIndex(ObjectId0[ipos + int2(-2, 0)]);
    uint prev_primitive_index2 = FetchPrimitiveIndex(ObjectId0[ipos + int2(0, 2)]);
    uint prev_primitive_index3 = FetchPrimitiveIndex(ObjectId0[ipos + int2(0, -2)]);

    bool edge0 = any(uint4(prev_primitive_index0, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) != uint4(center));
    bool edge1 = any(uint4(prev_primitive_index0, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) == uint4(center));

    return edge0 && edge1;
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
    int2 ipos = int2(DTid.xy) + int2(Camera.ViewportRect.xy);

    // More flexible sampling algorithm with dynamic weights
    float4 src = OutImage[ipos];
    float4 spatialSample = float4(0,0,0,0);

    bool useHistory = true;
    float2 motion = OutMotionVector[ipos].rg;
    int2 previpos = int2(floor(float2(ipos) + motion));
    const bool inside = all(previpos < int2(Camera.ViewportRect.xy + Camera.ViewportRect.zw)) &&
                        all(previpos >= int2(Camera.ViewportRect.xy) + int2(-1, -1));
    uint current_primitive_index0 = FetchPrimitiveIndex(ObjectId0[ipos]);
    uint prev_primitive_index0 = FetchPrimitiveIndex(ObjectId0[previpos]);
    const bool changed = current_primitive_index0 != prev_primitive_index0 ? true : false;
    const bool check = (length(motion) > 0.01) ? true : false;

    const int SAMPLE_RANGE = Camera.DisableSpatialReuse ? 0 : 2;

    float3 CenterNormal = OutNormalBuffer[ipos].rgb;

    float temporalWeight = 0.0;
    float totalWeight = 0.0;

    for (int y = -SAMPLE_RANGE; y <= SAMPLE_RANGE; y++) {
        for (int x = -SAMPLE_RANGE; x <= SAMPLE_RANGE; x++) {
            int2 offset = int2(x, y);
            int2 samplePos = ipos + offset;
            float4 samplePixel = OutImage[samplePos];
            uint primitiveIdx = FetchPrimitiveIndex(ObjectId0[samplePos]);
            float3 worldNormal = OutNormalBuffer[samplePos].rgb;

            bool isCenter = (x == 0 && y == 0) ? true : false;
            bool sameObject = (primitiveIdx == current_primitive_index0) ? true : false;
            float centerDist = length(float2(x, y));

            // Calculate the weight for this sample
            float weight = calculateWeight(centerDist, sameObject, isCenter, worldNormal, CenterNormal);

            if (isCenter) {
                temporalWeight = samplePixel.w;
            }

            spatialSample += samplePixel * weight;
            totalWeight += weight;
        }
    }

    // Normalize
    spatialSample /= totalWeight;
    
    float4 final = src;
    
    // fetch visibility to validate the history
    const bool miss = current_primitive_index0 == 65535 ? true : false;
    if (miss || Camera.TotalFrames == 0 || !inside)
    {
        useHistory = false;
    }

    if (useHistory)
    {
        // prev 9
        uint prev_primitive_index1 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 0)]);
        uint prev_primitive_index2 = FetchPrimitiveIndex(ObjectId1[previpos + int2(0, 1)]);
        uint prev_primitive_index3 = FetchPrimitiveIndex(ObjectId1[previpos + int2(1, 1)]);

        float3 historyColor[4];
        historyColor[0] = current_primitive_index0 == prev_primitive_index0 ? AccumulateImage[previpos].rgb : spatialSample.rgb;
        historyColor[1] = current_primitive_index0 == prev_primitive_index1 ? AccumulateImage[previpos + int2(1, 0)].rgb : spatialSample.rgb;
        historyColor[2] = current_primitive_index0 == prev_primitive_index2 ? AccumulateImage[previpos + int2(0, 1)].rgb : spatialSample.rgb;
        historyColor[3] = current_primitive_index0 == prev_primitive_index3 ? AccumulateImage[previpos + int2(1, 1)].rgb : spatialSample.rgb;

        float2 subpixel = frac(float2(ipos) + motion);
        float3 history = lerp(lerp(historyColor[0], historyColor[1], subpixel.x),
                                lerp(historyColor[2], historyColor[3], subpixel.x),
                                subpixel.y);

        // Judge current gbuffer / object id with prev frame, to deghosting
        float currKeep = temporalWeight / max(1, Camera.TemporalFrames);
        final.rgb = lerp(history, src.rgb, clamp(currKeep, 0.0, 1.0));
    }
    
    if (Camera.ShowEdge)
    {
        uint realInstanceId = FetchPrimitiveIndex(current_primitive_index0);
        if (realInstanceId == Camera.SelectedId)
        {
            if (EdgeDetect(current_primitive_index0, ipos))
            {
                final.rgb = float3(1, 1, 0.05) * Camera.PaperWhiteNit;
            }
        }
    }

    FinalImage[ipos] = float4(final.rgb, 1.0);
}