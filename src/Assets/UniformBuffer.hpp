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
		float PaperWhiteNit;

		glm::vec4 SunDirection;
		glm::vec4 SunColor;
		glm::vec4 BackGroundColor;
		float SkyIntensity;
		
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
		uint32_t HasSun; // bool
	};

	// lightquad can represent by 4 points
	// p1 +-------------------+ p2
	//    |                   |
	//    |                   |
	//    |                   |
	// p0 +-------------------+ p3

	// when calculating the random point inside quad
	// we can generate two random number r1 and r2
	// then the random point is p0 + r1 * (p1 - p0) + r2 * (p3 - p0)
	// the normal of the quad is (p1 - p0) x (p3 - p0)
	// the area of the quad is |(p1 - p0) x (p3 - p0)| / 2
	
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

}