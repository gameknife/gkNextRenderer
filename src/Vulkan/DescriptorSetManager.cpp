#include "DescriptorSetManager.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSetLayout.hpp"
#include "DescriptorSets.hpp"
#include "Device.hpp"
#include "Utilities/Exception.hpp"
#include <set>

namespace Vulkan {

DescriptorSetManager::DescriptorSetManager(const Device& device, const std::vector<DescriptorBinding>& descriptorBindings, const size_t maxSets, bool bindless)
{
	// Sanity check to avoid binding different resources to the same binding point.
	std::map<uint32_t, VkDescriptorType> bindingTypes;

	for (const auto& binding : descriptorBindings)
	{
		if (!bindingTypes.insert(std::make_pair(binding.Binding, binding.Type)).second)
		{
			Throw(std::invalid_argument("binding collision"));
		}
	}

	descriptorPool_.reset(new DescriptorPool(device, descriptorBindings, maxSets, bindless));
	descriptorSetLayout_.reset(new class DescriptorSetLayout(device, descriptorBindings, bindless));
	descriptorSets_.reset(new class DescriptorSets(*descriptorPool_, *descriptorSetLayout_, bindingTypes, maxSets));
}

DescriptorSetManager::~DescriptorSetManager()
{
	descriptorSets_.reset();
	descriptorSetLayout_.reset();
	descriptorPool_.reset();
}

}
