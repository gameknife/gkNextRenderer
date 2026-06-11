#pragma once
#include "Engine/Common/CoreMinimal.hpp" // GK_NON_COPIABLE
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include <deque>
#include <glm/vec4.hpp>

namespace NextUI
{

struct Statistics final
{
	VkExtent2D FramebufferSize;
	VkExtent2D RenderSize;
	float FrameRate;
	float FrameTime;
	float RayRate;
	uint32_t TotalSamples;
	uint32_t TotalFrames;
	double RenderTime;
	uint32_t TriCount;
	uint32_t InstanceCount;
	uint32_t NodeCount;
	uint32_t TextureCount;
	uint32_t ComputePassCount;
	bool LoadingStatus;

	mutable std::unordered_map< std::string, std::string> Stats;
};

class UserInterface final
{
public:

	GK_NON_COPIABLE(UserInterface)

	UserInterface(
		NextEngine* engine,
		Vulkan::CommandPool& commandPool, 
		const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer,
		Runtime::Config::UserSettings& userSettings,
		std::function<void()> funcPreConfig,
		std::function<void()> funcInit);
	~UserInterface();

	void PreRender();
	void Render(const Statistics& statistics, VulkanGpuTimer* gpuTimer, Assets::Scene* scene,
	            bool suppressStatisticsOverlay = false);
	void PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx,
	                bool suppressAllUi = false);
	void HandleEvent(const SDL_Event* event);

	bool WantsToCaptureKeyboard() const;
	bool WantsToCaptureMouse() const;

	Runtime::Config::UserSettings& Settings() { return userSettings_; }

	void OnCreateSurface(const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer);
	void OnDestroySurface();

	ImTextureID RequestImTextureId(uint32_t globalTextureId);
	ImTextureID RequestImTextureByName(const std::string& name);

	struct FUiTextureHandle
	{
		ImTextureID textureId = 0;
		ImVec2 pixelSize{0.0f, 0.0f};
		bool valid = false;
	};
	FUiTextureHandle RequestUiTexture(const std::string& path, bool srgb = true);
	
	void DrawPoint(float x, float y, float size, glm::vec4 color);
	void DrawLine(float fromx, float fromy,float tox, float toy, float size, glm::vec4 color);

private:
	struct UiDrawSegment
	{
		uint32_t vertexOffset = 0;
		uint32_t vertexCount = 0;
	};

	struct UiDrawOp
	{
		enum class EType : uint8_t
		{
			Draw,
			Callback,
		};

		EType type = EType::Draw;
		UiDrawSegment segment{};
		const ImDrawList* drawList = nullptr;
		const ImDrawCmd* drawCmd = nullptr;
	};

	struct FUiRenderBuffers;

	NextEngine& GetEngine() {return *engine_;}

	void DrawIndicator(uint32_t frameCount);
	void InitializeRendererBackend();
	void ShutdownRendererBackend();
	void BeginRendererBackendFrame();
	void CreateUiPipeline(const Vulkan::SwapChain& swapChain);
	void DestroyUiPipeline();
	void InitializeFontTexture(Vulkan::CommandPool& commandPool);
	VkPipeline GetOrCreatePlatformViewportPipeline(VkRenderPass renderPass);
	void CreatePlatformViewportWindow(ImGuiViewport* viewport);
	void DestroyPlatformViewportWindow(ImGuiViewport* viewport);
	void ResizePlatformViewportWindow(ImGuiViewport* viewport, ImVec2 size);
	void RenderPlatformViewportWindow(ImGuiViewport* viewport);
	void SwapPlatformViewportBuffers(ImGuiViewport* viewport);
	static UserInterface* GetRendererBackendOwner();
	static void CreatePlatformViewportWindowCallback(ImGuiViewport* viewport);
	static void DestroyPlatformViewportWindowCallback(ImGuiViewport* viewport);
	static void ResizePlatformViewportWindowCallback(ImGuiViewport* viewport, ImVec2 size);
	static void RenderPlatformViewportWindowCallback(ImGuiViewport* viewport, void* renderArg);
	static void SwapPlatformViewportBuffersCallback(ImGuiViewport* viewport, void* renderArg);
	void PrunePlatformViewportRenderBuffers();
	void RenderDrawData(ImDrawData* drawData, VkCommandBuffer commandBuffer, FUiRenderBuffers& renderBuffers,
	                    VkExtent2D framebufferExtent, bool hdrOutput, VkPipeline pipeline);
	static ImTextureID EncodeBindlessTextureId(uint32_t textureIndex);
	static bool DecodeBindlessTextureId(ImTextureID textureId, uint32_t& outTextureIndex);

	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	std::string imguiIniPath_;
	std::vector< Vulkan::FrameBuffer > uiFrameBuffers_;
	struct FUiRenderBuffers
	{
		std::unique_ptr<Vulkan::Buffer> vertexBuffer;
		std::unique_ptr<Vulkan::DeviceMemory> vertexBufferMemory;
		VkDeviceSize vertexBufferSize = 0;
		std::vector<UiDrawOp> drawOps;
	};
	std::vector<FUiRenderBuffers> uiRenderBuffers_;
	VkPipelineLayout uiPipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline uiPipeline_ = VK_NULL_HANDLE;
	VkPipeline uiPlatformViewportPipeline_ = VK_NULL_HANDLE;
	VkRenderPass uiPlatformViewportRenderPass_ = VK_NULL_HANDLE;
	Runtime::Config::UserSettings& userSettings_;	
	
	std::unordered_map<ImGuiID, std::vector<FUiRenderBuffers>> platformUiRenderBuffers_;
	std::unordered_set<std::string> uiTextureLoadRequests_;
	std::unordered_map<std::string, ImVec2> uiTexturePixelSizeCache_;
	uint32_t fontTextureIndex_ = UINT32_MAX;
	std::vector< std::function<void ()> > auxDrawRequest_;
	NextEngine* engine_;
};

}
