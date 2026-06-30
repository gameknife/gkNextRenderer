#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <memory>
#include <string>

namespace Vulkan::SoftwareModern
{
	class SoftwareModernRenderer final : public Vulkan::LogicRendererBase
	{
	public:
		VULKAN_NON_COPIABLE(SoftwareModernRenderer)

		explicit SoftwareModernRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~SoftwareModernRenderer();

		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		void ReloadShaders(const std::set<std::string>& changedShaderFiles, std::set<std::string>& handledShaderFiles) override;
		FRendererRequirements Requirements() const override { return GetRendererRequirements(ERT_SoftwareModern); }

	private:
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> accumulatePipeline_;

	};

}
