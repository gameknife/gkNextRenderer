#include "Editor/AI/EditorAIService.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Runtime/Subsystems/QuickJSEngine.hpp"

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

    // ========== System Prompt ==========

    std::string FEditorAIService::BuildSystemPrompt()
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
        prompt += BuildSceneContext();
        prompt += BuildPropertyTypeInfo();

        prompt += R"(
## Rules
1. Always include natural language explanation before and after code blocks
2. Use the user's language (if user writes Chinese, reply in Chinese)
3. Node names are case-sensitive
4. When names are ambiguous, use instanceId (number)
5. Transform values are world-space absolute coordinates, rotation is euler angles (degrees)
6. After execution summary, suggest 1-2 possible next actions the user might want
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
```
<node> can be a node name or instanceId (number).

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

    std::string FEditorAIService::BuildSceneContext()
    {
        auto* scene = engine_.GetScenePtr();
        if (!scene)
        {
            return "## Current Scene\nNo scene loaded.\n\n";
        }

        std::string result = "## Current Scene\n";
        const auto& nodes = scene->Nodes();
        result += fmt::format("Total nodes: {}\n\n", nodes.size());

        constexpr size_t maxNodes = 200;
        size_t count = 0;

        result += "| InstanceId | Name | Position | Components |\n";
        result += "|---|---|---|---|\n";

        for (const auto& node : nodes)
        {
            if (count >= maxNodes)
            {
                result += fmt::format("| ... | ({} more nodes) | ... | ... |\n", nodes.size() - maxNodes);
                break;
            }

            auto pos = node->Translation();
            std::string comps;
            for (const auto& comp : node->GetComponents())
            {
                if (!comps.empty())
                {
                    comps += ", ";
                }
                comps += std::string(comp->GetTypeName());
            }

            result += fmt::format("| {} | {} | ({:.1f}, {:.1f}, {:.1f}) | {} |\n", node->GetInstanceId(),
                                  node->GetName(), pos.x, pos.y, pos.z, comps);
            count++;
        }

        result += "\n";
        return result;
    }

    std::string FEditorAIService::BuildPropertyTypeInfo()
    {
        std::string result = "## Component Properties\n";

        auto* scene = engine_.GetScenePtr();
        if (!scene)
        {
            return result;
        }

        // Collect unique component types from the scene
        std::set<std::string> seenTypes;
        for (const auto& node : scene->Nodes())
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

    void FEditorAIService::ExecuteDirect(const std::string& input)
    {
        // Check if it looks like JavaScript (contains Editor. or starts with //)
        if (input.find("Editor.") != std::string::npos || input.find("const ") == 0 || input.find("let ") == 0 ||
            input.find("var ") == 0 || input.find("for ") == 0 || input.find("for(") == 0)
        {
            executor_.EvalJavaScript(input);
        }
        else
        {
            executor_.ExecuteScriptText(input);
        }
    }

    void FEditorAIService::GenerateAsync(const std::string& userPrompt)
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

        std::string systemPrompt = BuildSystemPrompt();
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

    void FEditorAIService::ConsumePendingResult()
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
                executor_.ExecuteScriptText(block.code);
            }
            else
            {
                executor_.EvalJavaScript(block.code);
            }
        }

        status_ = EEditorAIStatus::Idle;
        statusMessage_ = "Done";
    }

} // namespace Editor
