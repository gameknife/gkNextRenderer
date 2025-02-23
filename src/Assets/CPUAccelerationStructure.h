#pragma once
#include "Common/CoreMinimal.hpp"
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
    tinybvh::BVH4_CPU bvh;
    std::vector<tinybvh::bvhvec4> triangles;
    std::vector<FCPUBLASVertInfo> extinfos;
};

class FCPUAccelerationStructure
{
public:
    void InitBVH(Assets::Scene& scene);

    Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);
    
    void StartAmbientCubeGenerateTasks( glm::ivec3 start, glm::ivec3 end, Vulkan::DeviceMemory* GPUMemory );
    
    void Tick();

private:
    std::vector<FCPUBLASContext> bvhBLASContexts;
    std::vector<tinybvh::BLASInstance> bvhInstanceList;
    std::vector<tinybvh::BVHBase*> bvhBLASList;
};
