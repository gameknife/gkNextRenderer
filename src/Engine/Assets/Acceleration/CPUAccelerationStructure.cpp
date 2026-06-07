#include "Engine/Assets/Acceleration/CPUAccelerationStructure.h"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <xxhash.h>

#define TINYBVH_IMPLEMENTATION
#include "ThirdParty/tinybvh/tiny_bvh.h"

namespace Assets::CPU
{

static tinybvh::BVH GCpuBvh;
static std::vector<tinybvh::BLASInstance>* GbvhInstanceList;
static std::vector<FCPUTLASInstanceInfo>* GbvhTlasContexts;
static std::vector<FCPUBLASContext>* GbvhBlasContexts;

Assets::SphericalHarmonics HdrsHs[100];

namespace
{
    constexpr uint32_t kCascadeVoxelCount = Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
    // Dilate the solid-brick set by this many bricks when classifying active cube bricks, so the
    // near-surface shell the bake writes (distanceToSolid < ~16 voxels = 2 bricks) is fully covered.
    constexpr int kAmbientBrickDilationRadius = 3;
    constexpr uint8_t kMaxDistanceFieldSeed = 255;
    std::atomic_bool GLoggedInvalidCpuAsHit{false};
    std::atomic_bool GLoggedInvalidCpuAsMaterial{false};

    struct FWorldBounds
    {
        glm::vec3 min{FLT_MAX, FLT_MAX, FLT_MAX};
        glm::vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    };

    FWorldBounds ComputeWorldBounds(const Assets::Model& model, const glm::mat4& worldTransform)
    {
        const glm::vec3 localMin = model.GetLocalAABBMin();
        const glm::vec3 localMax = model.GetLocalAABBMax();

        glm::vec3 corners[8];
        corners[0] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, localMin.z, 1.0f));
        corners[1] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, localMin.z, 1.0f));
        corners[2] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, localMin.z, 1.0f));
        corners[3] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, localMin.z, 1.0f));
        corners[4] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, localMax.z, 1.0f));
        corners[5] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, localMax.z, 1.0f));
        corners[6] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, localMax.z, 1.0f));
        corners[7] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, localMax.z, 1.0f));

        FWorldBounds bounds;
        bounds.min = corners[0];
        bounds.max = corners[0];
        for (int i = 1; i < 8; ++i)
        {
            bounds.min = glm::min(bounds.min, corners[i]);
            bounds.max = glm::max(bounds.max, corners[i]);
        }
        return bounds;
    }

    bool BoundsNearlyEqual(const FWorldBounds& lhs, const FWorldBounds& rhs, float epsilon = 0.01f)
    {
        return glm::all(glm::lessThanEqual(glm::abs(lhs.min - rhs.min), glm::vec3(epsilon))) &&
               glm::all(glm::lessThanEqual(glm::abs(lhs.max - rhs.max), glm::vec3(epsilon)));
    }

    void AccumulateBounds(bool& hasBounds, glm::vec3& minBounds, glm::vec3& maxBounds, const FWorldBounds& bounds)
    {
        if (!hasBounds)
        {
            minBounds = bounds.min;
            maxBounds = bounds.max;
            hasBounds = true;
            return;
        }

        minBounds = glm::min(minBounds, bounds.min);
        maxBounds = glm::max(maxBounds, bounds.max);
    }

    uint8_t GetPackedByteX(uint32_t packedValue)
    {
        return static_cast<uint8_t>(packedValue & 0xFFu);
    }

    uint32_t SetPackedByteX(uint32_t packedValue, uint8_t value)
    {
        return (packedValue & 0xFFFFFF00u) | value;
    }

    uint32_t GetVoxelAddress(int x, int y, int z)
    {
        return static_cast<uint32_t>(y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY + z * Assets::CUBE_SIZE_XY + x);
    }

    struct FChamferNeighbor
    {
        int dx;
        int dy;
        int dz;
        float weight;
    };

    constexpr std::array<FChamferNeighbor, 13> kForwardChamferNeighbors = {
        FChamferNeighbor{-1, -1, -1, 1.73205081f},
        FChamferNeighbor{0, -1, -1, 1.41421356f},
        FChamferNeighbor{1, -1, -1, 1.73205081f},
        FChamferNeighbor{-1, -1, 0, 1.41421356f},
        FChamferNeighbor{0, -1, 0, 1.0f},
        FChamferNeighbor{1, -1, 0, 1.41421356f},
        FChamferNeighbor{-1, -1, 1, 1.73205081f},
        FChamferNeighbor{0, -1, 1, 1.41421356f},
        FChamferNeighbor{1, -1, 1, 1.73205081f},
        FChamferNeighbor{-1, 0, -1, 1.41421356f},
        FChamferNeighbor{0, 0, -1, 1.0f},
        FChamferNeighbor{1, 0, -1, 1.41421356f},
        FChamferNeighbor{-1, 0, 0, 1.0f},
    };

    constexpr std::array<FChamferNeighbor, 13> kBackwardChamferNeighbors = {
        FChamferNeighbor{1, 0, 0, 1.0f},
        FChamferNeighbor{-1, 0, 1, 1.41421356f},
        FChamferNeighbor{0, 0, 1, 1.0f},
        FChamferNeighbor{1, 0, 1, 1.41421356f},
        FChamferNeighbor{-1, 1, -1, 1.73205081f},
        FChamferNeighbor{0, 1, -1, 1.41421356f},
        FChamferNeighbor{1, 1, -1, 1.73205081f},
        FChamferNeighbor{-1, 1, 0, 1.41421356f},
        FChamferNeighbor{0, 1, 0, 1.0f},
        FChamferNeighbor{1, 1, 0, 1.41421356f},
        FChamferNeighbor{-1, 1, 1, 1.73205081f},
        FChamferNeighbor{0, 1, 1, 1.41421356f},
        FChamferNeighbor{1, 1, 1, 1.73205081f},
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

uint PackBytes(glm::u32vec4 values)
{
    return (values.x & 0xFF) |
           ((values.y & 0xFF) << 8) |
           ((values.z & 0xFF) << 16) |
           ((values.w & 0xFF) << 24);
}

FMaterial& FetchMaterial(uint matId)
{
    auto& materials = NextEngine::GetInstance()->GetScene().Materials();
    assert(matId < materials.size());
    matId = matId % materials.size(); // wrap around
    return materials[matId];
}

uint FetchMaterialId(uint materialIdx, uint instanceId)
{
    if (GbvhTlasContexts == nullptr || instanceId >= GbvhTlasContexts->size())
    {
        if (!GLoggedInvalidCpuAsMaterial.exchange(true))
        {
            SPDLOG_WARN("CPUAccelerationStructure: invalid TLAS instance {} while resolving material id.", instanceId);
        }
        return 0;
    }

    if (materialIdx >= (*GbvhTlasContexts)[instanceId].matIdxs.size())
    {
        if (!GLoggedInvalidCpuAsMaterial.exchange(true))
        {
            SPDLOG_WARN(
                "CPUAccelerationStructure: section/material index {} exceeds supported material slots ({}); falling back to material 0.",
                materialIdx, (*GbvhTlasContexts)[instanceId].matIdxs.size());
        }
        return 0;
    }

    return (*GbvhTlasContexts)[instanceId].matIdxs[materialIdx];
}

bool TraceRay(vec3 origin, vec3 rayDir, float dist, vec3& outNormal, uint& outMaterialId, float& outRayDist, uint& outInstanceId )
{
    tinybvh::Ray ray(tinybvh::bvhvec3(origin.x, origin.y, origin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), dist);
    GCpuBvh.Intersect(ray);

    if (ray.hit.t < dist)
    {
        uint32_t primIdx = ray.hit.prim;
        if (GbvhInstanceList == nullptr || GbvhTlasContexts == nullptr || GbvhBlasContexts == nullptr ||
            ray.hit.inst >= GbvhInstanceList->size() || ray.hit.inst >= GbvhTlasContexts->size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid hit instance {} during CPU ray trace.", ray.hit.inst);
            }
            return false;
        }

        tinybvh::BLASInstance& instance = (*GbvhInstanceList)[ray.hit.inst];
        FCPUTLASInstanceInfo& instContext = (*GbvhTlasContexts)[ray.hit.inst];
        if (instance.blasIdx >= GbvhBlasContexts->size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid BLAS index {} during CPU ray trace.", instance.blasIdx);
            }
            return false;
        }

        FCPUBLASContext& context = (*GbvhBlasContexts)[instance.blasIdx];
        if (primIdx >= context.extinfos.size())
        {
            if (!GLoggedInvalidCpuAsHit.exchange(true))
            {
                SPDLOG_WARN("CPUAccelerationStructure: invalid primitive index {} during CPU ray trace.", primIdx);
            }
            return false;
        }

        mat4* worldTS = (mat4*)instance.transform;
        vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;

        outRayDist = ray.hit.t;
        outNormal = vec3(normalWS.x, normalWS.y, normalWS.z);
        outMaterialId =  FetchMaterialId( context.extinfos[primIdx].matIdx, ray.hit.inst );
        outInstanceId = instContext.nodeId;
        return true;
    }
    
    return false;
}

