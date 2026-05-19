import Common;
import Bindless;
#include "common/PreProcessor.slang"

uint SelectCascade(float viewDist, float4 splits)
{
    if (viewDist < splits.x) return 0;
    if (viewDist < splits.y) return 1;
    if (viewDist < splits.z) return 2;
    return 3;
}

float SampleShadowCSM(float3 worldPos, float3 normal, float viewDist, UniformBufferObject Camera)
{
    uint c = SelectCascade(viewDist, Camera.CascadeSplits);
    float4 lightPos = mul(Camera.SunCascadeViewProjection[c], float4(worldPos, 1.0));
    float3 projCoords = lightPos.xyz / lightPos.w;
    float2 uv = projCoords.xy * 0.5 + 0.5;

    // 出 cascade 视椎视为无遮挡
    if (any(uv < float2(0.0)) || any(uv > float2(1.0)))
    {
        return 1.0;
    }
    if (projCoords.z < 0.0 || projCoords.z > 1.0)
    {
        return 1.0;
    }

    float NoL = max(dot(normalize(normal), Camera.SunDirection.xyz), 0.0);
    float bias = max(0.0008f * (1.0f - NoL), 0.0001f);

    Sampler2D shadowMap = Bindless.GetShadowMap(c);
    uint2 texSize;
    shadowMap.GetDimensions(texSize.x, texSize.y);
    float2 texelSize = 1.0f / float2(texSize);

    float shadow = 0.0f;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            float2 offset = float2(dx, dy) * texelSize;
            float closestDepth = shadowMap.SampleLevel(uv + offset, 0).r;
            shadow += (projCoords.z - bias) > closestDepth ? 0.0f : 1.0f;
        }
    }
    return shadow * (1.0f / 9.0f);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    let Camera = Bindless.GetGpuscene().Camera[0];
    uint2 size = Bindless.GetStorageTextureDimensions<float4>(Bindless.RT_SINGLE_DIFFUSE);
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

    uint packedValue = Bindless.GetStorageTexture<uint>(Bindless.RT_MINIGBUFFER)[pixel];
    uint2 vBuffer = uint2((packedValue >> 17) & 0x7FFF, packedValue & 0x1FFFF);
    if (vBuffer.x == 0)
    {
        float4 skyColor = Camera.HasSky
            ? Common.SampleIBL(Camera.SkyIdx, primaryRayDir, Camera.SkyRotation, 0) * Camera.SkyIntensity
            : float4(0, 0, 0, 1);
        Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, skyColor);
        Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO).Store(pixel, float4(1, 1, 1, 1));
        Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(pixel, 0);
        Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, 1000);
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
    hitVertex.MaterialIndex = FetchMaterialId(hitNode, v0.MaterialIndex);

    Material material = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex);
    float4 primaryAlbedo = material.Diffuse;
    if (material.DiffuseTextureId >= 0)
    {
        primaryAlbedo *= Bindless.GetSampleTexture(material.DiffuseTextureId).SampleLevel(hitVertex.TexCoord, 0);
    }

    float4 color;
    if (material.MaterialModel == MaterialDiffuseLight)
    {
        color = min(material.Diffuse, float4(1000, 1000, 1000, 1));
    }
    else
    {
        float3 N = normalize(hitVertex.Normal);
        float3 albedo = primaryAlbedo.rgb;

        // 天光：HDRSH irradiance (SampleIBLRough 走 EvaluateSH，相当于 SH-irradiance)
        float3 ambient = float3(0);
        if (Camera.HasSky)
        {
            float3 irr = Common.SampleIBLRough(Camera.SkyIdx, N, Camera.SkyRotation).rgb * Camera.SkyIntensity;
            ambient = albedo * irr;
        }

        // 太阳：Lambert + CSM 阴影
        float3 direct = float3(0);
        if (Camera.HasSun)
        {
            float NoL = max(dot(Camera.SunDirection.xyz, N), 0.0f);
            float viewDist = -mul(Camera.ModelView, float4(hitVertex.Position, 1.0f)).z;
            float shadow = SampleShadowCSM(hitVertex.Position, N, viewDist, Camera);
            direct = albedo * Camera.SunColor.rgb * (NoL * shadow * M_1_PI);
        }

        color = float4(ambient + direct, 1.0f);
    }

    float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    const bool selected = hitNode.reserved1 != 0;
    const bool hovered = (hitNode.reserved2 & 1u) != 0;
    const bool locked = (hitNode.reserved2 & 2u) != 0;
    const bool danger = (hitNode.reserved2 & 4u) != 0;
    Bindless.GetStorageTexture<float4>(Bindless.RT_SINGLE_DIFFUSE).Store(pixel, color);
    Bindless.GetStorageTexture<float4>(Bindless.RT_ALBEDO).Store(pixel, primaryAlbedo);
    Bindless.GetStorageTexture<uint>(Bindless.RT_OBJEDCTID_0).Store(pixel, Common.EncodeObjectId(hitNode.instanceId, selected, hovered, locked, danger));
    Bindless.GetStorageTexture<float>(Bindless.RT_PREV_DEPTHBUFFER).Store(pixel, ndcDepth);
}
