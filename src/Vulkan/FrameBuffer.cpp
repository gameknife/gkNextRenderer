#include "FrameBuffer.hpp"
#include "DepthBuffer.hpp"
#include "Device.hpp"
#include "ImageView.hpp"
#include "RenderPass.hpp"
#include "SwapChain.hpp"
#include <array>

namespace Vulkan {

FrameBuffer::FrameBuffer(const class ImageView& imageView, const class RenderPass& renderPass, bool withDS ) : device_(imageView.Device())
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
	framebufferInfo.width = renderPass.SwapChain().Extent().width;
	framebufferInfo.height = renderPass.SwapChain().Extent().height;
	framebufferInfo.layers = 1;

	Check(vkCreateFramebuffer(imageView.Device().Handle(), &framebufferInfo, nullptr, &framebuffer_),
		"create framebuffer");
}

FrameBuffer::FrameBuffer(const Vulkan::ImageView& imageView, const Vulkan::ImageView& imageView1,
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
	framebufferInfo.width = renderPass.SwapChain().Extent().width;
	framebufferInfo.height = renderPass.SwapChain().Extent().height;
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

}
