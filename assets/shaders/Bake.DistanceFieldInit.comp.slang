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

    VoxelData* Voxels = Bindless.GetGpuscene().Voxels;
    uint4* SeedBuffer = (uint4*)Bindless.GetGpuscene().CubesPong;

    uint y = localIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
    uint z = (localIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
    uint x = localIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

    VoxelData voxel = Voxels[cascadeBase + localIdx];
    uint4 unpacked = UnpackBytesLocal(voxel.distanceToSolid_gg_z01);
    uint seedDistance = unpacked.x;

    if (seedDistance >= 255u)
    {
        SeedBuffer[localIdx] = uint4(kInvalidSeed, kInvalidSeed, kInvalidSeed, kInvalidSeed);
        return;
    }

    SeedBuffer[localIdx] = uint4(x, y, z, seedDistance);
}
