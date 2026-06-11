#include "Engine/Runtime/Subsystems/QuickJSEngine.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Runtime/Reflection/PropertyAccessor.h"
#include "Engine/Runtime/Reflection/QuickJSReflectionBridge.h"
#include "Engine/Runtime/Reflection/QuickJSTypeConverter.h"
#include "Engine/Runtime/Subsystems/NextAudio.h"
#include "Engine/Runtime/Utilities/JsonHelpers.h"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/stopwatch.h>
#include <fstream>
#include <entt/core/hashed_string.hpp>
#include <cstdlib>
#include <limits>

#if WITH_QUICKJS
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#endif

#include "Engine/Runtime/Subsystems/QuickJSBindings.Internal.hpp"

using namespace NextQuickJSBindings;

QuickJSEngine::QuickJSEngine() = default;

QuickJSEngine::~QuickJSEngine() = default;

void QuickJSEngine::Initialize()
{
#if WITH_QUICKJS
    CompileTypeScriptSources();
    ResetContextAndLoadScript();
#endif
}

#if WITH_QUICKJS
void QuickJSEngine::ResetContextAndLoadScript()
{
    tickCallback_ = nullptr;
    context_.reset();
    runtime_.reset();

    runtime_ = std::make_unique<qjs::Runtime>();
    context_ = std::make_unique<qjs::Context>(*runtime_);
    GQuickJSCamera = FQuickJSCameraOverride{};

    context_->moduleLoader = [](std::string_view moduleName) -> qjs::Context::ModuleData
    {
        std::string assetPath(moduleName);
        if (assetPath == "Engine")
        {
            return {};
        }
        if (assetPath.rfind("file://", 0) == 0)
        {
            assetPath = assetPath.substr(7);
        }
        std::replace(assetPath.begin(), assetPath.end(), '\\', '/');
        if (assetPath == "./Engine" || assetPath == "assets/scripts/Engine")
        {
            return qjs::Context::ModuleData{std::string("Engine"), std::string("export * from \"Engine\";")};
        }
        if (assetPath.size() < 3 || assetPath.substr(assetPath.size() - 3) != ".js")
        {
            assetPath += ".js";
        }

        std::vector<uint8_t> buffer;
        if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(assetPath, buffer))
        {
            return {};
        }

        return qjs::Context::ModuleData{assetPath, std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size())};
    };

    try
    {
        auto& module = context_->addModule("Engine");
        auto globalNamespace = context_->newObject();
        globalNamespace.add<&Spdlog>("spdlog");
        globalNamespace.add<&GetEngine>("GetEngine");
        globalNamespace.add<&GetSceneForGlobal>("GetScene");
        module.add("Global", std::move(globalNamespace));

        JSContext* ctx = context_->ctx;
        auto addRawFunction = [ctx](qjs::Value& object, const char* name, JSCFunction* function, int length)
        {
            JS_SetPropertyStr(ctx, object.v, name, JS_NewCFunction(ctx, function, name, length));
        };

        auto inputNamespace = context_->newObject();
        addRawFunction(inputNamespace, "IsKeyDown", InputIsKeyDown, 1);
        addRawFunction(inputNamespace, "IsKeyPressed", InputIsKeyPressed, 1);
        addRawFunction(inputNamespace, "IsMouseButtonDown", InputIsMouseButtonDown, 1);
        addRawFunction(inputNamespace, "IsMouseButtonPressed", InputIsMouseButtonPressed, 1);
        addRawFunction(inputNamespace, "GetGamepadButton", InputGetGamepadButton, 1);
        module.add("Input", std::move(inputNamespace));

        auto audioNamespace = context_->newObject();
        addRawFunction(audioNamespace, "PlaySfx", AudioPlaySfx, 2);
        addRawFunction(audioNamespace, "PlayMusic", AudioPlayMusic, 2);
        addRawFunction(audioNamespace, "StopMusic", AudioStopMusic, 0);
        module.add("Audio", std::move(audioNamespace));

        auto uiNamespace = context_->newObject();
        addRawFunction(uiNamespace, "Begin", UIBegin, 2);
        addRawFunction(uiNamespace, "End", UIEnd, 0);
        addRawFunction(uiNamespace, "Text", UIText, 1);
        addRawFunction(uiNamespace, "SetCursorPos", UISetCursorPos, 2);
        addRawFunction(uiNamespace, "GetWindowSize", UIGetWindowSize, 0);
        addRawFunction(uiNamespace, "SetWindowFontScale", UISetWindowFontScale, 1);
        addRawFunction(uiNamespace, "GetScreenSize", GetScreenSize, 0);
        addRawFunction(uiNamespace, "CalcTextSize", UICalcTextSize, 2);
        addRawFunction(uiNamespace, "DrawText", UIDrawText, 8);
        addRawFunction(uiNamespace, "DrawRectFilled", UIDrawRectFilled, 9);
        addRawFunction(uiNamespace, "DrawRect", UIDrawRect, 10);
        module.add("UI", std::move(uiNamespace));

        auto sceneBuildNamespace = context_->newObject();
        addRawFunction(sceneBuildNamespace, "AddProceduralModel", SceneBuildAddProceduralModel, 1);
        addRawFunction(sceneBuildNamespace, "AddLambertianMaterial", SceneBuildAddLambertianMaterial, 1);
        addRawFunction(sceneBuildNamespace, "AddDiffuseLightMaterial", SceneBuildAddDiffuseLightMaterial, 2);
        addRawFunction(sceneBuildNamespace, "AddRenderNode", SceneBuildAddRenderNode, 1);
        module.add("SceneBuild", std::move(sceneBuildNamespace));

        module.add("RegisterLifecycleHooks", JS_NewCFunction(ctx, RegisterLifecycleHooks, "RegisterLifecycleHooks", 1));
        module.add("LoadJson", JS_NewCFunction(ctx, LoadJson, "LoadJson", 1));
        module.add("RequestLoadScene", JS_NewCFunction(ctx, RequestLoadScene, "RequestLoadScene", 1));
        module.add("RequestClose", JS_NewCFunction(ctx, RequestClose, "RequestClose", 0));
        module.add("GetScreenSize", JS_NewCFunction(ctx, GetScreenSize, "GetScreenSize", 0));
        module.add("SetOverrideCamera", JS_NewCFunction(ctx, SetOverrideCamera, "SetOverrideCamera", 1));
        module.add("IsReplayMode", JS_NewCFunction(ctx, IsReplayMode, "IsReplayMode", 0));
        module.add("WriteFile", JS_NewCFunction(ctx, WriteFile, "WriteFile", 2));

        module.class_<NextEngine>("NextEngine")
                .fun<&NextEngine::GetTotalFrames>("GetTotalFrames")
                .fun<&NextEngine::GetTime>("GetTime")
                .fun<&NextEngine::GetDeltaSeconds>("GetDeltaSeconds")
                .fun<&NextEngine::GetSmoothDeltaSeconds>("GetSmoothDeltaSeconds")
                .fun<&NextEngine::RegisterJSCallback>("RegisterJSCallback");
        module.class_<Assets::Scene>("Scene")
                .fun<&Assets::Scene::GetIndicesCount>("GetIndicesCount")
                .fun<&Assets::Scene::FindNodeIdWithComponent>("FindNodeIdWithComponent");

        qjs::Context* jsContext = context_.get();
        BindScenePrototype(jsContext->ctx);

        if (editorBindingsCallback_)
        {
            editorBindingsCallback_(jsContext->ctx);
        }

        const std::string entryScript = (GOption && !GOption->QuickJSEntry.empty()) ?
            GOption->QuickJSEntry : std::string("assets/scripts/test.js");
        std::vector<uint8_t> scriptBuffer;
        if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(entryScript, scriptBuffer))
        {
            std::string moduleName = entryScript;
            if (moduleName.size() >= 3 && moduleName.substr(moduleName.size() - 3) == ".js")
            {
                moduleName.resize(moduleName.size() - 3);
            }
            context_->eval(std::string_view(reinterpret_cast<char*>(scriptBuffer.data()), scriptBuffer.size()),
                           moduleName.c_str(),
                           JS_EVAL_TYPE_MODULE);
        }
    }
    catch (qjs::exception)
    {
        auto exc = context_->getException();
        std::cerr << static_cast<std::string>(exc) << std::endl;
        if ((bool)exc["stack"])
        {
            std::cerr << static_cast<std::string>(exc["stack"]) << std::endl;
        }
    }
}
#endif

