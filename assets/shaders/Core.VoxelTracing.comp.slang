import Shader.Bindless;
import Shader.PathTracing;
#include "common/ShaderClock.slang"

void OutputColor(int2 ipos, float3 Total)
{
    if (Bindless.GetGpuscene().Camera->HDR)
    {
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_DENOISED)[ipos] = float4(EncodeHdrOutput(Total, Bindless.GetGpuscene().Camera->PaperWhiteNit, Bindless.GetGpuscene().Camera->HDROutputMode), 0.0);
    }
    else
    {
        Bindless.GetViewStorageTexture<float4>(Bindless.RT_DENOISED)[ipos] = float4(Uncharted2_Tonemapping(Total * Bindless.GetGpuscene().Camera->PaperWhiteNit / 20000.0), 0.0);
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    START_SHADERCLOCK()

    let Camera = Bindless.GetGpuscene().Camera[0];

    // compose with renderer and tracer
    FHiVoxelDDARayTracer voxelTracer;
    FVoxelRayCaster rayCasterVoxel;

    int2 ipos = int2(DTid.xy);
    int2 size = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_DENOISED);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    // 第一次Tracing
    Vertex hitVertex;
    NodeProxy hitNode;

    float4 outColor = float4(0, 0, 0, 1);
    float3 rayDir;
    float rayLength = 0.0;
    float2 pixelOffset = float2(0, 0);
    if (rayCasterVoxel.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode, rayDir, rayLength, pixelOffset))
    {
        float4 residencyDebugColor;
        if (TryAmbientResidencyDebug(hitVertex.Position, residencyDebugColor))
        {
            Bindless.GetViewStorageTexture<float4>(Bindless.RT_DENOISED)[ipos] = residencyDebugColor;
            END_SHADERCLOCK(ipos)
            return;
        }

        float4 radiance = interpolateAmbientCubes<FullAmbientCubeSampler>(hitVertex.Position, hitVertex.Normal);
        outColor = Bindless.GetGpuscene().GetMaterial(hitVertex.MaterialIndex).Diffuse * radiance;
        if (Camera.DebugDraw_Lighting)
        {
            outColor = radiance;
        }

        //outColor= float4(50,50,50,1);
        //Bindless.GetViewStorageTexture<float4>(Bindless.RT_NORMAL).Store(ipos, float4(hitVertex.Normal, 0));
    }
    else
    {
        outColor= float4(10,10,10,0);
    }

    OutputColor(ipos, outColor.rgb);

    END_SHADERCLOCK(ipos)
}
