#include "ScadStudioInterface.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <spdlog/spdlog.h>

namespace ScadStudio
{
    namespace
    {
        int CountLines(const std::string& text)
        {
            if (text.empty())
            {
                return 0;
            }
            int lines = 1;
            for (char c : text)
            {
                if (c == '\n')
                {
                    ++lines;
                }
            }
            return lines;
        }

        std::string MakeTitle(const std::string& prompt)
        {
            std::string title = prompt;
            const size_t nl = title.find('\n');
            if (nl != std::string::npos)
            {
                title = title.substr(0, nl);
            }
            if (title.size() > 28)
            {
                title = title.substr(0, 28) + "...";
            }
            return title.empty() ? std::string("New Model") : title;
        }

        void SetBuf(char* buf, size_t cap, const std::string& text)
        {
            const size_t n = std::min(cap - 1, text.size());
            std::memcpy(buf, text.data(), n);
            buf[n] = '\0';
        }
    } // namespace

    ScadStudioInterface::ScadStudioInterface(NextEngine& engine)
        : engine_(engine)
        , ai_(engine)
        , store_(std::filesystem::current_path() / "scad_studio")
    {
        // Restore persisted sessions.
        sessions_ = store_.LoadAll();
        for (FScadSession& s : sessions_)
        {
            // Re-point scenePath at the workspace .scad (rewritten lazily on first render).
            s.scenePath = std::filesystem::absolute(store_.ScadPath(s.id)).string();
            s.outlineDirty = true;
            // Keep the session counter ahead of any restored "model_NNNN" id.
            if (s.id.rfind("model_", 0) == 0)
            {
                try
                {
                    sessionCounter_ = std::max(sessionCounter_, std::stoi(s.id.substr(6)));
                }
                catch (...)
                {
                }
            }
        }
        if (!sessions_.empty())
        {
            current_ = 0;
        }
    }

