import Shader.Bindless;
import Shader.Types;
import Shader.GpuScene;

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
    const uint2 size = Bindless.GetStorageTextureDimensions<float4>(params.SkyViewLutId);
    if (any(dispatchThreadId.xy >= size))
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(size);
    const float azimuthCos = uv.x * 2.0f - 1.0f;
    const float mappedElevation = uv.y * 2.0f - 1.0f;
    const float elevation = sign(mappedElevation) * mappedElevation * mappedElevation;
    const float horizontal = sqrt(max(1.0f - elevation * elevation, 0.0f));
    const float3 sunDirection = normalize(camera.SunDirection.xyz);
    const float3 sunHorizontal =
        normalize(float3(sunDirection.x, 0.0f, sunDirection.z) + float3(1.0e-6f, 0, 0));
    const float3 sunRight = float3(-sunHorizontal.z, 0.0f, sunHorizontal.x);
    const float azimuthSin = sqrt(max(1.0f - azimuthCos * azimuthCos, 0.0f));
    const float3 viewDirection =
        normalize((sunHorizontal * azimuthCos + sunRight * azimuthSin) * horizontal +
                  float3(0.0f, elevation, 0.0f));

    const float3 cameraWorld =
        mul(camera.ModelViewInverse, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
    const float cameraAltitude =
        params.WorldOriginAltitude + cameraWorld.y / max(params.WorldUnitsPerKm, 1.0e-3f);
    const float3 origin = float3(0.0f, params.BottomRadius + max(cameraAltitude, 0.001f), 0.0f);
    float distance = Atmosphere.RaySphereNearest(origin, viewDirection, params.TopRadius);
    const float groundDistance = Atmosphere.RaySphereNearest(origin, viewDirection, params.BottomRadius);
    if (groundDistance > 0.0f)
    {
        distance = min(distance, groundDistance);
    }

    const uint stepCount = 32;
    const float stepLength = max(distance, 0.0f) / float(stepCount);
    float3 transmittance = float3(1.0f);
    float3 radiance = float3(0.0f);
    const float cosTheta = dot(viewDirection, sunDirection);
    const float rayleighPhase = Atmosphere.RayleighPhase(cosTheta);
    const float miePhase = Atmosphere.MiePhase(cosTheta, params.MiePhaseG);
    for (uint i = 0; i < stepCount; ++i)
    {
        const float t = (float(i) + 0.5f) * stepLength;
        const float3 samplePosition = origin + viewDirection * t;
        const float altitude = max(length(samplePosition) - params.BottomRadius, 0.0f);
        const float rayleighDensity = exp(-altitude / max(params.RayleighDensityH, 1.0e-3f));
        const float mieDensity = exp(-altitude / max(params.MieDensityH, 1.0e-3f));
        const float sunDistance = Atmosphere.RaySphereNearest(samplePosition, sunDirection, params.TopRadius);
        const float3 sunTransmittance = sunDistance > 0.0f
            ? Atmosphere.IntegrateTransmittance(params, samplePosition, sunDirection, sunDistance, 12)
            : float3(0.0f);
        const float3 source =
            params.RayleighScattering * rayleighDensity * rayleighPhase +
            params.MieScattering * mieDensity * miePhase;
        radiance += transmittance * sunTransmittance * source * stepLength;
        transmittance *= exp(-Atmosphere.MediumExtinction(params, altitude) * stepLength);
    }
    const float3 multiScatter =
        Bindless.GetSampleTexture(params.MultiScatterLutId).SampleLevel(
            float2(saturate(sunDirection.y * 0.5f + 0.5f),
                   saturate(cameraAltitude / max(params.TopRadius - params.BottomRadius, 1.0f))), 0.0f).rgb;
    radiance += multiScatter * (1.0f - transmittance);
    Bindless.GetStorageTexture<float4>(params.SkyViewLutId)[dispatchThreadId.xy] =
        float4(max(radiance, float3(0.0f)), 1.0f);
}
