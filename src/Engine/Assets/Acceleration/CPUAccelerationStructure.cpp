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

    CancelRuntimeBuilds();

    auto blasSet = std::make_shared<FCPUBLASSet>();
    blasSet->generation = ++blasGeneration_;
    blasSet->contexts.resize(scene.Models().size());
    blasSet->list.reserve(scene.Models().size());
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
            blasSet->contexts[m].triangles.push_back(tinybvh::bvhvec4(v0.Position.x, v0.Position.y, v0.Position.z, 0));
            blasSet->contexts[m].triangles.push_back(tinybvh::bvhvec4(v1.Position.x, v1.Position.y, v1.Position.z, 0));
            blasSet->contexts[m].triangles.push_back(tinybvh::bvhvec4(v2.Position.x, v2.Position.y, v2.Position.z, 0));

            // Store additional triangle information
            blasSet->contexts[m].extinfos.push_back({normal, v0.MaterialIndex});
        }

        // here we can cache the blas to disk if its big enough
        if (blasSet->contexts[m].triangles.size() > 16384 * 3)
        {
            XXH64_hash_t vhash = XXH64(blasSet->contexts[m].triangles.data(), blasSet->contexts[m].triangles.size() * sizeof(tinybvh::bvhvec4), 0);
            std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", vhash), "cpubvh");

            if (!std::filesystem::exists(cacheFileName))
            {
                blasSet->contexts[m].bvh.Build(blasSet->contexts[m].triangles.data(), static_cast<int>(blasSet->contexts[m].triangles.size()) / 3);
                blasSet->contexts[m].bvh.Save(cacheFileName.c_str());
            }
            else
            {
                blasSet->contexts[m].bvh.Load(cacheFileName.c_str(), blasSet->contexts[m].triangles.data(), static_cast<int>(blasSet->contexts[m].triangles.size()) / 3);
            }
        }
        else
        {
            blasSet->contexts[m].bvh.Build(blasSet->contexts[m].triangles.data(), static_cast<int>(blasSet->contexts[m].triangles.size()) / 3);
        }

        blasSet->list.push_back(&blasSet->contexts[m].bvh);
    }

    blasSet_ = std::move(blasSet);
    
    const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
    InitCascadeBakers(settings, scene.AmbientCubeCascadeCapacity());

    UpdateBVH(scene);
}

void FCPUAccelerationStructure::UpdateBVH(Scene& scene)
{
    std::shared_ptr<FCPUTLASBuildInput> input = CaptureBuildInput(scene);
    std::shared_ptr<FCPUTLASBuildResult> result = BuildSnapshot(*input);
    PublishSnapshot(result->snapshot);
    MergeNavDirtyBounds(*result);
    publishedRevision_.store(result->snapshot->sceneRevision, std::memory_order_release);
    ++completedBuildCount_;
}

std::shared_ptr<FCPUTLASBuildInput> FCPUAccelerationStructure::CaptureBuildInput(Scene& scene)
{
    auto input = std::make_shared<FCPUTLASBuildInput>();
    const auto captureStartTime = std::chrono::steady_clock::now();
    input->epoch = buildEpoch_.load(std::memory_order_acquire);
    input->sceneRevision = ++sceneRevision_;
    input->requestTime = std::chrono::steady_clock::now();
    input->blasSet = blasSet_;
    input->previousSnapshot = AcquireSnapshot();

    auto materialTable = std::make_shared<FCPUMaterialTable>();
    materialTable->generation = ++materialGeneration_;
    materialTable->entries.reserve(scene.Materials().size());
    for (const FMaterial& material : scene.Materials())
    {
        materialTable->entries.push_back({material.gpuMaterial_.MaterialModel});
    }
    input->materialTable = std::move(materialTable);

    const auto renderComponents = scene.Components<Runtime::RenderComponent>();
    input->instances.reserve(renderComponents.size());
    input->contexts.reserve(renderComponents.size());
    for (auto* render : renderComponents)
    {
        Node* node = render->GetOwner();
        if (!node) continue;
        const uint32_t modelId = render->GetModelId();
        if (modelId == -1) continue;
        if (!render->GetVisible()) continue;
        const uint32_t participation = render->GetRenderParticipationMask();
        if ((participation & (Runtime::RenderParticipation::giBake | Runtime::RenderParticipation::gpuAs)) == 0u)
        {
            continue;
        }

        //node->RecalcTransform(true);
        const glm::mat4 nodeWorldTransform = node->WorldTransform();
        const FWorldBounds worldBounds = ComputeWorldBounds(scene.Models()[modelId], nodeWorldTransform);

        const mat4 worldTS = transpose(nodeWorldTransform);
        tinybvh::BLASInstance instance;
        instance.blasIdx = modelId;
        std::memcpy(instance.transform, &worldTS[0], sizeof(instance.transform));
        input->instances.emplace_back(std::move(instance));

        FCPUTLASInstanceInfo info;
        info.matIdxs.fill(0);
        info.nodeId = node->GetInstanceId();
        info.rayCastVisible = render->GetRayCastVisible();
        info.worldBoundsMin = worldBounds.min;
        info.worldBoundsMax = worldBounds.max;
        info.navRelevant = true;

        const auto& mats = render->GetMaterials();
        for (int i = 0; i < mats.size() && i < static_cast<int>(info.matIdxs.size()); ++i)
        {
            info.matIdxs[i] = mats[i];
        }
        input->contexts.emplace_back(std::move(info));
    }

    input->captureMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStartTime).count();
    latestRequestedRevision_.store(input->sceneRevision, std::memory_order_release);
    return input;
}

