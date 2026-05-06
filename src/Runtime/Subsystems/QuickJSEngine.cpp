#include "Runtime/Subsystems/QuickJSEngine.hpp"

#include "Runtime/Engine.hpp"
#include "Assets/Core/Scene.hpp"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Scene/SceneBuilder.h"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Runtime/Reflection/QuickJSReflectionBridge.h"
#include "Runtime/Reflection/QuickJSTypeConverter.h"
#include "Runtime/Subsystems/NextAudio.h"
#include "Runtime/Utilities/JsonHelpers.h"
#include "Assets/Loaders/FProcModel.h"
#include "Utilities/FileHelper.hpp"
#include "Runtime/Platform/PlatformCommon.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <fstream>
#include <entt/core/hashed_string.hpp>
#include <cstdlib>
#include <limits>

#if WITH_QUICKJS
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#endif

namespace
{
#if WITH_QUICKJS
    struct TypeScriptPaths
    {
        std::filesystem::path tsconfigPath;
        std::filesystem::path projectDir;
        std::filesystem::path outputDir;
    };

    bool HasExtension(const std::filesystem::path& path, std::initializer_list<const char*> extensions)
    {
        const std::string extension = path.extension().string();
        for (const char* candidate : extensions)
        {
            if (extension == candidate)
            {
                return true;
            }
        }
        return false;
    }

