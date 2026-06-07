#include "RenderingPipeline.hpp"
#include "GpuResources.hpp"
#include "Device.hpp"
#include "SwapChain.hpp"
#include "GpuResources.hpp"
#include "DescriptorSystem.hpp"
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
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapChain.Format();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = colorBufferLoadOp;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = colorBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 1> attachments =
    {
        colorAttachment,
    };

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

RenderPass::RenderPass(
    const class SwapChain& swapChain,
    const class DepthBuffer& depthBuffer,
    const VkAttachmentLoadOp colorBufferLoadOp,
    const VkAttachmentLoadOp depthBufferLoadOp) :
    swapChain_(swapChain),
    depthBuffer_(depthBuffer)
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapChain.Format();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = colorBufferLoadOp;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = colorBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthBuffer.Format();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = depthBufferLoadOp;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = depthBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments =
    {
        colorAttachment,
        depthAttachment
    };

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

RenderPass::RenderPass(const Vulkan::SwapChain& swapChain, VkFormat format, const Vulkan::DepthBuffer& depthBuffer,
                       VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp) : swapChain_(swapChain), depthBuffer_(depthBuffer)
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = colorBufferLoadOp;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = colorBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthBuffer.Format();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = depthBufferLoadOp;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = depthBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments =
    {
        colorAttachment,
        depthAttachment
    };

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

RenderPass::RenderPass(const Vulkan::SwapChain& swapChain, VkFormat format, VkFormat format1, VkFormat format2,
                       const Vulkan::DepthBuffer& depthBuffer, VkAttachmentLoadOp colorBufferLoadOp, VkAttachmentLoadOp depthBufferLoadOp) : swapChain_(swapChain), depthBuffer_(depthBuffer)
{
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = colorBufferLoadOp;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = colorBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription colorAttachment1 = {};
    colorAttachment1.format = format1;
    colorAttachment1.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment1.loadOp = colorBufferLoadOp;
    colorAttachment1.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment1.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment1.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment1.initialLayout = colorBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment1.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription colorAttachment2 = {};
    colorAttachment2.format = format2;
    colorAttachment2.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment2.loadOp = colorBufferLoadOp;
    colorAttachment2.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment2.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment2.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment2.initialLayout = colorBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    colorAttachment2.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthBuffer.Format();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = depthBufferLoadOp;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = depthBufferLoadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::vector<VkAttachmentReference> colorAttachmentRefs;
    colorAttachmentRefs.push_back({0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    colorAttachmentRefs.push_back({1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    colorAttachmentRefs.push_back({2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 3;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefs.size());
    subpass.pColorAttachments = colorAttachmentRefs.data();
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments;
    attachments.push_back(colorAttachment);
    attachments.push_back(colorAttachment1);
    attachments.push_back(colorAttachment2);
    attachments.push_back(depthAttachment);

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
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(cachedDescriptorSetLayouts_.size());
	pipelineLayoutInfo.pSetLayouts = cachedDescriptorSetLayouts_.data();
	pipelineLayoutInfo.pushConstantRangeCount = pushConstantRangeCount;
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges;

	Check(vkCreatePipelineLayout(device_.Handle(), &pipelineLayoutInfo, nullptr, &pipelineLayout_),
		"create pipeline layout");
}

PipelineLayout::PipelineLayout(const Device & device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges, uint32_t pushConstantRangeCount) :
	device_(device)
{
	// add the global texture set with set = 1, currently an ugly impl
	Assets::GlobalTexturePool* gPool = Assets::GlobalTexturePool::GetInstance();
	cachedDescriptorSetLayouts_ = { descriptorSetLayout.Handle(), gPool->Layout() };

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 2;
	pipelineLayoutInfo.pSetLayouts = cachedDescriptorSetLayouts_.data();
	pipelineLayoutInfo.pushConstantRangeCount = pushConstantRangeCount;
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges;

	Check(vkCreatePipelineLayout(device_.Handle(), &pipelineLayoutInfo, nullptr, &pipelineLayout_),
		"create pipeline layout");
}

PipelineLayout::PipelineLayout(const Device& device, const VkPushConstantRange* pushConstantRanges,
	uint32_t pushConstantRangeCount) : device_(device)
{
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 0;
	pipelineLayoutInfo.pSetLayouts = nullptr;
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
