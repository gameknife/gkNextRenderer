#include "Automation/EditorScriptExecutor.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "EditorContext.hpp"
#include "Engine/Runtime/Command/CommandHistory.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/DevTools/Command/PropertyCommand.hpp"
#include "Modules/DevTools/Command/RenameNodeCommand.hpp"
#include "Modules/DevTools/Command/TransformNodesCommand.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <glm/gtc/quaternion.hpp>
#include <cctype>
#include <filesystem>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>

namespace Editor
{
    namespace
    {
        std::string ToLowerCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::string JoinTokens(const std::vector<std::string>& tokens, size_t startIdx)
        {
            std::string joined;
            for (size_t i = startIdx; i < tokens.size(); ++i)
            {
                if (!joined.empty())
                {
                    joined += " ";
                }
                joined += tokens[i];
            }
            return joined;
        }

        bool ParseVec3FromText(const std::string& text, glm::vec3& outValue)
        {
            std::string normalized = text;
            for (char& ch : normalized)
            {
                if (ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == ',')
                {
                    ch = ' ';
                }
            }

            std::istringstream iss(normalized);
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            if (!(iss >> x >> y >> z))
            {
                return false;
            }

            outValue = glm::vec3(x, y, z);
            return true;
        }

        bool ParseVec3FromTokens(const std::vector<std::string>& tokens, size_t startIdx, glm::vec3& outValue)
        {
            if (tokens.size() <= startIdx)
            {
                return false;
            }

            return ParseVec3FromText(JoinTokens(tokens, startIdx), outValue);
        }
    } // namespace

    FEditorScriptExecutor::FEditorScriptExecutor(NextEngine& engine)
        : engine_(engine)
    {
    }

    std::vector<ScriptLogEntry> FEditorScriptExecutor::TakeLog()
    {
        std::vector<ScriptLogEntry> result;
        std::swap(result, log_);
        return result;
    }

    std::vector<FDeferredEditorAction> FEditorScriptExecutor::TakeDeferredActions()
    {
        std::vector<FDeferredEditorAction> result;
        std::swap(result, deferredActions_);
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
        std::string token;

        bool inQuote = false;
        char quoteChar = '\0';

        auto flushToken = [&]() {
            if (!token.empty())
            {
                tokens.push_back(token);
                token.clear();
            }
        };

        for (size_t i = 0; i < line.size(); ++i)
        {
            const char ch = line[i];

            if (inQuote)
            {
                if (ch == '\\' && i + 1 < line.size() && (line[i + 1] == quoteChar || line[i + 1] == '\\'))
                {
                    token += line[i + 1];
                    ++i;
                    continue;
                }

                if (ch == quoteChar)
                {
                    inQuote = false;
                    continue;
                }

                token += ch;
                continue;
            }

            if (ch == '\'' || ch == '"')
            {
                inQuote = true;
                quoteChar = ch;
                continue;
            }

            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                flushToken();
                continue;
            }

            token += ch;
        }