void QuickJSEngine::Tick(double deltaSeconds)
{
#if WITH_QUICKJS
    if (tickCallback_)
    {
        tickCallback_(deltaSeconds);
    }
    GQuickJSInput.ClearPressed();

    TickHotReload(deltaSeconds);
#else
    (void)deltaSeconds;
#endif
}

void QuickJSEngine::RegisterTickCallback(std::function<void(double)> callback)
{
#if WITH_QUICKJS
    tickCallback_ = std::move(callback);
#else
    (void)callback;
#endif
}

void QuickJSEngine::HandleInputEvent(const SDL_Event& event)
{
#if WITH_QUICKJS
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat)
        {
            GQuickJSInput.keysPressed.insert(event.key.key);
        }
        GQuickJSInput.keysDown.insert(event.key.key);
        break;
    case SDL_EVENT_KEY_UP:
        GQuickJSInput.keysDown.erase(event.key.key);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        GQuickJSInput.mouseButtonsPressed.insert(event.button.button);
        GQuickJSInput.mouseButtonsDown.insert(event.button.button);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        GQuickJSInput.mouseButtonsDown.erase(event.button.button);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        GQuickJSInput.gamepadButtonsPressed.insert(event.gbutton.button);
        GQuickJSInput.gamepadButtonsDown.insert(event.gbutton.button);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        GQuickJSInput.gamepadButtonsDown.erase(event.gbutton.button);
        break;
    default:
        break;
    }

    if (!context_)
    {
        return;
    }

    JSContext* ctx = context_->ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue hooks = JS_GetPropertyStr(ctx, global, "__nextLifecycleHooks");
    JS_FreeValue(ctx, global);
    if (!JS_IsObject(hooks))
    {
        JS_FreeValue(ctx, hooks);
        return;
    }

    JSValue callback = JS_GetPropertyStr(ctx, hooks, "onInputEvent");
    if (!JS_IsFunction(ctx, callback))
    {
        JS_FreeValue(ctx, callback);
        JS_FreeValue(ctx, hooks);
        return;
    }

    JSValue inputEvent = BuildInputEventObject(ctx, event);
    JSValue result = JS_Call(ctx, callback, hooks, 1, &inputEvent);
    JS_FreeValue(ctx, inputEvent);
    JS_FreeValue(ctx, callback);
    JS_FreeValue(ctx, hooks);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(ctx);
        const char* message = JS_ToCString(ctx, exception);
        SPDLOG_ERROR("[QuickJS] lifecycle hook 'onInputEvent' failed: {}", message ? message : "unknown");
        if (message)
        {
            JS_FreeCString(ctx, message);
        }
        JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
        const char* stackText = JS_ToCString(ctx, stack);
        if (stackText && stackText[0] != '\0')
        {
            SPDLOG_ERROR("[QuickJS] stack:\n{}", stackText);
        }
        if (stackText)
        {
            JS_FreeCString(ctx, stackText);
        }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, exception);
        return;
    }
    JS_FreeValue(ctx, result);
