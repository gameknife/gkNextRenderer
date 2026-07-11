#include "ScadLibraryInterface.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_freetype.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <spdlog/spdlog.h>

namespace ScadLibrary
{
    namespace
    {
        constexpr float kTitleBarHeight = 44.0f;
        constexpr float kBottomBarHeight = 30.0f;
        constexpr float kCollapsedRailWidth = 46.0f;
        constexpr int kBenchGridColumns = 6;
        constexpr float kBenchGridStep = 14.0f;

        std::filesystem::path WorkspaceDir()
        {
            return std::filesystem::current_path() / "scad_library";
        }

        // The category token reads better with a friendly label where we have one.
        const char* CategoryLabel(const std::string& category)
        {
            if (category == "bldg") return "建筑";
            if (category == "block") return "街区";
            if (category == "furn") return "家具";
            if (category == "prop") return "道具";
            if (category == "nature") return "植被地景";
            if (category == "veh") return "载具";
            if (category == "boat") return "船只";
            if (category == "wall") return "墙体";
            if (category == "part") return "构件";
            if (category == "ground") return "地面";
            if (category == "road") return "道路";
            if (category == "misc") return "其他";
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
    } // namespace

    ScadLibraryInterface::ScadLibraryInterface(NextEngine& engine)
        : engine_(engine)
    {
        RescanKits();
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

        const std::string fontPath =
            Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf");
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr,
                                                    io.Fonts->GetGlyphRangesChineseFull());
        if (font == nullptr)
        {
            io.Fonts->AddFontDefault();
        }
    }

