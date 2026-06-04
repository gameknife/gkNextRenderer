import Common;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint idx = DTid.x;
    // custom_data_0 = cube-pool element count, custom_data_1 = dense voxel count. The sparse cube pool
    // (Phase 3b) is smaller than the dense voxel array, so the two are bounded independently.
    uint cubeTotal = Bindless.GetGpuscene().custom_data_0;
    uint voxelTotal = Bindless.GetGpuscene().custom_data_1;

    AmbientCube* Cubes = Bindless.GetGpuscene().Cubes;
    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;

    if (idx < cubeTotal)
    {
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
    }

    if (idx < voxelTotal)
    {
        Voxels[idx].age = 0;
    }
}
