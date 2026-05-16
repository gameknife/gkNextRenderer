#pragma once
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include "Vulkan/DebugUtilities.hpp"
#include "Vulkan/RenderingPipeline.hpp"
#include <vector>
#include <array>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <glm/vec4.hpp>

class NextEngine;
class VulkanGpuTimer;

namespace Assets
{
	class Scene;
}

namespace Vulkan
{
	class Window;
	class CommandPool;
	class Buffer;
	class DepthBuffer;
	class DeviceMemory;
	class FrameBuffer;
	class RenderPass;
	class SwapChain;
	class RenderImage;
}

struct UserSettings;

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

	struct FUiRenderBuffers;

	VULKAN_NON_COPIABLE(UserInterface)

	UserInterface(
		NextEngine* engine,
		Vulkan::CommandPool& commandPool, 
		const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer,
		UserSettings& userSettings,
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

	UserSettings& Settings() { return userSettings_; }

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
	
	static void SetStyle();

	void DrawPoint(float x, float y, float size, glm::vec4 color);
	void DrawLine(float fromx, float fromy,float tox, float toy, float size, glm::vec4 color);
	void SubmitConsoleCommand(const std::string& command);
	bool DrawConsoleCommandInput(const char* label, const char* hint, float width = 0.0f, bool closeConsoleOnSubmit = false,
		bool showMatchPopup = false, const char* matchPopupId = nullptr, bool refreshMatches = true);
	void DrawConsoleLogOutput(const char* childId, const ImVec2& size = ImVec2(0.0f, 0.0f), bool bordered = true);
	void ToggleConsole();
	bool IsConsoleOpen() const { return showConsole_; }

private:
	NextEngine& GetEngine() {return *engine_;}
	
	void DrawOverlay(const Statistics& statistics, VulkanGpuTimer* gpuTimer);
	void DrawIndicator(uint32_t frameCount);
	void DrawConsoleWindow();
	void RefreshConsoleMatches(size_t matchLimit);
	void DrawConsoleMatchPopup(float width, const char* popupId);
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
	static int ConsoleInputTextCallback(ImGuiInputTextCallbackData* data);
	int HandleConsoleInputTextCallback(ImGuiInputTextCallbackData* data);
	void DrawConsoleLogOutputInternal(const char* childId, const ImVec2& size, bool bordered);

	struct TimingSample
	{
		double sampleTime = 0.0;
		float milliseconds = 0.0f;
	};

	struct TimingHistory
	{
		std::deque<TimingSample> samples;
		std::string displayName;
		double lastSeenTime = 0.0;
		float average = 0.0f;
		float minimum = 0.0f;
		float maximum = 0.0f;
		int depth = 0;
		uint32_t displayOrder = 0;
	};

	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	std::vector< Vulkan::FrameBuffer > uiFrameBuffers_;
	struct FUiRenderBuffers
	{
		std::unique_ptr<Vulkan::Buffer> vertexBuffer;
		std::unique_ptr<Vulkan::DeviceMemory> vertexBufferMemory;
		VkDeviceSize vertexBufferSize = 0;
	};
	std::vector<FUiRenderBuffers> uiRenderBuffers_;
	VkPipelineLayout uiPipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline uiPipeline_ = VK_NULL_HANDLE;
	VkPipeline uiPlatformViewportPipeline_ = VK_NULL_HANDLE;
	VkRenderPass uiPlatformViewportRenderPass_ = VK_NULL_HANDLE;
	UserSettings& userSettings_;	
	
	std::unordered_map<ImGuiID, std::vector<FUiRenderBuffers>> platformUiRenderBuffers_;
	std::unordered_set<std::string> uiTextureLoadRequests_;
	std::unordered_map<std::string, ImVec2> uiTexturePixelSizeCache_;
	uint32_t fontTextureIndex_ = UINT32_MAX;
	std::vector< std::function<void ()> > auxDrawRequest_;
	std::vector<std::string> consoleHistory_;
	std::vector<std::string> consoleMatches_;
	std::string consoleInput_;
	std::string consoleLastInput_;
	std::string consoleCompletionBase_;
	int consoleHistoryIndex_ = -1;
	int consoleMatchIndex_ = 0;
	bool consoleSkipEditReset_ = false;
	bool showConsole_ = false;
	bool consoleScrollToBottom_ = false;
	bool requestConsoleFocus_ = false;
	uint64_t consoleLogRevision_ = 0;
	std::unordered_map<std::string, TimingHistory> gpuTimeHistory_;
	std::unordered_map<std::string, TimingHistory> cpuTimeHistory_;

	static constexpr int kOverlaySparklineSampleCount = 64;
	std::array<float, kOverlaySparklineSampleCount> frameRateSamples_{};
	std::array<float, kOverlaySparklineSampleCount> frameTimeSamples_{};
	int overlaySampleCursor_ = 0;
	int overlaySampleFilled_ = 0;

	NextEngine* engine_;
};
