#include "Editor/AI/EditorScriptExecutor.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Command/CommandHistory.hpp"
#include "Runtime/Command/DeleteNodeCommand.hpp"
#include "Runtime/Command/DuplicateNodeCommand.hpp"
#include "Runtime/Command/PropertyCommand.hpp"
#include "Runtime/Command/RenameNodeCommand.hpp"
#include "Runtime/Command/TransformNodeCommand.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Runtime/Subsystems/QuickJSEngine.hpp"

#include <glm/gtc/quaternion.hpp>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>

#if WITH_QUICKJS
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#endif

namespace Editor
{
    FEditorScriptExecutor* FEditorScriptExecutor::activeInstance_ = nullptr;

    FEditorScriptExecutor::FEditorScriptExecutor(NextEngine& engine)
        : engine_(engine)
    {
        activeInstance_ = this;
    }

    std::vector<ScriptLogEntry> FEditorScriptExecutor::TakeLog()
    {
        std::vector<ScriptLogEntry> result;
        std::swap(result, log_);
        return result;
    }

    void FEditorScriptExecutor::Log(const std::string& message)
    {
        log_.push_back({message, false});
        SPDLOG_INFO("[EditorScript] {}", message);
    }

    void FEditorScriptExecutor::LogError(const std::string& message)
    {
        log_.push_back({message, true});
        SPDLOG_ERROR("[EditorScript] {}", message);
    }