std::shared_ptr<FCPUTLASBuildResult> FCPUAccelerationStructure::BuildSnapshot(FCPUTLASBuildInput& input)
{
    auto result = std::make_shared<FCPUTLASBuildResult>();
    result->epoch = input.epoch;
    result->requestTime = input.requestTime;
    result->buildStartTime = std::chrono::steady_clock::now();

    auto snapshot = std::make_shared<FCPUTLASSnapshot>();
    snapshot->sceneRevision = input.sceneRevision;
    snapshot->blasSet = input.blasSet;
    snapshot->materialTable = input.materialTable;
    snapshot->instances = std::move(input.instances);
    snapshot->contexts = std::move(input.contexts);

    std::unordered_map<uint32_t, FWorldBounds> previousNavBounds;
    std::unordered_map<uint32_t, FWorldBounds> currentNavBounds;
    if (input.previousSnapshot)
    {
        previousNavBounds.reserve(input.previousSnapshot->contexts.size());
        for (const FCPUTLASInstanceInfo& previousInfo : input.previousSnapshot->contexts)
        {
            if (previousInfo.navRelevant)
            {
                previousNavBounds[previousInfo.nodeId] = {previousInfo.worldBoundsMin, previousInfo.worldBoundsMax};
            }
        }
    }
    currentNavBounds.reserve(snapshot->contexts.size());
    for (const FCPUTLASInstanceInfo& info : snapshot->contexts)
    {
        if (info.navRelevant)
        {
            currentNavBounds[info.nodeId] = {info.worldBoundsMin, info.worldBoundsMax};
        }
    }

    for (const auto& [nodeId, currentBounds] : currentNavBounds)
    {
        const auto previousIt = previousNavBounds.find(nodeId);
        if (previousIt == previousNavBounds.end())
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, currentBounds);
            continue;
        }

        if (!BoundsNearlyEqual(currentBounds, previousIt->second))
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, currentBounds);
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, previousIt->second);
        }
    }

    for (const auto& [nodeId, previousBounds] : previousNavBounds)
    {
        if (currentNavBounds.find(nodeId) == currentNavBounds.end())
        {
            AccumulateBounds(result->hasNavDirtyBounds, result->navDirtyWorldMin, result->navDirtyWorldMax, previousBounds);
        }
    }

    if (!snapshot->instances.empty() && snapshot->blasSet && !snapshot->blasSet->list.empty())
    {
        // tinybvh 1.3.8 does not const-qualify this non-owning BLAS pointer array,
        // although TLAS Build only reads the array and the BLAS objects.
        snapshot->tlas.Build(snapshot->instances.data(), static_cast<uint32_t>(snapshot->instances.size()),
                             const_cast<tinybvh::BVHBase**>(snapshot->blasSet->list.data()),
                             static_cast<uint32_t>(snapshot->blasSet->list.size()));
    }

    result->snapshot = std::move(snapshot);
    result->buildEndTime = std::chrono::steady_clock::now();
    return result;
}

