#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Runtime/ScriptRuntime.hpp"
#include "Modules/NextQuickJS/NextQuickJSModule.hpp"

#include <SDL3/SDL.h>

namespace qjs
{
    class Context;
    class Runtime;
}

namespace Modules::NextQuickJS
{
class QuickJSEngine final : public Runtime::IScriptRuntime
{
public:
    QuickJSEngine(NextEngine& engine, FConfig config);
    ~QuickJSEngine() override;

    void Initialize() override;
    void Tick(double deltaSeconds) override;
    void HandleEvent(const SDL_Event& event) override;
    void RegisterTickCallback(std::function<void(double)> callback);
    bool CallLifecycleHook(const char* hookName, double deltaSeconds = 0.0);
    bool CallBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                std::vector<Assets::Model>& models,
                                std::vector<Assets::FMaterial>& materials,
                                std::vector<Assets::LightObject>& lights,
                                std::vector<Assets::AnimationTrack>& tracks);
    bool TryGetOverrideCamera(Assets::Camera& outCamera) const;

    // Execute JS code in the current context, returns error string (empty on success)
    std::string Eval(const std::string& code);

    // Register a callback to bind Editor.* helpers into the JS context
    using BindingsCallback = std::function<void(void* jsContext)>;
    void SetEditorBindingsCallback(BindingsCallback callback);

private:
    bool CompileTypeScriptSources();
    void ResetContextAndLoadScript();
    void TickHotReload(double deltaSeconds);
    std::filesystem::path ResolveBundledTscExecutable();

    NextEngine& engine_;
    FConfig config_;
    std::unique_ptr<qjs::Runtime> runtime_;
    std::unique_ptr<qjs::Context> context_;
    std::function<void(double)> tickCallback_;
    BindingsCallback editorBindingsCallback_;
    double hotReloadElapsed_ = 0.0;
    bool tscChecked_ = false;
    bool forceTscCompileConsumed_ = false;
    std::filesystem::path bundledTscPath_;
};
}