#else
    (void)event;
#endif
}

bool QuickJSEngine::CallLifecycleHook(const char* hookName, double deltaSeconds)
{
#if WITH_QUICKJS
    if (!context_ || !hookName)
    {
        return false;
    }

    JSContext* ctx = context_->ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue hooks = JS_GetPropertyStr(ctx, global, "__nextLifecycleHooks");
    JS_FreeValue(ctx, global);
    if (!JS_IsObject(hooks))
    {
        JS_FreeValue(ctx, hooks);
        return false;
    }

    JSValue callback = JS_GetPropertyStr(ctx, hooks, hookName);
    if (!JS_IsFunction(ctx, callback))
    {
        JS_FreeValue(ctx, callback);
        JS_FreeValue(ctx, hooks);
        return false;
    }

    JSValue arg = JS_NewFloat64(ctx, deltaSeconds);
    JSValue result = JS_Call(ctx, callback, hooks, 1, &arg);
    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, callback);
    JS_FreeValue(ctx, hooks);
    if (JS_IsException(result))
    {
        JSValue exception = JS_GetException(ctx);
        const char* message = JS_ToCString(ctx, exception);
        SPDLOG_ERROR("[QuickJS] lifecycle hook '{}' failed: {}", hookName, message ? message : "unknown");
        if (message)
        {
            JS_FreeCString(ctx, message);
        }
        JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
        const char* stackText = JS_ToCString(ctx, stack);
        if (stackText && stackText[0] != '\0')
        {
            SPDLOG_ERROR("[QuickJS] stack:\n{}", stackText);
        }
        if (stackText)
        {
            JS_FreeCString(ctx, stackText);
        }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, exception);
        return false;
    }
    JS_FreeValue(ctx, result);
    return true;
