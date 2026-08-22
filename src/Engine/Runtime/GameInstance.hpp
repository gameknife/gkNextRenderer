#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Runtime/Editor/MultiViewportBackend.hpp"
#include "Engine/Runtime/Editor/UiFrame.hpp"
#include "Engine/Utilities/Glm.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

namespace Runtime::Agent
{
    class FAgentQueryRegistry;
}

class NextGameInstanceBase
{
public:
    struct FGameUiFrameContext
    {
        enum class ESurfaceKind : uint8_t
        {
            MainWindow,
            RemoteView,
        };

        ESurfaceKind surfaceKind = ESurfaceKind::MainWindow;
        std::string_view sessionId{};
        VkExtent2D framebufferExtent{};
        const Assets::Camera* viewCamera = nullptr;
        bool allowWindowCommands = true;
        NextUI::FUiFramePolicy policy{};
    };

    struct FRemoteViewActionContext
    {
        std::string sessionId;
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
    };

    NextGameInstanceBase(Vulkan::WindowConfig&, Runtime::Config::Options&, NextEngine* engine) : engine_(engine) {}
    virtual ~NextGameInstanceBase() = default;

    NextEngine& GetEngine() { return *engine_; }
    NextEngine& GetEngine() const { return *engine_; }

    virtual void OnInit() = 0;
    virtual void OnTick(double deltaSeconds) = 0;
    virtual void OnDestroy() = 0;
    virtual bool OnRenderUI() = 0;
    virtual bool OnRenderUI(const FGameUiFrameContext& context)
    {
        (void)context;
        return OnRenderUI();
    }
    virtual NextUI::FUiFrameResult RenderUiFrame(const FGameUiFrameContext& context)
    {
        return NextUI::FUiFrameResult::FromLegacyHandled(OnRenderUI(context));
    }
    virtual bool ShouldRenderUiDuringScreenshot() const { return false; }
    virtual void OnPreConfigUI() {}
    virtual void OnInitUI() {}
    virtual void OnRemoteUiSessionClosed(std::string_view sessionId) { (void)sessionId; }
    virtual std::unique_ptr<NextUI::IMultiViewportBackend> CreateMultiViewportBackend() { return nullptr; }
    virtual void OnRayHitResponse(Assets::RayCastResult& result) {}
    virtual void ConfigureCVars(NextCVar::FCVarSystem& cvars) {}
    virtual void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) { (void)reg; }
    virtual float GetGraphicsDebugPanelTopOffset() const { return 0.0f; }
    virtual void DrawAdditionalPhysicsDebugOverlay(const Assets::Camera& camera) const {}

    // Camera override hook.
    virtual bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const { return false; }

    // Scene lifecycle hooks.
    virtual void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                    std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                                    std::vector<Assets::LightObject>& lights,
                                    std::vector<Assets::AnimationTrack>& tracks)
    {
    }
    virtual void OnSceneLoaded() {}
    virtual void OnSceneUnloaded() {}

    // Application-owned debug shortcuts; engine-owned F1/F2/F3 are handled by NextEngine.
    virtual bool SupportsAppDebugShortcut(SDL_Keycode key) const { return false; }
    virtual bool IsAppDebugShortcutActive(SDL_Keycode key) const { return false; }
    virtual bool SetAppDebugShortcutActive(SDL_Keycode key, bool active) { return false; }
    virtual bool WantsMouseInputWhenUiCaptures() const { return false; }
    virtual bool OnKey(SDL_Event& event) { return false; }
    virtual bool OnCursorPosition(double xpos, double ypos) { return false; }
    virtual bool OnMouseButton(SDL_Event& event) { return false; }
    virtual bool OnTouch(SDL_Event& event) { return false; }
    virtual bool OnScroll(double xoffset, double yoffset) { return false; }
    virtual bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
                                int16_t leftTrigger, int16_t rightTrigger)
    {
        return false;
    }
    virtual bool OnRemoteViewAction(const FRemoteViewActionContext& context, std::string_view action)
    {
        (void)context;
        (void)action;
        return false;
    }

private:
    NextEngine* engine_ = nullptr;

protected:
    static void ConfigureWindow(Vulkan::WindowConfig& config,
                                Runtime::Config::Options& options,
                                std::string_view title,
                                int width,
                                int height,
                                bool forceSDR)
    {
        config.Title = std::string(title);
        config.Width = width;
        config.Height = height;
        config.ForceSDR = forceSDR;
        options.Width = width;
        options.Height = height;
        options.ForceSDR = forceSDR;
    }
};

class NextGameInstanceVoid : public NextGameInstanceBase
{
public:
    NextGameInstanceVoid(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
        NextGameInstanceBase(config, options, engine)
    {
    }
    ~NextGameInstanceVoid() override = default;

    void OnInit() override {}
    void OnTick(double deltaSeconds) override {}
    void OnDestroy() override {}
    bool OnRenderUI() override { return false; }
};

extern std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                                Runtime::Config::Options& options,
                                                                NextEngine* engine);
