#pragma once
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <glm/glm.hpp>
#include "ThirdParty/tinybvh/tiny_bvh.h"
#include <queue>

#include "Engine/Assets/Data/Material.hpp"

namespace std {
    template <>
    struct hash<glm::ivec3> {
        std::size_t operator()(const glm::ivec3& v) const noexcept {
            return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1) ^ (std::hash<int>()(v.z) << 2);
        }
    };

    template <>
    struct equal_to<glm::ivec3> {
        bool operator()(const glm::ivec3& lhs, const glm::ivec3& rhs) const noexcept {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }
    };
}

namespace Assets::CPU
{

enum class ECubeProcType : uint8_t
{
    ECPT_Clear,
    ECPT_Fence,
    ECPT_Voxelize,
};

enum class EBakerType : uint8_t
{
    EBT_Probe,
};

struct FCPUBLASVertInfo
{
    glm::vec3 normal;
    uint32_t matIdx;
};

struct FCPUTLASInstanceInfo
{
    std::array<uint32_t, 16> matIdxs;
    uint32_t nodeId;
    glm::vec3 worldBoundsMin{0.0f};
    glm::vec3 worldBoundsMax{0.0f};
    bool navRelevant = false;
    bool rayCastVisible = true;
};

struct FCPUBLASContext
{
    tinybvh::BVH bvh;
    std::vector<tinybvh::bvhvec4> triangles;
    std::vector<FCPUBLASVertInfo> extinfos;
};

struct FCPUBLASSet
{
    FCPUBLASSet() = default;
    FCPUBLASSet(const FCPUBLASSet&) = delete;
    FCPUBLASSet& operator=(const FCPUBLASSet&) = delete;
    FCPUBLASSet(FCPUBLASSet&&) = delete;
    FCPUBLASSet& operator=(FCPUBLASSet&&) = delete;

    uint64_t generation = 0;
    std::vector<FCPUBLASContext> contexts;
    std::vector<tinybvh::BVHBase*> list;
};

struct FCPUMaterialView
{
    Material::Enum materialModel = Material::Enum::Lambertian;
};

struct FCPUMaterialTable
{
    uint64_t generation = 0;
    std::vector<FCPUMaterialView> entries;
};

// A TLAS is built directly in its final heap allocation. tinybvh keeps pointers
// into instances and the BLAS list, so snapshots must never be copied or moved.
struct FCPUTLASSnapshot
{
    FCPUTLASSnapshot() = default;
    FCPUTLASSnapshot(const FCPUTLASSnapshot&) = delete;
    FCPUTLASSnapshot& operator=(const FCPUTLASSnapshot&) = delete;
    FCPUTLASSnapshot(FCPUTLASSnapshot&&) = delete;
    FCPUTLASSnapshot& operator=(FCPUTLASSnapshot&&) = delete;

    uint64_t sceneRevision = 0;
    std::shared_ptr<const FCPUBLASSet> blasSet;
    std::shared_ptr<const FCPUMaterialTable> materialTable;
    std::vector<tinybvh::BLASInstance> instances;
    std::vector<FCPUTLASInstanceInfo> contexts;
    tinybvh::BVH tlas;
};

struct FCPUTLASBuildInput
{
    uint64_t epoch = 0;
    uint64_t sceneRevision = 0;
    std::chrono::steady_clock::time_point requestTime;
    double captureMilliseconds = 0.0;
    std::shared_ptr<const FCPUBLASSet> blasSet;
    std::shared_ptr<const FCPUMaterialTable> materialTable;
    std::shared_ptr<const FCPUTLASSnapshot> previousSnapshot;
    std::vector<tinybvh::BLASInstance> instances;
    std::vector<FCPUTLASInstanceInfo> contexts;
};

struct FCPUTLASBuildResult
{
    uint64_t epoch = 0;
    std::shared_ptr<FCPUTLASSnapshot> snapshot;
    std::chrono::steady_clock::time_point requestTime;
    std::chrono::steady_clock::time_point buildStartTime;
    std::chrono::steady_clock::time_point buildEndTime;
    bool hasNavDirtyBounds = false;
    glm::vec3 navDirtyWorldMin{0.0f};
    glm::vec3 navDirtyWorldMax{0.0f};
};

struct FCPUTLASBuildStats
{
    uint64_t publishedRevision = 0;
    uint64_t latestRequestedRevision = 0;
    uint64_t coalescedRequestCount = 0;
    uint64_t completedBuildCount = 0;
    uint64_t snapshotStaleness = 0;
    double lastBuildMilliseconds = 0.0;
    double lastCaptureMilliseconds = 0.0;
    double lastBuildToPublishMilliseconds = 0.0;
    double buildToPublishP95Milliseconds = 0.0;
};

// A CPU baker owns an independent context and task-dispatch mechanism.
// Its lifetime and execution are controlled by CpuAS.
struct FCPUProbeBaker
{
    uint32_t cascadeIndex = 0;
    float UNIT_SIZE;
    glm::vec3 CUBE_OFFSET;
    
