import Shader.Bindless;
import Shader.PathTracing;
import Shader.VisibilityBuffer;
#include "common/PreProcessor.slang"
#include "common/CheckerboardRendering.slang"

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
    const UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    return camera.ForceBlackBackground
        ? float4(0.0f, 0.0f, 0.0f, 1.0f)
        : Common.SampleSkyRadiance(direction, 0.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    uint2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
    const uint2 shadingPixel = ResolveShadingPixel(DTid.xy);
    int2 pixel = int2(shadingPixel);
    if (any(shadingPixel >= size))
    {
        return;
    }

    float2 uv = ((float2(pixel) + 0.5f) / float2(size)) * 2.0 - 1.0;
    float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
    float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    float4 rayDir4 = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
    float3 primaryRayDir = normalize(rayDir4.xyz);

    uint2 vBuffer = Visibility.Load(pixel, Bindless.GetGpuscene().CustomData0);
    if (!Visibility.IsValid(vBuffer))
    {
        float4 skyColor = VisibleBackground(primaryRayDir);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, skyColor);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, float4(0));
        Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0).Store(pixel, 0);
        Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, 1000);
        float2 skyMotionPixels =
            Common.CalculateSkyMotionVector(Camera, primaryRayDir) * float2(size);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(
            pixel, float4(skyMotionPixels, 0, 0));
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(pixel, float4(0));
        return;
    }

    GPUVertex* vertices = Bindless.GetGpuscene().Vertices;
    uint* indices = Bindless.GetGpuscene().Indices;
    NodeProxy* nodes = Bindless.GetGpuscene().Nodes;
    ModelData* models = Bindless.GetGpuscene().Offsets;

    Vertex hitVertex;
    if (!Common.get_material_data(
            vBuffer, origin.xyz, primaryRayDir, nodes, models, vertices, indices, hitVertex))
    {
        // A stale or numerically degenerate visibility sample must not feed invalid
        // attributes into the G-buffer. Treat it as an uncovered sample.
        float4 skyColor = VisibleBackground(primaryRayDir);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, skyColor);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, float4(0));
        Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0).Store(pixel, 0);
        Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, 1000);
        float2 skyMotionPixels =
            Common.CalculateSkyMotionVector(Camera, primaryRayDir) * float2(size);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(
            pixel, float4(skyMotionPixels, 0, 0));
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(pixel, float4(0));
        return;
    }

    NodeProxy hitNode = nodes[vBuffer.x - 1];
    hitVertex.MaterialIndex = FetchMaterialId(hitNode, hitVertex.MaterialIndex);

    float4 color;
    float4 specularColor = float4(0);
    float4 ambientColor = float4(0);
    float4 primaryAlbedo = float4(1, 1, 1, 1);

    {
        Material material = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
        float4 gbuffer;
        Common.FetchGBuffer(hitVertex, material, hitNode, primaryRayDir, primaryAlbedo, gbuffer);

        if (material.MaterialModel == MaterialDiffuseLight)
        {
            color = min(primaryAlbedo, float4(1000, 1000, 1000, 1));
        }
        else
        {
            float3 N = normalize(gbuffer.xyz);
            float3 V = normalize(origin.xyz - hitVertex.Position);
            float3 R = reflect(-V, N);
            float roughness = clamp(gbuffer.a, 0.04f, 1.0f);
            float metalness = saturate(material.Metalness);
            if (material.MRATextureId >= 0)
            {
                float4 mra = Bindless.GetSampleTexture(material.MRATextureId).SampleLevel(hitVertex.TexCoord, 0);
                metalness = saturate(metalness * mra.b);
            }
            if (material.MaterialModel == MaterialMetallic)
            {
                metalness = 1.0f;
            }
            if (material.MaterialModel == MaterialDielectric)
            {
                metalness = 0.0f;
            }

            float3 albedo = Camera.DebugDraw_Lighting ? float3(1.0f, 1.0f, 1.0f) : primaryAlbedo.rgb;
            float3 f0 = MaterialF0(material, albedo, metalness);
            float NoV = max(dot(N, V), 0.001f);
            float3 kS = FresnelSchlickRoughness(NoV, f0, roughness);
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
            ambientColor = float4(ambient, 1.0f);

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
                const Common.FLightCell lightCell = Common.GetLightCell(hitVertex.Position);

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
                                                             hitVertex.Position, probeL,
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
                                                         hitVertex.Position, shadowL,
                                                         shadowIncident, shadowPosition))
                        {
                            lightVisibility = Common.ScreenSpaceShadowVisibility(
                                hitVertex.Position, N, shadowPosition, shadowDistance,
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
                        if (!Common.EvaluateLightIncident(lightIndex, lightUV, hitVertex.Position,
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
                            Common.GetAreaLightSpecularUV(lightIndex, hitVertex.Position, R);
                        float3 specularL;
                        float3 specularIncident;
                        float3 specularPosition;
                        if (Common.EvaluateLightIncident(lightIndex, specularUV,
                                                         hitVertex.Position, specularL,
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
                float viewDist = -mul(Camera.ModelView, float4(hitVertex.Position, 1.0f)).z;
                float shadow = Common.SampleSunShadowCSM(hitVertex.Position, N, viewDist);
                float3 sunRadiance = Camera.SunColor.rgb * (NoL * shadow);

                direct += kD * albedo * sunRadiance * M_1_PI;
                specularColor.rgb += SpecularBRDF(N, V, L, NoV, NoL, roughness, f0) * sunRadiance;
            }

            // GTAO is applied only to the sky term in Process.GTAOCompose. Direct sun,
            // specular, and emissive lighting remain untouched.
            color = float4(direct, 1.0f);
        }
    }

    float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    float2 motion = Common.CalculateMotionVector(Camera, hitNode, hitVertex, pixel) * float2(size);
    const bool selected = hitNode.reserved1 != 0;
    const bool hovered = (hitNode.reserved2 & 1u) != 0;
    const bool locked = (hitNode.reserved2 & 2u) != 0;
    const bool danger = (hitNode.reserved2 & 4u) != 0;
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(
        pixel, float4(color.rgb + specularColor.rgb, color.a));
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, ambientColor);
    Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJECTID_0).Store(pixel, Common.EncodeObjectId(hitNode.instanceId, selected, hovered, locked, danger));
    Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, ndcDepth);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(pixel, float4(motion, 0, 0));
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(pixel, float4(normalize(hitVertex.Normal), 1));
}
