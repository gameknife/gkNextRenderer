#pragma once

#include "Utilities/Glm.hpp"
#include <memory>

namespace Vulkan
{
	class Buffer;
	class Device;
	class DeviceMemory;
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
		glm::vec4 Origin;
		glm::vec4 Direction;
		float TMin;
		float TMax;
		float Reversed0;
		float Reversed1;
	};

	struct RayCastResult
	{
		glm::vec4 HitPoint;
		glm::vec4 Normal;
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

	class RayCastBuffer
	{
	public:

		RayCastBuffer(const RayCastBuffer&) = delete;
		RayCastBuffer& operator = (const RayCastBuffer&) = delete;
		RayCastBuffer& operator = (RayCastBuffer&&) = delete;

		explicit RayCastBuffer(const Vulkan::Device& device);
		RayCastBuffer(RayCastBuffer&& other) noexcept;
		~RayCastBuffer();

		const Vulkan::Buffer& Buffer() const { return *buffer_; }

		RayCastResult GetResult() {return  rayCastIO.Result;}
		void SetContext(RayCastContext& context) {rayCastIO.Context = context;}

		void SyncWithGPU();

	private:
		RayCastIO rayCastIO;
		std::unique_ptr<Vulkan::Buffer> buffer_;
		std::unique_ptr<Vulkan::DeviceMemory> memory_;
	};

}