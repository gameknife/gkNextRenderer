#include "VulkanBaseRenderer.hpp"
#include "Buffer.hpp"
#include "CommandPool.hpp"
#include "CommandBuffers.hpp"
#include "DebugUtilsMessenger.hpp"
#include "DepthBuffer.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "FrameBuffer.hpp"
#include "GraphicsPipeline.hpp"
#include "Instance.hpp"
#include "PipelineLayout.hpp"
#include "RenderPass.hpp"
#include "Semaphore.hpp"
#include "Surface.hpp"
#include "SwapChain.hpp"
#include "Window.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Texture.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Console.hpp"
#include <array>
#include <fmt/format.h>

#include "ImageMemoryBarrier.hpp"
#include "SingleTimeCommands.hpp"
#include "TaskCoordinator.hpp"

namespace Vulkan {
	
VulkanBaseRenderer::VulkanBaseRenderer(const char* rendererType, const WindowConfig& windowConfig, const VkPresentModeKHR presentMode, const bool enableValidationLayers) :
	rendererType_(rendererType),
	presentMode_(presentMode)
{
	const auto validationLayers = enableValidationLayers
		? std::vector<const char*>{"VK_LAYER_KHRONOS_validation"}
		: std::vector<const char*>{};
	
	WindowConfig windowConfig_ = windowConfig;
	windowConfig_.Title = windowConfig_.Title + " - " + GetRendererType();

	window_.reset(new class Window(windowConfig_));
	instance_.reset(new Instance(*window_, validationLayers, VK_API_VERSION_1_2));
	debugUtilsMessenger_.reset(enableValidationLayers ? new DebugUtilsMessenger(*instance_, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) : nullptr);
	surface_.reset(new Surface(*instance_));
	supportDenoiser_ = false;
	supportScreenShot_ = windowConfig.NeedScreenShot;
	forceSDR_ = windowConfig.ForceSDR;

	uptime = std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

VulkanGpuTimer::VulkanGpuTimer(VkDevice device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop)
{
	device_ = device;
	time_stamps.resize(totalCount);
	timeStampPeriod_ = prop.limits.timestampPeriod;
	// Create the query pool object used to get the GPU time tamps
	VkQueryPoolCreateInfo query_pool_info{};
	query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	// We need to specify the query type for this pool, which in our case is for time stamps
	query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	// Set the no. of queries in this pool
	query_pool_info.queryCount = static_cast<uint32_t>(time_stamps.size());
	Check( vkCreateQueryPool(device, &query_pool_info, nullptr, &query_pool_timestamps), "create timestamp pool");
}

VulkanGpuTimer::~VulkanGpuTimer()
{
	vkDestroyQueryPool(device_, query_pool_timestamps, nullptr);
}

VulkanBaseRenderer::~VulkanBaseRenderer()
{
	VulkanBaseRenderer::DeleteSwapChain();

	gpuTimer_.reset();
	globalTexturePool_.reset();
	commandPool_.reset();
	commandPool2_.reset();
	device_.reset();
	surface_.reset();
	debugUtilsMessenger_.reset();
	instance_.reset();
	window_.reset();
}

const std::vector<VkExtensionProperties>& VulkanBaseRenderer::Extensions() const
{
	return instance_->Extensions();
}

const std::vector<VkLayerProperties>& VulkanBaseRenderer::Layers() const
{
	return instance_->Layers();
}

const std::vector<VkPhysicalDevice>& VulkanBaseRenderer::PhysicalDevices() const
{
	return instance_->PhysicalDevices();
}

void VulkanBaseRenderer::SetPhysicalDevice(VkPhysicalDevice physicalDevice)
{
	if (device_)
	{
		Throw(std::logic_error("physical device has already been set"));
	}

	supportRayTracing_ = false;

	std::vector<const char*> requiredExtensions = 
	{
		// VK_KHR_swapchain
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};

	VkPhysicalDeviceFeatures deviceFeatures = {};

	deviceFeatures.multiDrawIndirect = true;
	deviceFeatures.drawIndirectFirstInstance = true;
	
	SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nullptr);

	// Global Texture Pool Creation Here
	globalTexturePool_.reset(new Assets::GlobalTexturePool(*device_, *commandPool2_));
	
	OnDeviceSet();
	
	// Create swap chain and command buffers.
	CreateSwapChain();

	window_->Show();

	uptime = std::chrono::high_resolution_clock::now().time_since_epoch().count() - uptime;
	fmt::print("\n{} renderer initialized in {:.2f}ms{}\n", CONSOLE_GREEN_COLOR, uptime * 1e-6f, CONSOLE_DEFAULT_COLOR);
}

void VulkanBaseRenderer::Run()
{
	if (!device_)
	{
		Throw(std::logic_error("physical device has not been set"));
	}

	currentFrame_ = 0;

	window_->DrawFrame = [this]() { DrawFrame(); };
	window_->OnKey = [this](const int key, const int scancode, const int action, const int mods) { OnKey(key, scancode, action, mods); };
	window_->OnCursorPosition = [this](const double xpos, const double ypos) { OnCursorPosition(xpos, ypos); };
	window_->OnMouseButton = [this](const int button, const int action, const int mods) { OnMouseButton(button, action, mods); };
	window_->OnScroll = [this](const double xoffset, const double yoffset) { OnScroll(xoffset, yoffset); };
	window_->OnDropFile = [this](int path_count, const char* paths[]) { OnDropFile(path_count, paths); };
	window_->Run();
	device_->WaitIdle();
}

void VulkanBaseRenderer::Start()
{
	currentFrame_ = 0;

	window_->DrawFrame = [this]() { DrawFrame(); };
	window_->OnKey = [this](const int key, const int scancode, const int action, const int mods) { OnKey(key, scancode, action, mods); };
	window_->OnCursorPosition = [this](const double xpos, const double ypos) { OnCursorPosition(xpos, ypos); };
	window_->OnMouseButton = [this](const int button, const int action, const int mods) { OnMouseButton(button, action, mods); };
	window_->OnScroll = [this](const double xoffset, const double yoffset) { OnScroll(xoffset, yoffset); };
	window_->OnDropFile = [this](int path_count, const char* paths[]) { OnDropFile(path_count, paths); };
}

void VulkanBaseRenderer::End()
{
	device_->WaitIdle();
	gpuTimer_.reset();
}

bool VulkanBaseRenderer::Tick()
{
#if ANDROID
	DrawFrame();
	return false;
#else
	glfwPollEvents();
	DrawFrame();
	return glfwWindowShouldClose( window_->Handle() ) != 0;
#endif
}

void VulkanBaseRenderer::SetPhysicalDeviceImpl(
	VkPhysicalDevice physicalDevice, 
	std::vector<const char*>& requiredExtensions, 
	VkPhysicalDeviceFeatures& deviceFeatures,
	void* nextDeviceFeatures)
{
	// support bindless material
	VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
	indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
	indexingFeatures.pNext = nextDeviceFeatures;
	indexingFeatures.runtimeDescriptorArray = true;
	indexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;
	indexingFeatures.descriptorBindingPartiallyBound = true;
	indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = true;
	indexingFeatures.descriptorBindingVariableDescriptorCount = true;
	

	VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
	bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	bufferDeviceAddressFeatures.pNext = &indexingFeatures;
	bufferDeviceAddressFeatures.bufferDeviceAddress = true;

	VkPhysicalDeviceHostQueryResetFeaturesEXT hostQueryResetFeatures = {};
	hostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
	hostQueryResetFeatures.pNext = &bufferDeviceAddressFeatures;
	hostQueryResetFeatures.hostQueryReset = true;
	
	device_.reset(new class Device(physicalDevice, *surface_, requiredExtensions, deviceFeatures, &hostQueryResetFeatures));
	commandPool_.reset(new class CommandPool(*device_, device_->GraphicsFamilyIndex(), 0, true));
	commandPool2_.reset(new class CommandPool(*device_, device_->TransferFamilyIndex(), 1, true));
	gpuTimer_.reset(new VulkanGpuTimer(device_->Handle(), 10 * 2, device_->DeviceProperties()));
}

void VulkanBaseRenderer::OnDeviceSet()
{
}

void VulkanBaseRenderer::CreateSwapChain()
{
	// Wait until the window is visible.
	while (window_->IsMinimized())
	{
		window_->WaitForEvents();
	}

	swapChain_.reset(new class SwapChain(*device_, presentMode_, forceSDR_));
	depthBuffer_.reset(new class DepthBuffer(*commandPool_, swapChain_->Extent()));

	for (size_t i = 0; i != swapChain_->ImageViews().size(); ++i)
	{
		imageAvailableSemaphores_.emplace_back(*device_);
		renderFinishedSemaphores_.emplace_back(*device_);
		inFlightFences_.emplace_back(*device_, true);
		uniformBuffers_.emplace_back(*device_);
	}

	graphicsPipeline_.reset(new class GraphicsPipeline(*swapChain_, *depthBuffer_, uniformBuffers_, GetScene(), isWireFrame_));

	for (const auto& imageView : swapChain_->ImageViews())
	{
		swapChainFramebuffers_.emplace_back(*imageView, graphicsPipeline_->SwapRenderPass());
	}
	
	commandBuffers_.reset(new CommandBuffers(*commandPool_, static_cast<uint32_t>(swapChainFramebuffers_.size())));

	fence = nullptr;

	screenShotImage_.reset(new Image(*device_, swapChain_->Extent(), swapChain_->Format(), VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
	screenShotImageMemory_.reset(new DeviceMemory(screenShotImage_->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
}

void VulkanBaseRenderer::DeleteSwapChain()
{
	screenShotImageMemory_.reset();
	screenShotImage_.reset();
	commandBuffers_.reset();
	swapChainFramebuffers_.clear();
	graphicsPipeline_.reset();
	uniformBuffers_.clear();
	inFlightFences_.clear();
	renderFinishedSemaphores_.clear();
	imageAvailableSemaphores_.clear();
	depthBuffer_.reset();
	swapChain_.reset();
	fence = nullptr;
}

void VulkanBaseRenderer::CaptureScreenShot()
{
	SingleTimeCommands::Submit(CommandPool(), [&](VkCommandBuffer commandBuffer)
	{
		const auto& image = swapChain_->Images()[currentImageIndex_];

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;
	
		ImageMemoryBarrier::Insert(commandBuffer, image, subresourceRange,
					   0, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		ImageMemoryBarrier::Insert(commandBuffer, screenShotImage_->Handle(), subresourceRange, 0,
						   VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
						   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		// Copy output image into swap-chain image.
		VkImageCopy copyRegion;
		copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copyRegion.srcOffset = {0, 0, 0};
		copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copyRegion.dstOffset = {0, 0, 0};
		copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

		vkCmdCopyImage(commandBuffer,
					   image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					   screenShotImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					   1, &copyRegion);

		ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[currentImageIndex_], subresourceRange,
							   VK_ACCESS_TRANSFER_READ_BIT,
							   0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	});
}
	
void VulkanBaseRenderer::DrawFrame()
{
	const auto noTimeout = std::numeric_limits<uint64_t>::max();

	// wait the last frame command buffer to complete
	if(fence)
	{
		SCOPED_CPU_TIMER("sync-wait");
		fence->Wait(noTimeout);
	}

	{
		SCOPED_CPU_TIMER("query-wait");
		gpuTimer_->FrameEnd((*commandBuffers_)[currentImageIndex_]);
	}

	{
		SCOPED_CPU_TIMER("cpu-prepare");
		BeforeNextFrame();
	}

	// next frame synchronization objects
	fence = &(inFlightFences_[currentFrame_]);
	const auto imageAvailableSemaphore = imageAvailableSemaphores_[currentFrame_].Handle();
	const auto renderFinishedSemaphore = renderFinishedSemaphores_[currentFrame_].Handle();
	
	auto result = vkAcquireNextImageKHR(device_->Handle(), swapChain_->Handle(), noTimeout, imageAvailableSemaphore, nullptr, &currentImageIndex_);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || isWireFrame_ != graphicsPipeline_->IsWireFrame())
	{
		RecreateSwapChain();
		return;
	}

	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		Throw(std::runtime_error(std::string("failed to acquire next image (") + ToString(result) + ")"));
	}

	const auto commandBuffer = commandBuffers_->Begin(currentImageIndex_);
	gpuTimer_->Reset(commandBuffer);

	{
		SCOPED_GPU_TIMER("gpu time");
		Render(commandBuffer, currentImageIndex_);
	}
	
	commandBuffers_->End(currentImageIndex_);

	{
		SCOPED_CPU_TIMER("cpugpu-io");
		AfterRenderCmd();
	}
	
	UpdateUniformBuffer(currentImageIndex_);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkCommandBuffer commandBuffers[]{ commandBuffer };
	VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
	VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };

	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = commandBuffers;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	fence->Reset();

	Check(vkQueueSubmit(device_->GraphicsQueue(), 1, &submitInfo, fence->Handle()),
		"submit draw command buffer");
	
	VkSwapchainKHR swapChains[] = { swapChain_->Handle() };
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &currentImageIndex_;
	presentInfo.pResults = nullptr; // Optional

	{
		SCOPED_CPU_TIMER("present");
		result = vkQueuePresentKHR(device_->PresentQueue(), &presentInfo);
	}
	

	AfterPresent();
	
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		RecreateSwapChain();
		return;
	}
	
	if (result != VK_SUCCESS)
	{
		Throw(std::runtime_error(std::string("failed to present next image (") + ToString(result) + ")"));
	}

	currentFrame_ = (currentFrame_ + 1) % inFlightFences_.size();

	frameCount_++;
}

void VulkanBaseRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
{
	std::array<VkClearValue, 2> clearValues = {};
	clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = graphicsPipeline_->RenderPass().Handle();
	renderPassInfo.framebuffer = swapChainFramebuffers_[imageIndex].Handle();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = swapChain_->Extent();
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();
	
	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	{
		const auto& scene = GetScene();

		VkDescriptorSet descriptorSets[] = { graphicsPipeline_->DescriptorSet(imageIndex) };
		VkBuffer vertexBuffers[] = { scene.VertexBuffer().Handle() };
		const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
		VkDeviceSize offsets[] = { 0 };

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		// bind the global bindless set
		static const uint32_t k_bindless_set = 1;
		VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
		vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->PipelineLayout().Handle(), k_bindless_set,
								 1, GlobalDescriptorSets, 0, nullptr );
		
		uint32_t vertexOffset = 0;
		uint32_t indexOffset = 0;

		// drawcall one by one
		for (const auto& node : scene.Nodes())
		{
			auto& model = scene.Models()[node.GetModel()];
			auto& offset = scene.Offsets()[node.GetModel()];
			const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
			const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());

			// use push constants to set world matrix
			glm::mat4 worldMatrix = node.WorldTransform();

			vkCmdPushConstants(commandBuffer, graphicsPipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_VERTEX_BIT,
				   0, sizeof(glm::mat4), &worldMatrix);
			
			vkCmdDrawIndexed(commandBuffer, indexCount, 1, offset.r, offset.g, 0);
		}
	}
	vkCmdEndRenderPass(commandBuffer);
}

void VulkanBaseRenderer::UpdateUniformBuffer(const uint32_t imageIndex)
{
	lastUBO = GetUniformBufferObject(swapChain_->Extent());
	uniformBuffers_[imageIndex].SetValue(lastUBO);
}

void VulkanBaseRenderer::RecreateSwapChain()
{
	device_->WaitIdle();
	DeleteSwapChain();
	CreateSwapChain();
}

}
