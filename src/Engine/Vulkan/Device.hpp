#pragma once

#include "Allocator.hpp"
#include <memory>
#include <vector>

#include "DebugUtilities.hpp"

namespace Vulkan
{
	class Surface;
	class DeviceProcedures;

	class Device final
	{
	public:

		VULKAN_NON_COPIABLE(Device)

		Device(
			VkPhysicalDevice physicalDevice, 
			const Surface& surface, 
			const std::vector<const char*>& requiredExtensionsconst,
			const VkPhysicalDeviceFeatures& deviceFeatures,
			const void* nextDeviceFeatures);
		
		~Device();

		VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
		const class Surface& Surface() const { return surface_; }

		const class DebugUtils& DebugUtils() const { return debugUtils_; }

		uint32_t GraphicsFamilyIndex() const { return graphicsFamilyIndex_; }
		uint32_t ComputeFamilyIndex() const { return computeFamilyIndex_; }
		uint32_t PresentFamilyIndex() const { return presentFamilyIndex_; }
		int32_t TransferFamilyIndex() const { return transferFamilyIndex_; }
		
		VkQueue GraphicsQueue() const { return graphicsQueue_; }
		VkQueue ComputeQueue() const { return computeQueue_; }
		VkQueue PresentQueue() const { return presentQueue_; }
		VkQueue TransferQueue() const { return transferQueue_; }

		VkPhysicalDeviceProperties DeviceProperties() const { return deviceProp_; }
		MemoryStatsSnapshot CaptureMemoryStats(bool includeDetails = false) const;

		void WaitIdle() const;

		const DeviceProcedures& GetDeviceProcedures() const { return *deviceProcedures_; }
		const MemoryAllocator& GetMemoryAllocator() const { return *memoryAllocator_; }

	private:

		void CheckRequiredExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions) const;

		const VkPhysicalDevice physicalDevice_;
		const class Surface& surface_;

		VULKAN_HANDLE(VkDevice, device_)

		class DebugUtils debugUtils_;

		uint32_t graphicsFamilyIndex_ {};
		uint32_t computeFamilyIndex_{};
		uint32_t presentFamilyIndex_{};
		uint32_t transferFamilyIndex_{};

		VkQueue graphicsQueue_{};
		VkQueue computeQueue_{};
		VkQueue presentQueue_{};
		VkQueue transferQueue_{};
				
		std::unique_ptr<DeviceProcedures> deviceProcedures_;
		std::unique_ptr<MemoryAllocator> memoryAllocator_;
		VkPhysicalDeviceProperties deviceProp_;
	};

}
