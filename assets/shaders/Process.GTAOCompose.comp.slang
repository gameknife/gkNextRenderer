import Common;
import Bindless;

bool IsSurfaceDepth(float depth)
{
    return depth >= 0.0f && depth <= 1.0f;
}

float3 ReconstructViewPosition(UniformBufferObject camera, int2 pixel, uint2 size, float depth)
{
    const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    const float4 view = mul(camera.ProjectionInverse, float4(ndc, depth, 1.0f));
    const float safeW = abs(view.w) > 1e-6f ? view.w : (view.w >= 0.0f ? 1e-6f : -1e-6f);
    return view.xyz / safeW;
}

float UpsampleAO(UniformBufferObject camera, int2 pixel, uint2 fullSize, float centerDepth, float3 centerNormal)
{
    const uint2 aoSize = Bindless.GetStorageTextureDimensions<float>(Bindless.RT_GTAO);
    const int2 aoMax = int2(aoSize) - int2(1, 1);
    const int2 fullMax = int2(fullSize) - int2(1, 1);
    const float3 centerPosition = ReconstructViewPosition(camera, pixel, fullSize, centerDepth);
    const float2 halfPosition = (float2(pixel) + 0.5f) * 0.5f - 0.5f;
    const int2 halfBase = int2(floor(halfPosition));
    float weightedAO = 0.0f;
    float weightSum = 0.0f;

    // A 3x3 joint bilateral footprint is still cheap at full resolution and finds valid
    // geometry when a temporally jittered half-resolution representative landed on sky.
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const int2 aoPixel = clamp(halfBase + int2(x, y), int2(0, 0), aoMax);
            const int2 representative = min(aoPixel * 2 + int2(1, 1), fullMax);
            const float sampleDepth = Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[representative];
            const float3 sampleNormal = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL)[representative].xyz;
            if (!IsSurfaceDepth(sampleDepth) || dot(sampleNormal, sampleNormal) < 0.25f)
            {
                continue;
            }

            const float3 samplePosition = ReconstructViewPosition(camera, representative, fullSize, sampleDepth);
            const float normalWeight = pow(saturate(dot(centerNormal, normalize(sampleNormal))), 32.0f);
            const float depthScale = max(0.02f, abs(centerPosition.z) * 0.02f);
            const float depthWeight = exp2(-abs(samplePosition.z - centerPosition.z) / depthScale);
            const float2 spatialDelta = float2(aoPixel) - halfPosition;
            const float spatialWeight = exp2(-dot(spatialDelta, spatialDelta));
            const float weight = normalWeight * depthWeight * spatialWeight;
            weightedAO += Bindless.GetStorageTexture<float>(Bindless.RT_GTAO)[aoPixel] * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 1e-5f)
    {
        return Bindless.GetStorageTexture<float>(Bindless.RT_GTAO)[clamp(pixel / 2, int2(0, 0), aoMax)];
    }
    return weightedAO / weightSum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const uint2 fullSize = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    const int2 pixel = int2(DTid.xy);
    if (DTid.x >= fullSize.x || DTid.y >= fullSize.y)
    {
        return;
    }

    const float4 directOrEmissive = Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE)[pixel];
    const float4 ambient = Bindless.GetStorageTexture<float4>(Bindless.RT_AMBIENT)[pixel];
    const float depth = Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixel];
    const float3 worldNormal = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL)[pixel].xyz;
    const bool surface = IsSurfaceDepth(depth) && dot(worldNormal, worldNormal) >= 0.25f;
    float ao = 1.0f;
    if (camera.GTAOEnable && surface)
    {
        ao = UpsampleAO(camera, pixel, fullSize, depth, normalize(worldNormal));
    }

    float4 output = float4(directOrEmissive.rgb + ambient.rgb * ao, 1.0f);
    if (camera.GTAODebugMode == 1u)
    {
        // ComposeSimple applies the regular scene tonemap after this pass. Convert the
        // normalized diagnostic value to its linear scene-referred range first.
        const float debugExposure = 40000.0f / max(camera.PaperWhiteNit, 1.0f);
        output = surface ? float4(ao * debugExposure, ao * debugExposure, ao * debugExposure, 1.0f)
                         : float4(0, 0, 0, 1);
    }
    else if (camera.GTAODebugMode == 2u)
    {
        output = float4(ambient.rgb, 1.0f);
    }

    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, output);
}
