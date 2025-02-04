#pragma once

#include "Common/CoreMinimal.hpp"
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

class NextPhysics;

namespace qjs
{
	class Context;
	class Runtime;
}

class NextEngine;

class NextGameInstanceBase
{
public:
	NextGameInstanceBase(Vulkan::WindowConfig& config, Options& options, NextEngine* engine){}
	virtual ~NextGameInstanceBase() {}
	virtual void OnInit() =0;
	virtual void OnTick(double deltaSeconds) =0;
	virtual void OnDestroy() =0;
	virtual bool OnRenderUI() =0;
	virtual void OnPreConfigUI() {}
	virtual void OnInitUI() {}
	virtual void OnRayHitResponse(Assets::RayCastResult& result) {}

	// camera
	virtual bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const {return false;}

	// scene
	virtual void OnSceneLoaded() {}
	virtual void OnSceneUnloaded() {}

	// input
	virtual bool OnKey(int key, int scancode, int action, int mods) =0;
	virtual bool OnCursorPosition(double xpos, double ypos) =0;
	virtual bool OnMouseButton(int button, int action, int mods) =0;
	virtual bool OnScroll(double xoffset, double yoffset) {return false;}
};

class NextGameInstanceVoid : public NextGameInstanceBase
{
public:
	NextGameInstanceVoid(Vulkan::WindowConfig& config, Options& options, NextEngine* engine):NextGameInstanceBase(config,options,engine){}
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

extern std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);

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

class NextComponent;

class NextActor
{
public:
	std::vector<NextComponent*> components;
};

class NextComponent : std::enable_shared_from_this<NextComponent>
{
public:
	NextComponent() = default;
	std::string name_;
	int id_;
};

class NextEngine final
{
public:

	VULKAN_NON_COPIABLE(NextEngine)

	NextEngine(Options& options, void* userdata = nullptr);
	~NextEngine();

	static NextEngine* instance_;
	static NextEngine* GetInstance() { return instance_; }

	Vulkan::VulkanBaseRenderer& GetRenderer() { return *renderer_; }

	void Start();
	bool Tick();
	void End();
	
	void OnTouch(bool down, double xpos, double ypos);
	void OnTouchMove(double xpos, double ypos);

	Assets::Scene& GetScene() { return *scene_; }
	Assets::Scene* GetScenePtr() { return scene_.get(); }
	UserSettings& GetUserSettings() { return userSettings_; }

	float GetTime() const { return static_cast<float>(time_); }
	float GetDeltaSeconds() const { return static_cast<float>(deltaSeconds_); }
	float GetSmoothDeltaSeconds() const { return static_cast<float>(smoothedDeltaSeconds_); }
	uint32_t GetTotalFrames() const { return totalFrames_; }
	uint32_t GetTestNumber() const { return 20; }

	void RegisterJSCallback(std::function<void(double)> callback);

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
	glm::vec3 ProjectScreenToWorld(glm::vec2 locationSS);
	glm::vec3 ProjectWorldToScreen(glm::vec3 locationWS);
	void GetScreenToWorldRay(glm::vec2 locationSS, glm::vec3& origin, glm::vec3& dir);
	void DrawAuxLine( glm::vec3 from, glm::vec3 to, glm::vec4 color, float size = 1 );
	void DrawAuxBox( glm::vec3 min, glm::vec3 max, glm::vec4 color, float size = 1 );
	void DrawAuxPoint( glm::vec3 location, glm::vec4 color, float size = 1, int32_t durationInTick = 0 );

	// cursor pos
	glm::dvec2 GetMousePos();

	// life cycle
	void RequestClose();
	void RequestMinimize();
	bool IsMaximumed();
	void ToggleMaximize();

	// capture
	void RequestScreenShot(std::string filename);

	// scene loading
	void RequestLoadScene(std::string sceneFileName);

	Vulkan::Window& GetWindow() {return *window_;}
	
	class UserInterface* GetUserInterface() {return userInterface_.get();}

	// monitor info
	glm::ivec2 GetMonitorSize(int monitorIndex = 0) const;

	// gpu raycast
	void RayCastGPU(glm::vec3 rayOrigin, glm::vec3 rayDir, std::function<bool (Assets::RayCastResult rayResult)> callback );

	void SetProgressiveRendering(bool enable) { progressiveRendering_ = enable; }
	bool IsProgressiveRendering() const { return progressiveRendering_; }

	NextRenderer::EApplicationStatus GetEngineStatus() const { return status_; }

	NextPhysics* GetPhysicsEngine() { return physicsEngine_.get(); }
	
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
	
private:
	void LoadScene(std::string sceneFileName);

	void InitJSEngine();
	void TestJSEngine();

	void InitPhysics();

	// engine stuff
	std::unique_ptr<Vulkan::Window> window_;
	std::unique_ptr<Vulkan::VulkanBaseRenderer> renderer_;

	int rendererType = 0;
	mutable UserSettings userSettings_{};
	
	mutable Assets::UniformBufferObject prevUBO_ {};

	std::shared_ptr<Assets::Scene> scene_;

	// integrate interface, may add from application later?
	std::unique_ptr<class UserInterface> userInterface_;

	// timing
	uint32_t totalFrames_{};
	double time_{};
	double deltaSeconds_{};
	double smoothedDeltaSeconds_{};
	bool progressiveRendering_{};

	// game instance
	std::unique_ptr<NextGameInstanceBase> gameInstance_;

	// tasks
	std::vector<TickedTask> tickedTasks_;
	std::vector<FDelayTaskContext> delayedTasks_;

	// audio
	std::unique_ptr<struct ma_engine> audioEngine_;
	std::unordered_map<std::string, std::unique_ptr<ma_sound> > soundMaps_;

	// physics
	std::unique_ptr<NextPhysics> physicsEngine_;

	// package
	std::unique_ptr<Utilities::Package::FPackageFileSystem> packageFileSystem_;

	// engine status
	NextRenderer::EApplicationStatus status_{};

	std::unique_ptr<qjs::Runtime> JSRuntime_;
	std::unique_ptr<qjs::Context> JSContext_;

	std::function<void(double)> JSTickCallback_;
};