void FCPUAccelerationStructure::MergeNavDirtyBounds(const FCPUTLASBuildResult& result)
{
    if (result.hasNavDirtyBounds)
    {
        if (!hasProbeDirtyBounds_)
        {
            probeDirtyWorldMin_ = result.navDirtyWorldMin;
            probeDirtyWorldMax_ = result.navDirtyWorldMax;
            hasProbeDirtyBounds_ = true;
        }
        else
        {
            probeDirtyWorldMin_ = glm::min(probeDirtyWorldMin_, result.navDirtyWorldMin);
            probeDirtyWorldMax_ = glm::max(probeDirtyWorldMax_, result.navDirtyWorldMax);
        }

        if (!hasNavRelevantDirtyBounds_)
        {
            navRelevantDirtyWorldMin_ = result.navDirtyWorldMin;
            navRelevantDirtyWorldMax_ = result.navDirtyWorldMax;
            hasNavRelevantDirtyBounds_ = true;
        }
        else
        {
            navRelevantDirtyWorldMin_ = glm::min(navRelevantDirtyWorldMin_, result.navDirtyWorldMin);
            navRelevantDirtyWorldMax_ = glm::max(navRelevantDirtyWorldMax_, result.navDirtyWorldMax);
        }
    }
}

void FCPUAccelerationStructure::RebuildBVHOnly(Scene& scene)
{
    RequestRuntimeBuild(scene);
}

RayCastResult FCPUAccelerationStructure::RayCastInCPU(vec3 rayOrigin, vec3 rayDir)
{
    RayCastResult result {};
    const SnapshotPtr snapshot = AcquireSnapshot();

    if (snapshot && !snapshot->instances.empty())
    {
        constexpr float maxDistance = 2000.0f;
        constexpr float skipEpsilon = 1e-3f;
        float accumulatedT = 0.0f;
        glm::vec3 currentOrigin = rayOrigin;
        for (uint32_t iteration = 0; iteration < 16 && accumulatedT < maxDistance; ++iteration)
        {
            const float remainingDistance = maxDistance - accumulatedT;
            tinybvh::Ray ray(tinybvh::bvhvec3(currentOrigin.x, currentOrigin.y, currentOrigin.z),
                             tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z), remainingDistance);
            snapshot->tlas.Intersect(ray);
            if (ray.hit.t >= remainingDistance)
            {
                break;
            }

            uint32_t primIdx = ray.hit.prim;
            if (!snapshot->blasSet || ray.hit.inst >= snapshot->instances.size() || ray.hit.inst >= snapshot->contexts.size())
            {
                break;
            }
            const tinybvh::BLASInstance& instance = snapshot->instances[ray.hit.inst];
            const FCPUTLASInstanceInfo& instContext = snapshot->contexts[ray.hit.inst];
            const float globalT = accumulatedT + ray.hit.t;
            vec3 hitPos = rayOrigin + rayDir * globalT;
            if (!instContext.rayCastVisible)
            {
                accumulatedT = globalT + skipEpsilon;
                currentOrigin = rayOrigin + rayDir * accumulatedT;
                continue;
            }

            if (instance.blasIdx >= snapshot->blasSet->contexts.size())
            {
                break;
            }
            const FCPUBLASContext& context = snapshot->blasSet->contexts[instance.blasIdx];
            if (primIdx >= context.extinfos.size())
            {
                break;
            }
            mat4* worldTS = (mat4*)instance.transform;
            vec4 normalWS = vec4( context.extinfos[primIdx].normal, 0.0f) * *worldTS;
            // Ensure normal faces toward the ray origin (flip if we hit a back face)
            if (glm::dot(glm::vec3(normalWS), rayDir) > 0.0f)
                normalWS = -normalWS;
            result.HitPoint = vec4(hitPos, 0);
            result.Normal = normalWS;
            result.Hit = true;
            result.T = globalT;
            result.InstanceId = instContext.nodeId;
            result.MaterialId = FetchMaterialId(*snapshot, context.extinfos[primIdx].matIdx, ray.hit.inst);
            break;
        }
    }

    return result;
}

FCPUAccelerationStructure::SnapshotPtr FCPUAccelerationStructure::AcquireSnapshot() const
{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    return activeSnapshot_.load(std::memory_order_acquire);
#else
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return activeSnapshot_;
#endif
}

