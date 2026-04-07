#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/GPU/UniformBuffer.hpp"
#include <glm/glm.hpp>
#include "ThirdParty/tinybvh/tiny_bvh.h"
#include <functional>
#include <queue>

#include "Assets/Data/Material.hpp"

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

namespace Assets
{
    class Scene;
    struct RayCastResult;
}

struct UserSettings;

namespace Vulkan
{
    class DeviceMemory;
}

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
};

struct FCPUBLASContext
{
    tinybvh::BVH bvh;
    std::vector<tinybvh::bvhvec4> triangles;
    std::vector<FCPUBLASVertInfo> extinfos;
};

// 抽象一个CPUBaker，拥有独立的上下文和独立的Task发起机制
// 由CpuAS来控制
struct FCPUProbeBaker
{
    uint32_t cascadeIndex = 0;
    float UNIT_SIZE;
    glm::vec3 CUBE_OFFSET;
    
    std::vector<Assets::VoxelData> voxels;

    void Init(uint32_t cascadeIdx, float unitSize, glm::vec3 offset);
    void ProcessCube(int x, int y, int z, ECubeProcType procType);
    void UploadGPU(Vulkan::DeviceMemory& voxelDeviceMemory);
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
};

class FCPUAccelerationStructure
{
public:
    void InitBVH(Assets::Scene& scene);

    
    Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);
    
    bool AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* VoxelGPUMemory, bool Incremental = false);
    void AsyncProcessGroup(int xInMeter, int zInMeter, Assets::Scene& scene, ECubeProcType procType, EBakerType bakerType,
                           uint32_t cascadeIndex);
    
    void Tick(Assets::Scene& scene, Vulkan::DeviceMemory* GPUMemory, Vulkan::DeviceMemory* FarGPUMemory, Vulkan::DeviceMemory* PageIndexMemory);

    void RequestUpdate(glm::vec3 worldPos, float radius);
    bool ConsumeNavRelevantDirtyBounds(glm::vec3& outWorldMin, glm::vec3& outWorldMax);
    void ClearNavRelevantDirtyBounds();

    void GenShadowMap(Assets::Scene& scene);
    
    void ClearAllTasks();

private:
    bool InitCascadeBakers(const UserSettings& settings);
    uint32_t GetActiveCascadeCount() const { return static_cast<uint32_t>(cascadeBakers.size()); }

    void UpdateBVH(Assets::Scene& scene);
    
    std::vector<FCPUBLASContext> bvhBLASContexts;
    std::vector<tinybvh::BLASInstance> bvhInstanceList;
    std::vector<FCPUTLASInstanceInfo> bvhTLASContexts;
    std::vector<tinybvh::BVHBase*> bvhBLASList;
        
    std::vector<uint32_t> lastBatchTasks;

    std::queue<std::tuple<glm::ivec3, ECubeProcType, EBakerType, uint32_t> > needUpdateGroups;

    std::vector<float> shadowMapR32;
    bool needFlush = false;
    bool hasNavRelevantDirtyBounds_ = false;
    glm::vec3 navRelevantDirtyWorldMin_{0.0f};
    glm::vec3 navRelevantDirtyWorldMax_{0.0f};

    std::vector<FCPUProbeBaker> cascadeBakers;
    FCPUPageIndex cpuPageIndex;
};