#define FLOAT2 vec2
#define FLOAT3 vec3
#define FLOAT4 vec4

float DetectDistance(FLOAT3 origin, FLOAT3 rayDir, float cubeUnit)
{
    vec3 outNormal;
    float outRayDist;
    uint tempMaterialId;
    uint tempInstanceId;
    if (TraceRay(origin, rayDir, cubeUnit * 64.0f, outNormal, tempMaterialId, outRayDist, tempInstanceId))
    {
        return outRayDist;
    }
    return 255;
}

bool InsideGeometry(FLOAT3& origin, FLOAT3 rayDir, VoxelData& outCube, float& distance, float cubeUnit)
{
    // 求交测试
    vec3 outNormal;
    float outRayDist;
    uint tempMaterialId;
    uint tempInstanceId;

    if (TraceRay(origin, rayDir, cubeUnit * 64.0f, outNormal, tempMaterialId, outRayDist, tempInstanceId))
    {
        distance = outRayDist;
        if (distance <= cubeUnit)
        {
            FMaterial hitMaterial = FetchMaterial(tempMaterialId);
            outCube.matId = tempMaterialId;

            // 命中反面，识别为固体，并将lightprobe推出体外
            if (dot(outNormal, rayDir) > 0.0 || ((hitMaterial.gpuMaterial_.MaterialModel == Material::Enum::DiffuseLight)))// && OutRayDist < 0.02f))
            {
                distance = 0;
                return true;
            }
        }
    }
    return false;
}

void VoxelizeCube(VoxelData& cube, FLOAT3 origin, float cubeUnit)
{
    // just write matid and solid status
    cube.age = 0;
    cube.matId = 0;

    float distPY = 255.0f;
    float distNY = 255.0f;
    float distPX = 255.0f;
    float distNX = 255.0f;
    float distPZ = 255.0f;
    float distNZ = 255.0f;

    // 现在是向轴向上发射了6根光线，记录下距离，并用于后续采样判断
    InsideGeometry(origin, FLOAT3(0, 1, 0), cube, distPY, cubeUnit);
    InsideGeometry(origin, FLOAT3(0, -1, 0), cube, distNY, cubeUnit);
    InsideGeometry(origin, FLOAT3(1, 0, 0), cube, distPX, cubeUnit);
    InsideGeometry(origin, FLOAT3(-1, 0, 0), cube, distNX, cubeUnit);
    InsideGeometry(origin, FLOAT3(0, 0, 1), cube, distPZ, cubeUnit);
    InsideGeometry(origin, FLOAT3(0, 0, -1), cube, distNZ, cubeUnit);

    // get the min dist of each direction
    float minDist = std::min({distPY, distNY, distPX, distNX, distPZ, distNZ});
    if (minDist > 254.0f)
    {
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(1, 1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, -1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, 1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(1, 1, -1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, -1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, -1, -1), cubeUnit)});
        minDist = std::min({minDist, DetectDistance(origin, FLOAT3(-1, 1, -1), cubeUnit)});
    }

    // 现在，相当于每一个体素，都有了一个距离场，通过判断这个，可以快速跳过？
    distPY = glm::fclamp(distPY / cubeUnit, 0.0f, 1.0f);
    distNY = glm::fclamp(distNY / cubeUnit, 0.0f, 1.0f);
    distPX = glm::fclamp(distPX / cubeUnit, 0.0f, 1.0f);
    distNX = glm::fclamp(distNX / cubeUnit, 0.0f, 1.0f);
    distPZ = glm::fclamp(distPZ / cubeUnit, 0.0f, 1.0f);
    distNZ = glm::fclamp(distNZ / cubeUnit, 0.0f, 1.0f);

    float inside = distPY * distNY * distPX * distNX * distPZ * distNZ;

    cube.distanceToSolid_gg_z01 =
        PackBytes(glm::u32vec4(minDist / cubeUnit, uint(inside * 255.0f), uint(distPZ * 255.0f), uint(distNZ * 255.0f)));
    cube.distanceToSolid_x01_y01 = PackBytes(glm::u32vec4(uint(distPX * 255.0f), uint(distNX * 255.0f), uint(distPY * 255.0f), uint(distNY * 255.0f)));
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
        const float clampedDistance = glm::clamp(distanceField[voxelIndex], 0.0f, 255.0f);
        const uint8_t packedDistance = static_cast<uint8_t>(glm::floor(clampedDistance));
        voxels[voxelIndex].distanceToSolid_gg_z01 =
            SetPackedByteX(voxels[voxelIndex].distanceToSolid_gg_z01, packedDistance);
    }
}

