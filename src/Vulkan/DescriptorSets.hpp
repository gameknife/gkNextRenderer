#pragma once

#include "Vulkan.hpp"
#include <map>
#include <vector>
#include <glm/ext/scalar_common.hpp>

namespace Vulkan
{
	class Buffer;
	class DescriptorPool;
	class DescriptorSetLayout;
	class ImageView;

	class DescriptorSets final
	{
	public:

		VULKAN_NON_COPIABLE(DescriptorSets)

		DescriptorSets(
			const DescriptorPool& descriptorPool, 
			const DescriptorSetLayout& layout,
		    std::map<uint32_t, VkDescriptorType> bindingTypes, 
			size_t size);

		~DescriptorSets();

		VkDescriptorSet Handle(uint32_t index) const
		{
			// always return avaliable
			index = glm::min(index, static_cast<uint32_t>(descriptorSets_.size() - 1));
			return descriptorSets_[index];
		}

		VkWriteDescriptorSet Bind(uint32_t index, uint32_t binding, const VkDescriptorBufferInfo& bufferInfo, uint32_t arrayElement = 0,uint32_t count = 1) const;
		VkWriteDescriptorSet Bind(uint32_t index, uint32_t binding, const VkDescriptorImageInfo& imageInfo, uint32_t arrayElement = 0, uint32_t count = 1) const;
		VkWriteDescriptorSet Bind(uint32_t index, uint32_t binding, const VkWriteDescriptorSetAccelerationStructureKHR& structureInfo, uint32_t arrayElement = 0, uint32_t count = 1) const;

		void UpdateDescriptors(uint32_t index, const std::vector<VkWriteDescriptorSet>& descriptorWrites);

	private:

		VkDescriptorType GetBindingType(uint32_t binding) const;

		const DescriptorPool& descriptorPool_;
		const std::map<uint32_t, VkDescriptorType> bindingTypes_;
		
		std::vector<VkDescriptorSet> descriptorSets_;
	};

}
