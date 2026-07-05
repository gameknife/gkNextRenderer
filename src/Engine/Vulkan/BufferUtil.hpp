#pragma once

#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include <cstring>
#include <memory>
#include <string>
#include <vector>


namespace Vulkan
{
	class BufferUtil final
	{
	public:

		template <class T>
		static void CopyFromStagingBuffer(CommandPool& commandPool, Buffer& dstBuffer, const std::vector<T>& content);

		template <class T>
		static void CopyToStagingBuffer(CommandPool& commandPool, Buffer& srcBuffer, std::vector<T>& content);

		template <class T>
		static void CreateDeviceBuffer(
			CommandPool& commandPool,
			const char* name,
			VkBufferUsageFlags usage,
			const std::vector<T>& content,
			std::unique_ptr<Buffer>& buffer,
			std::unique_ptr<DeviceMemory>& memory);
		
		static void CreateDeviceBufferLocal(
			CommandPool& commandPool,
			const char* const name,
			const VkBufferUsageFlags usage,
			const VkMemoryPropertyFlags memProp,
			const size_t size,
			std::unique_ptr<Buffer>& buffer,
			std::unique_ptr<DeviceMemory>& memory);

	private:
		static void CreateRaw(
			CommandPool& commandPool,
			const char* name,
			VkBufferUsageFlags usage,
			VkMemoryPropertyFlags memProp,
			size_t size,
			std::unique_ptr<Buffer>& buffer,
			std::unique_ptr<DeviceMemory>& memory);
	};

	inline void BufferUtil::CreateRaw(
		CommandPool& commandPool,
		const char* const name,
		const VkBufferUsageFlags usage,
		const VkMemoryPropertyFlags memProp,
		const size_t size,
		std::unique_ptr<Buffer>& buffer,
		std::unique_ptr<DeviceMemory>& memory)
	{
		const auto& device = commandPool.Device();
		const VkMemoryAllocateFlags allocateFlags = usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
			? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
			: 0;

		buffer.reset(new Buffer(
			device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | usage));
		memory.reset(new DeviceMemory(buffer->AllocateMemory(
			memProp,
			{
				.AllocateFlags = allocateFlags
			})));

		device.DebugUtils().SetObjectName(buffer->Handle(), (name + std::string(" Buffer")).c_str());
		memory->SetName((name + std::string(" Memory")).c_str());
	}

	template <class T>
	void BufferUtil::CopyFromStagingBuffer(CommandPool& commandPool, Buffer& dstBuffer, const std::vector<T>& content)
	{
		const auto& device = commandPool.Device();
		const auto contentSize = sizeof(content[0]) * content.size();
		
		// Create a temporary host-visible staging buffer.
		auto stagingBuffer = std::make_unique<Buffer>(device, contentSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		auto stagingBufferMemory = stagingBuffer->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		device.DebugUtils().SetObjectName(stagingBuffer->Handle(), "BufferUpload Staging Buffer");
		stagingBufferMemory.SetName("BufferUpload Staging Memory");

		// Copy the host data into the staging buffer.
		const auto data = stagingBufferMemory.Map(0, contentSize);
		std::memcpy(data, content.data(), contentSize);
		stagingBufferMemory.Unmap();

		// Copy the staging buffer to the device buffer.
		dstBuffer.CopyFrom(commandPool, *stagingBuffer, contentSize);

		// Delete the buffer before the memory
		stagingBuffer.reset();
	}

	template <class T>
	void BufferUtil::CopyToStagingBuffer(CommandPool& commandPool, Buffer& srcBuffer, std::vector<T>& content)
	{
		const auto& device = commandPool.Device();
		const auto contentSize = sizeof(content[0]) * content.size();
		
		// Create a temporary host-visible staging buffer.
		auto stagingBuffer = std::make_unique<Buffer>(device, contentSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
		auto stagingBufferMemory = stagingBuffer->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		device.DebugUtils().SetObjectName(stagingBuffer->Handle(), "BufferReadback Staging Buffer");
		stagingBufferMemory.SetName("BufferReadback Staging Memory");

		// Copy the staging buffer to the device buffer.
		srcBuffer.CopyTo(commandPool, *stagingBuffer, contentSize);
		
		// Copy the host data into the staging buffer.
		void* data = stagingBufferMemory.Map(0, contentSize);
		std::memcpy(content.data(), data, contentSize);
		stagingBufferMemory.Unmap();

		// Delete the buffer before the memory
		stagingBuffer.reset();
	}

	template <class T>
	void BufferUtil::CreateDeviceBuffer(
		CommandPool& commandPool,
		const char* const name,
		const VkBufferUsageFlags usage, 
		const std::vector<T>& content,
		std::unique_ptr<Buffer>& buffer,
		std::unique_ptr<DeviceMemory>& memory)
	{
		const auto contentSize = sizeof(T) * (content.size() == 0 ? 1 : content.size()); // judge if contentSize == 0
		CreateRaw(
			commandPool, name, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, contentSize, buffer, memory);

		if(content.size() > 0)
		{
			CopyFromStagingBuffer(commandPool, *buffer, content);
		}
	}
	
	inline void BufferUtil::CreateDeviceBufferLocal(
	CommandPool& commandPool,
	const char* const name,
	const VkBufferUsageFlags usage,
	const VkMemoryPropertyFlags memProp,
	const size_t size,
	std::unique_ptr<Buffer>& buffer,
	std::unique_ptr<DeviceMemory>& memory)
	{
		CreateRaw(commandPool, name, usage, memProp, size, buffer, memory);
	}

	
        
}
