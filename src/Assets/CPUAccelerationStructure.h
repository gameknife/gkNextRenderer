#pragma once
#include <glm/glm.hpp>

namespace Assets
{
    class Scene;
    struct RayCastResult;
}

namespace Vulkan
{
    class DeviceMemory;
}

class FCPUAccelerationStructure
{
public:
    void InitBVH(Assets::Scene& scene);

    Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);
    
    void StartAmbientCubeGenerateTasks( glm::ivec3 start, glm::ivec3 end, Vulkan::DeviceMemory* GPUMemory );
    
    void Tick();
};
