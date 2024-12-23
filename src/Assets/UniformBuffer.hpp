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

	#include "../assets/shaders/common/UniformBufferObject.glsl"
	// struct UniformBufferObject
	// struct NodeProxy
		
	struct alignas(16) LightObject final
	{
		glm::vec4 p0;
		glm::vec4 p1;
		glm::vec4 p3;
		glm::vec4 normal_area;
	};

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

	struct RayCastContext
	{
		vec4 Origin;
		vec4 Direction;
		float TMin;
		float TMax;
		float Reversed0;
		float Reversed1;
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

	struct RayCastIO
	{
		RayCastContext Context;
		RayCastResult Result;
	};

	struct RayCastRequest
	{
		RayCastContext context;
		std::function<bool(RayCastResult)> callback;
	};

	class RayCastBuffer
	{
	public:

		RayCastBuffer(const RayCastBuffer&) = delete;
		RayCastBuffer& operator = (const RayCastBuffer&) = delete;
		RayCastBuffer& operator = (RayCastBuffer&&) = delete;

		explicit RayCastBuffer(Vulkan::CommandPool& commandPool);
		~RayCastBuffer();

		const Vulkan::Buffer& Buffer() const { return *buffer_; }
		
		void SyncWithGPU();

		std::vector<RayCastIO> rayCastIO;
		std::vector<RayCastIO> rayCastIOTemp;
	private:
		
		std::unique_ptr<Vulkan::Buffer> buffer_;
		std::unique_ptr<Vulkan::DeviceMemory> memory_;
	};

}