void FCPUAccelerationStructure::PublishSnapshot(SnapshotPtr snapshot)
{
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    activeSnapshot_.store(std::move(snapshot), std::memory_order_release);
#else
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    activeSnapshot_ = std::move(snapshot);
#endif
}

uint64_t FCPUAccelerationStructure::RequestRuntimeBuild(Scene& scene)
{
    return QueueBuildInput(CaptureBuildInput(scene));
}

uint64_t FCPUAccelerationStructure::QueueBuildInput(std::shared_ptr<FCPUTLASBuildInput> input)
{
    const uint64_t requestedRevision = input->sceneRevision;
    latestRequestedRevision_.store(requestedRevision, std::memory_order_release);
    bool shouldStartBuild = false;
    {
        std::lock_guard<std::mutex> lock(buildMutex_);
        lastCaptureMilliseconds_ = input->captureMilliseconds;
        if (buildInFlight_)
        {
            latestBuildInput_ = std::move(input);
            ++coalescedRequestCount_;
        }
        else
        {
            buildInFlight_ = true;
            shouldStartBuild = true;
        }
    }

    if (shouldStartBuild)
    {
        StartBuild(std::move(input));
    }
    return requestedRevision;
}

void FCPUAccelerationStructure::StartBuild(std::shared_ptr<FCPUTLASBuildInput> input)
{
    Tasks::TaskCoordinator::GetInstance()->AddNamedTask(
        Tasks::ENamedTaskThread::CPU_AS_BUILD,
        [this, input = std::move(input)](Tasks::ResTask& task) mutable
        {
            std::shared_ptr<FCPUTLASBuildResult> result = BuildSnapshot(*input);
            std::lock_guard<std::mutex> lock(buildMutex_);
            completedBuildResult_ = std::move(result);
        });
}

void FCPUAccelerationStructure::PollBVHBuild()
{
    std::shared_ptr<FCPUTLASBuildResult> result;
    std::shared_ptr<FCPUTLASBuildInput> nextInput;
    {
        std::lock_guard<std::mutex> lock(buildMutex_);
        if (!completedBuildResult_)
        {
            return;
        }

        result = std::move(completedBuildResult_);
        nextInput = std::move(latestBuildInput_);
        buildInFlight_ = nextInput != nullptr;
    }

    const uint64_t activeEpoch = buildEpoch_.load(std::memory_order_acquire);
    const bool generationMatches = result->snapshot && result->snapshot->blasSet && blasSet_ &&
                                   result->snapshot->blasSet->generation == blasSet_->generation;
    if (result->epoch == activeEpoch && generationMatches)
    {
        const auto publishTime = std::chrono::steady_clock::now();
        PublishSnapshot(result->snapshot);
        MergeNavDirtyBounds(*result);
        publishedRevision_.store(result->snapshot->sceneRevision, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(buildMutex_);
            ++completedBuildCount_;
            lastBuildMilliseconds_ =
                std::chrono::duration<double, std::milli>(result->buildEndTime - result->buildStartTime).count();
            lastBuildToPublishMilliseconds_ =
                std::chrono::duration<double, std::milli>(publishTime - result->requestTime).count();
            buildToPublishSamples_.push_back(lastBuildToPublishMilliseconds_);
            constexpr size_t maxBuildTimingSamples = 256;
            if (buildToPublishSamples_.size() > maxBuildTimingSamples)
            {
                buildToPublishSamples_.erase(buildToPublishSamples_.begin());
            }
        }
    }

    if (nextInput)
    {
        StartBuild(std::move(nextInput));
    }
}

void FCPUAccelerationStructure::CancelRuntimeBuilds()
{
    buildEpoch_.fetch_add(1, std::memory_order_acq_rel);
    Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::CPU_AS_BUILD);
    std::lock_guard<std::mutex> lock(buildMutex_);
    buildInFlight_ = false;
    latestBuildInput_.reset();
    completedBuildResult_.reset();
    pendingProbeRevision_ = 0;
}

