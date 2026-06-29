import Common;
import Bindless;
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

    float2 uv = (float2(pixel) / float2(size)) * 2.0 - 1.0;
    float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
    float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    float4 rayDir4 = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));
    float3 primaryRayDir = normalize(rayDir4.xyz);

    uint packedValue = Bindless.GetViewStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[pixel];
    uint2 vBuffer = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF);
    if (vBuffer.x == 0)
    {
        float4 skyColor = Camera.HasSky
            ? Common.SampleIBL(Camera.SkyIdx, primaryRayDir, Camera.SkyRotation, 0) * Camera.SkyIntensity
            : float4(0, 0, 0, 1);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, skyColor);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR).Store(pixel, float4(0));
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, float4(0));
        Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(pixel, 0);
        Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, 1000);
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(pixel, float4(0));
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(pixel, float4(0));
        Bindless.GetViewStorageTexture<uint>(Bindless.RT_MOTIONMOMENT).Store(pixel, Camera.TemporalFrames);
        return;
    }

    GPUVertex* vertices = Bindless.GetGpuscene().Vertices;
    uint* indices = Bindless.GetGpuscene().Indices;
    NodeProxy* nodes = Bindless.GetGpuscene().Nodes;
    ModelData* models = Bindless.GetGpuscene().Offsets;

    NodeProxy hitNode = nodes[vBuffer.x - 1];
    ModelData model = models[hitNode.modelId];
    if (hitNode.skinId != 0xFFFFFFFF)
    {
        vertices = Bindless.GetGpuscene().SkinnedVertices;
    }

    uint indexBase = model.indexOffset + vBuffer.y * 3;
    Vertex v0 = UnpackVertex(vertices[model.vertexOffset + indices[indexBase]]);
    Vertex v1 = UnpackVertex(vertices[model.vertexOffset + indices[indexBase + 1]]);
    Vertex v2 = UnpackVertex(vertices[model.vertexOffset + indices[indexBase + 2]]);

    float3 p0 = mul(hitNode.worldTS, float4(v0.Position, 1)).xyz;
    float3 p1 = mul(hitNode.worldTS, float4(v1.Position, 1)).xyz;
    float3 p2 = mul(hitNode.worldTS, float4(v2.Position, 1)).xyz;
    float3 n0 = mul(hitNode.worldTS, float4(v0.Normal, 0)).xyz;
    float3 n1 = mul(hitNode.worldTS, float4(v1.Normal, 0)).xyz;
    float3 n2 = mul(hitNode.worldTS, float4(v2.Normal, 0)).xyz;
    float3 ln0 = v0.Normal;
    float3 ln1 = v1.Normal;
    float3 ln2 = v2.Normal;
    float4 lt0 = v0.Tangent;
    float4 lt1 = v1.Tangent;
    float4 lt2 = v2.Tangent;

    float3 edge0 = p1 - p0;
    float3 edge1 = p2 - p0;
    float3 rayCrossEdge1 = cross(primaryRayDir, edge1);
    float inverseDet = 1.0f / dot(edge0, rayCrossEdge1);
    float3 rayTo0 = origin.xyz - p0;
    float baryY = inverseDet * dot(rayTo0, rayCrossEdge1);
    float baryZ = -inverseDet * dot(primaryRayDir, cross(edge0, rayTo0));
    float baryX = 1.0f - baryY - baryZ;

    Vertex hitVertex = {};
    hitVertex.Position = p0 * baryX + p1 * baryY + p2 * baryZ;
    hitVertex.Normal = normalize(n0 * baryX + n1 * baryY + n2 * baryZ);
    hitVertex.TexCoord = v0.TexCoord * baryX + v1.TexCoord * baryY + v2.TexCoord * baryZ;
    hitVertex.LocalNormal = normalize(ln0 * baryX + ln1 * baryY + ln2 * baryZ);
    float4 localTangent = lt0 * baryX + lt1 * baryY + lt2 * baryZ;
    hitVertex.LocalTangent = float4(normalize(localTangent.xyz), localTangent.w < 0.5f ? -1.0f : 1.0f);
    hitVertex.MaterialIndex = FetchMaterialId(hitNode, v0.MaterialIndex);

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
                float3 irr = Common.SampleIBLDiffuse(Camera.SkyIdx, N, Camera.SkyRotation).rgb * Camera.SkyIntensity;
                ambient = kD * albedo * irr;

                float3 skySpecular = Common.SampleIBL(Camera.SkyIdx, R, Camera.SkyRotation, roughness).rgb *
                    Camera.SkyIntensity;
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

            // GTAO is applied only to the sky term in Process.GTAOCompose. Direct sun
            // (already CSM-shadowed) and emissive lighting remain untouched.
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
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, color);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SINGLE_SPECULAR).Store(pixel, specularColor);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_AMBIENT).Store(pixel, ambientColor);
    Bindless.GetViewStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(pixel, Common.EncodeObjectId(hitNode.instanceId, selected, hovered, locked, danger));
    Bindless.GetViewStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, ndcDepth);
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_MOTIONVECTOR).Store(pixel, float4(motion, 0, 0));
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(pixel, float4(normalize(hitVertex.Normal), 1));
}