    bool FindLatestTimestamp(const std::filesystem::path& root,
        std::initializer_list<const char*> extensions,
        std::filesystem::file_time_type& outTimestamp)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(root, ec))
        {
            return false;
        }

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            SPDLOG_WARN("Failed to enumerate {}: {}", root.string(), ec.message());
            return false;
        }

        const fs::recursive_directory_iterator end;
        bool hasTimestamp = false;
        for (; it != end; it.increment(ec))
        {
            if (ec)
            {
                SPDLOG_WARN("Directory iteration error under {}: {}", root.string(), ec.message());
                ec.clear();
                continue;
            }

            if (it->is_directory(ec))
            {
                if (!ec && it->path().filename() == "node_modules")
                {
                    it.disable_recursion_pending();
                }
                ec.clear();
                continue;
            }

            if (ec)
            {
                SPDLOG_WARN("Failed to inspect {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!it->is_regular_file(ec))
            {
                ec.clear();
                continue;
            }

            if (ec)
            {
                SPDLOG_WARN("Failed to query file type for {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!HasExtension(it->path(), extensions))
            {
                continue;
            }

            auto timestamp = it->last_write_time(ec);
            if (ec)
            {
                SPDLOG_WARN("Failed to query timestamp for {}: {}", it->path().string(), ec.message());
                ec.clear();
                continue;
            }

            if (!hasTimestamp || timestamp > outTimestamp)
            {
                outTimestamp = timestamp;
                hasTimestamp = true;
            }
        }

        return hasTimestamp;
    }

    int ToQuickJSArity(entt::meta_func::size_type arity)
    {
        constexpr auto maxQuickJSArity = static_cast<entt::meta_func::size_type>(std::numeric_limits<int>::max());
        if (arity > maxQuickJSArity)
        {
            SPDLOG_WARN("QuickJS method arity {} exceeds int range; clamping.", static_cast<size_t>(arity));
            return std::numeric_limits<int>::max();
        }

        return static_cast<int>(arity);
    }

    bool GetLatestTypeScriptTimestamp(const std::filesystem::path& projectDir,
        const std::filesystem::path& tsconfigPath,
        std::filesystem::file_time_type& outTimestamp)
    {
        outTimestamp = std::filesystem::file_time_type{};
        if (!FindLatestTimestamp(projectDir, { ".ts", ".d.ts" }, outTimestamp))
        {
            return false;
        }

        std::error_code ec;
        if (!tsconfigPath.empty() && std::filesystem::exists(tsconfigPath, ec))
        {
            auto tsconfigTime = std::filesystem::last_write_time(tsconfigPath, ec);
            if (!ec && tsconfigTime > outTimestamp)
            {
                outTimestamp = tsconfigTime;
            }
        }

        return true;
    }

    TypeScriptPaths ResolveTypeScriptPaths()
    {
        namespace fs = std::filesystem;

        TypeScriptPaths paths;
        paths.outputDir = fs::absolute(fs::path(Utilities::FileHelper::GetPlatformFilePath("assets/scripts")));

        fs::path tsconfigPath = fs::path(Utilities::FileHelper::GetNormalizedFilePath("assets/typescript/tsconfig.json"));
#if defined(GK_NEXT_SOURCE_DIR)
        const fs::path sourceRoot = fs::path(GK_NEXT_SOURCE_DIR);
        const fs::path sourceTsconfig = sourceRoot / "assets/typescript/tsconfig.json";
        if (fs::exists(sourceTsconfig))
        {
            tsconfigPath = sourceTsconfig;
        }
#endif

        paths.tsconfigPath = tsconfigPath;
        paths.projectDir = tsconfigPath.parent_path();
        return paths;
    }

    bool HasNewerTypeScriptSources(const std::filesystem::path& projectDir,
        const std::filesystem::path& outputDir,
        const std::filesystem::path& tsconfigPath,
        std::filesystem::file_time_type& outLatestSource)
    {
        if (!GetLatestTypeScriptTimestamp(projectDir, tsconfigPath, outLatestSource))
        {
            return false;
        }

        const std::filesystem::path stampPath = outputDir / ".tsc.stamp";
        std::error_code ec;
        if (std::filesystem::exists(stampPath, ec))
        {
            auto stampTime = std::filesystem::last_write_time(stampPath, ec);
            if (!ec && stampTime >= outLatestSource)
            {
                return false;
            }
        }

        return true;
    }

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

    FQuickJSInputState GQuickJSInput;
    FQuickJSCameraOverride GQuickJSCamera;

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

    FQuickJSSceneBuildContext GQuickJSSceneBuild;

    std::optional<SDL_Keycode> KeyNameToCode(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

        if (name == "space") return SDLK_SPACE;
        if (name == "esc" || name == "escape") return SDLK_ESCAPE;
        if (name == "a") return SDLK_A;
        if (name == "w") return SDLK_W;
        if (name == "s") return SDLK_S;
        if (name == "d") return SDLK_D;
        if (name == "return" || name == "enter") return SDLK_RETURN;
        return std::nullopt;
    }

    std::optional<uint8_t> GamepadNameToButton(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

        if (name == "a" || name == "south") return SDL_GAMEPAD_BUTTON_SOUTH;
        if (name == "b" || name == "east") return SDL_GAMEPAD_BUTTON_EAST;
        if (name == "x" || name == "west") return SDL_GAMEPAD_BUTTON_WEST;
        if (name == "y" || name == "north") return SDL_GAMEPAD_BUTTON_NORTH;
        if (name == "start") return SDL_GAMEPAD_BUTTON_START;
        if (name == "back") return SDL_GAMEPAD_BUTTON_BACK;
        return std::nullopt;
    }

    std::string KeyCodeToName(const SDL_Keycode key)
    {
        switch (key)
        {
        case SDLK_SPACE:
            return "space";
        case SDLK_ESCAPE:
            return "esc";
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return "enter";
        default:
            break;
        }

        std::string name = SDL_GetKeyName(key);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return name;
    }

    std::string GamepadButtonToName(const uint8_t button)
    {
        switch (button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return "south";
        case SDL_GAMEPAD_BUTTON_EAST:
            return "east";
        case SDL_GAMEPAD_BUTTON_WEST:
            return "west";
        case SDL_GAMEPAD_BUTTON_NORTH:
            return "north";
        case SDL_GAMEPAD_BUTTON_START:
            return "start";
        case SDL_GAMEPAD_BUTTON_BACK:
            return "back";
        default:
            return fmt::format("button{}", button);
        }
    }

    JSValue BuildInputEventObject(JSContext* ctx, const SDL_Event& event)
    {
        JSValue eventObject = JS_NewObject(ctx);
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            JS_SetPropertyStr(ctx, eventObject, "type",
                              JS_NewString(ctx, event.type == SDL_EVENT_KEY_DOWN ? "keyDown" : "keyUp"));
            JS_SetPropertyStr(ctx, eventObject, "key",
                              JS_NewString(ctx, KeyCodeToName(event.key.key).c_str()));
            JS_SetPropertyStr(ctx, eventObject, "repeated", JS_NewBool(ctx, event.key.repeat));
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            JS_SetPropertyStr(ctx, eventObject, "type",
                              JS_NewString(ctx, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "mouseButtonDown" : "mouseButtonUp"));
            JS_SetPropertyStr(ctx, eventObject, "mouseButton", JS_NewUint32(ctx, event.button.button));
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            JS_SetPropertyStr(ctx, eventObject, "type",
                              JS_NewString(ctx, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? "gamepadButtonDown" : "gamepadButtonUp"));
            JS_SetPropertyStr(ctx, eventObject, "gamepadButton",
                              JS_NewString(ctx, GamepadButtonToName(event.gbutton.button).c_str()));
            break;
        default:
            JS_SetPropertyStr(ctx, eventObject, "type", JS_NewString(ctx, "unknown"));
            break;
        }
        return eventObject;
    }

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

    std::filesystem::path FindProjectRoot()
    {
        std::filesystem::path cursor = std::filesystem::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            if (std::filesystem::exists(cursor / "AGENTS.md") && std::filesystem::exists(cursor / "assets"))
            {
                return cursor;
            }
            if (!cursor.has_parent_path())
            {
                break;
            }
            cursor = cursor.parent_path();
        }
        return std::filesystem::current_path();
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

    std::string GetObjectString(JSContext* ctx, JSValueConst object, const char* key, std::string fallback = {})
    {
        JSValue value = JS_GetPropertyStr(ctx, object, key);
        std::string result = JS_IsUndefined(value) ? std::move(fallback) : ToCString(ctx, value);
        JS_FreeValue(ctx, value);
        return result;
    }

    float GetObjectFloat(JSContext* ctx, JSValueConst object, const char* key, float fallback = 0.0f)
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

    uint32_t GetObjectUint32(JSContext* ctx, JSValueConst object, const char* key, uint32_t fallback = 0)
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

    bool GetObjectBool(JSContext* ctx, JSValueConst object, const char* key, bool fallback = false)
    {
        JSValue value = JS_GetPropertyStr(ctx, object, key);
        const bool result = JS_IsUndefined(value) ? fallback : JS_ToBool(ctx, value) != 0;
        JS_FreeValue(ctx, value);
        return result;
    }

    glm::vec3 GetObjectVec3(JSContext* ctx, JSValueConst object, const char* key, const glm::vec3& fallback = glm::vec3(0.0f))
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

    uint32_t GenerateBuildInstanceId(const std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        uint32_t maxId = 0;
        for (const auto& node : nodes)
        {
            if (node)
            {
                maxId = std::max(maxId, node->GetInstanceId());
            }
        }
        return nodes.empty() ? 0 : maxId + 1;
    }

    std::shared_ptr<Assets::Node> CreateRenderNodeFromSpec(JSContext* ctx,
                                                           JSValueConst spec,
                                                           uint32_t instanceId,
                                                           uint32_t modelCount)
    {
        const std::string name = GetObjectString(ctx, spec, "name", "ScriptNode");
        const uint32_t modelId = GetObjectUint32(ctx, spec, "modelId");
        const uint32_t materialId = GetObjectUint32(ctx, spec, "materialId");
        if (modelId >= modelCount)
        {
            return nullptr;
        }

        const glm::vec3 translation = GetObjectVec3(ctx, spec, "translation");
        const glm::vec3 scale = GetObjectVec3(ctx, spec, "scale", glm::vec3(1.0f));
        const bool visible = GetObjectBool(ctx, spec, "visible", true);
        return SceneBuilder::CreateRenderNode(name, translation, scale, instanceId, modelId, materialId, visible);
    }

    JSValue ComponentPropertyGetter(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                                    int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)argc;
        (void)argv;
        (void)magic;

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);

        const char* componentType = JS_ToCString(ctx, data[1]);
        const char* propertyName = JS_ToCString(ctx, data[2]);
        if (!componentType || !propertyName)
        {
            if (componentType)
            {
                JS_FreeCString(ctx, componentType);
            }
            if (propertyName)
            {
                JS_FreeCString(ctx, propertyName);
            }
            return JS_UNDEFINED;
        }

        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            JS_FreeCString(ctx, componentType);
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        Assets::Component* component = FindComponentByTypeName(nodeId, componentType);
        if (!component)
        {
            JS_FreeCString(ctx, componentType);
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = component->GetMetaType();
        entt::meta_any value = Reflection::PropertyAccessor::GetPropertyValue(metaType, component, propertyName);
        JS_FreeCString(ctx, componentType);
        JS_FreeCString(ctx, propertyName);

        if (!value)
        {
            return JS_UNDEFINED;
        }

        return Reflection::QuickJSTypeConverter::ToJSValue(ctx, value);
    }

    JSValue ComponentPropertySetter(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                                    int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)magic;

        if (argc < 1)
        {
            return JS_UNDEFINED;
        }

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);

        const char* componentType = JS_ToCString(ctx, data[1]);
        const char* propertyName = JS_ToCString(ctx, data[2]);
        if (!componentType || !propertyName)
        {
            if (componentType)
            {
                JS_FreeCString(ctx, componentType);
            }
            if (propertyName)
            {
                JS_FreeCString(ctx, propertyName);
            }
            return JS_UNDEFINED;
        }

        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            JS_FreeCString(ctx, componentType);
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        Assets::Component* component = FindComponentByTypeName(nodeId, componentType);
        if (!component)
        {
            JS_FreeCString(ctx, componentType);
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = component->GetMetaType();
        auto dataEntry = metaType.data(entt::hashed_string::value(propertyName));
        if (!dataEntry)
        {
            JS_FreeCString(ctx, componentType);
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        entt::meta_type valueType = dataEntry.type();
        entt::meta_any converted = Reflection::QuickJSTypeConverter::FromJSValue(ctx, argv[0], valueType);
        JS_FreeCString(ctx, componentType);
        JS_FreeCString(ctx, propertyName);

        if (!converted)
        {
            return JS_UNDEFINED;
        }

        Reflection::PropertyAccessor::SetPropertyValue(metaType, component, dataEntry.name(), converted);
        return JS_UNDEFINED;
    }

    JSValue ComponentMethodInvoker(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                                   int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)magic;

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);

        const char* componentType = JS_ToCString(ctx, data[1]);
        const char* functionName = JS_ToCString(ctx, data[2]);
        if (!componentType || !functionName)
        {
            if (componentType)
            {
                JS_FreeCString(ctx, componentType);
            }
            if (functionName)
            {
                JS_FreeCString(ctx, functionName);
            }
            return JS_UNDEFINED;
        }

        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            JS_FreeCString(ctx, componentType);
            JS_FreeCString(ctx, functionName);
            return JS_UNDEFINED;
        }

        Assets::Component* component = FindComponentByTypeName(nodeId, componentType);
        if (!component)
        {
            JS_FreeCString(ctx, componentType);
            JS_FreeCString(ctx, functionName);
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = component->GetMetaType();
        auto function = metaType.func(entt::hashed_string::value(functionName));
        JS_FreeCString(ctx, componentType);
        JS_FreeCString(ctx, functionName);

        if (!function)
        {
            return JS_UNDEFINED;
        }

        if (function.arity() != static_cast<entt::meta_func::size_type>(argc))
        {
            JS_ThrowTypeError(ctx, "Invalid argument count");
            return JS_EXCEPTION;
        }

        entt::meta_any instanceAny = metaType.from_void(component);
        entt::meta_any result;
        if (argc == 0)
        {
            result = function.invoke(instanceAny);
        }
        else
        {
            std::vector<entt::meta_any> args;
            args.reserve(static_cast<size_t>(argc));
            for (int idx = 0; idx < argc; ++idx)
            {
                entt::meta_type argType = function.arg(static_cast<entt::meta_func::size_type>(idx));
                args.emplace_back(Reflection::QuickJSTypeConverter::FromJSValue(ctx, argv[idx], argType));
            }

            result = function.invoke(instanceAny, args.data(), args.size());
        }

        if (!result)
        {
            return JS_UNDEFINED;
        }

        return Reflection::QuickJSTypeConverter::ToJSValue(ctx, result);
    }

    JSValue CreateComponentObject(JSContext* ctx, Assets::Component* component, uint32_t nodeId, const std::string& componentType)
    {
        if (!component)
        {
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = component->GetMetaType();
        JSValue obj = JS_NewObject(ctx);

        auto properties = Reflection::PropertyAccessor::GetProperties(metaType);
        for (const auto& prop : properties)
        {
            if (!prop.meta.IsJSExposed())
            {
                continue;
            }

            JSValue data[3];
            data[0] = JS_NewUint32(ctx, nodeId);
            data[1] = JS_NewString(ctx, componentType.c_str());
            data[2] = JS_NewString(ctx, prop.name.c_str());

            JSValue getter = JS_NewCFunctionData(ctx, ComponentPropertyGetter, 0, 0, 3, data);
            JSValue setter = JS_UNDEFINED;
            if (!prop.meta.IsReadOnly())
            {
                setter = JS_NewCFunctionData(ctx, ComponentPropertySetter, 1, 0, 3, data);
            }

            JSAtom propAtom = JS_NewAtom(ctx, prop.name.c_str());
            JS_DefinePropertyGetSet(ctx, obj, propAtom, getter, setter,
                                    JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
            JS_FreeAtom(ctx, propAtom);
        }

        for (auto&& [id, func] : metaType.func())
        {
            const char* funcName = func.name();
            if (!funcName)
            {
                continue;
            }

            JSValue data[3];
            data[0] = JS_NewUint32(ctx, nodeId);
            data[1] = JS_NewString(ctx, componentType.c_str());
            data[2] = JS_NewString(ctx, funcName);

            JSValue jsFunc = JS_NewCFunctionData(ctx, ComponentMethodInvoker, ToQuickJSArity(func.arity()), 0, 3, data);
            JS_SetPropertyStr(ctx, obj, funcName, jsFunc);
        }

        return obj;
    }

    JSValue NodePropertyGetter(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                               int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)argc;
        (void)argv;
        (void)magic;

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);

        const char* propertyName = JS_ToCString(ctx, data[1]);
        if (!propertyName)
        {
            return JS_UNDEFINED;
        }

        Assets::Node* node = FindNodeById(nodeId);
        if (!node)
        {
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = entt::resolve<Assets::Node>();
        entt::meta_any value = Reflection::PropertyAccessor::GetPropertyValue(metaType, node, propertyName);
        JS_FreeCString(ctx, propertyName);

        if (!value)
        {
            return JS_UNDEFINED;
        }

        return Reflection::QuickJSTypeConverter::ToJSValue(ctx, value);
    }

    JSValue NodePropertySetter(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                               int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)magic;

        if (argc < 1)
        {
            return JS_UNDEFINED;
        }

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);

        const char* propertyName = JS_ToCString(ctx, data[1]);
        if (!propertyName)
        {
            return JS_UNDEFINED;
        }

        Assets::Node* node = FindNodeById(nodeId);
        if (!node)
        {
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = entt::resolve<Assets::Node>();
        auto dataEntry = metaType.data(entt::hashed_string::value(propertyName));
        if (!dataEntry)
        {
            JS_FreeCString(ctx, propertyName);
            return JS_UNDEFINED;
        }

        entt::meta_type valueType = dataEntry.type();
        entt::meta_any converted = Reflection::QuickJSTypeConverter::FromJSValue(ctx, argv[0], valueType);
        JS_FreeCString(ctx, propertyName);

        if (!converted)
        {
            return JS_UNDEFINED;
        }

        Reflection::PropertyAccessor::SetPropertyValue(metaType, node, dataEntry.name(), converted);
        return JS_UNDEFINED;
    }

    JSValue NodeMethodInvoker(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                              int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)magic;

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);

        const char* functionName = JS_ToCString(ctx, data[1]);
        if (!functionName)
        {
            return JS_UNDEFINED;
        }

        Assets::Node* node = FindNodeById(nodeId);
        if (!node)
        {
            JS_FreeCString(ctx, functionName);
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = entt::resolve<Assets::Node>();
        auto function = metaType.func(entt::hashed_string::value(functionName));
        JS_FreeCString(ctx, functionName);

        if (!function)
        {
            return JS_UNDEFINED;
        }

        if (function.arity() != static_cast<entt::meta_func::size_type>(argc))
        {
            JS_ThrowTypeError(ctx, "Invalid argument count");
            return JS_EXCEPTION;
        }

        entt::meta_any instanceAny = metaType.from_void(node);
        entt::meta_any result;
        if (argc == 0)
        {
            result = function.invoke(instanceAny);
        }
        else
        {
            std::vector<entt::meta_any> args;
            args.reserve(static_cast<size_t>(argc));
            for (int idx = 0; idx < argc; ++idx)
            {
                entt::meta_type argType = function.arg(static_cast<entt::meta_func::size_type>(idx));
                args.emplace_back(Reflection::QuickJSTypeConverter::FromJSValue(ctx, argv[idx], argType));
            }

            result = function.invoke(instanceAny, args.data(), args.size());
        }

        if (!result)
        {
            return JS_UNDEFINED;
        }

        return Reflection::QuickJSTypeConverter::ToJSValue(ctx, result);
    }

    JSValue NodeGetComponent(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                             int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)magic;

        if (argc < 1)
        {
            return JS_UNDEFINED;
        }

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);

        const char* componentType = JS_ToCString(ctx, argv[0]);
        if (!componentType)
        {
            return JS_UNDEFINED;
        }

        Assets::Node* node = FindNodeById(nodeId);
        if (!node)
        {
            JS_FreeCString(ctx, componentType);
            return JS_UNDEFINED;
        }

        Assets::Component* component = node->GetComponentByTypeName(componentType);
        JSValue result = CreateComponentObject(ctx, component, nodeId, componentType);
        JS_FreeCString(ctx, componentType);
        return result;
    }

    JSValue NodeRecalcTransform(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv,
                                int magic, JSValueConst* data)
    {
        (void)thisVal;
        (void)magic;

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, data[0]);
        Assets::Node* node = FindNodeById(nodeId);
        if (!node)
        {
            return JS_UNDEFINED;
        }

        const bool full = argc < 1 || JS_ToBool(ctx, argv[0]) != 0;
        node->RecalcTransform(full);
        return JS_UNDEFINED;
    }

    JSValue CreateNodeObject(JSContext* ctx, uint32_t nodeId)
    {
        Assets::Node* node = FindNodeById(nodeId);
        if (!node)
        {
            return JS_UNDEFINED;
        }

        entt::meta_type metaType = entt::resolve<Assets::Node>();
        JSValue obj = JS_NewObject(ctx);

        auto properties = Reflection::PropertyAccessor::GetProperties(metaType);
        for (const auto& prop : properties)
        {
            if (!prop.meta.IsJSExposed())
            {
                continue;
            }

            JSValue data[2];
            data[0] = JS_NewUint32(ctx, nodeId);
            data[1] = JS_NewString(ctx, prop.name.c_str());

            JSValue getter = JS_NewCFunctionData(ctx, NodePropertyGetter, 0, 0, 2, data);
            JSValue setter = JS_UNDEFINED;
            if (!prop.meta.IsReadOnly())
            {
                setter = JS_NewCFunctionData(ctx, NodePropertySetter, 1, 0, 2, data);
            }

            JSAtom propAtom = JS_NewAtom(ctx, prop.name.c_str());
            JS_DefinePropertyGetSet(ctx, obj, propAtom, getter, setter,
                                    JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE);
            JS_FreeAtom(ctx, propAtom);
        }

        JSValue componentData[1];
        componentData[0] = JS_NewUint32(ctx, nodeId);
        JS_SetPropertyStr(ctx, obj, "GetComponent",
                          JS_NewCFunctionData(ctx, NodeGetComponent, 1, 0, 1, componentData));

        JSValue recalcData[1];
        recalcData[0] = JS_NewUint32(ctx, nodeId);
        JS_SetPropertyStr(ctx, obj, "RecalcTransform",
                          JS_NewCFunctionData(ctx, NodeRecalcTransform, 1, 0, 1, recalcData));

        for (auto&& [id, func] : metaType.func())
        {
            const char* funcName = func.name();
            if (!funcName)
            {
                continue;
            }

            if (std::strcmp(funcName, "GetComponent") == 0)
            {
                continue;
            }

            JSValue data[2];
            data[0] = JS_NewUint32(ctx, nodeId);
            data[1] = JS_NewString(ctx, funcName);

            JSValue jsFunc = JS_NewCFunctionData(ctx, NodeMethodInvoker, ToQuickJSArity(func.arity()), 0, 2, data);
            JS_SetPropertyStr(ctx, obj, funcName, jsFunc);
        }

        return obj;
    }

    JSValue SceneGetNodeById(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;

        if (argc < 1)
        {
            return JS_UNDEFINED;
        }

        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, argv[0]);
        return CreateNodeObject(ctx, nodeId);
    }

    JSValue SceneAddLambertianMaterial(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "AddLambertianMaterial expects a Vec3 color");
        }

        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return JS_UNDEFINED;
        }

        glm::vec3 color(1.0f);
        JSValueToVec3(ctx, argv[0], color);
        return JS_NewUint32(ctx, SceneBuilder::AddLambertianMaterialToScene(engine->GetScene(), color));
    }

    JSValue SceneAddDiffuseLightMaterial(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "AddDiffuseLightMaterial expects a Vec3 color");
        }

        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return JS_UNDEFINED;
        }

        glm::vec3 color(1.0f);
        JSValueToVec3(ctx, argv[0], color);
        float parsedIntensity = 1.0f;
        if (argc >= 2)
        {
            JSToFloat(ctx, argv[1], parsedIntensity);
        }
        return JS_NewUint32(ctx, SceneBuilder::AddDiffuseLightMaterialToScene(engine->GetScene(), color, parsedIntensity));
    }

    JSValue SceneAddRenderNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "AddRenderNode expects a render node spec");
        }

        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return JS_UNDEFINED;
        }

        Assets::Scene& scene = engine->GetScene();
        auto node = CreateRenderNodeFromSpec(ctx,
                                             argv[0],
                                             scene.GenerateInstanceId(),
                                             static_cast<uint32_t>(scene.Models().size()));
        if (!node)
        {
            return JS_ThrowRangeError(ctx, "AddRenderNode received an invalid modelId");
        }
        const uint32_t nodeId = node->GetInstanceId();
        scene.AddNode(node);
        scene.MarkDirty();
        return JS_NewUint32(ctx, nodeId);
    }

    JSValue SceneBuildAddProceduralModel(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (!GQuickJSSceneBuild.IsValid())
        {
            return JS_ThrowInternalError(ctx, "SceneBuild is only available during onBeforeSceneRebuild");
        }
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "AddProceduralModel expects a model spec");
        }

        const std::string type = GetObjectString(ctx, argv[0], "type", "box");
        if (type == "sphere")
        {
            const glm::vec3 center = GetObjectVec3(ctx, argv[0], "center");
            const float radius = std::max(0.001f, GetObjectFloat(ctx, argv[0], "radius", 1.0f));
            GQuickJSSceneBuild.models->push_back(Assets::FProcModel::CreateSphere(center, radius));
        }
        else if (type == "box")
        {
            const glm::vec3 minPos = GetObjectVec3(ctx, argv[0], "min", glm::vec3(-0.5f));
            const glm::vec3 maxPos = GetObjectVec3(ctx, argv[0], "max", glm::vec3(0.5f));
            GQuickJSSceneBuild.models->push_back(Assets::FProcModel::CreateBox(minPos, maxPos));
        }
        else
        {
            return JS_ThrowRangeError(ctx, "Unknown procedural model type '%s'", type.c_str());
        }

        return JS_NewUint32(ctx, static_cast<uint32_t>(GQuickJSSceneBuild.models->size() - 1));
    }

    JSValue SceneBuildAddLambertianMaterial(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (!GQuickJSSceneBuild.IsValid())
        {
            return JS_ThrowInternalError(ctx, "SceneBuild is only available during onBeforeSceneRebuild");
        }
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "AddLambertianMaterial expects a Vec3 color");
        }

        glm::vec3 color(1.0f);
        JSValueToVec3(ctx, argv[0], color);
        return JS_NewUint32(ctx, SceneBuilder::AddLambertianMaterial(*GQuickJSSceneBuild.materials, color));
    }

    JSValue SceneBuildAddDiffuseLightMaterial(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (!GQuickJSSceneBuild.IsValid())
        {
            return JS_ThrowInternalError(ctx, "SceneBuild is only available during onBeforeSceneRebuild");
        }
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "AddDiffuseLightMaterial expects a Vec3 color");
        }

        glm::vec3 color(1.0f);
        JSValueToVec3(ctx, argv[0], color);
        float intensity = 1.0f;
        if (argc >= 2)
        {
            JSToFloat(ctx, argv[1], intensity);
        }
        return JS_NewUint32(ctx, SceneBuilder::AddDiffuseLightMaterial(*GQuickJSSceneBuild.materials, color, intensity));
    }

    JSValue SceneBuildAddRenderNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (!GQuickJSSceneBuild.IsValid())
        {
            return JS_ThrowInternalError(ctx, "SceneBuild is only available during onBeforeSceneRebuild");
        }
        if (argc < 1 || !JS_IsObject(argv[0]))
        {
            return JS_ThrowTypeError(ctx, "AddRenderNode expects a render node spec");
        }

        auto node = CreateRenderNodeFromSpec(ctx,
                                             argv[0],
                                             GenerateBuildInstanceId(*GQuickJSSceneBuild.nodes),
                                             static_cast<uint32_t>(GQuickJSSceneBuild.models->size()));
        if (!node)
        {
            return JS_ThrowRangeError(ctx, "AddRenderNode received an invalid modelId");
        }

        const uint32_t nodeId = node->GetInstanceId();
        GQuickJSSceneBuild.nodes->push_back(node);
        return JS_NewUint32(ctx, nodeId);
    }

    JSValue SceneRemoveNodeById(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_UNDEFINED;
        }
        uint32_t nodeId = 0;
        JS_ToUint32(ctx, &nodeId, argv[0]);
        if (auto* engine = NextEngine::GetInstance())
        {
            engine->GetScene().RemoveNodeByInstanceId(nodeId);
            engine->GetScene().MarkDirty();
        }
        return JS_UNDEFINED;
    }

    JSValue SceneMarkTransformDirty(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)ctx;
        (void)thisVal;
        (void)argc;
        (void)argv;
        if (auto* engine = NextEngine::GetInstance())
        {
            engine->GetScene().MarkTransformDirty();
        }
        return JS_UNDEFINED;
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

    JSValue InputIsKeyDown(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const std::string name = ToCString(ctx, argv[0]);
        if (name == "any")
        {
            return JS_NewBool(ctx, !GQuickJSInput.keysDown.empty());
        }
        const auto key = KeyNameToCode(name);
        return JS_NewBool(ctx, key.has_value() && GQuickJSInput.keysDown.contains(*key));
    }

    JSValue InputIsKeyPressed(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const std::string name = ToCString(ctx, argv[0]);
        if (name == "any")
        {
            return JS_NewBool(ctx,
                              !GQuickJSInput.keysPressed.empty() ||
                                  !GQuickJSInput.mouseButtonsPressed.empty() ||
                                  !GQuickJSInput.gamepadButtonsPressed.empty());
        }
        const auto key = KeyNameToCode(name);
        return JS_NewBool(ctx, key.has_value() && GQuickJSInput.keysPressed.contains(*key));
    }

    JSValue InputIsMouseButtonDown(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        uint32_t button = 0;
        JS_ToUint32(ctx, &button, argv[0]);
        return JS_NewBool(ctx, GQuickJSInput.mouseButtonsDown.contains(static_cast<uint8_t>(button)));
    }

    JSValue InputIsMouseButtonPressed(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        uint32_t button = 0;
        JS_ToUint32(ctx, &button, argv[0]);
        return JS_NewBool(ctx, GQuickJSInput.mouseButtonsPressed.contains(static_cast<uint8_t>(button)));
    }

    JSValue InputGetGamepadButton(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const auto button = GamepadNameToButton(ToCString(ctx, argv[0]));
        return JS_NewBool(ctx, button.has_value() && GQuickJSInput.gamepadButtonsDown.contains(*button));
    }

    JSValue AudioPlaySfx(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_UNDEFINED;
        }
        const std::string path = ToCString(ctx, argv[0]);
        double volume = 1.0;
        if (argc >= 2)
        {
            JS_ToFloat64(ctx, &volume, argv[1]);
        }
        if (auto* engine = NextEngine::GetInstance(); engine && engine->GetAudio())
        {
            engine->GetAudio()->PlaySfx(path, static_cast<float>(volume));
        }
        return JS_UNDEFINED;
    }

    JSValue AudioPlayMusic(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_UNDEFINED;
        }
        const std::string path = ToCString(ctx, argv[0]);
        double volume = 1.0;
        if (argc >= 2)
        {
            JS_ToFloat64(ctx, &volume, argv[1]);
        }
        if (auto* engine = NextEngine::GetInstance(); engine && engine->GetAudio())
        {
            engine->GetAudio()->PlayMusic(path, static_cast<float>(volume));
        }
        return JS_UNDEFINED;
    }

    JSValue AudioStopMusic(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)ctx;
        (void)thisVal;
        (void)argc;
        (void)argv;
        if (auto* engine = NextEngine::GetInstance(); engine && engine->GetAudio())
        {
            engine->GetAudio()->StopMusic();
        }
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

    JSValue UIBegin(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return JS_NewBool(ctx, false);
        }
        const std::string name = ToCString(ctx, argv[0]);
        int flags = 0;
        if (argc >= 2)
        {
            JS_ToInt32(ctx, &flags, argv[1]);
        }
        return JS_NewBool(ctx, ImGui::Begin(name.c_str(), nullptr, flags));
    }

    JSValue UIEnd(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)ctx;
        (void)thisVal;
        (void)argc;
        (void)argv;
        ImGui::End();
        return JS_UNDEFINED;
    }

    JSValue UIText(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc >= 1)
        {
            const std::string text = ToCString(ctx, argv[0]);
            ImGui::TextUnformatted(text.c_str());
        }
        return JS_UNDEFINED;
    }

    JSValue UISetCursorPos(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc >= 2)
        {
            float x = 0.0f;
            float y = 0.0f;
            JSToFloat(ctx, argv[0], x);
            JSToFloat(ctx, argv[1], y);
            ImGui::SetCursorPos(ImVec2(x, y));
        }
        return JS_UNDEFINED;
    }

    JSValue UIGetWindowSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        (void)argc;
        (void)argv;
        const ImVec2 size = ImGui::GetWindowSize();
        return Vec2ToJS(ctx, glm::vec2(size.x, size.y));
    }

    JSValue UISetWindowFontScale(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc >= 1)
        {
            float scale = 1.0f;
            JSToFloat(ctx, argv[0], scale);
            ImGui::SetWindowFontScale(scale);
        }
        return JS_UNDEFINED;
    }

    JSValue UICalcTextSize(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 1)
        {
            return Vec2ToJS(ctx, glm::vec2(0.0f));
        }

        const std::string text = ToCString(ctx, argv[0]);
        float scale = 1.0f;
        if (argc >= 2)
        {
            JSToFloat(ctx, argv[1], scale);
        }

        const ImVec2 rawSize = ImGui::CalcTextSize(text.c_str());
        return Vec2ToJS(ctx, glm::vec2(rawSize.x * scale, rawSize.y * scale));
    }

    JSValue UIDrawText(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
    {
        (void)thisVal;
        if (argc < 3)
        {
            return JS_UNDEFINED;
        }

        const std::string text = ToCString(ctx, argv[0]);
        float x = 0.0f;
        float y = 0.0f;
        float scale = 1.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        JSToFloat(ctx, argv[1], x);
        JSToFloat(ctx, argv[2], y);
        if (argc >= 4) JSToFloat(ctx, argv[3], scale);
        if (argc >= 5) JSToFloat(ctx, argv[4], r);
        if (argc >= 6) JSToFloat(ctx, argv[5], g);
        if (argc >= 7) JSToFloat(ctx, argv[6], b);
        if (argc >= 8) JSToFloat(ctx, argv[7], a);

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return JS_UNDEFINED;
        }

        const auto toByte = [](float value) -> int
        {
            return static_cast<int>(glm::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        drawList->AddText(nullptr,
                          ImGui::GetFontSize() * std::max(scale, 0.01f),
                          ImVec2(x, y),
                          IM_COL32(toByte(r), toByte(g), toByte(b), toByte(a)),
                          text.c_str());
        return JS_UNDEFINED;
    }
    

    void AddDynamicSceneMethods(JSContext* ctx, JSValue proto)
    {
        if (!JS_IsObject(proto))
        {
            return;
        }

        JS_SetPropertyStr(ctx, proto, "GetNodeById",
                          JS_NewCFunction(ctx, SceneGetNodeById, "GetNodeById", 1));
        JS_SetPropertyStr(ctx, proto, "AddLambertianMaterial",
                          JS_NewCFunction(ctx, SceneAddLambertianMaterial, "AddLambertianMaterial", 1));
        JS_SetPropertyStr(ctx, proto, "AddDiffuseLightMaterial",
                          JS_NewCFunction(ctx, SceneAddDiffuseLightMaterial, "AddDiffuseLightMaterial", 2));
        JS_SetPropertyStr(ctx, proto, "AddRenderNode",
                          JS_NewCFunction(ctx, SceneAddRenderNode, "AddRenderNode", 1));
        JS_SetPropertyStr(ctx, proto, "RemoveNodeById",
                          JS_NewCFunction(ctx, SceneRemoveNodeById, "RemoveNodeById", 1));
        JS_SetPropertyStr(ctx, proto, "MarkTransformDirty",
                          JS_NewCFunction(ctx, SceneMarkTransformDirty, "MarkTransformDirty", 0));
    }

    void BindScenePrototype(JSContext* ctx)
    {
        JSValue proto = JS_GetClassProto(ctx, qjs::js_traits<std::shared_ptr<Assets::Scene>>::QJSClassId);
        AddDynamicSceneMethods(ctx, proto);
        JS_FreeValue(ctx, proto);
    }

    std::string BuildTypeScriptDefinitions()
    {
        std::string result;
        result += "export interface Vec2 { x: number; y: number; }\n";
        result += "export interface Vec3 { x: number; y: number; z: number; }\n";
        result += "export interface Vec4 { x: number; y: number; z: number; w: number; }\n";
        result += "export interface Quat { x: number; y: number; z: number; w: number; }\n\n";

        result += "export class NextEngine {\n";
        result += "    GetTotalFrames(): number;\n";
        result += "    GetTime(): number;\n";
        result += "    GetDeltaSeconds(): number;\n";
        result += "    GetSmoothDeltaSeconds(): number;\n";
        result += "    RegisterJSCallback(arg0: any): void;\n";
        result += "}\n";
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Assets::Node>("Node");
        result += "export interface Node {\n";
        result += "    RecalcTransform(full?: boolean): void;\n";
        result += "}\n";
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Assets::Scene>("Scene");
        result += "export interface Scene {\n";
        result += "    GetNodeById(nodeId: number): Node;\n";
        result += "    AddLambertianMaterial(color: Vec3): number;\n";
        result += "    AddDiffuseLightMaterial(color: Vec3, intensity?: number): number;\n";
        result += "    AddRenderNode(spec: RenderNodeSpec): number;\n";
        result += "    RemoveNodeById(nodeId: number): void;\n";
        result += "    MarkTransformDirty(): void;\n";
        result += "}\n";

        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::RenderComponent>("RenderComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::PhysicsComponent>("PhysicsComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::SkinnedMeshComponent>("SkinnedMeshComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateEnumTypeScriptDef<Runtime::ENodeMobility>("ENodeMobility");

        result += "\nexport namespace Global {\n";
        result += "    function spdlog(level: string, ...args: any[]): void;\n";
        result += "    function GetEngine(): NextEngine;\n";
        result += "    function GetScene(): Scene;\n";
        result += "}\n";
        result += "\nexport namespace Input {\n";
        result += "    function IsKeyDown(name: string): boolean;\n";
        result += "    function IsKeyPressed(name: string): boolean;\n";
        result += "    function IsMouseButtonDown(button: number): boolean;\n";
        result += "    function IsMouseButtonPressed(button: number): boolean;\n";
        result += "    function GetGamepadButton(name: string): boolean;\n";
        result += "}\n";
        result += "\nexport namespace Audio {\n";
        result += "    function PlaySfx(path: string, volume?: number): void;\n";
        result += "    function PlayMusic(path: string, volume?: number): void;\n";
        result += "    function StopMusic(): void;\n";
        result += "}\n";
        result += "\nexport namespace UI {\n";
        result += "    function Begin(name: string, flags?: number): boolean;\n";
        result += "    function End(): void;\n";
        result += "    function Text(text: string): void;\n";
        result += "    function SetCursorPos(x: number, y: number): void;\n";
        result += "    function GetWindowSize(): Vec2;\n";
        result += "    function SetWindowFontScale(scale: number): void;\n";
        result += "    function GetScreenSize(): Vec2;\n";
        result += "    function CalcTextSize(text: string, scale?: number): Vec2;\n";
        result += "    function DrawText(text: string, x: number, y: number, scale?: number, r?: number, g?: number, b?: number, a?: number): void;\n";
        result += "}\n";
        result += "\nexport type ProceduralModelSpec =\n";
        result += "    | { type: \"box\"; min: Vec3; max: Vec3 }\n";
        result += "    | { type: \"sphere\"; center?: Vec3; radius: number };\n";
        result += "export interface RenderNodeSpec {\n";
        result += "    name: string;\n";
        result += "    modelId: number;\n";
        result += "    materialId: number;\n";
        result += "    translation?: Vec3;\n";
        result += "    scale?: Vec3;\n";
        result += "    visible?: boolean;\n";
        result += "}\n";
        result += "\nexport namespace SceneBuild {\n";
        result += "    function AddProceduralModel(spec: ProceduralModelSpec): number;\n";
        result += "    function AddLambertianMaterial(color: Vec3): number;\n";
        result += "    function AddDiffuseLightMaterial(color: Vec3, intensity?: number): number;\n";
        result += "    function AddRenderNode(spec: RenderNodeSpec): number;\n";
        result += "}\n";
        result += "export type InputEventType = \"keyDown\" | \"keyUp\" | \"mouseButtonDown\" | \"mouseButtonUp\" | \"gamepadButtonDown\" | \"gamepadButtonUp\";\n";
        result += "export interface InputEvent {\n";
        result += "    type: InputEventType;\n";
        result += "    key?: string;\n";
        result += "    mouseButton?: number;\n";
        result += "    gamepadButton?: string;\n";
        result += "    repeated?: boolean;\n";
        result += "}\n";
        result += "\nexport interface LifecycleHooks {\n";
        result += "    onInit?: () => void;\n";
        result += "    onDestroy?: () => void;\n";
        result += "    onBeforeSceneRebuild?: () => void;\n";
        result += "    onSceneLoaded?: () => void;\n";
        result += "    onRenderUI?: () => boolean | void;\n";
        result += "    onInputEvent?: (event: InputEvent) => boolean | void;\n";
        result += "}\n";
        result += "export interface CameraOverride { position: Vec3; target: Vec3; up: Vec3; fov: number; }\n";
        result += "export function RegisterLifecycleHooks(hooks: LifecycleHooks): void;\n";
        result += "export function LoadJson(path: string): any;\n";
        result += "export function RequestLoadScene(filename: string): void;\n";
        result += "export function RequestClose(): void;\n";
        result += "export function GetScreenSize(): Vec2;\n";
        result += "export function SetOverrideCamera(camera: CameraOverride): void;\n";
        result += "export function IsReplayMode(): boolean;\n";
        result += "export function WriteFile(path: string, content: string): void;\n";

        return result;
    }

    void UpdateTypeScriptDefinitions(const std::filesystem::path& tsconfigPath)
    {
        namespace fs = std::filesystem;

        if (tsconfigPath.empty())
        {
            return;
        }

        const fs::path outputPath = tsconfigPath.parent_path() / "Engine.d.ts";
        const std::string content = BuildTypeScriptDefinitions();

        std::ifstream reader(outputPath, std::ios::binary);
        if (reader)
        {
            std::string existing((std::istreambuf_iterator<char>(reader)), std::istreambuf_iterator<char>());
            if (existing == content)
            {
                return;
            }
        }

        std::ofstream writer(outputPath, std::ios::binary);
        if (!writer)
        {
            SPDLOG_WARN("Failed to write TypeScript definitions to {}", outputPath.string());
            return;
        }

        writer << content;
    }
#endif
}

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
