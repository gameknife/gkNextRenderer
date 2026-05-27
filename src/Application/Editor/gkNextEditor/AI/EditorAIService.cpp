#include "AI/EditorAIService.hpp"
#include "AI/EditorTools.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "EditorContext.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.h"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Subsystems/AI/AgentLoop.hpp"
#include "Engine/Runtime/Subsystems/AI/Tools/RepoTools.hpp"
#include "Engine/Runtime/Subsystems/QuickJSEngine.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <spdlog/spdlog.h>
#include <thread>

namespace Editor
{
    FEditorAIService::FEditorAIService(NextEngine& engine)
        : engine_(engine)
        , executor_(engine)
    {
        // Register Editor.* JS bindings into QuickJS
        auto* qjs = engine_.GetQuickJSEngine();
        if (qjs)
        {
            qjs->SetEditorBindingsCallback([this](void* ctx) { executor_.RegisterEditorBindings(ctx); });
        }
        LoadAgentConfig();
    }

    FEditorAIService::~FEditorAIService() = default;

    void FEditorAIService::EnsureToolRegistry()
    {
        if (toolRegistryInitialized_) return;

        NextAI::FRepoToolsOptions repoOpts;
        repoOpts.repoRoot = Utilities::FileHelper::GetPlatformFilePath(".");
        // Read-only knowledge tools; run_cmd whitelist still active.
        NextAI::RegisterRepoTools(toolRegistry_, repoOpts);

        FEditorToolsOptions editorOpts;
        editorOpts.engine = &engine_;
        editorOpts.executor = &executor_;
        editorOpts.contextProvider = [this]() -> EditorContext* { return currentContext_; };
        editorOpts.deferHighRiskActions = true;
        RegisterEditorTools(toolRegistry_, editorOpts);

        toolRegistryInitialized_ = true;
        SPDLOG_INFO("[EditorAI] tool registry initialized ({} tools)", toolRegistry_.Size());
    }

    void FEditorAIService::PumpMainThread()
    {
        // Drain queued tool tasks (agent worker may be waiting on these).
        dispatcher_.DrainOnce();
        // Collect any deferred actions that the executor accumulated this frame.
        HarvestDeferredActions();
    }

    void FEditorAIService::HarvestDeferredActions()
    {
        auto deferred = executor_.TakeDeferredActions();
        for (auto& d : deferred)
        {
            FPendingEditorAction pa;
            pa.id = nextPendingActionId_++;
            pa.request = std::move(d);
            pendingActions_.push_back(std::move(pa));
        }
    }

    std::string FEditorAIService::BuildAgentSystemPrompt(const EditorContext& ctx)
    {
        std::string prompt =
            "You are the gkNextEngine editor AI agent. Use the provided tools to inspect "
            "and modify the scene. Prefer calling tools over guessing.\n\n"
            "## Rules\n"
            "1. Reply in the user's language (Chinese if the user wrote Chinese).\n"
            "2. When unsure of node names, call scene_list_nodes or scene_summary first.\n"
            "3. When unsure of a component's property names, call scene_get_node on a "
            "representative node to see live values.\n"
            "4. For source-code or .spec questions, use list_dir / find_files / "
            "search_text / find_symbol / read_file to gather evidence before answering.\n"
            "5. scene_action with name='load_scene' is HIGH-RISK: it will be queued for "
            "user confirmation. Tell the user what you queued, then stop.\n"
            "6. Use scene_select / scene_move / scene_set_property / etc. for mutations. "
            "Each mutation is undoable.\n"
            "7. When the task is complete, respond in plain text describing what was "
            "done and (optionally) suggest one next action.\n";

        // A short live snapshot to anchor the model without dumping the whole scene.
        prompt += "\n## Current Selection\n";
        prompt += BuildSelectionContext(ctx);
        return prompt;
    }

