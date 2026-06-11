#pragma once

#include "Engine/Vulkan/Allocator.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <string>
#include <vector>

namespace Vulkan
{
	// ============================================================================
	// DeviceMemory
	// ============================================================================

	class DeviceMemory final
	{
	public:

		struct BufferAllocationOptions final
		{
			VkMemoryAllocateFlags AllocateFlags = 0;
			bool Dedicated = false;
			bool Passthrough = false;
		};

		DeviceMemory(const DeviceMemory&) = delete;
		DeviceMemory& operator = (const DeviceMemory&) = delete;
		DeviceMemory& operator = (DeviceMemory&&) = delete;

		DeviceMemory(
			const Device& device,
			const Buffer& buffer,
			VkMemoryPropertyFlags propertyFlags);
		DeviceMemory(
			const Device& device,
			const Buffer& buffer,
			VkMemoryPropertyFlags propertyFlags,
			const BufferAllocationOptions& options);
		DeviceMemory(const Device& device, const Image& image, VkMemoryPropertyFlags propertyFlags, bool external = false, bool dedicated = false);
		DeviceMemory(DeviceMemory&& other) noexcept;
		~DeviceMemory();

		const class Device& Device() const { return device_; }

		void* Map(size_t offset, size_t size);
		void Unmap();
		void SetName(const char* name);

	private:

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

		const class Device& device_;
		VmaAllocationHandle allocation_ = nullptr;

		VULKAN_HANDLE(VkDeviceMemory, memory_)
	};

	// ============================================================================
	// Sampler
	// ============================================================================

	struct SamplerConfig final
	{
		VkFilter MagFilter = VK_FILTER_LINEAR;
		VkFilter MinFilter = VK_FILTER_LINEAR;
		VkSamplerAddressMode AddressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSamplerAddressMode AddressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		VkSamplerAddressMode AddressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		bool AnisotropyEnable = true;
		float MaxAnisotropy = 16;
		VkBorderColor BorderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		bool UnnormalizedCoordinates = false;
		bool CompareEnable = false;
		VkCompareOp CompareOp = VK_COMPARE_OP_ALWAYS;
		VkSamplerMipmapMode MipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		float MipLodBias = 0.0f;
		float MinLod = 0.0f;
		float MaxLod = 0.0f;
	};

	class Sampler final
	{
	public:

		VULKAN_NON_COPIABLE(Sampler)

		Sampler(const Device& device, const SamplerConfig& config);
		~Sampler();

		const class Device& Device() const { return device_; }

	private:

		const class Device& device_;

		VULKAN_HANDLE(VkSampler, sampler_)
	};

	// ============================================================================
	// ShaderModule
	// ============================================================================

	class ShaderModule final
	{
	public:

		VULKAN_NON_COPIABLE(ShaderModule)

		ShaderModule(const Device& device, const std::string& filename);
		ShaderModule(const Device& device, const std::vector<uint8_t>& code);
		~ShaderModule();

		const class Device& Device() const { return device_; }

		VkPipelineShaderStageCreateInfo CreateShaderStage(VkShaderStageFlagBits stage) const;

	private:

		static std::vector<uint8_t> ReadFile(const std::string& filename);

		const class Device& device_;

		VULKAN_HANDLE(VkShaderModule, shaderModule_)
	};

}
