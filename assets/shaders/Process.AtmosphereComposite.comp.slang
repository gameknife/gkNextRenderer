import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

float3 ReconstructWorldPosition(
    UniformBufferObject camera, int2 pixel, uint2 size, float depth)
{
    const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
    const float4 viewPositionH = mul(camera.ProjectionInverseUnJit, float4(ndc, depth, 1.0f));
    const float safeW = abs(viewPositionH.w) > 1.0e-6f
        ? viewPositionH.w
        : (viewPositionH.w >= 0.0f ? 1.0e-6f : -1.0e-6f);
    const float3 viewPosition = viewPositionH.xyz / safeW;
    return mul(camera.ModelViewInverse, float4(viewPosition, 1.0f)).xyz;
}

float HeightFogTransmittance(
    FAtmosphereParams params, float3 cameraPosition, float3 surfacePosition, float distance)
{
    const float activeDistance = max(distance - params.FogStartDistance, 0.0f);
    if (activeDistance <= 0.0f || params.FogDensity <= 0.0f)
    {
        return 1.0f;
    }
    const float3 direction = (surfacePosition - cameraPosition) / max(distance, 1.0e-5f);
    const float cameraDensity =
        params.FogDensity * exp(-params.FogHeightFalloff * (cameraPosition.y - params.FogBaseHeight));
    const float heightDelta = direction.y * activeDistance;
    const float exponent = params.FogHeightFalloff * heightDelta;
    const float integralFactor = abs(exponent) < 1.0e-4f
        ? 1.0f
        : (1.0f - exp(-exponent)) / exponent;
    const float opticalDepth = max(cameraDensity * activeDistance * integralFactor, 0.0f);
    const float rawTransmittance = exp(-opticalDepth);
    return max(rawTransmittance, 1.0f - params.FogMaxOpacity);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    if (camera.AtmosphereParams == 0)
    {
        return;
    }
    const FAtmosphereParams params = ((FAtmosphereParams*)camera.AtmosphereParams)[0];
    const uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }

    const int2 pixel = int2(dispatchThreadId.xy);
    const float depth = Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER)[pixel];
    const bool surface = depth >= 0.0f && depth <= 1.0f;
    if (!surface)
    {
        return;
    }

    const float4 originalColor =
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel];
    const float3 cameraPosition = camera.ModelViewInverse[3].xyz;
    const float3 worldPosition = ReconstructWorldPosition(camera, pixel, size, depth);
    const float distance = length(worldPosition - cameraPosition);

    float3 color = originalColor.rgb;
    float3 aerialInScatter = float3(0.0f);
    float aerialTransmittance = 1.0f;
    const bool hasAerialVolume = Bindless.GetGpuscene().CustomData1 != 0;
    if (hasAerialVolume && (params.Flags & ATMOSPHERE_FLAG_AERIAL_PERSPECTIVE) != 0)
    {
        const float3 uvw = float3(
            (float2(pixel) + 0.5f) / float2(size),
            saturate(distance / max(params.AerialPerspectiveMaxDistance, 1.0f)));
        const float4 aerial =
            Bindless.GetSampleTexture3D(params.AerialPerspectiveLutId).SampleLevel(uvw, 0.0f);
        aerialInScatter = aerial.rgb * camera.SunColor.rgb *
            camera.SkyColor.rgb * params.SkyLuminanceScale;
        aerialTransmittance = saturate(aerial.a);
        color = aerialInScatter + color * aerialTransmittance;
    }

    float fogTransmittance = 1.0f;
    if ((params.Flags & ATMOSPHERE_FLAG_HEIGHT_FOG) != 0)
    {
        fogTransmittance =
            HeightFogTransmittance(params, cameraPosition, worldPosition, distance);
        color = params.FogInscatteringColor * (1.0f - fogTransmittance) +
                color * fogTransmittance;
    }

    const uint debugMode = Bindless.GetGpuscene().CustomData2;
    if (debugMode == 1u)
    {
        color = aerialInScatter;
    }
    else if (debugMode == 2u)
    {
        color = float3(aerialTransmittance * fogTransmittance);
    }
    else if (debugMode == 3u)
    {
        const float2 ndc = ((float2(pixel) + 0.5f) / float2(size)) * 2.0f - 1.0f;
        const float4 viewTarget = mul(camera.ProjectionInverseUnJit, float4(ndc, 1.0f, 1.0f));
        const float3 viewDirection = normalize(viewTarget.xyz / max(abs(viewTarget.w), 1.0e-6f));
        const float3 worldDirection =
            normalize(mul(camera.ModelViewInverse, float4(viewDirection, 0.0f)).xyz);
        const float3 directionHorizontal =
            normalize(float3(worldDirection.x, 0.0f, worldDirection.z) + float3(1.0e-6f, 0, 0));
        const float3 sunHorizontal =
            normalize(float3(camera.SunDirection.x, 0.0f, camera.SunDirection.z) + float3(1.0e-6f, 0, 0));
        const float2 skyUv = float2(
            dot(directionHorizontal, sunHorizontal) * 0.5f + 0.5f,
            sign(worldDirection.y) * sqrt(abs(worldDirection.y)) * 0.5f + 0.5f);
        color = Bindless.GetSampleTexture(params.SkyViewLutId).SampleLevel(skyUv, 0.0f).rgb;
    }

    // Alpha carries noSkyBackground through the post chain and must remain untouched.
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[pixel] =
        float4(max(color, float3(0.0f)), originalColor.a);
}
