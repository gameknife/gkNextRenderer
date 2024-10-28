#pragma once
#include "Common/CoreMinimal.hpp"
#include "AccelerationStructure.hpp"
#include "Utilities/Glm.hpp"

namespace Vulkan::RayTracing
{
	class BottomLevelAccelerationStructure;

	class TopLevelAccelerationStructure final : public AccelerationStructure
	{
	public:

		TopLevelAccelerationStructure(const TopLevelAccelerationStructure&) = delete;
		TopLevelAccelerationStructure& operator = (const TopLevelAccelerationStructure&) = delete;
		TopLevelAccelerationStructure& operator = (TopLevelAccelerationStructure&&) = delete;

		TopLevelAccelerationStructure(
			const class DeviceProcedures& deviceProcedures,
			const class RayTracingProperties& rayTracingProperties,
			VkDeviceAddress instanceAddress, 
			uint32_t instancesCount);
		TopLevelAccelerationStructure(TopLevelAccelerationStructure&& other) noexcept;
		virtual ~TopLevelAccelerationStructure();

		void Generate(
			VkCommandBuffer commandBuffer,
			Buffer& scratchBuffer,
			VkDeviceSize scratchOffset,
			Buffer& resultBuffer,
			VkDeviceSize resultOffset);

		void Update(
			VkCommandBuffer commandBuffer,
			uint32_t instanceCount);

		static VkAccelerationStructureInstanceKHR CreateInstance(
			const BottomLevelAccelerationStructure& bottomLevelAs,
			const glm::mat4& transform,
			uint32_t instanceId,
			bool visible);

	private:

		uint32_t instancesCount_;
		VkAccelerationStructureGeometryInstancesDataKHR instancesVk_{};
		VkAccelerationStructureGeometryKHR topASGeometry_{};
	};

}