bool FCPUAccelerationStructure::InitCascadeBakers(const Runtime::Config::UserSettings& settings, uint32_t maxCascadeCapacity)
{
    const float baseUnit = SanitizeAmbientCubeUnit(settings.AmbientCubeUnit);
    const vec3 cubeOffsetBias = vec3(settings.AmbientCubeOffsetX, settings.AmbientCubeOffsetY, settings.AmbientCubeOffsetZ);
    // Clamp to the GPU arena's allocated capacity so the per-cascade upload never writes out of bounds.
    const uint32_t cascadeCount =
        std::min(SanitizeAmbientCubeCascadeCount(settings.AmbientCubeCascadeCount), std::max(1u, maxCascadeCapacity));
    const float cascadeRatio = SanitizeAmbientCubeCascadeRatio(settings.AmbientCubeCascadeRatio);

    bool needRebuild = cascadeBakers.size() != cascadeCount;
    if (!needRebuild)
    {
        for (uint32_t i = 0; i < cascadeCount; ++i)
        {
            const float unit = CalculateAmbientCubeCascadeUnit(baseUnit, cascadeRatio, i);
            const vec3 offset = CalculateAmbientCubeOffset(unit, cubeOffsetBias);
            const FCPUProbeBaker& baker = cascadeBakers[i];
            if (glm::abs(unit - baker.UNIT_SIZE) > 1e-6f || glm::length(offset - baker.CUBE_OFFSET) > 1e-6f)
            {
                needRebuild = true;
                break;
            }
        }
    }

    if (!needRebuild)
    {
        return false;
    }

    cascadeBakers.clear();
    cascadeBakers.resize(cascadeCount);
    for (uint32_t i = 0; i < cascadeCount; ++i)
    {
        const float unit = CalculateAmbientCubeCascadeUnit(baseUnit, cascadeRatio, i);
        const vec3 offset = CalculateAmbientCubeOffset(unit, cubeOffsetBias);
        cascadeBakers[i].Init(i, unit, offset);
    }

    cpuPageIndex.Init();
    return true;
}

void FCPUAccelerationStructure::InitBVH(Scene& scene)
{
    auto& hdr = GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics();
    std::memcpy(HdrsHs, hdr.data(), hdr.size() * sizeof(SphericalHarmonics));
    
    const auto timer = std::chrono::high_resolution_clock::now();

    bvhBLASList.clear();
    bvhBLASContexts.clear();

    bvhBLASContexts.resize(scene.Models().size());
    for ( size_t m = 0; m < scene.Models().size(); ++m )
    {
        const Model& model = scene.Models()[m];
        for (size_t i = 0; i < model.CPUIndices().size(); i += 3)
        {
            // Get the three vertices of the triangle
            const Vertex& v0 = model.CPUVertices()[model.CPUIndices()[i]];
            const Vertex& v1 = model.CPUVertices()[model.CPUIndices()[i + 1]];
            const Vertex& v2 = model.CPUVertices()[model.CPUIndices()[i + 2]];
            
            // Calculate face normal
            vec3 edge1 = vec3(v1.Position) - vec3(v0.Position);
            vec3 edge2 = vec3(v2.Position) - vec3(v1.Position);
            vec3 normal = normalize(cross(edge1, edge2));
            
            // Add triangle vertices to BVH
            bvhBLASContexts[m].triangles.push_back(tinybvh::bvhvec4(v0.Position.x, v0.Position.y, v0.Position.z, 0));
            bvhBLASContexts[m].triangles.push_back(tinybvh::bvhvec4(v1.Position.x, v1.Position.y, v1.Position.z, 0));
            bvhBLASContexts[m].triangles.push_back(tinybvh::bvhvec4(v2.Position.x, v2.Position.y, v2.Position.z, 0));

            // Store additional triangle information
            bvhBLASContexts[m].extinfos.push_back({normal, v0.MaterialIndex});
        }

        // here we can cache the blas to disk if its big enough
        if (bvhBLASContexts[m].triangles.size() > 16384 * 3)
        {
            XXH64_hash_t vhash = XXH64(bvhBLASContexts[m].triangles.data(), bvhBLASContexts[m].triangles.size() * sizeof(tinybvh::bvhvec4), 0);
            std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", vhash), "cpubvh");

            if (!std::filesystem::exists(cacheFileName))
            {
                bvhBLASContexts[m].bvh.Build( bvhBLASContexts[m].triangles.data(), static_cast<int>(bvhBLASContexts[m].triangles.size()) / 3 );
                bvhBLASContexts[m].bvh.Save(cacheFileName.c_str());
            }
            else
            {
                bvhBLASContexts[m].bvh.Load(cacheFileName.c_str(), bvhBLASContexts[m].triangles.data(), static_cast<int>(bvhBLASContexts[m].triangles.size()) / 3 );
            }
        }
        else
        {
            bvhBLASContexts[m].bvh.Build( bvhBLASContexts[m].triangles.data(), static_cast<int>(bvhBLASContexts[m].triangles.size()) / 3 );
        }

        bvhBLASList.push_back( &bvhBLASContexts[m].bvh );
    }
    
    const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    InitCascadeBakers(settings, scene.AmbientCubeCascadeCapacity());

    UpdateBVH(scene);
}

