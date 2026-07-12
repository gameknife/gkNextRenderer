#include "EditorUi.hpp"

#include "AI/EditorAIService.hpp"
#include "Panels/imgui_markdown_custom.h"
#include "Engine/Runtime/Engine.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>

namespace Editor
{
    // ========== Panel State ==========
    namespace
    {
        struct ChatMessage
        {
            std::string text;
            bool isUser;
            bool isError = false;
        };

        // AI tab: conversation history
        char aiInputBuffer[4096] = {};
        std::vector<ChatMessage> chatHistory;
        bool chatScrollToBottom = false;

        // Agent event stream (rolling buffer of recent steps; capped to keep ImGui draw cheap).
        struct AgentEventDisplay
        {
            int step;
            std::string phase;
            std::string toolName;
            std::string summary;
            std::string detail;
        };
        std::vector<AgentEventDisplay> agentEventLog;
        bool agentStepsOpen = false;
        constexpr size_t kAgentEventCap = 200;

        // EditorScript tab: execution logs
        char scriptInputBuffer[8192] = {};
        std::vector<ScriptLogEntry> logHistory;
        bool logScrollToBottom = false;

        std::unique_ptr<FEditorAIService> aiService;

        void SetAiInputBuffer(const std::string& text)
        {
            const size_t capacity = sizeof(aiInputBuffer) - 1;
            const size_t copyLength = std::min(capacity, text.size());
            if (copyLength > 0)
            {
                std::memcpy(aiInputBuffer, text.data(), copyLength);
            }
            aiInputBuffer[copyLength] = '\0';
        }
    } // namespace

    // ========== UI Drawing ==========

