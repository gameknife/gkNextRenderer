#include "Editor/EditorUi.hpp"

#include "Editor/AI/EditorAIService.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/AIService.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>

namespace Editor
{
    namespace
    {
        struct ChatMessage
        {
            std::string text;
            bool isUser; // true = user prompt, false = AI response
            bool isError = false;
        };

        // AI tab: conversation history
        char aiInputBuffer[4096] = {};
        std::vector<ChatMessage> chatHistory;
        bool chatScrollToBottom = false;

        // EditorScript tab: execution logs
        char scriptInputBuffer[8192] = {};
        std::vector<ScriptLogEntry> logHistory;
        bool logScrollToBottom = false;

        std::unique_ptr<FEditorAIService> aiService;
    } // namespace

    static void DrawProviderSelector(NextEngine& engine, FEditorAIService& service)
    {
        auto* ai = engine.GetAIService();
        if (!ai)
        {
            ImGui::TextDisabled("AI service unavailable");
            return;
        }

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
            if (ai->IsConfigured())
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
        auto providers = NextAI::FAIService::GetAvailableProviders();
        auto currentType = ai->GetProviderType();
        std::string currentName = ai->GetProviderName();

        if (ImGui::BeginCombo("##ProviderSelect", currentName.c_str()))
        {
            for (const auto& [type, name] : providers)
            {
                bool isSelected = (type == currentType);
                bool isConfigured = ai->IsProviderConfigured(type);

                if (!isConfigured)
                {
                    ImGui::BeginDisabled();
                }

                if (ImGui::Selectable(name.c_str(), isSelected))
                {
                    if (type != currentType)
                    {
                        if (ai->SwitchProvider(type))
                        {
                            chatHistory.push_back({fmt::format("Switched to {} provider", name), false});
                            chatScrollToBottom = true;
                        }
                        else
                        {
                            chatHistory.push_back(
                                {fmt::format("Failed to switch to {}: {}", name, ai->GetStatusMessage()), false, true});
                            chatScrollToBottom = true;
                        }
                    }
                }

                if (!isConfigured)
                {
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip("Not configured in ai_config.json");
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

    static void DrawAIAssistantTab(FEditorAIService& service)
    {
        auto status = service.GetStatus();
        bool generating = (status == EEditorAIStatus::Generating);

        // Chat area
        float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.5f;
        ImGui::BeginChild("AIChatRegion", ImVec2(0, -footerHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& msg : chatHistory)
        {
            if (msg.isUser)
            {
                // User message - right-aligned style
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
                ImGui::TextWrapped("%s %s", ICON_FA_USER, msg.text.c_str());
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
                // AI response
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.85f, 1.0f, 1.0f));
                ImGui::TextWrapped("%s %s", ICON_FA_ROBOT, msg.text.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
        }

        // Show generating indicator
        if (generating)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 0.7f));
            ImGui::TextWrapped(ICON_FA_SPINNER " Thinking...");
            ImGui::PopStyleColor();
        }

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

        // Enter or button → AI Generate
        if (ImGui::Button(ICON_FA_PAPER_PLANE " Send", ImVec2(buttonWidth, ImGui::GetFrameHeightWithSpacing() * 1.5f)) ||
            submitted)
        {
            if (aiInputBuffer[0] != '\0')
            {
                chatHistory.push_back({aiInputBuffer, true});
                service.GenerateAsync(aiInputBuffer);
                aiInputBuffer[0] = '\0';
                chatScrollToBottom = true;
            }
        }

        if (generating)
        {
            ImGui::EndDisabled();
        }
    }

    static void DrawEditorScriptTab(FEditorAIService& service)
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
                                  ImVec2(availWidth, editorHeight),
                                  ImGuiInputTextFlags_AllowTabInput);

        // Buttons row
        float buttonWidth = 120.0f;
        if (ImGui::Button(ICON_FA_PLAY " Run Script", ImVec2(buttonWidth, 0)))
        {
            if (scriptInputBuffer[0] != '\0')
            {
                logHistory.push_back({fmt::format("> [script]\n{}", scriptInputBuffer), false});
                service.ExecuteDirect(scriptInputBuffer);
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

        // Poll async results - AI response goes to chat, execution logs go to logHistory
        if (aiService->HasPendingResult())
        {
            aiService->ConsumePendingResult();

            // Add AI response text to chat history
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
                DrawAIAssistantTab(*aiService);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem(ICON_FA_CODE " EditorScript"))
            {
                DrawEditorScriptTab(*aiService);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
} // namespace Editor
