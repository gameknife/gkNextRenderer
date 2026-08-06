import Shader.Bindless;
import Shader.PathTracing;
import Shader.VisibilityBuffer;
#include "common/PreProcessor.slang"

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
    int2 pixel = int2(DTid.xy);
    if (DTid.x >= size.x || DTid.y >= size.y)
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

            // Finite lights (GPUScene LightObjects, point or quad emitters). This renderer has
            // neither a ray tracer nor a reservoir, so every light is evaluated deterministically
            // from its centre and shaded with the same Lambert + GGX pair the sun uses below.
            // Visibility is the optional coarse screen-space contact shadow.
            GPUScene lightScene = Bindless.GetGpuscene();
            if (Camera.LightCount > 0 && lightScene.SoftMeshShader.Lights != 0)
            {
                const float shadowDistance = max(Camera.LightObjectShadowDistance, 0.0f);
                const bool traceShadow =
                    Camera.LightObjectScreenSpaceShadow && shadowDistance > 0.0f;

                // The trace dominates the cost of this loop and the loop covers every light in the
                // scene, so bound it: find the strongest contributor at this pixel first, then
                // trace only lights within a fraction of it. A light far below the strongest is
                // washed out here, so its shadow edge would not be visible anyway.
                float shadowThreshold = 0.0f;
                if (traceShadow)
                {
                    float strongest = 0.0f;
                    for (uint probeIndex = 0; probeIndex < Camera.LightCount; ++probeIndex)
                    {
                        float3 probeL;
                        float3 probeIncident;
                        float3 probePosition;
                        if (Common.EvaluateLightIncident(probeIndex, float2(0.5f, 0.5f),
                                                         hitVertex.Position, probeL, probeIncident,
                                                         probePosition))
                        {
                            strongest = max(strongest,
                                            LightContribution(probeIncident, max(dot(N, probeL), 0.0f)));
                        }
                    }
                    shadowThreshold = strongest * 0.2f;
                }

                for (uint lightIndex = 0; lightIndex < Camera.LightCount; ++lightIndex)
                {
                    float3 L;
                    float3 incident;
                    float3 lightPosition;
                    if (!Common.EvaluateLightIncident(lightIndex, float2(0.5f, 0.5f),
                                                      hitVertex.Position, L, incident, lightPosition))
                    {
                        continue;
                    }

                    const float NoL = max(dot(N, L), 0.0f);
                    const float contribution = LightContribution(incident, NoL);
                    if (contribution < 1e-4f)
                    {
                        continue;
                    }

                    float3 radiance = incident * NoL;
                    if (traceShadow && contribution >= shadowThreshold)
                    {
                        radiance *= Common.ScreenSpaceShadowVisibility(
                            hitVertex.Position, N, lightPosition, shadowDistance, uint2(pixel),
                            Camera.PrimaryRayJitter);
                    }

                    direct += kD * albedo * radiance * M_1_PI;
                    specularColor.rgb +=
                        SpecularBRDF(N, V, L, NoV, NoL, roughness, f0) * radiance;
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
