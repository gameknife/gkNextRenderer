import Shader.Bindless;
import Shader.PathTracing;
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

float3 MaterialF0(Material material, float3 albedo, float metalness)
{
    float3 dielectricF0 = material.MaterialModel == MaterialDielectric
        ? DielectricF0(max(material.RefractionIndex, 1.0f))
        : float3(0.04f);
    return lerp(dielectricF0, albedo, metalness);
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

    uint packedValue = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[pixel];
    uint2 vBuffer = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF);
    if (vBuffer.x == 0)
    {
        float4 skyColor = Camera.HasSky
            ? Common.SampleIBL(Camera.SkyIdx, primaryRayDir, Camera.SkyRotation, 0) *
                Camera.SkyColor * Camera.SkyIntensity
            : float4(0, 0, 0, 1);
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
        float4 skyColor = Camera.HasSky
            ? Common.SampleIBL(Camera.SkyIdx, primaryRayDir, Camera.SkyRotation, 0) *
                Camera.SkyColor * Camera.SkyIntensity
            : float4(0, 0, 0, 1);
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
                float3 irr = Common.SampleIBLDiffuse(Camera.SkyIdx, N, Camera.SkyRotation).rgb *
                    Camera.SkyColor.rgb * Camera.SkyIntensity;
                ambient = kD * albedo * irr;

                float3 skySpecular = Common.SampleIBL(Camera.SkyIdx, R, Camera.SkyRotation, roughness).rgb *
                    Camera.SkyColor.rgb * Camera.SkyIntensity;
                specularColor.rgb += skySpecular * kS;
            }
            ambientColor = float4(ambient, 1.0f);

            // 太阳：Lambert + CSM 阴影
            float3 direct = float3(0);
            if (Camera.HasSun)
            {
                float3 L = normalize(Camera.SunDirection.xyz);
                float NoL = max(dot(L, N), 0.0f);
                float viewDist = -mul(Camera.ModelView, float4(hitVertex.Position, 1.0f)).z;
                float shadow = Common.SampleSunShadowCSM(hitVertex.Position, N, viewDist);
                float3 sunRadiance = Camera.SunColor.rgb * (NoL * shadow);

                float3 H = normalize(V + L);
                float NoH = max(dot(N, H), 0.0f);
                float VoH = max(dot(V, H), 0.0f);
                float D = DistributionGGX(NoH, roughness);
                float G = GeometrySmith(NoV, NoL, roughness);
                float3 F = FresnelSchlick(VoH, f0);
                float3 specularBrdf = (D * G * F) / max(4.0f * NoV * NoL, 1e-5f);

                direct = kD * albedo * sunRadiance * M_1_PI;
                specularColor.rgb += specularBrdf * sunRadiance;
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
