#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Vulkan/RayTracing/AccelerationStructure.hpp"
#include "Engine/Vulkan/RayTracing/BottomLevelGeometry.hpp"

namespace Vulkan::RayTracing
{

    class BottomLevelAccelerationStructure final : public AccelerationStructure
    {
    public:

        BottomLevelAccelerationStructure(const BottomLevelAccelerationStructure&) = delete;
        BottomLevelAccelerationStructure& operator = (const BottomLevelAccelerationStructure&) = delete;
        BottomLevelAccelerationStructure& operator = (BottomLevelAccelerationStructure&&) = delete;

        BottomLevelAccelerationStructure(
            const class DeviceProcedures& deviceProcedures, 
            const class RayTracingProperties& rayTracingProperties, 
            const BottomLevelGeometry& geometries);
        BottomLevelAccelerationStructure(BottomLevelAccelerationStructure&& other) noexcept;
        ~BottomLevelAccelerationStructure();

        void Generate(
            VkCommandBuffer commandBuffer,
            Buffer& scratchBuffer,
            VkDeviceSize scratchOffset,
            Buffer& resultBuffer,
            VkDeviceSize resultOffset);

        void Update(
            VkCommandBuffer commandBuffer,
            Buffer& scratchBuffer,
            VkDeviceSize scratchOffset);

    private:

        BottomLevelGeometry geometries_;
    };

}
