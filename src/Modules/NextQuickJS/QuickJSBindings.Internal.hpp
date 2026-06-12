#pragma once

// Internal declarations shared between QuickJSBindings.cpp and
// QuickJSEngine.cpp. Not part of the engine public API.

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

#if WITH_QUICKJS

#include <ThirdParty/quickjs-ng/quickjspp.hpp>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

namespace NextQuickJSBindings
{
    // Shared JS-visible state
    struct FQuickJSInputState
    {
        std::unordered_set<SDL_Keycode> keysDown;
        std::unordered_set<SDL_Keycode> keysPressed;
        std::unordered_set<uint8_t> mouseButtonsDown;
        std::unordered_set<uint8_t> mouseButtonsPressed;
        std::unordered_set<uint8_t> gamepadButtonsDown;
        std::unordered_set<uint8_t> gamepadButtonsPressed;

        void ClearPressed()
        {
            keysPressed.clear();
            mouseButtonsPressed.clear();
            gamepadButtonsPressed.clear();
        }
    };

    struct FQuickJSCameraOverride
    {
        bool enabled = false;
        glm::vec3 position{0.0f, 0.0f, 12.0f};
        glm::vec3 target{0.0f, 0.0f, 0.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        float fieldOfView = 50.0f;
    };

    struct FQuickJSSceneBuildContext
    {
        std::vector<std::shared_ptr<Assets::Node>>* nodes = nullptr;
        std::vector<Assets::Model>* models = nullptr;
        std::vector<Assets::FMaterial>* materials = nullptr;
        std::vector<Assets::LightObject>* lights = nullptr;
        std::vector<Assets::AnimationTrack>* tracks = nullptr;

        bool IsValid() const
        {
            return nodes && models && materials && lights && tracks;
        }
    };

    extern FQuickJSInputState GQuickJSInput;
    extern FQuickJSCameraOverride GQuickJSCamera;
    extern FQuickJSSceneBuildContext GQuickJSSceneBuild;

    // Global namespace helpers
    void Spdlog(qjs::rest<std::string> args);
    NextEngine* GetEngine();
    Assets::Scene* GetSceneForGlobal();

    // Small JS value conversion / lookup helpers shared between the
    // QuickJSBindings.*.cpp partial TUs
    Assets::Node* FindNodeById(uint32_t nodeId);
    Assets::Component* FindComponentByTypeName(uint32_t nodeId, const std::string& componentType);
    bool JSValueToVec3(JSContext* ctx, JSValueConst value, glm::vec3& outVec);
    JSValue Vec2ToJS(JSContext* ctx, const glm::vec2& value);
    std::string ToCString(JSContext* ctx, JSValueConst value);
    void JSToFloat(JSContext* ctx, JSValueConst value, float& outValue);
    std::string GetObjectString(JSContext* ctx, JSValueConst object, const char* key, std::string fallback = {});
    float GetObjectFloat(JSContext* ctx, JSValueConst object, const char* key, float fallback = 0.0f);
    uint32_t GetObjectUint32(JSContext* ctx, JSValueConst object, const char* key, uint32_t fallback = 0);
    bool GetObjectBool(JSContext* ctx, JSValueConst object, const char* key, bool fallback = false);
    glm::vec3 GetObjectVec3(JSContext* ctx, JSValueConst object, const char* key,
                            const glm::vec3& fallback = glm::vec3(0.0f));

    // Raw JSCFunction bindings registered on the Engine module
    JSValue InputIsKeyDown(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue InputIsKeyPressed(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue InputIsMouseButtonDown(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue InputIsMouseButtonPressed(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue InputGetGamepadButton(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue AudioPlaySfx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue AudioPlayMusic(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue AudioStopMusic(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UIBegin(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UIEnd(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UIText(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UISetCursorPos(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UIGetWindowSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UISetWindowFontScale(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UICalcTextSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UIDrawText(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UIDrawRectFilled(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue UIDrawRect(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue GetScreenSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue SceneBuildAddProceduralModel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue SceneBuildAddLambertianMaterial(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue SceneBuildAddDiffuseLightMaterial(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue SceneBuildAddRenderNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue RegisterLifecycleHooks(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue RegisterTickCallbackBinding(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue LoadJson(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue RequestLoadScene(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue RequestClose(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue SetOverrideCamera(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue IsReplayMode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);
    JSValue WriteFile(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv);

    // Scene prototype + input event helpers
    void BindScenePrototype(JSContext* ctx);
    JSValue BuildInputEventObject(JSContext* ctx, const SDL_Event& event);

    // TypeScript toolchain helpers
    struct TypeScriptPaths
    {
        std::filesystem::path tsconfigPath;
        std::filesystem::path projectDir;
        std::filesystem::path outputDir;
    };
    std::filesystem::path FindProjectRoot();
    TypeScriptPaths ResolveTypeScriptPaths();
    bool GetLatestTypeScriptTimestamp(const std::filesystem::path& projectDir,
                                      const std::filesystem::path& tsconfigPath,
                                      std::filesystem::file_time_type& outTimestamp);
    bool HasNewerTypeScriptSources(const std::filesystem::path& projectDir, const std::filesystem::path& outputDir,
                                   const std::filesystem::path& tsconfigPath,
                                   std::filesystem::file_time_type& outLatestSource);
    void UpdateTypeScriptDefinitions(const std::filesystem::path& tsconfigPath);
}

#endif
