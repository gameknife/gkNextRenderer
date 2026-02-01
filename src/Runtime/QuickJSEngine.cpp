#include "QuickJSEngine.hpp"

#include "Engine.hpp"
#include "Assets/Scene.hpp"
#include "Assets/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Runtime/Reflection/QuickJSReflectionBridge.h"
#include "Runtime/Reflection/QuickJSTypeConverter.h"
#include "Utilities/FileHelper.hpp"
#include "Platform/PlatformCommon.h"

#include <spdlog/spdlog.h>
#include <spdlog/stopwatch.h>
#include <fstream>
#include <entt/core/hashed_string.hpp>
#include <cstdlib>

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

    void Println(qjs::rest<std::string> args)
    {
        for (auto const& arg : args)
        {
            SPDLOG_INFO("{}", arg);
        }
    }

    NextEngine* GetEngine()
    {
        return NextEngine::GetInstance();
    }

    Assets::Component* FindComponentByTypeName(Assets::Scene& scene, uint32_t nodeId, const std::string& componentType)
    {
        auto* node = scene.GetNodeByInstanceId(nodeId);
        if (!node)
        {
            return nullptr;
        }

        for (const auto& component : node->GetComponents())
        {
            if (!component)
            {
                continue;
            }

            if (component->GetTypeName() == componentType)
            {
                return component.get();
            }
        }

        return nullptr;
    }

    Assets::Node* FindNodeById(Assets::Scene& scene, uint32_t nodeId)
    {
        return scene.GetNodeByInstanceId(nodeId);
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

        Assets::Component* component = FindComponentByTypeName(*scene, nodeId, componentType);
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

        Assets::Component* component = FindComponentByTypeName(*scene, nodeId, componentType);
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

        Assets::Component* component = FindComponentByTypeName(*scene, nodeId, componentType);
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

            JSValue jsFunc = JS_NewCFunctionData(ctx, ComponentMethodInvoker, func.arity(), 0, 3, data);
            JS_SetPropertyStr(ctx, obj, funcName, jsFunc);
        }

        return obj;
    }

    int32_t FindNodeIdWithComponent(Assets::Scene& scene, const std::string& componentType)
    {
        for (const auto& node : scene.Nodes())
        {
            if (!node)
            {
                continue;
            }

            for (const auto& component : node->GetComponents())
            {
                if (component && component->GetTypeName() == componentType)
                {
                    return static_cast<int32_t>(node->GetInstanceId());
                }
            }
        }

        return -1;
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
        result += "    GetTestNumber(): number;\n";
        result += "    RegisterJSCallback(callback: (param: number) => void): void;\n";
        result += "    GetScenePtr(): Scene;\n";
        result += "}\n\n";

        result += "export class NextComponent {\n";
        result += "    name_: string;\n";
        result += "    id_: number;\n";
        result += "}\n\n";

        result += "export class Scene {\n";
        result += "    GetIndicesCount(): number;\n";
        result += "}\n\n";

        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::RenderComponent>("RenderComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::PhysicsComponent>("PhysicsComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateTypeScriptDef<Runtime::SkinnedMeshComponent>("SkinnedMeshComponent");
        result += Reflection::QuickJSReflectionBridge::GenerateEnumTypeScriptDef<Runtime::ENodeMobility>("ENodeMobility");

        result += "\nexport function println(...args: any[]): void;\n";
        result += "export function GetEngine(): NextEngine;\n";
        result += "export function FindNodeIdWithComponent(componentType: string): number;\n";
        result += "export function GetNodeName(nodeId: number): string;\n";
        result += "export function GetNodeTranslation(nodeId: number): Vec3;\n";
        result += "export function GetComponent(nodeId: number, componentType: string): any;\n";
        result += "export function GetComponentProperty(nodeId: number, componentType: string, propertyName: string): any;\n";
        result += "export function SetComponentProperty(nodeId: number, componentType: string, propertyName: string, value: any): boolean;\n";
        result += "export function CallComponentFunction(nodeId: number, componentType: string, functionName: string, ...args: any[]): any;\n";

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
        module.function<&Println>("println");
        module.function<&GetEngine>("GetEngine");

        module.class_<NextEngine>("NextEngine")
                .fun<&NextEngine::GetTotalFrames>("GetTotalFrames")
                .fun<&NextEngine::GetTestNumber>("GetTestNumber")
                .fun<&NextEngine::RegisterJSCallback>("RegisterJSCallback")
                .fun<&NextEngine::GetScenePtr>("GetScenePtr");
        module.class_<Assets::Scene>("Scene")
                .fun<&Assets::Scene::GetIndicesCount>("GetIndicesCount");
        module.class_<NextComponent>("NextComponent")
                .constructor<>()
                .fun<&NextComponent::name_>("name_")
                .fun<&NextComponent::id_>("id_");

        qjs::Context* jsContext = context_.get();
        module.function("FindNodeIdWithComponent", [](const std::string& componentType) -> int32_t {
            auto* engine = NextEngine::GetInstance();
            if (!engine)
            {
                return -1;
            }

            auto* scene = engine->GetScenePtr();
            if (!scene)
            {
                return -1;
            }

            return FindNodeIdWithComponent(*scene, componentType);
        });

        module.function("GetNodeName", [](uint32_t nodeId) -> std::string {
            auto* engine = NextEngine::GetInstance();
            if (!engine)
            {
                return {};
            }

            auto* scene = engine->GetScenePtr();
            if (!scene)
            {
                return {};
            }

            auto* node = FindNodeById(*scene, nodeId);
            if (!node)
            {
                return {};
            }

            return node->GetName();
        });

        module.function("GetNodeTranslation", [jsContext](uint32_t nodeId) -> JSValue {
            auto* engine = NextEngine::GetInstance();
            if (!engine)
            {
                return JS_UNDEFINED;
            }

            auto* scene = engine->GetScenePtr();
            if (!scene)
            {
                return JS_UNDEFINED;
            }

            auto* node = FindNodeById(*scene, nodeId);
            if (!node)
            {
                return JS_UNDEFINED;
            }

            const glm::vec3 translation = node->Translation();
            JSValue obj = JS_NewObject(jsContext->ctx);
            JS_SetPropertyStr(jsContext->ctx, obj, "x", JS_NewFloat64(jsContext->ctx, translation.x));
            JS_SetPropertyStr(jsContext->ctx, obj, "y", JS_NewFloat64(jsContext->ctx, translation.y));
            JS_SetPropertyStr(jsContext->ctx, obj, "z", JS_NewFloat64(jsContext->ctx, translation.z));
            return obj;
        });

        module.function("GetComponent", [jsContext](uint32_t nodeId, const std::string& componentType) -> JSValue {
            auto* engine = NextEngine::GetInstance();
            if (!engine)
            {
                return JS_UNDEFINED;
            }

            auto* scene = engine->GetScenePtr();
            if (!scene)
            {
                return JS_UNDEFINED;
            }

            Assets::Component* component = FindComponentByTypeName(*scene, nodeId, componentType);
            if (!component)
            {
                return JS_UNDEFINED;
            }

            return CreateComponentObject(jsContext->ctx, component, nodeId, componentType);
        });

        module.function("GetComponentProperty", [jsContext](uint32_t nodeId, const std::string& componentType,
                                                           const std::string& propertyName) -> JSValue {
            auto* engine = NextEngine::GetInstance();
            if (!engine)
            {
                return JS_UNDEFINED;
            }

            auto* scene = engine->GetScenePtr();
            if (!scene)
            {
                return JS_UNDEFINED;
            }

            Assets::Component* component = FindComponentByTypeName(*scene, nodeId, componentType);
            if (!component)
            {
                SPDLOG_WARN("Component '{}' not found on node {}", componentType, nodeId);
                return JS_UNDEFINED;
            }

            entt::meta_type metaType = component->GetMetaType();
            entt::meta_any value = Reflection::PropertyAccessor::GetPropertyValue(metaType, component, propertyName);
            if (!value)
            {
                SPDLOG_WARN("Property '{}' not found on component '{}'", propertyName, componentType);
                return JS_UNDEFINED;
            }

            return Reflection::QuickJSTypeConverter::ToJSValue(jsContext->ctx, value);
        });

        module.function("SetComponentProperty", [jsContext](uint32_t nodeId, const std::string& componentType,
                                                           const std::string& propertyName, qjs::Value value) -> bool {
            auto* engine = NextEngine::GetInstance();
            if (!engine)
            {
                return false;
            }

            auto* scene = engine->GetScenePtr();
            if (!scene)
            {
                return false;
            }

            Assets::Component* component = FindComponentByTypeName(*scene, nodeId, componentType);
            if (!component)
            {
                SPDLOG_WARN("Component '{}' not found on node {}", componentType, nodeId);
                return false;
            }

            entt::meta_type metaType = component->GetMetaType();
            auto data = metaType.data(entt::hashed_string::value(propertyName.c_str()));
            if (!data)
            {
                SPDLOG_WARN("Property '{}' not found on component '{}'", propertyName, componentType);
                return false;
            }

            entt::meta_type valueType = data.type();
            if (!Reflection::QuickJSTypeConverter::IsTypeSupported(valueType))
            {
                SPDLOG_WARN("Property '{}' on component '{}' is not supported for JS conversion", propertyName, componentType);
                return false;
            }
            entt::meta_any converted = Reflection::QuickJSTypeConverter::FromJSValue(jsContext->ctx, value.v, valueType);
            if (!converted)
            {
                SPDLOG_WARN("Failed to convert value for property '{}' on component '{}'", propertyName, componentType);
                return false;
            }

            return Reflection::PropertyAccessor::SetPropertyValue(metaType, component, propertyName, converted);
        });

        module.function("CallComponentFunction", [jsContext](uint32_t nodeId, const std::string& componentType,
                                                            const std::string& functionName, qjs::rest<qjs::Value> args) -> JSValue {
            auto* engine = NextEngine::GetInstance();
            if (!engine)
            {
                return JS_UNDEFINED;
            }

            auto* scene = engine->GetScenePtr();
            if (!scene)
            {
                return JS_UNDEFINED;
            }

            Assets::Component* component = FindComponentByTypeName(*scene, nodeId, componentType);
            if (!component)
            {
                SPDLOG_WARN("Component '{}' not found on node {}", componentType, nodeId);
                return JS_UNDEFINED;
            }

            entt::meta_type metaType = component->GetMetaType();
            auto function = metaType.func(entt::hashed_string::value(functionName.c_str()));
            if (!function)
            {
                SPDLOG_WARN("Function '{}' not found on component '{}'", functionName, componentType);
                return JS_UNDEFINED;
            }

            if (function.arity() != 0 || !args.empty())
            {
                SPDLOG_WARN("Function '{}' on component '{}' expects {} args, but {} provided", functionName,
                            componentType, function.arity(), args.size());
                return JS_UNDEFINED;
            }

            entt::meta_any instanceAny = metaType.from_void(component);
            entt::meta_any result = function.invoke(instanceAny);
            if (!result)
            {
                return JS_UNDEFINED;
            }

            return Reflection::QuickJSTypeConverter::ToJSValue(jsContext->ctx, result);
        });

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
#if ANDROID
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

#if WIN32
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
#endif
