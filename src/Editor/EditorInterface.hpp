#pragma once
#include <imgui.h>
#include <imgui_internal.h>

#include "Vulkan/Vulkan.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Runtime/UserInterface.hpp"

#include <vector>
#include <memory>
#include <string>
#include <unordered_map>


namespace Assets
{
	class Scene;
}
namespace Editor
{
	struct GUI;
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

class EditorInterface final
{
public:

	VULKAN_NON_COPIABLE(EditorInterface)

	EditorInterface(
		Vulkan::CommandPool& commandPool, 
		const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer);
	~EditorInterface();

	void PreRender();
	void Render(const Statistics& statistics, Vulkan::VulkanGpuTimer* gpuTimer, Assets::Scene* scene);
	void PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx);
	
	bool WantsToCaptureKeyboard() const;
	bool WantsToCaptureMouse() const;
	
	VkDescriptorSet RequestImTextureId(uint32_t globalTextureId);

	void OnCreateSurface(const Vulkan::SwapChain& swapChain, 
		const Vulkan::DepthBuffer& depthBuffer);
	void OnDestroySurface();

	void DrawPoint(float x, float y) {};
	void DrawLine(float fromx, float fromy,float tox, float toy) {};
private:

	ImGuiID DockSpaceUI();
	void ToolbarUI();
	
	void DrawIndicator(uint32_t frameCount);
	std::unique_ptr<Vulkan::DescriptorPool> descriptorPool_;
	std::unique_ptr<Vulkan::RenderPass> renderPass_;
	std::vector< Vulkan::FrameBuffer > uiFrameBuffers_;

	std::unique_ptr<Editor::GUI> editorGUI_;

	ImFont* fontBigIcon_;
	ImFont* fontIcon_;

	bool firstRun;
	
	std::unordered_map<uint32_t, VkDescriptorSet> imTextureIdMap_;
	
};

inline EditorInterface* GUserInterface = nullptr;
