#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Utilities/Exception.hpp"
#include <array>
#include <set>
#include <utility>

namespace Vulkan
{

// ============================================================================
// DescriptorPool
// ============================================================================

DescriptorPool::DescriptorPool(const Vulkan::Device& device, const std::vector<DescriptorBinding>& descriptorBindings, const size_t maxSets, bool bindless) :
    device_(device)
{
    std::vector<VkDescriptorPoolSize> poolSizes;

    for (const auto& binding : descriptorBindings)
    {
        poolSizes.push_back(VkDescriptorPoolSize{ binding.Type, static_cast<uint32_t>(binding.DescriptorCount * maxSets )});
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = bindless ? VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT : VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(maxSets);

    Check(vkCreateDescriptorPool(device.Handle(), &poolInfo, nullptr, &descriptorPool_),
        "create descriptor pool");
}

DescriptorPool::~DescriptorPool()
{
    if (descriptorPool_ != nullptr)
    {
        vkDestroyDescriptorPool(device_.Handle(), descriptorPool_, nullptr);
        descriptorPool_ = nullptr;
    }
}

// ============================================================================
// DescriptorSetLayout
// ============================================================================

DescriptorSetLayout::DescriptorSetLayout(const Device& device, const std::vector<DescriptorBinding>& descriptorBindings, bool bindless) :
    device_(device)
{
    std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
    std::vector<VkDescriptorBindingFlags> bindlessBindingFlags;

    for (const auto& binding : descriptorBindings)
    {
        VkDescriptorSetLayoutBinding b = {};
        b.binding = binding.Binding;
        b.descriptorCount = binding.DescriptorCount;
        b.descriptorType = binding.Type;
        b.stageFlags = binding.Stage;

        layoutBindings.push_back(b);
    }

    if (bindless)
    {
        for (size_t i = 0; i < layoutBindings.size(); ++i)
        {
            bindlessBindingFlags.push_back(
                VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT);
        }
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutInfo.pBindings = layoutBindings.data();

    // bindless stuff
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr
    };

    extendedInfo.bindingCount = layoutInfo.bindingCount;
    extendedInfo.pBindingFlags = bindlessBindingFlags.data();

    if( bindless )
    {
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
        layoutInfo.pNext = &extendedInfo;
    }

    Check(vkCreateDescriptorSetLayout(device.Handle(), &layoutInfo, nullptr, &layout_),
        "create descriptor set layout");
}

DescriptorSetLayout::~DescriptorSetLayout()
{
    if (layout_ != nullptr)
    {
        vkDestroyDescriptorSetLayout(device_.Handle(), layout_, nullptr);
        layout_ = nullptr;
    }
}

// ============================================================================
// DescriptorSets
// ============================================================================

DescriptorSets::DescriptorSets(
    const DescriptorPool& descriptorPool,
    const DescriptorSetLayout& layout,
    std::map<uint32_t, VkDescriptorType> bindingTypes,
    const size_t size,
    bool bindless) :
    descriptorPool_(descriptorPool),
    bindingTypes_(std::move(bindingTypes))
{
    std::vector<VkDescriptorSetLayout> layouts(size, layout.Handle());

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool.Handle();
    allocInfo.descriptorSetCount = static_cast<uint32_t>(size);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(size);

    Check(vkAllocateDescriptorSets(descriptorPool.Device().Handle(), &allocInfo, descriptorSets_.data()),
        "allocate descriptor sets");
}

DescriptorSets::~DescriptorSets()
{
    //if (!descriptorSets_.empty())
    //{
    //	vkFreeDescriptorSets(
    //		descriptorPool_.Device().Handle(),
    //		descriptorPool_.Handle(),
    //		static_cast<uint32_t>(descriptorSets_.size()),
    //		descriptorSets_.data());

    //	descriptorSets_.clear();
    //}
}

VkWriteDescriptorSet DescriptorSets::Bind(const uint32_t index, const uint32_t binding, const VkDescriptorBufferInfo& bufferInfo, uint32_t arrayElement, const uint32_t count) const
{
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSets_[index];
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = arrayElement;
    descriptorWrite.descriptorType = GetBindingType(binding);
    descriptorWrite.descriptorCount = count;
    descriptorWrite.pBufferInfo = &bufferInfo;

    return descriptorWrite;
}

VkWriteDescriptorSet DescriptorSets::Bind(const uint32_t index, const uint32_t binding, const VkDescriptorImageInfo& imageInfo, uint32_t arrayElement, const uint32_t count) const
{
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSets_[index];
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = arrayElement;
    descriptorWrite.descriptorType = GetBindingType(binding);
    descriptorWrite.descriptorCount = count;
    descriptorWrite.pImageInfo = &imageInfo;

    return descriptorWrite;
}

VkWriteDescriptorSet DescriptorSets::Bind(uint32_t index, uint32_t binding, const VkWriteDescriptorSetAccelerationStructureKHR& structureInfo, uint32_t arrayElement, const uint32_t count) const
{
    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSets_[index];
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = arrayElement;
    descriptorWrite.descriptorType = GetBindingType(binding);
    descriptorWrite.descriptorCount = count;
    descriptorWrite.pNext = &structureInfo;

    return descriptorWrite;
}

void DescriptorSets::UpdateDescriptors(uint32_t index, const std::vector<VkWriteDescriptorSet>& descriptorWrites)
{
    vkUpdateDescriptorSets(
        descriptorPool_.Device().Handle(),
        static_cast<uint32_t>(descriptorWrites.size()),
        descriptorWrites.data(), 0, nullptr);
}

VkDescriptorType DescriptorSets::GetBindingType(uint32_t binding) const
{
    const auto it = bindingTypes_.find(binding);
    if (it == bindingTypes_.end())
    {
        Throw(std::invalid_argument("binding not found"));
    }

    return it->second;
}

// ============================================================================
// DescriptorSetManager
// ============================================================================

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
    descriptorSets_.reset(new class DescriptorSets(*descriptorPool_, *descriptorSetLayout_, bindingTypes, maxSets, bindless));
}

DescriptorSetManager::~DescriptorSetManager()
{
    descriptorSets_.reset();
    descriptorSetLayout_.reset();
    descriptorPool_.reset();
}

}
