#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/UniformBuffer.hpp"
#include <glm/glm.hpp>
#include "ThirdParty/tinybvh/tiny_bvh.h"
#include <functional>
#include <queue>

#include "Material.hpp"

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
    float UNIT_SIZE;
    glm::vec3 CUBE_OFFSET;
    
    std::vector<Assets::VoxelData> voxels;

    void Init( float unit_size, glm::vec3 offset );
    void ProcessCube(int x, int y, int z, ECubeProcType procType);
    void UploadGPU(Vulkan::DeviceMemory& voxelDeviceMemory);
    void ClearAmbientCubes();
};

struct FCPUPageIndex
{
    std::vector<Assets::PageIndex> pageIndex;

    void Init();
    void UpdateData(FCPUProbeBaker& baker);
    Assets::PageIndex& GetPage(glm::vec3 worldpos);
    void UploadGPU(Vulkan::DeviceMemory& deviceMemory);
};

class FCPUAccelerationStructure
{
public:
    void InitBVH(Assets::Scene& scene);

    void UpdateBVH(Assets::Scene& scene);

    Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);
    
    void AsyncProcessFull(Assets::Scene& scene, Vulkan::DeviceMemory* VoxelGPUMemory, bool Incremental = false);
    void AsyncProcessGroup(int xInMeter, int zInMeter, Assets::Scene& scene, ECubeProcType procType, EBakerType bakerType);
    
    void Tick(Assets::Scene& scene, Vulkan::DeviceMemory* GPUMemory, Vulkan::DeviceMemory* FarGPUMemory, Vulkan::DeviceMemory* PageIndexMemory);

    void RequestUpdate(glm::vec3 worldPos, float radius);

    void GenShadowMap(Assets::Scene& scene);

private:
    std::vector<FCPUBLASContext> bvhBLASContexts;
    std::vector<tinybvh::BLASInstance> bvhInstanceList;
    std::vector<FCPUTLASInstanceInfo> bvhTLASContexts;
    std::vector<tinybvh::BVHBase*> bvhBLASList;
        
    std::vector<uint32_t> lastBatchTasks;

    std::queue<std::tuple<glm::ivec3, ECubeProcType, EBakerType> > needUpdateGroups;

    std::vector<float> shadowMapR32;
    bool needFlush = false;

    FCPUProbeBaker probeBaker;
    FCPUPageIndex cpuPageIndex;
};
