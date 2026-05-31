#include "ScadStudioInterface.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Editor/ProfessionalUI.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <spdlog/spdlog.h>

namespace ScadStudio
{
    namespace
    {
        constexpr float kTitleBarHeight = 44.0f;
        constexpr float kBottomBarHeight = 30.0f;
        constexpr float kCollapsedRailWidth = 46.0f;
        constexpr size_t kMaxSessionTitleBytes = 28;
        constexpr const char* kDefaultNewSessionPrompt =
            "生成一个现代城市，有住宅区，商业区，工厂区。住宅区和商业区普遍高楼林立，城市中有中央公园。有交通路网，有河流。";

        int64_t NowUnixSeconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }

        std::string RelativeTime(int64_t unixSeconds)
        {
            if (unixSeconds <= 0)
            {
                return "now";
            }
            const int64_t delta = std::max<int64_t>(0, NowUnixSeconds() - unixSeconds);
            if (delta < 60)
            {
                return "now";
            }
            if (delta < 3600)
            {
                return fmt::format("{}m", std::max<int64_t>(1, delta / 60));
            }
            if (delta < 86400)
            {
                return fmt::format("{}h", std::max<int64_t>(1, delta / 3600));
            }
            return fmt::format("{}d", std::max<int64_t>(1, delta / 86400));
        }

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