FCPUTLASBuildStats FCPUAccelerationStructure::GetBuildStats() const
{
    std::lock_guard<std::mutex> lock(buildMutex_);
    FCPUTLASBuildStats stats;
    stats.publishedRevision = publishedRevision_.load(std::memory_order_acquire);
    stats.latestRequestedRevision = latestRequestedRevision_.load(std::memory_order_acquire);
    stats.coalescedRequestCount = coalescedRequestCount_;
    stats.completedBuildCount = completedBuildCount_;
    stats.snapshotStaleness = stats.latestRequestedRevision > stats.publishedRevision
                                  ? stats.latestRequestedRevision - stats.publishedRevision
                                  : 0;
    stats.lastBuildMilliseconds = lastBuildMilliseconds_;
    stats.lastCaptureMilliseconds = lastCaptureMilliseconds_;
    stats.lastBuildToPublishMilliseconds = lastBuildToPublishMilliseconds_;
    if (!buildToPublishSamples_.empty())
    {
        std::vector<double> sortedSamples = buildToPublishSamples_;
        std::sort(sortedSamples.begin(), sortedSamples.end());
        const size_t p95Index = static_cast<size_t>(std::ceil(sortedSamples.size() * 0.95)) - 1;
        stats.buildToPublishP95Milliseconds = sortedSamples[p95Index];
    }
    return stats;
}

void FCPUAccelerationStructure::QueueFullProbeBake()
{
    constexpr int groupSize = 16;
    const int lengthX = CUBE_SIZE_XY / groupSize;
    const int lengthZ = CUBE_SIZE_XY / groupSize;

    std::vector<std::pair<int, int>> coordinates;
    coordinates.reserve(lengthX * lengthZ);
    for (int x = 0; x < lengthX; ++x)
    {
        for (int z = 0; z < lengthZ; ++z)
        {
            coordinates.push_back({x, z});
        }
    }

    std::random_device rd;
    std::mt19937 generator(rd());
    std::shuffle(coordinates.begin(), coordinates.end(), generator);

    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        for (const auto& [x, z] : coordinates)
        {
            needUpdateGroups.push({ivec3(x, 0, z), ECubeProcType::ECPT_Voxelize, EBakerType::EBT_Probe, cascadeIndex});
        }
        needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe, cascadeIndex});
    }
}

void FCPUAccelerationStructure::QueueProbeBakeBounds(const glm::vec3& worldMin, const glm::vec3& worldMax)
{
    constexpr int groupSize = 16;
    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        const FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        const glm::ivec3 minCell = glm::clamp(
            glm::ivec3(glm::floor((worldMin - baker.CUBE_OFFSET) / baker.UNIT_SIZE)),
            glm::ivec3(0), glm::ivec3(CUBE_SIZE_XY - 1, CUBE_SIZE_Z - 1, CUBE_SIZE_XY - 1));
        const glm::ivec3 maxCell = glm::clamp(
            glm::ivec3(glm::ceil((worldMax - baker.CUBE_OFFSET) / baker.UNIT_SIZE)),
            glm::ivec3(0), glm::ivec3(CUBE_SIZE_XY - 1, CUBE_SIZE_Z - 1, CUBE_SIZE_XY - 1));
        const int minGroupX = std::max(0, minCell.x / groupSize - 1);
        const int minGroupZ = std::max(0, minCell.z / groupSize - 1);
        const int maxGroupX = std::min(CUBE_SIZE_XY / groupSize - 1, maxCell.x / groupSize + 1);
        const int maxGroupZ = std::min(CUBE_SIZE_XY / groupSize - 1, maxCell.z / groupSize + 1);
        for (int groupX = minGroupX; groupX <= maxGroupX; ++groupX)
        {
            for (int groupZ = minGroupZ; groupZ <= maxGroupZ; ++groupZ)
            {
                needUpdateGroups.push({ivec3(groupX, 0, groupZ), ECubeProcType::ECPT_Voxelize,
                                       EBakerType::EBT_Probe, cascadeIndex});
            }
        }
        needUpdateGroups.push({ivec3(0), ECubeProcType::ECPT_Fence, EBakerType::EBT_Probe, cascadeIndex});
    }
}

bool FCPUAccelerationStructure::AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* voxelGpuMemory, bool incremental)
{
    if (incremental)
    {
        // Geometry revisions must continue to coalesce while an older probe batch
        // is running. The new batch is queued only after this revision publishes.
        pendingProbeRevision_ = RequestRuntimeBuild(scene);
        ambientBakeIdle_ = false;
        return true;
    }

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

    for (uint32_t cascadeIndex = 0; cascadeIndex < GetActiveCascadeCount(); ++cascadeIndex)
    {
        FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
        baker.ClearAmbientCubes();
        baker.UploadGPU(*voxelGpuMemory, scene.AmbientVoxelsByteOffset(), cascadeIndex * kCascadeVoxelCount);
    }

    QueueFullProbeBake();
    fullProbeBakePending_ = true;
    ambientBakeIdle_ = false;

    return true;
}

