#include "common/PreProcessor.slang"

import Common;

[[vk::binding(0)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(2)]] RWTexture2D<float4> OutImage;
[[vk::binding(3)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(4)]] StructuredBuffer<float> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;

[[vk::binding(9)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(10)]] RaytracingAccelerationStructure Scene;

[[vk::binding(13)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(14)]] RWTexture2D<float4> OutNormalBuffer;

[[vk::binding(16)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(17)]] RWStructuredBuffer<AmbientCube> FarCubes;
[[vk::binding(18)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(19)]] StructuredBuffer<LightObject> Lights;

[[vk::binding(0, 1)]] Sampler2D TextureArray[];
[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FSoftwareRayTracingRenderer renderer = { Vertices, Indices };
    
    int2 ipos = int2(DTid.xy);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    int2 size;
    MiniGBuffer.GetDimensions(size.x, size.y);
    uint2 vBuffer = MiniGBuffer[ipos].rg;
    float2 uv = float2(ipos) / float2(size) * 2.0 - 1.0;

    float4 origin = mul(Camera.ModelViewInverse, float4(0, 0, 0, 1));
    float4 target = mul(Camera.ProjectionInverse, float4(uv.x, uv.y, 1, 1));
    float4 dir = mul(Camera.ModelViewInverse, float4(normalize(target.xyz), 0));

    float3 ray_dir = normalize(dir.xyz);

    Vertex v = Common.get_material_data(ipos, vBuffer, origin.xyz, ray_dir, NodeProxies, Vertices);
    NodeProxy node = NodeProxies[vBuffer.x - 1];

    // geometry info
    float3 normal = normalize(v.Normal.rgb);
    float3 tangent = normalize(v.Tangent.rgb);
    float3 bitangent = cross(normal, tangent);

    // motion info
    float4 currFrameHPos = mul(Camera.ViewProjection, float4(v.Position, 1));
    float2 currfpos = float2((currFrameHPos.xy / currFrameHPos.w * 0.5) * float2(size));
    float4 prevFrameHPos = mul(mul(Camera.PrevViewProjection, node.combinedPrevTS), float4(v.Position, 1));
    float2 prevfpos = float2((prevFrameHPos.xy / prevFrameHPos.w * 0.5) * float2(size));
    float2 motion = prevfpos - currfpos;
    OutMotionVector[ipos] = float4(motion, 0, 0);

    // mat info
    uint materialIndex = FetchMaterialId(node, v.MaterialIndex);
    Material mat = Materials[materialIndex];
    float4 albedo = mat.Diffuse;
    float roughness = mat.Fuzziness;
    if (mat.DiffuseTextureId >= 0)
    {
        float4 tex = TextureArray[NonUniformResourceIndex(mat.DiffuseTextureId)].Sample(v.TexCoord);
        albedo *= tex;
    }
    if (mat.MRATextureId >= 0)
    {
        float4 mra = TextureArray[NonUniformResourceIndex(mat.MRATextureId)].Sample(v.TexCoord);
        roughness = roughness * mra.g;
    }

    // ibl
    const float dotValue = dot(ray_dir, normal);
    const float3 outwardNormal = dotValue > 0 ? -normal : normal;
    const float cosine = dotValue > 0 ? mat.RefractionIndex * dotValue : -dotValue;
    const float reflectProb = Schlick(cosine, mat.RefractionIndex);
    const float metalProb = mat.Metalness;

    const float3 lightVector = Camera.SunDirection.xyz;
    const float4 d = Camera.SunColor * max(dot(lightVector, normalize(v.Normal.rgb)), 0.0) * M_1_PI;

    float4 indirectColor = float4(0, 0, 0, 0);

    float shadow = 0.0f;
    const uint jitcount = Camera.FastGather ? 1 : max(1, Camera.NumberOfSamples / 4);
    for (int i = 0; i < jitcount; i++) {

        bool chanceReflect = (RandomFloat(RandomSeed) < reflectProb);
        bool chanceMetal = (RandomFloat(RandomSeed) < metalProb);
        bool chanceGGX = chanceReflect || chanceMetal;
        const float3 trace_next = chanceGGX ? reflect(ray_dir, outwardNormal) : outwardNormal;
        float3 trace_dir = chanceGGX ? ggxSampling(RandomSeed, sqrt(roughness), trace_next) : AlignWithNormal(RandomInHemiSphere1(RandomSeed), trace_next);

        float4 reflectionColor;
        float2 jitter2D = (RandomFloat2(RandomSeed) - float2(0.5f)) * 0.04f;
        float3 jit = tangent * jitter2D.x + bitangent * jitter2D.y;
        if (Common.HardWareRayTraceScene(v.Position, trace_dir, normal, materialIndex, ipos, reflectionColor, Materials, Camera, NodeProxies, Vertices, Indices, Offsets, Scene, Cubes, FarCubes))
        {
            indirectColor += reflectionColor;
        }
        else
        {
            if (chanceGGX)
            {
                indirectColor += Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, trace_dir, Camera.SkyRotation, mat.Fuzziness, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0.0, 0.0, 0.0, 0.0);
            }
            else
            {
                indirectColor += interpolateSkyProbes(v.Position, normal, Cubes, FarCubes) * 2.0f;
            }
        }
    }

    indirectColor /= float(jitcount);

    if (Camera.HasSun)
    {
        const float3 lightVector = Camera.SunDirection.xyz;
        const float3 lightVectorCone = AlignWithNormal(RandomInCone(RandomSeed, cos(0.25f / 180.f * M_PI)), lightVector);
        shadow = 1;
        if (Common.HardWareRayOcclusionCheck(Scene, v.Position, lightVectorCone))
        {
            shadow = 0;
        }
    }

    float4 outColor = albedo * float4(indirectColor.rgb, 1);
    outColor += albedo * d * shadow;
    outColor.a = 1.0f;

    OutAlbedoBuffer[ipos] = albedo;
    OutNormalBuffer[ipos] = float4(normal, 1.0);
    OutImage[ipos] = outColor;
}