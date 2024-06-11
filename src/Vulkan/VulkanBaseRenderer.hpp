#pragma once

#include "FrameBuffer.hpp"
#include "WindowConfig.hpp"
#include <vector>
#include <memory>
#include <map>
#include <cassert>
#include "Image.hpp"

namespace Assets
{
	class Scene;
	class UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan 
{
	class VulkanGpuTimer
	{
	public:
		VulkanGpuTimer(VkDevice device, uint32_t totalCount);
		~VulkanGpuTimer();

		void Reset(VkCommandBuffer commandBuffer)
		{
			vkCmdResetQueryPool(commandBuffer, query_pool_timestamps, 0, time_stamps.size());
			queryIdx = 0;
		}

		void FrameEnd(VkCommandBuffer commandBuffer)
		{
			uint32_t count = static_cast<uint32_t>(time_stamps.size());

			// Fetch the time stamp results written in the command buffer submissions
			// A note on the flags used:
			//	VK_QUERY_RESULT_64_BIT: Results will have 64 bits. As time stamp values are on nano-seconds, this flag should always be used to avoid 32 bit overflows
			//  VK_QUERY_RESULT_WAIT_BIT: Since we want to immediately display the results, we use this flag to have the CPU wait until the results are available
			vkGetQueryPoolResults(
				device_,
				query_pool_timestamps,
				0,
				queryIdx,
				time_stamps.size() * sizeof(uint64_t),
				time_stamps.data(),
				sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
		}

		void Start(VkCommandBuffer commandBuffer, const char* name)
		{
			if( timer_query_map.find(name) == timer_query_map.end())
			{
				timer_query_map[name] = std::make_tuple(0, 0);
			}
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
			std::get<0>(timer_query_map[name]) = queryIdx;
			queryIdx++;
		}
		void End(VkCommandBuffer commandBuffer, const char* name)
		{
			assert( timer_query_map.find(name) != timer_query_map.end() );
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
			std::get<1>(timer_query_map[name]) = queryIdx;
			queryIdx++;
		}
		uint64_t GetTime(const char* name)
		{
			if(timer_query_map.find(name) == timer_query_map.end())
			{
				return 0;
			}
			return time_stamps[ std::get<1>(timer_query_map[name]) ] - time_stamps[ std::get<0>(timer_query_map[name])];
		}
		
		VkQueryPool query_pool_timestamps = VK_NULL_HANDLE;
		std::vector<uint64_t> time_stamps{};
		std::map<std::string, std::tuple<uint64_t, uint64_t> > timer_query_map{};
		VkDevice device_ = VK_NULL_HANDLE;
		uint64_t queryIdx = 0;
	};

	class ScopedGpuTimer
	{
	public:
		ScopedGpuTimer(const char* name, VulkanGpuTimer& timer);
		~ScopedGpuTimer();
	};
	
	class VulkanBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(VulkanBaseRenderer)

		virtual ~VulkanBaseRenderer();

		const std::vector<VkExtensionProperties>& Extensions() const;
		const std::vector<VkLayerProperties>& Layers() const;
		const std::vector<VkPhysicalDevice>& PhysicalDevices() const;

		const class SwapChain& SwapChain() const { return *swapChain_; }
		class Window& Window() { return *window_; }
		const class Window& Window() const { return *window_; }

		bool HasSwapChain() const { return swapChain_.operator bool(); }

		void SetPhysicalDevice(VkPhysicalDevice physicalDevice);
		void Run();
		
		void Start();
		bool Tick();
		void End();

	protected:

		VulkanBaseRenderer(const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);

		const class Device& Device() const { return *device_; }
		class CommandPool& CommandPool() { return *commandPool_; }
		const class DepthBuffer& DepthBuffer() const { return *depthBuffer_; }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return uniformBuffers_; }
		const class GraphicsPipeline& GraphicsPipeline() const { return *graphicsPipeline_; }
		const class FrameBuffer& SwapChainFrameBuffer(const size_t i) const { return swapChainFramebuffers_[i]; }
		const bool CheckerboxRendering() {return checkerboxRendering_;}
		
		virtual const Assets::Scene& GetScene() const = 0;
		virtual Assets::UniformBufferObject GetUniformBufferObject(VkExtent2D extent) const = 0;

		virtual void SetPhysicalDeviceImpl(
			VkPhysicalDevice physicalDevice, 
			std::vector<const char*>& requiredExtensions, 
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures);
		
		virtual void OnDeviceSet();
		virtual void CreateSwapChain();
		virtual void DeleteSwapChain();
		virtual void DrawFrame();
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);

		virtual void OnPreLoadScene() {}
		virtual void OnPostLoadScene() {}

		virtual void OnKey(int key, int scancode, int action, int mods) { }
		virtual void OnCursorPosition(double xpos, double ypos) { }
		virtual void OnMouseButton(int button, int action, int mods) { }
		virtual void OnScroll(double xoffset, double yoffset) { }

		bool isWireFrame_{};
		bool checkerboxRendering_{};
		bool supportRayTracing_ {};
		int denoiseIteration_{};
		int frameCount_{};
		bool supportScreenShot_{};

		DeviceMemory* GetScreenShotMemory() const {return screenShotImageMemory_.get();}
	private:

		void UpdateUniformBuffer(uint32_t imageIndex);
		void RecreateSwapChain();

		const VkPresentModeKHR presentMode_;
		
		std::unique_ptr<class Window> window_;
		std::unique_ptr<class Instance> instance_;
		std::unique_ptr<class DebugUtilsMessenger> debugUtilsMessenger_;
		std::unique_ptr<class Surface> surface_;
		std::unique_ptr<class Device> device_;
		std::unique_ptr<class SwapChain> swapChain_;
		std::vector<Assets::UniformBuffer> uniformBuffers_;
		std::unique_ptr<class DepthBuffer> depthBuffer_;
		std::unique_ptr<class GraphicsPipeline> graphicsPipeline_;
		std::vector<class FrameBuffer> swapChainFramebuffers_;
		std::unique_ptr<class CommandPool> commandPool_;
		std::unique_ptr<class CommandBuffers> commandBuffers_;
		std::vector<class Semaphore> imageAvailableSemaphores_;
		std::vector<class Semaphore> renderFinishedSemaphores_;
		std::vector<class Fence> inFlightFences_;

		std::unique_ptr<Image> screenShotImage_;
		std::unique_ptr<DeviceMemory> screenShotImageMemory_;
		std::unique_ptr<ImageView> screenShotImageView_;

		std::unique_ptr<VulkanGpuTimer> gpuTimer_;
		
		size_t currentFrame_{};

		Fence* fence;
	};

}
