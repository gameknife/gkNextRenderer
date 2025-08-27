import Common;
#include "common/ShaderClock.slang"

void OutputColor(int2 ipos, float3 Total)
{
    if (Bindless.GetGpuscene().Camera->HDR)
    {
        Bindless.GetStorageTexture<float4>(Bindless.RT_DENOISED)[ipos] = float4(LinearToST2084UE(Total * Bindless.GetGpuscene().Camera->PaperWhiteNit / 230.0), 0.0);
    }
    else
    {
        Bindless.GetStorageTexture<float4>(Bindless.RT_DENOISED)[ipos] = float4(Uncharted2_Tonemapping(Total * Bindless.GetGpuscene().Camera->PaperWhiteNit / 20000.0), 0.0);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // START_SHADERCLOCK()
    // // compose with renderer and tracer
    // FHiVoxelDDARayTracer voxelTracer = { Voxels, Pages };
    // FVoxelRayCaster rayCasterVoxel = { Camera, voxelTracer };

    // int2 ipos = int2(DTid.xy);
    // int2 size;
    // uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);
    // OutDenoisedBuffer.GetDimensions(size.x, size.y);

    // // 第一次Tracing
    // Vertex hitVertex;
    // NodeProxy hitNode;

    // float4 outColor = float4(0, 0, 0, 1);
    // float3 rayDir;
    // float rayLength = 0.0;
    // float2 pixelOffset = float2(0, 0);
    // if (rayCasterVoxel.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode, rayDir, rayLength, pixelOffset))
    // {
    //     // float4 radiance = interpolateAmbientCubes<FullAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal, Cubes, Voxels);
    //     // outColor = Materials[hitVertex.MaterialIndex].Diffuse * radiance;
    //     // //outColor = Materials[hitVertex.MaterialIndex].Diffuse * 50.0f;
    //     // if (Camera.DebugDraw_Lighting)
    //     // {
    //     //     outColor = radiance;
    //     // }

    //     outColor= float4(50,50,50,1);
    // }
    // else
    // {
    //     outColor= float4(0,0,0,0);
    // }

    // OutputColor(ipos, outColor.rgb);

    // END_SHADERCLOCK(ipos)
}