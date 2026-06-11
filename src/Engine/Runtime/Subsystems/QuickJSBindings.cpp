// QuickJS engine bindings: JS<->engine glue (Global/Input/Audio/UI/SceneBuild
// namespaces, node/component reflection objects, lifecycle hooks) plus the
// TypeScript compile helpers. Split out of QuickJSEngine.cpp.
#include "Engine/Runtime/Subsystems/QuickJSEngine.hpp"
#include "Engine/Runtime/Subsystems/QuickJSBindings.Internal.hpp"

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

namespace NextQuickJSBindings
{
#if WITH_QUICKJS
    std::string JoinArgs(const qjs::rest<std::string>& args, size_t startIndex)
    {
        std::string result;
        for (size_t index = startIndex; index < args.size(); ++index)
        {
            if (!result.empty())
            {
                result += " ";
            }
            result += args[index];
        }
        return result;
    }

    spdlog::level::level_enum ParseLogLevel(const std::string& value)
    {
        if (value == "trace") return spdlog::level::trace;
        if (value == "debug") return spdlog::level::debug;
        if (value == "info") return spdlog::level::info;
        if (value == "warn") return spdlog::level::warn;
        if (value == "warning") return spdlog::level::warn;
        if (value == "error") return spdlog::level::err;
        if (value == "critical") return spdlog::level::critical;
        return spdlog::level::info;
    }

    void Spdlog(qjs::rest<std::string> args)
    {
        if (args.empty())
        {
            return;
        }

        const spdlog::level::level_enum level = ParseLogLevel(args[0]);
        const std::string message = JoinArgs(args, 1);
        spdlog::log(level, "{}", message);
    }

    NextEngine* GetEngine()
    {
        return NextEngine::GetInstance();
    }

    Assets::Scene* GetSceneForGlobal()
    {
        auto* engine = NextEngine::GetInstance();
        return engine ? &engine->GetScene() : nullptr;
    }

    Assets::Node* FindNodeById(uint32_t nodeId)
    {
        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return nullptr;
        }

