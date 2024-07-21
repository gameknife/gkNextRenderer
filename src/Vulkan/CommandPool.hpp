#pragma once

#include "Vulkan.hpp"

namespace Vulkan
{
	class Device;

	class CommandPool final
	{
	public:

		VULKAN_NON_COPIABLE(CommandPool)

		CommandPool(const Device& device, uint32_t queueFamilyIndex, uint32_t queue, bool allowReset);
		~CommandPool();

		const class Device& Device() const { return device_; }
		VkQueue Queue() const { return queue_; }

	private:

		const class Device& device_;

		VULKAN_HANDLE(VkCommandPool, commandPool_)

		VkQueue queue_;
	};

}
