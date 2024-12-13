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
#include <glm/vec4.hpp>


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

	EditorInterface( class EditorGameInstance* editor );
	~EditorInterface();

	void Config();
	void Init();
	void Render();

private:
	ImGuiID DockSpaceUI();
	void ToolbarUI();
	void MainWindowGUI(Editor::GUI & gui, Assets::Scene& scene, ImGuiID id, bool firstRun);

	void DrawIndicator(uint32_t frameCount);

	EditorGameInstance* editor_;
	std::unique_ptr<Editor::GUI> editorGUI_;

	ImFont* fontBigIcon_;
	ImFont* fontIcon_;
	bool firstRun;
};

inline UserInterface* GUserInterface = nullptr;
