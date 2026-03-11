#include "Editor/AI/EditorAIService.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Editor/EditorContext.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Runtime/Scene/SceneList.hpp"
#include "Runtime/Subsystems/QuickJSEngine.hpp"
#include "Utilities/FileHelper.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
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
// Modification (each creates an ICommand, undoable)
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
            if (!SceneList::IsSupportedScenePath(file))
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