    void ScadStudioInterface::Config()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = "scadstudio.ini";
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    void ScadStudioInterface::Init()
    {
        ImGuiIO& io = ImGui::GetIO();
        // The vcpkg imgui is built with the FreeType feature, so the font atlas must use
        // the FreeType builder (matches gkNextEditor); otherwise atlas build crashes.
        io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
        io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;

        const std::string fontPath =
            Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf");
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr,
                                                    io.Fonts->GetGlyphRangesChineseFull());
        if (font == nullptr)
        {
            io.Fonts->AddFontDefault();
        }
    }

    void ScadStudioInterface::BuildDockLayout(unsigned int dockId)
    {
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace |
                                              ImGuiDockNodeFlags_PassthruCentralNode |
                                              ImGuiDockNodeFlags_NoDockingInCentralNode);
        ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetMainViewport()->Size);

        ImGuiID dockMain = dockId;
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.30f, nullptr, &dockMain);

        ImGui::DockBuilderDockWindow("Sessions", dockLeft);
        ImGui::DockBuilderDockWindow("Chat", dockRight);
        ImGui::DockBuilderFinish(dockId);
    }

    void ScadStudioInterface::Render()
    {
        NextUI::UserInterface* uiSys = engine_.GetUserInterface();
        if (uiSys == nullptr)
        {
            return;
        }

        // First frame: load the active session's model, or a bundled example so the
        // viewport isn't an empty void before anything has been generated.
        if (!welcomeLoaded_)
        {
            welcomeLoaded_ = true;
            std::error_code ec;
            if (current_ >= 0 && !sessions_[current_].currentSource.empty())
            {
                WriteAndReload(sessions_[current_]);
            }
            else
            {
                const std::string example =
                    Utilities::FileHelper::GetPlatformFilePath("assets/scad/beer_cup.scad");
                if (std::filesystem::exists(example, ec))
                {
                    engine_.RequestLoadScene({.filename = example});
                }
            }
        }

        PollAI();

        // Full-viewport transparent dock host so the renderer shows through the central node.
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("##ScadStudioDockHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockId = ImGui::GetID("ScadStudioDockSpace");
        if (firstRun_)
        {
            BuildDockLayout(dockId);
            firstRun_ = false;
        }
        ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f),
                         ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode);
        ImGui::End();

        DrawSessionPanel();
        DrawChatPanel();

        // Point the 3D renderer at the dockspace central node rectangle.
        if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockId))
        {
            if (central->Size.x >= 1.0f && central->Size.y >= 1.0f)
            {
                engine_.GetRenderer().SwapChain().UpdateOutputViewport(
                    Utilities::Math::floorToInt(central->Pos.x - viewport->Pos.x),
                    Utilities::Math::floorToInt(central->Pos.y - viewport->Pos.y),
                    Utilities::Math::ceilToInt(central->Size.x),
                    Utilities::Math::ceilToInt(central->Size.y));
            }
        }
    }

    FScadSession& ScadStudioInterface::NewSession()
    {
        FScadSession session;
        session.id = fmt::format("model_{:04d}", ++sessionCounter_);
        session.title = "New Model";
        sessions_.push_back(std::move(session));
        current_ = static_cast<int>(sessions_.size()) - 1;
        ai_.ResetConversation();
        inputBuf_[0] = '\0';
        store_.SaveIndex(sessions_);
        return sessions_[current_];
    }

    void ScadStudioInterface::SelectSession(int index)
    {
        if (index == current_ || index < 0 || index >= static_cast<int>(sessions_.size()))
        {
            return;
        }
        current_ = index;
        ai_.ResetConversation();
        FScadSession& s = sessions_[index];
        if (!s.currentSource.empty())
        {
            WriteAndReload(s);
        }
    }

    void ScadStudioInterface::DeleteSession(int index)
    {
        if (index < 0 || index >= static_cast<int>(sessions_.size()))
        {
            return;
        }
        store_.DeleteSession(sessions_[index].id);
        sessions_.erase(sessions_.begin() + index);
        store_.SaveIndex(sessions_);

        if (sessions_.empty())
        {
            current_ = -1;
        }
        else
        {
            current_ = std::min(index, static_cast<int>(sessions_.size()) - 1);
            ai_.ResetConversation();
            if (!sessions_[current_].currentSource.empty())
            {
                WriteAndReload(sessions_[current_]);
            }
        }
    }

    void ScadStudioInterface::PersistSession(const FScadSession& session)
    {
        store_.SaveSession(session);
        store_.SaveIndex(sessions_);
    }

    void ScadStudioInterface::RefreshOutline(FScadSession& session)
    {
        if (!session.outlineDirty)
        {
            return;
        }
        session.outline = BuildScadOutline(session.currentSource);
        session.outlineDirty = false;
    }

    void ScadStudioInterface::DrawOutline(const std::vector<FOutlineNode>& nodes)
    {
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const FOutlineNode& node = nodes[i];
            ImGui::PushID(static_cast<int>(i));

            ImVec4 colour(0.85f, 0.85f, 0.85f, 1.0f);
            if (node.kind == "module")
            {
                colour = ImVec4(0.6f, 0.85f, 1.0f, 1.0f);
            }
            else if (node.kind == "assign")
            {
                colour = ImVec4(0.8f, 0.7f, 0.5f, 1.0f);
            }
            else if (node.kind == "function")
            {
                colour = ImVec4(0.7f, 0.6f, 0.9f, 1.0f);
            }

            if (!node.children.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                const bool open = ImGui::TreeNodeEx(node.label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
                ImGui::PopStyleColor();
                if (open)
                {
                    DrawOutline(node.children);
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextColored(colour, "%s", node.label.c_str());
            }
            ImGui::PopID();
        }
    }

    void ScadStudioInterface::DrawSessionPanel()
    {
        if (!ImGui::Begin("Sessions"))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("+ New Model", ImVec2(-1.0f, 0.0f)))
        {
            NewSession();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Sessions");

        int deleteIndex = -1;
        int selectIndex = -1;
        for (int i = 0; i < static_cast<int>(sessions_.size()); ++i)
        {
            ImGui::PushID(i);
            const bool selected = (i == current_);

            if (renamingIndex_ == i)
            {
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##rename", renameBuf_, sizeof(renameBuf_),
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                {
                    sessions_[i].title = renameBuf_[0] ? renameBuf_ : sessions_[i].title;
                    renamingIndex_ = -1;
                    PersistSession(sessions_[i]);
                }
                if (!ImGui::IsItemActive() && !ImGui::IsItemActivated())
                {
                    // Commit on focus loss too.
                    if (ImGui::IsItemDeactivated())
                    {
                        sessions_[i].title = renameBuf_[0] ? renameBuf_ : sessions_[i].title;
                        renamingIndex_ = -1;
                        PersistSession(sessions_[i]);
                    }
                }
            }
            else
            {
                if (ImGui::Selectable(sessions_[i].title.c_str(), selected))
                {
                    selectIndex = i;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    renamingIndex_ = i;
                    SetBuf(renameBuf_, sizeof(renameBuf_), sessions_[i].title);
                }
                if (ImGui::BeginPopupContextItem("##sessionctx"))
                {
                    if (ImGui::MenuItem("Rename"))
                    {
                        renamingIndex_ = i;
                        SetBuf(renameBuf_, sizeof(renameBuf_), sessions_[i].title);
                    }
                    if (ImGui::MenuItem("Delete"))
                    {
                        deleteIndex = i;
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::PopID();
        }

        if (selectIndex >= 0)
        {
            SelectSession(selectIndex);
        }
        if (deleteIndex >= 0)
        {
            DeleteSession(deleteIndex);
        }

        ImGui::Separator();

        if (current_ >= 0)
        {
            FScadSession& s = sessions_[current_];

            // Status line (coloured).
            if (!s.statusLine.empty())
            {
                const ImVec4 col = s.statusError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                                                 : ImVec4(0.5f, 0.85f, 0.55f, 1.0f);
                ImGui::TextColored(col, "%s", s.statusLine.c_str());
            }

            if (ImGui::Button("Export .scad"))
            {
                ExportSession(s);
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Structure  ·  %d lines", CountLines(s.currentSource));

            RefreshOutline(s);
            ImGui::BeginChild("##outline", ImVec2(0.0f, 260.0f), ImGuiChildFlags_Borders);
            if (!s.outline.ok)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
                ImGui::TextWrapped("parse error: %s", s.outline.error.c_str());
                ImGui::PopStyleColor();
            }
            else if (s.outline.roots.empty())
            {
                ImGui::TextDisabled("(empty)");
            }
            else
            {
                DrawOutline(s.outline.roots);
            }
            ImGui::EndChild();
        }
        else
        {
            ImGui::TextWrapped("No model yet. Click \"+ New Model\" and describe what you want.");
        }

        ImGui::End();
    }

    void ScadStudioInterface::DrawChatPanel()
    {
        if (!ImGui::Begin("Chat"))
        {
            ImGui::End();
            return;
        }

        if (ai_.IsGenerating())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Generating...");
        }
        else if (ai_.IsConfigured())
        {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Ready");
            ImGui::SameLine();
            ImGui::TextDisabled("· %s", ai_.ProviderName().c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Not configured (assets/configs/ai_config.json)");
        }
        const bool controlsDisabled = ai_.IsGenerating();
        if (controlsDisabled)
        {
            ImGui::BeginDisabled();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(115.0f);
        if (ImGui::BeginCombo("##provider", ai_.ProviderName().c_str()))
        {
            const NextAI::EAIProviderType currentType = ai_.ProviderType();
            for (const auto& [type, name] : ai_.Providers())
            {
                const bool configured = ai_.IsProviderConfigured(type);
                const bool selected = (type == currentType);
                if (!configured)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Selectable(name.c_str(), selected))
                {
                    ai_.SwitchProvider(type);
                }
                if (!configured)
                {
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::SetTooltip("Not configured");
                    }
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const std::vector<std::string> models = ai_.CurrentProviderModels();
        std::string currentModel = ai_.CurrentModel();
        if (currentModel.empty())
        {
            currentModel = "(default)";
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170.0f);
        if (models.empty())
        {
            ImGui::BeginDisabled();
            if (ImGui::BeginCombo("##model", currentModel.c_str()))
            {
                ImGui::EndCombo();
            }
            ImGui::EndDisabled();
        }
        else if (ImGui::BeginCombo("##model", currentModel.c_str()))
        {
            for (const std::string& model : models)
            {
                const bool selected = (model == currentModel);
                if (ImGui::Selectable(model.c_str(), selected))
                {
                    ai_.SetCurrentModel(model);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (controlsDisabled)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::Checkbox("auto-fix", &autoRepair_);
        ImGui::Separator();

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 4.5f;
        ImGui::BeginChild("##messages", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (current_ >= 0)
        {
            for (const FChatTurn& turn : sessions_[current_].turns)
            {
                if (turn.isError)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    ImGui::TextWrapped("Error: %s", turn.content.c_str());
                    ImGui::PopStyleColor();
                }
                else if (turn.role == "user")
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
                    ImGui::TextUnformatted("You");
                    ImGui::PopStyleColor();
                    ImGui::TextWrapped("%s", turn.content.c_str());
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                    ImGui::TextUnformatted("Model");
                    ImGui::PopStyleColor();
                    ImGui::TextWrapped("%s", turn.content.c_str());
                }
                ImGui::Spacing();
            }
        }
        if (ai_.IsGenerating())
        {
            ImGui::TextDisabled("thinking...");
        }
        if (scrollChatToBottom_)
        {
            ImGui::SetScrollHereY(1.0f);
            scrollChatToBottom_ = false;
        }
        ImGui::EndChild();

        ImGui::Separator();

        const bool blocked = ai_.IsGenerating() || !ai_.IsConfigured();
        if (blocked)
        {
            ImGui::BeginDisabled();
        }
        const bool submitted = ImGui::InputTextMultiline(
            "##chatinput", inputBuf_, sizeof(inputBuf_), ImVec2(-1.0f, ImGui::GetTextLineHeight() * 2.5f),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
        const bool send = ImGui::Button("Send", ImVec2(-1.0f, 0.0f));
        if (blocked)
        {
            ImGui::EndDisabled();
        }

        if ((submitted || send) && inputBuf_[0] != '\0' && !blocked)
        {
            SubmitCurrentInput();
        }

        ImGui::End();
    }

    void ScadStudioInterface::SubmitCurrentInput()
    {
        FScadSession& s = (current_ >= 0) ? sessions_[current_] : NewSession();

        const std::string instruction = inputBuf_;
        if (s.turns.empty())
        {
            s.title = MakeTitle(instruction);
        }
        s.turns.push_back(FChatTurn{"user", instruction, "", false});

        pendingSessionId_ = s.id;
        repairBudget_ = autoRepair_ ? kMaxRepairAttempts : 0;
        ai_.SubmitAsync(s.currentSource, instruction);
        PersistSession(s);

        inputBuf_[0] = '\0';
        scrollChatToBottom_ = true;
    }

    void ScadStudioInterface::PollAI()
    {
        if (!ai_.HasPendingResult())
        {
            return;
        }

        FScadGenResult result = ai_.TakePendingResult();

        // Route the result to the session that owns the in-flight request, not whatever
        // happens to be selected now.
        FScadSession* target = nullptr;
        for (FScadSession& s : sessions_)
        {
            if (s.id == pendingSessionId_)
            {
                target = &s;
                break;
            }
        }
        if (target == nullptr)
        {
            return; // session was deleted mid-flight; drop the result
        }
        FScadSession& s = *target;
        scrollChatToBottom_ = true;

        if (!result.success)
        {
            s.turns.push_back(FChatTurn{"assistant", result.error, "", true});
            s.statusLine = "✗ " + result.error;
            s.statusError = true;
            PersistSession(s);
            return;
        }

        s.turns.push_back(FChatTurn{"assistant", result.assistantText, result.scadSource, false});

        if (result.scadSource.empty())
        {
            s.statusLine = "(no SCAD code in reply)";
            s.statusError = true;
            PersistSession(s);
            return;
        }

        // Headless validation: lex+parse the proposed source before committing it.
        const FOutlineResult check = BuildScadOutline(result.scadSource);
        if (!check.ok && repairBudget_ > 0)
        {
            // Auto-repair: feed the parse error back for one more attempt. Keep the
            // broken source out of currentSource so the viewport keeps the last good model.
            repairBudget_ -= 1;
            s.statusLine = "⟳ auto-fixing parse error...";
            s.statusError = true;
            const std::string repairPrompt =
                "The SCAD you produced has a parse error: " + check.error +
                "\nReturn the COMPLETE corrected .scad file.";
            ai_.SubmitAsync(s.currentSource, repairPrompt);
            PersistSession(s);
            return;
        }

        // Commit (even if still not ok after exhausting the repair budget — surface it).
        s.currentSource = result.scadSource;
        s.outlineDirty = true;
        if (!check.ok)
        {
            s.statusLine = "✗ parse error: " + check.error;
            s.statusError = true;
            PersistSession(s);
            return;
        }

        WriteAndReload(s);
        PersistSession(s);
    }

    void ScadStudioInterface::WriteAndReload(FScadSession& session)
    {
        std::error_code ec;
        std::filesystem::create_directories(store_.WorkspaceDir(), ec);
        const std::filesystem::path path = store_.ScadPath(session.id);

        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                session.statusLine = "✗ failed to write " + path.string();
                session.statusError = true;
                SPDLOG_ERROR("[ScadStudio] failed to write {}", path.string());
                return;
            }
            out << session.currentSource;
        }

        session.scenePath = std::filesystem::absolute(path, ec).string();
        session.statusLine = "✓ rendering " + session.id + ".scad";
        session.statusError = false;
        session.outlineDirty = true;
        SPDLOG_INFO("[ScadStudio] loading generated scene {}", session.scenePath);
        engine_.RequestLoadScene({.filename = session.scenePath});
    }

    void ScadStudioInterface::ExportSession(const FScadSession& session)
    {
        if (session.currentSource.empty())
        {
            return;
        }
        // Export next to the executable with a friendly name; the workspace copy stays.
        std::error_code ec;
        const std::filesystem::path dest =
            std::filesystem::current_path(ec) / (session.id + "_export.scad");
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (out)
        {
            out << session.currentSource;
            SPDLOG_INFO("[ScadStudio] exported {}", dest.string());
        }
        else
        {
            SPDLOG_WARN("[ScadStudio] export failed: {}", dest.string());
        }
    }
}
