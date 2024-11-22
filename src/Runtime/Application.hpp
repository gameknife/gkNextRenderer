#pragma once

#include "Common/CoreMinimal.hpp"
#include "ModelViewController.hpp"
#include "SceneList.hpp"
#include "UserSettings.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Model.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Options.hpp"
#include "ThirdParty/miniaudio/miniaudio.h"
#include "Utilities/FileHelper.hpp"

class BenchMarker;
class NextRendererApplication;

class NextGameInstanceBase
{
public:
	NextGameInstanceBase(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine){}
	virtual ~NextGameInstanceBase() {}
	virtual void OnInit() =0;
	virtual void OnTick(double deltaSeconds) =0;
	virtual void OnDestroy() =0;
	virtual bool OnRenderUI() =0;
	virtual void OnInitUI() {}
	virtual void OnRayHitResponse(Assets::RayCastResult& result) =0;

	virtual bool OverrideModelView(glm::mat4& OutMatrix) const {return false;}

	virtual void OnSceneLoaded() {}
	virtual void OnSceneUnloaded() {}

	virtual bool OnKey(int key, int scancode, int action, int mods) =0;
	virtual bool OnCursorPosition(double xpos, double ypos) =0;
	virtual bool OnMouseButton(int button, int action, int mods) =0;
};

class NextGameInstanceVoid : public NextGameInstanceBase
{
public:
	NextGameInstanceVoid(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine):NextGameInstanceBase(config,options,engine){}
	~NextGameInstanceVoid() override = default;
	
	void OnInit() override {}
	void OnTick(double deltaSeconds) override {}
	void OnDestroy() override {}
	bool OnRenderUI() override {return false;}
	void OnRayHitResponse(Assets::RayCastResult& result) override {}
	
	bool OnKey(int key, int scancode, int action, int mods) override {return false;}
	bool OnCursorPosition(double xpos, double ypos) override {return false;}
	bool OnMouseButton(int button, int action, int mods) override {return false;}
};

extern std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine);

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


typedef std::function<bool (double DeltaSeconds)> TickedTask;
typedef std::function<bool ()> DelayedTask;

struct FDelayTaskContext
{
	double triggerTime;
	double loopTime;
	DelayedTask task;
};

class NextRendererApplication final
{
public:

	VULKAN_NON_COPIABLE(NextRendererApplication)

	NextRendererApplication(Options& options, void* userdata = nullptr);
	~NextRendererApplication();

	Vulkan::VulkanBaseRenderer& GetRenderer() { return *renderer_; }

	void Start();
	bool Tick();
	void End();
	
	void OnTouch(bool down, double xpos, double ypos);
	void OnTouchMove(double xpos, double ypos);

	Assets::Scene& GetScene() { return *scene_; }
	UserSettings& GetUserSettings() { return userSettings_; }

	float GetTime() const { return static_cast<float>(time_); }
	float GetDeltaSeconds() const { return static_cast<float>(deltaSeconds_); }
	uint32_t GetTotalFrames() const { return totalFrames_; }

	// remove till return true
	void AddTickedTask( TickedTask task ) { tickedTasks_.push_back(task); }
	void AddTimerTask( double delay, DelayedTask task );

	// sound
	void PlaySound(const std::string& soundName, bool loop = false, float volume = 1.0f);
	void PauseSound(const std::string& soundName, bool pause);
	bool IsSoundPlaying(const std::string& soundName);

	// screen shot
	void SaveScreenShot(const std::string& filename, int x, int y, int width, int height);

	// pak
	Utilities::Package::FPackageFileSystem& GetPakSystem() { return *packageFileSystem_; }

	// fast aux drawing using interface
	glm::vec3 ProjectWorldToScreen(glm::vec3 locationWS);
	void DrawAuxLine( glm::vec3 from, glm::vec3 to, glm::vec4 color, float size = 1 );
	void DrawAuxBox( glm::vec3 min, glm::vec3 max, glm::vec4 color, float size = 1 );
	void DrawAuxPoint( glm::vec3 location, glm::vec4 color, float size = 1 );

	// life cycle
	void RequestClose();
	void RequestMinimize();
	bool IsMaximumed();
	void ToggleMaximize();

	// capture
	void RequestScreenShot(std::string filename);
	
#if !WITH_EDITOR
	class UserInterface* GetUserInterface() {return userInterface_.get();};
#endif
protected:
	
	Assets::UniformBufferObject GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent);
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
	double deltaSeconds_{};

	glm::vec2 mousePos_ {};

	std::unique_ptr<NextGameInstanceBase> gameInstance_;

	std::vector<TickedTask> tickedTasks_;
	std::vector<FDelayTaskContext> delayedTasks_;

	std::unique_ptr<struct ma_engine> audioEngine_;
	std::unordered_map<std::string, std::unique_ptr<ma_sound> > soundMaps_;
	
	std::unique_ptr<Utilities::Package::FPackageFileSystem> packageFileSystem_;
};