        auto* scene = &engine->GetScene();
        const auto node = scene->GetNodeSharedByInstanceId(nodeId);
        return node ? node.get() : nullptr;
    }
    
    Assets::Component* FindComponentByTypeName(uint32_t nodeId, const std::string& componentType)
    {
        auto* node = FindNodeById(nodeId);
        if (!node)
        {
            return nullptr;
        }

        return node->GetComponentByTypeName(componentType);
    }

    FQuickJSInputState GQuickJSInput;
    FQuickJSCameraOverride GQuickJSCamera;

    FQuickJSSceneBuildContext GQuickJSSceneBuild;

    bool JSValueToVec3(JSContext* ctx, JSValueConst value, glm::vec3& outVec)
    {
        auto readFloat = [&](const char* key, float& outValue) -> bool
        {
            JSValue prop = JS_GetPropertyStr(ctx, value, key);
            double numericValue = 0.0;
            const bool ok = JS_ToFloat64(ctx, &numericValue, prop) == 0;
            outValue = static_cast<float>(numericValue);
            JS_FreeValue(ctx, prop);
            return ok;
        };

        return readFloat("x", outVec.x) && readFloat("y", outVec.y) && readFloat("z", outVec.z);
    }

    JSValue Vec2ToJS(JSContext* ctx, const glm::vec2& value)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, value.x));
        JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, value.y));
        return obj;
    }

    JSValue JsonToJSValue(JSContext* ctx, const nlohmann::json& value)
    {
        if (value.is_null())
        {
            return JS_NULL;
        }
        if (value.is_boolean())
        {
            return JS_NewBool(ctx, value.get<bool>());
        }
        if (value.is_number_integer())
        {
            return JS_NewInt64(ctx, value.get<int64_t>());
        }
        if (value.is_number_unsigned())
        {
            return JS_NewUint32(ctx, value.get<uint32_t>());
        }
        if (value.is_number_float())
        {
            return JS_NewFloat64(ctx, value.get<double>());
        }
        if (value.is_string())
        {
            return JS_NewString(ctx, value.get<std::string>().c_str());
        }
        if (value.is_array())
        {
            JSValue array = JS_NewArray(ctx);
            uint32_t index = 0;
            for (const auto& item : value)
            {
                JS_SetPropertyUint32(ctx, array, index++, JsonToJSValue(ctx, item));
            }
            return array;
        }
        if (value.is_object())
        {
            JSValue obj = JS_NewObject(ctx);
            for (auto it = value.begin(); it != value.end(); ++it)
            {
                JS_SetPropertyStr(ctx, obj, it.key().c_str(), JsonToJSValue(ctx, it.value()));
            }
            return obj;
        }
        return JS_UNDEFINED;
    }

    std::string ToCString(JSContext* ctx, JSValueConst value)
    {
        const char* raw = JS_ToCString(ctx, value);
        if (!raw)
        {
            return {};
        }
        std::string result(raw);
        JS_FreeCString(ctx, raw);
        return result;
    }

    void JSToFloat(JSContext* ctx, JSValueConst value, float& outValue)
    {
        double numericValue = static_cast<double>(outValue);
        if (JS_ToFloat64(ctx, &numericValue, value) == 0)
        {
            outValue = static_cast<float>(numericValue);
        }
    }

    std::string GetObjectString(JSContext* ctx, JSValueConst object, const char* key, std::string fallback)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, key);
        std::string result = JS_IsUndefined(value) ? std::move(fallback) : ToCString(ctx, value);
        JS_FreeValue(ctx, value);
        return result;
    }

    float GetObjectFloat(JSContext* ctx, JSValueConst object, const char* key, float fallback)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, key);
        float result = fallback;
        if (!JS_IsUndefined(value))
        {
            JSToFloat(ctx, value, result);
        }
        JS_FreeValue(ctx, value);
        return result;
    }

    uint32_t GetObjectUint32(JSContext* ctx, JSValueConst object, const char* key, uint32_t fallback)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, key);
        uint32_t result = fallback;
        if (!JS_IsUndefined(value))
        {
            JS_ToUint32(ctx, &result, value);
        }
        JS_FreeValue(ctx, value);
        return result;
    }

    bool GetObjectBool(JSContext* ctx, JSValueConst object, const char* key, bool fallback)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, key);
        const bool result = JS_IsUndefined(value) ? fallback : JS_ToBool(ctx, value) != 0;
        JS_FreeValue(ctx, value);
        return result;
    }

    glm::vec3 GetObjectVec3(JSContext* ctx, JSValueConst object, const char* key, const glm::vec3& fallback)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, key);
        glm::vec3 result = fallback;
        if (JS_IsObject(value))
        {
            JSValueToVec3(ctx, value, result);
        }
        JS_FreeValue(ctx, value);
        return result;
    }

    JSValue RegisterLifecycleHooks(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "RegisterLifecycleHooks expects an object");
        }
        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, "__nextLifecycleHooks", JS_DupValue(ctx, argv[0]));
        JS_FreeValue(ctx, global);
        return JS_UNDEFINED;
    }


    JSValue LoadJson(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_UNDEFINED;
        }
        try
        {
            const nlohmann::json document = NextJson::LoadFile(ToCString(ctx, argv[0]));
            return JsonToJSValue(ctx, document);
        }
        catch (const std::exception& exception)
        {
            return JS_ThrowInternalError(ctx, "%s", exception.what());
        }
    }

    JSValue RequestLoadScene(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc >= 1)
        {
            if (auto* engine = NextEngine::GetInstance())
            {
                engine->RequestLoadScene({.filename = ToCString(ctx, argv[0])});
            }
        }
        return JS_UNDEFINED;
    }

    JSValue RequestClose(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)ctx;
        (void)thisVal;
        (void)argc;
        (void)argv;
        if (auto* engine = NextEngine::GetInstance())
        {
            engine->RequestClose();
        }
        return JS_UNDEFINED;
    }

    JSValue GetScreenSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        (void)argc;
        (void)argv;
        if (auto* engine = NextEngine::GetInstance())
        {
            const VkExtent2D extent = engine->GetWindow().WindowSize();
            return Vec2ToJS(ctx, glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
        }
        return Vec2ToJS(ctx, glm::vec2(0.0f));
    }

    JSValue SetOverrideCamera(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "SetOverrideCamera expects an object");
        }

        JSValue position = JS_GetPropertyStr(ctx, argv[0], "position");
        JSValue target = JS_GetPropertyStr(ctx, argv[0], "target");
        JSValue up = JS_GetPropertyStr(ctx, argv[0], "up");
        JSValue fov = JS_GetPropertyStr(ctx, argv[0], "fov");
        JSValueToVec3(ctx, position, GQuickJSCamera.position);
        JSValueToVec3(ctx, target, GQuickJSCamera.target);
        JSValueToVec3(ctx, up, GQuickJSCamera.up);
        JSToFloat(ctx, fov, GQuickJSCamera.fieldOfView);
        GQuickJSCamera.enabled = true;
        JS_FreeValue(ctx, position);
        JS_FreeValue(ctx, target);
        JS_FreeValue(ctx, up);
        JS_FreeValue(ctx, fov);
        return JS_UNDEFINED;
    }

    JSValue IsReplayMode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        (void)argc;
        (void)argv;
        return JS_NewBool(ctx, GOption && GOption->FlappyReplay);
    }

    JSValue WriteFile(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 2)
        {
            return JS_UNDEFINED;
        }

        const std::string path = ToCString(ctx, argv[0]);
        const std::string content = ToCString(ctx, argv[1]);
        std::filesystem::path outputPath(path);
        if (outputPath.is_relative())
        {
            outputPath = FindProjectRoot() / outputPath;
        }
        std::filesystem::create_directories(outputPath.parent_path());
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        output << content;
        return JS_UNDEFINED;
    }

#endif
}
