#include "EditorUi.hpp"

#include "Automation/EditorScriptExecutor.hpp"
#include "EditorContext.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextQuickJS/NextQuickJSModule.hpp"
#include "Modules/NextQuickJS/QuickJSEngine.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <cstring>
#include <imgui.h>

namespace Editor
{
    namespace
    {
        char scriptInputBuffer[8192] = {};
        std::vector<ScriptLogEntry> logHistory;
        std::vector<FDeferredEditorAction> pendingActions;
        bool logScrollToBottom = false;
        std::unique_ptr<FEditorScriptExecutor> executor;

        void EnsureExecutor(NextEngine& engine)
        {
            if (executor)
            {
                return;
            }
            executor = std::make_unique<FEditorScriptExecutor>(engine);
            if (auto* qjs = Modules::NextQuickJS::Get(engine))
            {
                qjs->SetEditorBindingsCallback([](void* context)
                {
                    if (executor)
                    {
                        executor->RegisterEditorBindings(context);
                    }
                });
            }
        }

        void DrainExecutionState()
        {
            auto entries = executor->TakeLog();
            for (auto& entry : entries)
            {
                logHistory.push_back(std::move(entry));
                logScrollToBottom = true;
            }
            auto deferred = executor->TakeDeferredActions();
            for (auto& action : deferred)
            {
                pendingActions.push_back(std::move(action));
            }
        }

        void DrawPendingActions(EditorContext& context)
        {
            if (pendingActions.empty())
            {
                return;
            }
            ImGui::SeparatorText("Confirmation Required");
            std::optional<size_t> confirmed;
            std::optional<size_t> cancelled;
            for (size_t index = 0; index < pendingActions.size(); ++index)
            {
                const auto& pending = pendingActions[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextWrapped("%s", pending.description.c_str());
                if (ImGui::Button("Confirm"))
                {
                    confirmed = index;
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    cancelled = index;
                }
                ImGui::PopID();
            }
            if (confirmed)
            {
                const bool ok = executor->ExecuteDeferredAction(pendingActions[*confirmed], context);
                logHistory.push_back({ok ? "Confirmed and executed." : "Execution failed.", !ok});
                pendingActions.erase(pendingActions.begin() + static_cast<std::ptrdiff_t>(*confirmed));
            }
            else if (cancelled)
            {
                logHistory.push_back({"Cancelled: " + pendingActions[*cancelled].commandText, false});
                pendingActions.erase(pendingActions.begin() + static_cast<std::ptrdiff_t>(*cancelled));
            }
        }
    }

    void DrawScriptConsolePanel(EditorContext& context, EditorUiState& ui)
    {
        if (!ImGui::Begin("Script Console", &ui.scriptConsolePanel))
        {
            ImGui::End();
            return;
        }
        EnsureExecutor(context.engine);
        DrainExecutionState();

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 7.0f;
        ImGui::BeginChild("ScriptLogRegion", ImVec2(0, -footerHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& entry : logHistory)
        {
            if (entry.isError)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s %s", ICON_FA_CIRCLE_XMARK,
                                   entry.message.c_str());
            }
            else
            {
                ImGui::TextWrapped("%s", entry.message.c_str());
            }
        }
        DrawPendingActions(context);
        if (logScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            logScrollToBottom = false;
        }
        ImGui::EndChild();
        ImGui::Separator();

        const float editorHeight = footerHeight - ImGui::GetFrameHeightWithSpacing() * 1.5f;
        ImGui::InputTextMultiline("##ScriptEditor", scriptInputBuffer, sizeof(scriptInputBuffer),
                                  ImVec2(-FLT_MIN, editorHeight), ImGuiInputTextFlags_AllowTabInput);
        if (ImGui::Button(ICON_FA_PLAY " Run", ImVec2(110.0f, 0.0f)) && scriptInputBuffer[0] != '\0')
        {
            const std::string input = scriptInputBuffer;
            logHistory.push_back({"> [script]\n" + input, false});
            if (input.find("Editor.") != std::string::npos || input.starts_with("const ") ||
                input.starts_with("let ") || input.starts_with("var ") || input.starts_with("for"))
            {
                executor->EvalJavaScript(input);
            }
            else
            {
                executor->ExecuteScriptText(input, &context, true);
            }
            DrainExecutionState();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ERASER " Clear", ImVec2(110.0f, 0.0f)))
        {
            scriptInputBuffer[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " Clear Log", ImVec2(110.0f, 0.0f)))
        {
            logHistory.clear();
        }
        ImGui::End();
    }
} // namespace Editor
