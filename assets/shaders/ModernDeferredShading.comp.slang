#include "common/PreProcessor.slang"

import Common;

[[vk::binding(0)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(1)]] RWTexture2D<float4> OutImage;
[[vk::binding(2)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(4)]] StructuredBuffer<float> Vertices;
[[vk::binding(5)]] StructuredBuffer<uint> Indices;
[[vk::binding(6)]] StructuredBuffer<Material> Materials;
[[vk::binding(7)]] StructuredBuffer<uint2> Offsets;
[[vk::binding(8)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(9)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(10)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(11)]] RWStructuredBuffer<AmbientCube> FarCubes;
[[vk::binding(12)]] StructuredBuffer<SphericalHarmonics> HDRSHs;

[[vk::binding(13)]] Sampler2D ShadowMapSampler;

[[vk::binding(14)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(15)]] RWTexture2D<float4> OutNormalBuffer;

[[vk::binding(0, 1)]] Sampler2D SamplerStates[];

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    int2 ipos = int2(dispatchThreadId.xy);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    
     int2 size;
     MiniGBuffer.GetDimensions(size.x, size.y);

    uint2 vBuffer = MiniGBuffer[ipos];
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
    OutImage[ipos] = float4(normal,1.0);
}
