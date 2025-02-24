#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/UniformBuffer.hpp"
#include <glm/glm.hpp>
#include "ThirdParty/tinybvh/tiny_bvh.h"

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
    
    void Tick(Vulkan::DeviceMemory* GPUMemory);

private:
    std::vector<FCPUBLASContext> bvhBLASContexts;
    std::vector<tinybvh::BLASInstance> bvhInstanceList;
    std::vector<tinybvh::BVHBase*> bvhBLASList;

    
    std::vector<Assets::AmbientCube> ambientCubes;
    std::vector<Assets::AmbientCube> ambientCubesCopy;

    bool needFlush = false;
};
