import Common;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    uint totalCount = Bindless.GetGpuscene().custom_data_0;
    if (idx >= totalCount)
    {
        return;
    }

    AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

    Cubes[idx].PosZ = 0;
    Cubes[idx].NegZ = 0;
    Cubes[idx].PosY = 0;
    Cubes[idx].NegY = 0;
    Cubes[idx].PosX = 0;
    Cubes[idx].NegX = 0;
    Cubes[idx].PosZ_D = 0;
    Cubes[idx].NegZ_D = 0;
    Cubes[idx].PosY_D = 0;
    Cubes[idx].NegY_D = 0;
    Cubes[idx].PosX_D = 0;
    Cubes[idx].NegX_D = 0;
    Cubes[idx].skyVisibility_pznzpyny = 0;
    Cubes[idx].skyVisibility_pxnxs0s1 = 0;
    Voxels[idx].age = 0;
}
