#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include <array>

namespace Vulkan
{

// ============================================================================
// RenderPass
// ============================================================================

RenderPass::RenderPass(const Vulkan::SwapChain& swapChain, const class DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp) :
    swapChain_(swapChain),
    depthBuffer_(depthBuffer)
{
    Init({.colorFormats = {swapChain.Format()},
          .colorLoadOp = colorBufferLoadOp});
}

RenderPass::RenderPass(
    const class SwapChain& swapChain,
    const class DepthBuffer& depthBuffer,
    const VkAttachmentLoadOp colorBufferLoadOp,
    const VkAttachmentLoadOp depthBufferLoadOp) :
    swapChain_(swapChain),
    depthBuffer_(depthBuffer)
{
    Init({.colorFormats = {swapChain.Format()},
          .hasDepth = true,
          .colorLoadOp = colorBufferLoadOp,
          .depthLoadOp = depthBufferLoadOp,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});
}

RenderPass::RenderPass(const Vulkan::SwapChain& swapChain, VkFormat format, const Vulkan::DepthBuffer& depthBuffer,
                       VkAttachmentLoadOp colorBufferLoadOp, VkImageLayout colorInitialLayout,
                       VkImageLayout colorFinalLayout) : swapChain_(swapChain), depthBuffer_(depthBuffer)
{
    Init({.colorFormats = {format},
          .colorLoadOp = colorBufferLoadOp,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .colorInitialLayout = colorInitialLayout,
          .colorFinalLayout = colorFinalLayout});
}

RenderPass::RenderPass(
    const Vulkan::SwapChain& swapChain,
    VkFormat format,
    const Vulkan::DepthBuffer& depthBuffer,
    VkAttachmentLoadOp colorBufferLoadOp,
    VkAttachmentLoadOp depthBufferLoadOp,
    VkImageLayout colorInitialLayout,
    VkImageLayout colorFinalLayout) : swapChain_(swapChain), depthBuffer_(depthBuffer)
{
    Init({.colorFormats = {format},
          .hasDepth = true,
          .colorLoadOp = colorBufferLoadOp,
          .depthLoadOp = depthBufferLoadOp,
          .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          .colorInitialLayout = colorInitialLayout,
          .colorFinalLayout = colorFinalLayout});
}

RenderPass::RenderPass(const Vulkan::SwapChain& swapChain, VkFormat format, const Vulkan::DepthBuffer& depthBuffer,
                       VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp) : swapChain_(swapChain), depthBuffer_(depthBuffer)
{
    Init({.colorFormats = {format},
          .hasDepth = true,
          .colorLoadOp = colorBufferLoadOp,
          .depthLoadOp = depthBufferLoadOp,
          .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});
}

RenderPass::RenderPass(const Vulkan::SwapChain& swapChain, VkFormat format, VkFormat format1, VkFormat format2,
                       const Vulkan::DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp) : swapChain_(swapChain), depthBuffer_(depthBuffer)
{
    Init({.colorFormats = {format, format1, format2},
          .hasDepth = true,
          .colorLoadOp = colorBufferLoadOp,
          .depthLoadOp = depthBufferLoadOp,
          .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});
}

void RenderPass::Init(const FRenderPassSpec& spec)
{
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorAttachmentRefs;
    attachments.reserve(spec.colorFormats.size() + (spec.hasDepth ? 1 : 0));
    colorAttachmentRefs.reserve(spec.colorFormats.size());

    for (VkFormat colorFormat : spec.colorFormats)
    {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = colorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = spec.colorLoadOp;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = spec.colorInitialLayout != VK_IMAGE_LAYOUT_UNDEFINED
            ? spec.colorInitialLayout
            : (spec.colorLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        colorAttachment.finalLayout = spec.colorFinalLayout;

        colorAttachmentRefs.push_back({static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        attachments.push_back(colorAttachment);
    }

    VkAttachmentReference depthAttachmentRef = {};
    if (spec.hasDepth)
    {
        VkAttachmentDescription depthAttachment = {};
        depthAttachment.format = depthBuffer_.Format();
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = spec.depthLoadOp;
        depthAttachment.storeOp = spec.depthStoreOp;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = spec.depthLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depthAttachmentRef.attachment = static_cast<uint32_t>(attachments.size());
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.push_back(depthAttachment);
    }

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
    subpass.pColorAttachments = colorAttachmentRefs.data();
    subpass.pDepthStencilAttachment = spec.hasDepth ? &depthAttachmentRef : nullptr;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = spec.dstAccessMask;
    if (spec.colorLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
    {
        dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    }
    if (spec.hasDepth)
    {
        dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        if (spec.depthLoadOp == VK_ATTACHMENT_LOAD_OP_LOAD)
        {
            dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        }
    }

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    Check(vkCreateRenderPass(swapChain_.Device().Handle(), &renderPassInfo, nullptr, &renderPass_),
          "create render pass");
}

RenderPass::~RenderPass()
{
    if (renderPass_ != nullptr)
    {
        vkDestroyRenderPass(swapChain_.Device().Handle(), renderPass_, nullptr);
        renderPass_ = nullptr;
    }
}

void RenderPass::SetDebugName(const std::string& name)
{
    const auto& debugUtils = swapChain_.Device().DebugUtils();
    debugUtils.SetObjectName(renderPass_, name.c_str());
}

// ============================================================================
// FrameBuffer
// ============================================================================

FrameBuffer::FrameBuffer(const VkExtent2D& extent, const class ImageView& imageView, const class RenderPass& renderPass, bool withDS ) : device_(imageView.Device())
{
    std::vector<VkImageView> attachments;
    attachments.push_back(imageView.Handle());
    if(withDS)
    {
        attachments.push_back( renderPass.DepthBuffer().ImageView().Handle() );
    }

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass.Handle();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    Check(vkCreateFramebuffer(imageView.Device().Handle(), &framebufferInfo, nullptr, &framebuffer_),
        "create framebuffer");
}

FrameBuffer::FrameBuffer(const VkExtent2D& extent, const Vulkan::ImageView& imageView, const Vulkan::ImageView& imageView1,
const Vulkan::ImageView& imageView2, const Vulkan::RenderPass& renderPass): device_(imageView.Device())
{
    std::array<VkImageView, 4> attachments =
    {
        imageView.Handle(),
        imageView1.Handle(),
        imageView2.Handle(),
        renderPass.DepthBuffer().ImageView().Handle()
    };

    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass.Handle();
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;

    Check(vkCreateFramebuffer(imageView.Device().Handle(), &framebufferInfo, nullptr, &framebuffer_),
        "create framebuffer");
}

FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept :
    device_(other.device_),
    framebuffer_(other.framebuffer_)
{
    other.framebuffer_ = nullptr;
}

FrameBuffer::~FrameBuffer()
{
    if (framebuffer_ != nullptr)
    {
        vkDestroyFramebuffer(device_.Handle(), framebuffer_, nullptr);
        framebuffer_ = nullptr;
    }
}

// ============================================================================
// PipelineLayout
// ============================================================================

PipelineLayout::PipelineLayout(const Device& device, const std::vector<DescriptorSetManager*> managers, uint32_t maxSets, const VkPushConstantRange* pushConstantRanges,
    uint32_t pushConstantRangeCount) : device_(device)
{
    for ( DescriptorSetManager* manager : managers )
    {
        cachedDescriptorSetLayouts_.push_back(manager->DescriptorSetLayout().Handle());
    }

    cachedDescriptorSets_.resize(maxSets);
    for( uint32_t i = 0; i < maxSets; ++i )
    {
        for ( DescriptorSetManager* manager : managers )
        {
            cachedDescriptorSets_[i].push_back(manager->DescriptorSets().Handle(i));
        }
    }
    CreateLayout(pushConstantRanges, pushConstantRangeCount);
}

PipelineLayout::PipelineLayout(const Device & device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges, uint32_t pushConstantRangeCount) :
    device_(device)
{
    // add the global texture set with set = 1, currently an ugly impl
    Assets::GlobalTexturePool* gPool = Assets::GlobalTexturePool::GetInstance();
    cachedDescriptorSetLayouts_ = { descriptorSetLayout.Handle(), gPool->Layout() };

    CreateLayout(pushConstantRanges, pushConstantRangeCount);
}

PipelineLayout::PipelineLayout(const Device& device, const VkPushConstantRange* pushConstantRanges,
    uint32_t pushConstantRangeCount) : device_(device)
{
    CreateLayout(pushConstantRanges, pushConstantRangeCount);
}

void PipelineLayout::CreateLayout(const VkPushConstantRange* pushConstantRanges, uint32_t pushConstantRangeCount)
{
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(cachedDescriptorSetLayouts_.size());
    pipelineLayoutInfo.pSetLayouts = cachedDescriptorSetLayouts_.empty() ? nullptr : cachedDescriptorSetLayouts_.data();
    pipelineLayoutInfo.pushConstantRangeCount = pushConstantRangeCount;
    pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges;

    Check(vkCreatePipelineLayout(device_.Handle(), &pipelineLayoutInfo, nullptr, &pipelineLayout_),
        "create pipeline layout");
}

PipelineLayout::~PipelineLayout()
{
    if (pipelineLayout_ != nullptr)
    {
        vkDestroyPipelineLayout(device_.Handle(), pipelineLayout_, nullptr);
        pipelineLayout_ = nullptr;
    }
}

void PipelineLayout::BindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t idx, VkPipelineBindPoint bindPoint) const
{
    vkCmdBindDescriptorSets( commandBuffer, bindPoint,Handle(), 0,
                         static_cast<uint32_t>(cachedDescriptorSets_[idx].size()), cachedDescriptorSets_[idx].data(), 0, nullptr );

}

}