    std::vector<std::string> FEditorScriptExecutor::Tokenize(const std::string& line)
    {
        std::vector<std::string> tokens;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token)
        {
            tokens.push_back(token);
        }
        return tokens;
    }

    uint32_t FEditorScriptExecutor::ResolveNode(const std::string& nameOrId)
    {
        auto* scene = engine_.GetScenePtr();
        if (!scene)
        {
            return static_cast<uint32_t>(-1);
        }

        // Try as numeric instanceId first
        try
        {
            size_t pos = 0;
            unsigned long id = std::stoul(nameOrId, &pos);
            if (pos == nameOrId.size())
            {
                auto node = scene->GetNodeSharedByInstanceId(static_cast<uint32_t>(id));
                if (node)
                {
                    return static_cast<uint32_t>(id);
                }
            }
        }
        catch (...)
        {
        }

        // Try as name
        auto* node = scene->GetNode(nameOrId);
        if (node)
        {
            return node->GetInstanceId();
        }

        return static_cast<uint32_t>(-1);
    }

    // ========== EditorScript command execution ==========

    void FEditorScriptExecutor::ExecuteScriptText(const std::string& scriptText)
    {
        auto& history = engine_.GetCommandHistory();
        history.BeginGroup("AI EditorScript");

        std::istringstream stream(scriptText);
        std::string line;
        int lineNum = 0;

        while (std::getline(stream, line))
        {
            lineNum++;
            // Trim
            auto start = line.find_first_not_of(" \t");
            if (start == std::string::npos)
            {
                continue;
            }
            line = line.substr(start);

            // Skip comments
            if (line[0] == '#' || line.substr(0, 2) == "//")
            {
                continue;
            }

            auto tokens = Tokenize(line);
            if (tokens.empty())
            {
                continue;
            }

            const std::string& cmd = tokens[0];

            if (cmd == "select")
                ExecSelect(tokens);
            else if (cmd == "rename")
                ExecRename(tokens);
            else if (cmd == "delete")
                ExecDelete(tokens);
            else if (cmd == "duplicate")
                ExecDuplicate(tokens);
            else if (cmd == "move")
                ExecMove(tokens);
            else if (cmd == "rotate")
                ExecRotate(tokens);
            else if (cmd == "scale")
                ExecScale(tokens);
            else if (cmd == "set_property")
                ExecSetProperty(tokens);
            else if (cmd == "rename_pattern")
                ExecRenamePattern(tokens);
            else if (cmd == "list_nodes")
                ExecListNodes(tokens);
            else if (cmd == "cvar")
                ExecCVar(tokens);
            else
                LogError(fmt::format("Line {}: Unknown command '{}'", lineNum, cmd));
        }

        history.EndGroup();
    }

    void FEditorScriptExecutor::ExecSelect(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 2)
        {
            LogError("select: missing node name/id");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("select: node '{}' not found", tokens[1]));
            return;
        }
        engine_.GetScenePtr()->SetSelectedId(id);
        Log(fmt::format("Selected node '{}'", tokens[1]));
    }

    void FEditorScriptExecutor::ExecRename(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 3)
        {
            LogError("rename: usage: rename <node> <newName>");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("rename: node '{}' not found", tokens[1]));
            return;
        }
        auto cmd = std::make_unique<RenameNodeCommand>(engine_.GetScene(), id, tokens[2]);
        engine_.ExecuteCommand(std::move(cmd));
        Log(fmt::format("Renamed '{}' to '{}'", tokens[1], tokens[2]));
    }

    void FEditorScriptExecutor::ExecDelete(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 2)
        {
            LogError("delete: missing node name/id");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("delete: node '{}' not found", tokens[1]));
            return;
        }
        auto cmd = std::make_unique<DeleteNodeCommand>(engine_.GetScene(), id);
        engine_.ExecuteCommand(std::move(cmd));
        Log(fmt::format("Deleted node '{}'", tokens[1]));
    }

    void FEditorScriptExecutor::ExecDuplicate(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 2)
        {
            LogError("duplicate: missing node name/id");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("duplicate: node '{}' not found", tokens[1]));
            return;
        }
        auto cmd = std::make_unique<DuplicateNodeCommand>(engine_.GetScene(), id);
        engine_.ExecuteCommand(std::move(cmd));
        Log(fmt::format("Duplicated node '{}'", tokens[1]));
    }

    void FEditorScriptExecutor::ExecMove(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 5)
        {
            LogError("move: usage: move <node> <x> <y> <z>");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("move: node '{}' not found", tokens[1]));
            return;
        }
        auto* node = engine_.GetScenePtr()->GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        TransformSnapshot before;
        before.translation = node->Translation();
        before.rotation = node->Rotation();
        before.scale = node->Scale();

        TransformSnapshot after = before;
        after.translation = glm::vec3(std::stof(tokens[2]), std::stof(tokens[3]), std::stof(tokens[4]));

        auto cmd = std::make_unique<TransformNodeCommand>(engine_.GetScene(), id, before, after);
        engine_.ExecuteCommand(std::move(cmd));
        Log(fmt::format("Moved '{}' to ({}, {}, {})", tokens[1], tokens[2], tokens[3], tokens[4]));
    }

    void FEditorScriptExecutor::ExecRotate(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 5)
        {
            LogError("rotate: usage: rotate <node> <rx> <ry> <rz>");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("rotate: node '{}' not found", tokens[1]));
            return;
        }
        auto* node = engine_.GetScenePtr()->GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        TransformSnapshot before;
        before.translation = node->Translation();
        before.rotation = node->Rotation();
        before.scale = node->Scale();

        TransformSnapshot after = before;
        float rx = glm::radians(std::stof(tokens[2]));
        float ry = glm::radians(std::stof(tokens[3]));
        float rz = glm::radians(std::stof(tokens[4]));
        after.rotation = glm::quat(glm::vec3(rx, ry, rz));

        auto cmd = std::make_unique<TransformNodeCommand>(engine_.GetScene(), id, before, after);
        engine_.ExecuteCommand(std::move(cmd));
        Log(fmt::format("Rotated '{}' to ({}, {}, {}) degrees", tokens[1], tokens[2], tokens[3], tokens[4]));
    }

    void FEditorScriptExecutor::ExecScale(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 5)
        {
            LogError("scale: usage: scale <node> <sx> <sy> <sz>");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("scale: node '{}' not found", tokens[1]));
            return;
        }
        auto* node = engine_.GetScenePtr()->GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        TransformSnapshot before;
        before.translation = node->Translation();
        before.rotation = node->Rotation();
        before.scale = node->Scale();

        TransformSnapshot after = before;
        after.scale = glm::vec3(std::stof(tokens[2]), std::stof(tokens[3]), std::stof(tokens[4]));

        auto cmd = std::make_unique<TransformNodeCommand>(engine_.GetScene(), id, before, after);
        engine_.ExecuteCommand(std::move(cmd));
        Log(fmt::format("Scaled '{}' to ({}, {}, {})", tokens[1], tokens[2], tokens[3], tokens[4]));
    }

    void FEditorScriptExecutor::ExecSetProperty(const std::vector<std::string>& tokens)
    {
        // set_property <node> <component> <property> <value>
        if (tokens.size() < 5)
        {
            LogError("set_property: usage: set_property <node> <component> <property> <value>");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("set_property: node '{}' not found", tokens[1]));
            return;
        }
        auto* node = engine_.GetScenePtr()->GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        auto* component = node->GetComponentByTypeName(tokens[2]);
        if (!component)
        {
            LogError(fmt::format("set_property: component '{}' not found on node '{}'", tokens[2], tokens[1]));
            return;
        }

        const std::string& propName = tokens[3];
        const std::string& valueStr = tokens[4];

        entt::meta_type metaType = component->GetMetaType();
        auto dataEntry = metaType.data(entt::hashed_string::value(propName.c_str()));
        if (!dataEntry)
        {
            LogError(fmt::format("set_property: property '{}' not found on '{}'", propName, tokens[2]));
            return;
        }

        entt::meta_any oldValue = Reflection::PropertyAccessor::GetPropertyValue(metaType, component, propName);

        // Parse value based on type
        entt::meta_type valueType = dataEntry.type();
        entt::meta_any newValue;

        if (valueType == entt::resolve<bool>())
        {
            newValue = entt::meta_any{valueStr == "true" || valueStr == "1"};
        }
        else if (valueType == entt::resolve<float>())
        {
            newValue = entt::meta_any{std::stof(valueStr)};
        }
        else if (valueType == entt::resolve<int32_t>())
        {
            newValue = entt::meta_any{std::stoi(valueStr)};
        }
        else if (valueType == entt::resolve<uint32_t>())
        {
            newValue = entt::meta_any{static_cast<uint32_t>(std::stoul(valueStr))};
        }
        else if (valueType == entt::resolve<std::string>())
        {
            newValue = entt::meta_any{valueStr};
        }
        else
        {
            LogError(fmt::format("set_property: unsupported type for property '{}'", propName));
            return;
        }

        auto cmd = std::make_unique<PropertyCommand>(component, propName, std::move(newValue), std::move(oldValue));
        engine_.ExecuteCommand(std::move(cmd));
        Log(fmt::format("Set {}.{} = {} on '{}'", tokens[2], propName, valueStr, tokens[1]));
    }

    void FEditorScriptExecutor::ExecRenamePattern(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 3)
        {
            LogError("rename_pattern: usage: rename_pattern <search> <replace>");
            return;
        }
        auto* scene = engine_.GetScenePtr();
        if (!scene)
        {
            return;
        }

        const std::string& search = tokens[1];
        const std::string& replace = tokens[2];
        int count = 0;

        for (const auto& node : scene->Nodes())
        {
            const std::string& name = node->GetName();
            if (name.find(search) != std::string::npos)
            {
                std::string newName = name;
                size_t pos = 0;
                while ((pos = newName.find(search, pos)) != std::string::npos)
                {
                    newName.replace(pos, search.length(), replace);
                    pos += replace.length();
                }
                auto cmd = std::make_unique<RenameNodeCommand>(*scene, node->GetInstanceId(), newName);
                engine_.ExecuteCommand(std::move(cmd));
                count++;
            }
        }
        Log(fmt::format("Renamed {} nodes: '{}' -> '{}'", count, search, replace));
    }

    void FEditorScriptExecutor::ExecListNodes(const std::vector<std::string>& tokens)
    {
        auto* scene = engine_.GetScenePtr();
        if (!scene)
        {
            return;
        }

        std::string pattern = tokens.size() > 1 ? tokens[1] : "";
        int count = 0;

        for (const auto& node : scene->Nodes())
        {
            const std::string& name = node->GetName();
            if (pattern.empty() || name.find(pattern) != std::string::npos)
            {
                auto pos = node->Translation();
                Log(fmt::format("  [{}] {} ({:.1f}, {:.1f}, {:.1f})", node->GetInstanceId(), name, pos.x, pos.y,
                                pos.z));
                count++;
            }
        }
        Log(fmt::format("Total: {} nodes", count));
    }

    void FEditorScriptExecutor::ExecCVar(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 2)
        {
            LogError("cvar: missing command");
            return;
        }

        // Rejoin all tokens after "cvar"
        std::string cvarCmd;
        for (size_t i = 1; i < tokens.size(); ++i)
        {
            if (i > 1)
            {
                cvarCmd += " ";
            }
            cvarCmd += tokens[i];
        }

        auto result = engine_.GetCVarSystem().ExecuteCommand(cvarCmd);
        if (result.success)
        {
            Log(fmt::format("CVar: {}", result.message.empty() ? "OK" : result.message));
        }
        else
        {
            LogError(fmt::format("CVar error: {}", result.message));
        }
    }

    // ========== JavaScript eval ==========

    void FEditorScriptExecutor::EvalJavaScript(const std::string& code)
    {
        auto& history = engine_.GetCommandHistory();
        history.BeginGroup("AI JavaScript");

        auto* qjs = engine_.GetQuickJSEngine();
        if (!qjs)
        {
            LogError("QuickJS engine not available");
            history.EndGroup();
            return;
        }

        std::string error = qjs->Eval(code);
        if (!error.empty())
        {
            LogError(fmt::format("JS error: {}", error));
        }
        else
        {
            Log("JavaScript executed successfully");
        }

        history.EndGroup();
    }

    // ========== Editor.* JS bindings ==========