void FCPUAccelerationStructure::AsyncProcessGroup(int xInMeter, int zInMeter, Scene& scene, ECubeProcType procType,
                                                  EBakerType bakerType, uint32_t cascadeIndex)
{
    (void)bakerType;
    const SnapshotPtr snapshot = AcquireSnapshot();
    if (!snapshot || snapshot->instances.empty())
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

    if (scene.GetEnvSettings().HasSun)
    {
        sunDir.push_back(scene.GetEnvSettings().SunDirection());
    }

    uint32_t taskId = Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
                [this, snapshot, actualX, actualZ, groupSize, procType, cascadeIndex](Tasks::ResTask& task)
            {
                FCPUProbeBaker& baker = cascadeBakers[cascadeIndex];
                for (int z = actualZ; z < actualZ + groupSize; z++)
                    for (int y = 0; y < CUBE_SIZE_Z; y++)
                        for (int x = actualX; x < actualX + groupSize; x++)
                        {
                            baker.ProcessCube(x, y, z, procType, snapshot);
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
    CancelRuntimeBuilds();

    // Discard queued voxelization work before waiting. A full ambient-cube bake
    // contains many independent groups; dispatching its entire queue here makes
    // LoadScene wait for obsolete work from the outgoing scene.
    Tasks::TaskCoordinator::GetInstance()->CancelAllParralledTasks();

    // Wait for running groups and their main-thread completion callbacks before
    // clearing the state those callbacks capture.
    Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();
    
    // Clear the pending-update queue.
    while (!needUpdateGroups.empty())
    {
        needUpdateGroups.pop();
    }
    
    // Clear the current batch's task list.
    lastBatchTasks.clear();
    distanceFieldRebuildTasks.clear();
    
    // Reset the refresh flag.
    needFlush = false;
    distanceFieldRebuildScheduled_ = false;
    fullProbeBakePending_ = true;
    ambientBakeIdle_ = false;
    hasProbeDirtyBounds_ = false;
    cpuBrickTable = {};
    ClearNavRelevantDirtyBounds();
    PublishSnapshot(std::make_shared<FCPUTLASSnapshot>());
    blasSet_.reset();
}

bool FCPUAccelerationStructure::Tick(Scene& scene, Vulkan::DeviceMemory* gpuMemory, Vulkan::DeviceMemory* voxelGpuMemory, Vulkan::DeviceMemory* pageIndexMemory)
{
    if (pendingProbeRevision_ != 0 &&
        publishedRevision_.load(std::memory_order_acquire) >= pendingProbeRevision_)
    {
        if (hasProbeDirtyBounds_)
        {
            QueueProbeBakeBounds(probeDirtyWorldMin_, probeDirtyWorldMax_);
        }
        else
        {
            QueueFullProbeBake();
            fullProbeBakePending_ = true;
        }
        pendingProbeRevision_ = 0;
    }

    bool voxelUploadCompleted = false;
    const bool batchComplete = lastBatchTasks.empty() || Tasks::TaskCoordinator::GetInstance()->IsAllTaskComplete(lastBatchTasks);
    NextEngine* engine = NextEngine::GetInstance();
    const Runtime::Config::UserSettings& settings = engine->GetUserSettings();
    const uint32_t currentFrame = engine->GetTotalFrames();
    auto rebuildBrickResidency = [&]()
    {
        const uint32_t residencyCount =
            scene.AmbientCubeCascadeCapacity() * static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
        std::vector<Assets::AmbientBrickResidency> residency(residencyCount);
        void* mapped = voxelGpuMemory->Map(scene.AmbientResidencyByteOffset(),
                                          residency.size() * sizeof(Assets::AmbientBrickResidency));
        std::memcpy(residency.data(), mapped, residency.size() * sizeof(Assets::AmbientBrickResidency));
        voxelGpuMemory->Unmap();

        cpuBrickTable.UpdateData(
            cascadeBakers, scene.AmbientCubeCascadeCapacity(), scene.AmbientPoolBricksPerCascade(),
            kAmbientBrickDilationRadius, &residency, currentFrame, settings.AmbientCubeHitDrivenResidency,
            settings.AmbientCubeBounceHitAffectsResidency, settings.AmbientCubeGraceFrames,
            settings.AmbientCubeEvictFrames);

        if (!cpuBrickTable.slotsToClear.empty())
        {
            mapped = gpuMemory->Map(scene.AmbientCubesByteOffset(), scene.AmbientVoxelsByteOffset());
            auto* cubeBytes = static_cast<std::byte*>(mapped);
            constexpr size_t brickBytes =
                static_cast<size_t>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME) * sizeof(Assets::AmbientCube);
            for (uint32_t globalSlot : cpuBrickTable.slotsToClear)
            {
                std::memset(cubeBytes + static_cast<size_t>(globalSlot) * brickBytes, 0, brickBytes);
            }
            gpuMemory->Unmap();
        }

        scene.SetAmbientActiveBrickCounts(cpuBrickTable.activeBricksPerCascade);
        cpuBrickTable.UploadGPU(*voxelGpuMemory, scene.AmbientBrickTableByteOffset(),
                                scene.AmbientActiveBrickListByteOffset());
    };

    if (needFlush && batchComplete)
    {
        if (!distanceFieldRebuildScheduled_)
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
                rebuildBrickResidency();
                if (fullProbeBakePending_)
                {
                    cpuBrickTable.MarkAllActiveDirty();
                }
                else if (hasProbeDirtyBounds_)
                {
                    cpuBrickTable.MarkDirtyBounds(cascadeBakers, probeDirtyWorldMin_, probeDirtyWorldMax_,
                                                  scene.AmbientPoolBricksPerCascade());
                }

                // Keep the previous radiance while locally dirty bricks are refined. Clearing
                // resident slots here makes animated geometry visibly pulse black every time a
                // new CPU voxel revision is published. Slot ownership changes are still cleared
                // by slotsToClear in rebuildBrickResidency().
                cpuBrickTable.UploadGPU(*voxelGpuMemory, scene.AmbientBrickTableByteOffset(),
                                        scene.AmbientActiveBrickListByteOffset());
                scene.SetAmbientActiveBrickCounts(cpuBrickTable.activeBricksPerCascade);
                fullProbeBakePending_ = false;
                hasProbeDirtyBounds_ = false;
            }
            cpuPageIndex.UploadGPU(*pageIndexMemory, scene.AmbientPagesByteOffset());
            needFlush = false;
            distanceFieldRebuildScheduled_ = false;
            distanceFieldRebuildTasks.clear();
            voxelUploadCompleted = true;
        }
    }
    else if (!ambientBakeIdle_ && !cpuBrickTable.brickTable.empty() &&
             engine->GetRenderer().ActiveRendererRequirements().requestAmbientCube)
    {
        rebuildBrickResidency();
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
    std::lock_guard<std::mutex> lock(buildMutex_);
    return buildInFlight_ || pendingProbeRevision_ != 0 || needFlush || !lastBatchTasks.empty() ||
           !distanceFieldRebuildTasks.empty() || distanceFieldRebuildScheduled_ || !needUpdateGroups.empty();
}

uint32_t FCPUAccelerationStructure::AmbientBakeDirtyBrickCount(uint32_t cascadeIndex) const
{
    return cascadeIndex < cpuBrickTable.dirtyBricksPerCascade.size()
        ? cpuBrickTable.dirtyBricksPerCascade[cascadeIndex]
        : 0u;
}

void FCPUAccelerationStructure::AcknowledgeAmbientBake(uint64_t revision)
{
    cpuBrickTable.AcknowledgeDirty(revision);
    if (revision == cpuBrickTable.dirtyRevision)
    {
        ambientBakeIdle_ = true;
    }
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

void FCPUAccelerationStructure::AccumulateProbeDirtyBounds(const glm::vec3& worldMin,
                                                            const glm::vec3& worldMax)
{
    if (!hasProbeDirtyBounds_)
    {
        probeDirtyWorldMin_ = worldMin;
        probeDirtyWorldMax_ = worldMax;
        hasProbeDirtyBounds_ = true;
    }
    else
    {
        probeDirtyWorldMin_ = glm::min(probeDirtyWorldMin_, worldMin);
        probeDirtyWorldMax_ = glm::max(probeDirtyWorldMax_, worldMax);
    }
    ambientBakeIdle_ = false;
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
