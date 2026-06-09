#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <functional>

namespace Vulkan
{
	class DeviceProcedures final
	{
	public:

		VULKAN_NON_COPIABLE(DeviceProcedures)

		explicit DeviceProcedures(const Device& device, bool raytracing, bool rayquery);
		~DeviceProcedures();

		const class Device& Device() const { return device_; }
		
		const std::function<VkResult(
			VkDevice device,
			const VkAccelerationStructureCreateInfoKHR* pCreateInfo,
			const VkAllocationCallbacks* pAllocator,
			VkAccelerationStructureKHR* pAccelerationStructure)>
		vkCreateAccelerationStructureKHR;

		const std::function<void(
			VkDevice device,
			VkAccelerationStructureKHR accelerationStructure,
			const VkAllocationCallbacks* pAllocator)>
		vkDestroyAccelerationStructureKHR;

		const std::function<void(
			VkDevice device,
			VkAccelerationStructureBuildTypeKHR buildType,
			const VkAccelerationStructureBuildGeometryInfoKHR* pBuildInfo,
			const uint32_t* pMaxPrimitiveCounts,
			VkAccelerationStructureBuildSizesInfoKHR* pSizeInfo)>
		vkGetAccelerationStructureBuildSizesKHR;

		const std::function<void(
			VkCommandBuffer commandBuffer,
			uint32_t infoCount,
			const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
			const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos)>
		vkCmdBuildAccelerationStructuresKHR;

		const std::function<void(
			VkCommandBuffer commandBuffer,
			const VkCopyAccelerationStructureInfoKHR* pInfo)>
		vkCmdCopyAccelerationStructureKHR;

		const std::function<void(
			VkCommandBuffer commandBuffer,
			const VkStridedDeviceAddressRegionKHR* pRaygenShaderBindingTable, 
			const VkStridedDeviceAddressRegionKHR* pMissShaderBindingTable,
			const VkStridedDeviceAddressRegionKHR* pHitShaderBindingTable,
			const VkStridedDeviceAddressRegionKHR* pCallableShaderBindingTable,
			uint32_t width, 
			uint32_t height, 
			uint32_t depth)>
		vkCmdTraceRaysKHR;

		const std::function<VkResult(
			VkDevice device,
			VkDeferredOperationKHR deferredOperation,
			VkPipelineCache pipelineCache,
			uint32_t createInfoCount,
			const VkRayTracingPipelineCreateInfoKHR* pCreateInfos,
			const VkAllocationCallbacks* pAllocator,
			VkPipeline* pPipelines)>
		vkCreateRayTracingPipelinesKHR;

		const std::function<VkResult(
			VkDevice device,
			VkPipeline pipeline,
			uint32_t firstGroup,
			uint32_t groupCount,
			size_t dataSize,
			void* pData)>
		vkGetRayTracingShaderGroupHandlesKHR;

		const std::function<VkDeviceAddress(
			VkDevice device, 
			const VkAccelerationStructureDeviceAddressInfoKHR* pInfo)>
		vkGetAccelerationStructureDeviceAddressKHR;

		const std::function<void(
			VkCommandBuffer commandBuffer,
			uint32_t accelerationStructureCount,
			const VkAccelerationStructureKHR* pAccelerationStructures,
			VkQueryType queryType,
			VkQueryPool queryPool,
			uint32_t firstQuery)>
		vkCmdWriteAccelerationStructuresPropertiesKHR;
#if WIN32
		const std::function<VkResult(
			VkDevice device,
			const VkMemoryGetWin32HandleInfoKHR* pGetWin32HandleInfo,
			HANDLE* pHandle)>
		vkGetMemoryWin32HandleKHR;
#else
		const std::function<VkResult(
			VkDevice device,
			const VkMemoryGetFdInfoKHR* pGetFdInfo,
			int* pFd)>
		vkGetMemoryFdKHR;
#endif

		// VK_KHR_video_queue / VK_KHR_video_encode_queue (null when the extensions are not enabled).
		const PFN_vkCreateVideoSessionKHR vkCreateVideoSessionKHR;
		const PFN_vkDestroyVideoSessionKHR vkDestroyVideoSessionKHR;
		const PFN_vkGetVideoSessionMemoryRequirementsKHR vkGetVideoSessionMemoryRequirementsKHR;
		const PFN_vkBindVideoSessionMemoryKHR vkBindVideoSessionMemoryKHR;
		const PFN_vkCreateVideoSessionParametersKHR vkCreateVideoSessionParametersKHR;
		const PFN_vkDestroyVideoSessionParametersKHR vkDestroyVideoSessionParametersKHR;
		const PFN_vkGetEncodedVideoSessionParametersKHR vkGetEncodedVideoSessionParametersKHR;
		const PFN_vkCmdBeginVideoCodingKHR vkCmdBeginVideoCodingKHR;
		const PFN_vkCmdEndVideoCodingKHR vkCmdEndVideoCodingKHR;
		const PFN_vkCmdControlVideoCodingKHR vkCmdControlVideoCodingKHR;
		const PFN_vkCmdEncodeVideoKHR vkCmdEncodeVideoKHR;

	private:

		const class Device& device_;
	};

}
