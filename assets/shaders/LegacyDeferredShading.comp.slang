import Common;
#include "common/ShaderClock.slang"

[[vk::binding(0, 0)]] Sampler2D TextureArray[];

[[vk::binding(0, 1)]] ConstantBuffer<UniformBufferObject> Camera;
[[vk::binding(1, 1)]] Sampler2D ShadowMapSampler;

[[vk::binding(0, 2)]] RWTexture2D<float4> FinalImage;
[[vk::binding(1, 2)]] RWTexture2D<float4> OutImage;
[[vk::binding(2, 2)]] RWTexture2D<uint2> MiniGBuffer;
[[vk::binding(3, 2)]] RWTexture2D<uint> ObjectId0;
[[vk::binding(4, 2)]] RWTexture2D<uint> ObjectId1;
[[vk::binding(5, 2)]] RWTexture2D<float4> OutMotionVector;
[[vk::binding(6, 2)]] RWTexture2D<float4> OutAlbedoBuffer;
[[vk::binding(7, 2)]] RWTexture2D<float4> OutNormalBuffer;
[[vk::binding(8, 2)]] RWTexture2D<float4> ShaderTimerBuffer;
[[vk::binding(9, 2)]] RWTexture2D<float4> OutDenoisedBuffer;
[[vk::binding(10, 2)]] RWTexture2D<float> OutDepthBuffer;

[[vk::binding(0, 3)]] StructuredBuffer<float> Vertices;
[[vk::binding(1, 3)]] StructuredBuffer<uint> Indices;
[[vk::binding(2, 3)]] StructuredBuffer<Material> Materials;
[[vk::binding(3, 3)]] StructuredBuffer<ModelData> Offsets;
[[vk::binding(4, 3)]] StructuredBuffer<NodeProxy> NodeProxies;
[[vk::binding(5, 3)]] RWStructuredBuffer<AmbientCube> Cubes;
[[vk::binding(6, 3)]] RWStructuredBuffer<VoxelData> Voxels;
[[vk::binding(7, 3)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(8, 3)]] StructuredBuffer<LightObject> Lights;
[[vk::binding(9, 3)]] StructuredBuffer<PageIndex> Pages;

void OutputColor(int2 ipos, float3 Total)
{
    if (Camera.HDR)
    {
        OutDenoisedBuffer[ipos] = float4(LinearToST2084UE(Total * Camera.PaperWhiteNit / 230.0), 1.0);
    }
    else
    {
        OutDenoisedBuffer[ipos] = float4(Uncharted2_Tonemapping(Total * Camera.PaperWhiteNit / 20000.0), 1.0);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid: SV_DispatchThreadID) {
    START_SHADERCLOCK()
    // compose with renderer and tracer
    FVisibilityBufferRayCaster rayCaster = { Camera, Vertices, NodeProxies, MiniGBuffer };
 
    int2 ipos = int2(DTid.xy);
    int2 size;
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    MiniGBuffer.GetDimensions(size.x, size.y);

    // 第一次Tracing
    Vertex hitVertex;
    NodeProxy hitNode;
    float3 rayDir;
    if (!rayCaster.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode, rayDir))
    {
        float4 skyColor = Camera.HasSky ? Common.SampleIBL(Camera.SkyIdx, rayDir, Camera.SkyRotation, 0, HDRSHs, TextureArray) * Camera.SkyIntensity : float4(0, 0, 0, 0);
        OutputColor(ipos, skyColor.rgb);

        END_SHADERCLOCK(ipos)
        return;
    }

    Material hitMaterial = Materials[hitVertex.MaterialIndex];

    float4 albedo = float4(0, 0, 0, 1);
    float4 illuminaceColor = float4(0, 0, 0, 1);
    float4 gbuffer = float4(0, 0, 0, 0);
    Common.FetchGBuffer(hitVertex, Materials[hitVertex.MaterialIndex], TextureArray, albedo, gbuffer);

    float4 radiance = interpolateAmbientCubes<FullAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, Voxels);

    float3 tracenext = reflect(rayDir, hitVertex.Normal);
    float4 skyColor = Common.SampleIBL(Camera.SkyIdx, tracenext, Camera.SkyRotation, gbuffer.a, HDRSHs, TextureArray);

    OutputColor(ipos, skyColor.rgb * radiance.rgb);
    ObjectId0[ipos] = hitNode.instanceId;

    if (Camera.DebugDraw_Lighting)
    {
        OutputColor(ipos, radiance.rgb);
    }

    // write to NDC depth
    float4 clipPos = mul(Camera.ViewProjection, float4(hitVertex.Position, 1.0));
    float ndcDepth = clipPos.z / clipPos.w;
    OutDepthBuffer[ipos] = ndcDepth;

    END_SHADERCLOCK(ipos)
}