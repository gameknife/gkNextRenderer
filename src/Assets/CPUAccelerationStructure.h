#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/UniformBuffer.hpp"
#include <glm/glm.hpp>
#include "ThirdParty/tinybvh/tiny_bvh.h"
#include <functional>

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

struct FCPUBLASVertInfo
{
    glm::vec3 normal;
    uint32_t instanceId;
};

struct FCPUBLASContext
{
    tinybvh::BVH bvh;
    std::vector<tinybvh::bvhvec4> triangles;
    std::vector<FCPUBLASVertInfo> extinfos;
};

class FCPUAccelerationStructure
{
public:
    void InitBVH(Assets::Scene& scene);

    void UpdateBVH(Assets::Scene& scene);

    Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);

    void ProcessCube(int x, int y, int z);
    void StartAmbientCubeGenerateTasks();
    void AsyncProcessGroup(int xInMeter, int zInMeter);
    void AsyncProcessGroupInWorld(glm::vec3 worldPos, float radius);
    
    void Tick(Assets::Scene& scene, Vulkan::DeviceMemory* GPUMemory);

    void RequestUpdate(glm::vec3 worldPos, float radius);

private:
    std::vector<FCPUBLASContext> bvhBLASContexts;
    std::vector<tinybvh::BLASInstance> bvhInstanceList;
    std::vector<tinybvh::BVHBase*> bvhBLASList;

    
    std::vector<Assets::AmbientCube> ambientCubes;
    std::vector<Assets::AmbientCube> ambientCubesCopy;
    
    std::vector<uint32_t> lastBatchTasks;

    std::unordered_set<glm::ivec3>  needUpdateGroups;

    bool needFlush = false;
};
