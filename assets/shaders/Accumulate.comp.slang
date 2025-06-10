import Common;

// Texture bindings
[[vk::binding(0)]] RWTexture2D<float4> NewSourceImage;
[[vk::binding(1)]] RWTexture2D<float4> AccumulateImage;
[[vk::binding(3)]] RWTexture2D<float4> MotionVectorImage;
[[vk::binding(4)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(5)]] RWTexture2D<uint2> VisibilityBuffer;
[[vk::binding(6)]] RWTexture2D<uint2> Visibility1Buffer;
[[vk::binding(7)]] RWTexture2D<float4> OutImage;
[[vk::binding(8)]] RWTexture2D<float4> InNormal;

// Helper functions
uint FetchPrimitiveIndex(uint2 InstanceID)
{
    return InstanceID.x;
}

bool EdgeDetect(uint current_primitive_index, int2 ipos)
{
    uint center = FetchPrimitiveIndex(current_primitive_index);

    uint prev_primitive_index0 = FetchPrimitiveIndex(VisibilityBuffer[ipos + int2(2, 0)]);
    uint prev_primitive_index1 = FetchPrimitiveIndex(VisibilityBuffer[ipos + int2(-2, 0)]);
    uint prev_primitive_index2 = FetchPrimitiveIndex(VisibilityBuffer[ipos + int2(0, 2)]);
    uint prev_primitive_index3 = FetchPrimitiveIndex(VisibilityBuffer[ipos + int2(0, -2)]);

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
    float4 src = float4(0, 0, 0, 0);

        
    bool useHistory = true;
    float2 motion = MotionVectorImage[ipos].rg;
    int2 previpos = int2(floor(float2(ipos) + motion));
    const bool inside = all(previpos < int2(Camera.ViewportRect.xy + Camera.ViewportRect.zw)) &&
                        all(previpos >= int2(Camera.ViewportRect.xy) + int2(-1, -1));
    uint current_primitive_index0 = FetchPrimitiveIndex(VisibilityBuffer[ipos]);
    uint prev_primitive_index0 = FetchPrimitiveIndex(Visibility1Buffer[previpos]);
    const bool changed = current_primitive_index0 != prev_primitive_index0 ? true : false;
    const bool check = (length(motion) > 0.01) ? true : false;

    const int SAMPLE_RANGE = Camera.DisableSpatialReuse ? 0 : 1;
    
    float3 CenterNormal = InNormal[ipos].rgb;

    float temporalWeight = 0.0;
    float totalWeight = 0.0;

    for (int y = -SAMPLE_RANGE; y <= SAMPLE_RANGE; y++) {
        for (int x = -SAMPLE_RANGE; x <= SAMPLE_RANGE; x++) {
            int2 offset = int2(x, y);
            int2 samplePos = ipos + offset;
            float4 samplePixel = NewSourceImage[samplePos];
            uint primitiveIdx = FetchPrimitiveIndex(VisibilityBuffer[samplePos]);
            float3 worldNormal = InNormal[samplePos].rgb;

            bool isCenter = (x == 0 && y == 0) ? true : false;
            bool sameObject = (primitiveIdx == current_primitive_index0) ? true : false;
            float centerDist = length(float2(x, y));

            // Calculate the weight for this sample
            float weight = calculateWeight(centerDist, sameObject, isCenter, worldNormal, CenterNormal);

            if (isCenter) {
                temporalWeight = samplePixel.w;
            }

            src += samplePixel * weight;
            totalWeight += weight;
        }
    }

    // Normalize
    src /= totalWeight;
    
    float4 final = src;
    
    // fetch visibility to validate the history
    const bool miss = current_primitive_index0 == 65535 ? true : false;
    if (miss || Camera.TotalFrames == 0 || !inside)
    {
        useHistory = false;
    }

    if (useHistory)
    {
        if (!Camera.ProgressiveRender)
        {
            // current 4
            uint current_primitive_index1 = FetchPrimitiveIndex(VisibilityBuffer[ipos + int2(1, 0)]);
            uint current_primitive_index2 = FetchPrimitiveIndex(VisibilityBuffer[ipos + int2(0, 1)]);
            uint current_primitive_index3 = FetchPrimitiveIndex(VisibilityBuffer[ipos + int2(1, 1)]);

            // prev 9
            uint prev_primitive_index1 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(1, 0)]);
            uint prev_primitive_index2 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(0, 1)]);
            uint prev_primitive_index3 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(1, 1)]);
            uint prev_primitive_index4 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(-1, 0)]);
            uint prev_primitive_index5 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(0, -1)]);
            uint prev_primitive_index6 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(-1, -1)]);
            uint prev_primitive_index7 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(1, -1)]);
            uint prev_primitive_index8 = FetchPrimitiveIndex(Visibility1Buffer[previpos + int2(-1, 1)]);

            bool4 match1 = uint4(prev_primitive_index8, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) == 
                            uint4(current_primitive_index0, current_primitive_index0, current_primitive_index0, current_primitive_index0);
            bool4 match2 = uint4(prev_primitive_index4, prev_primitive_index5, prev_primitive_index6, prev_primitive_index7) == 
                            uint4(current_primitive_index0, current_primitive_index0, current_primitive_index0, current_primitive_index0);
            bool4 match3 = uint4(prev_primitive_index8, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) == 
                            uint4(current_primitive_index1, current_primitive_index1, current_primitive_index1, current_primitive_index1);
            bool4 match4 = uint4(prev_primitive_index4, prev_primitive_index5, prev_primitive_index6, prev_primitive_index7) == 
                            uint4(current_primitive_index1, current_primitive_index1, current_primitive_index1, current_primitive_index1);
            bool4 match5 = uint4(prev_primitive_index8, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) == 
                            uint4(current_primitive_index2, current_primitive_index2, current_primitive_index2, current_primitive_index2);
            bool4 match6 = uint4(prev_primitive_index4, prev_primitive_index5, prev_primitive_index6, prev_primitive_index7) == 
                            uint4(current_primitive_index2, current_primitive_index2, current_primitive_index2, current_primitive_index2);
            bool4 match7 = uint4(prev_primitive_index8, prev_primitive_index1, prev_primitive_index2, prev_primitive_index3) == 
                            uint4(current_primitive_index3, current_primitive_index3, current_primitive_index3, current_primitive_index3);
            bool4 match8 = uint4(prev_primitive_index4, prev_primitive_index5, prev_primitive_index6, prev_primitive_index7) == 
                            uint4(current_primitive_index3, current_primitive_index3, current_primitive_index3, current_primitive_index3);
            bool4 match9 = uint4(prev_primitive_index0, prev_primitive_index0, prev_primitive_index0, prev_primitive_index0) == 
                            uint4(current_primitive_index0, current_primitive_index1, current_primitive_index2, current_primitive_index3);

            // 在点积计算中，bool 会自动转换为 0 或 1
            float sum = dot(float4(match1), float4(1, 1, 1, 1));
            sum += dot(float4(match2), float4(1, 1, 1, 1));
            sum += dot(float4(match3), float4(1, 1, 1, 1));
            sum += dot(float4(match4), float4(1, 1, 1, 1));
            sum += dot(float4(match5), float4(1, 1, 1, 1));
            sum += dot(float4(match6), float4(1, 1, 1, 1));
            sum += dot(float4(match7), float4(1, 1, 1, 1));
            sum += dot(float4(match8), float4(1, 1, 1, 1));
            sum += dot(float4(match9), float4(1, 1, 1, 1));

            bool miss = sum < 4 ? true : false;
            if (miss)
            {
                useHistory = false;
            }
        }

        if (useHistory)
        {
            float3 historyColor[4];
            historyColor[0] = AccumulateImage[previpos].rgb;
            historyColor[1] = AccumulateImage[previpos + int2(1, 0)].rgb;
            historyColor[2] = AccumulateImage[previpos + int2(0, 1)].rgb;
            historyColor[3] = AccumulateImage[previpos + int2(1, 1)].rgb;

            float3 currentColor[25];

            for (int i = 0; i < 25; i++)
            {
                currentColor[i] = NewSourceImage[ipos + int2(offsets5x5[i])].rgb;
            }

            float2 subpixel = frac(float2(ipos) + motion);
            float3 history = lerp(lerp(historyColor[0], historyColor[1], subpixel.x), 
                                 lerp(historyColor[2], historyColor[3], subpixel.x), 
                                 subpixel.y);

            if (!Camera.ProgressiveRender)
            {
                float3 AABBMin, AABBMax;
                AABBMax = rgb2ycocg(src.rgb);
                AABBMin = AABBMax;
                // Get the clip range in YCoCg color space
                for (int k = 0; k < 25; k++)
                {
                    AABBMin = min(AABBMin, rgb2ycocg(currentColor[k]));
                    AABBMax = max(AABBMax, rgb2ycocg(currentColor[k]));
                }
                // History data that needs to be clipped
                float3 HistoryYCoCg = rgb2ycocg(history);
                float3 ResultYCoCg = clamp(HistoryYCoCg, AABBMin, AABBMax);
                // Convert back to RGB color space for final result
                history = ycocg2rgb(ResultYCoCg);
            }

            // Judge current gbuffer / object id with prev frame, to deghosting
            float currKeep = temporalWeight / max(1, Camera.TemporalFrames);
            final.rgb = lerp(history, src.rgb, clamp(currKeep, 0.0, 1.0));
        }
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

    OutImage[ipos] = float4(final.rgb, 1.0);
}