#include "DeviceMemory.hpp"
#include "Device.hpp"
#include "Utilities/Exception.hpp"

#ifdef VK_USE_PLATFORM_WIN32_KHR
#	include <aclapi.h>
#	include <dxgi1_2.h>
#endif

namespace Vulkan {
	DeviceMemory::DeviceMemory(const Vulkan::Device& device, size_t size, bool external, uint32_t memoryTypeBits, VkMemoryAllocateFlags allocateFLags, VkMemoryPropertyFlags propertyFlags):device_(device)
	{
		VkExportMemoryAllocateInfoKHR export_memory_allocate_info{};
		export_memory_allocate_info.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR;
		export_memory_allocate_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;

		_SECURITY_ATTRIBUTES            win_security_attributes;
		VkExportMemoryWin32HandleInfoKHR export_memory_win32_handle_info{};
		export_memory_win32_handle_info.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
		export_memory_win32_handle_info.pAttributes = &win_security_attributes;
		export_memory_win32_handle_info.dwAccess    = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
		export_memory_allocate_info.pNext           = &export_memory_win32_handle_info;
		
		VkMemoryAllocateInfo memory_allocate_info = {};
		memory_allocate_info.sType				  = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memory_allocate_info.pNext                = &export_memory_allocate_info;
		memory_allocate_info.allocationSize       = size;
		memory_allocate_info.memoryTypeIndex      = FindMemoryType(memoryTypeBits, propertyFlags);

		Check(vkAllocateMemory(device.Handle(), &memory_allocate_info, nullptr, &memory_), "allocate ext memory");
	}

DeviceMemory::DeviceMemory(
	const class Device& device, 
	const size_t size, 
	const uint32_t memoryTypeBits,
	const VkMemoryAllocateFlags allocateFLags,
	const VkMemoryPropertyFlags propertyFlags) :
	device_(device)
{
	VkMemoryAllocateFlagsInfo flagsInfo = {};
	flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flagsInfo.pNext = nullptr;
	flagsInfo.flags = allocateFLags;
	
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.pNext = &flagsInfo;
	allocInfo.allocationSize = size;
	allocInfo.memoryTypeIndex = FindMemoryType(memoryTypeBits, propertyFlags);

	Check(vkAllocateMemory(device.Handle(), &allocInfo, nullptr, &memory_),
		"allocate memory");
}

DeviceMemory::DeviceMemory(DeviceMemory&& other) noexcept :
	device_(other.device_),
	memory_(other.memory_)
{
	other.memory_ = nullptr;
}

DeviceMemory::~DeviceMemory()
{
	if (memory_ != nullptr)
	{
		vkFreeMemory(device_.Handle(), memory_, nullptr);
		memory_ = nullptr;
	}
}

void* DeviceMemory::Map(const size_t offset, const size_t size)
{
	void* data;
	Check(vkMapMemory(device_.Handle(), memory_, offset, size, 0, &data),
		"map memory");

	return data;
}

void DeviceMemory::Unmap()
{
	vkUnmapMemory(device_.Handle(), memory_);
}

uint32_t DeviceMemory::FindMemoryType(const uint32_t typeFilter, const VkMemoryPropertyFlags propertyFlags) const
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memProperties);

	for (uint32_t i = 0; i != memProperties.memoryTypeCount; ++i)
	{
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags)
		{
			return i;
		}
	}

	Throw(std::runtime_error("failed to find suitable memory type"));
}

}