void FCPUAccelerationStructure::UpdateBVH(Scene& scene)
{
    std::vector<tinybvh::BLASInstance> tmpbvhInstanceList;
    std::vector<FCPUTLASInstanceInfo> tmpbvhTLASContexts;
    std::unordered_map<uint32_t, FWorldBounds> previousNavBounds;
    std::unordered_map<uint32_t, FWorldBounds> currentNavBounds;

    previousNavBounds.reserve(bvhTLASContexts.size());
    for (const FCPUTLASInstanceInfo& previousInfo : bvhTLASContexts)
    {
        if (!previousInfo.navRelevant)
        {
            continue;
        }
        previousNavBounds[previousInfo.nodeId] = {previousInfo.worldBoundsMin, previousInfo.worldBoundsMax};
    }

    for (auto& node : scene.Nodes())
    {
        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render) continue;
        const uint32_t modelId = render->GetModelId();
        if (modelId == -1) continue;
        if (!render->GetVisible()) continue;
        if (!render->GetRayCastVisible()) continue;

        node->RecalcTransform(true);
        const glm::mat4 nodeWorldTransform = node->WorldTransform();
        const FWorldBounds worldBounds = ComputeWorldBounds(scene.Models()[modelId], nodeWorldTransform);

        mat4 worldTS = transpose(nodeWorldTransform);

        tinybvh::BLASInstance instance;
        instance.blasIdx = modelId;
        std::memcpy( (float*)instance.transform, &(worldTS[0]), sizeof(float) * 16);

        tmpbvhInstanceList.push_back(instance);
        FCPUTLASInstanceInfo info;
        info.matIdxs.fill(0);
        info.nodeId = node->GetInstanceId();
        info.worldBoundsMin = worldBounds.min;
        info.worldBoundsMax = worldBounds.max;
        if (const auto physics = node->GetComponent<Runtime::PhysicsComponent>())
        {
            info.navRelevant = physics->GetMobility() != Runtime::ENodeMobility::Dynamic;
        }
        else
        {
            info.navRelevant = true;
        }

        const auto& mats = render->GetMaterials();
        for (int i = 0; i < mats.size() && i < static_cast<int>(info.matIdxs.size()); ++i)
        {
            uint32_t matId = mats[i];
            info.matIdxs[i] = matId;
        }

        if (info.navRelevant)
        {
            currentNavBounds[info.nodeId] = worldBounds;
        }
        tmpbvhTLASContexts.push_back( info );
    }

    bool hasNavDirtyBounds = false;
    glm::vec3 navDirtyWorldMin(0.0f);
    glm::vec3 navDirtyWorldMax(0.0f);

    for (const auto& [nodeId, currentBounds] : currentNavBounds)
    {
        const auto previousIt = previousNavBounds.find(nodeId);
        if (previousIt == previousNavBounds.end())
        {
            AccumulateBounds(hasNavDirtyBounds, navDirtyWorldMin, navDirtyWorldMax, currentBounds);
            continue;
        }

        if (!BoundsNearlyEqual(currentBounds, previousIt->second))
        {
            AccumulateBounds(hasNavDirtyBounds, navDirtyWorldMin, navDirtyWorldMax, currentBounds);
            AccumulateBounds(hasNavDirtyBounds, navDirtyWorldMin, navDirtyWorldMax, previousIt->second);
        }
    }

    for (const auto& [nodeId, previousBounds] : previousNavBounds)
    {
        if (currentNavBounds.find(nodeId) == currentNavBounds.end())
        {
            AccumulateBounds(hasNavDirtyBounds, navDirtyWorldMin, navDirtyWorldMax, previousBounds);
        }
    }

    if (tmpbvhInstanceList.size() > 0)
    {
        GCpuBvh.Build( tmpbvhInstanceList.data(), static_cast<int>(tmpbvhInstanceList.size()), bvhBLASList.data(), static_cast<int>(bvhBLASList.size()) );
    }

    Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();

    bvhInstanceList.swap(tmpbvhInstanceList);
    bvhTLASContexts.swap(tmpbvhTLASContexts);
    
    // rebind with new address
    GbvhInstanceList = &bvhInstanceList;
    GbvhTlasContexts = &bvhTLASContexts;
    GbvhBlasContexts = &bvhBLASContexts;

    if (hasNavDirtyBounds)
    {
        if (!hasNavRelevantDirtyBounds_)
        {
            navRelevantDirtyWorldMin_ = navDirtyWorldMin;
            navRelevantDirtyWorldMax_ = navDirtyWorldMax;
            hasNavRelevantDirtyBounds_ = true;
        }
        else
        {
            navRelevantDirtyWorldMin_ = glm::min(navRelevantDirtyWorldMin_, navDirtyWorldMin);
            navRelevantDirtyWorldMax_ = glm::max(navRelevantDirtyWorldMax_, navDirtyWorldMax);
        }
    }
}

void FCPUAccelerationStructure::RebuildBVHOnly(Scene& scene)
{
    UpdateBVH(scene);
}

