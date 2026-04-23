import Common;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FGpuProbeGenerator probeGen;

    uint cascadeIndex = Bindless.GetGpuscene().custom_data_1;
    uint cascadeBase = GetCascadeBaseOffset(cascadeIndex);
    uint gIdx = DTid.x + Bindless.GetGpuscene().custom_data_0;
    uint localIdx = gIdx - cascadeBase;

    uint y = localIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
    uint z = (localIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
    uint x = localIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

    probeGen.InjectAmbientCubePropagation(gIdx, cascadeIndex, int3(int(x), int(y), int(z)));
}
