#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"

#include <vector>
#include <memory>

namespace Vulkan
{
	class RenderImage;
}

namespace Assets
{
	class Scene;
	struct UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::LegacyDeferred
{
	class LegacyDeferredRenderer final : public Vulkan::RayTracing::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(LegacyDeferredRenderer)
		
		LegacyDeferredRenderer(RayTracing::RayTraceBaseRenderer& baseRender);
		~LegacyDeferredRenderer();

		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<class GBufferPipeline> gbufferPipeline_;
		std::unique_ptr<class ShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;

		// simple gbuffer, shading mode may increase
		// gbuffer0 albedo, gbuffer1 normal+roughness. gbuffer2 reserved
		// simulating total size of 32 + 128 + 32 = 192 bits
		std::unique_ptr<RenderImage> rtGBuffer0_;
		std::unique_ptr<RenderImage> rtGBuffer1_;
		std::unique_ptr<RenderImage> rtGBuffer2_;
		std::unique_ptr<RenderImage> rtOutput_;
	};

}
