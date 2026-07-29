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
    const uint3 size = Bindless.GetStorageTexture3DDimensions<float4>(params.AerialPerspectiveLutId);
    if (any(dispatchThreadId >= size))
    {
        return;
    }

    const float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2(size.xy);
    const float2 ndc = uv * 2.0f - 1.0f;
    const float4 viewTarget = mul(camera.ProjectionInverseUnJit, float4(ndc, 1.0f, 1.0f));
    const float3 viewDirection = normalize(viewTarget.xyz / max(abs(viewTarget.w), 1.0e-6f));
    const float3 worldDirection = normalize(mul(camera.ModelViewInverse, float4(viewDirection, 0.0f)).xyz);
    const float3 cameraWorld =
        mul(camera.ModelViewInverse, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
    const float cameraAltitude =
        params.WorldOriginAltitude + cameraWorld.y / max(params.WorldUnitsPerKm, 1.0e-3f);
    const float3 origin = float3(0.0f, params.BottomRadius + max(cameraAltitude, 0.001f), 0.0f);
    const float distanceWorld =
        (float(dispatchThreadId.z) + 0.5f) / float(size.z) * params.AerialPerspectiveMaxDistance;
    const float distanceKm = distanceWorld / max(params.WorldUnitsPerKm, 1.0e-3f);

    const uint stepCount = 16;
    const float stepLength = distanceKm / float(stepCount);
    const float3 sunDirection = normalize(camera.SunDirection.xyz);
    const float cosTheta = dot(worldDirection, sunDirection);
    const float rayleighPhase = Atmosphere.RayleighPhase(cosTheta);
    const float miePhase = Atmosphere.MiePhase(cosTheta, params.MiePhaseG);
    float3 transmittance = float3(1.0f);
    float3 inScatter = float3(0.0f);
    for (uint i = 0; i < stepCount; ++i)
    {
        const float t = (float(i) + 0.5f) * stepLength;
        const float3 samplePosition = origin + worldDirection * t;
        const float altitude = max(length(samplePosition) - params.BottomRadius, 0.0f);
        const float rayleighDensity = exp(-altitude / max(params.RayleighDensityH, 1.0e-3f));
        const float mieDensity = exp(-altitude / max(params.MieDensityH, 1.0e-3f));
        const float sunDistance = Atmosphere.RaySphereNearest(samplePosition, sunDirection, params.TopRadius);
        const float3 sunTransmittance = sunDistance > 0.0f
            ? Atmosphere.IntegrateTransmittance(params, samplePosition, sunDirection, sunDistance, 8)
            : float3(0.0f);
        const float3 source =
            params.RayleighScattering * rayleighDensity * rayleighPhase +
            params.MieScattering * mieDensity * miePhase;
        inScatter += transmittance * sunTransmittance * source * stepLength;
        transmittance *= exp(-Atmosphere.MediumExtinction(params, altitude) * stepLength);
    }

    Bindless.GetStorageTexture3D<float4>(params.AerialPerspectiveLutId)[dispatchThreadId] =
        float4(max(inScatter, float3(0.0f)),
               dot(transmittance, float3(0.3333333f)));
}
