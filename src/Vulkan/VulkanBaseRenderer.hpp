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
#include <functional>
#include <map>
#include <glm/vec2.hpp>

#include "Image.hpp"
#include "Options.hpp"
#include "Assets/Scene.hpp"

#define SCOPED_GPU_TIMER_FOLDER(name, folder) ScopedGpuTimer scopedGpuTimer(commandBuffer, GpuTimer(), name, folder)
#define SCOPED_GPU_TIMER(name) ScopedGpuTimer scopedGpuTimer(commandBuffer, GpuTimer(), name)
#define SCOPED_CPU_TIMER(name) ScopedCpuTimer scopedCpuTimer(GpuTimer(), name)
#define BENCH_MARK_CHECK() if(!GOption->HardwareQuery) return

namespace Vulkan
{
	namespace PipelineCommon
	{
		class BufferClearPipeline;
		class SoftAmbientCubeGenPipeline;
		class SimpleComposePipeline;
		class GPUCullPipeline;
		class VisualDebuggerPipeline;
	}

	class RenderImage;
	class DescriptorSetManager;
}

namespace Assets
{
	class GlobalTexturePool;
	class Scene;
	struct UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan 
{
	enum ERendererType
	{
		ERT_PathTracing,
		ERT_Hybrid,
		ERT_ModernDeferred,
		ERT_LegacyDeferred,
		ERT_VoxelTracing,
	};
	
	class VulkanGpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(VulkanGpuTimer)
		
		VulkanGpuTimer(VkDevice device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop);
		virtual ~VulkanGpuTimer();

		void Reset(VkCommandBuffer commandBuffer)
		{
			BENCH_MARK_CHECK();
			vkCmdResetQueryPool(commandBuffer, query_pool_timestamps, 0, static_cast<uint32_t>(time_stamps.size()));
			queryIdx = 0;
			started_ = true;

			for(auto& [name, query] : gpu_timer_query_map)
			{
				std::get<1>(gpu_timer_query_map[name]) = 0;
				std::get<0>(gpu_timer_query_map[name]) = 0;
			}
		}

		void CpuFrameEnd()
		{
			// iterate the cpu_timer_query_map
			for(auto& [name, query] : cpu_timer_query_map)
			{
				std::get<2>(cpu_timer_query_map[name]) = std::get<1>(cpu_timer_query_map[name]) - std::get<0>(cpu_timer_query_map[name]);
			}
		}

