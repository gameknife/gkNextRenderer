import Common;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
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

    probeGen.InjectAmbientCubePropagation(voxelIdx, cascadeIndex, probePos);
}