    std::vector<Assets::VoxelData> voxels;
    std::vector<uint8_t> distanceToSolidSeeds;

    void Init(uint32_t cascadeIdx, float unitSize, glm::vec3 offset);
    void ProcessCube(int x, int y, int z, ECubeProcType procType,
                     const std::shared_ptr<const FCPUTLASSnapshot>& snapshot);
    void RebuildDistanceField();
    void UploadGPU(Vulkan::DeviceMemory& voxelDeviceMemory);
    void UploadGPU(Vulkan::DeviceMemory& voxelDeviceMemory, size_t byteBaseOffset, uint32_t elementOffset);
    void UploadGPU(Vulkan::DeviceMemory& voxelDeviceMemory, uint32_t elementOffset);
    void ClearAmbientCubes();
};

struct FCPUPageIndex
{
    std::vector<Assets::PageIndex> pageIndex;

    void Init();
    void UpdateData(const std::vector<FCPUProbeBaker>& bakers);
    Assets::PageIndex& GetPage(glm::vec3 worldpos);
    void UploadGPU(Vulkan::DeviceMemory& deviceMemory);
    void UploadGPU(Vulkan::DeviceMemory& deviceMemory, size_t byteBaseOffset);
};

// Phase 3 sparse cube storage: per-cascade brick -> cube-pool slot table (GPU_SCENE_AMBIENT_BRICK_INVALID
// when a brick is unallocated). Rebuilt from the CPU voxel occupancy each flush: bricks within
// dilationRadius of a solid voxel get a compacted slot (capped at poolBricksPerCascade); far/empty
// bricks stay INVALID so the GPU fetch returns zero there and no cube storage is spent on them.
struct FCPUBrickTable
{
    std::vector<uint32_t> brickTable;
    std::vector<uint32_t> activeBrickList;
    std::vector<uint32_t> activeBricksPerCascade;
    std::vector<uint32_t> candidateBricksPerCascade;
    std::vector<uint32_t> recentlyHitBricksPerCascade;
    std::vector<uint32_t> candidateFirstSeenFrames;
    std::vector<uint32_t> slotsToClear;
    uint32_t activeBricksLastBuild = 0;

    bool UpdateData(const std::vector<FCPUProbeBaker>& bakers, uint32_t cascadeCapacity,
                    uint32_t poolBricksPerCascade, int dilationRadius,
                    const std::vector<Assets::AmbientBrickResidency>* residency,
                    uint32_t currentFrame, bool hitDriven, bool bounceHitAffectsResidency,
                    uint32_t graceFrames, uint32_t evictFrames);
    void UploadGPU(Vulkan::DeviceMemory& deviceMemory, size_t tableByteOffset, size_t activeListByteOffset);
};

class FCPUAccelerationStructure
{
public:
    using SnapshotPtr = std::shared_ptr<const FCPUTLASSnapshot>;

    void InitBVH(Assets::Scene& scene);
    void RebuildBVHOnly(Assets::Scene& scene);
    void PollBVHBuild();

    SnapshotPtr AcquireSnapshot() const;
    FCPUTLASBuildStats GetBuildStats() const;

    
    Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);
    
