#pragma once
#include "Common/CoreMinimal.hpp"
#include "Utilities/Glm.hpp"
#include <memory>

namespace Vulkan
{
	class Buffer;
	class Device;
	class DeviceMemory;
	class CommandPool;
}

namespace Assets
{
	using namespace glm;


	const int ACGI_PAGE_COUNT = 64; // 64x64
	const float ACGI_PAGE_SIZE = 16; // 16m
	const vec3 ACGI_PAGE_OFFSET = vec3( -512, 0, -512);
	const int SHADOWMAP_SIZE = 4096;
	const int CUBE_SIZE_XY = 192;//256;
	const int CUBE_SIZE_Z = 48;
	const float CUBE_UNIT = 0.25f;
	const vec3 CUBE_OFFSET = vec3(-CUBE_SIZE_XY / 2, -1.375f, -CUBE_SIZE_XY / 2) * CUBE_UNIT;

#define float3 vec3
#define float4 vec4
#define float4x4 mat4
#define uint8_t4_packed uint
#define half4 glm::detail::hdata
#define public
#define bool uint32_t
	
	#include "../assets/shaders/common/BasicTypes.slang"
	#include "../assets/shaders/common/BindlessTexture.slang"

#undef public
#undef bool
#undef half4
#undef float3
#undef float4
#undef float4x4
#undef uint8_t4_packed
	class UniformBuffer
	{
	public:

		UniformBuffer(const UniformBuffer&) = delete;
		UniformBuffer& operator = (const UniformBuffer&) = delete;
		UniformBuffer& operator = (UniformBuffer&&) = delete;

		explicit UniformBuffer(const Vulkan::Device& device);
		UniformBuffer(UniformBuffer&& other) noexcept;
		~UniformBuffer();

		const Vulkan::Buffer& Buffer() const { return *buffer_; }

		void SetValue(const UniformBufferObject& ubo);

	private:

		std::unique_ptr<Vulkan::Buffer> buffer_;
		std::unique_ptr<Vulkan::DeviceMemory> memory_;
	};

	struct RayCastResult
	{
		vec4 HitPoint;
		vec4 Normal;
		float T;
		uint32_t InstanceId;
		uint32_t MaterialId;
		uint32_t Hitted;
	};

}