    static void DrawProviderSelector(NextEngine& engine, FEditorAIService& service)
    {
		(void)engine;

        // Status indicator
        auto status = service.GetStatus();
        if (status == EEditorAIStatus::Generating)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_FA_SPINNER " Generating...");
        }
        else if (status == EEditorAIStatus::Executing)
        {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), ICON_FA_GEAR " Executing...");
        }
        else if (status == EEditorAIStatus::Error)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_FA_CIRCLE_EXCLAMATION " Error");
        }
        else
        {
            if (service.IsAIConfigured())
            {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), ICON_FA_CIRCLE_CHECK " Ready");
            }
            else
            {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), ICON_FA_CIRCLE_MINUS " Not Configured");
            }
        }

        // Provider combo (same line, right aligned)
        ImGui::SameLine();
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availWidth - 120.0f);

        bool generating = (status == EEditorAIStatus::Generating);
        if (generating)
        {
            ImGui::BeginDisabled();
        }

        ImGui::SetNextItemWidth(120.0f);
		const auto& providers = service.GetProviderCatalog();
		const std::string& currentId = service.GetCurrentProviderId();
		std::string currentName = currentId;
		if (auto current = std::find_if(providers.begin(), providers.end(), [&currentId](const auto& option) { return option.id == currentId; }); current != providers.end()) currentName = current->displayName;

        if (ImGui::BeginCombo("##ProviderSelect", currentName.c_str()))
        {
            for (const auto& option : providers)
            {
				const bool isSelected = option.id == currentId;
				const bool isConfigured = option.configured;

                if (!isConfigured)
                {
                    ImGui::BeginDisabled();
                }

				if (ImGui::Selectable(option.displayName.c_str(), isSelected))
                {
					if (!isSelected)
                    {
						if (service.SelectProvider(option.id))
                        {
							chatHistory.push_back({fmt::format("Switched to {} provider", option.displayName), false});
                            chatScrollToBottom = true;
                        }
                        else
                        {
                            chatHistory.push_back(
								{fmt::format("Failed to switch to {}: {}", option.displayName, service.GetStatusMessage()), false, true});
                            chatScrollToBottom = true;
                        }
                    }
                }

                if (!isConfigured)
                {
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
						ImGui::SetTooltip("Provider is not configured in gnb AI catalog");
                    }
                }

                if (isSelected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (generating)
        {
            ImGui::EndDisabled();
        }
    }

    static void DrawPendingActions(EditorContext& ctx, FEditorAIService& service)
    {
        const auto& pendingActions = service.GetPendingActions();
        if (pendingActions.empty())
        {
            return;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Pending Actions");

        uint64_t confirmId = 0;
        uint64_t cancelId = 0;
        bool doConfirm = false;
        bool doCancel = false;

        for (const auto& pending : pendingActions)
        {
            ImGui::PushID(static_cast<int>(pending.id));

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.82f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", pending.request.description.c_str());
            ImGui::PopStyleColor();

            if (ImGui::Button("Confirm", ImVec2(90.0f, 0.0f)))
            {
                confirmId = pending.id;
                doConfirm = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f)))
            {
                cancelId = pending.id;
                doCancel = true;
            }
            ImGui::Spacing();
            ImGui::PopID();
        }

        if (doConfirm)
        {
            const bool ok = service.ConfirmPendingAction(confirmId, ctx);
            chatHistory.push_back({ok ? "已确认并执行待处理 action。" : "确认失败：无法执行待处理 action。", false, !ok});
            chatScrollToBottom = true;
        }

        if (doCancel)
        {
            const bool ok = service.CancelPendingAction(cancelId);
            chatHistory.push_back({ok ? "已取消待处理 action。" : "取消失败：未找到待处理 action。", false, !ok});
            chatScrollToBottom = true;
        }
    }

    static const char* PhaseColorTag(const std::string& phase)
    {
        if (phase == "call") return "calling";
        if (phase == "result") return "result";
        if (phase == "error") return "error";
        if (phase == "final") return "final";
        if (phase == "cancelled") return "cancelled";
        return phase.c_str();
    }

    static ImVec4 PhaseColor(const std::string& phase)
    {
        if (phase == "error" || phase == "cancelled") return ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
        if (phase == "final") return ImVec4(0.6f, 0.9f, 0.6f, 1.0f);
        if (phase == "result") return ImVec4(0.6f, 0.85f, 1.0f, 1.0f);
        return ImVec4(0.95f, 0.8f, 0.4f, 1.0f);
    }

    static void DrawAgentStepsSection(FEditorAIService& service)
    {
        if (!ImGui::CollapsingHeader("Agent Steps", agentStepsOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0))
        {
            return;
        }
        // Drain new events from the sink into our display log.
        auto drained = service.GetAgentEventSink().Drain();
        for (auto& e : drained)
        {
            agentEventLog.push_back({e.step, NextAI::AgentEventPhaseToString(e.phase),
                                     e.toolName, e.summary, e.detail});
        }
        while (agentEventLog.size() > kAgentEventCap)
        {
            agentEventLog.erase(agentEventLog.begin());
        }

        if (ImGui::Button(ICON_FA_TRASH " Clear"))
        {
            agentEventLog.clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu events (cap %zu)", agentEventLog.size(), kAgentEventCap);

        ImGui::BeginChild("AgentEventList", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 10),
                          ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& e : agentEventLog)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, PhaseColor(e.phase));
            ImGui::Text("step %d  %s  %s", e.step, PhaseColorTag(e.phase),
                        e.toolName.empty() ? "" : e.toolName.c_str());
            ImGui::PopStyleColor();
            if (!e.summary.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("· %s", e.summary.c_str());
            }
            if (!e.detail.empty())
            {
                ImGui::Indent(16.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.7f, 1.0f));
                ImGui::TextWrapped("%s", e.detail.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent(16.0f);
            }
        }
        if (!drained.empty())
        {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    static void DrawAIAssistantTab(EditorContext& ctx, FEditorAIService& service)
    {
        auto status = service.GetStatus();
        bool generating = (status == EEditorAIStatus::Generating);

        DrawAgentStepsSection(service);

        if (ImGui::Button(ICON_FA_ERASER " New Chat"))
        {
            chatHistory.clear();
            agentEventLog.clear();
            service.ClearConversation();
            chatScrollToBottom = false;
        }

        // Chat area
        float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.5f;
        ImGui::BeginChild("AIChatRegion", ImVec2(0, -footerHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& msg : chatHistory)
        {
            if (msg.isUser)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted(ICON_FA_USER);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(msg.text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            else if (msg.isError)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("%s %s", ICON_FA_CIRCLE_EXCLAMATION, msg.text.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                // AI response rendered as markdown
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted(ICON_FA_ROBOT);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImVec4 aiColor(0.7f, 0.85f, 1.0f, 1.0f);
                RenderMarkdown(msg.text, aiColor);
            }
            ImGui::Spacing();
            ImGui::Spacing();
        }

        // Generating indicator
        if (generating)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 0.7f));
            ImGui::TextWrapped(ICON_FA_SPINNER " Thinking...");
            ImGui::PopStyleColor();
        }

        DrawPendingActions(ctx, service);

        if (chatScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            chatScrollToBottom = false;
        }

        ImGui::EndChild();

        ImGui::Separator();

        // Input area: text input + Send button
        float buttonWidth = 80.0f;
        float inputWidth = ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x;
        inputWidth = std::max(inputWidth, 100.0f);

        if (generating)
        {
            ImGui::BeginDisabled();
        }

        ImGui::PushItemWidth(inputWidth);
        bool submitted = ImGui::InputTextMultiline("##AIInput", aiInputBuffer, sizeof(aiInputBuffer),
                                                   ImVec2(inputWidth, ImGui::GetFrameHeightWithSpacing() * 1.5f),
                                                   ImGuiInputTextFlags_CtrlEnterForNewLine |
                                                       ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopItemWidth();

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_PAPER_PLANE " Send", ImVec2(buttonWidth, ImGui::GetFrameHeightWithSpacing() * 1.5f)) ||
            submitted)
        {
            if (aiInputBuffer[0] != '\0')
            {
                chatHistory.push_back({aiInputBuffer, true});
                service.GenerateAsync(aiInputBuffer, ctx);
                aiInputBuffer[0] = '\0';
                chatScrollToBottom = true;
            }
        }

        if (generating)
        {
            ImGui::EndDisabled();
            // Stop button replaces the disabled Send footprint so the user has a
            // way out of an in-flight agent loop.
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.25f, 0.25f, 1.0f));
            if (ImGui::Button(ICON_FA_STOP " Stop", ImVec2(80.0f, ImGui::GetFrameHeightWithSpacing() * 1.5f)))
            {
                service.RequestCancel();
            }
            ImGui::PopStyleColor(2);
            if (service.IsCancelRequested())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "cancelling...");
            }
        }

    }

    static void DrawEditorScriptTab(EditorContext& ctx, FEditorAIService& service)
    {
        // Execution log area
        float footerHeight = ImGui::GetFrameHeightWithSpacing() * 6.0f + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("ScriptLogRegion", ImVec2(0, -footerHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& entry : logHistory)
        {
            if (entry.isError)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("%s %s", ICON_FA_CIRCLE_XMARK, entry.message.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::TextWrapped("%s", entry.message.c_str());
            }
        }

        if (logScrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            logScrollToBottom = false;
        }

        ImGui::EndChild();

        ImGui::Separator();

        // Multiline script editor
        float buttonHeight = ImGui::GetFrameHeightWithSpacing();
        float editorHeight = footerHeight - buttonHeight - ImGui::GetStyle().ItemSpacing.y;
        float availWidth = ImGui::GetContentRegionAvail().x;

        ImGui::InputTextMultiline("##ScriptEditor", scriptInputBuffer, sizeof(scriptInputBuffer),
                                  ImVec2(availWidth, editorHeight), ImGuiInputTextFlags_AllowTabInput);

        // Buttons row
        float buttonWidth = 120.0f;
        if (ImGui::Button(ICON_FA_PLAY " Run Script", ImVec2(buttonWidth, 0)))
        {
            if (scriptInputBuffer[0] != '\0')
            {
                logHistory.push_back({fmt::format("> [script]\n{}", scriptInputBuffer), false});
                service.ExecuteDirect(scriptInputBuffer, ctx);
                logScrollToBottom = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ERASER " Clear", ImVec2(buttonWidth, 0)))
        {
            scriptInputBuffer[0] = '\0';
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH " Clear Log", ImVec2(buttonWidth, 0)))
        {
            logHistory.clear();
        }
    }

    void TickAIAgentMainThread(EditorContext& ctx)
    {
        // aiService is created lazily by DrawAIPanel. Once it exists, keep pumping
        // its main-thread queue every frame regardless of panel visibility, so agent
        // tool calls (RequiresMainThread) don't time out — and deferred high-risk
        // actions still reach pendingActions_ — when the panel is hidden, collapsed,
        // or on an inactive docked tab.
        if (!aiService)
        {
            return;
        }
        aiService->SetCurrentContext(&ctx);
        aiService->PumpMainThread();
    }

    void DrawAIPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (!ImGui::Begin("AI Assistant", &ui.aiPanel))
        {
            ImGui::End();
            return;
        }

        // Lazy init
        if (!aiService)
        {
            aiService = std::make_unique<FEditorAIService>(ctx.engine);
        }

        // Main-thread pump now runs every frame via TickAIAgentMainThread (called
        // from EditorInterface::Render), independent of this panel's visibility.
        // Ensure the freshly-created service sees this frame's context immediately.
        aiService->SetCurrentContext(&ctx);

        // Poll async results
        if (aiService->HasPendingResult())
        {
            aiService->ConsumePendingResult(ctx);

            auto status = aiService->GetStatus();
            if (status == EEditorAIStatus::Error)
            {
                chatHistory.push_back({aiService->GetStatusMessage(), false, true});
            }
            else
            {
                const auto& response = aiService->GetLastResponse();
                if (!response.empty())
                {
                    chatHistory.push_back({response, false});
                }
            }
            chatScrollToBottom = true;
        }

        // Execution logs → EditorScript tab only
        auto newLogs = aiService->TakeLog();
        for (auto& entry : newLogs)
        {
            logHistory.push_back(std::move(entry));
            logScrollToBottom = true;
        }

        // Provider selector + status
        DrawProviderSelector(ctx.engine, *aiService);

        ImGui::Separator();

        // Tab bar
        if (ImGui::BeginTabBar("##AITabs"))
        {
            if (ImGui::BeginTabItem(ICON_FA_ROBOT " AI"))
            {
                DrawAIAssistantTab(ctx, *aiService);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(ICON_FA_CODE " EditorScript"))
            {
                DrawEditorScriptTab(ctx, *aiService);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // Clear context at end of frame so background workers don't deref a stale
        // pointer if the panel is closed next frame.
        aiService->SetCurrentContext(nullptr);

        ImGui::End();
    }
} // namespace Editor