RayCastResult FCPUAccelerationStructure::RayCastInCPU(vec3 rayOrigin, vec3 rayDir)
{
    RayCastResult result {};

    if (GCpuBvh.blasCount > 0)
    {
        tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), 2000.0f);
        GCpuBvh.Intersect(ray);

        if (ray.hit.t < 2000.f)
        {
            vec3 hitPos = rayOrigin + rayDir * ray.hit.t;
            uint32_t primIdx = ray.hit.prim;
            tinybvh::BLASInstance& instance = (*GbvhInstanceList)[ray.hit.inst];
            FCPUTLASInstanceInfo& instContext = (*GbvhTlasContexts)[ray.hit.inst];
            FCPUBLASContext& context = (*GbvhBlasContexts)[instance.blasIdx];
            mat4* worldTS = (mat4*)instance.transform;
            vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;
            // Ensure normal faces toward the ray origin (flip if we hit a back face)
            if (glm::dot(glm::vec3(normalWS), rayDir) > 0.0f)
                normalWS = -normalWS;
            result.HitPoint = vec4(hitPos, 0);
            result.Normal = normalWS;
            result.Hitted = true;
            result.T = ray.hit.t;
            result.InstanceId = instContext.nodeId;
        }
    }

    return result;
}

void FCPUProbeBaker::ProcessCube(int x, int y, int z, ECubeProcType procType)
{
    vec3 probePos = vec3(x, y, z) * UNIT_SIZE + CUBE_OFFSET;
    uint32_t addressIdx = GetVoxelAddress(x, y, z);
    VoxelData& voxel = voxels[addressIdx];
        
    switch (procType)
    {
        case ECubeProcType::ECPT_Clear:
            voxel = {};
            voxel.distanceToSolid_gg_z01 = SetPackedByteX(voxel.distanceToSolid_gg_z01, kMaxDistanceFieldSeed);
            distanceToSolidSeeds[addressIdx] = kMaxDistanceFieldSeed;
            break;
        case ECubeProcType::ECPT_Fence:
            break;
        case ECubeProcType::ECPT_Voxelize:
            VoxelizeCube(voxel, probePos, UNIT_SIZE);
            distanceToSolidSeeds[addressIdx] = GetPackedByteX(voxel.distanceToSolid_gg_z01);
            break;
    }
}

void FCPUProbeBaker::UploadGPU(Vulkan::DeviceMemory& voxelGpuMemory)
{
    UploadGPU(voxelGpuMemory, 0);
}

bool FCPUAccelerationStructure::AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* voxelGpuMemory, bool incremental)
{
    if ( !Tasks::TaskCoordinator::GetInstance()->IsAllParralledTaskComplete() )
    {
        return false;
    }
    // clean
    while (!needUpdateGroups.empty())
        needUpdateGroups.pop();
    lastBatchTasks.clear();

    const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    if (InitCascadeBakers(settings, scene.AmbientCubeCascadeCapacity()))
    {
        incremental = false;
    }

    if (!incremental)
    {
        for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
        {
            FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
            baker.ClearAmbientCubes();
            baker.UploadGPU(*voxelGpuMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount);
        }
    }
    else
    {
        UpdateBVH(scene);
    }
    
    const int groupSize = 16;
    const int lengthX = CUBE_SIZE_XY / groupSize;
    const int lengthZ = CUBE_SIZE_XY / groupSize;

    // far probe gen
    // for (int x = 0; x < lengthX; x++)
    //     for (int z = 0; z < lengthZ; z++)
    //         needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Voxelize, EBakerType::EBT_FarProbe});
    
    // 2 pass near probe iterate
    for(int pass = 0; pass < 1; ++pass)
    {
        // shuffle
        std::vector<std::pair<int, int>> coordinates;
        for (int x = 0; x < lengthX; x++)
            for (int z = 0; z < lengthZ; z++)
                coordinates.push_back({x, z});
        
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(coordinates.begin(), coordinates.end(), g);

        // dispatch
        for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
        {
            for (const auto& [x, z] : coordinates)
            {
                needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Voxelize, EBakerType::EBT_Probe, cascadeIndex});
            }
            needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe, cascadeIndex});
        }
        // add fence
    }

    return true;
}

void FCPUAccelerationStructure::AsyncProcessGroup(int xInMeter, int zInMeter, Scene& scene, ECubeProcType procType,
                                                  EBakerType bakerType, uint32_t cascadeIndex)
{
    (void)bakerType;
    if (bvhInstanceList.empty())
    {
        return;
    }

    if (cascadeIndex >= cascadeBakers.size())
    {
        return;
    }
    
    int groupSize = 16; // 4 x 4 x 40 a group
    
    int actualX = xInMeter * groupSize;
    int actualZ = zInMeter * groupSize;

    std::vector<vec3> lightPos;
    std::vector<vec3> sunDir;
    for( auto& light : scene.Lights() )
    {
        lightPos.push_back(mix(light.p1, light.p3, 0.5f));
    }

    if (scene.HasSun())
    {
        sunDir.push_back( scene.GetSunDir() );
    }

    uint32_t taskId = Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
                [this, actualX, actualZ, groupSize, procType, cascadeIndex](Tasks::ResTask& task)
            {
                FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
                for (int z = actualZ; z < actualZ + groupSize; z++)
                    for (int y = 0; y < CUBE_SIZE_Z; y++)
                        for (int x = actualX; x < actualX + groupSize; x++)
                        {
                            baker.ProcessCube(x, y, z, procType);
                        }
            },
            [this](Tasks::ResTask& task)
            {
                // flush here
                //bakerType == EBakerType::EBT_Probe ? probeBaker.UploadGPU(*GPUMemory) : farProbeBaker.UploadGPU(*FarGPUMemory);
                needFlush = true;
            });

    lastBatchTasks.push_back(taskId);
}
void FCPUAccelerationStructure::ClearAllTasks()
{
    // 等待所有并行任务完成
    Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();
    
    // 清空待更新队列
    while (!needUpdateGroups.empty())
    {
        needUpdateGroups.pop();
    }
    
    // 清空当前批次任务列表
    lastBatchTasks.clear();
    distanceFieldRebuildTasks.clear();
    
    // 重置刷新标志
    needFlush = false;
    distanceFieldRebuildScheduled_ = false;
    ClearNavRelevantDirtyBounds();
}

