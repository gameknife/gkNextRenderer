implementing PathTracing;

namespace Common
{
    public bool AtmosphereEnabled(UniformBufferObject camera)
    {
        if (camera.AtmosphereParams == 0)
        {
            return false;
        }
        const FAtmosphereParams params = ((FAtmosphereParams*)camera.AtmosphereParams)[0];
        return (params.Flags & ATMOSPHERE_FLAG_SKY) != 0;
    }

    float2 SkyViewUv(float3 direction, float3 sunDirection)
    {
        direction = normalize(direction);
        sunDirection = normalize(sunDirection);
        const float3 directionHorizontal = normalize(float3(direction.x, 0.0f, direction.z) + float3(1.0e-6f, 0, 0));
        const float3 sunHorizontal = normalize(float3(sunDirection.x, 0.0f, sunDirection.z) + float3(1.0e-6f, 0, 0));
        const float azimuthCos = clamp(dot(directionHorizontal, sunHorizontal), -1.0f, 1.0f);
        const float mappedElevation = sign(direction.y) * sqrt(abs(direction.y));
        return float2(azimuthCos * 0.5f + 0.5f, mappedElevation * 0.5f + 0.5f);
    }

    public float4 SampleSkyRadiance(float3 direction, float roughness)
    {
        const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
        if (!AtmosphereEnabled(camera))
        {
            return camera.HasSky
                ? Common.SampleIBL(camera.SkyIdx, direction, camera.SkyRotation, roughness) *
                    camera.SkyColor * camera.SkyIntensity
                : float4(0.0f, 0.0f, 0.0f, 1.0f);
        }

        const FAtmosphereParams params = ((FAtmosphereParams*)camera.AtmosphereParams)[0];
        const float2 uv = SkyViewUv(direction, camera.SunDirection.xyz);
        const float3 lutRadiance =
            Bindless.GetSampleTexture(params.SkyViewLutId).SampleLevel(uv, 0.0f).rgb;
        const float3 cameraWorld = camera.ModelViewInverse[3].xyz;
        const float cameraAltitude =
            params.WorldOriginAltitude + cameraWorld.y / max(params.WorldUnitsPerKm, 1.0e-3f);
        const float altitudeUv =
            saturate(cameraAltitude / max(params.TopRadius - params.BottomRadius, 1.0f));
        const float3 viewTransmittance =
            Bindless.GetSampleTexture(params.TransmittanceLutId).SampleLevel(
                float2(saturate(direction.y * 0.5f + 0.5f), altitudeUv), 0.0f).rgb;
        const float3 sunTransmittance =
            Bindless.GetSampleTexture(params.TransmittanceLutId).SampleLevel(
                float2(saturate(camera.SunDirection.y * 0.5f + 0.5f), altitudeUv), 0.0f).rgb;
        const float3 scattering = params.RayleighScattering + params.MieScattering;
        const float3 extinction = scattering + params.MieAbsorption + params.OzoneAbsorption;
        const float cosTheta = dot(normalize(direction), normalize(camera.SunDirection.xyz));
        const float phase =
            Atmosphere.RayleighPhase(cosTheta) + Atmosphere.MiePhase(cosTheta, params.MiePhaseG);
        const float3 analyticSingleScatter =
            (1.0f - viewTransmittance) * (scattering / max(extinction, float3(1.0e-4f))) *
            sunTransmittance * phase;
        const float3 unitRadiance = max(lutRadiance, analyticSingleScatter);
        return float4(
            max(unitRadiance, float3(0.0f)) * camera.SunColor.rgb *
                camera.SkyColor.rgb * params.SkyLuminanceScale,
            1.0f);
    }

    public float4 SampleSkyIrradiance(float3 normal)
    {
        const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
        if (!AtmosphereEnabled(camera))
        {
            return camera.HasSky
                ? Common.SampleIBLDiffuse(camera.SkyIdx, normal, camera.SkyRotation) *
                    camera.SkyColor * camera.SkyIntensity
                : float4(0.0f);
        }

        // Four deterministic cosine-weighted taps keep diffuse consumers and the ambient-cube
        // baker on the same procedural source without adding renderer-specific code.
        const float3 n = normalize(normal);
        const float3 helper = abs(n.y) < 0.99f ? float3(0, 1, 0) : float3(1, 0, 0);
        const float3 tangent = normalize(cross(helper, n));
        const float3 bitangent = cross(n, tangent);
        float3 irradiance = SampleSkyRadiance(n, 1.0f).rgb;
        irradiance += SampleSkyRadiance(normalize(n + tangent), 1.0f).rgb;
        irradiance += SampleSkyRadiance(normalize(n - tangent), 1.0f).rgb;
        irradiance += SampleSkyRadiance(normalize(n + bitangent), 1.0f).rgb;
        irradiance += SampleSkyRadiance(normalize(n - bitangent), 1.0f).rgb;
        return float4(irradiance * (3.14159265359f / 5.0f), 1.0f);
    }
}
