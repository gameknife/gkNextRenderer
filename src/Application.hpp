#pragma once

#include "ModelViewController.hpp"
#include "SceneList.hpp"
#include "UserSettings.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Model.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"

#include <fstream>

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
}


template <typename Renderer>
class NextRendererApplication final : public Renderer
{
public:

	VULKAN_NON_COPIABLE(NextRendererApplication)

	NextRendererApplication(const UserSettings& userSettings, const Vulkan::WindowConfig& windowConfig, VkPresentModeKHR presentMode);
	~NextRendererApplication();
	
	void OnTouch(bool down, double xpos, double ypos) override;
	void OnTouchMove(double xpos, double ypos) override;
	
protected:

	const Assets::Scene& GetScene() const override { return *scene_; }
	Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const override;

	void SetPhysicalDeviceImpl(
		VkPhysicalDevice physicalDevice, 
		std::vector<const char*>& requiredExtensions, 
		VkPhysicalDeviceFeatures& deviceFeatures, 
		void* nextDeviceFeatures) override;

	void OnDeviceSet() override;
	void CreateSwapChain() override;
	void DeleteSwapChain() override;
	void DrawFrame() override;
	void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
	void RenderUI(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
	void OnKey(int key, int scancode, int action, int mods) override;
	void OnCursorPosition(double xpos, double ypos) override;
	void OnMouseButton(int button, int action, int mods) override;
	void OnScroll(double xoffset, double yoffset) override;
	void OnDropFile(int path_count, const char* paths[]) override;
	void BeforeNextFrame() override;

	Vulkan::Window& GetWindow() {return Renderer::Window();}

private:

	void LoadScene(uint32_t sceneIndex);
	void CheckAndUpdateBenchmarkState(double prevTime);
	void CheckFramebufferSize() const;

	void Report(int fps, const std::string& sceneName, bool upload_screen, bool save_screen);

	uint32_t sceneIndex_{((uint32_t)~((uint32_t)0))};
	mutable UserSettings userSettings_{};
	UserSettings previousSettings_{};
	Assets::CameraInitialSate cameraInitialSate_{};

	mutable ModelViewController modelViewController_{};

	mutable Assets::UniformBufferObject prevUBO_ {};

	std::unique_ptr<const Assets::Scene> scene_;
	std::unique_ptr<class UserInterface> userInterface_;

	double time_{};

	NextRenderer::EApplicationStatus status_{};

	uint32_t totalFrames_{};

	// Benchmark stats
	double sceneInitialTime_{};
	double periodInitialTime_{};
	uint32_t periodTotalFrames_{};
	uint32_t benchmarkTotalFrames_{};
	uint32_t benchmarkNumber_{0};
	std::ofstream benchmarkCsvReportFile;
};
