#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <string>
#include <vector>
#include <memory>
#include <span>

namespace Vulkan
{
    // ============================================================================
    // RenderPass
    // ============================================================================

    class RenderPass final
    {
    public:

        VULKAN_NON_COPIABLE(RenderPass)

        RenderPass(const SwapChain& swapChain, const class DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp);
        RenderPass(const SwapChain& swapChain, const DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp);
        RenderPass(const SwapChain& swapChain, VkFormat format, const DepthBuffer& depthBuffer,
                   VkAttachmentLoadOp colorBufferLoadOp, VkImageLayout colorInitialLayout,
                   VkImageLayout colorFinalLayout);
        RenderPass(const SwapChain& swapChain, VkFormat format, const DepthBuffer& depthBuffer,
                   VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp,
                   VkImageLayout colorInitialLayout, VkImageLayout colorFinalLayout);
        RenderPass(const SwapChain& swapChain, VkFormat format, const DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp);
        RenderPass(const SwapChain& swapChain, VkFormat format,  VkFormat format1,  VkFormat format2, const DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp);
        RenderPass(const SwapChain& swapChain, std::span<const VkFormat> colorFormats,
                   const DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp,
                   VkAttachmentLoadOp depthBufferLoadOp);
        ~RenderPass();

        const class SwapChain& SwapChain() const { return swapChain_; }
        const class DepthBuffer& DepthBuffer() const { return depthBuffer_; }

        void SetDebugName(const std::string& name);
    private:

        // Declarative attachment layout shared by all public constructors
        struct FRenderPassSpec
        {
            std::vector<VkFormat> colorFormats;
            bool hasDepth = false;
            VkAttachmentLoadOp colorLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentLoadOp depthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            VkAttachmentStoreOp depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            VkAccessFlags dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VkImageLayout colorInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout colorFinalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        };
        void Init(const FRenderPassSpec& spec);

        const class SwapChain& swapChain_;
        const class DepthBuffer& depthBuffer_;

        VULKAN_HANDLE(VkRenderPass, renderPass_)
    };

    // ============================================================================
    // FrameBuffer
    // ============================================================================

    class FrameBuffer final
    {
    public:

        FrameBuffer(const FrameBuffer&) = delete;
        FrameBuffer& operator = (const FrameBuffer&) = delete;
        FrameBuffer& operator = (FrameBuffer&&) = delete;

        explicit FrameBuffer(const VkExtent2D& extent, const ImageView& imageView, const RenderPass& renderPass, bool withDS = true);
        explicit FrameBuffer(const VkExtent2D& extent, const ImageView& imageView, const ImageView& imageView1, const ImageView& imageView2,const RenderPass& renderPass);
        FrameBuffer(const VkExtent2D& extent, std::span<const ImageView* const> colorImageViews,
                    const RenderPass& renderPass, bool withDS = true);
        FrameBuffer(FrameBuffer&& other) noexcept;
        ~FrameBuffer();

    private:

        const class Device& device_;
        VULKAN_HANDLE(VkFramebuffer, framebuffer_)
    };

    // ============================================================================
    // PipelineLayout
    // ============================================================================

    class PipelineLayout final
    {
    public:

        VULKAN_NON_COPIABLE(PipelineLayout)

        PipelineLayout(const Device& device, const std::vector<DescriptorSetManager*> managers, uint32_t maxSets, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
        PipelineLayout(const Device& device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
        PipelineLayout(const Device& device, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
        ~PipelineLayout();

        void BindDescriptorSets(
            VkCommandBuffer commandBuffer,
            uint32_t idx,
            VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE) const;
    private:

        // Shared vkCreatePipelineLayout call over cachedDescriptorSetLayouts_
        void CreateLayout(const VkPushConstantRange* pushConstantRanges, uint32_t pushConstantRangeCount);

        const Device& device_;

        VULKAN_HANDLE(VkPipelineLayout, pipelineLayout_)

        std::vector<VkDescriptorSetLayout> cachedDescriptorSetLayouts_;
        std::vector< std::vector<VkDescriptorSet> > cachedDescriptorSets_;
    };

    // ============================================================================
    // PipelineBase
    // ============================================================================

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
