// QuickJS Scene/SceneBuild bindings: node & component reflection objects,
// scene mutation methods and the Scene prototype.
// Split from QuickJSBindings.cpp; same namespace, separate TU.
#include "Modules/NextQuickJS/QuickJSEngine.hpp"
#include "Modules/NextQuickJS/QuickJSBindings.Internal.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"
#include "Modules/NextQuickJS/Reflection/QuickJSReflectionBridge.hpp"
#include "Modules/NextQuickJS/Reflection/QuickJSTypeConverter.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Engine/Runtime/Utilities/JsonHelpers.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/stopwatch.h>
#include <fstream>
#include <entt/core/hashed_string.hpp>
#include <cstdlib>
#include <limits>

#include <ThirdParty/quickjs-ng/quickjspp.hpp>

namespace NextQuickJSBindings
{
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
        return Assets::SceneBuilder::CreateRenderNode(name, translation, scale, instanceId, modelId, materialId, visible);
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
        return JS_NewUint32(ctx, Assets::SceneBuilder::AddLambertianMaterialToScene(engine->GetScene(), color));
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
        return JS_NewUint32(ctx, Assets::SceneBuilder::AddDiffuseLightMaterialToScene(engine->GetScene(), color, parsedIntensity));
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
        return JS_NewUint32(ctx, Assets::SceneBuilder::AddLambertianMaterial(*GQuickJSSceneBuild.materials, color));
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
        return JS_NewUint32(ctx, Assets::SceneBuilder::AddDiffuseLightMaterial(*GQuickJSSceneBuild.materials, color, intensity));
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
}
