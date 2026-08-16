import Shader.Bindless;
import Shader.PathTracing;
#include "common/ShaderClock.slang"

void OutputColor(int2 ipos, float3 total)
{
    Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[ipos] =
        float4(total, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    const uint2 outputSize = Bindless.GetViewStorageTextureDimensions<float4>(Bindless.RT_SCENE_COLOR);
    const uint2 shadingPixel = DTid.xy;
    if (any(shadingPixel >= outputSize))
    {
        return;
    }
    START_SHADERCLOCK()

    let Camera = Bindless.GetGpuscene().Camera[0];

    // compose with renderer and tracer
    FHiVoxelDDARayTracer voxelTracer;
    FVoxelRayCaster rayCasterVoxel;

    int2 ipos = int2(shadingPixel);
    int2 size = int2(outputSize);
    uint4 RandomSeed = InitRandomSeed(ipos.x, ipos.y, Camera.TotalFrames);

    // 第一次Tracing
    Vertex hitVertex;
    NodeProxy hitNode;

    float4 outColor = float4(0, 0, 0, 1);
    bool noSkyBackground = false;
    float3 rayDir;
    float rayLength = 0.0;
    float2 pixelOffset = float2(0, 0);
    FPrimarySurfaceSample surfaceSample = FPrimarySurfaceSample();
    if (rayCasterVoxel.TracePrimaryRay(ipos, size, RandomSeed, hitVertex, hitNode, rayDir, rayLength, pixelOffset, surfaceSample))
    {
        float4 residencyDebugColor;
        if (TryAmbientResidencyDebug(hitVertex.Position, residencyDebugColor))
        {
            Bindless.GetViewStorageTexture<float4>(Bindless.RT_SCENE_COLOR)[ipos] = residencyDebugColor;
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
        noSkyBackground = !Camera.HasSky;
    }

    const float exposure = max(Camera.PaperWhiteNit / 40000.0, 1.0e-6);
    const float3 sceneColor = noSkyBackground
        ? NoSkyClearSceneColor(Camera.HDR, Camera.PaperWhiteNit)
        : max(outColor.rgb, 0.0) * exposure;
    OutputColor(ipos, sceneColor);

    END_SHADERCLOCK(ipos)
}
