#pragma once

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Vulkan::RayTracing
{

	class PathTracingRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(PathTracingRenderer);
	
	public:

		PathTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender): LogicRendererBase(baseRender) {}
		virtual ~PathTracingRenderer();

		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
		FRendererRequirements Requirements() const override { return GetRendererRequirements(ERT_PathTracing); }

		struct FSharcBuffer
		{
			std::unique_ptr<Vulkan::Buffer> buffer;
			std::unique_ptr<Vulkan::DeviceMemory> memory;
			VkDeviceSize size = 0;
		};

		struct FSharcState
		{
			FSharcBuffer hashEntries;
			FSharcBuffer lockBuffer;
			FSharcBuffer accumulation;
			FSharcBuffer resolved;
			FSharcBuffer parameters;
			FSharcBuffer resources;
			uint32_t entriesPow2 = 0;
			uint32_t entryCount = 0;
			bool pendingClear = false;
		};

	private:

		// individual textures
		std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> rayTracingPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> sharcUpdatePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> sharcQueryPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> sharcResolvePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipelineNonDenoiser_;
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> accumulatePipeline_;

		uint32_t prevSingleDiffuseId_{};
		uint32_t prevSingleSpecularId_{};
		uint32_t prevSingleAlbedoId_{};
		FSharcState sharc_;

		void EnsureSharcPipelines();
		void EnsureSharcResources();
		void UpdateSharcParameters();
		void ClearSharcResources(VkCommandBuffer commandBuffer);
		void InsertSharcBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask) const;
		Assets::GPUScene BuildSharcGPUScene(uint32_t imageIndex);
	};

}
