#pragma once

#include "FrameBuffer.hpp"
#include "WindowConfig.hpp"
#include "Assets/UniformBuffer.hpp"
#include <vector>
#include <list>
#include <memory>
#include <unordered_map>
#include <cassert>
#include <chrono>
#include <glm/vec2.hpp>

#include "Image.hpp"

#define SCOPED_GPU_TIMER(name) ScopedGpuTimer scopedGpuTimer(commandBuffer, GpuTimer(), name)
#define SCOPED_CPU_TIMER(name) ScopedCpuTimer scopedCpuTimer(GpuTimer(), name)

namespace Vulkan
{
	namespace PipelineCommon
	{
		class BufferClearPipeline;
	}

	class RenderImage;
}

namespace Assets
{
	class GlobalTexturePool;
	class Scene;
	class UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan 
{
	class VulkanGpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(VulkanGpuTimer)
		
		VulkanGpuTimer(VkDevice device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop);
		virtual ~VulkanGpuTimer();

		void Reset(VkCommandBuffer commandBuffer)
		{
			vkCmdResetQueryPool(commandBuffer, query_pool_timestamps, 0, static_cast<uint32_t>(time_stamps.size()));
			queryIdx = 0;
			started_ = true;
		}

		void FrameEnd(VkCommandBuffer commandBuffer)
		{
			if(started_)
			{
				started_ = false;
			}
			else
			{
				return;
			}
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
		void StartCpuTimer(const char* name)
		{
			if( cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
			{
				cpu_timer_query_map[name] = std::make_tuple(0, 0);
			}
			std::get<0>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		}
		void EndCpuTimer(const char* name)
		{
			assert( cpu_timer_query_map.find(name) != cpu_timer_query_map.end() );
			std::get<1>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		}
		float GetTime(const char* name)
		{
			if(timer_query_map.find(name) == timer_query_map.end())
			{
				return 0;
			}
			return (time_stamps[ std::get<1>(timer_query_map[name]) ] - time_stamps[ std::get<0>(timer_query_map[name])]) * timeStampPeriod_ * 1e-6f;
		}
		float GetCpuTime(const char* name)
		{
			if(cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
			{
				return 0;
			}
			return (std::get<1>(cpu_timer_query_map[name]) - std::get<0>(cpu_timer_query_map[name])) * 1e-6f;
		}
		std::vector<std::tuple<std::string, float> > FetchAllTimes()
		{
			std::list<std::tuple<std::string, float, uint64_t, uint64_t> > order_list;
			std::vector<std::tuple<std::string, float> > result;
			for(auto& [name, query] : timer_query_map)
			{
				order_list.insert(order_list.begin(), (std::make_tuple(name, GetTime(name.c_str()), std::get<0>(query), std::get<1>(query))));
			}

			// sort by tuple 2
			order_list.sort([](const std::tuple<std::string, float, uint64_t, uint64_t>& a, const std::tuple<std::string, float, uint64_t, uint64_t>& b) -> bool
			{
				return std::get<2>(a) < std::get<2>(b);
			});

			uint64_t last_order = 99;
			std::string prefix = "";
			for(auto& [name, time, startIdx, endIdx] : order_list)
			{
				if( startIdx < last_order)
				{
					prefix += " ";
					last_order = endIdx;
				}
				result.push_back(std::make_tuple(prefix + name, time));
			}

			return result;
		}
		
		VkQueryPool query_pool_timestamps = VK_NULL_HANDLE;
		std::vector<uint64_t> time_stamps{};
		std::unordered_map<std::string, std::tuple<uint64_t, uint64_t> > timer_query_map{};
		std::unordered_map<std::string, std::tuple<uint64_t, uint64_t> > cpu_timer_query_map{};
		VkDevice device_ = VK_NULL_HANDLE;
		uint32_t queryIdx = 0;
		float timeStampPeriod_ = 1;
		bool started_ = false;
	};

	class ScopedGpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(ScopedGpuTimer)
		
		ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name ):commandBuffer_(commandBuffer),timer_(timer), name_(name)
		{
			timer_->Start(commandBuffer_, name_.c_str());
		}
		virtual ~ScopedGpuTimer()
		{
			timer_->End(commandBuffer_, name_.c_str());
		}
		VkCommandBuffer commandBuffer_;
		VulkanGpuTimer* timer_;
		std::string name_;
	};