    void ScadLibraryInterface::Render()
    {
        // First frame: show something instead of an empty void.
        if (!welcomeLoaded_)
        {
            welcomeLoaded_ = true;
            for (size_t k = 0; k < kits_.size() && selectedModule_.empty(); ++k)
            {
                for (const FKitModuleInfo& moduleInfo : kits_[k].modules)
                {
                    if (moduleInfo.category == "block" || moduleInfo.category == "bldg")
                    {
                        PreviewModule(static_cast<int>(k), moduleInfo.name);
                        break;
                    }
                }
            }
            if (selectedModule_.empty() && !kits_.empty() && !kits_[0].modules.empty())
            {
                PreviewModule(0, kits_[0].modules[0].name);
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
        const float leftFullWidth = std::clamp(viewport->Size.x * 0.24f, 300.0f, 380.0f);
        const float rightFullWidth = std::clamp(viewport->Size.x * 0.28f, 340.0f, 460.0f);
        const float leftWidth = browserCollapsed_ ? kCollapsedRailWidth : leftFullWidth;
        const float rightWidth = benchCollapsed_ ? kCollapsedRailWidth : rightFullWidth;

        DrawBrowserPanel(ImVec2(viewport->Pos.x, panelY), ImVec2(leftWidth, panelHeight));
        DrawBenchPanel(ImVec2(viewport->Pos.x + viewport->Size.x - rightWidth, panelY),
                       ImVec2(rightWidth, panelHeight));

        // Deferred bench reload: wait until the drag/edit is released so the scene
        // is not rebuilt on every mouse-move.
        if (benchDirty_ && autoReload_ && !ImGui::IsAnyItemActive())
        {
            ReloadBench();
        }

        engine_.GetRenderer().SwapChain().UpdateOutputViewport(
            Utilities::Math::floorToInt(leftWidth),
            Utilities::Math::floorToInt(kTitleBarHeight),
            Utilities::Math::ceilToInt(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth)),
            Utilities::Math::ceilToInt(panelHeight));
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
                if (ImGui::MenuItem("Rescan Kits", "F5"))
                {
                    RescanKits();
                }
                if (ImGui::MenuItem("Export Bench", nullptr, false, !bench_.empty()))
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
                bool browserOpen = !browserCollapsed_;
                bool benchOpen = !benchCollapsed_;
                if (ImGui::MenuItem("Kit Browser", nullptr, browserOpen))
                {
                    browserCollapsed_ = !browserCollapsed_;
                }
                if (ImGui::MenuItem("Compose Bench", nullptr, benchOpen))
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
                ImGui::TextDisabled("点击零件预览；\"+\" 加入组合台");
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
        ImGui::TextUnformatted("零件库");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "##rescan", ImVec2(30.0f, 30.0f)))
        {
            RescanKits();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("重新扫描 assets/scad/lib");
        }

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
                const bool isSelected =
                    selectedKit_ == static_cast<int>(k) && selectedModule_ == moduleInfo.name;
                if (ImGui::Selectable(fmt::format("{}##sel_{}_{}", moduleInfo.name, k, moduleInfo.name).c_str(),
                                      isSelected, 0,
                                      ImVec2(ImGui::GetContentRegionAvail().x - 34.0f, 0.0f)))
                {
                    PreviewModule(static_cast<int>(k), moduleInfo.name);
                }
                if (ImGui::IsItemHovered())
                {
                    if (moduleInfo.hasMetrics)
                    {
                        ImGui::SetTooltip("%s(%s)\n脚印 %.1f x %.1f  高 %.1f  三角形 %d",
                                          moduleInfo.name.c_str(), moduleInfo.params.c_str(),
                                          moduleInfo.footprintX, moduleInfo.footprintY, moduleInfo.height,
                                          moduleInfo.triangles);
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
                    ImGui::SetTooltip("加入组合台");
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

    void ScadLibraryInterface::DrawBenchPanel(const ImVec2& pos, const ImVec2& size)
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
        ImGui::Text("组合台 (%zu)", bench_.size());

        ImGui::Spacing();
        ImGui::Checkbox("自动刷新", &autoReload_);
        ImGui::SameLine();
        ImGui::Checkbox("地板", &showFloor_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::SliderInt("$fn", &fnSegments_, 6, 32))
        {
            benchDirty_ = true;
        }

        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新"))
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
        ImGui::BeginChild("##bench_list", ImVec2(0, -64.0f), ImGuiChildFlags_None);
        for (size_t i = 0; i < bench_.size(); ++i)
        {
            FBenchItem& benchItem = bench_[i];
            ImGui::PushID(static_cast<int>(i));
            const bool open = ImGui::TreeNodeEx(
                fmt::format("{} #{}", benchItem.moduleName, i).c_str(),
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding);
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
            ImGui::TextDisabled("从左侧零件库点 \"+\" 添加模块，");
            ImGui::TextDisabled("在这里摆位置、转角度，组合成新场景。");
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
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 96.0f);
        ImGui::InputTextWithHint("##export_name", "导出文件名", exportNameBuf_, sizeof(exportNameBuf_));
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_EXPORT " 导出") && !bench_.empty())
        {
            ExportBench();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("写入 assets/scad/gen/<名>.scad（use 相对路径，可直接作为场景加载）");
        }
        ImGui::End();
    }

    void ScadLibraryInterface::RescanKits()
    {
        const std::string libDir = Utilities::FileHelper::GetPlatformFilePath("assets/scad/lib");
        bool fromCatalog = false;
        kits_ = LoadKits(libDir, fromCatalog);
        size_t moduleCount = 0;
        for (const FKitInfo& kit : kits_)
        {
            moduleCount += kit.modules.size();
        }
        statusLine_ = fmt::format("{} {} 个 kit / {} 个模块", fromCatalog ? "catalog.json:" : "文本扫描:",
                                  kits_.size(), moduleCount);
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
        bench_.erase(std::remove_if(bench_.begin(), bench_.end(),
                                    [](const FBenchItem& b) { return b.kitIndex < 0; }),
                     bench_.end());
    }

    void ScadLibraryInterface::PreviewModule(int kitIndex, const std::string& moduleName)
    {
        if (kitIndex < 0 || kitIndex >= static_cast<int>(kits_.size()))
        {
            return;
        }
        selectedKit_ = kitIndex;
        selectedModule_ = moduleName;

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

    std::string ScadLibraryInterface::BuildBenchSource(bool relativeUses) const
    {
        std::string source;
        source += "// generated by ScadLibrary compose bench\n";
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
            std::string usePath;
            if (relativeUses)
            {
                usePath = "../lib/" + std::filesystem::path(kits_[kitIndex].filePath).filename().string();
            }
            else
            {
                usePath = kits_[kitIndex].filePath;
                std::replace(usePath.begin(), usePath.end(), '\\', '/');
            }
            source += fmt::format("use <{}>\n", usePath);
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
            source += fmt::format("translate([{:.2f}, {:.2f}, 0]) rotate([0, 0, {:.1f}]) scale([{:.3f}, {:.3f}, {:.3f}]) {}({});\n",
                                  benchItem.x, benchItem.y, benchItem.rotZ,
                                  benchItem.scale, benchItem.scale, benchItem.scale,
                                  benchItem.moduleName, args);
        }
        return source;
    }

    void ScadLibraryInterface::ReloadBench()
    {
        benchDirty_ = false;
        if (bench_.empty())
        {
            statusLine_ = "组合台为空";
            statusError_ = false;
            return;
        }
        if (WriteAndLoad("bench.scad", BuildBenchSource(false)))
        {
            statusLine_ = fmt::format("组合台 {} 件已重载", bench_.size());
            statusError_ = false;
        }
    }

    void ScadLibraryInterface::ExportBench()
    {
        std::string name(exportNameBuf_);
        if (name.empty())
        {
            name = "bench_export";
        }
        const std::filesystem::path genDir = Utilities::FileHelper::GetPlatformFilePath("assets/scad") +
            std::string(1, std::filesystem::path::preferred_separator) + "gen";
        std::error_code ec;
        std::filesystem::create_directories(genDir, ec);
        const std::filesystem::path outPath = genDir / (name + ".scad");
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            statusLine_ = fmt::format("导出失败: {}", outPath.string());
            statusError_ = true;
            return;
        }
        out << BuildBenchSource(true);
        statusLine_ = fmt::format("已导出 {}", outPath.string());
        statusError_ = false;
        SPDLOG_INFO("[ScadLibrary] exported bench -> {}", outPath.string());
    }

    bool ScadLibraryInterface::WriteAndLoad(const std::string& fileName, const std::string& source)
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
        engine_.RequestLoadScene({.filename = std::filesystem::absolute(path, ec).string()});
        return true;
    }
}
