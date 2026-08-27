#ifndef GK_NEXT_NO_AMBIENT_SHADING_SLANG
#define GK_NEXT_NO_AMBIENT_SHADING_SLANG

// SoftwareModernNoAmbient shading kernels, shared by the three scheduler bucket entries
// (Core.SwModernNoAmbient{Standard,Background,Emissive}).
//
// The split is the whole point of the surface refactor: this file knows how to light a resolved
// surface and nothing about where that surface came from. Anything that decodes the visibility
// buffer belongs in Core.SurfaceBuild, never here.
//
// Requires: Shader.Bindless, Shader.PathTracing and common/PreProcessor.slang.

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    float f = 1.0f - saturate(cosTheta);
    float f2 = f * f;
    return f0 + (1.0f - f0) * (f2 * f2 * f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    float f = 1.0f - saturate(cosTheta);
    float f2 = f * f;
    return f0 + (max(float3(1.0f - roughness), f0) - f0) * (f2 * f2 * f);
}

float DistributionGGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NoH * NoH * (a2 - 1.0f) + 1.0f;
    return a2 / max(M_PI * denom * denom, 1e-5f);
}

float GeometrySchlickGGX(float NoV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) * 0.125f;
    return NoV / max(NoV * (1.0f - k) + k, 1e-5f);
}

float GeometrySmith(float NoV, float NoL, float roughness)
{
    return GeometrySchlickGGX(NoV, roughness) * GeometrySchlickGGX(NoL, roughness);
}

float3 DielectricF0(float refractionIndex)
{
    float f = (refractionIndex - 1.0f) / max(refractionIndex + 1.0f, 1e-5f);
    return float3(f * f);
}

// Cook-Torrance specular, shared by the sun and every finite light so both analytic light types
// reflect off a surface the same way.
float3 SpecularBRDF(float3 N, float3 V, float3 L, float NoV, float NoL, float roughness, float3 f0)
{
    const float3 H = normalize(V + L);
    const float NoH = max(dot(N, H), 0.0f);
    const float VoH = max(dot(V, H), 0.0f);
    const float D = DistributionGGX(NoH, roughness);
    const float G = GeometrySmith(NoV, NoL, roughness);
    const float3 F = FresnelSchlick(VoH, f0);
    return (D * G * F) / max(4.0f * NoV * NoL, 1e-5f);
}

// Perceived brightness a light lands on the receiver before shadowing, used both to skip lights
// that cannot change the pixel and to rank which ones deserve a screen-space shadow trace.
float LightContribution(float3 incidentWeight, float NoL)
{
    return dot(incidentWeight * NoL, float3(0.2126f, 0.7152f, 0.0722f));
}

// A 2x2 Gauss-Legendre rule integrates a rectangular emitter over its actual p0/p1/p3
// footprint. Unlike a centre sample, these four directions produce near-field diffuse falloff and
// a spatial penumbra while remaining deterministic for the temporal pass.
float2 AreaLightQuadratureUV(uint sampleIndex)
{
    const float low = 0.2113248654f;
    const float high = 0.7886751346f;
    return float2((sampleIndex & 1u) != 0u ? high : low,
                  (sampleIndex & 2u) != 0u ? high : low);
}

// Each pixel starts at a decorrelated corner and rotates through all four quadrature samples.
// Native TAAU integrates the resulting binary visibility into a stable penumbra. Without temporal
// jitter use the centre so a non-temporal renderer gets a stable, conservative approximation.
float2 AreaLightShadowUV(uint2 pixel, uint frame, bool temporalDither)
{
    if (!temporalDither)
    {
        return float2(0.5f);
    }
    const float seed = dot(float2(pixel), float2(0.06711056f, 0.00583715f));
    const uint spatialOffset = uint(frac(52.9829189f * frac(seed)) * 4.0f);
    return AreaLightQuadratureUV((spatialOffset + frame) & 3u);
}

float3 MaterialF0(Material material, float3 albedo, float metalness)
{
    float3 dielectricF0 = material.MaterialModel == MaterialDielectric
        ? DielectricF0(max(material.RefractionIndex, 1.0f))
        : float3(0.04f);
    return lerp(dielectricF0, albedo, metalness);
}