#else
    (void)hookName;
    (void)deltaSeconds;
    return false;
#endif
}

bool QuickJSEngine::CallBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                           std::vector<Assets::Model>& models,
                                           std::vector<Assets::FMaterial>& materials,
                                           std::vector<Assets::LightObject>& lights,
                                           std::vector<Assets::AnimationTrack>& tracks)
{
#if WITH_QUICKJS
    GQuickJSSceneBuild = FQuickJSSceneBuildContext{
        .nodes = &nodes,
        .models = &models,
        .materials = &materials,
        .lights = &lights,
        .tracks = &tracks,
    };
    const bool called = CallLifecycleHook("onBeforeSceneRebuild");
    GQuickJSSceneBuild = FQuickJSSceneBuildContext{};
    return called;
#else
    (void)nodes;
    (void)models;
    (void)materials;
    (void)lights;
    (void)tracks;
    return false;
#endif
}

bool QuickJSEngine::TryGetOverrideCamera(Assets::Camera& outCamera) const
{
#if WITH_QUICKJS
    if (!GQuickJSCamera.enabled)
    {
        return false;
    }
    outCamera.ModelView = glm::lookAtRH(GQuickJSCamera.position, GQuickJSCamera.target, GQuickJSCamera.up);
    outCamera.FieldOfView = GQuickJSCamera.fieldOfView;
    return true;
#else
    (void)outCamera;
    return false;
#endif
}

#if WITH_QUICKJS
void QuickJSEngine::TickHotReload(double deltaSeconds)
{
#if ANDROID || IOS
    (void)deltaSeconds;
    return;
#else
    constexpr double hotReloadIntervalSeconds = 0.5;
    hotReloadElapsed_ += deltaSeconds;
    if (hotReloadElapsed_ < hotReloadIntervalSeconds)
    {
        return;
    }

    hotReloadElapsed_ = 0.0;
    if (CompileTypeScriptSources())
    {
        SPDLOG_INFO("TypeScript outputs updated. Reloading QuickJS context.");
        ResetContextAndLoadScript();
    }
#endif
}

std::filesystem::path QuickJSEngine::ResolveBundledTscExecutable()
{
    if (tscChecked_)
    {
        return bundledTscPath_;
    }

    tscChecked_ = true;
    namespace fs = std::filesystem;

#if WIN32
    constexpr const char* tscExecutableName = "tsc.exe";
#else
    constexpr const char* tscExecutableName = "tsc";
#endif

    const fs::path executableDir = NextRenderer::GetExecutableDirectory();
    const fs::path currentDir = fs::current_path();
    std::vector<fs::path> candidates;

    if (!executableDir.empty())
    {
        candidates.push_back(executableDir / ".." / "tools" / "tsc" / tscExecutableName);
        candidates.push_back(executableDir / "tools" / "tsc" / tscExecutableName);
    }
    if (!currentDir.empty())
    {
        candidates.push_back(currentDir / "tools" / "tsc" / tscExecutableName);
        candidates.push_back(currentDir / ".." / "tools" / "tsc" / tscExecutableName);
    }
#if defined(GK_NEXT_SOURCE_DIR)
    candidates.push_back(fs::path(GK_NEXT_SOURCE_DIR) / "tools" / "tsc" / tscExecutableName);
#endif
    candidates.push_back(FindProjectRoot() / "tools" / "tsc" / tscExecutableName);

    std::error_code ec;
    for (const fs::path& candidate : candidates)
    {
        const fs::path normalizedCandidate = candidate.lexically_normal();
        if (fs::exists(normalizedCandidate, ec) && fs::is_regular_file(normalizedCandidate, ec))
        {
            bundledTscPath_ = fs::absolute(normalizedCandidate, ec);
            if (ec)
            {
                bundledTscPath_ = normalizedCandidate;
                ec.clear();
            }
            return bundledTscPath_;
        }
        ec.clear();
    }

    SPDLOG_WARN("Bundled TypeScript compiler not found under tools/tsc; TS hot reload disabled.");
    return {};
}