#if WITH_QUICKJS
    namespace
    {
        Assets::Scene* GetScene()
        {
            auto* engine = NextEngine::GetInstance();
            return engine ? engine->GetScenePtr() : nullptr;
        }

        uint32_t ResolveNodeJS(JSContext* ctx, JSValueConst val)
        {
            auto* scene = GetScene();
            if (!scene)
            {
                return static_cast<uint32_t>(-1);
            }

            // Try as number
            if (JS_IsNumber(val))
            {
                uint32_t id = 0;
                JS_ToUint32(ctx, &id, val);
                if (scene->GetNodeByInstanceId(id))
                {
                    return id;
                }
                return static_cast<uint32_t>(-1);
            }

            // Try as string (name)
            const char* str = JS_ToCString(ctx, val);
            if (!str)
            {
                return static_cast<uint32_t>(-1);
            }
            std::string name(str);
            JS_FreeCString(ctx, str);

            auto* node = scene->GetNode(name);
            return node ? node->GetInstanceId() : static_cast<uint32_t>(-1);
        }

        // Editor.renameNode(nameOrId, newName)
        JSValue JsEditorRenameNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 2)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            const char* newName = JS_ToCString(ctx, argv[1]);
            if (!newName)
            {
                return JS_UNDEFINED;
            }

            auto* engine = NextEngine::GetInstance();
            auto cmd = std::make_unique<RenameNodeCommand>(engine->GetScene(), id, newName);
            engine->ExecuteCommand(std::move(cmd));
            JS_FreeCString(ctx, newName);

            if (FEditorScriptExecutor::activeInstance_)
            {
                FEditorScriptExecutor::activeInstance_->Log(fmt::format("JS: Renamed node {}", id));
            }
            return JS_UNDEFINED;
        }

        // Editor.deleteNode(nameOrId)
        JSValue JsEditorDeleteNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 1)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            auto* engine = NextEngine::GetInstance();
            auto cmd = std::make_unique<DeleteNodeCommand>(engine->GetScene(), id);
            engine->ExecuteCommand(std::move(cmd));
            return JS_UNDEFINED;
        }

        // Editor.duplicateNode(nameOrId) -> id
        JSValue JsEditorDuplicateNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 1)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            auto* engine = NextEngine::GetInstance();
            auto cmd = std::make_unique<DuplicateNodeCommand>(engine->GetScene(), id);
            engine->ExecuteCommand(std::move(cmd));
            return JS_NewUint32(ctx, cmd->GetNewInstanceId());
        }

        // Editor.moveNode(nameOrId, x, y, z)
        JSValue JsEditorMoveNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 4)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            auto* scene = GetScene();
            auto* node = scene->GetNodeByInstanceId(id);
            if (!node)
            {
                return JS_UNDEFINED;
            }

            double x, y, z;
            JS_ToFloat64(ctx, &x, argv[1]);
            JS_ToFloat64(ctx, &y, argv[2]);
            JS_ToFloat64(ctx, &z, argv[3]);

            TransformSnapshot before;
            before.translation = node->Translation();
            before.rotation = node->Rotation();
            before.scale = node->Scale();

            TransformSnapshot after = before;
            after.translation = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));

            auto* engine = NextEngine::GetInstance();
            auto cmd = std::make_unique<TransformNodeCommand>(*scene, id, before, after);
            engine->ExecuteCommand(std::move(cmd));
            return JS_UNDEFINED;
        }

        // Editor.rotateNode(nameOrId, rx, ry, rz) - euler angles in degrees
        JSValue JsEditorRotateNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 4)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            auto* scene = GetScene();
            auto* node = scene->GetNodeByInstanceId(id);
            if (!node)
            {
                return JS_UNDEFINED;
            }

            double rx, ry, rz;
            JS_ToFloat64(ctx, &rx, argv[1]);
            JS_ToFloat64(ctx, &ry, argv[2]);
            JS_ToFloat64(ctx, &rz, argv[3]);

            TransformSnapshot before;
            before.translation = node->Translation();
            before.rotation = node->Rotation();
            before.scale = node->Scale();

            TransformSnapshot after = before;
            after.rotation = glm::quat(glm::vec3(glm::radians(static_cast<float>(rx)),
                                                   glm::radians(static_cast<float>(ry)),
                                                   glm::radians(static_cast<float>(rz))));

            auto* engine = NextEngine::GetInstance();
            auto cmd = std::make_unique<TransformNodeCommand>(*scene, id, before, after);
            engine->ExecuteCommand(std::move(cmd));
            return JS_UNDEFINED;
        }

        // Editor.scaleNode(nameOrId, sx, sy, sz)
        JSValue JsEditorScaleNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 4)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            auto* scene = GetScene();
            auto* node = scene->GetNodeByInstanceId(id);
            if (!node)
            {
                return JS_UNDEFINED;
            }

            double sx, sy, sz;
            JS_ToFloat64(ctx, &sx, argv[1]);
            JS_ToFloat64(ctx, &sy, argv[2]);
            JS_ToFloat64(ctx, &sz, argv[3]);

            TransformSnapshot before;
            before.translation = node->Translation();
            before.rotation = node->Rotation();
            before.scale = node->Scale();

            TransformSnapshot after = before;
            after.scale = glm::vec3(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz));

            auto* engine = NextEngine::GetInstance();
            auto cmd = std::make_unique<TransformNodeCommand>(*scene, id, before, after);
            engine->ExecuteCommand(std::move(cmd));
            return JS_UNDEFINED;
        }

        // Editor.setProperty(nameOrId, comp, prop, val)
        JSValue JsEditorSetProperty(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 4)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            auto* scene = GetScene();
            auto* node = scene->GetNodeByInstanceId(id);
            if (!node)
            {
                return JS_UNDEFINED;
            }

            const char* compName = JS_ToCString(ctx, argv[1]);
            const char* propName = JS_ToCString(ctx, argv[2]);
            if (!compName || !propName)
            {
                if (compName)
                {
                    JS_FreeCString(ctx, compName);
                }
                if (propName)
                {
                    JS_FreeCString(ctx, propName);
                }
                return JS_UNDEFINED;
            }

            auto* component = node->GetComponentByTypeName(compName);
            if (!component)
            {
                JS_FreeCString(ctx, compName);
                JS_FreeCString(ctx, propName);
                return JS_ThrowReferenceError(ctx, "Component not found");
            }

            entt::meta_type metaType = component->GetMetaType();
            auto dataEntry = metaType.data(entt::hashed_string::value(propName));
            if (!dataEntry)
            {
                JS_FreeCString(ctx, compName);
                JS_FreeCString(ctx, propName);
                return JS_ThrowReferenceError(ctx, "Property not found");
            }

            entt::meta_any oldValue = Reflection::PropertyAccessor::GetPropertyValue(metaType, component, propName);
            entt::meta_type valueType = dataEntry.type();

            entt::meta_any newValue;
            if (valueType == entt::resolve<bool>())
            {
                newValue = entt::meta_any{static_cast<bool>(JS_ToBool(ctx, argv[3]))};
            }
            else if (valueType == entt::resolve<float>())
            {
                double d = 0;
                JS_ToFloat64(ctx, &d, argv[3]);
                newValue = entt::meta_any{static_cast<float>(d)};
            }
            else if (valueType == entt::resolve<int32_t>())
            {
                int32_t i = 0;
                JS_ToInt32(ctx, &i, argv[3]);
                newValue = entt::meta_any{i};
            }
            else if (valueType == entt::resolve<uint32_t>())
            {
                uint32_t u = 0;
                JS_ToUint32(ctx, &u, argv[3]);
                newValue = entt::meta_any{u};
            }
            else if (valueType == entt::resolve<std::string>())
            {
                const char* s = JS_ToCString(ctx, argv[3]);
                newValue = entt::meta_any{std::string(s ? s : "")};
                if (s)
                {
                    JS_FreeCString(ctx, s);
                }
            }

            JS_FreeCString(ctx, compName);
            JS_FreeCString(ctx, propName);

            if (!newValue)
            {
                return JS_ThrowTypeError(ctx, "Unsupported property type");
            }

            auto* engine = NextEngine::GetInstance();
            auto cmd = std::make_unique<PropertyCommand>(component, propName, std::move(newValue), std::move(oldValue));
            engine->ExecuteCommand(std::move(cmd));
            return JS_UNDEFINED;
        }

        // Editor.findNodes(regexPattern) -> [{id, name, position:{x,y,z}}]
        JSValue JsEditorFindNodes(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            auto* scene = GetScene();
            if (!scene)
            {
                return JS_NewArray(ctx);
            }

            std::string pattern = ".*";
            if (argc >= 1)
            {
                if (JS_IsString(argv[0]))
                {
                    const char* str = JS_ToCString(ctx, argv[0]);
                    if (str)
                    {
                        pattern = str;
                        JS_FreeCString(ctx, str);
                    }
                }
                else if (JS_IsObject(argv[0]))
                {
                    // RegExp object - extract .source property for the raw pattern
                    JSValue source = JS_GetPropertyStr(ctx, argv[0], "source");
                    if (JS_IsString(source))
                    {
                        const char* str = JS_ToCString(ctx, source);
                        if (str)
                        {
                            pattern = str;
                            JS_FreeCString(ctx, str);
                        }
                    }
                    JS_FreeValue(ctx, source);
                }
            }

            std::regex re;
            try
            {
                re = std::regex(pattern);
            }
            catch (...)
            {
                return JS_ThrowSyntaxError(ctx, "Invalid regex pattern");
            }

            JSValue arr = JS_NewArray(ctx);
            uint32_t idx = 0;

            for (const auto& node : scene->Nodes())
            {
                if (std::regex_search(node->GetName(), re))
                {
                    JSValue obj = JS_NewObject(ctx);
                    JS_SetPropertyStr(ctx, obj, "id", JS_NewUint32(ctx, node->GetInstanceId()));
                    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, node->GetName().c_str()));

                    JSValue pos = JS_NewObject(ctx);
                    auto t = node->Translation();
                    JS_SetPropertyStr(ctx, pos, "x", JS_NewFloat64(ctx, t.x));
                    JS_SetPropertyStr(ctx, pos, "y", JS_NewFloat64(ctx, t.y));
                    JS_SetPropertyStr(ctx, pos, "z", JS_NewFloat64(ctx, t.z));
                    JS_SetPropertyStr(ctx, obj, "position", pos);

                    JS_SetPropertyUint32(ctx, arr, idx++, obj);
                }
            }

            return arr;
        }

        // Editor.getNodeCount() -> number
        JSValue JsEditorGetNodeCount(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            (void)argc;
            (void)argv;
            auto* scene = GetScene();
            return JS_NewUint32(ctx, scene ? static_cast<uint32_t>(scene->Nodes().size()) : 0);
        }

        // Editor.getNodeInfo(nameOrId) -> {id, name, position, rotation, scale, components:[]}
        JSValue JsEditorGetNodeInfo(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 1)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_UNDEFINED;
            }

            auto* scene = GetScene();
            auto* node = scene->GetNodeByInstanceId(id);
            if (!node)
            {
                return JS_UNDEFINED;
            }

            JSValue obj = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, obj, "id", JS_NewUint32(ctx, node->GetInstanceId()));
            JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, node->GetName().c_str()));

            auto t = node->Translation();
            JSValue pos = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, pos, "x", JS_NewFloat64(ctx, t.x));
            JS_SetPropertyStr(ctx, pos, "y", JS_NewFloat64(ctx, t.y));
            JS_SetPropertyStr(ctx, pos, "z", JS_NewFloat64(ctx, t.z));
            JS_SetPropertyStr(ctx, obj, "position", pos);

            auto r = glm::eulerAngles(node->Rotation());
            JSValue rot = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, rot, "x", JS_NewFloat64(ctx, glm::degrees(r.x)));
            JS_SetPropertyStr(ctx, rot, "y", JS_NewFloat64(ctx, glm::degrees(r.y)));
            JS_SetPropertyStr(ctx, rot, "z", JS_NewFloat64(ctx, glm::degrees(r.z)));
            JS_SetPropertyStr(ctx, obj, "rotation", rot);

            auto s = node->Scale();
            JSValue scl = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, scl, "x", JS_NewFloat64(ctx, s.x));
            JS_SetPropertyStr(ctx, scl, "y", JS_NewFloat64(ctx, s.y));
            JS_SetPropertyStr(ctx, scl, "z", JS_NewFloat64(ctx, s.z));
            JS_SetPropertyStr(ctx, obj, "scale", scl);

            JSValue comps = JS_NewArray(ctx);
            uint32_t ci = 0;
            for (const auto& comp : node->GetComponents())
            {
                JS_SetPropertyUint32(ctx, comps, ci++, JS_NewString(ctx, std::string(comp->GetTypeName()).c_str()));
            }
            JS_SetPropertyStr(ctx, obj, "components", comps);

            return obj;
        }

        // Editor.selectNode(nameOrId)
        JSValue JsEditorSelectNode(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 1)
            {
                return JS_UNDEFINED;
            }

            uint32_t id = ResolveNodeJS(ctx, argv[0]);
            if (id == static_cast<uint32_t>(-1))
            {
                return JS_ThrowReferenceError(ctx, "Node not found");
            }

            auto* scene = GetScene();
            if (scene)
            {
                scene->SetSelectedId(id);
            }
            return JS_UNDEFINED;
        }

        // Editor.log(message)
        JSValue JsEditorLog(JSContext* ctx, JSValueConst thisVal, int argc, JSValueConst* argv)
        {
            (void)thisVal;
            if (argc < 1)
            {
                return JS_UNDEFINED;
            }

            const char* msg = JS_ToCString(ctx, argv[0]);
            if (msg)
            {
                if (FEditorScriptExecutor::activeInstance_)
                {
                    FEditorScriptExecutor::activeInstance_->Log(msg);
                }
                JS_FreeCString(ctx, msg);
            }
            return JS_UNDEFINED;
        }
    } // namespace

    void FEditorScriptExecutor::RegisterEditorBindings(void* jsContext)
    {
        auto* ctx = static_cast<JSContext*>(jsContext);

        JSValue global = JS_GetGlobalObject(ctx);
        JSValue editor = JS_NewObject(ctx);

        // Modification functions (create ICommand)
        JS_SetPropertyStr(ctx, editor, "renameNode", JS_NewCFunction(ctx, JsEditorRenameNode, "renameNode", 2));
        JS_SetPropertyStr(ctx, editor, "deleteNode", JS_NewCFunction(ctx, JsEditorDeleteNode, "deleteNode", 1));
        JS_SetPropertyStr(ctx, editor, "duplicateNode",
                          JS_NewCFunction(ctx, JsEditorDuplicateNode, "duplicateNode", 1));
        JS_SetPropertyStr(ctx, editor, "moveNode", JS_NewCFunction(ctx, JsEditorMoveNode, "moveNode", 4));
        JS_SetPropertyStr(ctx, editor, "rotateNode", JS_NewCFunction(ctx, JsEditorRotateNode, "rotateNode", 4));
        JS_SetPropertyStr(ctx, editor, "scaleNode", JS_NewCFunction(ctx, JsEditorScaleNode, "scaleNode", 4));
        JS_SetPropertyStr(ctx, editor, "setProperty", JS_NewCFunction(ctx, JsEditorSetProperty, "setProperty", 4));

        // Query functions (read-only)
        JS_SetPropertyStr(ctx, editor, "findNodes", JS_NewCFunction(ctx, JsEditorFindNodes, "findNodes", 1));
        JS_SetPropertyStr(ctx, editor, "getNodeCount",
                          JS_NewCFunction(ctx, JsEditorGetNodeCount, "getNodeCount", 0));
        JS_SetPropertyStr(ctx, editor, "getNodeInfo", JS_NewCFunction(ctx, JsEditorGetNodeInfo, "getNodeInfo", 1));
        JS_SetPropertyStr(ctx, editor, "selectNode", JS_NewCFunction(ctx, JsEditorSelectNode, "selectNode", 1));

        // Output
        JS_SetPropertyStr(ctx, editor, "log", JS_NewCFunction(ctx, JsEditorLog, "log", 1));

        JS_SetPropertyStr(ctx, global, "Editor", editor);
        JS_FreeValue(ctx, global);
    }
#else
    void FEditorScriptExecutor::RegisterEditorBindings(void* jsContext)
    {
        (void)jsContext;
    }
#endif

} // namespace Editor