bool FCPUAccelerationStructure::Tick(Scene& scene, Vulkan::DeviceMemory* gpuMemory, Vulkan::DeviceMemory* voxelGpuMemory, Vulkan::DeviceMemory* pageIndexMemory)
{
    bool voxelUploadCompleted = false;
    const bool batchComplete = lastBatchTasks.empty() || Tasks::TaskCoordinator::GetInstance()->IsAllTaskComplete(lastBatchTasks);
    if (needFlush && batchComplete)
    {
        const bool useGpuAmbientCubeSdf = NextEngine::GetInstance()->GetUserSettings().UseGpuAmbientCubeSdf;
        if (useGpuAmbientCubeSdf)
        {
            distanceFieldRebuildScheduled_ = false;
            distanceFieldRebuildTasks.clear();
            for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
            {
                cascadeBakers[cascadeIndex].UploadGPU(
                    *voxelGpuMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount);
            }
            if (!cascadeBakers.empty())
            {
                cpuPageIndex.UpdateData(cascadeBakers);
                cpuBrickTable.UpdateData(cascadeBakers, scene.AmbientCubeCascadeCapacity(),
                                         scene.AmbientPoolBricksPerCascade(), kAmbientBrickDilationRadius);
                scene.SetAmbientActiveBrickCounts(cpuBrickTable.activeBricksPerCascade);
                cpuBrickTable.UploadGPU(*voxelGpuMemory, scene.AmbientBrickTableByteOffset(),
                                        scene.AmbientActiveBrickListByteOffset());
            }
            cpuPageIndex.UploadGPU(*pageIndexMemory, scene.AmbientPagesByteOffset());
            needFlush = false;
            voxelUploadCompleted = true;
        }
        else if (!distanceFieldRebuildScheduled_)
        {
            distanceFieldRebuildTasks.clear();
            distanceFieldRebuildTasks.reserve(GetActiveCascadeCount());
            for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
            {
                const uint32_t taskId = Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
                    [this, cascadeIndex](Tasks::ResTask& task)
                    {
                        cascadeBakers[cascadeIndex].RebuildDistanceField();
                    },
                    nullptr);
                distanceFieldRebuildTasks.push_back(taskId);
            }
            distanceFieldRebuildScheduled_ = true;
        }
        else if (distanceFieldRebuildTasks.empty() || Tasks::TaskCoordinator::GetInstance()->IsAllTaskComplete(distanceFieldRebuildTasks))
        {
            // Upload to GPU, now entire range, optimize to partial upload later
            for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
            {
                cascadeBakers[cascadeIndex].UploadGPU(
                    *voxelGpuMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount);
            }
            if (!cascadeBakers.empty())
            {
                cpuPageIndex.UpdateData(cascadeBakers);
                cpuBrickTable.UpdateData(cascadeBakers, scene.AmbientCubeCascadeCapacity(),
                                         scene.AmbientPoolBricksPerCascade(), kAmbientBrickDilationRadius);
                scene.SetAmbientActiveBrickCounts(cpuBrickTable.activeBricksPerCascade);
                cpuBrickTable.UploadGPU(*voxelGpuMemory, scene.AmbientBrickTableByteOffset(),
                                        scene.AmbientActiveBrickListByteOffset());
            }
            cpuPageIndex.UploadGPU(*pageIndexMemory, scene.AmbientPagesByteOffset());
            needFlush = false;
            distanceFieldRebuildScheduled_ = false;
            distanceFieldRebuildTasks.clear();
            voxelUploadCompleted = true;
        }
    }

    if (!lastBatchTasks.empty())
    {
        if (Tasks::TaskCoordinator::GetInstance()->IsAllTaskComplete(lastBatchTasks))
        {
            lastBatchTasks.clear();
        }
    }
    else
    {
        while (!needUpdateGroups.empty())
        {
            auto& group = needUpdateGroups.front();
            ECubeProcType type = std::get<1>(group);
            uint32_t cascadeIndex = std::get<3>(group);
            if (type == ECubeProcType::ECPT_Fence)
            {
                if (!Tasks::TaskCoordinator::GetInstance()->IsAllTaskComplete(lastBatchTasks))
                {
                    break; 
                }
                needUpdateGroups.pop();
                continue;
            }
            AsyncProcessGroup(std::get<0>(group).x, std::get<0>(group).z, scene, std::get<1>(group), std::get<2>(group), cascadeIndex);
            needUpdateGroups.pop();
        }
    }

    return voxelUploadCompleted;
}

bool FCPUAccelerationStructure::HasPendingWork() const
{
    return needFlush || !lastBatchTasks.empty() || !distanceFieldRebuildTasks.empty() || distanceFieldRebuildScheduled_ || !needUpdateGroups.empty();
}

void FCPUAccelerationStructure::RequestUpdate(vec3 worldPos, float radius)
{
    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        ivec3 center = ivec3((worldPos - baker.CUBE_OFFSET) / baker.UNIT_SIZE);
        const int radiusInCells = static_cast<int>(radius / baker.UNIT_SIZE);
        ivec3 min = center - ivec3(radiusInCells);
        ivec3 max = center + ivec3(radiusInCells);

        for (int x = min.x; x <= max.x; ++x)
        {
            for (int z = min.z; z <= max.z; ++z)
            {
                ivec3 point(x, 1, z);
                needUpdateGroups.push({point, ECubeProcType::ECPT_Voxelize, EBakerType::EBT_Probe, cascadeIndex});
            }
        }
    }
}

bool FCPUAccelerationStructure::ConsumeNavRelevantDirtyBounds(glm::vec3& outWorldMin, glm::vec3& outWorldMax)
{
    if (!hasNavRelevantDirtyBounds_)
    {
        return false;
    }

    outWorldMin = navRelevantDirtyWorldMin_;
    outWorldMax = navRelevantDirtyWorldMax_;
    hasNavRelevantDirtyBounds_ = false;
    navRelevantDirtyWorldMin_ = glm::vec3(0.0f);
    navRelevantDirtyWorldMax_ = glm::vec3(0.0f);
    return true;
}

void FCPUAccelerationStructure::ClearNavRelevantDirtyBounds()
{
    hasNavRelevantDirtyBounds_ = false;
    navRelevantDirtyWorldMin_ = glm::vec3(0.0f);
    navRelevantDirtyWorldMax_ = glm::vec3(0.0f);
}