bool QuickJSEngine::CompileTypeScriptSources()
{
    namespace fs = std::filesystem;

    try
    {
        const TypeScriptPaths paths = ResolveTypeScriptPaths();
        const fs::path tsconfigPath = paths.tsconfigPath;
        if (tsconfigPath.empty())
        {
            SPDLOG_DEBUG("TypeScript tsconfig not found; skipping compilation.");
            return false;
        }

        UpdateTypeScriptDefinitions(tsconfigPath);

        std::error_code ec;
        if (!fs::exists(tsconfigPath, ec))
        {
            SPDLOG_DEBUG("TypeScript tsconfig missing at {}", tsconfigPath.string());
            return false;
        }

        const fs::path projectDir = paths.projectDir;
        const fs::path outputDir = paths.outputDir;
        if (outputDir.empty())
        {
            SPDLOG_WARN("TypeScript output directory is not available.");
            return false;
        }

        const bool forceCompileRequested = std::getenv("NEXTENGINE_FORCE_TSC") != nullptr;
        const bool forceCompile = forceCompileRequested && !forceTscCompileConsumed_;
        if (forceCompile)
        {
            forceTscCompileConsumed_ = true;
        }

        std::filesystem::file_time_type latestSource{};
        if (!forceCompile && !HasNewerTypeScriptSources(projectDir, outputDir, tsconfigPath, latestSource))
        {
            return false;
        }

        if (!fs::exists(outputDir, ec))
        {
            fs::create_directories(outputDir, ec);
            if (ec)
            {
                SPDLOG_WARN("Failed to create TypeScript output directory {}: {}", outputDir.string(), ec.message());
            }
        }

        if (forceCompile)
        {
            GetLatestTypeScriptTimestamp(projectDir, tsconfigPath, latestSource);
        }

        const fs::path bundledTsc = ResolveBundledTscExecutable();
        if (bundledTsc.empty())
        {
            return false;
        }

        const std::string command = fmt::format("\"{}\" -p \"{}\" --outDir \"{}\"",
                                                bundledTsc.string(),
                                                tsconfigPath.string(),
                                                outputDir.string());

        SPDLOG_INFO("Compiling TypeScript scripts using: {}", command);
        spdlog::stopwatch stopwatch;
        int result = NextRenderer::OSProcess(command.c_str());
        SPDLOG_INFO("---- Compiling TypeScript in {}", stopwatch.elapsed_ms());
        if (result == 0)
        {
            const fs::path stampPath = outputDir / ".tsc.stamp";
            std::ofstream writer(stampPath, std::ios::binary | std::ios::trunc);
            if (!writer)
            {
                SPDLOG_WARN("Failed to update TypeScript stamp at {}", stampPath.string());
            }
            return true;
        }

        SPDLOG_WARN("Bundled TypeScript compile command failed with code {}; continuing with existing JavaScript outputs.", result);
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("Exception while compiling TypeScript sources: {}", e.what());
    }

    return false;
}

void QuickJSEngine::SetEditorBindingsCallback(BindingsCallback callback)
{
    editorBindingsCallback_ = std::move(callback);
    // Re-register if context already exists
    if (context_ && editorBindingsCallback_)
    {
        editorBindingsCallback_(context_->ctx);
    }
}

std::string QuickJSEngine::Eval(const std::string& code)
{
    if (!context_)
    {
        return "QuickJS context not initialized";
    }

    try
    {
        JSContext* ctx = context_->ctx;
        // Wrap in IIFE so const/let declarations don't persist in global scope
        // across multiple eval calls (avoids "redeclaration" errors).
        std::string wrapped = "(function(){" + code + "\n})()";
        JSValue result = JS_Eval(ctx, wrapped.c_str(), wrapped.size(), "<ai-eval>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result))
        {
            JSValue exc = JS_GetException(ctx);
            const char* str = JS_ToCString(ctx, exc);
            std::string errMsg = str ? str : "Unknown error";
            if (str)
            {
                JS_FreeCString(ctx, str);
            }
            JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
            if (JS_IsString(stack))
            {
                const char* stackStr = JS_ToCString(ctx, stack);
                if (stackStr)
                {
                    errMsg += "\n";
                    errMsg += stackStr;
                    JS_FreeCString(ctx, stackStr);
                }
            }
            JS_FreeValue(ctx, stack);
            JS_FreeValue(ctx, exc);
            return errMsg;
        }
        JS_FreeValue(ctx, result);
        return "";
    }
    catch (const std::exception& e)
    {
        return e.what();
    }
}
#endif

// Non-QuickJS stubs
#if !WITH_QUICKJS
std::string QuickJSEngine::Eval(const std::string& code)
{
    (void)code;
    return "QuickJS not available";
}

void QuickJSEngine::SetEditorBindingsCallback(BindingsCallback callback)
{
    (void)callback;
}
#endif
