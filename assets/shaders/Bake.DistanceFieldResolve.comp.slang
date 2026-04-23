import Common;

static const uint kInvalidSeed = 0xffffffffu;

uint4 UnpackBytesLocal(uint packed)
{
    return uint4(
        (packed & 0xFF),
        ((packed >> 8) & 0xFF),
        ((packed >> 16) & 0xFF),
        ((packed >> 24) & 0xFF)
    );
}

uint PackBytesLocal(uint4 values)
{
    return (values.x & 0xFF) |
           ((values.y & 0xFF) << 8) |
           ((values.z & 0xFF) << 16) |
           ((values.w & 0xFF) << 24);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint cascadeBase = Bindless.GetGpuscene().custom_data_0;
    uint localIdx = DTid.x;
    uint totalCount = CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z;
    if (localIdx >= totalCount)
    {
        return;
    }

    uint4* SeedA = (uint4*)Bindless.GetGpuscene().CubesPong;
    uint4* SeedB = (uint4*)Bindless.GetGpuscene().SkinnedVerticesSimple;
    uint lastPassParity = Bindless.GetGpuscene().custom_data_1;
    uint4 seed = (lastPassParity & 1u) == 0u ? SeedB[localIdx] : SeedA[localIdx];

    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    VoxelData voxel = Voxels[cascadeBase + localIdx];
    uint4 unpacked = UnpackBytesLocal(voxel.distanceToSolid_gg_z01);

    if (seed.w == kInvalidSeed)
    {
        unpacked.x = 255u;
        Voxels[cascadeBase + localIdx].distanceToSolid_gg_z01 = PackBytesLocal(unpacked);
        return;
    }

    int y = int(localIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY));
    int z = int((localIdx - uint(y) * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY);
    int x = int(localIdx - uint(y) * CUBE_SIZE_XY * CUBE_SIZE_XY - uint(z) * CUBE_SIZE_XY);

    float3 delta = float3(int(seed.x) - x, int(seed.y) - y, int(seed.z) - z);
    float distance = float(seed.w) + length(delta);
    unpacked.x = uint(clamp(floor(distance), 0.0f, 255.0f));
    Voxels[cascadeBase + localIdx].distanceToSolid_gg_z01 = PackBytesLocal(unpacked);
}