float4 VisibleBackground(float3 direction)
{
    return Common.SampleVisibleBackground(direction);
}

// A surface resolved far enough to be lit. Both entry paths fill this in; the kernel below cannot
// tell them apart, which is what makes the surface path a pure refactor at full rate.
struct FNoAmbientShadingInput
{
    float3 Position;
    float3 Normal;      // world shading normal, normal map already applied
    float Roughness;    // perceptual, MRA already applied
    float Metalness;    // effective, MRA and material-model override already applied
    float4 Albedo;      // base color, diffuse texture already multiplied in
    uint MaterialId;    // resolved GPUScene material index
    // Sky-occlusion multiplier for the ambient term, strength already applied. On the Primary
    // Surface path GTAO runs before shading, so the caller supplies it here and Process.GTAOCompose
    // no longer upsamples. The legacy path has no AO available yet and passes 1.
    float SkyOcclusion;
};

struct FNoAmbientShadingOutput
{
    float4 Diffuse; // RT_SINGLE_DIFFUSE payload: direct + specular
    float4 Ambient; // RT_AMBIENT payload: sky diffuse, GTAO is applied later by Process.GTAOCompose
};

// Background shading method: the sky the primary ray escaped into. Its own scheduler bucket.
FNoAmbientShadingOutput ShadeNoAmbientBackground(float3 primaryRayDir)
{
    FNoAmbientShadingOutput result;
    result.Diffuse = VisibleBackground(primaryRayDir);
    result.Ambient = float4(0, 0, 0, 0);
    return result;
}

// Emissive shading method: emitted radiance, no BRDF, no lights, no ambient. Its own scheduler
// bucket, and the reason Background/Emissive were the first real buckets - they already existed as
// early-outs inside every Core Shading kernel.
FNoAmbientShadingOutput ShadeNoAmbientEmissive(float4 albedo)
{
    FNoAmbientShadingOutput result;
    result.Diffuse = min(albedo, float4(1000, 1000, 1000, 1));
    result.Ambient = float4(0, 0, 0, 0);
    return result;
}

