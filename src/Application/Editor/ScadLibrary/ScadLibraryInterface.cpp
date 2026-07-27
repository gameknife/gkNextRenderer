#define GLM_ENABLE_EXPERIMENTAL
#include "ScadLibraryInterface.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <limits>
#include <regex>
#include <spdlog/spdlog.h>
#include <sstream>

namespace ScadLibrary
{
    namespace
    {
        constexpr float kTitleBarHeight = 64.0f;
        constexpr float kBottomBarHeight = 30.0f;
        constexpr float kCollapsedRailWidth = 46.0f;
        constexpr int kBenchGridColumns = 6;
        constexpr float kBenchGridStep = 14.0f;

        std::filesystem::path WorkspaceDir() { return std::filesystem::current_path() / "scad_library"; }

        // Authoring tools must prefer the repository source tree even when the
        // executable is launched with out/build/<preset>/bin as its cwd. A
        // packaged build has no AGENTS.md marker and falls back to the normal
        // platform asset resolver.
        std::filesystem::path AuthoringPath(const std::filesystem::path& relativePath)
        {
            if (relativePath.is_absolute())
            {
                return relativePath;
            }
            std::filesystem::path cursor = std::filesystem::current_path();
            std::error_code ec;
            while (!cursor.empty())
            {
                if (std::filesystem::exists(cursor / "AGENTS.md", ec) &&
                    std::filesystem::exists(cursor / relativePath, ec))
                {
                    return std::filesystem::absolute(cursor / relativePath, ec);
                }
                const std::filesystem::path parent = cursor.parent_path();
                if (parent == cursor)
                {
                    break;
                }
                cursor = parent;
            }
            return Utilities::FileHelper::GetPlatformFilePath(relativePath.string().c_str());
        }

        // The category token reads better with a friendly label where we have one.
        const char* CategoryLabel(const std::string& category)
        {
            if (category == "bldg")
                return "建筑";
            if (category == "block")
                return "街区";
            if (category == "furn")
                return "家具";
            if (category == "prop")
                return "道具";
            if (category == "nature")
                return "植被地景";
            if (category == "veh")
                return "载具";
            if (category == "boat")
                return "船只";
            if (category == "wall")
                return "墙体";
            if (category == "part")
                return "构件";
            if (category == "ground")
                return "地面";
            if (category == "road")
                return "道路";
            if (category == "head")
                return "头部";
            if (category == "hair")
                return "发型";
            if (category == "hat")
                return "帽饰";
            if (category == "torso")
                return "躯干";
            if (category == "arm")
                return "手臂";
            if (category == "leg")
                return "腿部";
            if (category == "acc")
                return "配饰";
            if (category == "char")
                return "角色";
            if (category == "misc")
                return "其他";
            return category.c_str();
        }

        bool PassesFilter(const FKitModuleInfo& moduleInfo, const char* filter)
        {
            if (filter[0] == '\0')
            {
                return true;
            }
            return moduleInfo.name.find(filter) != std::string::npos ||
                moduleInfo.category.find(filter) != std::string::npos;
        }

        bool IsPathWithin(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            std::error_code ec;
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                return false;
            }
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
            if (ec)
            {
                return false;
            }
            const std::filesystem::path relative = canonicalPath.lexically_relative(canonicalRoot);
            return !relative.empty() && *relative.begin() != "..";
        }

