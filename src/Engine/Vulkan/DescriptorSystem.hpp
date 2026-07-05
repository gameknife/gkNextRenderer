#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <map>
#include <memory>
#include <vector>
#include <algorithm>

namespace Vulkan
{
    // ============================================================================
    // DescriptorBinding
    // ============================================================================

    struct DescriptorBinding
    {
        uint32_t Binding; // Slot to which the descriptor will be bound, corresponding to the layout index in the shader.
        uint32_t DescriptorCount; // Number of descriptors to bind
        VkDescriptorType Type; // Type of the bound descriptor(s)
        VkShaderStageFlags Stage; // Shader stage at which the bound resources will be available
    };

    // ============================================================================
    // DescriptorPool
    // ============================================================================

    class DescriptorPool final
    {
    public:

        VULKAN_NON_COPIABLE(DescriptorPool)

        DescriptorPool(const Device& device, const std::vector<DescriptorBinding>& descriptorBindings, size_t maxSets, bool bindless = false);
        ~DescriptorPool();

        const class Device& Device() const { return device_; }

    private:

        const class Device& device_;

        VULKAN_HANDLE(VkDescriptorPool, descriptorPool_)
    };

    // ============================================================================
    // DescriptorSetLayout
    // ============================================================================

    class DescriptorSetLayout final
    {
    public:

        VULKAN_NON_COPIABLE(DescriptorSetLayout)

        DescriptorSetLayout(const Device& device, const std::vector<DescriptorBinding>& descriptorBindings, bool bindless = false);
        ~DescriptorSetLayout();

    private:

        const Device& device_;

        VULKAN_HANDLE(VkDescriptorSetLayout, layout_)
    };

    // ============================================================================
    // DescriptorSets
    // ============================================================================

    class DescriptorSets final
    {
    public:

        VULKAN_NON_COPIABLE(DescriptorSets)

        DescriptorSets(
            const DescriptorPool& descriptorPool,
            const DescriptorSetLayout& layout,
            std::map<uint32_t, VkDescriptorType> bindingTypes,
            size_t size,
            bool bindless);

        ~DescriptorSets();

        VkDescriptorSet Handle(uint32_t index) const
        {
            // always return available
            index = std::min(index, static_cast<uint32_t>(descriptorSets_.size() - 1));
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

    // ============================================================================
    // DescriptorSetManager
    // ============================================================================

    class DescriptorSetManager final
    {
    public:

        VULKAN_NON_COPIABLE(DescriptorSetManager)

        explicit DescriptorSetManager(const Device& device, const std::vector<DescriptorBinding>& descriptorBindings, size_t maxSets, bool bindless = false);
        ~DescriptorSetManager();

        const class DescriptorSetLayout& DescriptorSetLayout() const { return *descriptorSetLayout_; }
        class DescriptorSets& DescriptorSets() { return *descriptorSets_; }

    private:

        std::unique_ptr<DescriptorPool> descriptorPool_;
        std::unique_ptr<class DescriptorSetLayout> descriptorSetLayout_;
        std::unique_ptr<class DescriptorSets> descriptorSets_;
    };

}