// Standard shading method: sky irradiance + finite lights + sun, with a Cook-Torrance specular
// lobe. Contains no miss and no emissive branch, which is exactly what the scheduler buys.
FNoAmbientShadingOutput ShadeNoAmbientStandard(FNoAmbientShadingInput surface, int2 pixel)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    const float3 eye = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1)).xyz;
    const Material material = Bindless.GetGpuscene().GetMaterial(surface.MaterialId);

    FNoAmbientShadingOutput result;
    result.Diffuse = float4(0, 0, 0, 1);
    result.Ambient = float4(0, 0, 0, 0);

    float4 specularColor = float4(0);
    const float3 N = normalize(surface.Normal);
    const float3 V = normalize(eye - surface.Position);
    const float3 R = reflect(-V, N);
    const float roughness = clamp(surface.Roughness, 0.04f, 1.0f);
    const float metalness = saturate(surface.Metalness);

    const float3 albedo = Camera.DebugDraw_Lighting ? float3(1.0f, 1.0f, 1.0f) : surface.Albedo.rgb;
    const float3 f0 = MaterialF0(material, albedo, metalness);
    const float NoV = max(dot(N, V), 0.001f);
    const float3 kS = FresnelSchlickRoughness(NoV, f0, roughness);
    float3 kD = (1.0f - kS) * (1.0f - metalness);
    if (material.MaterialModel == MaterialDielectric)
    {
        kD = float3(0.0f);
    }

    // 天光：HDR radiance SH 经 Lambert cosine kernel 卷积后的 diffuse radiance。
    float3 ambient = float3(0);
    if (Camera.HasSky)
    {
        float3 irr = Common.SampleSkyRadiance(N, 1).rgb;
        ambient = kD * albedo * irr;

        float3 skySpecular = Common.SampleSkyRadiance(R, roughness).rgb;
        specularColor.rgb += skySpecular * kS;
    }
    // GTAO occludes sky diffuse and nothing else. Applying it here rather than to the composed
    // total is what keeps CSM sun light, specular and emissive out of its reach.
    result.Ambient = float4(ambient * surface.SkyOcclusion, 1.0f);

    float3 direct = float3(0);

    // Finite lights (GPUScene LightObjects, point or quad emitters). Point lights retain a
    // single delta sample. Quad lights use deterministic 2x2 Gaussian quadrature across
    // p0/p1/p3, so their declared extent and orientation affect diffuse, GGX highlights,
    // falloff and the optional coarse screen-space penumbra.
    GPUScene lightScene = Bindless.GetGpuscene();
    if (Camera.LightCount > 0 && lightScene.SoftMeshShader.Lights != 0)
    {
        const float shadowDistance = max(Camera.LightObjectShadowDistance, 0.0f);
        const bool traceShadow =
            Camera.LightObjectScreenSpaceShadow && shadowDistance > 0.0f &&
            Camera.LightObjectMaxShadowedLights > 0u;

        // Resolve the lights that matter here once. Inside the light grid this is a
        // bounded set; outside every cascade the cell reports a global fallback and both
        // loops below degrade to the full scene, which stays correct but unaccelerated.
        const Common.FLightCell lightCell = Common.GetLightCell(surface.Position);

        // Shadow marching dominates this pass. Select exactly the two strongest lights at
        // this pixel instead of using a relative threshold that can admit every similarly
        // bright light in a cell. Lighting still sums the full cell; only visibility is
        // budgeted. The ranking stays per pixel because cell-centre ordering is too coarse
        // near cell borders.
        uint shadowLight0 = 0xFFFFFFFFu;
        uint shadowLight1 = 0xFFFFFFFFu;
        float shadowWeight0 = 0.0f;
        float shadowWeight1 = 0.0f;
        if (traceShadow)
        {
            for (uint probeSlot = 0; probeSlot < lightCell.Count; ++probeSlot)
            {
                const uint probeLightIndex = lightCell.GetLightIndex(probeSlot);
                const uint probeSampleCount =
                    Common.GetLightQuadratureSampleCount(probeLightIndex);
                float3 probeIrradiance = float3(0.0f);
                for (uint probeSample = 0u; probeSample < probeSampleCount; ++probeSample)
                {
                    const float2 probeUV = probeSampleCount == 1u
                        ? float2(0.5f)
                        : AreaLightQuadratureUV(probeSample);
                    float3 probeL;
                    float3 probeIncident;
                    float3 probePosition;
                    if (Common.EvaluateLightIncident(probeLightIndex, probeUV,
                                                     surface.Position, probeL,
                                                     probeIncident, probePosition))
                    {
                        probeIrradiance += probeIncident * max(dot(N, probeL), 0.0f) /
                                           float(probeSampleCount);
                    }
                }
                const float probeWeight = LightContribution(probeIrradiance, 1.0f);
                if (probeWeight > shadowWeight0)
                {
                    shadowWeight1 = shadowWeight0;
                    shadowLight1 = shadowLight0;
                    shadowWeight0 = probeWeight;
                    shadowLight0 = probeLightIndex;
                }
                else if (probeWeight > shadowWeight1)
                {
                    shadowWeight1 = probeWeight;
                    shadowLight1 = probeLightIndex;
                }
            }
        }

        for (uint lightSlot = 0; lightSlot < lightCell.Count; ++lightSlot)
        {
            const uint lightIndex = lightCell.GetLightIndex(lightSlot);
            const uint lightSampleCount =
                Common.GetLightQuadratureSampleCount(lightIndex);
            float lightVisibility = 1.0f;
            if (traceShadow &&
                (lightIndex == shadowLight0 ||
                 (Camera.LightObjectMaxShadowedLights > 1u && lightIndex == shadowLight1)))
            {
                const float2 shadowUV = lightSampleCount == 1u
                    ? float2(0.5f)
                    : AreaLightShadowUV(uint2(pixel), Camera.TotalFrames,
                                        Camera.PrimaryRayJitter);
                float3 shadowL;
                float3 shadowIncident;
                float3 shadowPosition;
                if (Common.EvaluateLightIncident(lightIndex, shadowUV,
                                                 surface.Position, shadowL,
                                                 shadowIncident, shadowPosition))
                {
                    lightVisibility = Common.ScreenSpaceShadowVisibility(
                        surface.Position, N, shadowPosition, shadowDistance,
                        uint2(pixel), Camera.PrimaryRayJitter);
                }
            }

            for (uint lightSample = 0u; lightSample < lightSampleCount; ++lightSample)
            {
                const float2 lightUV = lightSampleCount == 1u
                    ? float2(0.5f)
                    : AreaLightQuadratureUV(lightSample);
                float3 L;
                float3 incident;
                float3 lightPosition;
                if (!Common.EvaluateLightIncident(lightIndex, lightUV, surface.Position,
                                                  L, incident, lightPosition))
                {
                    continue;
                }

                const float NoL = max(dot(N, L), 0.0f);
                const float contribution = LightContribution(incident, NoL);
                if (contribution < 1e-4f)
                {
                    continue;
                }

                const float3 radiance =
                    incident * (NoL * lightVisibility / float(lightSampleCount));

                direct += kD * albedo * radiance * M_1_PI;
                if (lightSampleCount == 1u)
                {
                    specularColor.rgb +=
                        SpecularBRDF(N, V, L, NoV, NoL, roughness, f0) * radiance;
                }
            }

            if (lightSampleCount > 1u)
            {
                // Glossy area-light response uses a continuous representative point on
                // the declared rectangle. Diffuse and visibility above remain integrated
                // over its area; separating the two prevents four discrete GGX highlights.
                const float2 specularUV =
                    Common.GetAreaLightSpecularUV(lightIndex, surface.Position, R);
                float3 specularL;
                float3 specularIncident;
                float3 specularPosition;
                if (Common.EvaluateLightIncident(lightIndex, specularUV,
                                                 surface.Position, specularL,
                                                 specularIncident, specularPosition))
                {
                    const float specularNoL = max(dot(N, specularL), 0.0f);
                    const float specularContribution =
                        LightContribution(specularIncident, specularNoL);
                    if (specularContribution >= 1e-4f)
                    {
                        const float3 specularRadiance =
                            specularIncident * (specularNoL * lightVisibility);
                        specularColor.rgb += SpecularBRDF(
                            N, V, specularL, NoV, specularNoL, roughness, f0) *
                            specularRadiance;
                    }
                }
            }
        }
    }

    // 太阳：Lambert + CSM 阴影
    if (Camera.HasSun)
    {
        float3 L = normalize(Camera.SunDirection.xyz);
        float NoL = max(dot(L, N), 0.0f);
        float viewDist = -mul(Camera.ModelView, float4(surface.Position, 1.0f)).z;
        float shadow = Common.SampleSunShadowCSM(surface.Position, N, viewDist);
        float3 sunRadiance = Camera.SunColor.rgb * (NoL * shadow);

        direct += kD * albedo * sunRadiance * M_1_PI;
        specularColor.rgb += SpecularBRDF(N, V, L, NoV, NoL, roughness, f0) * sunRadiance;
    }

    // GTAO is applied only to the sky term in Process.GTAOCompose. Direct sun,
    // specular, and emissive lighting remain untouched.
    result.Diffuse = float4(direct + specularColor.rgb, 1.0f);
    return result;
}

// Full-screen entry used by the analytic allocation (scheduler off) and by the legacy inline path,
// where one dispatch has to cope with every shading method. The scheduler replaces this dispatch
// with one per bucket; the arithmetic is the same either way.
FNoAmbientShadingOutput ShadeNoAmbientSurface(FNoAmbientShadingInput surface, int2 pixel)
{
    const Material material = Bindless.GetGpuscene().GetMaterial(surface.MaterialId);
    if (material.MaterialModel == MaterialDiffuseLight)
    {
        return ShadeNoAmbientEmissive(surface.Albedo);
    }
    return ShadeNoAmbientStandard(surface, pixel);
}

#endif