		void FrameEnd(VkCommandBuffer commandBuffer)
		{
			BENCH_MARK_CHECK();
			if(started_)
			{
				started_ = false;
			}
			else
			{
				return;
			}
			vkGetQueryPoolResults(
				device_,
				query_pool_timestamps,
				0,
				queryIdx,
				time_stamps.size() * sizeof(uint64_t),
				time_stamps.data(),
				sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

			for(auto& [name, query] : gpu_timer_query_map)
			{
				std::get<2>(gpu_timer_query_map[name]) = (time_stamps[ std::get<1>(gpu_timer_query_map[name]) ] - time_stamps[ std::get<0>(gpu_timer_query_map[name])]);
			}
		}

		void Start(VkCommandBuffer commandBuffer, const char* name)
		{
			BENCH_MARK_CHECK();
			if( gpu_timer_query_map.find(name) == gpu_timer_query_map.end())
			{
				gpu_timer_query_map[name] = std::make_tuple(0, 0, 0);
			}
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
			std::get<0>(gpu_timer_query_map[name]) = queryIdx;
			queryIdx++;
		}
		void End(VkCommandBuffer commandBuffer, const char* name)
		{
			BENCH_MARK_CHECK();
			assert( gpu_timer_query_map.find(name) != gpu_timer_query_map.end() );
			vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool_timestamps, queryIdx);
			std::get<1>(gpu_timer_query_map[name]) = queryIdx;
			queryIdx++;
		}
		void StartCpuTimer(const char* name)
		{
			BENCH_MARK_CHECK();
			if( cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
			{
				cpu_timer_query_map[name] = std::make_tuple(0, 0, 0);
			}
			std::get<0>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		}
		void EndCpuTimer(const char* name)
		{
			BENCH_MARK_CHECK();
			assert( cpu_timer_query_map.find(name) != cpu_timer_query_map.end() );
			std::get<1>(cpu_timer_query_map[name]) = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		}
		float GetGpuTime(const char* name)
		{
			if(gpu_timer_query_map.find(name) == gpu_timer_query_map.end())
			{
				return 0;
			}
			return std::get<2>(gpu_timer_query_map[name]) * timeStampPeriod_ * 1e-6f;
		}
		float GetCpuTime(const char* name)
		{
			if(cpu_timer_query_map.find(name) == cpu_timer_query_map.end())
			{
				return 0;
			}
			return std::get<2>(cpu_timer_query_map[name]) * 1e-6f;
		}
		std::vector<std::tuple<std::string, float> > FetchAllTimes( int maxStack )
		{
			std::list<std::tuple<std::string, float, uint64_t, uint64_t> > order_list;
			std::vector<std::tuple<std::string, float> > result;
			for(auto& [name, query] : gpu_timer_query_map)
			{
				if ( std::get<2>(gpu_timer_query_map[name]) == 0)
				{
					continue;
				}
				order_list.insert(order_list.begin(), (std::make_tuple(name, GetGpuTime(name.c_str()), std::get<0>(query), std::get<1>(query))));
			}

			// sort by tuple 2
			order_list.sort([](const std::tuple<std::string, float, uint64_t, uint64_t>& a, const std::tuple<std::string, float, uint64_t, uint64_t>& b) -> bool
			{
				return std::get<2>(a) < std::get<2>(b);
			});

			// 创建一个栈来存储活动的计时区间
			std::vector<std::tuple<std::string, float, uint64_t, uint64_t>> activeTimers;
			std::string prefix = "";

			for(auto& [name, time, startIdx, endIdx] : order_list)
			{
			    // 检查是否有计时区间已结束
			    while (!activeTimers.empty() && std::get<3>(activeTimers.back()) < startIdx) {
			        activeTimers.pop_back();
			    }

				// 计算当前计时的嵌套深度
				size_t stackDepth = activeTimers.size();
			    
			    // 添加当前计时到活动计时栈
			    activeTimers.push_back(std::make_tuple(name, time, startIdx, endIdx));
				
			    // 构建缩进前缀
			    prefix = " ";
			    for (size_t i = 0; i < stackDepth; i++) {
			        prefix += "-";
			    }
			    
			    // 添加结果
				if (maxStack > stackDepth)
				{
					result.push_back(std::make_tuple(prefix + name, time));
				}
			}
			return result;
		}
		
		VkQueryPool query_pool_timestamps = VK_NULL_HANDLE;
		std::vector<uint64_t> time_stamps{};
		std::unordered_map<std::string, std::tuple<uint64_t, uint64_t, uint64_t> > gpu_timer_query_map{};
		std::unordered_map<std::string, std::tuple<uint64_t, uint64_t, uint64_t> > cpu_timer_query_map{};
		VkDevice device_ = VK_NULL_HANDLE;
		uint32_t queryIdx = 0;
		float timeStampPeriod_ = 1;
		bool started_ = false;
	};

	class ScopedGpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(ScopedGpuTimer)

		ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name, const char* foldername ):commandBuffer_(commandBuffer),timer_(timer), name_(name)
		{
			timer_->Start(commandBuffer_, name_.c_str());
			folderTimer = true;
			PushFolder(foldername);
		}
		ScopedGpuTimer(VkCommandBuffer commandBuffer, VulkanGpuTimer* timer, const char* name ):commandBuffer_(commandBuffer),timer_(timer), name_(name)
		{
			timer_->Start(commandBuffer_, (folderName_ + name_).c_str());
		}
		virtual ~ScopedGpuTimer()
		{
			if (folderTimer)
			{
				PopFolder();
			}
			timer_->End(commandBuffer_, (folderName_ + name_).c_str());
		}
		VkCommandBuffer commandBuffer_;
		VulkanGpuTimer* timer_;
		std::string name_;
		bool folderTimer = false;

