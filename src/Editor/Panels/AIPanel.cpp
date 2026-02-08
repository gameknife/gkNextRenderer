#include "Editor/EditorUi.hpp"

#include "Editor/AI/EditorAIService.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/AIService.hpp"
#include "Runtime/Subsystems/VoiceInputService.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <sstream>

namespace Editor
{
    // ========== Lightweight Markdown Renderer ==========
    namespace
    {
        enum class EMdBlockType
        {
            Text,
            CodeBlock,
            Header,
            Bullet,
            Separator
        };

        struct MdBlock
        {
            EMdBlockType type;
            std::string content;
            std::string lang;
            int level = 0;
        };

        enum class EMdInlineType
        {
            Normal,
            Bold,
            Code
        };

        struct MdInlineSegment
        {
            EMdInlineType type;
            std::string text;
        };

        std::vector<MdBlock> ParseMarkdownBlocks(const std::string& text)
        {
            std::vector<MdBlock> blocks;
            std::istringstream stream(text);
            std::string line;
            bool inCodeBlock = false;
            std::string codeContent;
            std::string codeLang;
            std::string textAccum;

            auto flushText = [&]() {
                if (!textAccum.empty())
                {
                    blocks.push_back({EMdBlockType::Text, textAccum});
                    textAccum.clear();
                }
            };

            while (std::getline(stream, line))
            {
                // Remove trailing \r
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                // Code fence
                if (line.size() >= 3 && line[0] == '`' && line[1] == '`' && line[2] == '`')
                {
                    if (!inCodeBlock)
                    {
                        flushText();
                        inCodeBlock = true;
                        codeLang = line.length() > 3 ? line.substr(3) : "";
                        auto s = codeLang.find_first_not_of(" \t");
                        if (s != std::string::npos)
                        {
                            codeLang = codeLang.substr(s);
                        }
                        auto e = codeLang.find_last_not_of(" \t");
                        if (e != std::string::npos)
                        {
                            codeLang = codeLang.substr(0, e + 1);
                        }
                        codeContent.clear();
                    }
                    else
                    {
                        blocks.push_back({EMdBlockType::CodeBlock, codeContent, codeLang});
                        inCodeBlock = false;
                    }
                    continue;
                }

                if (inCodeBlock)
                {
                    if (!codeContent.empty())
                    {
                        codeContent += "\n";
                    }
                    codeContent += line;
                    continue;
                }

                // Separator
                if (line == "---" || line == "***" || line == "___")
                {
                    flushText();
                    blocks.push_back({EMdBlockType::Separator});
                    continue;
                }

                // Header
                int headerLevel = 0;
                for (size_t ci = 0; ci < line.size() && line[ci] == '#'; ++ci)
                {
                    headerLevel++;
                }
                if (headerLevel > 0 && headerLevel <= 3 && static_cast<int>(line.size()) > headerLevel &&
                    line[headerLevel] == ' ')
                {
                    flushText();
                    blocks.push_back({EMdBlockType::Header, line.substr(headerLevel + 1), "", headerLevel});
                    continue;
                }

                // Bullet (- item, * item, 1. item)
                if (line.size() >= 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ')
                {
                    flushText();
                    blocks.push_back({EMdBlockType::Bullet, line.substr(2)});
                    continue;
                }
                // Numbered list
                {
                    size_t numEnd = 0;
                    while (numEnd < line.size() && line[numEnd] >= '0' && line[numEnd] <= '9')
                    {
                        numEnd++;
                    }
                    if (numEnd > 0 && numEnd + 1 < line.size() && line[numEnd] == '.' && line[numEnd + 1] == ' ')
                    {
                        flushText();
                        blocks.push_back({EMdBlockType::Bullet, line.substr(numEnd + 2)});
                        continue;
                    }
                }

                // Blank line → flush as paragraph break
                if (line.empty())
                {
                    flushText();
                    continue;
                }

                // Regular text: accumulate
                if (!textAccum.empty())
                {
                    textAccum += " ";
                }
                textAccum += line;
            }

            flushText();

            if (inCodeBlock && !codeContent.empty())
            {
                blocks.push_back({EMdBlockType::CodeBlock, codeContent, codeLang});
            }

            return blocks;
        }

        std::vector<MdInlineSegment> ParseInlineSegments(const std::string& text)
        {
            std::vector<MdInlineSegment> segments;
            size_t i = 0;
            std::string current;

            while (i < text.size())
            {
                // Bold: **text**
                if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*')
                {
                    if (!current.empty())
                    {
                        segments.push_back({EMdInlineType::Normal, current});
                        current.clear();
                    }
                    i += 2;
                    size_t end = text.find("**", i);
                    if (end == std::string::npos)
                    {
                        current = "**";
                        continue;
                    }
                    segments.push_back({EMdInlineType::Bold, text.substr(i, end - i)});
                    i = end + 2;
                    continue;
                }

                // Inline code: `text`
                if (text[i] == '`')
                {
                    if (!current.empty())
                    {
                        segments.push_back({EMdInlineType::Normal, current});
                        current.clear();
                    }
                    i += 1;
                    size_t end = text.find('`', i);
                    if (end == std::string::npos)
                    {
                        current = "`";
                        continue;
                    }
                    segments.push_back({EMdInlineType::Code, text.substr(i, end - i)});
                    i = end + 1;
                    continue;
                }

                current += text[i];
                i++;
            }

            if (!current.empty())
            {
                segments.push_back({EMdInlineType::Normal, current});
            }

            return segments;
        }

        void DrawInlineFormatted(const std::string& text, const ImVec4& baseColor)
        {
            auto segments = ParseInlineSegments(text);
            if (segments.empty())
            {
                return;
            }

            ImGui::PushTextWrapPos(0.0f);
            bool first = true;

            for (const auto& seg : segments)
            {
                if (!first)
                {
                    ImGui::SameLine(0, 0);
                }

                switch (seg.type)
                {
                case EMdInlineType::Bold:
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    ImGui::TextUnformatted(seg.text.c_str());
                    ImGui::PopStyleColor();
                    break;
                case EMdInlineType::Code: {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.4f, 1.0f));
                    ImGui::TextUnformatted(seg.text.c_str());
                    ImGui::PopStyleColor();
                    break;
                }
                default:
                    ImGui::PushStyleColor(ImGuiCol_Text, baseColor);
                    ImGui::TextUnformatted(seg.text.c_str());
                    ImGui::PopStyleColor();
                    break;
                }

                first = false;
            }

