#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <memory>

namespace Vulkan::PipelineCommon
{
}

namespace Vulkan::ModernDeferred
{
	class SoftwareTracingRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(SoftwareTracingRenderer)
		
		SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~SoftwareTracingRenderer();
		
		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		FRendererRequirements Requirements() const override { return GetRendererRequirements(ERT_ModernDeferred); }

	private:
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;

		uint32_t prevSingleDiffuseId_{};
		uint32_t prevSingleSpecularId_{};
		uint32_t prevSingleAlbedoId_{};
	};

}
