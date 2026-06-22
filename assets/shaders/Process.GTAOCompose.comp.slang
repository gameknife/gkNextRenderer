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

// Joint-bilateral blur of the per-pixel voxel sky-visibility (stored in RT_AMBIENT.a). Smooths
// voxel-scale steps / bake noise and the solid-as-zero contact patch edges, while depth+normal
// weights stop it bleeding across geometry edges (so contact occlusion stays put, just softened).
float BlurSkyVisibility(UniformBufferObject camera, int2 pixel, uint2 size, float centerDepth,
                        float3 centerNormal, int radius)
{
    const float centerViewZ = ReconstructViewPosition(camera, pixel, size, centerDepth).z;
    const int2 maxPixel = int2(size) - int2(1, 1);
    float weightedVis = 0.0f;
    float weightSum = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const int2 samplePixel = clamp(pixel + int2(x, y), int2(0, 0), maxPixel);
            const float sampleDepth = Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[samplePixel];
            const float3 sampleNormal = Bindless.GetStorageTexture<float4>(Bindless.RT_NORMAL)[samplePixel].xyz;
            if (!IsSurfaceDepth(sampleDepth) || dot(sampleNormal, sampleNormal) < 0.25f)
            {
                continue;
            }

            const float3 sampleViewPos = ReconstructViewPosition(camera, samplePixel, size, sampleDepth);
            const float normalWeight = pow(saturate(dot(centerNormal, normalize(sampleNormal))), 16.0f);
            const float depthScale = max(0.05f, abs(centerViewZ) * 0.05f);
            const float depthWeight = exp2(-abs(sampleViewPos.z - centerViewZ) / depthScale);
            const float weight = normalWeight * depthWeight;
            weightedVis += Bindless.GetStorageTexture<float4>(Bindless.RT_AMBIENT)[samplePixel].a * weight;
            weightSum += weight;
        }
    }

    return weightSum > 1e-5f ? weightedVis / weightSum
                             : Bindless.GetStorageTexture<float4>(Bindless.RT_AMBIENT)[pixel].a;
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

    // Voxel sky-visibility lives in the ambient alpha channel (written by the shading pass; 1.0 when
    // disabled or unbaked). GTAO covers near-field contact occlusion, skyVis covers large-scale /
    // off-screen occlusion; combine per cvar so both only darken the sky term. An optional bilateral
    // blur smooths voxel-scale steps / bake noise without bleeding across geometry edges.
    float skyVis = 1.0f;
    if (camera.SkyVisEnable && surface)
    {
        skyVis = (camera.SkyVisBlurRadius > 0u)
            ? BlurSkyVisibility(camera, pixel, fullSize, depth, normalize(worldNormal),
                                int(camera.SkyVisBlurRadius))
            : ambient.a;
    }
    // Optional skyVis-only trim (SkyVisStrength) before combining, so the user can bias the voxel
    // term relative to GTAO. Applied as a darkening scale on the sky-vis occlusion amount.
    const float skyVisEff = saturate(1.0f - (1.0f - skyVis) * max(camera.SkyVisStrength, 0.0f));

    // Combine the two RAW visibilities (GTAO near-field + voxel large-scale), then apply ONE master
    // occlusion strength (GTAOStrength) to the combined darkening. This unifies the control: a single
    // knob scales both terms, and dialing it down lightens the whole occlusion uniformly.
    float combinedVis;
    if (camera.SkyVisCombineMode == 1u)
    {
        combinedVis = min(ao, skyVisEff);               // avoid double darkening at contacts
    }
    else if (camera.SkyVisCombineMode == 0u)
    {
        combinedVis = ao * skyVisEff;                   // independent occlusions multiply
    }
    else
    {
        // near-bright (default): where ao is low (tight corners/contacts) skyVis fades out and GTAO's
        // fine gradient takes over; where ao is high (open) the coarse voxel skyVis applies its
        // large-scale darkening. Stops a coarse skyVis voxel from slamming a corner to flat black and
        // masking GTAO's detail transition.
        combinedVis = ao * lerp(1.0f, skyVisEff, ao);
    }

    // Gamma-style master strength: monotone and never hard-clips to pure black (unlike a linear
    // 1 - strength*(1-vis), which zeroes everything below 1-1/strength). This keeps GTAO's gradient
    // visible in deep corners as a soft falloff instead of a dead-black patch.
    const float strength = max(camera.GTAOStrength, 0.0f);
    const float skyOcclusion = pow(saturate(combinedVis), strength);

    float4 output = float4(directOrEmissive.rgb + ambient.rgb * skyOcclusion, 1.0f);
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
    else if (camera.GTAODebugMode == 3u)
    {
        const float debugExposure = 40000.0f / max(camera.PaperWhiteNit, 1.0f);
        output = surface ? float4(float3(skyVis) * debugExposure, 1.0f) : float4(0, 0, 0, 1);
    }
    else if (camera.GTAODebugMode == 4u)
    {
        const float debugExposure = 40000.0f / max(camera.PaperWhiteNit, 1.0f);
        output = surface ? float4(float3(skyOcclusion) * debugExposure, 1.0f) : float4(0, 0, 0, 1);
    }

    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, output);
}