            ImGui::PopTextWrapPos();
        }

        void DrawCodeBlock(const std::string& code, const std::string& lang)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float padding = 6.0f;
            float width = ImGui::GetContentRegionAvail().x;
            float labelHeight = lang.empty() ? 0.0f : ImGui::GetTextLineHeight() + 2.0f;
            ImVec2 textSize = ImGui::CalcTextSize(code.c_str(), nullptr, false, width - padding * 2);
            float height = textSize.y + padding * 2 + labelHeight;

            ImVec2 pos = ImGui::GetCursorScreenPos();

            // Background
            drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(25, 30, 40, 230), 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(55, 65, 85, 200), 4.0f);

            // Language label
            if (!lang.empty())
            {
                drawList->AddText(ImVec2(pos.x + padding, pos.y + 2.0f), IM_COL32(100, 120, 160, 200), lang.c_str());
            }

            // Code text
            ImGui::SetCursorScreenPos(ImVec2(pos.x + padding, pos.y + padding + labelHeight));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.88f, 0.78f, 1.0f));
            ImGui::PushTextWrapPos(pos.x + width - padding);
            ImGui::TextUnformatted(code.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            // Advance cursor past block
            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + 4.0f));
        }

        void DrawMarkdownText(const std::string& text, const ImVec4& baseColor)
        {
            auto blocks = ParseMarkdownBlocks(text);

            for (const auto& block : blocks)
            {
                switch (block.type)
                {
                case EMdBlockType::Header: {
                    float scale = block.level == 1 ? 1.3f : block.level == 2 ? 1.15f : 1.05f;
                    ImGui::SetWindowFontScale(scale);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(block.content.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::Spacing();
                    break;
                }
                case EMdBlockType::CodeBlock:
                    DrawCodeBlock(block.content, block.lang);
                    ImGui::Spacing();
                    break;
                case EMdBlockType::Bullet:
                    ImGui::Indent(12.0f);
                    DrawInlineFormatted("\xe2\x80\xa2 " + block.content, baseColor);
                    ImGui::Unindent(12.0f);
                    break;
                case EMdBlockType::Separator:
                    ImGui::Separator();
                    break;
                case EMdBlockType::Text:
                    DrawInlineFormatted(block.content, baseColor);
                    ImGui::Spacing();
                    break;
                }
            }
        }
    } // namespace

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

    static void DrawAIAssistantTab(EditorContext& ctx, FEditorAIService& service)
    {
        auto status = service.GetStatus();
        bool generating = (status == EEditorAIStatus::Generating);
        auto* voiceService = ctx.engine.GetVoiceInputService();

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
                DrawMarkdownText(msg.text, aiColor);
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
        float micButtonWidth = 42.0f;
        float inputWidth = ImGui::GetContentRegionAvail().x - buttonWidth - micButtonWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f;
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

        bool micEnabled = voiceService != nullptr && voiceService->IsEnabled() && !voiceService->IsTranscribing();
        if (!micEnabled)
        {
            ImGui::BeginDisabled();
        }

        const float inputHeight = ImGui::GetFrameHeightWithSpacing() * 1.5f;
        ImGui::Button(ICON_FA_MICROPHONE, ImVec2(micButtonWidth, inputHeight));
        if (voiceService)
        {
            if (ImGui::IsItemActivated())
            {
                if (!voiceService->BeginCapture())
                {
                    chatHistory.push_back({voiceService->GetStatusMessage(), false, true});
                    chatScrollToBottom = true;
                }
            }
            if (ImGui::IsItemDeactivated() && voiceService->IsCapturing())
            {
                voiceService->EndCaptureAndTranscribeAsync();
            }
        }

        if (ImGui::IsItemHovered())
        {
            if (!voiceService || !voiceService->IsEnabled())
            {
                ImGui::SetTooltip("%s", voiceService ? voiceService->GetStatusMessage().c_str() : "Voice input unavailable");
            }
            else if (voiceService->IsCapturing())
            {
                ImGui::SetTooltip("Release to transcribe");
            }
            else if (voiceService->IsTranscribing())
            {
                ImGui::SetTooltip("Transcribing...");
            }
            else
            {
                ImGui::SetTooltip("Hold to talk");
            }
        }

        if (!micEnabled)
        {
            ImGui::EndDisabled();
        }

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
        }

        if (voiceService && voiceService->IsEnabled())
        {
            if (voiceService->IsCapturing())
            {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.6f, 1.0f), ICON_FA_MICROPHONE " Recording...");
            }
            else if (voiceService->IsTranscribing())
            {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1.0f), ICON_FA_SPINNER " Transcribing...");
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

        if (auto* voiceService = ctx.engine.GetVoiceInputService();
            voiceService && voiceService->HasPendingResult())
        {
            auto result = voiceService->ConsumePendingResult();
            if (result.success)
            {
                SetAiInputBuffer(result.text);
            }
            else
            {
                chatHistory.push_back({result.message, false, true});
                chatScrollToBottom = true;
            }
        }

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

        ImGui::End();
    }
} // namespace Editor
