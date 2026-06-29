import Common;
import Bindless;

static const float kPi = 3.14159265359f;

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

float PixelNoise(uint2 pixel, uint frame)
{
    const float seed = dot(float2(pixel), float2(0.06711056f, 0.00583715f));
    return frac(52.9829189f * frac(seed + float(frame) * 0.000005f));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    const uint2 aoSize = Bindless.GetViewStorageTextureDimensions<float>(Bindless.RT_GTAO);
    const uint2 fullSize = Bindless.GetViewStorageTextureDimensions<float>(Bindless.RT_PREV_DEPTHBUFFER);
    const int2 halfPixel = int2(DTid.xy);
    if (DTid.x >= aoSize.x || DTid.y >= aoSize.y)
    {
        return;
    }

    const uint samplePhase = (camera.TotalFrames + DTid.x * 3u + DTid.y * 5u) & 3u;
    const int2 phaseOffset = int2(int(samplePhase & 1u), int((samplePhase >> 1u) & 1u));
    const int2 fullMax = int2(fullSize) - int2(1, 1);
    const int2 centerPixel = min(halfPixel * 2 + phaseOffset, fullMax);
    const float centerDepth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[centerPixel];
    const float3 worldNormal = Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL)[centerPixel].xyz;

    if (!camera.GTAOEnable || !IsSurfaceDepth(centerDepth) || dot(worldNormal, worldNormal) < 0.25f)
    {
        Bindless.GetViewStorageTexture<float>(Bindless.RT_GTAO).Store(halfPixel, 1.0f);
        return;
    }

    const float3 centerPosition = ReconstructViewPosition(camera, centerPixel, fullSize, centerDepth);
    const float3 viewNormal = normalize(mul(camera.ModelView, float4(normalize(worldNormal), 0.0f)).xyz);
    const float radius = max(camera.GTAORadius, 0.01f);
    const float thickness = max(camera.GTAOThickness, 0.01f);
    const float focalY = 0.5f * float(fullSize.y) * abs(camera.Projection[1][1]);
    const float projectedRadius = clamp(focalY * radius / max(-centerPosition.z, 0.05f), 2.0f, 64.0f);
    const float noise = PixelNoise(uint2(halfPixel), camera.TotalFrames);
    // Low preserves the original 2x4 cost. Higher levels increase both angular coverage
    // and radial density; increasing only the step count leaves visible directional bands
    // when the world-space radius is large.
    const uint quality = min(camera.GTAOQuality, 3u);
    const uint sliceCounts[4] = {2u, 3u, 4u, 6u};
    const uint stepCounts[4] = {4u, 6u, 8u, 10u};
    const uint sliceCount = sliceCounts[quality];
    const uint stepCount = stepCounts[quality];
    float horizonOcclusion = 0.0f;

    for (uint slice = 0u; slice < sliceCount; ++slice)
    {
        const float angle = (float(slice) + noise) * (kPi / float(sliceCount));
        const float2 axis = float2(cos(angle), sin(angle));

        for (int side = -1; side <= 1; side += 2)
        {
            float horizon = 0.0f;
            for (uint step = 1u; step <= stepCount; ++step)
            {
                const float stepPhase = (float(step) - 0.5f + frac(noise * 1.6180339f)) / float(stepCount);
                const float2 offset = axis * float(side) * projectedRadius * stepPhase;
                const int2 samplePixel = clamp(int2(round(float2(centerPixel) + offset)), int2(0, 0), fullMax);
                const float sampleDepth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[samplePixel];
                if (!IsSurfaceDepth(sampleDepth))
                {
                    continue;
                }

                const float3 samplePosition = ReconstructViewPosition(camera, samplePixel, fullSize, sampleDepth);
                const float3 toSample = samplePosition - centerPosition;
                const float distanceToSample = length(toSample);
                if (distanceToSample <= 1e-4f || distanceToSample >= radius)
                {
                    continue;
                }

                const float distanceWeight = saturate(1.0f - distanceToSample / radius);
                const float excessDepth = max(abs(toSample.z) - thickness, 0.0f);
                const float thicknessWeight = 1.0f / (1.0f + excessDepth * 4.0f / thickness);
                const float horizonCosine = max(dot(viewNormal, toSample / distanceToSample) - 0.05f, 0.0f);
                horizon = max(horizon, horizonCosine * distanceWeight * thicknessWeight);
            }
            horizonOcclusion += horizon;
        }
    }

    // Output RAW horizon visibility [0,1]; the user-facing occlusion strength is applied once in
    // Process.GTAOCompose.
    const float visibility = saturate(1.0f - horizonOcclusion / float(sliceCount * 2u));
    Bindless.GetViewStorageTexture<float>(Bindless.RT_GTAO).Store(halfPixel, visibility);
}
