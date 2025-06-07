import Common;

[[vk::binding(0)]] RWTexture2D<float4> OutImage;
[[vk::binding(1)]] ConstantBuffer<UniformBufferObject> Camera;

[[vk::binding(2)]] StructuredBuffer<Material> Materials;
[[vk::binding(3)]] StructuredBuffer<SphericalHarmonics> HDRSHs;
[[vk::binding(4)]] StructuredBuffer<AmbientCube> Cubes;
[[vk::binding(5)]] StructuredBuffer<VoxelData> Voxels;

[[vk::binding(0, 1)]] Sampler2D TextureArray[];

[shader("compute")]
[numthreads(8, 4, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // compose with renderer and tracer
    FVoxelDDARayTracer voxelTracer = { Cubes, Voxels };
    FVoxelRayCaster rayCasterVoxel = { Camera, voxelTracer };

    int2 ipos = int2(DTid.xy);
    int2 size;
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    OutImage.GetDimensions(size.x, size.y);

    // 第一次Tracing
    Vertex hitVertex;
    NodeProxy hitNode;

    float4 outColor = float4(0, 0, 0, 1);
    float3 rayDir;
    if (rayCasterVoxel.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode, rayDir))
    {
        float4 radiance = interpolateAmbientCubes<FullAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, Voxels);
        outColor = Materials[hitVertex.MaterialIndex].Diffuse * radiance;
        //outColor = Materials[hitVertex.MaterialIndex].Diffuse * 50.0f;
    }
    else
    {
        outColor= float4(10,10,10,0);
    }

    if (Camera.HDR)
    {
        OutImage[ipos] = float4(LinearToST2084UE(outColor.rgb * Camera.PaperWhiteNit / 230.0), 1.0);
    }
    else
    {
        OutImage[ipos] = float4(Uncharted2_Tonemapping(outColor.rgb * Camera.PaperWhiteNit / 20000.0), 1.0);
    }
}