#include "UniformBuffer.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandPool.hpp"
#include <cstring>

#include "Vulkan/BufferUtil.hpp"

namespace Assets {

UniformBuffer::UniformBuffer(const Vulkan::Device& device)
{
	const auto bufferSize = sizeof(UniformBufferObject);

	buffer_.reset(new Vulkan::Buffer(device, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
	memory_.reset(new Vulkan::DeviceMemory(buffer_->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept :
	buffer_(other.buffer_.release()),
	memory_(other.memory_.release())
{
}

UniformBuffer::~UniformBuffer()
{
	buffer_.reset();
	memory_.reset(); // release memory after bound buffer has been destroyed
}

void UniformBuffer::SetValue(const UniformBufferObject& ubo)
{
	const auto data = memory_->Map(0, sizeof(UniformBufferObject));
	std::memcpy(data, &ubo, sizeof(ubo));
	memory_->Unmap();
}
constexpr int32_t raycastMax = 1000;
RayCastBuffer::RayCastBuffer(Vulkan::CommandPool& commandPool)
{
	const auto bufferSize = sizeof(RayCastIO) * raycastMax;

	Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "RayCastIO", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, bufferSize, buffer_, memory_);

	rayCastIO.resize(raycastMax);
	rayCastIOTemp.resize(raycastMax);
}
	
RayCastBuffer::~RayCastBuffer()
{
	buffer_.reset();
	memory_.reset(); // release memory after bound buffer has been destroyed
}

void RayCastBuffer::SyncWithGPU()
{
	const auto data = memory_->Map(0, sizeof(RayCastIO) * raycastMax);
	// download
	RayCastIO* gpuData = static_cast<RayCastIO*>(data);
	std::memcpy(rayCastIOTemp.data(), rayCastIO.data(), sizeof(RayCastIO) * raycastMax);
	std::memcpy(rayCastIO.data(), gpuData, sizeof(RayCastIO) * raycastMax);
	// upload
	std::memcpy(data, rayCastIOTemp.data(), sizeof(RayCastIO) * raycastMax);
	memory_->Unmap();
}
}