    bool AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* VoxelGPUMemory, bool Incremental = false);
    void AsyncProcessGroup(int xInMeter, int zInMeter, Assets::Scene& scene, ECubeProcType procType, EBakerType bakerType,
                           uint32_t cascadeIndex);
    
    bool Tick(Assets::Scene& scene, Vulkan::DeviceMemory* GPUMemory, Vulkan::DeviceMemory* FarGPUMemory, Vulkan::DeviceMemory* PageIndexMemory);
    bool HasPendingWork() const;

    void RequestUpdate(glm::vec3 worldPos, float radius);
    bool ConsumeNavRelevantDirtyBounds(glm::vec3& outWorldMin, glm::vec3& outWorldMax);
    void ClearNavRelevantDirtyBounds();

    
    void ClearAllTasks();

private:
    friend struct FCPUAccelerationStructureTestAccess;

    bool InitCascadeBakers(const Runtime::Config::UserSettings& settings, uint32_t maxCascadeCapacity);
    uint32_t GetActiveCascadeCount() const { return static_cast<uint32_t>(cascadeBakers.size()); }

    void UpdateBVH(Assets::Scene& scene);
    void PublishSnapshot(SnapshotPtr snapshot);
    std::shared_ptr<FCPUTLASBuildInput> CaptureBuildInput(Assets::Scene& scene);
    static std::shared_ptr<FCPUTLASBuildResult> BuildSnapshot(FCPUTLASBuildInput& input);
    uint64_t RequestRuntimeBuild(Assets::Scene& scene);
    uint64_t QueueBuildInput(std::shared_ptr<FCPUTLASBuildInput> input);
    void StartBuild(std::shared_ptr<FCPUTLASBuildInput> input);
    void CancelRuntimeBuilds();
    void MergeNavDirtyBounds(const FCPUTLASBuildResult& result);
    void QueueFullProbeBake();

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    std::atomic<SnapshotPtr> activeSnapshot_;
#else
    // Keep a mutex fallback for C++20 standard libraries that do not yet
    // provide the std::atomic<std::shared_ptr<T>> specialization.
    SnapshotPtr activeSnapshot_;
    mutable std::mutex snapshotMutex_;
#endif
    std::shared_ptr<const FCPUBLASSet> blasSet_;
    uint64_t blasGeneration_ = 0;
    uint64_t materialGeneration_ = 0;
    uint64_t sceneRevision_ = 0;
    std::atomic<uint64_t> buildEpoch_{1};
    std::atomic<uint64_t> publishedRevision_{0};
    std::atomic<uint64_t> latestRequestedRevision_{0};
    mutable std::mutex buildMutex_;
    bool buildInFlight_ = false;
    std::shared_ptr<FCPUTLASBuildInput> latestBuildInput_;
    std::shared_ptr<FCPUTLASBuildResult> completedBuildResult_;
    uint64_t coalescedRequestCount_ = 0;
    uint64_t completedBuildCount_ = 0;
    double lastBuildMilliseconds_ = 0.0;
    double lastCaptureMilliseconds_ = 0.0;
    double lastBuildToPublishMilliseconds_ = 0.0;
    std::vector<double> buildToPublishSamples_;
    uint64_t pendingProbeRevision_ = 0;
        
    std::vector<uint32_t> lastBatchTasks;
    std::vector<uint32_t> distanceFieldRebuildTasks;

    std::queue<std::tuple<glm::ivec3, ECubeProcType, EBakerType, uint32_t> > needUpdateGroups;

    std::vector<float> shadowMapR32;
    bool needFlush = false;
    bool distanceFieldRebuildScheduled_ = false;
    bool hasNavRelevantDirtyBounds_ = false;
    glm::vec3 navRelevantDirtyWorldMin_{0.0f};
    glm::vec3 navRelevantDirtyWorldMax_{0.0f};

    std::vector<FCPUProbeBaker> cascadeBakers;
    FCPUPageIndex cpuPageIndex;
    FCPUBrickTable cpuBrickTable;
};

}