        flushToken();
        return tokens;
    }

    uint32_t FEditorScriptExecutor::ResolveNode(const std::string& nameOrId)
    {
        auto* scene = &engine_.GetScene();
        if (!scene)
        {
            return static_cast<uint32_t>(-1);
        }

        if (nameOrId == "$selected")
        {
            uint32_t selectedId = scene->GetSelectedId();
            if (selectedId != static_cast<uint32_t>(-1) && scene->GetNodeByInstanceId(selectedId))
            {
                return selectedId;
            }
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

    bool FEditorScriptExecutor::DispatchAction(EditorContext& editorContext, EEditorAction action, std::string_view args,
                                               std::string_view commandText)
    {
        (void)action;
        const bool dispatched = editorContext.actions.Dispatch(editorContext, action, args);
        if (dispatched)
        {
            Log(fmt::format("Executed action: {}", commandText));
            return true;
        }

        LogError(fmt::format("action dispatch failed: {}", commandText));
        return false;
    }

    bool FEditorScriptExecutor::IsHighRiskAction(EEditorAction action) const
    {
        return action == EEditorAction::IO_LoadScene;
    }

    std::optional<std::string> FEditorScriptExecutor::ResolveScenePath(const std::string& sceneRef,
                                                                        std::string& errorMessage,
                                                                        std::vector<std::string>* outCandidates) const
    {
        namespace fs = std::filesystem;

        if (sceneRef.empty())
        {
            errorMessage = "sceneRef is empty";
            return std::nullopt;
        }

        auto IsSceneAsset = [](const fs::path& path) { return Runtime::Scene::SceneList::IsSupportedScenePath(path); };

        fs::path inputPath(sceneRef);
        if (inputPath.is_absolute())
        {
            if (fs::exists(inputPath) && fs::is_regular_file(inputPath) && IsSceneAsset(inputPath))
            {
                return fs::absolute(inputPath).string();
            }

            errorMessage = fmt::format("absolute path is not a valid scene asset: {}", sceneRef);
            return std::nullopt;
        }

        const fs::path assetsRoot(Utilities::FileHelper::GetPlatformFilePath("assets"));
        fs::path relPath = inputPath;
        if (relPath.string().rfind("assets/", 0) == 0 || relPath.string() == "assets")
        {
            relPath = relPath.lexically_relative("assets");
        }

        auto TryPath = [&](const fs::path& pathCandidate) -> std::optional<std::string>
        {
            if (fs::exists(pathCandidate) && fs::is_regular_file(pathCandidate) && IsSceneAsset(pathCandidate))
            {
                return fs::absolute(pathCandidate).string();
            }
            return std::nullopt;
        };

        if (auto direct = TryPath(assetsRoot / relPath))
        {
            return direct;
        }

        if (!relPath.has_extension())
        {
            for (std::string_view extension : Runtime::Scene::SceneList::SupportedSceneExtensions())
            {
                fs::path candidate = assetsRoot / relPath;
                candidate.replace_extension(std::string(extension));
                if (auto withExt = TryPath(candidate))
                {
                    return withExt;
                }
            }
        }

        std::vector<std::string> matches;
        const std::string targetName = ToLowerCopy(inputPath.filename().string());
        const std::string targetStem = ToLowerCopy(inputPath.stem().string());
        const std::string targetRel = ToLowerCopy(inputPath.generic_string());

        std::error_code iterErr;
        const auto iterOpts = fs::directory_options::skip_permission_denied;
        for (fs::recursive_directory_iterator it(assetsRoot, iterOpts, iterErr), end; it != end; it.increment(iterErr))
        {
            if (iterErr)
            {
                continue;
            }

            const auto& entry = *it;
            if (!entry.is_regular_file())
            {
                continue;
            }

            const fs::path& file = entry.path();
            if (!IsSceneAsset(file))
            {
                continue;
            }

            const std::string rel = ToLowerCopy(fs::relative(file, assetsRoot).generic_string());
            const std::string name = ToLowerCopy(file.filename().string());
            const std::string stem = ToLowerCopy(file.stem().string());

            const bool matchesName = (!targetName.empty() && name == targetName);
            const bool matchesStem = (!targetStem.empty() && stem == targetStem);
            const bool matchesRel = (!targetRel.empty() && rel == targetRel);

            if (matchesName || matchesStem || matchesRel)
            {
                matches.push_back(fs::absolute(file).string());
            }
        }

        if (outCandidates)
        {
            *outCandidates = matches;
        }

        if (matches.empty())
        {
            errorMessage = fmt::format("no supported scene matched '{}'", sceneRef);
            return std::nullopt;
        }

        if (matches.size() > 1)
        {
            errorMessage = fmt::format("scene reference '{}' is ambiguous ({} matches)", sceneRef, matches.size());
            return std::nullopt;
        }

        return matches.front();
    }

    // ========== EditorScript command execution ==========

    void FEditorScriptExecutor::ExecuteScriptText(const std::string& scriptText, EditorContext* editorContext,
                                                  bool deferHighRiskActions)
    {
        activeEditorContext_ = editorContext;
        deferHighRiskActions_ = deferHighRiskActions;
        deferredActions_.clear();

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
            else if (cmd == "action")
                ExecAction(tokens);
            else
                LogError(fmt::format("Line {}: Unknown command '{}'", lineNum, cmd));
        }

        history.EndGroup();

        activeEditorContext_ = nullptr;
        deferHighRiskActions_ = false;
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
        engine_.GetScene().SetSelectedId(id);
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
        auto cmd = std::make_unique<Runtime::Command::RenameNodeCommand>(engine_.GetScene(), id, tokens[2]);
        engine_.GetCommandHistory().Execute(std::move(cmd));
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
        auto cmd = std::make_unique<Runtime::Command::DeleteNodesCommand>(engine_.GetScene(), std::vector<uint32_t>{id});
        engine_.GetCommandHistory().Execute(std::move(cmd));
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
        auto cmd = std::make_unique<Runtime::Command::DuplicateNodesCommand>(engine_.GetScene(), std::vector<uint32_t>{id});
        engine_.GetCommandHistory().Execute(std::move(cmd));
        Log(fmt::format("Duplicated node '{}'", tokens[1]));
    }

    void FEditorScriptExecutor::ExecMove(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 3)
        {
            LogError("move: usage: move <node> <x> <y> <z> or move <node> (x y z)");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("move: node '{}' not found", tokens[1]));
            return;
        }
        auto* node = engine_.GetScene().GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        Runtime::Command::TransformSnapshot before;
        before.translation = node->Translation();
        before.rotation = node->Rotation();
        before.scale = node->Scale();

        Runtime::Command::TransformSnapshot after = before;
        glm::vec3 targetPosition;
        if (!ParseVec3FromTokens(tokens, 2, targetPosition))
        {
            LogError("move: invalid vector, expected <x> <y> <z> or (x y z)");
            return;
        }
        after.translation = targetPosition;

        auto cmd = std::make_unique<Runtime::Command::TransformNodesCommand>(
            engine_.GetScene(), std::vector<uint32_t>{id}, std::vector<Runtime::Command::TransformSnapshot>{before},
            std::vector<Runtime::Command::TransformSnapshot>{after});
        engine_.GetCommandHistory().Execute(std::move(cmd));
        Log(fmt::format("Moved '{}' to ({:.3f}, {:.3f}, {:.3f})", tokens[1], targetPosition.x, targetPosition.y,
                        targetPosition.z));
    }

    void FEditorScriptExecutor::ExecRotate(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 3)
        {
            LogError("rotate: usage: rotate <node> <rx> <ry> <rz> or rotate <node> (rx ry rz)");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("rotate: node '{}' not found", tokens[1]));
            return;
        }
        auto* node = engine_.GetScene().GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        Runtime::Command::TransformSnapshot before;
        before.translation = node->Translation();
        before.rotation = node->Rotation();
        before.scale = node->Scale();

        Runtime::Command::TransformSnapshot after = before;
        glm::vec3 eulerDeg;
        if (!ParseVec3FromTokens(tokens, 2, eulerDeg))
        {
            LogError("rotate: invalid vector, expected <rx> <ry> <rz> or (rx ry rz)");
            return;
        }
        float rx = glm::radians(eulerDeg.x);
        float ry = glm::radians(eulerDeg.y);
        float rz = glm::radians(eulerDeg.z);
        after.rotation = glm::quat(glm::vec3(rx, ry, rz));

        auto cmd = std::make_unique<Runtime::Command::TransformNodesCommand>(
            engine_.GetScene(), std::vector<uint32_t>{id}, std::vector<Runtime::Command::TransformSnapshot>{before},
            std::vector<Runtime::Command::TransformSnapshot>{after});
        engine_.GetCommandHistory().Execute(std::move(cmd));
        Log(fmt::format("Rotated '{}' to ({:.3f}, {:.3f}, {:.3f}) degrees", tokens[1], eulerDeg.x, eulerDeg.y,
                        eulerDeg.z));
    }

    void FEditorScriptExecutor::ExecScale(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 3)
        {
            LogError("scale: usage: scale <node> <sx> <sy> <sz> or scale <node> (sx sy sz)");
            return;
        }
        uint32_t id = ResolveNode(tokens[1]);
        if (id == static_cast<uint32_t>(-1))
        {
            LogError(fmt::format("scale: node '{}' not found", tokens[1]));
            return;
        }
        auto* node = engine_.GetScene().GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        Runtime::Command::TransformSnapshot before;
        before.translation = node->Translation();
        before.rotation = node->Rotation();
        before.scale = node->Scale();

        Runtime::Command::TransformSnapshot after = before;
        glm::vec3 targetScale;
        if (!ParseVec3FromTokens(tokens, 2, targetScale))
        {
            LogError("scale: invalid vector, expected <sx> <sy> <sz> or (sx sy sz)");
            return;
        }
        after.scale = targetScale;

        auto cmd = std::make_unique<Runtime::Command::TransformNodesCommand>(
            engine_.GetScene(), std::vector<uint32_t>{id}, std::vector<Runtime::Command::TransformSnapshot>{before},
            std::vector<Runtime::Command::TransformSnapshot>{after});
        engine_.GetCommandHistory().Execute(std::move(cmd));
        Log(fmt::format("Scaled '{}' to ({:.3f}, {:.3f}, {:.3f})", tokens[1], targetScale.x, targetScale.y,
                        targetScale.z));
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
        auto* node = engine_.GetScene().GetNodeByInstanceId(id);
        if (!node)
        {
            return;
        }

        auto* component = node->GetComponent(tokens[2]);
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

        auto cmd = std::make_unique<Runtime::Command::PropertyCommand>(component, propName, std::move(newValue), std::move(oldValue));
        engine_.GetCommandHistory().Execute(std::move(cmd));
        Log(fmt::format("Set {}.{} = {} on '{}'", tokens[2], propName, valueStr, tokens[1]));
    }

    void FEditorScriptExecutor::ExecRenamePattern(const std::vector<std::string>& tokens)
    {
        if (tokens.size() < 3)
        {
            LogError("rename_pattern: usage: rename_pattern <search> <replace>");
            return;
        }
        auto* scene = &engine_.GetScene();
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
                auto cmd = std::make_unique<Runtime::Command::RenameNodeCommand>(*scene, node->GetInstanceId(), newName);
                engine_.GetCommandHistory().Execute(std::move(cmd));
                count++;
            }
        }
        Log(fmt::format("Renamed {} nodes: '{}' -> '{}'", count, search, replace));
    }

    void FEditorScriptExecutor::ExecListNodes(const std::vector<std::string>& tokens)
    {
        auto* scene = &engine_.GetScene();
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

        const std::string cvarCmd = JoinTokens(tokens, 1);

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

    bool FEditorScriptExecutor::ExecuteDeferredAction(const FDeferredEditorAction& deferredAction,
                                                      EditorContext& editorContext)
    {
        return DispatchAction(editorContext, deferredAction.action, deferredAction.args, deferredAction.commandText);
    }

    void FEditorScriptExecutor::ExecAction(const std::vector<std::string>& tokens)
    {
        if (!activeEditorContext_)
        {
            LogError("action: editor context unavailable");
            return;
        }

        if (tokens.size() < 2)
        {
            LogError("action: usage: action <load_scene|add_scene|focus_selected> [args]");
            return;
        }

        const std::string actionName = ToLowerCopy(tokens[1]);
        EEditorAction action = EEditorAction::Camera_FocusSelected;
        std::string args;

        if (actionName == "focus_selected")
        {
            action = EEditorAction::Camera_FocusSelected;
        }
        else if (actionName == "load_scene" || actionName == "add_scene")
        {
            const std::string sceneRef = JoinTokens(tokens, 2);
            if (sceneRef.empty())
            {
                LogError(fmt::format("action {}: missing scene reference", actionName));
                return;
            }

            std::string resolveError;
            std::vector<std::string> candidates;
            std::optional<std::string> resolvedPath = ResolveScenePath(sceneRef, resolveError, &candidates);
            if (!resolvedPath)
            {
                LogError(fmt::format("action {}: {}", actionName, resolveError));
                for (const auto& candidate : candidates)
                {
                    LogError(fmt::format("  candidate: {}", candidate));
                }
                return;
            }

            args = *resolvedPath;
            action = (actionName == "load_scene") ? EEditorAction::IO_LoadScene : EEditorAction::IO_LoadSceneAdd;
        }
        else
        {
            LogError(fmt::format("action: unknown action '{}'", tokens[1]));
            return;
        }

        std::string commandText = fmt::format("action {}", actionName);
        if (!args.empty())
        {
            commandText += " ";
            commandText += args;
        }

        if (deferHighRiskActions_ && IsHighRiskAction(action))
        {
            FDeferredEditorAction deferredAction;
            deferredAction.action = action;
            deferredAction.args = args;
            deferredAction.commandText = commandText;
            deferredAction.description = fmt::format("需要确认后执行: {}", commandText);
            deferredActions_.push_back(std::move(deferredAction));

            Log(fmt::format("Deferred action pending confirmation: {}", commandText));
            return;
        }

        DispatchAction(*activeEditorContext_, action, args, commandText);
    }

} // namespace Editor