void FCPUProbeBaker::ClearAmbientCubes()
{
    for (size_t voxelIndex = 0; voxelIndex < voxels.size(); ++voxelIndex)
    {
        VoxelData& voxel = voxels[voxelIndex];
        voxel = {};
        voxel.distanceToSolid_gg_z01 = SetPackedByteX(voxel.distanceToSolid_gg_z01, kMaxDistanceFieldSeed);
        distanceToSolidSeeds[voxelIndex] = kMaxDistanceFieldSeed;
    }
}

void FCPUPageIndex::Init()
{
    pageIndex.resize(Assets::ACGI_PAGE_COUNT * Assets::ACGI_PAGE_COUNT);
}

void FCPUBrickTable::UpdateData(const std::vector<FCPUProbeBaker>& bakers, uint32_t cascadeCapacity,
                                uint32_t poolBricksPerCascade, int dilationRadius)
{
    const int BX = Assets::GPU_SCENE_AMBIENT_BRICKS_X;
    const int BY = Assets::GPU_SCENE_AMBIENT_BRICKS_Y;
    const int BZ = Assets::GPU_SCENE_AMBIENT_BRICKS_Z;
    const int EDGE = Assets::GPU_SCENE_AMBIENT_BRICK_EDGE;
    const uint32_t BPC = static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
    const uint32_t kInvalid = Assets::GPU_SCENE_AMBIENT_BRICK_INVALID;

    brickTable.assign(static_cast<size_t>(cascadeCapacity) * BPC, kInvalid);
    activeBrickList.assign(static_cast<size_t>(cascadeCapacity) * poolBricksPerCascade, kInvalid);
    activeBricksPerCascade.assign(cascadeCapacity, 0u);

    const uint32_t cascadesToProcess = std::min<uint32_t>(cascadeCapacity, static_cast<uint32_t>(bakers.size()));
    uint32_t totalActive = 0;
    uint32_t totalOverflow = 0;
    std::vector<uint8_t> active(BPC, 0);
    for (uint32_t c = 0; c < cascadesToProcess; ++c)
    {
        const FCPUProbeBaker& baker = bakers[c];
        std::fill(active.begin(), active.end(), uint8_t(0));

        const uint32_t voxelCount = static_cast<uint32_t>(baker.voxels.size());
        for (uint32_t v = 0; v < voxelCount; ++v)
        {
            if (baker.voxels[v].matId == 0)
            {
                continue;
            }
            const uint32_t y = v / (Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY);
            const uint32_t z = (v - y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY) / Assets::CUBE_SIZE_XY;
            const uint32_t x = v - y * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY - z * Assets::CUBE_SIZE_XY;
            const int sbx = static_cast<int>(x) / EDGE;
            const int sby = static_cast<int>(y) / EDGE;
            const int sbz = static_cast<int>(z) / EDGE;
            for (int dy = -dilationRadius; dy <= dilationRadius; ++dy)
            {
                const int nby = sby + dy;
                if (nby < 0 || nby >= BY) continue;
                for (int dz = -dilationRadius; dz <= dilationRadius; ++dz)
                {
                    const int nbz = sbz + dz;
                    if (nbz < 0 || nbz >= BZ) continue;
                    for (int dx = -dilationRadius; dx <= dilationRadius; ++dx)
                    {
                        const int nbx = sbx + dx;
                        if (nbx < 0 || nbx >= BX) continue;
                        active[static_cast<uint32_t>(nby) * (BX * BZ) + static_cast<uint32_t>(nbz) * BX +
                               static_cast<uint32_t>(nbx)] = 1;
                    }
                }
            }
        }

        // Compact active bricks into pool slots in deterministic brick-linear order (stable for static
        // scenes, so the GPU cube pool stays coherent across flushes). Beyond the cap they stay INVALID.
        uint32_t slot = 0;
        for (uint32_t b = 0; b < BPC; ++b)
        {
            if (!active[b])
            {
                continue;
            }
            if (slot < poolBricksPerCascade)
            {
                brickTable[static_cast<size_t>(c) * BPC + b] = slot++;
                activeBrickList[static_cast<size_t>(c) * poolBricksPerCascade + (slot - 1u)] = b;
            }
            else
            {
                ++totalOverflow;
            }
        }
        activeBricksPerCascade[c] = slot;
        totalActive += slot;
    }

    activeBricksLastBuild = totalActive;
    if (totalOverflow > 0)
    {
        SPDLOG_WARN("[AmbientBrick] cube pool overflow: {} bricks dropped (cap {}/cascade); GI missing there",
                    totalOverflow, poolBricksPerCascade);
    }
    // SPDLOG_INFO("[AmbientBrick] active bricks {} / capacity {} ({}/cascade x {} cascades)", totalActive,
    //             poolBricksPerCascade * cascadesToProcess, poolBricksPerCascade, cascadesToProcess);
}

void FCPUBrickTable::UploadGPU(Vulkan::DeviceMemory& deviceMemory, size_t tableByteOffset, size_t activeListByteOffset)
{
    if (brickTable.empty())
    {
        return;
    }
    const size_t bytes = brickTable.size() * sizeof(uint32_t);
    void* mapped = deviceMemory.Map(static_cast<VkDeviceSize>(tableByteOffset), static_cast<VkDeviceSize>(bytes));
    std::memcpy(mapped, brickTable.data(), bytes);
    deviceMemory.Unmap();

    if (!activeBrickList.empty())
    {
        const size_t activeListBytes = activeBrickList.size() * sizeof(uint32_t);
        mapped = deviceMemory.Map(static_cast<VkDeviceSize>(activeListByteOffset),
                                  static_cast<VkDeviceSize>(activeListBytes));
        std::memcpy(mapped, activeBrickList.data(), activeListBytes);
        deviceMemory.Unmap();
    }
}

