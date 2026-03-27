#include "Runtime/Subsystems/QuickJSEngine.hpp"

#include "Runtime/Engine.hpp"
#include "Assets/Core/Scene.hpp"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Runtime/Reflection/QuickJSReflectionBridge.h"
#include "Runtime/Reflection/QuickJSTypeConverter.h"
#include "Utilities/FileHelper.hpp"
#include "Runtime/Platform/PlatformCommon.h"

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

    Assets::Node* FindNodeById(uint32_t nodeId)
    {
        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return nullptr;
        }

        auto* scene = engine->GetScenePtr();
        if (!scene)
        {
            return nullptr;
        }

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

        auto* scene = engine->GetScenePtr();
        if (!scene)
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

        auto* scene = engine->GetScenePtr();
        if (!scene)
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

        auto* scene = engine->GetScenePtr();
        if (!scene)
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
    

    void BindScenePrototype(JSContext* ctx)
    {
        JSValue proto = JS_GetClassProto(ctx, qjs::js_traits<std::shared_ptr<Assets::Scene>>::QJSClassId);
        if (!JS_IsObject(proto))
        {
            JS_FreeValue(ctx, proto);
            return;
        }

        JS_SetPropertyStr(ctx, proto, "GetNodeById",
                          JS_NewCFunction(ctx, SceneGetNodeById, "GetNodeById", 1));

        JS_FreeValue(ctx, proto);
    }

    std::string BuildTypeScriptDefinitions()
    {
        std::string result;
        result += "export interface Vec2 { x: number; y: number; }\n";
        result += "export interface Vec3 { x: number; y: number; z: number; }\n";
        result += "export interface Vec4 { x: number; y: number; z: number; w: number; }\n";
        result += "export interface Quat { x: number; y: number; z: number; w: number; }\n\n";

        result += "export class NextComponent {\n";
        result += "    name_: string;\n";
        result += "    id_: number;\n";
        result += "}\n\n";

        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<NextEngine>("NextEngine");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Assets::Node>("Node");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Assets::Scene>("Scene");

        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::RenderComponent>("RenderComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::PhysicsComponent>("PhysicsComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::SkinnedMeshComponent>("SkinnedMeshComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateEnumTypeScriptDef<Runtime::ENodeMobility>("ENodeMobility");

        result += "\nexport namespace Global {\n";
        result += "    function spdlog(level: string, ...args: any[]): void;\n";
        result += "    function GetEngine(): NextEngine;\n";
        result += "}\n";

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

    try
    {
        auto& module = context_->addModule("Engine");
        auto globalNamespace = context_->newObject();
        globalNamespace.add<&Spdlog>("spdlog");
        globalNamespace.add<&GetEngine>("GetEngine");
        module.add("Global", std::move(globalNamespace));

        module.class_<NextEngine>("NextEngine")
                .fun<&NextEngine::GetTotalFrames>("GetTotalFrames")
                .fun<&NextEngine::GetTestNumber>("GetTestNumber")
                .fun<&NextEngine::RegisterJSCallback>("RegisterJSCallback")
                .fun<&NextEngine::GetScenePtr>("GetScenePtr");
        module.class_<Assets::Scene>("Scene")
                .fun<&Assets::Scene::GetIndicesCount>("GetIndicesCount")
                .fun<&Assets::Scene::FindNodeIdWithComponent>("FindNodeIdWithComponent");
        module.class_<NextComponent>("NextComponent")
                .constructor<>()
                .fun<&NextComponent::name_>("name_")
                .fun<&NextComponent::id_>("id_");

        qjs::Context* jsContext = context_.get();
        BindScenePrototype(jsContext->ctx);

        if (editorBindingsCallback_)
        {
            editorBindingsCallback_(jsContext->ctx);
        }

        std::vector<uint8_t> scriptBuffer;
        if (Utilities::Package::FPackageFileSystem::GetInstance().LoadFile("assets/scripts/test.js", scriptBuffer))
        {
            context_->eval(std::string_view(reinterpret_cast<char*>(scriptBuffer.data())), "<import>", JS_EVAL_TYPE_MODULE);
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

bool QuickJSEngine::EnsureTscAvailable(const std::filesystem::path& localTsc)
{
    if (tscChecked_)
    {
        return tscAvailable_;
    }

    tscChecked_ = true;
    if (!localTsc.empty() && std::filesystem::exists(localTsc))
    {
        tscAvailable_ = true;
        return true;
    }

#if IOS
    tscAvailable_ = false;
#elif WIN32
    int result = std::system("where tsc >nul 2>&1");
    tscAvailable_ = (result == 0);
#else
    int result = std::system("command -v tsc >/dev/null 2>&1");
    tscAvailable_ = (result == 0);
#endif

    if (!tscAvailable_)
    {
        SPDLOG_WARN("TypeScript compiler not found; hot reload disabled.");
    }

    return tscAvailable_;
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

        const bool forceCompile = std::getenv("NEXTENGINE_FORCE_TSC") != nullptr;
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

        const fs::path localTsc = fs::current_path() / "tsc";
        if (!EnsureTscAvailable(localTsc))
        {
            return false;
        }

        std::vector<std::string> commands;
#if WIN32
        commands.emplace_back(fmt::format("tsc -p \"{}\" --outDir \"{}\"", tsconfigPath.string(), outputDir.string()));
#else
        if (fs::exists(localTsc, ec))
        {
            commands.emplace_back(fmt::format("\"{}\" -p \"{}\" --outDir \"{}\"", localTsc.string(), tsconfigPath.string(), outputDir.string()));
        }
        commands.emplace_back(fmt::format("tsc -p \"{}\" --outDir \"{}\"", tsconfigPath.string(), outputDir.string()));
#endif

        for (const std::string& command : commands)
        {
            if (command.empty())
            {
                continue;
            }

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

            SPDLOG_WARN("TypeScript compile command failed with code {}", result);
        }

        SPDLOG_WARN("Unable to compile TypeScript sources; continuing with existing JavaScript outputs.");
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
