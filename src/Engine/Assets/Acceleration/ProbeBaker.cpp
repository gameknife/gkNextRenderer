// FCPUProbeBaker: ambient cube voxelization, Chebyshev (L-infinity) distance field
// and GPU upload for the software GI probe cascades.
// Split from CPUAccelerationStructure.cpp; same namespace, separate TU.
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace Assets::CPU
{

namespace
{
    uint32_t SetPackedNibbleX(uint32_t packedValue, uint8_t value)
    {
        return (packedValue & 0xFFFFFFF0u) | (value & 0xFu);
    }

    struct FChamferNeighbor
    {
        int dx;
        int dy;
        int dz;
        float weight;
    };

    // All 26 neighbours carry weight 1, which makes the two-sweep chamfer produce an exact
    // Chebyshev (L-infinity) distance transform rather than a Euclidean approximation. The
    // shader consumes this field as "every cell within `d` of me in every axis is empty", and
    // only L-infinity supports that reading: with Euclidean weights a diagonal ray can leave
    // the guaranteed-empty region after d/sqrt(3) cells, which is how the skip used to tunnel
    // through thin geometry. See TraceRayDDA in common/RayTracers.slang.
    constexpr std::array<FChamferNeighbor, 13> kForwardChamferNeighbors = {
        FChamferNeighbor{-1, -1, -1, 1.0f},
        FChamferNeighbor{0, -1, -1, 1.0f},
        FChamferNeighbor{1, -1, -1, 1.0f},
        FChamferNeighbor{-1, -1, 0, 1.0f},
        FChamferNeighbor{0, -1, 0, 1.0f},
        FChamferNeighbor{1, -1, 0, 1.0f},
        FChamferNeighbor{-1, -1, 1, 1.0f},
        FChamferNeighbor{0, -1, 1, 1.0f},
        FChamferNeighbor{1, -1, 1, 1.0f},
        FChamferNeighbor{-1, 0, -1, 1.0f},
        FChamferNeighbor{0, 0, -1, 1.0f},
        FChamferNeighbor{1, 0, -1, 1.0f},
        FChamferNeighbor{-1, 0, 0, 1.0f},
    };

    constexpr std::array<FChamferNeighbor, 13> kBackwardChamferNeighbors = {
        FChamferNeighbor{1, 0, 0, 1.0f},
        FChamferNeighbor{-1, 0, 1, 1.0f},
        FChamferNeighbor{0, 0, 1, 1.0f},
        FChamferNeighbor{1, 0, 1, 1.0f},
        FChamferNeighbor{-1, 1, -1, 1.0f},
        FChamferNeighbor{0, 1, -1, 1.0f},
        FChamferNeighbor{1, 1, -1, 1.0f},
        FChamferNeighbor{-1, 1, 0, 1.0f},
        FChamferNeighbor{0, 1, 0, 1.0f},
        FChamferNeighbor{1, 1, 0, 1.0f},
        FChamferNeighbor{-1, 1, 1, 1.0f},
        FChamferNeighbor{0, 1, 1, 1.0f},
        FChamferNeighbor{1, 1, 1, 1.0f},
    };

    void ApplyChamferPass(std::vector<float>& distanceField, const std::array<FChamferNeighbor, 13>& neighbors, bool reverseSweep)
    {
        const int xStart = reverseSweep ? Assets::CUBE_SIZE_XY - 1 : 0;
        const int xEnd = reverseSweep ? -1 : Assets::CUBE_SIZE_XY;
        const int xStep = reverseSweep ? -1 : 1;
        const int yStart = reverseSweep ? Assets::CUBE_SIZE_Z - 1 : 0;
        const int yEnd = reverseSweep ? -1 : Assets::CUBE_SIZE_Z;
        const int yStep = reverseSweep ? -1 : 1;
        const int zStart = reverseSweep ? Assets::CUBE_SIZE_XY - 1 : 0;
        const int zEnd = reverseSweep ? -1 : Assets::CUBE_SIZE_XY;
        const int zStep = reverseSweep ? -1 : 1;

        for (int y = yStart; y != yEnd; y += yStep)
        {
            for (int z = zStart; z != zEnd; z += zStep)
            {
                for (int x = xStart; x != xEnd; x += xStep)
                {
                    const uint32_t voxelIndex = GetVoxelAddress(x, y, z);
                    float bestDistance = distanceField[voxelIndex];

                    for (const FChamferNeighbor& neighbor : neighbors)
                    {
                        const int nx = x + neighbor.dx;
                        const int ny = y + neighbor.dy;
                        const int nz = z + neighbor.dz;
                        if (nx < 0 || nx >= Assets::CUBE_SIZE_XY ||
                            ny < 0 || ny >= Assets::CUBE_SIZE_Z ||
                            nz < 0 || nz >= Assets::CUBE_SIZE_XY)
                        {
                            continue;
                        }

                        const uint32_t neighborIndex = GetVoxelAddress(nx, ny, nz);
                        bestDistance = std::min(bestDistance, distanceField[neighborIndex] + neighbor.weight);
                    }

                    distanceField[voxelIndex] = bestDistance;
                }
            }
        }
    }
}

using namespace Assets;

uint PackNibbles(glm::u32vec4 low, glm::u32vec4 high)
{
    return (low.x & 0xFu) |
           ((low.y & 0xFu) << 4) |
           ((low.z & 0xFu) << 8) |
           ((low.w & 0xFu) << 12) |
           ((high.x & 0xFu) << 16) |
           ((high.y & 0xFu) << 20) |
           ((high.z & 0xFu) << 24) |
           ((high.w & 0xFu) << 28);
}

#define FLOAT2 vec2
#define FLOAT3 vec3
#define FLOAT4 vec4

bool InsideGeometry(const FCPUTLASSnapshot& snapshot, FLOAT3& origin, FLOAT3 rayDir, VoxelData& outCube,
                    float& distance, float cubeUnit)
{
    // Intersection test.
    vec3 outNormal;
    float outRayDist;
    uint tempMaterialId;
    uint tempInstanceId;

    if (TraceRay(snapshot, origin, rayDir, cubeUnit * 64.0f, outNormal, tempMaterialId, outRayDist, tempInstanceId))
    {
        distance = outRayDist;
        if (distance <= cubeUnit)
        {
            outCube.matId = tempMaterialId;

            Material::Enum materialModel = Material::Enum::Lambertian;
            if (snapshot.materialTable && !snapshot.materialTable->entries.empty())
            {
                const size_t materialIndex = tempMaterialId % snapshot.materialTable->entries.size();
                materialModel = snapshot.materialTable->entries[materialIndex].materialModel;
            }

            // A back-face hit indicates solid geometry; push the light probe outside it.
            if (dot(outNormal, rayDir) > 0.0 || materialModel == Material::Enum::DiffuseLight)
            {
                distance = 0;
                return true;
            }
        }
    }
    return false;
}

void VoxelizeCube(const FCPUTLASSnapshot& snapshot, VoxelData& cube, FLOAT3 origin, float cubeUnit)
{
    // just write matid and solid status
    cube.matId = 0;

    float distPY = 255.0f;
    float distNY = 255.0f;
    float distPX = 255.0f;
    float distNX = 255.0f;
    float distPZ = 255.0f;
    float distNZ = 255.0f;

    // Cast six axis-aligned rays and retain their distances for later sampling decisions.
    InsideGeometry(snapshot, origin, FLOAT3(0, 1, 0), cube, distPY, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(0, -1, 0), cube, distNY, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(1, 0, 0), cube, distPX, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(-1, 0, 0), cube, distNX, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(0, 0, 1), cube, distPZ, cubeUnit);
    InsideGeometry(snapshot, origin, FLOAT3(0, 0, -1), cube, distNZ, cubeUnit);

    // Each voxel now has a distance field that can be used for fast skipping.
    distPY = glm::fclamp(distPY / cubeUnit, 0.0f, 1.0f);
    distNY = glm::fclamp(distNY / cubeUnit, 0.0f, 1.0f);
    distPX = glm::fclamp(distPX / cubeUnit, 0.0f, 1.0f);
    distNX = glm::fclamp(distNX / cubeUnit, 0.0f, 1.0f);
    distPZ = glm::fclamp(distPZ / cubeUnit, 0.0f, 1.0f);
    distNZ = glm::fclamp(distNZ / cubeUnit, 0.0f, 1.0f);

    float inside = distPY * distNY * distPX * distNX * distPZ * distNZ;

    // Nibble x is the L-infinity distance field. It is a whole-cascade sweep, so only a
    // placeholder goes in here; RebuildDistanceField() overwrites every voxel once the cascade
    // has finished voxelizing.
    cube.distanceToSolid = PackNibbles(
        glm::u32vec4(kMaxDistanceFieldSeed,
                     static_cast<uint>(inside * 15.0f),
                     static_cast<uint>(distPZ * 15.0f),
                     static_cast<uint>(distNZ * 15.0f)),
        glm::u32vec4(static_cast<uint>(distPX * 15.0f),
                     static_cast<uint>(distNX * 15.0f),
                     static_cast<uint>(distPY * 15.0f),
                     static_cast<uint>(distNY * 15.0f)));
}

#undef float2
#undef float3
#undef float4

void FCPUProbeBaker::Init(uint32_t cascadeIdx, float unitSize, vec3 offset)
{
    cascadeIndex = cascadeIdx;
    UNIT_SIZE = unitSize;
    CUBE_OFFSET = offset;
    voxels.resize(kCascadeVoxelCount);
    distanceToSolidSeeds.resize(kCascadeVoxelCount, kMaxDistanceFieldSeed);
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& voxelGpuMemory, uint32_t elementOffset)
{
    UploadGPU(voxelGpuMemory, 0, elementOffset);
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& voxelGpuMemory, size_t byteBaseOffset, uint32_t elementOffset)
{
    const size_t byteOffset = byteBaseOffset + static_cast<size_t>(elementOffset) * sizeof(VoxelData);
    VoxelData* data = reinterpret_cast<VoxelData*>(voxelGpuMemory.Map(byteOffset, sizeof(VoxelData) * voxels.size()));
    std::memcpy(data, voxels.data(), voxels.size() * sizeof(VoxelData));
    voxelGpuMemory.Unmap();
}

void FCPUProbeBaker::RebuildDistanceField()
{
    if (voxels.empty() || distanceToSolidSeeds.size() != voxels.size())
    {
        return;
    }

    std::vector<float> distanceField(voxels.size(), 0.0f);
    for (size_t voxelIndex = 0; voxelIndex < voxels.size(); ++voxelIndex)
    {
        distanceField[voxelIndex] = static_cast<float>(distanceToSolidSeeds[voxelIndex]);
    }

    constexpr int kChamferPassCount = 2;
    for (int passIndex = 0; passIndex < kChamferPassCount; ++passIndex)
    {
        ApplyChamferPass(distanceField, kForwardChamferNeighbors, false);
        ApplyChamferPass(distanceField, kBackwardChamferNeighbors, true);
    }

    for (size_t voxelIndex = 0; voxelIndex < voxels.size(); ++voxelIndex)
    {
        const float clampedDistance = glm::clamp(distanceField[voxelIndex], 0.0f, 15.0f);
        const uint8_t packedDistance = static_cast<uint8_t>(glm::floor(clampedDistance));
        voxels[voxelIndex].distanceToSolid =
            SetPackedNibbleX(voxels[voxelIndex].distanceToSolid, packedDistance);
    }
}

void FCPUProbeBaker::ProcessCube(int x, int y, int z, ECubeProcType procType,
                                const std::shared_ptr<const FCPUTLASSnapshot>& snapshot)
{
    vec3 probePos = vec3(x, y, z) * UNIT_SIZE + CUBE_OFFSET;
    uint32_t addressIdx = GetVoxelAddress(x, y, z);
    VoxelData& voxel = voxels[addressIdx];
        
    switch (procType)
    {
        case ECubeProcType::ECPT_Clear:
            voxel = {};
            voxel.distanceToSolid = SetPackedNibbleX(voxel.distanceToSolid, kMaxDistanceFieldSeed);
            distanceToSolidSeeds[addressIdx] = kMaxDistanceFieldSeed;
            break;
        case ECubeProcType::ECPT_Fence:
            break;
        case ECubeProcType::ECPT_Voxelize:
            if (snapshot)
            {
                VoxelizeCube(*snapshot, voxel, probePos, UNIT_SIZE);
            }
            // Seed the transform from occupancy alone. The DDA only ever reports a hit on a
            // voxel with matId != 0, so the distance to the nearest such voxel is exactly the
            // clearance a ray is allowed to skip -- an axis-ray distance seed could only make
            // the field smaller than that and cost traversal steps for nothing.
            distanceToSolidSeeds[addressIdx] = voxel.matId > 0 ? uint8_t{0} : kMaxDistanceFieldSeed;
            break;
    }
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& voxelGpuMemory)
{
    UploadGPU(voxelGpuMemory, 0);
}

void FCPUProbeBaker::ClearAmbientCubes()
{
    for (size_t voxelIndex = 0; voxelIndex < voxels.size(); ++voxelIndex)
    {
        VoxelData& voxel = voxels[voxelIndex];
        voxel = {};
        voxel.distanceToSolid = SetPackedNibbleX(voxel.distanceToSolid, kMaxDistanceFieldSeed);
        distanceToSolidSeeds[voxelIndex] = kMaxDistanceFieldSeed;
    }
}
}
