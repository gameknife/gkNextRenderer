#pragma once

#include "ModelViewController.hpp"
#include "SceneList.hpp"
#include "UserSettings.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Model.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Options.hpp"

class BenchMarker;

namespace NextRenderer
{
	enum class EApplicationStatus
	{
		Starting,
		Running,
		Loading,
		AsyncPreparing,
	};

	std::string GetBuildVersion();

	Vulkan::VulkanBaseRenderer* CreateRenderer(uint32_t rendererType, Vulkan::Window* window, const VkPresentModeKHR presentMode, const bool enableValidationLayers);
}

class NextRendererApplication final
{
public:

	VULKAN_NON_COPIABLE(NextRendererApplication)

	NextRendererApplication(const Options& options, void* userdata = nullptr);
	~NextRendererApplication();

	Vulkan::VulkanBaseRenderer& GetRenderer() { return *renderer_; }

	void Start();
	bool Tick();
	void End();

public:
	void OnTouch(bool down, double xpos, double ypos);
	void OnTouchMove(double xpos, double ypos);
	
protected:
	
	Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const;
	void OnRendererDeviceSet();
	void OnRendererCreateSwapChain();
	void OnRendererDeleteSwapChain();
	void OnRendererPostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex);
	void OnRendererBeforeNextFrame();

	const Assets::Scene& GetScene() const { return *scene_; }
	
	void OnKey(int key, int scancode, int action, int mods);
	void OnCursorPosition(double xpos, double ypos);
	void OnMouseButton(int button, int action, int mods);
	void OnScroll(double xoffset, double yoffset);
	void OnDropFile(int path_count, const char* paths[]);
	
	Vulkan::Window& GetWindow() {return *window_;}

	
private:

	void LoadScene(uint32_t sceneIndex);
	void TickBenchMarker();
	void CheckFramebufferSize();

	void Report(int fps, const std::string& sceneName, bool upload_screen, bool save_screen);

	std::unique_ptr<Vulkan::Window> window_;
	std::unique_ptr<Vulkan::VulkanBaseRenderer> renderer_;
	std::unique_ptr<BenchMarker> benchMarker_;

	int rendererType = 0;
	uint32_t sceneIndex_{((uint32_t)~((uint32_t)0))};
	mutable UserSettings userSettings_{};
	UserSettings previousSettings_{};
	Assets::CameraInitialSate cameraInitialSate_{};

	mutable ModelViewController modelViewController_{};

	mutable Assets::UniformBufferObject prevUBO_ {};

	std::shared_ptr<Assets::Scene> scene_;
#if WITH_EDITOR
	std::unique_ptr<class EditorInterface> userInterface_;
#else
	std::unique_ptr<class UserInterface> userInterface_;
#endif

	NextRenderer::EApplicationStatus status_{};

	uint32_t totalFrames_{};
	double time_{};

	glm::vec2 mousePos_ {};
};