    void FEditorAIService::LoadAgentConfig()
    {
        // Best-effort: read useAgentLoop / maxAgentSteps from the same ai_config.json
        // that FAIService consumes. Missing keys leave defaults.
        try
        {
            std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/configs/ai_config.json");
            std::ifstream f(path);
            if (!f.is_open()) return;
            nlohmann::json j;
            f >> j;
            if (j.contains("useAgentLoop") && j["useAgentLoop"].is_boolean())
            {
                useAgentLoop_ = j["useAgentLoop"].get<bool>();
            }
            if (j.contains("maxAgentSteps") && j["maxAgentSteps"].is_number_integer())
            {
                SetMaxAgentSteps(j["maxAgentSteps"].get<int>());
            }
        }
        catch (const std::exception& e)
        {
            SPDLOG_WARN("EditorAIService: failed to read agent config: {}", e.what());
        }
    }

    bool FEditorAIService::IsAIConfigured() const
    {
        auto* ai = engine_.GetAIService();
        return ai && ai->IsConfigured();
    }

    std::vector<ScriptLogEntry> FEditorAIService::TakeLog()
    {
        return executor_.TakeLog();
    }

    namespace
    {
        std::string BuildComponentList(const Assets::Node& node)
        {
            std::string comps;
            for (const auto& comp : node.GetComponents())
            {
                if (!comps.empty())
                {
                    comps += ", ";
                }
                comps += std::string(comp->GetTypeName());
            }
            return comps.empty() ? "-" : comps;
        }

        std::string ToLowerCopy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }
    } // namespace

    // ========== System Prompt ==========

    std::string FEditorAIService::BuildSystemPrompt(const EditorContext& ctx)
    {
        std::string prompt = R"(You are the gkNextEngine editor AI assistant. Help users manipulate the scene by generating operation scripts and explaining your actions.

## Output Format
Reply in this structure:
1. Briefly explain what you will do (1-2 sentences)
2. Output the code block (```editorscript``` or ```javascript```)
3. Summarize the result and suggest what the user could do next

## Output Mode Selection
- Simple operations (rename, delete, move a few nodes): use ```editorscript``` code block
- Complex operations (loops, conditions, regex, batch processing): use ```javascript``` code block

)";
        prompt += BuildAvailableCommands();
        prompt += BuildSceneContext(ctx);
        prompt += BuildSelectionContext(ctx);
        prompt += BuildSceneAssetCatalog();
        prompt += BuildPropertyTypeInfo(ctx);

        prompt += R"(
