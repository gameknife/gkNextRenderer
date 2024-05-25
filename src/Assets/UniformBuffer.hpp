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
	class UniformBufferObject
	{
	public:

		glm::mat4 ModelView;
		glm::mat4 Projection;
		glm::mat4 ModelViewInverse;
		glm::mat4 ProjectionInverse;
		glm::mat4 ViewProjection;
		glm::mat4 PrevViewProjection;
		float Aperture;
		float FocusDistance;
		float SkyRotation;
		float HeatmapScale;
		float ColorPhi;
		float DepthPhi;
		float NormalPhi;
		uint32_t TotalFrames;
		uint32_t TotalNumberOfSamples;
		uint32_t NumberOfSamples;
		uint32_t NumberOfBounces;
		uint32_t RandomSeed;
		uint32_t LightCount;
		uint32_t HasSky; // bool
		uint32_t ShowHeatmap; // bool
		uint32_t UseCheckerBoard; // bool
		uint32_t TemporalFrames;
	};

	struct alignas(16) LightObject final
	{
		glm::vec4 WorldPosMin;
		glm::vec4 WorldPosMax;
		glm::vec4 WorldDirection;
		float area;
		float Reserverd0;
		float Reserverd1;
		float Reserverd2;
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

}