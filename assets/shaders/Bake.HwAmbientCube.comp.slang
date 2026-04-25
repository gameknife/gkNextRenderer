import Common;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FHardwareRayTracer tracer;
    FGpuProbeGenerator probeGen;

    uint cascadeIndex = Bindless.GetGpuscene().custom_data_1;
    uint cascadeBase = GetCascadeBaseOffset(cascadeIndex);
    uint gIdx = DTid.x + Bindless.GetGpuscene().custom_data_0;
    uint localIdx = gIdx - cascadeBase;
    
    uint y = localIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
    uint z = (localIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
    uint x = localIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    uint4 RandomSeed = InitRandomSeed(x + y, y + z, camera.TotalFrames);
    float3 origin = float3(x, y, z) * cubeUnit + GetAmbientCubeOffsetAtCascade(cascadeIndex);

    probeGen.RenderAmbientCubeOnly(tracer, origin, gIdx, RandomSeed);
}
