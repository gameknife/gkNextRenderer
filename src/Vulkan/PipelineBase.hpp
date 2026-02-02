#pragma once

#include "Vulkan.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/DescriptorSystem.hpp"

namespace Vulkan
{
	class PipelineBase
	{
	public:
		PipelineBase(const Vulkan::SwapChain& swapChain):swapChain_(swapChain) {}
		virtual ~PipelineBase()
		{
			if (pipeline_ != nullptr)
			{
				vkDestroyPipeline(swapChain_.Device().Handle(), pipeline_, nullptr);
				pipeline_ = nullptr;
			}
			pipelineLayout_.reset();
			descriptorSetManager_.reset();
		}
		VkDescriptorSet DescriptorSet(uint32_t index) const {return descriptorSetManager_->DescriptorSets().Handle(index);}
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	
		VULKAN_HANDLE(VkPipeline, pipeline_)
		const Vulkan::SwapChain& swapChain_;
		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};
}