## Rules
1. Always include natural language explanation before and after code blocks
2. Use the user's language (if user writes Chinese, reply in Chinese)
3. Node names are case-sensitive
4. When names are ambiguous, use instanceId (number)
5. Transform values are world-space absolute coordinates, rotation is euler angles (degrees)
6. If user asks about selected objects/current object, prioritize the Current Selection section
7. Use `action load_scene` / `action add_scene` for scene loading requests
8. `action load_scene` is high-risk and will require user confirmation before execution
9. After execution summary, suggest 1-2 possible next actions the user might want
)";

        return prompt;
    }

    std::string FEditorAIService::BuildAvailableCommands()
    {
        return R"(## EditorScript Commands
```
select <node>                           # Select a node
rename <node> <newName>                 # Rename a node
delete <node>                           # Delete a node
duplicate <node>                        # Duplicate a node
move <node> <x> <y> <z>                # Set node position (absolute)
rotate <node> <rx> <ry> <rz>           # Set node rotation (euler degrees)
scale <node> <sx> <sy> <sz>            # Set node scale
set_property <node> <comp> <prop> <val> # Set component property
rename_pattern <search> <replace>       # Rename all matching nodes
list_nodes [pattern]                    # List nodes (query only)
cvar <command>                          # Execute console variable command
action load_scene <sceneRef>            # Load scene (high-risk, requires confirmation)
action add_scene <sceneRef>             # Append scene to current scene
action focus_selected                   # Focus camera on selected nodes
```
<node> can be a node name, instanceId (number), or `$selected` (primary selected node).
Use double quotes for args containing spaces.

## JavaScript API (Editor.* helpers)
```javascript
// Modification (each creates an Runtime::Command::ICommand, undoable)
Editor.renameNode(nameOrId, newName)
Editor.deleteNode(nameOrId)
Editor.duplicateNode(nameOrId) // returns new id
Editor.moveNode(nameOrId, x, y, z)
Editor.rotateNode(nameOrId, rx, ry, rz)  // euler degrees
Editor.scaleNode(nameOrId, sx, sy, sz)
Editor.setProperty(nameOrId, componentName, propertyName, value)

// Query (read-only)
Editor.findNodes(regexPattern) // returns [{id, name, position:{x,y,z}}]
Editor.getNodeCount()          // returns number
Editor.getNodeInfo(nameOrId)   // returns {id, name, position, rotation, scale, components:[]}
Editor.selectNode(nameOrId)

// Output
Editor.log(message)
```

)";
    }

    std::string FEditorAIService::BuildSceneContext(const EditorContext& ctx)
    {
        std::string result = "## Current Scene\n";
        const auto& nodes = ctx.scene.Nodes();
        result += fmt::format("Total nodes: {}\n\n", nodes.size());

        if (nodes.empty())
        {
            result += "No scene loaded.\n\n";
            return result;
        }

        constexpr size_t maxNodes = 80;
        result += "| InstanceId | Name | Position | Components |\n";
        result += "|---|---|---|---|\n";

        size_t count = 0;
        for (const auto& node : nodes)
        {
            if (count >= maxNodes)
            {
                result += fmt::format("| ... | ({} more nodes) | ... | ... |\n", nodes.size() - maxNodes);
                break;
            }

            const auto pos = node->Translation();
            result += fmt::format("| {} | {} | ({:.1f}, {:.1f}, {:.1f}) | {} |\n", node->GetInstanceId(),
                                  node->GetName(), pos.x, pos.y, pos.z, BuildComponentList(*node));
            count++;
        }

        result += "\n";
        return result;
    }

    std::string FEditorAIService::BuildSelectionContext(const EditorContext& ctx)
    {
        std::string result = "## Current Selection\n";

        std::vector<uint32_t> selectedIds = ctx.scene.GetSelectedIds();
        if (selectedIds.empty())
        {
            const uint32_t selectedId = ctx.scene.GetSelectedId();
            if (selectedId != static_cast<uint32_t>(-1))
            {
                selectedIds.push_back(selectedId);
            }
        }

        const uint32_t primaryId = ctx.scene.GetSelectedId();
        result += fmt::format("Selected count: {}\n", selectedIds.size());
        result += fmt::format("Primary selected id: {}\n\n",
                              primaryId == static_cast<uint32_t>(-1) ? std::string("None")
                                                                      : std::to_string(primaryId));

        if (selectedIds.empty())
        {
            result += "No current selection.\n\n";
            return result;
        }

        result += "| InstanceId | Name | Position | Rotation(deg) | Scale | Components |\n";
        result += "|---|---|---|---|---|---|\n";

        for (uint32_t id : selectedIds)
        {
            auto* node = ctx.scene.GetNodeByInstanceId(id);
            if (!node)
            {
                continue;
            }

            const auto t = node->Translation();
            const auto euler = glm::degrees(glm::eulerAngles(node->Rotation()));
            const auto s = node->Scale();

            result += fmt::format("| {} | {} | ({:.2f}, {:.2f}, {:.2f}) | ({:.1f}, {:.1f}, {:.1f}) | ({:.2f}, {:.2f}, {:.2f}) | {} |\n",
                                  node->GetInstanceId(), node->GetName(), t.x, t.y, t.z, euler.x, euler.y, euler.z,
                                  s.x, s.y, s.z, BuildComponentList(*node));
        }

        result += "\n";
        return result;
    }

    std::string FEditorAIService::BuildSceneAssetCatalog()
    {
        namespace fs = std::filesystem;

        const fs::path assetsRoot(Utilities::FileHelper::GetPlatformFilePath("assets"));
        std::vector<std::string> scenes;
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
            if (!Runtime::Scene::SceneList::IsSupportedScenePath(file))
            {
                continue;
            }

            scenes.push_back(fs::relative(file, assetsRoot).generic_string());
        }

        std::sort(scenes.begin(), scenes.end());
        scenes.erase(std::unique(scenes.begin(), scenes.end()), scenes.end());

        std::string result = "## Scene Asset Catalog (.glb/.gltf/.ldr/.mpd)\n";
        if (scenes.empty())
        {
            result += "No scene assets found.\n\n";
            return result;
        }

        constexpr size_t maxItems = 80;
        result += fmt::format("Total scene assets: {}\n", scenes.size());
        for (size_t i = 0; i < scenes.size() && i < maxItems; ++i)
        {
            result += fmt::format("- `{}`\n", scenes[i]);
        }
        if (scenes.size() > maxItems)
        {
            result += fmt::format("- ... ({} more)\n", scenes.size() - maxItems);
        }
        result += "\n";
        return result;
    }

    std::string FEditorAIService::BuildPropertyTypeInfo(const EditorContext& ctx)
    {
        std::string result = "## Component Properties\n";

        // Collect unique component types from the scene
        std::set<std::string> seenTypes;
        for (const auto& node : ctx.scene.Nodes())
        {
            for (const auto& comp : node->GetComponents())
            {
                std::string typeName(comp->GetTypeName());
                if (seenTypes.count(typeName))
                {
                    continue;
                }
                seenTypes.insert(typeName);

                entt::meta_type metaType = comp->GetMetaType();
                auto properties = Reflection::PropertyAccessor::GetProperties(metaType);

                result += fmt::format("### {}\n", typeName);
                for (const auto& prop : properties)
                {
                    std::string typeStr;
                    switch (prop.type)
                    {
                    case Reflection::PropertyType::Bool:
                        typeStr = "bool";
                        break;
                    case Reflection::PropertyType::Int32:
                        typeStr = "int";
                        break;
                    case Reflection::PropertyType::UInt32:
                        typeStr = "uint";
                        break;
                    case Reflection::PropertyType::Float:
                        typeStr = "float";
                        break;
                    case Reflection::PropertyType::String:
                        typeStr = "string";
                        break;
                    case Reflection::PropertyType::Vec2:
                        typeStr = "vec2";
                        break;
                    case Reflection::PropertyType::Vec3:
                        typeStr = "vec3";
                        break;
                    case Reflection::PropertyType::Vec4:
                        typeStr = "vec4";
                        break;
                    case Reflection::PropertyType::Quat:
                        typeStr = "quat";
                        break;
                    default:
                        typeStr = "other";
                        break;
                    }
                    result += fmt::format("- `{}` ({}){}\n", prop.name, typeStr,
                                          prop.meta.IsReadOnly() ? " [readonly]" : "");
                }
                result += "\n";
            }
        }

        return result;
    }

    // ========== Response Processing ==========

    std::vector<FCodeBlock> FEditorAIService::ExtractFromResponse(const std::string& response)
    {
        std::vector<FCodeBlock> blocks;
        size_t pos = 0;

        while (pos < response.size())
        {
            size_t blockStart = response.find("```", pos);
            if (blockStart == std::string::npos)
            {
                break;
            }

            size_t langEnd = response.find('\n', blockStart);
            if (langEnd == std::string::npos)
            {
                break;
            }

            std::string lang = response.substr(blockStart + 3, langEnd - blockStart - 3);
            // Trim lang
            auto langStart = lang.find_first_not_of(" \t\r");
            if (langStart != std::string::npos)
            {
                lang = lang.substr(langStart);
            }
            auto langEndPos = lang.find_last_not_of(" \t\r");
            if (langEndPos != std::string::npos)
            {
                lang = lang.substr(0, langEndPos + 1);
            }

            size_t codeStart = langEnd + 1;
            size_t codeEnd = response.find("```", codeStart);
            if (codeEnd == std::string::npos)
            {
                codeEnd = response.size();
            }

            std::string code = response.substr(codeStart, codeEnd - codeStart);

            FCodeBlock block;
            if (lang == "editorscript")
            {
                block.type = FCodeBlock::EType::EditorScript;
            }
            else if (lang == "javascript" || lang == "js")
            {
                block.type = FCodeBlock::EType::JavaScript;
            }
            else
            {
                // Default to editorscript for unrecognized
                block.type = FCodeBlock::EType::EditorScript;
            }
            block.code = code;
            blocks.push_back(std::move(block));

            pos = codeEnd + 3;
        }

        // If no code blocks found, treat entire response as editorscript
        if (blocks.empty() && !response.empty())
        {
            blocks.push_back({FCodeBlock::EType::EditorScript, response});
        }

        return blocks;
    }

    // ========== Execution ==========

    void FEditorAIService::ExecuteDirect(const std::string& input, EditorContext& ctx)
    {
        // Check if it looks like JavaScript (contains Editor. or starts with //)
        if (input.find("Editor.") != std::string::npos || input.find("const ") == 0 || input.find("let ") == 0 ||
            input.find("var ") == 0 || input.find("for ") == 0 || input.find("for(") == 0)
        {
            executor_.EvalJavaScript(input);
        }
        else
        {
            executor_.ExecuteScriptText(input, &ctx, false);
        }
    }

    void FEditorAIService::GenerateAsync(const std::string& userPrompt, const EditorContext& ctx)
    {
        auto* ai = engine_.GetAIService();
        if (!ai || !ai->IsConfigured())
        {
            status_ = EEditorAIStatus::Error;
            statusMessage_ = "AI service not configured. Check assets/configs/ai_config.json";
            return;
        }

        status_ = EEditorAIStatus::Generating;
        statusMessage_ = "Generating...";
        cancelRequested_.store(false);

        if (useAgentLoop_ && ai->SupportsTools())
        {
            RunAgentLoopAsync(userPrompt, ctx);
        }
        else
        {
            RunLegacyAsync(userPrompt, ctx);
        }
    }

    void FEditorAIService::RunLegacyAsync(const std::string& userPrompt, const EditorContext& ctx)
    {
        std::string systemPrompt = BuildSystemPrompt(ctx);
        std::string fullPrompt = systemPrompt + "\nUser request: " + userPrompt;

        std::thread([this, fullPrompt]() {
            auto* ai = engine_.GetAIService();
            auto response = ai->GenerateText(fullPrompt);

            {
                std::lock_guard<std::mutex> lock(resultMutex_);
                pendingResponse_ = response;
                hasPendingResult_ = true;
            }
        }).detach();
    }

    void FEditorAIService::RunAgentLoopAsync(const std::string& userPrompt, const EditorContext& ctx)
    {
        EnsureToolRegistry();

        NextAI::FChatRequest seed;
        seed.messages.push_back(NextAI::FChatMessage::System(BuildAgentSystemPrompt(ctx)));
        seed.messages.push_back(NextAI::FChatMessage::User(userPrompt));

        NextAI::FAgentOptions opts;
        opts.maxSteps = maxAgentSteps_;
        opts.temperature = 0.3f;
        opts.groundingReminder =
            "You produced a final answer without calling any tool. If the user's request "
            "requires looking at or modifying the scene/source, call the appropriate tool "
            "first. Retry now.";

        std::thread([this, seed = std::move(seed), opts]() mutable {
            auto* ai = engine_.GetAIService();
            NextAI::FChatProviderFn provider =
                [ai](const NextAI::FChatRequest& req) { return ai->Chat(req); };

            NextAI::FAgentResult result = NextAI::FAgentLoop::Run(
                std::move(seed), toolRegistry_, std::move(provider), opts,
                &agentEventSink_, &dispatcher_, &cancelRequested_);

            NextAI::FAIResponse legacyShaped;
            if (result.success)
            {
                legacyShaped = NextAI::FAIResponse::Success(result.finalContent);
            }
            else if (result.cancelled)
            {
                legacyShaped = NextAI::FAIResponse::Failure("cancelled by user");
            }
            else if (result.maxStepsExceeded)
            {
                legacyShaped = NextAI::FAIResponse::Failure(
                    fmt::format("agent loop hit max steps ({})", opts.maxSteps));
            }
            else
            {
                legacyShaped = NextAI::FAIResponse::Failure(
                    result.errorMessage.empty() ? "agent loop failed" : result.errorMessage);
            }

            {
                std::lock_guard<std::mutex> lock(resultMutex_);
                pendingResponse_ = legacyShaped;
                hasPendingResult_ = true;
            }
        }).detach();
    }

    void FEditorAIService::ConsumePendingResult(EditorContext& ctx)
    {
        NextAI::FAIResponse response;
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            if (!hasPendingResult_)
            {
                return;
            }
            response = pendingResponse_;
            hasPendingResult_ = false;
        }

        if (!response.success)
        {
            status_ = EEditorAIStatus::Error;
            statusMessage_ = response.message;
            SPDLOG_ERROR("[EditorAI] Generation failed: {}", response.message);
            return;
        }

        lastResponse_ = response.text;

        if (useAgentLoop_)
        {
            // Agent loop already executed tools during the run; the final text is
            // just a natural-language summary. Anything high-risk landed in
            // pendingActions_ via HarvestDeferredActions() during PumpMainThread().
            status_ = EEditorAIStatus::Idle;
            statusMessage_ = pendingActions_.empty() ? "Done" : "Waiting confirmation";
            return;
        }

        status_ = EEditorAIStatus::Executing;
        statusMessage_ = "Executing...";

        auto blocks = ExtractFromResponse(response.text);
        for (const auto& block : blocks)
        {
            if (block.type == FCodeBlock::EType::EditorScript)
            {
                executor_.ExecuteScriptText(block.code, &ctx, true);
                auto deferredActions = executor_.TakeDeferredActions();
                for (auto& deferred : deferredActions)
                {
                    FPendingEditorAction pendingAction;
                    pendingAction.id = nextPendingActionId_++;
                    pendingAction.request = std::move(deferred);
                    pendingActions_.push_back(std::move(pendingAction));
                }
            }
            else
            {
                executor_.EvalJavaScript(block.code);
            }
        }

        status_ = EEditorAIStatus::Idle;
        statusMessage_ = pendingActions_.empty() ? "Done" : "Waiting confirmation";
    }

    bool FEditorAIService::ConfirmPendingAction(uint64_t actionId, EditorContext& ctx)
    {
        auto it = std::find_if(pendingActions_.begin(), pendingActions_.end(),
                               [actionId](const FPendingEditorAction& action) { return action.id == actionId; });
        if (it == pendingActions_.end())
        {
            return false;
        }

        const bool ok = executor_.ExecuteDeferredAction(it->request, ctx);
        pendingActions_.erase(it);
        if (pendingActions_.empty() && status_ == EEditorAIStatus::Idle)
        {
            statusMessage_ = "Done";
        }
        return ok;
    }

    bool FEditorAIService::CancelPendingAction(uint64_t actionId)
    {
        auto it = std::find_if(pendingActions_.begin(), pendingActions_.end(),
                               [actionId](const FPendingEditorAction& action) { return action.id == actionId; });
        if (it == pendingActions_.end())
        {
            return false;
        }

        executor_.Log(fmt::format("Cancelled pending action: {}", it->request.commandText));
        pendingActions_.erase(it);
        if (pendingActions_.empty() && status_ == EEditorAIStatus::Idle)
        {
            statusMessage_ = "Done";
        }
        return true;
    }

} // namespace Editor