		static std::string folderName_;
		static void PushFolder(const std::string& name)
		{
			folderName_ = name;
		}
		static void PopFolder()
		{
			folderName_ = "";
		}
	};

	inline std::string ScopedGpuTimer::folderName_;

	class ScopedCpuTimer
	{
	public:
		DEFAULT_NON_COPIABLE(ScopedCpuTimer)
		
		ScopedCpuTimer(VulkanGpuTimer* timer, const char* name ):timer_(timer), name_(name)
		{
			timer_->StartCpuTimer((folderName_ + name_).c_str());
		}
		virtual ~ScopedCpuTimer()
		{
			timer_->EndCpuTimer( (folderName_ + name_).c_str());
		}
		VulkanGpuTimer* timer_;
		std::string name_;

		static std::string folderName_;
		static void PushFolder(const std::string& name)
		{
			folderName_ = name;
		}
		static void PopFolder(const std::string& name)
		{
			folderName_ = "";
		}
	};

	inline std::string ScopedCpuTimer::folderName_;
	
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
		
		void Start();
		void End();
		
		virtual void SetRaycastRay(glm::vec3 org, glm::vec3 dir, std::function<bool(Assets::RayCastResult)> callback) {};
		
		void CaptureScreenShot();
		void CaptureEditorViewport(VkCommandBuffer commandBuffer, const uint32_t imageIndex);
		void ClearViewport(VkCommandBuffer commandBuffer, const uint32_t imageIndex);
		
		RenderImage& GetRenderImage() const {return *rtEditorViewport_;}

		virtual void DrawFrame();


		VulkanBaseRenderer(Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers);

		const class Device& Device() const { return *device_; }
		class CommandPool& CommandPool() { return *commandPool_; }
		const class DepthBuffer& DepthBuffer() const { return *depthBuffer_; }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return uniformBuffers_; }
		const class GraphicsPipeline& GraphicsPipeline() const { return *graphicsPipeline_; }
		const class FrameBuffer& SwapChainFrameBuffer(const size_t i) const { return swapChainFramebuffers_[i]; }
		const bool CheckerboxRendering() {return checkerboxRendering_;}
		class VulkanGpuTimer* GpuTimer() const {return gpuTimer_.get();}
		
		Assets::Scene& GetScene();
		void SetScene(std::shared_ptr<Assets::Scene> scene);
		virtual Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const;

		int FrameCount() const {return frameCount_;}

		virtual void SetPhysicalDeviceImpl(
			VkPhysicalDevice physicalDevice, 
			std::vector<const char*>& requiredExtensions, 
			VkPhysicalDeviceFeatures& deviceFeatures,
			void* nextDeviceFeatures);
		
		virtual void OnDeviceSet();
		virtual void CreateSwapChain();
		virtual void DeleteSwapChain();
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex);

		virtual void BeforeNextFrame();

		virtual void AfterRenderCmd() {}
		virtual void AfterPresent() {}
		virtual void AfterUpdateScene() {}

		virtual void OnPreLoadScene() {}
		virtual void OnPostLoadScene() {}

		void InitializeBarriers(VkCommandBuffer commandBuffer);
		
		bool VisualDebug() const {return visualDebug_;}

		virtual void RegisterLogicRenderer(ERendererType type);
		virtual void SwitchLogicRenderer(ERendererType type);

		ERendererType CurrentLogicRendererType() const
		{
			return currentLogicRenderer_;
		}

		DescriptorSetManager& GetRTDescriptorSetManager() const
		{
			return *rtDescriptorSetManager_;
		}
		
		// Callbacks
		std::function<void()> DelegateOnDeviceSet;
		std::function<void()> DelegateCreateSwapChain;
		std::function<void()> DelegateDeleteSwapChain;
		std::function<void()> DelegateBeforeNextTick;
		std::function<Assets::UniformBufferObject(VkOffset2D, VkExtent2D)> DelegateGetUniformBufferObject;
		std::function<void(VkCommandBuffer, uint32_t)> DelegatePostRender;

		DeviceMemory* GetScreenShotMemory() const {return screenShotImageMemory_.get();}
	
		std::weak_ptr<Assets::Scene> scene_;
		
		bool checkerboxRendering_{};
		bool supportRayTracing_ {};
		bool supportDenoiser_ {};
		bool supportRayCast_ {};
		bool showWireframe_ {};
		int frameCount_{};
		bool forceSDR_{};
		bool visualDebug_{};

		// this texture could pass to global scope, it may contained by base renderer
		std::unique_ptr<RenderImage> rtDenoised;
		std::unique_ptr<RenderImage> rtOutput;
		std::unique_ptr<RenderImage> rtAccumlation;
		std::unique_ptr<RenderImage> rtVisibility;
		std::unique_ptr<RenderImage> rtObject0;
		std::unique_ptr<RenderImage> rtObject1;
		std::unique_ptr<RenderImage> rtPrevDepth;
		std::unique_ptr<RenderImage> rtMotionVector_;
		std::unique_ptr<RenderImage> rtAlbedo_;
		std::unique_ptr<RenderImage> rtNormal_;
		std::unique_ptr<RenderImage> rtShaderTimer_;
			
	protected:
		Assets::UniformBufferObject lastUBO;
	
	private:

		void UpdateUniformBuffer(uint32_t imageIndex);
		void RecreateSwapChain();

		const VkPresentModeKHR presentMode_;

		std::map< ERendererType, std::unique_ptr<class LogicRendererBase> > logicRenderers_;
		ERendererType currentLogicRenderer_;
		
		class Window* window_;
		std::unique_ptr<class Instance> instance_;
		std::unique_ptr<class DebugUtilsMessenger> debugUtilsMessenger_;
		std::unique_ptr<class Surface> surface_;
		std::unique_ptr<class Device> device_;
		std::unique_ptr<class SwapChain> swapChain_;
		std::vector<Assets::UniformBuffer> uniformBuffers_;
		std::unique_ptr<class DepthBuffer> depthBuffer_;
		std::unique_ptr<class GraphicsPipeline> graphicsPipeline_;
		std::unique_ptr<PipelineCommon::BufferClearPipeline> bufferClearPipeline_;
		std::unique_ptr<PipelineCommon::SimpleComposePipeline> simpleComposePipeline_;
		std::unique_ptr<PipelineCommon::VisualDebuggerPipeline> visualDebuggerPipeline_;
		std::vector<class FrameBuffer> swapChainFramebuffers_;
		std::unique_ptr<class CommandPool> commandPool_;
		std::unique_ptr<class CommandPool> commandPool2_;
		std::unique_ptr<class CommandBuffers> commandBuffers_;
		std::vector<class Semaphore> imageAvailableSemaphores_;
		std::vector<class Semaphore> renderFinishedSemaphores_;
		std::vector<class Fence> inFlightFences_;

		std::unique_ptr<PipelineCommon::SoftAmbientCubeGenPipeline> softAmbientCubeGenPipeline_;
		std::unique_ptr<PipelineCommon::GPUCullPipeline> gpuCullPipeline_;
		
		std::unique_ptr<Image> screenShotImage_;
		std::unique_ptr<DeviceMemory> screenShotImageMemory_;
		std::unique_ptr<ImageView> screenShotImageView_;

		std::unique_ptr<RenderImage> rtEditorViewport_;

		std::unique_ptr<VulkanGpuTimer> gpuTimer_;

		std::unique_ptr<Assets::GlobalTexturePool> globalTexturePool_;

		std::unique_ptr<Vulkan::DescriptorSetManager> rtDescriptorSetManager_;

		uint32_t currentImageIndex_{};
		size_t currentFrame_{};
		Fence* fence;

		uint64_t uptime {};
	};
	
	class LogicRendererBase
	{
	public:
		LogicRendererBase( VulkanBaseRenderer& baseRender );
		virtual ~LogicRendererBase() {};

		virtual void OnDeviceSet() {};
		virtual void CreateSwapChain(const VkExtent2D& extent) {};
		virtual void DeleteSwapChain() {};
		virtual void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) {};
		virtual void BeforeNextFrame() {};
		
		VulkanBaseRenderer& baseRender_;
		template<typename T>
		T& GetBaseRender() { return static_cast<T&>(baseRender_); }

		const class SwapChain& SwapChain() const { return baseRender_.SwapChain(); }
		class Window& Window() { return baseRender_.Window(); }
		
		const class Device& Device() const { return baseRender_.Device(); }
		class CommandPool& CommandPool() { return baseRender_.CommandPool(); }
		const class DepthBuffer& DepthBuffer() const { return baseRender_.DepthBuffer(); }
		const std::vector<Assets::UniformBuffer>& UniformBuffers() const { return baseRender_.UniformBuffers(); }
		class VulkanGpuTimer* GpuTimer() const {return baseRender_.GpuTimer();}
		
		const Assets::Scene& GetScene() {return baseRender_.GetScene();}

		int FrameCount() const {return baseRender_.FrameCount();}

		bool VisualDebug() const {return baseRender_.VisualDebug();}
	};

}
