#pragma once
#include <functional>
#include <imgui.h>
#include <imgui_internal.h>
#include "Vulkan/DebugUtilities.hpp"
#include "Vulkan/RenderingPipeline.hpp"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <glm/vec4.hpp>

class NextEngine;

namespace Assets
{
	class Scene;
}

namespace Vulkan
{
	class Window;
	class CommandPool;
	class DepthBuffer;
	class DescriptorPool;
	class FrameBuffer;
	class RenderPass;
	class SwapChain;
	class VulkanGpuTimer;
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
	void Render(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer, Assets::Scene* scene);
	void PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx);
	void HandleEvent(const SDL_Event* event);

	bool WantsToCaptureKeyboard() const;
	bool WantsToCaptureMouse() const;

	UserSettings& Settings() { return userSettings_; }

	void OnCreateSurface(const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer);
	void OnDestroySurface();

	VkDescriptorSet RequestImTextureId(uint32_t globalTextureId);
	VkDescriptorSet RequestImTextureByName(const std::string& name);
	
	static void SetStyle();

	void DrawPoint(float x, float y, float size, glm::vec4 color);
	void DrawLine(float fromx, float fromy,float tox, float toy, float size, glm::vec4 color);
	void SubmitConsoleCommand(const std::string& command);
	bool DrawConsoleCommandInput(const char* label, const char* hint, float width = 0.0f, bool closeConsoleOnSubmit = false,
		bool showMatchPopup = false, const char* matchPopupId = nullptr, bool refreshMatches = true);
	void DrawConsoleLogOutput(const char* childId, const ImVec2& size = ImVec2(0.0f, 0.0f), bool bordered = true);

private:
	NextEngine& GetEngine() {return *engine_;}
	
	void DrawOverlay(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer);
	void DrawIndicator(uint32_t frameCount);
	void DrawConsoleWindow();
	void RefreshConsoleMatches(size_t matchLimit);
	void DrawConsoleMatchPopup(float width, const char* popupId);
	static int ConsoleInputTextCallback(ImGuiInputTextCallbackData* data);
	int HandleConsoleInputTextCallback(ImGuiInputTextCallbackData* data);
	void DrawConsoleLogOutputInternal(const char* childId, const ImVec2& size, bool bordered);
	std::unique_ptr<Vulkan::DescriptorPool> descriptorPool_;
	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	std::vector< Vulkan::FrameBuffer > uiFrameBuffers_;
	UserSettings& userSettings_;	
	
	std::unordered_map<uint32_t, VkDescriptorSet> imTextureIdMap_;
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

	NextEngine* engine_;
};
