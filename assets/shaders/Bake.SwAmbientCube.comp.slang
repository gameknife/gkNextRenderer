import Common;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FHiVoxelDDARayTracerV2 tracer;
    FGpuProbeGenerator probeGen;

    uint gIdx = DTid.x + Bindless.GetGpuscene().custom_data_0;
    
    uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
    uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
    uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;
    
    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    float cubeUnit = max(camera.AmbientCubeUnit, 0.0001f);
    uint4 RandomSeed = InitRandomSeed(x + y, y + z, camera.TotalFrames);
    float3 origin = float3(x, y, z) * cubeUnit + camera.AmbientCubeOffset;

    probeGen.RenderAmbientCubeOnly(tracer, origin, gIdx, RandomSeed);
}