            if (title.size() > kMaxSessionTitleBytes)
            {
                size_t end = kMaxSessionTitleBytes;
                // Never cut inside a UTF-8 continuation sequence; JSON persistence
                // rejects invalid UTF-8 and would otherwise crash on save.
                while (end > 0 && end < title.size() &&
                       (static_cast<unsigned char>(title[end]) & 0xC0) == 0x80)
                {
                    --end;
                }
                title = title.substr(0, end) + "...";
            }
            return title.empty() ? std::string("New Model") : title;
        }

        void SetBuf(char* buf, size_t cap, const std::string& text)
        {
            const size_t n = std::min(cap - 1, text.size());
            std::memcpy(buf, text.data(), n);
            buf[n] = '\0';
        }

        struct FAssistantSegment
        {
            bool isCode = false;
            std::string language;
            std::string text;
        };

        std::string TrimCopy(std::string text)
        {
            const size_t b = text.find_first_not_of(" \t\r\n");
            if (b == std::string::npos)
            {
                return "";
            }
            const size_t e = text.find_last_not_of(" \t\r\n");
            return text.substr(b, e - b + 1);
        }

        std::string ToLower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return text;
        }

        bool ProjectEquals(const std::vector<FScadProjectFile>& a, const std::vector<FScadProjectFile>& b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (a[i].path != b[i].path || a[i].source != b[i].source)
                {
                    return false;
                }
            }
            return true;
        }

        FScadProjectFile* FindProjectFile(FScadSession& session, const std::string& path)
        {
            auto it = std::find_if(session.files.begin(), session.files.end(), [&](const FScadProjectFile& file) {
                return file.path == path;
            });
            return it == session.files.end() ? nullptr : &(*it);
        }

        const FScadProjectFile* FindProjectFile(const FScadSession& session, const std::string& path)
        {
            auto it = std::find_if(session.files.begin(), session.files.end(), [&](const FScadProjectFile& file) {
                return file.path == path;
            });
            return it == session.files.end() ? nullptr : &(*it);
        }

        std::string RootFilePath(const FScadSession& session)
        {
            if (session.files.empty())
            {
                return "";
            }
            auto it = std::find_if(session.files.begin(), session.files.end(), [](const FScadProjectFile& file) {
                return ToLower(file.path) == "main.scad";
            });
            return it == session.files.end() ? session.files.front().path : it->path;
        }

        std::string ActiveFilePath(const FScadSession& session)
        {
            if (session.files.empty())
            {
                return "";
            }
            if (!session.activeFilePath.empty() && FindProjectFile(session, session.activeFilePath) != nullptr)
            {
                return session.activeFilePath;
            }
            return RootFilePath(session);
        }

        std::string ActiveSource(const FScadSession& session)
        {
            const std::string path = ActiveFilePath(session);
            if (path.empty())
            {
                return session.currentSource;
            }
            const FScadProjectFile* file = FindProjectFile(session, path);
            return file ? file->source : session.currentSource;
        }

        std::string RootSource(const std::vector<FScadProjectFile>& files)
        {
            if (files.empty())
            {
                return "";
            }
            auto it = std::find_if(files.begin(), files.end(), [](const FScadProjectFile& file) {
                return ToLower(file.path) == "main.scad";
            });
            return it == files.end() ? files.front().source : it->source;
        }

        FScadEditScope CurrentEditScope(const FScadSession& session)
        {
            FScadEditScope scope;
            scope.activeFilePath = ActiveFilePath(session);
            if (!session.previewModuleName.empty())
            {
                scope.focusedModuleName = session.previewModuleName;
                scope.focusedModuleFilePath =
                    session.previewModuleFilePath.empty() ? scope.activeFilePath : session.previewModuleFilePath;
            }
            return scope;
        }

        FOutlineResult ValidateProjectFiles(const std::vector<FScadProjectFile>& files)
        {
            FOutlineResult result;
            if (files.empty())
            {
                result.ok = false;
                result.error = "project contains no SCAD files";
                return result;
            }
            for (const FScadProjectFile& file : files)
            {
                result = BuildScadOutline(file.source);
                if (!result.ok)
                {
                    result.error = file.path + ": " + result.error;
                    return result;
                }
            }
            result.ok = true;
            return result;
        }

        std::string FirstModuleName(const std::string& source)
        {
            FOutlineResult outline = BuildScadOutline(source);
            if (!outline.ok)
            {
                return "";
            }
            for (const FOutlineNode& node : outline.roots)
            {
                if (node.kind == "module")
                {
                    const size_t nameStart = node.label.find(' ');
                    const size_t argsStart = node.label.find('(');
                    if (nameStart != std::string::npos && argsStart != std::string::npos && argsStart > nameStart + 1)
                    {
                        return node.label.substr(nameStart + 1, argsStart - nameStart - 1);
                    }
                }
            }
            return "";
        }

        std::string ModuleNameFromOutlineLabel(const std::string& label)
        {
            constexpr const char* prefix = "module ";
            if (label.rfind(prefix, 0) != 0)
            {
                return "";
            }
            const size_t nameStart = std::strlen(prefix);
            const size_t argsStart = label.find('(', nameStart);
            if (argsStart == std::string::npos || argsStart <= nameStart)
            {
                return "";
            }
            return label.substr(nameStart, argsStart - nameStart);
        }

        bool OutlineContainsModule(const std::vector<FOutlineNode>& nodes, const std::string& moduleName)
        {
            for (const FOutlineNode& node : nodes)
            {
                if (node.kind == "module" && ModuleNameFromOutlineLabel(node.label) == moduleName)
                {
                    return true;
                }
                if (OutlineContainsModule(node.children, moduleName))
                {
                    return true;
                }
            }
            return false;
        }

        bool SourceContainsModule(const std::string& source, const std::string& moduleName)
        {
            if (source.empty() || moduleName.empty())
            {
                return false;
            }
            const FOutlineResult outline = BuildScadOutline(source);
            return outline.ok && OutlineContainsModule(outline.roots, moduleName);
        }

        std::filesystem::path SafeProjectPath(const std::filesystem::path& base, const std::string& relative, bool& ok)
        {
            ok = false;
            std::string rel = relative;
            std::replace(rel.begin(), rel.end(), '\\', '/');
            if (rel.empty() || rel.find("..") != std::string::npos || rel.find(':') != std::string::npos)
            {
                return {};
            }
            while (!rel.empty() && rel.front() == '/')
            {
                rel.erase(rel.begin());
            }
            if (rel.empty())
            {
                return {};
            }

            const std::filesystem::path root = std::filesystem::weakly_canonical(base);
            const std::filesystem::path full = std::filesystem::weakly_canonical(base / rel);
            const std::string rootText = root.string();
            const std::string fullText = full.string();
            if (fullText.rfind(rootText, 0) != 0)
            {
                return {};
            }
            ok = true;
            return full;
        }

        std::vector<FAssistantSegment> SplitAssistantText(const std::string& text)
        {
            std::vector<FAssistantSegment> segments;
            size_t cursor = 0;
            while (cursor < text.size())
            {
                const size_t fence = text.find("```", cursor);
                if (fence == std::string::npos)
                {
                    std::string plain = TrimCopy(text.substr(cursor));
                    if (!plain.empty())
                    {
                        segments.push_back(FAssistantSegment{false, "", std::move(plain)});
                    }
                    break;
                }

                std::string plain = TrimCopy(text.substr(cursor, fence - cursor));
                if (!plain.empty())
                {
                    segments.push_back(FAssistantSegment{false, "", std::move(plain)});
                }

                size_t headerEnd = text.find('\n', fence + 3);
                if (headerEnd == std::string::npos)
                {
                    std::string trailing = TrimCopy(text.substr(fence));
                    if (!trailing.empty())
                    {
                        segments.push_back(FAssistantSegment{false, "", std::move(trailing)});
                    }
                    break;
                }

                std::string language = TrimCopy(text.substr(fence + 3, headerEnd - (fence + 3)));
                const size_t blockStart = headerEnd + 1;
                const size_t blockEnd = text.find("```", blockStart);
                if (blockEnd == std::string::npos)
                {
                    std::string code = TrimCopy(text.substr(blockStart));
                    segments.push_back(FAssistantSegment{true, std::move(language), std::move(code)});
                    break;
                }

                std::string code = TrimCopy(text.substr(blockStart, blockEnd - blockStart));
                segments.push_back(FAssistantSegment{true, std::move(language), std::move(code)});
                cursor = blockEnd + 3;
            }

            return segments;
        }

        bool HasCodeSegment(const std::vector<FAssistantSegment>& segments)
        {
            return std::any_of(segments.begin(), segments.end(), [](const FAssistantSegment& segment) {
                return segment.isCode;
            });
        }

        void DrawUserBubble(const std::string& text)
        {
            const float avail = ImGui::GetContentRegionAvail().x;
            const float width = std::max(180.0f, avail * 0.78f);
            const float bubbleWidth = std::min(width, avail);
            const ImVec2 pad(12.0f, 9.0f);
            const float wrapWidth = std::max(80.0f, bubbleWidth - pad.x * 2.0f);
            const ImVec2 labelSize = ImGui::CalcTextSize("You");
            const ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapWidth);
            const float height = pad.y * 2.0f + labelSize.y + 4.0f + textSize.y;

            const float baseX = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(baseX + std::max(0.0f, avail - bubbleWidth));
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + bubbleWidth, min.y + height);
            ImGui::GetWindowDrawList()->AddRectFilled(
                min, max, ImGui::GetColorU32(ImVec4(0.16f, 0.28f, 0.42f, 0.92f)), 8.0f);

            ImGui::SetCursorScreenPos(ImVec2(min.x + pad.x, min.y + pad.y));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.88f, 1.0f, 1.0f));
            ImGui::TextUnformatted("You");
            ImGui::PopStyleColor();
            ImGui::SetCursorScreenPos(ImVec2(min.x + pad.x, min.y + pad.y + labelSize.y + 4.0f));
            ImGui::PushTextWrapPos(min.x + bubbleWidth - pad.x);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();

            ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 8.0f));
            ImGui::Dummy(ImVec2(bubbleWidth, 0.0f));
        }

        void DrawCodeBlock(const FAssistantSegment& segment, int blockIndex)
        {
            std::string language = segment.language.empty() ? "text" : segment.language;
            const int lines = CountLines(segment.text);
            const std::string label = fmt::format("{} block · {} lines###code_block_{}", language, lines, blockIndex);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.16f, 0.18f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.20f, 0.23f, 0.26f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.22f, 0.25f, 0.28f, 1.0f));
            const bool open = ImGui::TreeNodeEx(
                label.c_str(),
                ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen);
            ImGui::PopStyleColor(3);

            if (!open)
            {
                return;
            }

            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            const float height = std::clamp(lineHeight * static_cast<float>(lines + 1), 96.0f, 360.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.08f, 1.0f));
            ImGui::BeginChild(
                fmt::format("##code_body_{}", blockIndex).c_str(), ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(segment.text.c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
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
            s.scenePath = std::filesystem::absolute(s.files.empty() ? store_.LegacyScadPath(s.id) : store_.ScadPath(s.id)).string();
            if (!s.files.empty() && s.activeFilePath.empty())
            {
                s.activeFilePath = RootFilePath(s);
            }
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
        io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
    }

    void ScadStudioInterface::Init()
    {
        NextUI::Theme::ApplyProfessionalTheme();
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

        DrawTitleBar();
        DrawBottomBar();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        const float panelY = viewport->Pos.y + kTitleBarHeight;
        const float panelHeight = std::max(1.0f, viewport->Size.y - kTitleBarHeight - kBottomBarHeight);
        const float leftFullWidth = std::clamp(viewport->Size.x * 0.24f, 286.0f, 360.0f);
        const float rightFullWidth = std::clamp(viewport->Size.x * 0.34f, 380.0f, 540.0f);
        const float leftWidth = sessionsCollapsed_ ? kCollapsedRailWidth : leftFullWidth;
        const float rightWidth = chatCollapsed_ ? kCollapsedRailWidth : rightFullWidth;

        const ImVec2 leftPos(viewport->Pos.x, panelY);
        const ImVec2 leftSize(leftWidth, panelHeight);
        const ImVec2 rightPos(viewport->Pos.x + viewport->Size.x - rightWidth, panelY);
        const ImVec2 rightSize(rightWidth, panelHeight);
        DrawSessionPanel(leftPos, leftSize);
        DrawChatPanel(rightPos, rightSize);

        const float viewportX = viewport->Pos.x + leftWidth;
        const float viewportY = panelY;
        const float viewportW = std::max(1.0f, viewport->Size.x - leftWidth - rightWidth);
        const float viewportH = panelHeight;
        engine_.GetRenderer().SwapChain().UpdateOutputViewport(
            Utilities::Math::floorToInt(viewportX - viewport->Pos.x),
            Utilities::Math::floorToInt(viewportY - viewport->Pos.y),
            Utilities::Math::ceilToInt(viewportW),
            Utilities::Math::ceilToInt(viewportH));
    }

    void ScadStudioInterface::DrawTitleBar()
    {
        NextUI::Theme::FAppTitleBarConfig config{};
        config.BrandWindowId = "ScadStudioBrand";
        config.MenuWindowId = "ScadStudioMenu";
        config.RightWindowId = "ScadStudioWindowControls";
        config.AppName = "SCAD Studio";
        config.Height = kTitleBarHeight;
        config.RightContentWidth = 210.0f;
        config.DrawMenuBar = [&]() -> float
        {
            float menuRight = ImGui::GetCursorScreenPos().x;
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Model", "Ctrl+N"))
                {
                    NewSession();
                }
                if (current_ >= 0 && ImGui::MenuItem("Export Current"))
                {
                    ExportSession(sessions_[current_]);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    engine_.RequestClose();
                }
                ImGui::EndMenu();
            }
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);

            if (ImGui::BeginMenu("View"))
            {
                bool sessionsOpen = !sessionsCollapsed_;
                bool chatOpen = !chatCollapsed_;
                if (ImGui::MenuItem("Sessions", nullptr, sessionsOpen))
                {
                    sessionsCollapsed_ = !sessionsCollapsed_;
                }
                if (ImGui::MenuItem("Chat", nullptr, chatOpen))
                {
                    chatCollapsed_ = !chatCollapsed_;
                }
                ImGui::Separator();
                ImGui::MenuItem("Auto Fix", nullptr, &autoRepair_);
                ImGui::EndMenu();
            }
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            return menuRight;
        };
        config.DrawRightContent = [&]()
        {
            ImGui::SetCursorPosY(std::floor((kTitleBarHeight - ImGui::GetTextLineHeight()) * 0.5f));
            const char* status = ai_.IsGenerating() ? "Generating" : (ai_.IsConfigured() ? "Ready" : "Offline");
            const ImVec4 color = ai_.IsGenerating() ? NextUI::Theme::Color(NextUI::Theme::EColor::Warning)
                                : (ai_.IsConfigured() ? NextUI::Theme::Color(NextUI::Theme::EColor::Success)
                                                      : NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted));
            ImGui::TextColored(color, "%s", status);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ai_.ProviderName().c_str());
        };
        config.IsMaximized = engine_.IsMaximized();
        config.OnMinimize = [&]() { engine_.RequestMinimize(); };
        config.OnToggleMaximize = [&]() { engine_.ToggleMaximize(); };
        config.OnClose = [&]() { engine_.RequestClose(); };
        NextUI::Theme::DrawAppTitleBar(engine_, config);
    }

    void ScadStudioInterface::DrawBottomBar()
    {
        NextUI::Theme::FBottomBarConfig config{};
        config.WindowId = "ScadStudioBottomBar";
        config.Height = kBottomBarHeight;
        config.RightWidth = 170.0f;
        config.DrawLeftContent = [&]()
        {
            if (current_ >= 0)
            {
                const FScadSession& session = sessions_[current_];
                ImGui::TextUnformatted(session.title.c_str());
                if (!session.statusLine.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", session.statusLine.c_str());
                }
            }
            else
            {
                ImGui::TextDisabled("No active model");
            }
        };
        config.DrawRightContent = [&]()
        {
            ImGui::TextDisabled("FPS %.0f", engine_.GetFrameRate());
        };
        NextUI::Theme::DrawBottomBar(config);
    }

    FScadSession& ScadStudioInterface::NewSession()
    {
        FScadSession session;
        session.id = fmt::format("model_{:04d}", ++sessionCounter_);
        session.title = "New Model";
        session.createdAt = NowUnixSeconds();
        session.updatedAt = session.createdAt;
        sessions_.push_back(std::move(session));
        current_ = static_cast<int>(sessions_.size()) - 1;
        ai_.ResetConversation();
        SetBuf(inputBuf_, sizeof(inputBuf_), kDefaultNewSessionPrompt);
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
            ReloadSessionForScope(s, CurrentEditScope(s));
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
                ReloadSessionForScope(sessions_[current_], CurrentEditScope(sessions_[current_]));
            }
        }
    }

    void ScadStudioInterface::ArchiveSession(int index)
    {
        if (index < 0 || index >= static_cast<int>(sessions_.size()))
        {
            return;
        }

        sessions_[index].archived = true;
        sessions_[index].updatedAt = NowUnixSeconds();
        store_.SaveSession(sessions_[index]);
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
                ReloadSessionForScope(sessions_[current_], CurrentEditScope(sessions_[current_]));
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
        session.outline = BuildScadOutline(ActiveSource(session));
        session.outlineDirty = false;
    }

    void ScadStudioInterface::DrawOutline(FScadSession& session, const std::vector<FOutlineNode>& nodes)
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

            const std::string moduleName = node.kind == "module" ? ModuleNameFromOutlineLabel(node.label) : std::string();
            const std::string activePath = ActiveFilePath(session);
            const bool moduleSelected = !moduleName.empty() &&
                session.previewModuleName == moduleName &&
                session.previewModuleFilePath == activePath;

            if (!node.children.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
                if (moduleSelected)
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                const bool open = ImGui::TreeNodeEx(node.label.c_str(), flags);
                ImGui::PopStyleColor();
                if (!moduleName.empty() && ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    PreviewModule(session, moduleName, activePath);
                }
                if (open)
                {
                    DrawOutline(session, node.children);
                    ImGui::TreePop();
                }
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                if (!moduleName.empty())
                {
                    if (ImGui::Selectable(node.label.c_str(), moduleSelected))
                    {
                        PreviewModule(session, moduleName, activePath);
                    }
                }
                else
                {
                    ImGui::Bullet();
                    ImGui::SameLine();
                    ImGui::TextUnformatted(node.label.c_str());
                }
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }
    }

    void ScadStudioInterface::DrawProjectFiles(FScadSession& session)
    {
        if (session.files.empty())
        {
            return;
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Project files");
        if (ImGui::SmallButton("Render full"))
        {
            session.previewModuleName.clear();
            session.previewModuleFilePath.clear();
            WriteAndReload(session);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Preview selected"))
        {
            PreviewActiveFile(session);
        }

        const std::string activePath = ActiveFilePath(session);
        ImGui::BeginChild("##project_files", ImVec2(0.0f, 112.0f), ImGuiChildFlags_Borders);
        for (const FScadProjectFile& file : session.files)
        {
            const bool selected = (file.path == activePath);
            if (ImGui::Selectable(file.path.c_str(), selected))
            {
                session.activeFilePath = file.path;
                session.outlineDirty = true;
                session.previewModuleName.clear();
                session.previewModuleFilePath.clear();
                PersistSession(session);
            }
        }
        ImGui::EndChild();
    }

    void ScadStudioInterface::DrawSessionPanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, sessionsCollapsed_ ? ImVec2(7.0f, 8.0f) : ImVec2(12.0f, 12.0f));
        if (!ImGui::Begin("##ScadStudioSessions", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        if (sessionsCollapsed_)
        {
            if (ImGui::Button(ICON_FA_CHEVRON_RIGHT "##expand_sessions", ImVec2(30.0f, 30.0f)))
            {
                sessionsCollapsed_ = false;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Show sessions");
            }
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_PLUS "##new_collapsed", ImVec2(30.0f, 30.0f)))
            {
                NewSession();
                sessionsCollapsed_ = false;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("New model");
            }
            ImGui::End();
            return;
        }

        if (ImGui::Button(ICON_FA_CHEVRON_LEFT "##collapse_sessions", ImVec2(30.0f, 30.0f)))
        {
            sessionsCollapsed_ = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Collapse sessions");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Sessions");
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetWindowContentRegionMax().x - 100.0f));

        if (ImGui::Button(ICON_FA_PLUS " New", ImVec2(100.0f, 0.0f)))
        {
            NewSession();
        }

        ImGui::Separator();

        int deleteIndex = -1;
        int archiveIndex = -1;
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
                    sessions_[i].updatedAt = NowUnixSeconds();
                    renamingIndex_ = -1;
                    PersistSession(sessions_[i]);
                }
                if (!ImGui::IsItemActive() && !ImGui::IsItemActivated())
                {
                    // Commit on focus loss too.
                    if (ImGui::IsItemDeactivated())
                    {
                        sessions_[i].title = renameBuf_[0] ? renameBuf_ : sessions_[i].title;
                        sessions_[i].updatedAt = NowUnixSeconds();
                        renamingIndex_ = -1;
                        PersistSession(sessions_[i]);
                    }
                }
            }
            else
            {
                const float rowHeight = 52.0f;
                const float avail = ImGui::GetContentRegionAvail().x;
                if (ImGui::Selectable("##session_row", selected, 0, ImVec2(avail, rowHeight)))
                {
                    selectIndex = i;
                }
                const bool rowHovered = ImGui::IsItemHovered();
                const ImVec2 rowMin = ImGui::GetItemRectMin();
                const ImVec2 rowMax = ImGui::GetItemRectMax();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImU32 titleColor = ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_Text);
                const ImU32 timeColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                drawList->AddText(ImVec2(rowMin.x + 10.0f, rowMin.y + 8.0f), titleColor, sessions_[i].title.c_str());
                const std::string relative = RelativeTime(sessions_[i].updatedAt);
                drawList->AddText(ImVec2(rowMin.x + 10.0f, rowMin.y + 29.0f), timeColor, relative.c_str());

                ImGui::SetCursorScreenPos(ImVec2(rowMax.x - 36.0f, rowMin.y + 11.0f));
                if (ImGui::SmallButton(ICON_FA_BOX_ARCHIVE "##archive"))
                {
                    archiveIndex = i;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Archive");
                }
                ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y + 3.0f));

                if (rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
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
                    if (ImGui::MenuItem("Archive"))
                    {
                        archiveIndex = i;
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
        if (archiveIndex >= 0)
        {
            ArchiveSession(archiveIndex);
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

            if (!s.previewModuleName.empty())
            {
                ImGui::TextDisabled("Previewing module");
                ImGui::TextWrapped("%s", s.previewModuleName.c_str());
                if (ImGui::SmallButton("Render full model"))
                {
                    s.previewModuleName.clear();
                    s.previewModuleFilePath.clear();
                    WriteAndReload(s);
                    PersistSession(s);
                }
            }

            DrawProjectFiles(s);

            ImGui::Spacing();
            const std::string activePath = ActiveFilePath(s);
            if (activePath.empty())
            {
                ImGui::TextDisabled("Structure  ·  %d lines", CountLines(s.currentSource));
            }
            else
            {
                ImGui::TextDisabled("Structure  ·  %s  ·  %d lines", activePath.c_str(), CountLines(ActiveSource(s)));
            }

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
                DrawOutline(s, s.outline.roots);
            }
            ImGui::EndChild();
        }
        else
        {
            ImGui::TextWrapped("No model yet. Click \"+ New Model\" and describe what you want.");
        }

        ImGui::End();
    }

    void ScadStudioInterface::DrawChatPanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, chatCollapsed_ ? ImVec2(7.0f, 8.0f) : ImVec2(12.0f, 12.0f));
        if (!ImGui::Begin("##ScadStudioChat", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        if (chatCollapsed_)
        {
            if (ImGui::Button(ICON_FA_CHEVRON_LEFT "##expand_chat", ImVec2(30.0f, 30.0f)))
            {
                chatCollapsed_ = false;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Show chat");
            }
            ImGui::Spacing();
            ImGui::TextUnformatted(ICON_FA_COMMENTS);
            ImGui::End();
            return;
        }

        if (ImGui::Button(ICON_FA_CHEVRON_RIGHT "##collapse_chat", ImVec2(30.0f, 30.0f)))
        {
            chatCollapsed_ = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Collapse chat");
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Chat");
        ImGui::SameLine();

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

        if (current_ >= 0 && !sessions_[current_].files.empty())
        {
            FScadSession& session = sessions_[current_];
            const std::string activePath = ActiveFilePath(session);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##target_file", activePath.empty() ? "main.scad" : activePath.c_str()))
            {
                for (const FScadProjectFile& file : session.files)
                {
                    const bool selected = (file.path == activePath);
                    if (ImGui::Selectable(file.path.c_str(), selected))
                    {
                        session.activeFilePath = file.path;
                        session.outlineDirty = true;
                        session.previewModuleName.clear();
                        session.previewModuleFilePath.clear();
                        PersistSession(session);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            if (!session.previewModuleName.empty())
            {
                const std::string scopePath = session.previewModuleFilePath.empty() ? activePath : session.previewModuleFilePath;
                if (scopePath.empty())
                {
                    ImGui::TextDisabled("Default target: module %s", session.previewModuleName.c_str());
                }
                else
                {
                    ImGui::TextDisabled("Default target: %s :: %s", scopePath.c_str(), session.previewModuleName.c_str());
                }
            }
            else
            {
                ImGui::TextDisabled("Default target: selected file");
            }
            ImGui::Separator();
        }

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 4.5f;
        ImGui::BeginChild("##messages", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        if (current_ >= 0)
        {
            FScadSession& session = sessions_[current_];
            int resultIndex = 0;
            for (int turnIndex = 0; turnIndex < static_cast<int>(session.turns.size()); ++turnIndex)
            {
                const FChatTurn& turn = session.turns[turnIndex];
                ImGui::PushID(turnIndex);
                if (turn.isError)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                    ImGui::TextWrapped("Error: %s", turn.content.c_str());
                    ImGui::PopStyleColor();
                }
                else if (turn.role == "user")
                {
                    DrawUserBubble(turn.content);
                }
                else
                {
                    ++resultIndex;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.86f, 0.64f, 1.0f));
                    ImGui::Text("Model · result %d", resultIndex);
                    ImGui::PopStyleColor();

                    if (!turn.scadSource.empty() || !turn.files.empty())
                    {
                        bool isCurrent = false;
                        if (!turn.files.empty())
                        {
                            isCurrent = ProjectEquals(session.files, turn.files);
                        }
                        else if (!turn.targetFilePath.empty())
                        {
                            const FScadProjectFile* file = FindProjectFile(session, turn.targetFilePath);
                            isCurrent = file != nullptr && file->source == turn.scadSource;
                        }
                        else
                        {
                            isCurrent = (session.currentSource == turn.scadSource);
                        }
                        ImGui::SameLine();
                        if (isCurrent || ai_.IsGenerating())
                        {
                            ImGui::BeginDisabled();
                        }
                        if (ImGui::SmallButton(isCurrent ? "current render" : "restore this result"))
                        {
                            RestoreSessionToTurn(session, turnIndex);
                        }
                        if (isCurrent || ai_.IsGenerating())
                        {
                            ImGui::EndDisabled();
                        }
                    }

                    std::vector<FAssistantSegment> segments = SplitAssistantText(turn.content);
                    if (!turn.scadSource.empty() && !HasCodeSegment(segments))
                    {
                        segments.push_back(FAssistantSegment{true, "scad", turn.scadSource});
                    }

                    if (segments.empty())
                    {
                        ImGui::TextDisabled("(empty response)");
                    }
                    int codeBlockIndex = 0;
                    for (const FAssistantSegment& segment : segments)
                    {
                        if (segment.isCode)
                        {
                            DrawCodeBlock(segment, codeBlockIndex++);
                        }
                        else
                        {
                            ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);
                            ImGui::TextUnformatted(segment.text.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        ImGui::Spacing();
                    }
                }
                ImGui::PopID();
                ImGui::Spacing();
            }
        }
        if (ai_.IsGenerating())
        {
            const bool showStream = current_ >= 0 && sessions_[current_].id == pendingSessionId_;
            const std::string stream = showStream ? ai_.StreamingText() : std::string();
            if (stream.empty())
            {
                ImGui::TextDisabled("waiting for first token...");
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.58f, 0.86f, 0.64f, 1.0f));
                ImGui::TextUnformatted("Model");
                ImGui::PopStyleColor();

                std::vector<FAssistantSegment> segments = SplitAssistantText(stream);
                if (segments.empty())
                {
                    ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);
                    ImGui::TextUnformatted(stream.c_str());
                    ImGui::PopTextWrapPos();
                }
                int codeBlockIndex = 0;
                for (const FAssistantSegment& segment : segments)
                {
                    if (segment.isCode)
                    {
                        DrawCodeBlock(segment, codeBlockIndex++);
                    }
                    else
                    {
                        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x);
                        ImGui::TextUnformatted(segment.text.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::Spacing();
                }
            }
            scrollChatToBottom_ = true;
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
        const std::string instruction = inputBuf_;
        FScadSession& s = (current_ >= 0) ? sessions_[current_] : NewSession();
        if (s.turns.empty())
        {
            s.title = MakeTitle(instruction);
        }
        s.updatedAt = NowUnixSeconds();
        const FScadEditScope editScope = CurrentEditScope(s);
        const std::string targetPath = editScope.EffectiveFilePath();
        FChatTurn userTurn;
        userTurn.role = "user";
        if (editScope.HasFocusedModule())
        {
            userTurn.content = targetPath.empty()
                ? fmt::format("[module:{}]\n{}", editScope.focusedModuleName, instruction)
                : fmt::format("[{} :: module:{}]\n{}", targetPath, editScope.focusedModuleName, instruction);
        }
        else
        {
            userTurn.content = targetPath.empty() ? instruction : fmt::format("[{}]\n{}", targetPath, instruction);
        }
        userTurn.targetFilePath = targetPath;
        s.turns.push_back(std::move(userTurn));

        pendingSessionId_ = s.id;
        pendingEditScope_ = editScope;
        repairBudget_ = autoRepair_ ? kMaxRepairAttempts : 0;
        ai_.SubmitAsync(s.currentSource, s.files, editScope, instruction);
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
        s.updatedAt = NowUnixSeconds();
        scrollChatToBottom_ = true;
        const FScadEditScope requestScope = pendingEditScope_;
        const std::string targetPath = requestScope.EffectiveFilePath();

        if (!result.success)
        {
            FChatTurn errorTurn;
            errorTurn.role = "assistant";
            errorTurn.content = result.error;
            errorTurn.isError = true;
            s.turns.push_back(std::move(errorTurn));
            s.statusLine = "✗ " + result.error;
            s.statusError = true;
            PersistSession(s);
            pendingSessionId_.clear();
            pendingEditScope_ = {};
            return;
        }

        FChatTurn assistantTurn;
        assistantTurn.role = "assistant";
        assistantTurn.content = result.assistantText;
        assistantTurn.scadSource = result.scadSource;
        assistantTurn.files = result.files;
        assistantTurn.targetFilePath = targetPath;
        s.turns.push_back(std::move(assistantTurn));

        if (result.scadSource.empty() && result.files.empty())
        {
            s.statusLine = "(no SCAD code in reply)";
            s.statusError = true;
            PersistSession(s);
            pendingSessionId_.clear();
            pendingEditScope_ = {};
            return;
        }

        // Headless validation: lex+parse the proposed source before committing it.
        const FOutlineResult check = result.files.empty() ? BuildScadOutline(result.scadSource)
                                                          : ValidateProjectFiles(result.files);
        if (!check.ok && repairBudget_ > 0)
        {
            // Auto-repair: feed the parse error back for one more attempt. Keep the
            // broken source out of currentSource so the viewport keeps the last good model.
            repairBudget_ -= 1;
            s.statusLine = "⟳ auto-fixing parse error...";
            s.statusError = true;
            const std::string repairPrompt =
                "The SCAD you produced has a parse error: " + check.error +
                "\nReturn the COMPLETE corrected " + (result.files.empty() ? std::string(".scad file.")
                                                                            : std::string("scad-project block."));
            ai_.SubmitAsync(s.currentSource, s.files, requestScope, repairPrompt);
            PersistSession(s);
            return;
        }

        // Commit (even if still not ok after exhausting the repair budget — surface it).
        if (!result.files.empty())
        {
            s.files = result.files;
            s.currentSource = RootSource(s.files);
            if (FindProjectFile(s, s.activeFilePath) == nullptr)
            {
                s.activeFilePath = RootFilePath(s);
            }
        }
        else if (!s.files.empty())
        {
            FScadProjectFile* file = FindProjectFile(s, targetPath);
            if (file != nullptr)
            {
                file->source = result.scadSource;
                if (file->path == RootFilePath(s))
                {
                    s.currentSource = result.scadSource;
                }
            }
            else
            {
                s.currentSource = result.scadSource;
            }
        }
        else
        {
            s.currentSource = result.scadSource;
        }
        if (!check.ok)
        {
            s.statusLine = "✗ parse error: " + check.error;
            s.statusError = true;
            s.outlineDirty = true;
            PersistSession(s);
            pendingSessionId_.clear();
            pendingEditScope_ = {};
            return;
        }

        s.outlineDirty = true;
        ReloadSessionForScope(s, requestScope);
        PersistSession(s);
        pendingSessionId_.clear();
        pendingEditScope_ = {};
    }

    void ScadStudioInterface::RestoreSessionToTurn(FScadSession& session, int turnIndex)
    {
        if (turnIndex < 0 || turnIndex >= static_cast<int>(session.turns.size()))
        {
            return;
        }

        const FChatTurn& turn = session.turns[turnIndex];
        if (turn.scadSource.empty() && turn.files.empty())
        {
            return;
        }

        const FOutlineResult check = turn.files.empty() ? BuildScadOutline(turn.scadSource)
                                                        : ValidateProjectFiles(turn.files);
        if (!check.ok)
        {
            session.statusLine = "✗ cannot restore: " + check.error;
            session.statusError = true;
            PersistSession(session);
            return;
        }

        if (!turn.files.empty())
        {
            session.files = turn.files;
            session.currentSource = RootSource(session.files);
            if (FindProjectFile(session, session.activeFilePath) == nullptr)
            {
                session.activeFilePath = RootFilePath(session);
            }
        }
        else if (!session.files.empty())
        {
            const std::string path = turn.targetFilePath.empty() ? ActiveFilePath(session) : turn.targetFilePath;
            FScadProjectFile* file = FindProjectFile(session, path);
            if (file != nullptr)
            {
                file->source = turn.scadSource;
                if (file->path == RootFilePath(session))
                {
                    session.currentSource = turn.scadSource;
                }
                session.activeFilePath = file->path;
            }
            else
            {
                session.currentSource = turn.scadSource;
            }
        }
        else
        {
            session.currentSource = turn.scadSource;
        }
        session.outlineDirty = true;
        session.updatedAt = NowUnixSeconds();
        ReloadSessionForScope(session, CurrentEditScope(session));
        if (!session.statusError)
        {
            int resultIndex = 0;
            for (int i = 0; i <= turnIndex; ++i)
            {
                if (session.turns[i].role != "user")
                {
                    ++resultIndex;
                }
            }
            session.statusLine = fmt::format("✓ restored result {}", resultIndex);
        }
        PersistSession(session);
    }

    void ScadStudioInterface::PreviewActiveFile(FScadSession& session)
    {
        if (session.files.empty())
        {
            return;
        }

        const std::string activePath = ActiveFilePath(session);
        const FScadProjectFile* file = FindProjectFile(session, activePath);
        if (file == nullptr)
        {
            return;
        }

        const std::string moduleName = FirstModuleName(file->source);
        if (moduleName.empty())
        {
            session.statusLine = "✗ selected file has no module to preview";
            session.statusError = true;
            return;
        }

        PreviewModule(session, moduleName, activePath);
    }

    void ScadStudioInterface::PreviewModule(FScadSession& session, const std::string& moduleName, const std::string& moduleFilePath)
    {
        if (moduleName.empty())
        {
            return;
        }

        std::filesystem::path rootPath;
        if (!WriteSessionFiles(session, rootPath))
        {
            return;
        }

        std::error_code ec;
        const bool multiFile = !session.files.empty();
        const std::filesystem::path projectDir = multiFile ? store_.ProjectDir(session.id) : store_.WorkspaceDir();
        std::filesystem::create_directories(projectDir, ec);
        const std::filesystem::path previewPath = projectDir / "__preview_module.scad";
        std::ofstream out(previewPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            session.statusLine = "✗ failed to write " + previewPath.string();
            session.statusError = true;
            return;
        }

        const std::string usePath = multiFile
            ? (moduleFilePath.empty() ? RootFilePath(session) : moduleFilePath)
            : (session.id + ".scad");
        out << "$fn = 32;\n\n";
        std::string previewSource;
        if (multiFile)
        {
            const FScadProjectFile* file = FindProjectFile(session, usePath);
            previewSource = file != nullptr ? file->source : std::string();
        }
        else
        {
            previewSource = session.currentSource;
        }

        if (!previewSource.empty())
        {
            out << previewSource;
            if (previewSource.back() != '\n')
            {
                out << "\n";
            }
            out << "\n// SCAD Studio module preview\n";
        }
        else
        {
            out << "include <" << usePath << ">\n\n";
        }
        out << moduleName << "();\n";
        out.close();

        session.scenePath = std::filesystem::absolute(previewPath, ec).string();
        session.previewModuleName = moduleName;
        session.previewModuleFilePath = moduleFilePath;
        session.statusLine = "✓ previewing module " + moduleName;
        session.statusError = false;
        engine_.RequestLoadScene({.filename = session.scenePath});
    }

    bool ScadStudioInterface::WriteSessionFiles(FScadSession& session, std::filesystem::path& outRootPath)
    {
        std::error_code ec;
        std::filesystem::path path;
        if (session.files.empty())
        {
            std::filesystem::create_directories(store_.WorkspaceDir(), ec);
            path = store_.LegacyScadPath(session.id);

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                session.statusLine = "✗ failed to write " + path.string();
                session.statusError = true;
                SPDLOG_ERROR("[ScadStudio] failed to write {}", path.string());
                return false;
            }
            out << session.currentSource;
        }
        else
        {
            const std::filesystem::path projectDir = store_.ProjectDir(session.id);
            std::filesystem::create_directories(projectDir, ec);

            for (const FScadProjectFile& file : session.files)
            {
                bool ok = false;
                const std::filesystem::path filePath = SafeProjectPath(projectDir, file.path, ok);
                if (!ok)
                {
                    session.statusLine = "✗ invalid project path " + file.path;
                    session.statusError = true;
                    return false;
                }
                std::filesystem::create_directories(filePath.parent_path(), ec);
                std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    session.statusLine = "✗ failed to write " + filePath.string();
                    session.statusError = true;
                    SPDLOG_ERROR("[ScadStudio] failed to write {}", filePath.string());
                    return false;
                }
                out << file.source;
            }

            bool ok = false;
            path = SafeProjectPath(projectDir, RootFilePath(session), ok);
            if (!ok)
            {
                session.statusLine = "✗ invalid root file";
                session.statusError = true;
                return false;
            }
            session.currentSource = RootSource(session.files);
        }

        outRootPath = path;
        return true;
    }

    void ScadStudioInterface::ReloadSessionForScope(FScadSession& session, const FScadEditScope& editScope)
    {
        if (editScope.HasFocusedModule())
        {
            const std::string filePath = editScope.EffectiveFilePath();
            const bool canPreview = session.files.empty()
                ? SourceContainsModule(session.currentSource, editScope.focusedModuleName)
                : ([&]()
                {
                    const FScadProjectFile* file = FindProjectFile(session, filePath);
                    return file != nullptr && SourceContainsModule(file->source, editScope.focusedModuleName);
                })();
            if (canPreview)
            {
                PreviewModule(session, editScope.focusedModuleName, filePath);
                return;
            }
        }

        session.previewModuleName.clear();
        session.previewModuleFilePath.clear();
        WriteAndReload(session);
    }

    void ScadStudioInterface::WriteAndReload(FScadSession& session)
    {
        std::error_code ec;
        std::filesystem::path path;
        if (!WriteSessionFiles(session, path))
        {
            return;
        }

        session.scenePath = std::filesystem::absolute(path, ec).string();
        session.statusLine = "✓ rendering " + (session.files.empty() ? session.id + ".scad" : RootFilePath(session));
        session.statusError = false;
        session.outlineDirty = true;
        SPDLOG_INFO("[ScadStudio] loading generated scene {}", session.scenePath);
        engine_.RequestLoadScene({.filename = session.scenePath});
    }

    void ScadStudioInterface::ExportSession(const FScadSession& session)
    {
        if (session.currentSource.empty() && session.files.empty())
        {
            return;
        }
        // Export next to the executable with a friendly name; the workspace copy stays.
        std::error_code ec;
        if (session.files.empty())
        {
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
        else
        {
            const std::filesystem::path destDir = std::filesystem::current_path(ec) / (session.id + "_export");
            std::filesystem::create_directories(destDir, ec);
            for (const FScadProjectFile& file : session.files)
            {
                bool ok = false;
                const std::filesystem::path dest = SafeProjectPath(destDir, file.path, ok);
                if (!ok)
                {
                    SPDLOG_WARN("[ScadStudio] export skipped invalid path {}", file.path);
                    continue;
                }
                std::filesystem::create_directories(dest.parent_path(), ec);
                std::ofstream out(dest, std::ios::binary | std::ios::trunc);
                if (out)
                {
                    out << file.source;
                }
            }
            SPDLOG_INFO("[ScadStudio] exported project {}", destDir.string());
        }
    }
}
