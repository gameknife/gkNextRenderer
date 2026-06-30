import Shader.Bindless;
import Shader.PathTracing;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FHiVoxelDDARayTracer tracer;
    FGpuProbeGenerator probeGen;

    uint cascadeIndex = Bindless.GetGpuscene().custom_data_1;
    uint activeProbeIdx = DTid.x + Bindless.GetGpuscene().custom_data_0;
    uint activeBrickIndex = activeProbeIdx / uint(GPU_SCENE_AMBIENT_BRICK_VOLUME);
    uint localProbeOffset = activeProbeIdx % uint(GPU_SCENE_AMBIENT_BRICK_VOLUME);
    int3 probePos;
    uint voxelIdx;
    int cubeIdx;
    if (!DecodeAmbientActiveBrickProbe(cascadeIndex, activeBrickIndex, localProbeOffset, probePos, voxelIdx, cubeIdx))
    {
        return;
    }
    
    UniformBufferObject camera = Bindless.GetGpuscene().Camera[0];
    float cubeUnit = GetAmbientCubeUnitAtCascade(cascadeIndex);
    uint4 RandomSeed = InitRandomSeed(uint(probePos.x + probePos.y), uint(probePos.y + probePos.z), camera.TotalFrames);
    float3 origin = float3(probePos) * cubeUnit + GetAmbientCubeOffsetAtCascade(cascadeIndex);

    probeGen.RenderAmbientCubeOnly(tracer, origin, cubeIdx, voxelIdx, RandomSeed);
}