void FCPUPageIndex::UpdateData(const std::vector<FCPUProbeBaker>& bakers)
{
    // 粗暴实现，先全部page置空
    for (auto& page : pageIndex)
    {
        page = {};
        page.voxelCount = 0;
    }

    // 聚合所有cascade，构建全局PageIndex覆盖
    for (const FCPUProbeBaker& baker : bakers)
    {
        const uint32_t voxelCount = static_cast<uint32_t>(baker.voxels.size());
        for (uint32_t gIdx = 0; gIdx < voxelCount; ++gIdx)
        {
            const VoxelData& voxel = baker.voxels[gIdx];
            if (voxel.matId == 0)
            {
                continue;
            }

            // convert to local position
            uint y = gIdx / (CUBE_SIZE_XY * CUBE_SIZE_XY);
            uint z = (gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY) / CUBE_SIZE_XY;
            uint x = gIdx - y * CUBE_SIZE_XY * CUBE_SIZE_XY - z * CUBE_SIZE_XY;

            vec3 worldPos = vec3(x, y, z) * baker.UNIT_SIZE + baker.CUBE_OFFSET;

            Assets::PageIndex& page = GetPage(worldPos);

            // 当前仅使用voxelCount>0做粗粒度裁剪，保持占用标记语义即可
            page.voxelCount = 1;
        }
    }
}

Assets::PageIndex& FCPUPageIndex::GetPage(glm::vec3 worldpos)
{
    // 假设CUBE_OFFSET定义了世界空间的起始位置
    glm::vec3 relativePos = worldpos - Assets::ACGI_PAGE_OFFSET;

    // 计算页面索引，假设每个page对应PAGE_UNIT的世界空间距离
    // 使用xz平面进行映射
    int pageX = static_cast<int>(relativePos.x / Assets::ACGI_PAGE_SIZE);
    int pageZ = static_cast<int>(relativePos.z / Assets::ACGI_PAGE_SIZE);

    // 限制在有效范围内
    pageX = glm::clamp(pageX, 0, Assets::ACGI_PAGE_COUNT - 1);
    pageZ = glm::clamp(pageZ, 0, Assets::ACGI_PAGE_COUNT - 1);

    // 计算一维索引
    int index = pageZ * Assets::ACGI_PAGE_COUNT + pageX;

    // 返回对应的PageIndex引用
    return pageIndex[index];
}

void FCPUPageIndex::UploadGPU(Vulkan::DeviceMemory& gpuMemory)
{
    UploadGPU(gpuMemory, 0);
}

void FCPUPageIndex::UploadGPU(Vulkan::DeviceMemory& gpuMemory, size_t byteBaseOffset)
{
    PageIndex* data = reinterpret_cast<PageIndex*>(
        gpuMemory.Map(byteBaseOffset, sizeof(PageIndex) * pageIndex.size()));
    std::memcpy(data, pageIndex.data(), pageIndex.size() * sizeof(PageIndex));
    gpuMemory.Unmap();
}

void FCPUAccelerationStructure::GenShadowMap(Scene& scene)
{
    if (bvhInstanceList.empty())
    {
        return;
    }

    if (!scene.GetEnvSettings().HasSun)
    {
        return;
    }
    
    const vec3& sunDir = scene.GetEnvSettings().SunDirection();
    
    // 阴影图分辨率设置
    int shadowMapSize = SHADOWMAP_SIZE;
    int tileSize = 256; // 每个tile的大小
    int tilesPerRow = shadowMapSize / tileSize;
    shadowMapR32.resize(shadowMapSize * shadowMapSize, 0); // 初始化为1.0（不被遮挡）

    // 使用环境设置中的方法获取光源视图投影矩阵
    mat4 lightViewProj = scene.GetEnvSettings().GetSunViewProjection();
    mat4 invLVP = inverse(lightViewProj);
    vec3 lightDir = normalize(-sunDir);

    
    // 计算当前tile的起始像素坐标
    for ( int currentTileX = 0; currentTileX < tilesPerRow; ++currentTileX )
    {
        for ( int currentTileY = 0; currentTileY < tilesPerRow; ++currentTileY )
        {
            int startX = currentTileX * tileSize;
            int startY = currentTileY * tileSize;

                // 处理当前tile
            Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
                [this, lightViewProj, invLVP, lightDir, startX, startY, tileSize, shadowMapSize](Tasks::ResTask& task)
                {
                    for (int y = 0; y < tileSize; y++)
                    {
                        for (int x = 0; x < tileSize; x++)
                        {
                            int pixelX = startX + x;
                            int pixelY = startY + y;
                            
                            // 计算NDC坐标
                            float ndcX = (pixelX / static_cast<float>(shadowMapSize - 1)) * 2.0f - 1.0f;
                            float ndcY = 1.0f - (pixelY / static_cast<float>(shadowMapSize - 1)) * 2.0f;
                            
                            // 从NDC空间变换到世界空间
                            vec4 worldPos = invLVP * vec4(ndcX, ndcY, 0.0f, 1.0f);
                            worldPos /= worldPos.w;
                            
                            // 发射光线
                            vec3 origin = vec3(worldPos);
                            vec3 rayDir = normalize(lightDir);
                            
                            tinybvh::Ray ray(
                                tinybvh::bvhvec3(origin.x, origin.y, origin.z),
                                tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z),
                                10000.0f
                            );
                            
                            GCpuBvh.Intersect(ray);
                            if (ray.hit.t < 9999.0f)
                            {
                                vec3 hitPoint = origin + rayDir * ray.hit.t;
                                vec4 hitPosInLightSpace = lightViewProj * vec4(hitPoint, 1.0f);
                                float depth = (hitPosInLightSpace.z / hitPosInLightSpace.w + 1.0f) * 0.5f;
                                shadowMapR32[pixelY * shadowMapSize + pixelX] = depth;
                            }
                        }
                    }
                },
                [this, &scene, shadowMapSize, startX, startY, tileSize](Tasks::ResTask& task)
                {
                    // 更新当前tile到GPU
                    Vulkan::CommandPool& commandPool = GlobalTexturePool::GetInstance()->GetMainThreadCommandPool();
                    const unsigned char* tileData = reinterpret_cast<const unsigned char*>(shadowMapR32.data());
                    scene.EnsureCpuShadowMap(commandPool).UpdateDataMainThread(commandPool, startX, startY, tileSize, tileSize, shadowMapSize, shadowMapSize,
                        tileData, shadowMapSize * shadowMapSize * sizeof(float));
                }
            );
        }
    }
}

}
