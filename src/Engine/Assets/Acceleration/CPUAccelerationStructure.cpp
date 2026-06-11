#include "Engine/Assets/Acceleration/CPUAccelerationStructure.h"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.Internal.hpp"
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
#include <xxhash.h>

namespace Assets::CPU
{

namespace
{
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

}

using namespace Assets;

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
        GetCpuBvhState().bvh.Build( tmpbvhInstanceList.data(), static_cast<int>(tmpbvhInstanceList.size()), bvhBLASList.data(), static_cast<int>(bvhBLASList.size()) );
    }

    Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();

    bvhInstanceList.swap(tmpbvhInstanceList);
    bvhTLASContexts.swap(tmpbvhTLASContexts);
    
    // rebind with new address
    GetCpuBvhState().instanceList = &bvhInstanceList;
    GetCpuBvhState().tlasContexts = &bvhTLASContexts;
    GetCpuBvhState().blasContexts = &bvhBLASContexts;

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

    if (GetCpuBvhState().bvh.blasCount > 0)
    {
        tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), 2000.0f);
        GetCpuBvhState().bvh.Intersect(ray);

        if (ray.hit.t < 2000.f)
        {
            vec3 hitPos = rayOrigin + rayDir * ray.hit.t;
            uint32_t primIdx = ray.hit.prim;
            tinybvh::BLASInstance& instance = (*GetCpuBvhState().instanceList)[ray.hit.inst];
            FCPUTLASInstanceInfo& instContext = (*GetCpuBvhState().tlasContexts)[ray.hit.inst];
            FCPUBLASContext& context = (*GetCpuBvhState().blasContexts)[instance.blasIdx];
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

}