	class ScopedCpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(ScopedCpuTimer)
		
		ScopedCpuTimer(VulkanGpuTimer* timer, const char* name ):timer_(timer), name_(name)
		{
			timer_->StartCpuTimer(name_.c_str());
		}
		virtual ~ScopedCpuTimer()
		{
			timer_->EndCpuTimer( name_.c_str());
		}
		VulkanGpuTimer* timer_;
		std::string name_;
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

		virtual void OnTouch(bool down, double xpos, double ypos) {}
		virtual void OnTouchMove(double xpos, double ypos) {}
		virtual bool GetFocusDistance(float& distance) const {return false;}
		virtual bool GetLastRaycastResult(Assets::RayCastResult& result) const {return false;}
		virtual void SetRaycastRay(glm::vec3 org, glm::vec3 dir) const {};
		
		void CaptureScreenShot();
		void CaptureEditorViewport(VkCommandBuffer commandBuffer, const uint32_t imageIndex);
		void ClearViewport(VkCommandBuffer commandBuffer, const uint32_t imageIndex);
		
		RenderImage& GetRenderImage() const {return *rtEditorViewport_;}

		const std::string& GetRendererType() const {return rendererType_;}
		
	protected:

		VulkanBaseRenderer(const char* rendererType, const WindowConfig& windowConfig, VkPresentModeKHR presentMode, bool enableValidationLayers);

		const class Device& Device() const { return *device_; }
		class CommandPool& CommandPool() { return *commandPool_; }
		const class DepthBuffer& DepthBuffer() const { return *depthBuffer_; }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return uniformBuffers_; }
		const class GraphicsPipeline& GraphicsPipeline() const { return *graphicsPipeline_; }
		const class FrameBuffer& SwapChainFrameBuffer(const size_t i) const { return swapChainFramebuffers_[i]; }
		const bool CheckerboxRendering() {return checkerboxRendering_;}
		class VulkanGpuTimer* GpuTimer() const {return gpuTimer_.get();}
		
		virtual const Assets::Scene& GetScene() const = 0;
		virtual Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const = 0;

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
		virtual void RenderUI(VkCommandBuffer commandBuffer, uint32_t imageIndex) {}

		virtual void BeforeNextFrame() {}
		virtual void AfterRenderCmd() {}
		virtual void AfterPresent() {}

		virtual void OnPreLoadScene() {}
		virtual void OnPostLoadScene() {}

		virtual void OnKey(int key, int scancode, int action, int mods) { }
		virtual void OnCursorPosition(double xpos, double ypos) { }
		virtual void OnMouseButton(int button, int action, int mods) { }
		virtual void OnScroll(double xoffset, double yoffset) { }
		virtual void OnDropFile(int path_count, const char* paths[]) { }
		bool isWireFrame_{};
		bool checkerboxRendering_{};
		bool supportRayTracing_ {};
		bool supportDenoiser_ {};
		int frameCount_{};
		bool supportScreenShot_{};
		bool forceSDR_{};
		bool visualDebug_{};

		std::string rendererType_{};

		Assets::UniformBufferObject lastUBO;

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
		std::unique_ptr<PipelineCommon::BufferClearPipeline> bufferClearPipeline_;
		std::vector<class FrameBuffer> swapChainFramebuffers_;
		std::unique_ptr<class CommandPool> commandPool_;
		std::unique_ptr<class CommandPool> commandPool2_;
		std::unique_ptr<class CommandBuffers> commandBuffers_;
		std::vector<class Semaphore> imageAvailableSemaphores_;
		std::vector<class Semaphore> renderFinishedSemaphores_;
		std::vector<class Fence> inFlightFences_;

		std::unique_ptr<Image> screenShotImage_;
		std::unique_ptr<DeviceMemory> screenShotImageMemory_;
		std::unique_ptr<ImageView> screenShotImageView_;

		std::unique_ptr<RenderImage> rtEditorViewport_;

		std::unique_ptr<VulkanGpuTimer> gpuTimer_;

		std::unique_ptr<Assets::GlobalTexturePool> globalTexturePool_;

		uint32_t currentImageIndex_{};
		size_t currentFrame_{};
		Fence* fence;

		uint64_t uptime {};
	};

}
