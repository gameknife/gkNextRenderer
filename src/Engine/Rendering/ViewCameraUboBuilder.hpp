#pragma once

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <vulkan/vulkan.h>

namespace Assets
{
    class Scene;
}

namespace Vulkan
{
    struct FViewCameraUboRequest
    {
        Assets::Scene& scene;
        Assets::Camera camera;
        VkExtent2D extent{1, 1};
        const Assets::UniformBufferObject* baseUbo = nullptr;
        float cascadeDistance = 400.0f;
        uint32_t totalFrames = 1;
        bool fillSceneLighting = true;
        bool thumbnailDefaults = false;
    };

    Assets::UniformBufferObject BuildViewCameraUbo(const FViewCameraUboRequest& request);

    Assets::Camera BuildOrbitedCamera(
        const Assets::UniformBufferObject& baseUbo,
        const Assets::Camera& camera,
        float yawDegrees,
        float pivotDistance);
}
