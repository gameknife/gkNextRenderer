import Common;

static const uint kInvalidSeed = 0xffffffffu;

float SeedDistance(uint4 seed, int3 targetPos)
{
    if (seed.w == kInvalidSeed)
    {
        return 1e20f;
    }

    float3 delta = float3(int(seed.x), int(seed.y), int(seed.z)) - float3(targetPos);
    return float(seed.w) + length(delta);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint localIdx = DTid.x;
    uint totalCount = CUBE_SIZE_XY * CUBE_SIZE_XY * CUBE_SIZE_Z;
    if (localIdx >= totalCount)
    {
        return;
    }

    uint step = Bindless.GetGpuscene().custom_data_2;
    uint passParity = Bindless.GetGpuscene().custom_data_1;

    uint4* SeedA = Bindless.GetGpuscene().AmbientSdfSeedA;
    uint4* SeedB = Bindless.GetGpuscene().AmbientSdfScratch;
    uint4* SourceSeeds = (passParity & 1u) == 0u ? SeedA : SeedB;
    uint4* DestSeeds = (passParity & 1u) == 0u ? SeedB : SeedA;

    int y = int(localIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY));
    int z = int((localIdx - uint(y) * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY);
    int x = int(localIdx - uint(y) * CUBE_SIZE_XY * CUBE_SIZE_XY - uint(z) * CUBE_SIZE_XY);
    int3 pos = int3(x, y, z);

    uint4 bestSeed = SourceSeeds[localIdx];
    float bestDistance = SeedDistance(bestSeed, pos);

    for (int oy = -1; oy <= 1; ++oy)
    {
        for (int oz = -1; oz <= 1; ++oz)
        {
            for (int ox = -1; ox <= 1; ++ox)
            {
                int3 neighborPos = pos + int3(ox, oy, oz) * int(step);
                if (neighborPos.x < 0 || neighborPos.x >= CUBE_SIZE_XY ||
                    neighborPos.y < 0 || neighborPos.y >= CUBE_SIZE_Z ||
                    neighborPos.z < 0 || neighborPos.z >= CUBE_SIZE_XY)
                {
                    continue;
                }

                uint neighborIdx =
                    uint(neighborPos.y) * CUBE_SIZE_XY * CUBE_SIZE_XY +
                    uint(neighborPos.z) * CUBE_SIZE_XY +
                    uint(neighborPos.x);

                uint4 candidateSeed = SourceSeeds[neighborIdx];
                float candidateDistance = SeedDistance(candidateSeed, pos);
                if (candidateDistance < bestDistance)
                {
                    bestDistance = candidateDistance;
                    bestSeed = candidateSeed;
                }
            }
        }
    }

    DestSeeds[localIdx] = bestSeed;
}