        std::string ReadAssemblyTextFile(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return {};
            }
            return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }

        std::vector<std::string> FindKitDependencies(const std::string& source)
        {
            static const std::regex useRegex(R"((?:use|include)\s*<([^>]*kit_[^>]*)>)", std::regex_constants::icase);
            std::vector<std::string> dependencies;
            for (std::sregex_iterator it(source.begin(), source.end(), useRegex), end; it != end; ++it)
            {
                const std::string kitName = std::filesystem::path((*it)[1].str()).stem().string();
                if (std::find(dependencies.begin(), dependencies.end(), kitName) == dependencies.end())
                {
                    dependencies.push_back(kitName);
                }
            }
            return dependencies;
        }
    } // namespace

    ScadLibraryInterface::ScadLibraryInterface(NextEngine& engine) : engine_(engine)
    {
        RescanKits();
        RescanAssemblies();
    }

    void ScadLibraryInterface::Config()
    {
        ImGuiIO& io = ImGui::GetIO();
        imguiIniPath_ = Utilities::FileHelper::GetPlatformFilePath("scadlibrary.ini");
        io.IniFilename = imguiIniPath_.c_str();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
    }

    void ScadLibraryInterface::Init()
    {
        NextUI::Theme::ApplyProfessionalTheme();
        ImGuiIO& io = ImGui::GetIO();
        // The vcpkg imgui is built with the FreeType feature, so the font atlas must
        // use the FreeType builder (matches ScadStudio/gkNextEditor).
        io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
        io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting;

        const std::string fontPath = Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf");
        ImFont* font =
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
        if (font == nullptr)
        {
            io.Fonts->AddFontDefault();
        }
    }

    void ScadLibraryInterface::SetWorkspaceMode(EWorkspaceMode mode)
    {
        if (workspaceMode_ == mode)
        {
            return;
        }
        workspaceMode_ = mode;
        benchCollapsed_ = false;

        if (mode == EWorkspaceMode::SceneAssembly)
        {
            rigPreview_.SetActive(false);
            if (!bench_.empty())
            {
                ReloadBench();
            }
            else if (selectedKit_ >= 0 && !selectedModule_.empty())
            {
                PreviewModule(selectedKit_, selectedModule_);
            }
        }
        else if (mode == EWorkspaceMode::CharacterDesigner)
        {
            designerDirty_ = true;
        }
        else
        {
            workbenchReloadRequested_ = true;
        }
    }

    void ScadLibraryInterface::Render()
    {
        // First frame: open a real kit-based scene when available.
        if (!welcomeLoaded_)
        {
            welcomeLoaded_ = true;
            const auto preferred =
                std::find_if(assemblies_.begin(), assemblies_.end(), [](const FSceneAssemblyInfo& info)
                             { return info.relativePath == "assets/scad/scenes/scene_assembly_example.scad"; });
            if (preferred != assemblies_.end())
            {
                OpenAssembly(preferred->relativePath);
            }
            else if (!assemblies_.empty())
            {
                OpenAssembly(assemblies_[0].relativePath);
            }
        }

        DrawTitleBar();
        DrawBottomBar();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        const float panelY = viewport->Pos.y + kTitleBarHeight;
        const float panelHeight = std::max(1.0f, viewport->Size.y - kTitleBarHeight - kBottomBarHeight);
        const bool composeMode = workspaceMode_ == EWorkspaceMode::SceneAssembly;
        const bool workbenchMode = workspaceMode_ == EWorkspaceMode::CharacterWorkbench;
        const float leftFullWidth = workbenchMode ? std::clamp(viewport->Size.x * 0.16f, 230.0f, 290.0f)
                                                  : std::clamp(viewport->Size.x * 0.24f, 300.0f, 380.0f);
        const float rightFullWidth = workspaceMode_ == EWorkspaceMode::CharacterWorkbench
            ? std::clamp(viewport->Size.x * 0.28f, 420.0f, 520.0f)
            : (workspaceMode_ == EWorkspaceMode::CharacterDesigner
                   ? std::clamp(viewport->Size.x * 0.31f, 400.0f, 540.0f)
                   : std::clamp(viewport->Size.x * 0.28f, 340.0f, 460.0f));
        const float leftWidth = composeMode ? (browserCollapsed_ ? kCollapsedRailWidth : leftFullWidth)
                                            : (workbenchMode ? leftFullWidth : 0.0f);
        const float rightWidth = benchCollapsed_ ? kCollapsedRailWidth : rightFullWidth;

        if (composeMode)
        {
            DrawBrowserPanel(ImVec2(viewport->Pos.x, panelY), ImVec2(leftWidth, panelHeight));
        }
        else if (workbenchMode && workbenchEverLoaded_ && rigPreview_.HasRig())
        {
            DrawBoneHierarchyPanel(ImVec2(viewport->Pos.x, panelY), ImVec2(leftWidth, panelHeight));
        }
        DrawModePanel(ImVec2(viewport->Pos.x + viewport->Size.x - rightWidth, panelY), ImVec2(rightWidth, panelHeight));
        const bool showTimeline = workspaceMode_ == EWorkspaceMode::CharacterWorkbench && workbenchEditorTab_ == 0 &&
            workbenchEverLoaded_ && rigPreview_.HasRig();
        const float timelineHeight = showTimeline ? std::clamp(viewport->Size.y * 0.38f, 320.0f, 390.0f) : 0.0f;
        if (showTimeline)
        {
            DrawAnimationTimelinePanel(
                ImVec2(viewport->Pos.x + leftWidth, panelY + panelHeight - timelineHeight),
                ImVec2(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth), timelineHeight));
            DrawViewportToolbar(ImVec2(viewport->Pos.x + leftWidth, panelY));
            DrawBoneGizmo(ImVec2(viewport->Pos.x + leftWidth, panelY),
                          ImVec2(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth),
                                 std::max(1.0f, panelHeight - timelineHeight)));
        }

        // Deferred bench reload: wait until the drag/edit is released so the scene
        // is not rebuilt on every mouse-move.
        if (composeMode && benchDirty_ && autoReload_ && !ImGui::IsAnyItemActive())
        {
            ReloadBench();
        }
        if (workspaceMode_ == EWorkspaceMode::CharacterWorkbench && workbenchReloadRequested_ &&
            !ImGui::IsAnyItemActive())
        {
            ReloadWorkbench();
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterWorkbench && workbenchEquipmentRebuildRequested_ &&
                 !ImGui::IsAnyItemActive())
        {
            ReloadWorkbenchStage();
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner && designerDirty_ && !ImGui::IsAnyItemActive())
        {
            ReloadDesigner();
        }

        engine_.GetRenderer().SwapChain().UpdateOutputViewport(
            Utilities::Math::floorToInt(leftWidth), Utilities::Math::floorToInt(kTitleBarHeight),
            Utilities::Math::ceilToInt(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth)),
            Utilities::Math::ceilToInt(std::max(1.0f, panelHeight - timelineHeight)));
    }

    void ScadLibraryInterface::DrawTitleBar()
    {
        NextUI::Theme::FAppTitleBarConfig config{};
        config.BrandWindowId = "ScadLibraryBrand";
        config.MenuWindowId = "ScadLibraryMenu";
        config.RightWindowId = "ScadLibraryWindowControls";
        config.AppName = "SCAD Library";
        config.Height = kTitleBarHeight;
        config.RightContentWidth = 210.0f;
        config.DrawMenuBar = [&]() -> float
        {
            float menuRight = ImGui::GetCursorScreenPos().x;
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("重新扫描场景与 Kit", "F5"))
                {
                    RescanKits();
                    RescanAssemblies();
                }
                if (ImGui::MenuItem("保存场景", "Ctrl+S", false, !openedAssemblyPath_.empty()))
                {
                    SaveAssembly(false);
                }
                if (ImGui::MenuItem("导出新场景", nullptr, false, !bench_.empty()))
                {
                    ExportBench();
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
                if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
                {
                    bool browserOpen = !browserCollapsed_;
                    if (ImGui::MenuItem("场景与 Kit 浏览器", nullptr, browserOpen))
                    {
                        browserCollapsed_ = !browserCollapsed_;
                    }
                }
                bool inspectorOpen = !benchCollapsed_;
                if (ImGui::MenuItem("Inspector", nullptr, inspectorOpen))
                {
                    benchCollapsed_ = !benchCollapsed_;
                }
                ImGui::EndMenu();
            }
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);

            return menuRight;
        };
        config.DrawRightContent = [&]()
        {
            ImGui::SetCursorPosY(std::floor((kTitleBarHeight - ImGui::GetTextLineHeight()) * 0.5f));
            size_t moduleCount = 0;
            for (const FKitInfo& kit : kits_)
            {
                moduleCount += kit.modules.size();
            }
            ImGui::TextDisabled("%zu kits / %zu modules", kits_.size(), moduleCount);
        };
        config.IsMaximized = engine_.IsMaximized();
        config.OnMinimize = [&]() { engine_.RequestMinimize(); };
        config.OnToggleMaximize = [&]() { engine_.ToggleMaximize(); };
        config.OnClose = [&]() { engine_.RequestClose(); };
        NextUI::Theme::DrawAppTitleBar(engine_, config);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 tabsSize(552.0f, 52.0f);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + (viewport->Size.x - tabsSize.x) * 0.5f, viewport->Pos.y + 6.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(tabsSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        const ImGuiWindowFlags tabsFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
        if (ImGui::Begin("##ScadLibraryWorkspaceTabs", nullptr, tabsFlags))
        {
            const auto modeButton = [&](const char* label, const char* shortcut, EWorkspaceMode mode)
            {
                const bool active = workspaceMode_ == mode;
                if (active)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.34f));
                    ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover));
                }
                if (ImGui::Button(label, ImVec2(180.0f, 48.0f)))
                {
                    SetWorkspaceMode(mode);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", shortcut);
                }
                if (active)
                {
                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f),
                        ImGui::GetColorU32(NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover)), 2.0f);
                    ImGui::PopStyleColor(2);
                }
            };
            modeButton(ICON_FA_PERSON "  角色动作与装备", "Ctrl+3", EWorkspaceMode::CharacterWorkbench);
            ImGui::SameLine();
            modeButton(ICON_FA_CUBES_STACKED "  角色合成", "Ctrl+2", EWorkspaceMode::CharacterDesigner);
            ImGui::SameLine();
            modeButton(ICON_FA_CITY "  场景组装", "Ctrl+1", EWorkspaceMode::SceneAssembly);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ScadLibraryInterface::DrawBottomBar()
    {
        NextUI::Theme::FBottomBarConfig config{};
        config.WindowId = "ScadLibraryBottomBar";
        config.Height = kBottomBarHeight;
        config.RightWidth = 170.0f;
        config.DrawLeftContent = [&]()
        {
            if (!statusLine_.empty())
            {
                const ImVec4 color = statusError_ ? NextUI::Theme::Color(NextUI::Theme::EColor::Danger)
                                                  : NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted);
                ImGui::TextColored(color, "%s", statusLine_.c_str());
            }
            else
            {
                if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
                {
                    ImGui::TextDisabled("打开 kit 场景，或从零件库添加对象进行组装");
                }
                else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
                {
                    ImGui::TextDisabled("选择部件与颜色，生成可复用角色");
                }
                else
                {
                    ImGui::TextDisabled("编辑动作关键帧与骨架装备");
                }
            }
        };
        config.DrawRightContent = [&]() { ImGui::TextDisabled("FPS %.0f", engine_.GetFrameRate()); };
        NextUI::Theme::DrawBottomBar(config);
    }

    void ScadLibraryInterface::DrawBrowserPanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, browserCollapsed_ ? ImVec2(7.0f, 8.0f) : ImVec2(12.0f, 12.0f));
        if (!ImGui::Begin("##ScadLibraryBrowser", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        if (browserCollapsed_)
        {
            if (ImGui::Button(ICON_FA_CHEVRON_RIGHT "##expand_browser", ImVec2(30.0f, 30.0f)))
            {
                browserCollapsed_ = false;
            }
            ImGui::End();
            return;
        }

        if (ImGui::Button(ICON_FA_CHEVRON_LEFT "##collapse_browser", ImVec2(30.0f, 30.0f)))
        {
            browserCollapsed_ = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("场景与 Kit");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "##rescan", ImVec2(30.0f, 30.0f)))
        {
            RescanKits();
            RescanAssemblies();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("重新扫描 assets/scad 下的组装场景与 Kit");
        }

        ImGui::TextDisabled("组装场景  ·  %zu", assemblies_.size());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##assembly_filter", "搜索场景路径或 Kit…", assemblyFilterBuf_,
                                 sizeof(assemblyFilterBuf_));
        ImGui::BeginChild("##assembly_files", ImVec2(0.0f, 205.0f), ImGuiChildFlags_Borders);
        for (int index = 0; index < static_cast<int>(assemblies_.size()); ++index)
        {
            const FSceneAssemblyInfo& assembly = assemblies_[index];
            const std::string dependencies = fmt::format("{}", fmt::join(assembly.kitDependencies, ", "));
            if (assemblyFilterBuf_[0] != '\0' && assembly.relativePath.find(assemblyFilterBuf_) == std::string::npos &&
                dependencies.find(assemblyFilterBuf_) == std::string::npos)
            {
                continue;
            }
            const char* tag = assembly.generated ? "[生成] " : "";
            if (ImGui::Selectable(fmt::format("{}{}##assembly_{}", tag, assembly.relativePath, index).c_str(),
                                  index == selectedAssembly_))
            {
                selectedAssembly_ = index;
                OpenAssembly(assembly.relativePath);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Kit: %s%s", dependencies.c_str(),
                                  assembly.generated ? "\n由规格或工具生成，手改可能被覆盖" : "");
            }
        }
        if (assemblies_.empty())
        {
            ImGui::TextDisabled("未找到引用 kit_*.scad 的场景");
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextDisabled("Kit 零件库");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "搜索模块…", filterBuf_, sizeof(filterBuf_));
        ImGui::Spacing();

        ImGui::BeginChild("##kit_tree", ImVec2(0, 0), ImGuiChildFlags_None);
        for (size_t k = 0; k < kits_.size(); ++k)
        {
            const FKitInfo& kit = kits_[k];
            if (!ImGui::CollapsingHeader(fmt::format("{} ({})##kit{}", kit.name, kit.modules.size(), k).c_str(),
                                         k == 0 ? ImGuiTreeNodeFlags_DefaultOpen : 0))
            {
                continue;
            }
            std::string currentCategory;
            bool categoryOpen = false;
            for (const FKitModuleInfo& moduleInfo : kit.modules)
            {
                if (!PassesFilter(moduleInfo, filterBuf_))
                {
                    continue;
                }
                if (moduleInfo.category != currentCategory)
                {
                    if (categoryOpen)
                    {
                        ImGui::TreePop();
                    }
                    currentCategory = moduleInfo.category;
                    categoryOpen = ImGui::TreeNodeEx(
                        fmt::format("{}##cat_{}_{}", CategoryLabel(currentCategory), k, currentCategory).c_str(),
                        filterBuf_[0] != '\0' ? ImGuiTreeNodeFlags_DefaultOpen : 0);
                }
                if (!categoryOpen)
                {
                    continue;
                }
                const bool isSelected = selectedKit_ == static_cast<int>(k) && selectedModule_ == moduleInfo.name;
                if (ImGui::Selectable(fmt::format("{}##sel_{}_{}", moduleInfo.name, k, moduleInfo.name).c_str(),
                                      isSelected, 0, ImVec2(ImGui::GetContentRegionAvail().x - 34.0f, 0.0f)))
                {
                    PreviewModule(static_cast<int>(k), moduleInfo.name);
                }
                if (ImGui::IsItemHovered())
                {
                    if (moduleInfo.hasMetrics)
                    {
                        ImGui::SetTooltip("%s(%s)\n脚印 %.1f x %.1f  高 %.1f  三角形 %d", moduleInfo.name.c_str(),
                                          moduleInfo.params.c_str(), moduleInfo.footprintX, moduleInfo.footprintY,
                                          moduleInfo.height, moduleInfo.triangles);
                    }
                    else if (!moduleInfo.params.empty())
                    {
                        ImGui::SetTooltip("%s(%s)", moduleInfo.name.c_str(), moduleInfo.params.c_str());
                    }
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24.0f);
                if (ImGui::SmallButton(fmt::format(ICON_FA_PLUS "##add_{}_{}", k, moduleInfo.name).c_str()))
                {
                    AddToBench(static_cast<int>(k), moduleInfo.name);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("加入场景组装");
                }
            }
            if (categoryOpen)
            {
                ImGui::TreePop();
            }
        }
        if (kits_.empty())
        {
            ImGui::TextDisabled("assets/scad/lib 下没有 kit_*.scad");
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void ScadLibraryInterface::DrawBoneHierarchyPanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.98f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
        if (!ImGui::Begin("##ScadLibraryBoneHierarchy", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        ImGui::TextUnformatted(ICON_FA_BONE "  骨骼层级");
        ImGui::Separator();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##bone_filter", ICON_FA_MAGNIFYING_GLASS " 搜索骨骼/节点", boneFilterBuf_,
                                 sizeof(boneFilterBuf_));

        if (workbench_.Clips().empty() || rigPreview_.Asset().bones.empty())
        {
            ImGui::TextDisabled("当前角色没有骨骼。");
            ImGui::End();
            return;
        }

        FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
        workbenchBone_ = std::clamp(workbenchBone_, 0, static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
        const auto selectBone = [&](int boneIndex)
        {
            workbenchBone_ = boneIndex;
            timelineSelectedChannel_ = -1;
            timelineSelectedKey_ = -1;
            for (int channelIndex = 0; channelIndex < static_cast<int>(clip.channels.size()); ++channelIndex)
            {
                if (clip.channels[channelIndex].bone == boneIndex)
                {
                    timelineSelectedChannel_ = channelIndex;
                    break;
                }
            }
        };
        const auto channelCountForBone = [&](int boneIndex)
        {
            int count = 0;
            for (const FEditableRigChannel& channel : clip.channels)
            {
                count += channel.bone == boneIndex ? 1 : 0;
            }
            return count;
        };

        ImGui::BeginChild("##bone_tree", ImVec2(0.0f, -184.0f), ImGuiChildFlags_None);
        const std::string filter = boneFilterBuf_;
        if (!filter.empty())
        {
            for (int boneIndex = 0; boneIndex < static_cast<int>(rigPreview_.Asset().bones.size()); ++boneIndex)
            {
                const Assets::FRigBone& bone = rigPreview_.Asset().bones[boneIndex];
                if (bone.name.find(filter) == std::string::npos)
                {
                    continue;
                }
                const int channelCount = channelCountForBone(boneIndex);
                const std::string label =
                    channelCount > 0 ? fmt::format("{}  [{}]", bone.name, channelCount) : bone.name;
                if (ImGui::Selectable(label.c_str(), boneIndex == workbenchBone_))
                {
                    selectBone(boneIndex);
                }
            }
        }
        else
        {
            const auto drawBoneTree = [&](auto&& self, int boneIndex) -> void
            {
                const Assets::FRigBone& bone = rigPreview_.Asset().bones[boneIndex];
                ImGuiTreeNodeFlags treeFlags =
                    ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                if (bone.children.empty())
                {
                    treeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                }
                if (boneIndex == workbenchBone_)
                {
                    treeFlags |= ImGuiTreeNodeFlags_Selected;
                }
                const int channelCount = channelCountForBone(boneIndex);
                const std::string label =
                    channelCount > 0 ? fmt::format("{}  [{}]", bone.name, channelCount) : bone.name;
                const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<intptr_t>(boneIndex + 1)),
                                                    treeFlags, "%s", label.c_str());
                if (ImGui::IsItemClicked())
                {
                    selectBone(boneIndex);
                }
                if (open && !bone.children.empty())
                {
                    for (int32_t child : bone.children)
                    {
                        if (child >= 0 && child < static_cast<int32_t>(rigPreview_.Asset().bones.size()))
                        {
                            self(self, child);
                        }
                    }
                    ImGui::TreePop();
                }
            };
            for (int boneIndex = 0; boneIndex < static_cast<int>(rigPreview_.Asset().bones.size()); ++boneIndex)
            {
                if (rigPreview_.Asset().bones[boneIndex].parent < 0)
                {
                    drawBoneTree(drawBoneTree, boneIndex);
                }
            }
        }
        ImGui::EndChild();

        const Assets::FRigBone& selectedBone = rigPreview_.Asset().bones[workbenchBone_];
        ImGui::Separator();
        ImGui::TextUnformatted("骨骼信息");
        ImGui::Separator();
        ImGui::TextDisabled("名称");
        ImGui::SameLine(72.0f);
        ImGui::TextUnformatted(selectedBone.name.c_str());
        ImGui::TextDisabled("类型");
        ImGui::SameLine(72.0f);
        ImGui::TextUnformatted("Transform");
        ImGui::TextDisabled("子级数量");
        ImGui::SameLine(72.0f);
        ImGui::Text("%zu", selectedBone.children.size());
        ImGui::TextDisabled("动画轨道");
        ImGui::SameLine(72.0f);
        ImGui::Text("%d", channelCountForBone(workbenchBone_));
        ImGui::End();
    }

    void ScadLibraryInterface::DrawModePanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.96f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, benchCollapsed_ ? ImVec2(7.0f, 8.0f) : ImVec2(12.0f, 12.0f));
        if (!ImGui::Begin("##ScadLibraryBench", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(3);
            return;
        }
        ImGui::PopStyleVar(3);

        if (benchCollapsed_)
        {
            if (ImGui::Button(ICON_FA_CHEVRON_LEFT "##expand_bench", ImVec2(30.0f, 30.0f)))
            {
                benchCollapsed_ = false;
            }
            ImGui::End();
            return;
        }

        if (ImGui::Button(ICON_FA_CHEVRON_RIGHT "##collapse_bench", ImVec2(30.0f, 30.0f)))
        {
            benchCollapsed_ = true;
        }
        ImGui::SameLine();
        if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
        {
            ImGui::Text("场景组装  ·  %zu 对象", bench_.size());
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
        {
            ImGui::TextUnformatted("角色设计");
        }
        else
        {
            ImGui::TextUnformatted("角色动作与装备");
        }
        ImGui::Separator();

        if (workspaceMode_ == EWorkspaceMode::SceneAssembly)
        {
            DrawBenchContent();
        }
        else if (workspaceMode_ == EWorkspaceMode::CharacterDesigner)
        {
            DrawDesignerContent();
        }
        else
        {
            DrawWorkbenchContent();
        }
        ImGui::End();
    }

    void ScadLibraryInterface::DrawBenchContent()
    {
        ImGui::Spacing();
        ImGui::TextDisabled("资源文件");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##assembly_path", "assets/scad/scenes/my_scene.scad", assemblyPathBuf_,
                                 sizeof(assemblyPathBuf_));
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " 打开"))
        {
            OpenAssembly(assemblyPathBuf_);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLAY " 预览") && (!assemblySource_.empty() || !bench_.empty()))
        {
            PreviewAssemblySource();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FLOPPY_DISK " 保存") && !openedAssemblyPath_.empty())
        {
            SaveAssembly(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("另存为") && (!assemblySource_.empty() || !bench_.empty()))
        {
            SaveAssembly(true);
        }

        if (!openedAssemblyPath_.empty())
        {
            ImGui::TextDisabled("%s%s", openedAssemblyPath_.c_str(), assemblySourceDirty_ ? "  *" : "");
        }
        if (!openedAssemblyKits_.empty())
        {
            ImGui::TextDisabled("依赖: %s", fmt::format("{}", fmt::join(openedAssemblyKits_, ", ")).c_str());
        }
        if (openedAssemblyPath_.find("/gen/") != std::string::npos ||
            openedAssemblyPath_.find("\\gen\\") != std::string::npos)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
            ImGui::TextWrapped("gen/ 文件可能由 specs/ 重新生成；保存的手工修改可能被覆盖。");
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        if (ImGui::BeginTabBar("##assembly_editor_tabs"))
        {
            if (ImGui::BeginTabItem(fmt::format("对象 ({})", bench_.size()).c_str()))
            {
                assemblyEditorTab_ = 0;
                ImGui::Checkbox("自动刷新", &autoReload_);
                ImGui::SameLine();
                ImGui::Checkbox("地板", &showFloor_);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::SliderInt("$fn", &fnSegments_, 6, 32))
                {
                    benchDirty_ = true;
                }
                if (!assemblyStructured_ && !assemblySource_.empty())
                {
                    ImGui::TextDisabled("该场景包含自由 SCAD 结构；完整内容请在“源码”页编辑。");
                }

                if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新对象"))
                {
                    ReloadBench();
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_TRASH " 清空") && !bench_.empty())
                {
                    bench_.clear();
                    benchDirty_ = true;
                }
                ImGui::Separator();

                int removeIndex = -1;
                int duplicateIndex = -1;
                ImGui::BeginChild("##bench_list", ImVec2(0, -62.0f), ImGuiChildFlags_None);
                for (size_t i = 0; i < bench_.size(); ++i)
                {
                    FBenchItem& benchItem = bench_[i];
                    ImGui::PushID(static_cast<int>(i));
                    const bool open =
                        ImGui::TreeNodeEx(fmt::format("{} #{}", benchItem.moduleName, i).c_str(),
                                          ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap |
                                              ImGuiTreeNodeFlags_FramePadding);
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52.0f);
                    if (ImGui::SmallButton(ICON_FA_COPY "##dup"))
                    {
                        duplicateIndex = static_cast<int>(i);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK "##del"))
                    {
                        removeIndex = static_cast<int>(i);
                    }
                    if (open)
                    {
                        float position[2] = {benchItem.x, benchItem.y};
                        if (ImGui::DragFloat2("位置", position, 0.5f))
                        {
                            benchItem.x = position[0];
                            benchItem.y = position[1];
                            benchDirty_ = true;
                        }
                        if (ImGui::DragFloat("旋转", &benchItem.rotZ, 1.0f, -360.0f, 360.0f, "%.0f°"))
                        {
                            benchDirty_ = true;
                        }
                        if (ImGui::DragFloat("缩放", &benchItem.scale, 0.02f, 0.05f, 20.0f, "%.2f"))
                        {
                            benchDirty_ = true;
                        }
                        if (ImGui::InputTextWithHint("参数", "如 seed = 3", benchItem.args, sizeof(benchItem.args)))
                        {
                            benchDirty_ = true;
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (bench_.empty())
                {
                    ImGui::TextDisabled("从左侧 Kit 零件库点 \"+\" 添加模块，");
                    ImGui::TextDisabled("在这里调整位置、角度和参数。");
                }
                ImGui::EndChild();

                if (removeIndex >= 0)
                {
                    bench_.erase(bench_.begin() + removeIndex);
                    benchDirty_ = true;
                }
                if (duplicateIndex >= 0)
                {
                    FBenchItem copy = bench_[duplicateIndex];
                    copy.x += kBenchGridStep * 0.5f;
                    copy.y += kBenchGridStep * 0.5f;
                    bench_.push_back(copy);
                    benchDirty_ = true;
                }

                ImGui::Separator();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 116.0f);
                ImGui::InputTextWithHint("##export_name", "新场景文件名", exportNameBuf_, sizeof(exportNameBuf_));
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FILE_EXPORT " 导出场景") && !bench_.empty())
                {
                    ExportBench();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("写入 assets/scad/scenes/<名>.scad");
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("源码"))
            {
                assemblyEditorTab_ = 1;
                ImGui::TextDisabled("支持完整 SCAD；预览不会先覆盖源文件。");
                if (ImGui::InputTextMultiline("##assembly_source", &assemblySource_, ImVec2(-1.0f, -1.0f),
                                              ImGuiInputTextFlags_AllowTabInput))
                {
                    assemblySourceDirty_ = true;
                    assemblyStructured_ = false;
                    openedAssemblyKits_ = FindKitDependencies(assemblySource_);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (assemblyStructured_ && benchDirty_)
        {
            assemblySourceDirty_ = true;
        }
    }

    void ScadLibraryInterface::RescanKits()
    {
        const std::string libDir = AuthoringPath("assets/scad/lib").string();
        bool fromCatalog = false;
        kits_ = LoadKits(libDir, fromCatalog);
        size_t moduleCount = 0;
        for (const FKitInfo& kit : kits_)
        {
            moduleCount += kit.modules.size();
        }
        statusLine_ = fmt::format("{} {} 个 kit / {} 个模块", fromCatalog ? "catalog.json:" : "文本扫描:", kits_.size(),
                                  moduleCount);
        statusError_ = kits_.empty();
        SPDLOG_INFO("[ScadLibrary] kits loaded from {}: {} kits / {} modules",
                    fromCatalog ? "catalog.json" : "text scan", kits_.size(), moduleCount);
        // Bench items keep kit indices; a rescan can reorder them, so re-anchor by path.
        for (FBenchItem& benchItem : bench_)
        {
            if (benchItem.kitIndex >= static_cast<int>(kits_.size()))
            {
                benchItem.kitIndex = -1;
            }
        }
        bench_.erase(std::remove_if(bench_.begin(), bench_.end(), [](const FBenchItem& b) { return b.kitIndex < 0; }),
                     bench_.end());

        // Character designer feeds from the kit_char parts library.
        kitCharIndex_ = -1;
        for (size_t k = 0; k < kits_.size(); ++k)
        {
            if (kits_[k].name == "kit_char")
            {
                kitCharIndex_ = static_cast<int>(k);
                break;
            }
        }
        designer_.SetKit(kitCharIndex_ >= 0 ? &kits_[kitCharIndex_] : nullptr);
        designerEverLoaded_ = false;
    }

    void ScadLibraryInterface::RescanAssemblies()
    {
        const std::filesystem::path scadRoot = AuthoringPath("assets/scad");
        assemblies_.clear();
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator
                 it(scadRoot, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             it != end; it.increment(ec))
        {
            if (ec)
            {
                ec.clear();
                continue;
            }
            const std::filesystem::path relative = it->path().lexically_relative(scadRoot);
            const std::string firstPart = relative.empty() ? "" : relative.begin()->string();
            if (it->is_directory())
            {
                if (firstPart == "lib" || firstPart == "characters" || firstPart == "agents")
                {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file() || it->path().extension() != ".scad")
            {
                continue;
            }
            const std::string source = ReadAssemblyTextFile(it->path());
            std::vector<std::string> dependencies = FindKitDependencies(source);
            if (dependencies.empty())
            {
                continue;
            }
            FSceneAssemblyInfo info;
            info.relativePath = (std::filesystem::path("assets/scad") / relative).generic_string();
            info.absolutePath = std::filesystem::absolute(it->path(), ec).string();
            info.kitDependencies = std::move(dependencies);
            info.generated = firstPart == "gen";
            assemblies_.push_back(std::move(info));
        }
        std::sort(assemblies_.begin(), assemblies_.end(),
                  [](const FSceneAssemblyInfo& a, const FSceneAssemblyInfo& b)
                  {
                      if (a.generated != b.generated)
                      {
                          return !a.generated;
                      }
                      return a.relativePath < b.relativePath;
                  });
        selectedAssembly_ = -1;
        for (int index = 0; index < static_cast<int>(assemblies_.size()); ++index)
        {
            if (!openedAssemblyPath_.empty() &&
                std::filesystem::path(assemblies_[index].absolutePath) == std::filesystem::path(openedAssemblyPath_))
            {
                selectedAssembly_ = index;
                break;
            }
        }
    }

    void ScadLibraryInterface::PreviewModule(int kitIndex, const std::string& moduleName)
    {
        if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()))
        {
            return;
        }
        selectedKit_ = kitIndex;
        selectedModule_ = moduleName;
        rigPreview_.SetActive(false);

        std::string source;
        source += "// ScadLibrary module preview\n";
        source += fmt::format("$fn = {};\n", fnSegments_);
        std::string usePath = kits_[kitIndex].filePath;
        std::replace(usePath.begin(), usePath.end(), '\\', '/');
        source += fmt::format("use <{}>\n\n", usePath);
        source += fmt::format("{}();\n", moduleName);
        if (WriteAndLoad("preview.scad", source))
        {
            statusLine_ = fmt::format("预览 {}", moduleName);
            statusError_ = false;
        }
    }

    void ScadLibraryInterface::AddToBench(int kitIndex, const std::string& moduleName)
    {
        FBenchItem benchItem;
        benchItem.kitIndex = kitIndex;
        benchItem.moduleName = moduleName;

        // Row cursor: advance by the module's catalog footprint so city blocks
        // and hand-scale props both land side by side without overlap.
        float step = kBenchGridStep;
        for (const FKitModuleInfo& moduleInfo : kits_[kitIndex].modules)
        {
            if (moduleInfo.name == moduleName && moduleInfo.hasMetrics)
            {
                step = std::clamp(std::max(moduleInfo.footprintX, moduleInfo.footprintY) + 3.0f, 4.0f, 80.0f);
                break;
            }
        }
        if (bench_.empty())
        {
            benchCursorX_ = 0.0f;
            benchCursorY_ = 0.0f;
            benchRowDepth_ = 0.0f;
            benchColCount_ = 0;
        }
        if (benchColCount_ >= kBenchGridColumns)
        {
            benchCursorX_ = 0.0f;
            benchCursorY_ += benchRowDepth_;
            benchRowDepth_ = 0.0f;
            benchColCount_ = 0;
        }
        benchItem.x = benchCursorX_;
        benchItem.y = benchCursorY_;
        benchCursorX_ += step;
        benchRowDepth_ = std::max(benchRowDepth_, step);
        benchColCount_++;

        bench_.push_back(std::move(benchItem));
        benchDirty_ = true;
    }

    std::string ScadLibraryInterface::BuildBenchSource(const std::filesystem::path& outputPath) const
    {
        std::string source;
        source += "// generated by ScadLibrary scene assembly\n";
        source += "// This flat placement format can be reopened in the Scene Assembly object editor.\n";
        source += fmt::format("$fn = {};\n", fnSegments_);

        std::vector<int> usedKits;
        for (const FBenchItem& benchItem : bench_)
        {
            if (std::find(usedKits.begin(), usedKits.end(), benchItem.kitIndex) == usedKits.end())
            {
                usedKits.push_back(benchItem.kitIndex);
            }
        }
        for (const int kitIndex : usedKits)
        {
            if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()))
            {
                continue;
            }
            std::filesystem::path usePath = kits_[kitIndex].filePath;
            if (!outputPath.empty())
            {
                const std::filesystem::path relative = usePath.lexically_relative(outputPath.parent_path());
                if (!relative.empty())
                {
                    usePath = relative;
                }
            }
            source += fmt::format("use <{}>\n", usePath.generic_string());
        }
        source += "\n";

        if (showFloor_ && !bench_.empty())
        {
            float extent = 30.0f;
            for (const FBenchItem& benchItem : bench_)
            {
                extent = std::max({extent, std::abs(benchItem.x) + 30.0f, std::abs(benchItem.y) + 30.0f});
            }
            source += fmt::format(
                "color([0.50, 0.51, 0.53]) translate([0, 0, -0.15]) cube([{:.1f}, {:.1f}, 0.3], center = true);\n\n",
                extent * 2.0f, extent * 2.0f);
        }

        for (const FBenchItem& benchItem : bench_)
        {
            const char* args = benchItem.args;
            source += fmt::format(
                "translate([{:.2f}, {:.2f}, 0]) rotate([0, 0, {:.1f}]) scale([{:.3f}, {:.3f}, {:.3f}]) {}({});\n",
                benchItem.x, benchItem.y, benchItem.rotZ, benchItem.scale, benchItem.scale, benchItem.scale,
                benchItem.moduleName, args);
        }
        return source;
    }

    bool ScadLibraryInterface::ParseStructuredAssembly(const std::string& source)
    {
        const bool structured = source.find("generated by ScadLibrary scene assembly") != std::string::npos ||
            source.find("generated by ScadLibrary compose bench") != std::string::npos;
        if (!structured)
        {
            return false;
        }

        static const std::string number = R"([-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)";
        const std::regex itemRegex("^\\s*translate\\(\\[\\s*(" + number + ")\\s*,\\s*(" + number + ")\\s*,\\s*" +
                                       number + "\\s*\\]\\)\\s*rotate\\(\\[\\s*" + number + "\\s*,\\s*" + number +
                                       "\\s*,\\s*(" + number + ")\\s*\\]\\)\\s*scale\\(\\[\\s*(" + number +
                                       ")\\s*,\\s*" + number + "\\s*,\\s*" + number +
                                       "\\s*\\]\\)\\s*([A-Za-z_][A-Za-z0-9_]*)\\((.*)\\);\\s*$",
                                   std::regex_constants::icase);
        const std::regex fnRegex(R"(\$fn\s*=\s*(\d+)\s*;)");
        std::smatch fnMatch;
        if (std::regex_search(source, fnMatch, fnRegex))
        {
            fnSegments_ = std::clamp(std::stoi(fnMatch[1].str()), 3, 128);
        }
        showFloor_ = source.find("cube([") != std::string::npos;
        bench_.clear();

        std::istringstream lines(source);
        std::string line;
        while (std::getline(lines, line))
        {
            std::smatch match;
            if (!std::regex_match(line, match, itemRegex))
            {
                continue;
            }
            const std::string moduleName = match[5].str();
            int kitIndex = -1;
            const auto hasModule = [&](int candidate)
            {
                return std::any_of(kits_[candidate].modules.begin(), kits_[candidate].modules.end(),
                                   [&](const FKitModuleInfo& module) { return module.name == moduleName; });
            };
            for (const std::string& dependency : openedAssemblyKits_)
            {
                for (int candidate = 0; candidate < static_cast<int>(kits_.size()); ++candidate)
                {
                    if (kits_[candidate].name == dependency && hasModule(candidate))
                    {
                        kitIndex = candidate;
                        break;
                    }
                }
                if (kitIndex >= 0)
                {
                    break;
                }
            }
            if (kitIndex < 0)
            {
                for (int candidate = 0; candidate < static_cast<int>(kits_.size()); ++candidate)
                {
                    if (hasModule(candidate))
                    {
                        kitIndex = candidate;
                        break;
                    }
                }
            }
            if (kitIndex < 0)
            {
                continue;
            }
            FBenchItem item;
            item.kitIndex = kitIndex;
            item.moduleName = moduleName;
            item.x = std::stof(match[1].str());
            item.y = std::stof(match[2].str());
            item.rotZ = std::stof(match[3].str());
            item.scale = std::stof(match[4].str());
            std::snprintf(item.args, sizeof(item.args), "%s", match[6].str().c_str());
            bench_.push_back(std::move(item));
        }
        benchDirty_ = false;
        return true;
    }

    bool ScadLibraryInterface::OpenAssembly(const std::string& path)
    {
        const std::filesystem::path scadRoot = AuthoringPath("assets/scad");
        std::filesystem::path sourcePath(path);
        if (!sourcePath.is_absolute())
        {
            const std::string generic = sourcePath.generic_string();
            constexpr std::string_view prefix = "assets/scad/";
            sourcePath = generic.starts_with(prefix) ? scadRoot / generic.substr(prefix.size()) : scadRoot / sourcePath;
        }
        std::error_code ec;
        sourcePath = std::filesystem::weakly_canonical(sourcePath, ec);
        if (ec || !IsPathWithin(sourcePath, scadRoot) || sourcePath.extension() != ".scad" ||
            !std::filesystem::is_regular_file(sourcePath, ec))
        {
            statusLine_ = fmt::format("无法打开场景: {}", path);
            statusError_ = true;
            return false;
        }
        const std::string source = ReadAssemblyTextFile(sourcePath);
        if (source.empty())
        {
            statusLine_ = fmt::format("场景为空或读取失败: {}", sourcePath.string());
            statusError_ = true;
            return false;
        }

        rigPreview_.SetActive(false);
        openedAssemblyPath_ = sourcePath.string();
        assemblySource_ = source;
        openedAssemblyKits_ = FindKitDependencies(source);
        assemblySourceDirty_ = false;
        bench_.clear();
        assemblyStructured_ = ParseStructuredAssembly(source);
        const std::filesystem::path repoRoot = scadRoot.parent_path().parent_path();
        const std::string relativePath = sourcePath.lexically_relative(repoRoot).generic_string();
        std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", relativePath.c_str());
        for (int index = 0; index < static_cast<int>(assemblies_.size()); ++index)
        {
            if (std::filesystem::path(assemblies_[index].absolutePath) == sourcePath)
            {
                selectedAssembly_ = index;
                break;
            }
        }
        engine_.RequestLoadScene({.filename = openedAssemblyPath_});
        statusLine_ = fmt::format("已打开 {} · {} 个 Kit{}", relativePath, openedAssemblyKits_.size(),
                                  assemblyStructured_ ? " · 可视化对象编辑" : " · 源码编辑");
        statusError_ = openedAssemblyKits_.empty();
        return true;
    }

    std::string ScadLibraryInterface::BuildAssemblyPreviewSource() const
    {
        if (assemblyStructured_)
        {
            return BuildBenchSource();
        }
        if (assemblySource_.empty() || openedAssemblyPath_.empty())
        {
            return assemblySource_;
        }

        static const std::regex useRegex(R"(((?:use|include)\s*<)([^>]+)(>))", std::regex_constants::icase);
        std::string result;
        std::string::const_iterator cursor = assemblySource_.begin();
        std::smatch match;
        while (std::regex_search(cursor, assemblySource_.end(), match, useRegex))
        {
            result.append(cursor, match[0].first);
            std::filesystem::path dependencyPath(match[2].str());
            if (!dependencyPath.is_absolute())
            {
                dependencyPath = std::filesystem::path(openedAssemblyPath_).parent_path() / dependencyPath;
            }
            result += match[1].str();
            result += dependencyPath.lexically_normal().generic_string();
            result += match[3].str();
            cursor = match[0].second;
        }
        result.append(cursor, assemblySource_.end());
        return result;
    }

    void ScadLibraryInterface::PreviewAssemblySource()
    {
        rigPreview_.SetActive(false);
        if (WriteAndLoad("assembly_preview.scad", BuildAssemblyPreviewSource()))
        {
            statusLine_ =
                assemblyStructured_ ? fmt::format("预览 {} 个场景对象", bench_.size()) : "预览未保存的 SCAD 源码";
            statusError_ = false;
        }
    }

    void ScadLibraryInterface::SaveAssembly(bool saveAs)
    {
        const std::filesystem::path scadRoot = AuthoringPath("assets/scad");
        std::filesystem::path targetPath =
            saveAs ? std::filesystem::path(assemblyPathBuf_) : std::filesystem::path(openedAssemblyPath_);
        if (targetPath.empty())
        {
            statusLine_ = "请先输入场景保存路径";
            statusError_ = true;
            return;
        }
        if (!targetPath.is_absolute())
        {
            const std::string generic = targetPath.generic_string();
            constexpr std::string_view prefix = "assets/scad/";
            targetPath = generic.starts_with(prefix) ? scadRoot / generic.substr(prefix.size()) : scadRoot / targetPath;
        }
        if (targetPath.extension().empty())
        {
            targetPath += ".scad";
        }
        targetPath = targetPath.lexically_normal();
        if (!IsPathWithin(targetPath, scadRoot) || targetPath.extension() != ".scad" ||
            targetPath.lexically_relative(scadRoot).begin()->string() == "lib")
        {
            statusLine_ = "场景只能保存为 assets/scad 下、lib 目录外的 .scad 文件";
            statusError_ = true;
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(targetPath.parent_path(), ec);
        const std::string source = assemblyStructured_ ? BuildBenchSource(targetPath) : assemblySource_;
        std::ofstream output(targetPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            statusLine_ = fmt::format("保存失败: {}", targetPath.string());
            statusError_ = true;
            return;
        }
        output << source;
        output.close();

        openedAssemblyPath_ = std::filesystem::absolute(targetPath, ec).string();
        assemblySource_ = source;
        openedAssemblyKits_ = FindKitDependencies(source);
        assemblySourceDirty_ = false;
        benchDirty_ = false;
        const std::filesystem::path repoRoot = scadRoot.parent_path().parent_path();
        const std::string relativePath = targetPath.lexically_relative(repoRoot).generic_string();
        std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", relativePath.c_str());
        engine_.RequestLoadScene({.filename = openedAssemblyPath_});
        RescanAssemblies();
        statusLine_ = fmt::format("已保存 {}", relativePath);
        statusError_ = false;
        SPDLOG_INFO("[ScadLibrary] saved scene assembly -> {}", targetPath.string());
    }

    void ScadLibraryInterface::ReloadBench()
    {
        benchDirty_ = false;
        rigPreview_.SetActive(false);
        if (bench_.empty())
        {
            statusLine_ = "场景对象为空";
            statusError_ = false;
            return;
        }
        if (WriteAndLoad("bench.scad", BuildBenchSource()))
        {
            statusLine_ = fmt::format("场景组装 {} 个对象已重载", bench_.size());
            statusError_ = false;
        }
    }

    void ScadLibraryInterface::ExportBench()
    {
        std::string name(exportNameBuf_);
        if (name.empty())
        {
            name = "my_scene";
        }
        std::string cleanName;
        for (const char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')
            {
                cleanName.push_back(c);
            }
        }
        if (cleanName.empty())
        {
            cleanName = "my_scene";
        }
        const std::filesystem::path sceneDir = AuthoringPath("assets/scad") / "scenes";
        std::error_code ec;
        std::filesystem::create_directories(sceneDir, ec);
        const std::filesystem::path outPath = sceneDir / (cleanName + ".scad");
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            statusLine_ = fmt::format("导出失败: {}", outPath.string());
            statusError_ = true;
            return;
        }
        out << BuildBenchSource(outPath);
        out.close();
        statusLine_ = fmt::format("已导出 {}", outPath.string());
        statusError_ = false;
        RescanAssemblies();
        OpenAssembly(outPath.string());
        SPDLOG_INFO("[ScadLibrary] exported scene assembly -> {}", outPath.string());
    }

    bool ScadLibraryInterface::WriteWorkspaceFile(const std::string& fileName, const std::string& source,
                                                  std::string& outAbsPath)
    {
        std::error_code ec;
        std::filesystem::create_directories(WorkspaceDir(), ec);
        const std::filesystem::path path = WorkspaceDir() / fileName;
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            statusLine_ = fmt::format("写入失败: {}", path.string());
            statusError_ = true;
            return false;
        }
        out << source;
        out.close();
        outAbsPath = std::filesystem::absolute(path, ec).string();
        return true;
    }

    bool ScadLibraryInterface::WriteAndLoad(const std::string& fileName, const std::string& source)
    {
        std::string absPath;
        if (!WriteWorkspaceFile(fileName, source, absPath))
        {
            return false;
        }
        engine_.RequestLoadScene({.filename = absPath});
        return true;
    }

    // ------------------------------------------------------------- character designer

    std::string ScadLibraryInterface::KitCharUsePath(bool relative) const
    {
        if (relative)
        {
            return "../lib/kit_char.scad";
        }
        if (kitCharIndex_ < 0)
        {
            return "";
        }
        std::string usePath = kits_[kitCharIndex_].filePath;
        std::replace(usePath.begin(), usePath.end(), '\\', '/');
        return usePath;
    }

    void ScadLibraryInterface::ReloadDesigner()
    {
        designerDirty_ = false;
        if (!designer_.HasKit())
        {
            statusLine_ = "assets/scad/lib 下没有可用的 kit_char.scad";
            statusError_ = true;
            return;
        }

        const std::string source = designer_.BuildSource(KitCharUsePath(false));
        std::string characterPath;
        if (!WriteWorkspaceFile("character_preview.scad", source, characterPath))
        {
            return;
        }

        std::string error;
        std::vector<std::string> warnings;
        if (!rigPreview_.LoadRig(characterPath, error, &warnings))
        {
            // Rig 解析失败：退回静态场景加载，至少能看到几何和报错。
            rigPreview_.SetActive(false);
            engine_.RequestLoadScene({.filename = characterPath});
            statusLine_ = fmt::format("rig 解析失败: {}", error);
            statusError_ = true;
            return;
        }

        std::string equipmentError;
        rigPreview_.SetEquipment({}, equipmentError);
        rigPreview_.PlayClip("idle");
        rigPreview_.SetPaused(false);
        rigPreview_.SetTint(glm::vec3(designerTint_[0], designerTint_[1], designerTint_[2]));
        rigPreview_.SetActive(true);

        // 舞台场景只有地板；角色由 rig 实例化（避免与静态 bind 网格重影）。
        std::string stage;
        stage += "// rig preview stage\n";
        stage += "color([0.50, 0.51, 0.53]) translate([0, 0, -0.15]) cube([3.5, 3.5, 0.3], center = true);\n";
        stage += "color([0.62, 0.63, 0.65]) translate([0, 0, 0.001]) cube([0.9, 0.9, 0.002], center = true);\n";
        if (!WriteAndLoad("rigstage.scad", stage))
        {
            return;
        }

        // DroidSansFallback 缺"骼"字形，用"骨架"。
        statusLine_ = fmt::format("角色预览: 骨架 {} / 部件 {} / 动画 {}{}", rigPreview_.Asset().bones.size(),
                                  rigPreview_.Asset().parts.size(), rigPreview_.Asset().clips.size(),
                                  warnings.empty() ? "" : fmt::format("，{} 条 warning", warnings.size()));
        statusError_ = !warnings.empty();
    }

    void ScadLibraryInterface::ExportCharacter()
    {
        std::string clean;
        for (const char c : std::string(characterNameBuf_))
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            {
                clean.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        if (clean.empty())
        {
            clean = "my_character";
        }
        const std::filesystem::path charDir = AuthoringPath("assets/scad") / "characters";
        std::error_code ec;
        std::filesystem::create_directories(charDir, ec);
        const std::filesystem::path outPath = charDir / (clean + ".scad");
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            statusLine_ = fmt::format("导出失败: {}", outPath.string());
            statusError_ = true;
            return;
        }
        out << designer_.BuildSource(KitCharUsePath(true));
        statusLine_ = fmt::format("已导出 {}", outPath.string());
        statusError_ = false;
        SPDLOG_INFO("[ScadLibrary] exported character -> {}", outPath.string());
    }

    void ScadLibraryInterface::DrawDesignerContent()
    {
        if (!designer_.HasKit())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("assets/scad/lib 下没有可用的 kit_char.scad");
            ImGui::TextDisabled("（需要 head/torso/arm/leg 分类的部件）");
            return;
        }
        if (!designerEverLoaded_)
        {
            designerEverLoaded_ = true;
            designerDirty_ = true;
        }

        ImGui::Spacing();
        auto slotCombo = [&](const char* label, const std::vector<FKitModuleInfo>& options, int& index, bool optional)
        {
            const char* current =
                index >= 0 && index < static_cast<int>(options.size()) ? options[index].name.c_str() : "（无）";
            if (ImGui::BeginCombo(label, current))
            {
                if (optional && ImGui::Selectable("（无）", index < 0) && index != -1)
                {
                    index = -1;
                    designerDirty_ = true;
                }
                for (int i = 0; i < static_cast<int>(options.size()); ++i)
                {
                    if (ImGui::Selectable(options[i].name.c_str(), index == i) && index != i)
                    {
                        index = i;
                        designerDirty_ = true;
                    }
                }
                ImGui::EndCombo();
            }
        };

        slotCombo("头型", designer_.Heads(), designer_.head, false);
        slotCombo("发型", designer_.Hairs(), designer_.hair, true);
        slotCombo("帽饰", designer_.Hats(), designer_.hat, true);
        // DroidSansFallback 缺"躯"字形，用"上身"。
        slotCombo("上身", designer_.Torsos(), designer_.torso, false);
        slotCombo("手臂", designer_.Arms(), designer_.arm, false);
        slotCombo("腿部", designer_.Legs(), designer_.leg, false);

        if (!designer_.Accessories().empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("配饰");
            for (size_t i = 0; i < designer_.Accessories().size(); ++i)
            {
                bool enabled = i < designer_.accEnabled.size() && designer_.accEnabled[i] != 0;
                if (ImGui::Checkbox(designer_.Accessories()[i].name.c_str(), &enabled))
                {
                    designer_.accEnabled[i] = enabled ? 1 : 0;
                    designerDirty_ = true;
                }
            }
        }

        ImGui::Separator();
        if (ImGui::ColorEdit3("肤色", designer_.skinColor))
        {
            designerDirty_ = true;
        }
        if (ImGui::ColorEdit3("发色", designer_.hairColor))
        {
            designerDirty_ = true;
        }
        if (ImGui::ColorEdit3("主色", designerTint_))
        {
            // 运行时 tint（品红占位部位），改材质即可，无需重载。
            rigPreview_.SetTint(glm::vec3(designerTint_[0], designerTint_[1], designerTint_[2]));
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("运行时换色（ch_TINT() 品红占位部位）；导出文件保留占位");
        }

        ImGui::Separator();
        if (rigPreview_.HasRig() && rigPreview_.Active())
        {
            const std::string& current = rigPreview_.CurrentClip();
            if (ImGui::BeginCombo("动画", current.empty() ? "绑定姿态" : current.c_str()))
            {
                if (ImGui::Selectable("绑定姿态", current.empty()))
                {
                    rigPreview_.PlayClip("");
                }
                for (const Assets::FRigClip& clip : rigPreview_.Asset().clips)
                {
                    if (ImGui::Selectable(clip.name.c_str(), current == clip.name))
                    {
                        rigPreview_.PlayClip(clip.name);
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新预览"))
        {
            ReloadDesigner();
        }

        ImGui::Separator();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 96.0f);
        ImGui::InputTextWithHint("##char_name", "角色文件名", characterNameBuf_, sizeof(characterNameBuf_));
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_EXPORT " 导出##char"))
        {
            ExportCharacter();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("写入 assets/scad/characters/<名>.scad（use ../lib/kit_char.scad，游戏可直接加载）");
        }
    }

    // ---------------------------------------------------- rig action/equipment workbench

    void ScadLibraryInterface::ReloadWorkbench()
    {
        workbenchReloadRequested_ = false;
        workbenchEquipmentRebuildRequested_ = false;

        const std::filesystem::path sourcePath = AuthoringPath(rigSourceBuf_);

        std::string error;
        std::vector<std::string> warnings;
        if (!rigPreview_.LoadRig(sourcePath.string(), error, &warnings))
        {
            statusLine_ = fmt::format("角色载入失败: {}", error);
            statusError_ = true;
            rigPreview_.SetActive(false);
            return;
        }
        if (!workbench_.CaptureRig(sourcePath.string(), rigPreview_.Asset(), error))
        {
            statusLine_ = fmt::format("动作编辑器初始化失败: {}", error);
            statusError_ = true;
            return;
        }

        if (!workbench_.LoadEquipment(workbench_.EquipmentPath(), error))
        {
            std::string coldwarPath;
            for (const FKitInfo& kit : kits_)
            {
                if (kit.name == "kit_coldwar")
                {
                    coldwarPath = kit.filePath;
                    break;
                }
            }
            workbench_.SetDefaultEquipment(coldwarPath);
        }

        workbenchClip_ = 0;
        for (size_t index = 0; index < workbench_.Clips().size(); ++index)
        {
            if (workbench_.Clips()[index].name == "stand_idle")
            {
                workbenchClip_ = static_cast<int>(index);
                break;
            }
        }
        workbenchBone_ = 0;
        timelineSelectedChannel_ = -1;
        timelineSelectedKey_ = -1;
        timelineDraggingKey_ = false;
        timelineVisibleDuration_ = 0.0f;
        if (!workbench_.Clips().empty() && !workbench_.Clips()[workbenchClip_].channels.empty())
        {
            timelineSelectedChannel_ = 0;
            workbenchBone_ = std::clamp(workbench_.Clips()[workbenchClip_].channels.front().bone, 0,
                                        static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
        }
        rigPreview_.SetActive(true);
        rigPreview_.SetPaused(false);
        rigPreview_.SetPlaySpeed(1.0f);
        if (!workbench_.Clips().empty())
        {
            rigPreview_.PlayClip(workbench_.Clips()[workbenchClip_].name);
            rigPreview_.SetCurrentTime(0.0f);
        }
        ReloadWorkbenchStage();
        workbenchEverLoaded_ = true;
        statusLine_ = fmt::format("角色工作室: {} 骨架 / {} 动作 / {} 装备{}", rigPreview_.Asset().bones.size(),
                                  workbench_.Clips().size(), workbench_.Equipment().size(),
                                  warnings.empty() ? "" : fmt::format("，{} 条 warning", warnings.size()));
        statusError_ = !warnings.empty();
    }

    void ScadLibraryInterface::ReloadWorkbenchStage()
    {
        workbenchEquipmentRebuildRequested_ = false;
        std::string equipmentError;
        const bool equipmentOk = rigPreview_.SetEquipment(workbench_.Equipment(), equipmentError);
        rigPreview_.SetActive(true);

        std::string stage;
        stage += "// ScadLibrary rig workbench stage\n";
        stage += "color([0.48, 0.49, 0.51]) translate([0, 0, -0.15]) cube([4.5, 4.5, 0.3], center = true);\n";
        stage += "color([0.62, 0.63, 0.65]) translate([0, 0, 0.001]) cube([1.0, 1.0, 0.002], center = true);\n";
        if (!WriteAndLoad("rig_workbench_stage.scad", stage))
        {
            return;
        }
        if (!equipmentOk)
        {
            statusLine_ = fmt::format("部分装备预览失败: {}", equipmentError);
            statusError_ = true;
        }
    }

    void ScadLibraryInterface::ApplyWorkbenchRigEdit()
    {
        workbench_.CommitRigEdit();
        std::string error;
        if (!workbench_.ApplyToAsset(rigPreview_.MutableAsset(), error))
        {
            statusLine_ = fmt::format("动作应用失败: {}", error);
            statusError_ = true;
            return;
        }
        rigPreview_.SetCurrentTime(std::min(rigPreview_.CurrentTime(), rigPreview_.CurrentDuration()));
    }

    void ScadLibraryInterface::DrawWorkbenchContent()
    {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-88.0f);
        ImGui::InputTextWithHint("##rig_source", "ScadRig .scad 路径", rigSourceBuf_, sizeof(rigSourceBuf_));
        ImGui::SameLine();
        if (ImGui::Button("打开", ImVec2(80.0f, 0.0f)))
        {
            workbenchReloadRequested_ = true;
        }

        if (!workbenchEverLoaded_ || !rigPreview_.HasRig() || workspaceMode_ != EWorkspaceMode::CharacterWorkbench)
        {
            ImGui::TextDisabled("正在载入角色工作室…");
            return;
        }

        ImGui::TextDisabled("%s", workbench_.SourcePath().c_str());
        if (ImGui::Button(workbench_.RigDirty() ? "保存动作 *" : "保存动作"))
        {
            std::string error;
            if (workbench_.SaveRig(error))
            {
                statusLine_ = fmt::format("动作已保存到 {}", workbench_.SourcePath());
                statusError_ = false;
            }
            else
            {
                statusLine_ = fmt::format("动作保存失败: {}", error);
                statusError_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(workbench_.EquipmentDirty() ? "保存装备 *" : "保存装备"))
        {
            std::string error;
            if (workbench_.SaveEquipment(error))
            {
                statusLine_ = fmt::format("装备已保存到 {}", workbench_.EquipmentPath());
                statusError_ = false;
            }
            else
            {
                statusLine_ = fmt::format("装备保存失败: {}", error);
                statusError_ = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("从磁盘重载"))
        {
            workbenchReloadRequested_ = true;
        }

        if (!workbench_.Clips().empty())
        {
            workbenchClip_ = std::clamp(workbenchClip_, 0, static_cast<int>(workbench_.Clips().size()) - 1);
            FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
            if (ImGui::BeginCombo("动作", clip.name.c_str()))
            {
                for (int index = 0; index < static_cast<int>(workbench_.Clips().size()); ++index)
                {
                    const bool selected = index == workbenchClip_;
                    if (ImGui::Selectable(workbench_.Clips()[index].name.c_str(), selected))
                    {
                        workbenchClip_ = index;
                        timelineSelectedChannel_ = -1;
                        timelineSelectedKey_ = -1;
                        timelineDraggingKey_ = false;
                        timelineVisibleDuration_ = 0.0f;
                        if (!workbench_.Clips()[index].channels.empty())
                        {
                            timelineSelectedChannel_ = 0;
                            workbenchBone_ = std::clamp(workbench_.Clips()[index].channels.front().bone, 0,
                                                        static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
                        }
                        rigPreview_.PlayClip(workbench_.Clips()[index].name);
                    }
                }
                ImGui::EndCombo();
            }

            if (ImGui::Button(rigPreview_.Paused() ? "播放" : "暂停"))
            {
                rigPreview_.SetPaused(!rigPreview_.Paused());
            }
            ImGui::SameLine();
            if (ImGui::Button("回到开头"))
            {
                rigPreview_.SetPaused(true);
                rigPreview_.SetCurrentTime(0.0f);
            }
            ImGui::SameLine();
            float speed = rigPreview_.PlaySpeed();
            ImGui::SetNextItemWidth(100.0f);
            if (ImGui::DragFloat("速度", &speed, 0.05f, -3.0f, 3.0f, "%.2fx"))
            {
                rigPreview_.SetPlaySpeed(speed);
            }
            float time = rigPreview_.CurrentTime();
            if (ImGui::SliderFloat("时间", &time, 0.0f, std::max(rigPreview_.CurrentDuration(), 0.001f), "%.3f s"))
            {
                rigPreview_.SetPaused(true);
                rigPreview_.SetCurrentTime(time);
            }
        }

        ImGui::Separator();
        if (ImGui::BeginTabBar("##workbench_modes"))
        {
            if (ImGui::BeginTabItem("动作修改"))
            {
                workbenchEditorTab_ = 0;
                ImGui::TextDisabled("关键帧编辑器已停靠在视口底部。");
                if (!workbench_.Clips().empty())
                {
                    const FEditableRigClip& activeClip = workbench_.Clips()[workbenchClip_];
                    ImGui::Spacing();
                    ImGui::Text("%s", activeClip.name.c_str());
                    ImGui::TextDisabled("%.3f 秒 / %zu 条轨道", activeClip.duration, activeClip.channels.size());
                    ImGui::TextDisabled("左侧骨骼树选择骨骼，右侧时间轴编辑轨道。");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(fmt::format("装备 ({})", workbench_.Equipment().size()).c_str()))
            {
                workbenchEditorTab_ = 1;
                DrawEquipmentEditor();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void ScadLibraryInterface::DrawViewportToolbar(const ImVec2& viewportPos)
    {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 12.0f, viewportPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        if (ImGui::Begin("##ScadLibraryViewportToolbar", nullptr, flags))
        {
            ImGui::TextDisabled(ICON_FA_ARROW_POINTER);
            ImGui::SameLine();
            if (ImGui::SmallButton(boneGizmoOperation_ == 0 ? "[移动]" : "移动"))
            {
                boneGizmoOperation_ = 0;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(boneGizmoOperation_ == 1 ? "[旋转]" : "旋转"))
            {
                boneGizmoOperation_ = 1;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(boneGizmoOperation_ == 2 ? "[缩放]" : "缩放"))
            {
                boneGizmoOperation_ = 2;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextDisabled("本地");
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextDisabled("透视");
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ScadLibraryInterface::UpsertGizmoKey(EEditableRigChannel type, const glm::vec3& value)
    {
        if (workbench_.Clips().empty() || workbenchClip_ < 0 ||
            workbenchClip_ >= static_cast<int>(workbench_.Clips().size()))
        {
            return;
        }

        FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
        int channelIndex = -1;
        for (int index = 0; index < static_cast<int>(clip.channels.size()); ++index)
        {
            if (clip.channels[index].bone == workbenchBone_ && clip.channels[index].type == type)
            {
                channelIndex = index;
                break;
            }
        }
        if (channelIndex < 0)
        {
            FEditableRigChannel channel;
            channel.bone = workbenchBone_;
            channel.type = type;
            clip.channels.push_back(std::move(channel));
            channelIndex = static_cast<int>(clip.channels.size()) - 1;
        }

        FEditableRigChannel& channel = clip.channels[channelIndex];
        const float currentTime = rigPreview_.CurrentTime();
        int keyIndex = -1;
        for (int index = 0; index < static_cast<int>(channel.keys.size()); ++index)
        {
            if (std::abs(channel.keys[index].time - currentTime) <= 0.0005f)
            {
                keyIndex = index;
                break;
            }
        }
        if (keyIndex < 0)
        {
            channel.keys.push_back({currentTime, value});
            keyIndex = static_cast<int>(channel.keys.size()) - 1;
        }
        else
        {
            channel.keys[keyIndex].value = value;
        }

        timelineSelectedChannel_ = channelIndex;
        timelineSelectedKey_ = keyIndex;
        ApplyWorkbenchRigEdit();

        const std::vector<FEditableRigKey>& sortedKeys = clip.channels[channelIndex].keys;
        float nearest = std::numeric_limits<float>::max();
        for (int index = 0; index < static_cast<int>(sortedKeys.size()); ++index)
        {
            const float distance = std::abs(sortedKeys[index].time - currentTime);
            if (distance < nearest)
            {
                nearest = distance;
                timelineSelectedKey_ = index;
            }
        }
    }

    void ScadLibraryInterface::DrawBoneGizmo(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f || workbenchBone_ < 0 ||
            workbenchBone_ >= static_cast<int>(rigPreview_.Asset().bones.size()))
        {
            return;
        }
        Assets::Node* boneNode = rigPreview_.BoneNode(workbenchBone_);
        if (boneNode == nullptr)
        {
            return;
        }

        const Assets::UniformBufferObject& ubo = engine_.GetLastUniformBufferObject();
        const glm::mat4& view = ubo.ModelView;
        glm::mat4 projection = ubo.Projection;
        projection[1][1] *= -1.0f;
        glm::mat4 worldMatrix = boneNode->WorldTransform();

        constexpr ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
        boneGizmoOperation_ = std::clamp(boneGizmoOperation_, 0, 2);
        ImGuizmo::SetAlternativeWindow(nullptr);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
        ImGuizmo::GetStyle().Colors[ImGuizmo::COLOR::SELECTION] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operations[boneGizmoOperation_],
                             ImGuizmo::LOCAL, glm::value_ptr(worldMatrix));

        const bool interacting = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        ImGui::GetIO().WantCaptureMouse = ImGui::GetIO().WantCaptureMouse || interacting;
        if (!ImGuizmo::IsUsing())
        {
            return;
        }

        rigPreview_.SetPaused(true);
        glm::mat4 parentWorld(1.0f);
        if (Assets::Node* parent = boneNode->GetParent())
        {
            parentWorld = parent->WorldTransform();
        }
        const glm::mat4 localMatrix = glm::inverse(parentWorld) * worldMatrix;
        const Assets::FRigBone& bone = rigPreview_.Asset().bones[workbenchBone_];
        const glm::mat4 bindMatrix = glm::translate(glm::mat4(1.0f), bone.bindT) * glm::mat4_cast(bone.bindR) *
            glm::scale(glm::mat4(1.0f), bone.bindS);
        const glm::mat4 offsetMatrix = glm::inverse(bindMatrix) * localMatrix;

        glm::vec3 scale{};
        glm::quat rotation{};
        glm::vec3 translation{};
        glm::vec3 skew{};
        glm::vec4 perspective{};
        if (!glm::decompose(offsetMatrix, scale, rotation, translation, skew, perspective))
        {
            return;
        }
        rotation = glm::normalize(rotation);
        if (boneGizmoOperation_ == 0)
        {
            UpsertGizmoKey(EEditableRigChannel::Position, FCharacterWorkbench::EnginePositionToScad(translation));
        }
        else if (boneGizmoOperation_ == 1)
        {
            UpsertGizmoKey(EEditableRigChannel::Rotation, FCharacterWorkbench::EngineRotationToScad(rotation));
        }
        else
        {
            UpsertGizmoKey(EEditableRigChannel::Scale, FCharacterWorkbench::EngineScaleToScad(scale));
        }
    }

    void ScadLibraryInterface::DrawAnimationTimelinePanel(const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.98f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        if (!ImGui::Begin("##ScadLibraryAnimationTimeline", nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleVar(2);
            return;
        }

        if (workbench_.Clips().empty() || rigPreview_.Asset().bones.empty())
        {
            ImGui::TextDisabled("当前角色没有可编辑动作或骨架。");
            ImGui::End();
            ImGui::PopStyleVar(2);
            return;
        }

        FEditableRigClip& clip = workbench_.Clips()[workbenchClip_];
        bool edited = false;

        const auto sampleChannel = [](const FEditableRigChannel& channel, float time)
        {
            const glm::vec3 defaultValue =
                channel.type == EEditableRigChannel::Scale ? glm::vec3(1.0f) : glm::vec3(0.0f);
            if (channel.keys.empty())
            {
                return defaultValue;
            }
            if (time <= channel.keys.front().time)
            {
                return channel.keys.front().value;
            }
            if (time >= channel.keys.back().time)
            {
                return channel.keys.back().value;
            }
            for (size_t index = 1; index < channel.keys.size(); ++index)
            {
                if (time <= channel.keys[index].time)
                {
                    const FEditableRigKey& lhs = channel.keys[index - 1];
                    const FEditableRigKey& rhs = channel.keys[index];
                    const float alpha = (time - lhs.time) / std::max(rhs.time - lhs.time, 0.0001f);
                    return glm::mix(lhs.value, rhs.value, alpha);
                }
            }
            return channel.keys.back().value;
        };

        ImGui::TextUnformatted(ICON_FA_FILM " 动作时间轴");
        ImGui::SameLine();
        ImGui::TextDisabled("%s  ·  %.3f 秒  ·  %zu 条轨道", clip.name.c_str(), clip.duration, clip.channels.size());
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - 244.0f));
        if (ImGui::SmallButton(boneGizmoOperation_ == 0 ? "[移动]" : "移动"))
        {
            boneGizmoOperation_ = 0;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(boneGizmoOperation_ == 1 ? "[旋转]" : "旋转"))
        {
            boneGizmoOperation_ = 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(boneGizmoOperation_ == 2 ? "[缩放]" : "缩放"))
        {
            boneGizmoOperation_ = 2;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("拖动 Gizmo 自动 K 帧");
        ImGui::Separator();

        workbenchBone_ = std::clamp(workbenchBone_, 0, static_cast<int>(rigPreview_.Asset().bones.size()) - 1);
        ImGui::BeginChild("##timeline_editor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);

        if (ImGui::SmallButton(ICON_FA_BACKWARD_STEP))
        {
            rigPreview_.SetPaused(true);
            rigPreview_.SetCurrentTime(0.0f);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(rigPreview_.Paused() ? ICON_FA_PLAY : ICON_FA_PAUSE))
        {
            rigPreview_.SetPaused(!rigPreview_.Paused());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_STOP))
        {
            rigPreview_.SetPaused(true);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_FORWARD_STEP))
        {
            rigPreview_.SetPaused(true);
            rigPreview_.SetCurrentTime(clip.duration);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.3f s", rigPreview_.CurrentTime());
        ImGui::SameLine();
        if (ImGui::Checkbox("循环", &clip.loop))
        {
            edited = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%.3f s / %zu 通道", clip.duration, clip.channels.size());

        const Assets::FRigBone& selectedBone = rigPreview_.Asset().bones[workbenchBone_];
        ImGui::SameLine();
        ImGui::TextDisabled("当前骨骼: %s", selectedBone.name.c_str());

        bool hasPosition = false;
        bool hasRotation = false;
        bool hasScale = false;
        for (const FEditableRigChannel& channel : clip.channels)
        {
            if (channel.bone != workbenchBone_)
            {
                continue;
            }
            hasPosition |= channel.type == EEditableRigChannel::Position;
            hasRotation |= channel.type == EEditableRigChannel::Rotation;
            hasScale |= channel.type == EEditableRigChannel::Scale;
        }

        const auto addChannel = [&](const char* label, EEditableRigChannel type, bool exists)
        {
            if (exists)
            {
                ImGui::BeginDisabled();
            }
            const bool pressed = ImGui::SmallButton(label);
            if (exists)
            {
                ImGui::EndDisabled();
            }
            if (pressed)
            {
                FEditableRigChannel channel;
                channel.bone = workbenchBone_;
                channel.type = type;
                channel.keys.push_back({rigPreview_.CurrentTime(),
                                        type == EEditableRigChannel::Scale ? glm::vec3(1.0f) : glm::vec3(0.0f)});
                clip.channels.push_back(std::move(channel));
                timelineSelectedChannel_ = static_cast<int>(clip.channels.size()) - 1;
                timelineSelectedKey_ = 0;
                edited = true;
            }
        };

        ImGui::TextDisabled("添加轨道");
        ImGui::SameLine();
        addChannel("+ pos", EEditableRigChannel::Position, hasPosition);
        ImGui::SameLine();
        addChannel("+ rot", EEditableRigChannel::Rotation, hasRotation);
        ImGui::SameLine();
        addChannel("+ scale", EEditableRigChannel::Scale, hasScale);

        if (timelineSelectedChannel_ >= static_cast<int>(clip.channels.size()))
        {
            timelineSelectedChannel_ = -1;
            timelineSelectedKey_ = -1;
        }
        if (timelineSelectedChannel_ >= 0 && clip.channels[timelineSelectedChannel_].bone != workbenchBone_)
        {
            timelineSelectedChannel_ = -1;
            timelineSelectedKey_ = -1;
        }

        std::vector<int> visibleChannels;
        for (int channelIndex = 0; channelIndex < static_cast<int>(clip.channels.size()); ++channelIndex)
        {
            if (clip.channels[channelIndex].bone == workbenchBone_)
            {
                visibleChannels.push_back(channelIndex);
            }
        }

        ImGui::SameLine();
        const bool hasSelectedTrack = timelineSelectedChannel_ >= 0;
        if (!hasSelectedTrack)
        {
            ImGui::BeginDisabled();
        }
        if (ImGui::SmallButton(ICON_FA_PLUS " 关键帧"))
        {
            FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
            channel.keys.push_back({rigPreview_.CurrentTime(), sampleChannel(channel, rigPreview_.CurrentTime())});
            timelineSelectedKey_ = static_cast<int>(channel.keys.size()) - 1;
            edited = true;
        }
        if (!hasSelectedTrack)
        {
            ImGui::EndDisabled();
        }

        timelineVisibleDuration_ = std::max(timelineVisibleDuration_, std::max(clip.duration * 1.1f, 0.1f));
        const float duration = timelineVisibleDuration_;
        const float rowHeight = 28.0f;
        const float rulerHeight = 30.0f;
        const float labelWidth = 104.0f;
        const ImU32 rulerColor = ImGui::GetColorU32(ImGuiCol_FrameBg);
        const ImU32 rowColor = ImGui::GetColorU32(ImGuiCol_TableRowBg);
        const ImU32 alternateRowColor = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt);
        const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_Border, 0.46f);
        const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32 accentColor = ImGui::GetColorU32(NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover));
        const ImU32 selectedColor = ImGui::GetColorU32(ImGuiCol_Text);

        const ImVec2 rulerPos = ImGui::GetCursorScreenPos();
        const float totalWidth = ImGui::GetContentRegionAvail().x;
        const float timeWidth = std::max(totalWidth - labelWidth, 80.0f);
        ImGui::InvisibleButton("##timeline_ruler", ImVec2(totalWidth, rulerHeight));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(rulerPos, ImVec2(rulerPos.x + totalWidth, rulerPos.y + rulerHeight), rulerColor);
        drawList->AddLine(ImVec2(rulerPos.x + labelWidth, rulerPos.y),
                          ImVec2(rulerPos.x + labelWidth, rulerPos.y + rulerHeight), gridColor);
        drawList->AddText(ImVec2(rulerPos.x + 7.0f, rulerPos.y + 7.0f), textColor, "属性轨道");

        const float roughStep = duration / std::max(2.0f, std::floor(timeWidth / 75.0f));
        const float tickOptions[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
        float tickStep = tickOptions[std::size(tickOptions) - 1];
        for (float option : tickOptions)
        {
            if (option >= roughStep)
            {
                tickStep = option;
                break;
            }
        }
        for (float tick = 0.0f; tick <= duration + tickStep * 0.5f; tick += tickStep)
        {
            const float x = rulerPos.x + labelWidth + (tick / duration) * timeWidth;
            drawList->AddLine(ImVec2(x, rulerPos.y + rulerHeight - 8.0f), ImVec2(x, rulerPos.y + rulerHeight),
                              gridColor);
            drawList->AddText(ImVec2(x + 3.0f, rulerPos.y + 5.0f), textColor, fmt::format("{:.2f}", tick).c_str());
        }
        const float currentTime = std::clamp(rigPreview_.CurrentTime(), 0.0f, duration);
        const float rulerPlayheadX = rulerPos.x + labelWidth + (currentTime / duration) * timeWidth;
        drawList->AddTriangleFilled(ImVec2(rulerPlayheadX - 5.0f, rulerPos.y),
                                    ImVec2(rulerPlayheadX + 5.0f, rulerPos.y),
                                    ImVec2(rulerPlayheadX, rulerPos.y + 8.0f), accentColor);
        drawList->AddLine(ImVec2(rulerPlayheadX, rulerPos.y + 8.0f), ImVec2(rulerPlayheadX, rulerPos.y + rulerHeight),
                          accentColor, 2.0f);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            ImGui::GetIO().MousePos.x >= rulerPos.x + labelWidth)
        {
            const float time =
                std::clamp((ImGui::GetIO().MousePos.x - rulerPos.x - labelWidth) / timeWidth, 0.0f, 1.0f) * duration;
            rigPreview_.SetPaused(true);
            rigPreview_.SetCurrentTime(time);
        }

        const float detailHeight = timelineSelectedChannel_ >= 0 && timelineSelectedKey_ >= 0 ? 102.0f : 34.0f;
        const float timelineHeight = std::max(88.0f, ImGui::GetContentRegionAvail().y - detailHeight);
        ImGui::BeginChild("##timeline_tracks", ImVec2(0.0f, timelineHeight), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const float canvasWidth = ImGui::GetContentRegionAvail().x;
        const float canvasTimeWidth = std::max(canvasWidth - labelWidth, 80.0f);
        const float canvasHeight =
            std::max(ImGui::GetContentRegionAvail().y, rowHeight * static_cast<float>(visibleChannels.size()));
        ImGui::InvisibleButton("##timeline_canvas", ImVec2(canvasWidth, canvasHeight));
        drawList = ImGui::GetWindowDrawList();

        for (int rowIndex = 0; rowIndex < static_cast<int>(visibleChannels.size()); ++rowIndex)
        {
            const int channelIndex = visibleChannels[rowIndex];
            const FEditableRigChannel& channel = clip.channels[channelIndex];
            const float y = canvasPos.y + rowIndex * rowHeight;
            const bool selectedTrack = channelIndex == timelineSelectedChannel_;
            drawList->AddRectFilled(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasWidth, y + rowHeight),
                                    selectedTrack ? ImGui::GetColorU32(ImGuiCol_Header)
                                                  : (rowIndex % 2 == 0 ? rowColor : alternateRowColor));
            drawList->AddLine(ImVec2(canvasPos.x, y + rowHeight), ImVec2(canvasPos.x + canvasWidth, y + rowHeight),
                              gridColor);
            drawList->AddLine(ImVec2(canvasPos.x + labelWidth, y), ImVec2(canvasPos.x + labelWidth, y + rowHeight),
                              gridColor);

            const std::string label = FCharacterWorkbench::ChannelName(channel.type);
            drawList->PushClipRect(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + labelWidth - 4.0f, y + rowHeight),
                                   true);
            drawList->AddText(ImVec2(canvasPos.x + 7.0f, y + 6.0f), selectedTrack ? selectedColor : textColor,
                              label.c_str());
            drawList->PopClipRect();

            for (float tick = 0.0f; tick <= duration + tickStep * 0.5f; tick += tickStep)
            {
                const float x = canvasPos.x + labelWidth + (tick / duration) * canvasTimeWidth;
                drawList->AddLine(ImVec2(x, y), ImVec2(x, y + rowHeight), gridColor);
            }
            for (int keyIndex = 0; keyIndex < static_cast<int>(channel.keys.size()); ++keyIndex)
            {
                const FEditableRigKey& key = channel.keys[keyIndex];
                const float x =
                    canvasPos.x + labelWidth + (std::clamp(key.time, 0.0f, duration) / duration) * canvasTimeWidth;
                const float centerY = y + rowHeight * 0.5f;
                const bool selectedKey = channelIndex == timelineSelectedChannel_ && keyIndex == timelineSelectedKey_;
                const ImU32 keyColor = selectedKey ? selectedColor : accentColor;
                const ImVec2 points[] = {ImVec2(x, centerY - 6.0f), ImVec2(x + 6.0f, centerY),
                                         ImVec2(x, centerY + 6.0f), ImVec2(x - 6.0f, centerY)};
                drawList->AddConvexPolyFilled(points, 4, keyColor);
            }
        }

        const float bodyPlayheadX = canvasPos.x + labelWidth + (currentTime / duration) * canvasTimeWidth;
        drawList->AddLine(ImVec2(bodyPlayheadX, canvasPos.y), ImVec2(bodyPlayheadX, canvasPos.y + canvasHeight),
                          accentColor, 2.0f);

        const bool timelineHovered = ImGui::IsItemHovered();
        if (timelineHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int row = static_cast<int>(std::floor((mouse.y - canvasPos.y) / rowHeight));
            if (row >= 0 && row < static_cast<int>(visibleChannels.size()))
            {
                timelineSelectedChannel_ = visibleChannels[row];
                timelineSelectedKey_ = -1;
                if (mouse.x >= canvasPos.x + labelWidth)
                {
                    const float clickedTime =
                        std::clamp((mouse.x - canvasPos.x - labelWidth) / canvasTimeWidth, 0.0f, 1.0f) * duration;
                    float nearestDistance = 9.0f;
                    for (int keyIndex = 0;
                         keyIndex < static_cast<int>(clip.channels[timelineSelectedChannel_].keys.size()); ++keyIndex)
                    {
                        const float keyX = canvasPos.x + labelWidth +
                            (clip.channels[timelineSelectedChannel_].keys[keyIndex].time / duration) * canvasTimeWidth;
                        const float distance = std::abs(mouse.x - keyX);
                        if (distance < nearestDistance)
                        {
                            nearestDistance = distance;
                            timelineSelectedKey_ = keyIndex;
                        }
                    }
                    if (timelineSelectedKey_ >= 0)
                    {
                        timelineDraggingKey_ = true;
                        rigPreview_.SetPaused(true);
                        rigPreview_.SetCurrentTime(
                            clip.channels[timelineSelectedChannel_].keys[timelineSelectedKey_].time);
                    }
                    else
                    {
                        rigPreview_.SetPaused(true);
                        rigPreview_.SetCurrentTime(clickedTime);
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
                            channel.keys.push_back({clickedTime, sampleChannel(channel, clickedTime)});
                            timelineSelectedKey_ = static_cast<int>(channel.keys.size()) - 1;
                            edited = true;
                        }
                    }
                }
            }
        }

        if (timelineDraggingKey_ && timelineSelectedChannel_ >= 0 && timelineSelectedKey_ >= 0)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
                if (timelineSelectedKey_ < static_cast<int>(channel.keys.size()))
                {
                    const float draggedTime =
                        std::clamp((ImGui::GetIO().MousePos.x - canvasPos.x - labelWidth) / canvasTimeWidth, 0.0f,
                                   1.0f) *
                        duration;
                    channel.keys[timelineSelectedKey_].time = draggedTime;
                    rigPreview_.SetCurrentTime(draggedTime);
                    edited = true;
                }
            }
            else
            {
                timelineDraggingKey_ = false;
            }
        }
        ImGui::EndChild();

        if (timelineSelectedChannel_ >= 0)
        {
            FEditableRigChannel& channel = clip.channels[timelineSelectedChannel_];
            ImGui::Text("%s / %s", rigPreview_.Asset().bones[channel.bone].name.c_str(),
                        FCharacterWorkbench::ChannelName(channel.type));
            ImGui::SameLine();
            if (timelineSelectedKey_ >= 0 && timelineSelectedKey_ < static_cast<int>(channel.keys.size()))
            {
                ImGui::TextDisabled("关键帧 %d", timelineSelectedKey_ + 1);
                FEditableRigKey& key = channel.keys[timelineSelectedKey_];
                ImGui::SetNextItemWidth(112.0f);
                if (ImGui::DragFloat("时间##selected_key", &key.time, 0.005f, 0.0f,
                                     std::max(clip.duration + 2.0f, 2.0f), "%.3f s"))
                {
                    rigPreview_.SetPaused(true);
                    rigPreview_.SetCurrentTime(key.time);
                    edited = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH " 删除关键帧"))
                {
                    channel.keys.erase(channel.keys.begin() + timelineSelectedKey_);
                    timelineSelectedKey_ = -1;
                    edited = true;
                }
                const float dragSpeed = channel.type == EEditableRigChannel::Rotation ? 0.25f : 0.005f;
                const char* format = channel.type == EEditableRigChannel::Rotation ? "%.2f deg" : "%.4f";
                if (timelineSelectedKey_ >= 0 &&
                    ImGui::DragFloat3("值##selected_key", &channel.keys[timelineSelectedKey_].value.x, dragSpeed, 0.0f,
                                      0.0f, format))
                {
                    edited = true;
                }
            }
            else
            {
                ImGui::TextDisabled("点击菱形关键帧；双击轨道空白处创建");
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_TRASH " 删除轨道"))
                {
                    clip.channels.erase(clip.channels.begin() + timelineSelectedChannel_);
                    timelineSelectedChannel_ = -1;
                    timelineSelectedKey_ = -1;
                    edited = true;
                }
            }
        }

        if (edited)
        {
            float selectedTime = -1.0f;
            if (timelineSelectedChannel_ >= 0 && timelineSelectedChannel_ < static_cast<int>(clip.channels.size()) &&
                timelineSelectedKey_ >= 0 &&
                timelineSelectedKey_ < static_cast<int>(clip.channels[timelineSelectedChannel_].keys.size()))
            {
                selectedTime = clip.channels[timelineSelectedChannel_].keys[timelineSelectedKey_].time;
            }
            ApplyWorkbenchRigEdit();
            if (selectedTime >= 0.0f && timelineSelectedChannel_ >= 0 &&
                timelineSelectedChannel_ < static_cast<int>(clip.channels.size()))
            {
                const std::vector<FEditableRigKey>& keys = clip.channels[timelineSelectedChannel_].keys;
                float nearest = std::numeric_limits<float>::max();
                for (int index = 0; index < static_cast<int>(keys.size()); ++index)
                {
                    const float distance = std::abs(keys[index].time - selectedTime);
                    if (distance < nearest)
                    {
                        nearest = distance;
                        timelineSelectedKey_ = index;
                    }
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void ScadLibraryInterface::DrawEquipmentEditor()
    {
        std::vector<FEquipmentAttachment>& equipment = workbench_.Equipment();
        int removeIndex = -1;
        int duplicateIndex = -1;
        bool structuralEdit = false;
        bool metadataEdit = false;

        ImGui::TextDisabled("任意 kit 模块 -> 任意骨架；变换采用 SCAD 本地坐标。");
        ImGui::BeginChild("##equipment_list", ImVec2(0.0f, -38.0f), ImGuiChildFlags_None);
        for (int index = 0; index < static_cast<int>(equipment.size()); ++index)
        {
            FEquipmentAttachment& attachment = equipment[index];
            ImGui::PushID(index);
            const bool open = ImGui::TreeNodeEx(
                fmt::format("{}##equipment", attachment.label.empty() ? attachment.id : attachment.label).c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding);
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 54.0f);
            if (ImGui::SmallButton(ICON_FA_COPY "##duplicate"))
            {
                duplicateIndex = index;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_TRASH "##remove"))
            {
                removeIndex = index;
            }
            if (open)
            {
                if (ImGui::Checkbox("启用", &attachment.enabled))
                {
                    structuralEdit = true;
                }
                if (ImGui::InputText("名称", &attachment.label) || ImGui::InputText("ID", &attachment.id))
                {
                    metadataEdit = true;
                }

                const char* currentKit = "（选择 kit）";
                int currentKitIndex = -1;
                for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
                {
                    if (kits_[kitIndex].filePath == attachment.kitPath)
                    {
                        currentKit = kits_[kitIndex].name.c_str();
                        currentKitIndex = kitIndex;
                        break;
                    }
                }
                if (ImGui::BeginCombo("来源 kit", currentKit))
                {
                    for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
                    {
                        if (ImGui::Selectable(kits_[kitIndex].name.c_str(), kitIndex == currentKitIndex))
                        {
                            attachment.kitPath = kits_[kitIndex].filePath;
                            attachment.moduleName =
                                kits_[kitIndex].modules.empty() ? "" : kits_[kitIndex].modules.front().name;
                            currentKitIndex = kitIndex;
                            structuralEdit = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (currentKitIndex >= 0)
                {
                    if (ImGui::BeginCombo("模块", attachment.moduleName.c_str()))
                    {
                        for (const FKitModuleInfo& moduleInfo : kits_[currentKitIndex].modules)
                        {
                            if (ImGui::Selectable(moduleInfo.name.c_str(), moduleInfo.name == attachment.moduleName))
                            {
                                attachment.moduleName = moduleInfo.name;
                                structuralEdit = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                if (ImGui::InputTextWithHint("参数", "如 seed = 3", &attachment.arguments))
                {
                    structuralEdit = true;
                }

                const char* currentBone = attachment.bone.c_str();
                if (ImGui::BeginCombo("挂点骨架", currentBone))
                {
                    for (const Assets::FRigBone& bone : rigPreview_.Asset().bones)
                    {
                        if (ImGui::Selectable(bone.name.c_str(), bone.name == attachment.bone))
                        {
                            attachment.bone = bone.name;
                            structuralEdit = true;
                        }
                    }
                    ImGui::EndCombo();
                }

                bool transformEdit = false;
                transformEdit |= ImGui::DragFloat3("位置", &attachment.translation.x, 0.005f, 0.0f, 0.0f, "%.3f");
                transformEdit |=
                    ImGui::DragFloat3("旋转", &attachment.rotationDegrees.x, 0.25f, 0.0f, 0.0f, "%.2f deg");
                transformEdit |= ImGui::DragFloat3("缩放", &attachment.scale.x, 0.01f, 0.01f, 10.0f, "%.3f");
                if (transformEdit)
                {
                    workbench_.MarkEquipmentDirty();
                    rigPreview_.UpdateEquipmentTransform(static_cast<size_t>(index), attachment);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (removeIndex >= 0)
        {
            equipment.erase(equipment.begin() + removeIndex);
            structuralEdit = true;
        }
        if (duplicateIndex >= 0)
        {
            FEquipmentAttachment copy = equipment[duplicateIndex];
            copy.id += "_copy";
            copy.label += " 副本";
            equipment.push_back(std::move(copy));
            structuralEdit = true;
        }
        if (ImGui::Button(ICON_FA_PLUS " 添加装备"))
        {
            FEquipmentAttachment attachment;
            if (!kits_.empty())
            {
                attachment.kitPath = kits_.front().filePath;
                if (!kits_.front().modules.empty())
                {
                    attachment.moduleName = kits_.front().modules.front().name;
                }
            }
            equipment.push_back(std::move(attachment));
            structuralEdit = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新装备"))
        {
            structuralEdit = true;
        }

        if (structuralEdit || metadataEdit)
        {
            workbench_.MarkEquipmentDirty();
        }
        if (structuralEdit)
        {
            workbenchEquipmentRebuildRequested_ = true;
        }
    }
} // namespace ScadLibrary
