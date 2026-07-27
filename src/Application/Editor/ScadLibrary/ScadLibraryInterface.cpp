#define GLM_ENABLE_EXPERIMENTAL
#include "ScadLibraryInterface.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Modules/ScadLoader/FScadEvaluator.h"
#include "Modules/ScadLoader/FScadShared.h"
#include "ThirdParty/ImGuizmo/ImGuizmo.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fstream>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
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

        glm::mat4 SceneObjectWorldMatrix(const FBenchItem& item)
        {
            const glm::dmat4 scadMatrix = glm::translate(glm::dmat4(1.0), glm::dvec3(item.x, item.y, item.z)) *
                Assets::Scad::ScadRotateXYZ(glm::dvec3(item.rotX, item.rotY, item.rotZ)) *
                glm::scale(glm::dmat4(1.0), glm::dvec3(item.scale, item.scaleY, item.scaleZ));
            const glm::dmat4 basis = Assets::Scad::ScadToWorldBasis(1.0);
            return glm::mat4(basis * scadMatrix * glm::inverse(basis));
        }

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

        std::string RewriteScadDependencyPaths(const std::string& source, const std::filesystem::path& sourceDir,
                                               const std::filesystem::path& targetDir, bool absolute)
        {
            static const std::regex useRegex(R"(((?:use|include)\s*<)([^>]+)(>))", std::regex_constants::icase);
            std::string result;
            std::string::const_iterator cursor = source.begin();
            std::smatch match;
            while (std::regex_search(cursor, source.end(), match, useRegex))
            {
                result.append(cursor, match[0].first);
                std::filesystem::path dependencyPath(match[2].str());
                if (!dependencyPath.is_absolute())
                {
                    dependencyPath = sourceDir / dependencyPath;
                }
                dependencyPath = dependencyPath.lexically_normal();
                if (!absolute)
                {
                    const std::filesystem::path relative = dependencyPath.lexically_relative(targetDir);
                    if (!relative.empty())
                    {
                        dependencyPath = relative;
                    }
                }
                result += match[1].str();
                result += dependencyPath.generic_string();
                result += match[3].str();
                cursor = match[0].second;
            }
            result.append(cursor, source.end());
            return result;
        }

        std::string ScadValueSource(const Assets::Scad::Value& value)
        {
            using Value = Assets::Scad::Value;
            switch (value.type)
            {
            case Value::Type::Number:
                return fmt::format("{:.9g}", value.num);
            case Value::Type::Bool:
                return value.boolean ? "true" : "false";
            case Value::Type::Str:
                {
                    std::string escaped;
                    escaped.reserve(value.str.size() + 2);
                    for (const char character : value.str)
                    {
                        if (character == '\\' || character == '"')
                        {
                            escaped.push_back('\\');
                        }
                        escaped.push_back(character);
                    }
                    return fmt::format("\"{}\"", escaped);
                }
            case Value::Type::Vec:
                {
                    std::string result = "[";
                    for (size_t index = 0; index < value.vec.size(); ++index)
                    {
                        if (index > 0)
                        {
                            result += ", ";
                        }
                        result += ScadValueSource(value.vec[index]);
                    }
                    result += "]";
                    return result;
                }
            case Value::Type::Range:
                return fmt::format("[{:.9g}:{:.9g}:{:.9g}]", value.rangeBegin, value.rangeStep, value.rangeEnd);
            default:
                return "undef";
            }
        }

        std::vector<std::string> ExtractTerrainSources(const std::string& source)
        {
            // Preserve offsets while masking comments and strings so token
            // positions still address the original source.
            std::string searchable = source;
            bool lineComment = false;
            bool blockComment = false;
            bool stringLiteral = false;
            bool escaped = false;
            for (size_t index = 0; index < searchable.size(); ++index)
            {
                const char character = searchable[index];
                const char next = index + 1 < searchable.size() ? searchable[index + 1] : '\0';
                if (lineComment)
                {
                    if (character == '\n')
                    {
                        lineComment = false;
                    }
                    else
                    {
                        searchable[index] = ' ';
                    }
                    continue;
                }
                if (blockComment)
                {
                    searchable[index] = character == '\n' ? '\n' : ' ';
                    if (character == '*' && next == '/')
                    {
                        searchable[index + 1] = ' ';
                        ++index;
                        blockComment = false;
                    }
                    continue;
                }
                if (stringLiteral)
                {
                    if (!escaped && character == '"')
                    {
                        stringLiteral = false;
                    }
                    else
                    {
                        searchable[index] = character == '\n' ? '\n' : ' ';
                    }
                    escaped = !escaped && character == '\\';
                    if (character != '\\')
                    {
                        escaped = false;
                    }
                    continue;
                }
                if (character == '/' && next == '/')
                {
                    searchable[index] = searchable[index + 1] = ' ';
                    ++index;
                    lineComment = true;
                }
                else if (character == '/' && next == '*')
                {
                    searchable[index] = searchable[index + 1] = ' ';
                    ++index;
                    blockComment = true;
                }
                else if (character == '"')
                {
                    stringLiteral = true;
                }
            }

            std::vector<std::string> blocks;
            static const std::regex terrainCallRegex(R"(\bgk_terrain\s*\()");
            for (std::sregex_iterator it(searchable.begin(), searchable.end(), terrainCallRegex), terrainEnd;
                 it != terrainEnd; ++it)
            {
                const size_t callPosition = static_cast<size_t>(it->position());
                const size_t openParen = searchable.find('(', callPosition);
                size_t closeParen = std::string::npos;
                int depth = 0;
                for (size_t cursor = openParen; cursor < searchable.size(); ++cursor)
                {
                    if (searchable[cursor] == '(')
                    {
                        ++depth;
                    }
                    else if (searchable[cursor] == ')' && --depth == 0)
                    {
                        closeParen = cursor;
                        break;
                    }
                }
                if (closeParen == std::string::npos)
                {
                    continue;
                }
                size_t statementEnd = closeParen + 1;
                while (statementEnd < searchable.size() &&
                       std::isspace(static_cast<unsigned char>(searchable[statementEnd])))
                {
                    ++statementEnd;
                }
                if (statementEnd < searchable.size() && searchable[statementEnd] == ';')
                {
                    ++statementEnd;
                }

                const size_t previousSemicolon =
                    callPosition == 0 ? std::string::npos : searchable.rfind(';', callPosition - 1);
                size_t statementStart = previousSemicolon == std::string::npos ? 0 : previousSemicolon + 1;
                while (statementStart < callPosition &&
                       std::isspace(static_cast<unsigned char>(source[statementStart])))
                {
                    ++statementStart;
                }

                size_t argumentStart = openParen + 1;
                size_t argumentEnd = closeParen;
                while (argumentStart < argumentEnd && std::isspace(static_cast<unsigned char>(source[argumentStart])))
                {
                    ++argumentStart;
                }
                while (argumentEnd > argumentStart && std::isspace(static_cast<unsigned char>(source[argumentEnd - 1])))
                {
                    --argumentEnd;
                }
                const std::string argument = source.substr(argumentStart, argumentEnd - argumentStart);

                std::string block;
                static const std::regex identifierRegex(R"([A-Za-z_][A-Za-z0-9_]*)");
                if (std::regex_match(argument, identifierRegex))
                {
                    const std::regex assignmentRegex(fmt::format(R"((?:^|[;\n])\s*{}\s*=)", argument),
                                                     std::regex_constants::ECMAScript);
                    const std::string prefix = searchable.substr(0, callPosition);
                    size_t assignmentStart = std::string::npos;
                    size_t assignmentEnd = std::string::npos;
                    for (std::sregex_iterator assignment(prefix.begin(), prefix.end(), assignmentRegex),
                         assignmentEndIt;
                         assignment != assignmentEndIt; ++assignment)
                    {
                        assignmentStart = static_cast<size_t>(assignment->position());
                        if (prefix[assignmentStart] == ';' || prefix[assignmentStart] == '\n')
                        {
                            ++assignmentStart;
                        }
                        assignmentEnd = searchable.find(';', assignmentStart);
                    }
                    if (assignmentStart != std::string::npos && assignmentEnd != std::string::npos)
                    {
                        block = source.substr(assignmentStart, assignmentEnd - assignmentStart + 1);
                        block += "\n";
                    }
                }
                block += source.substr(statementStart, statementEnd - statementStart);
                if (std::find(blocks.begin(), blocks.end(), block) == blocks.end())
                {
                    blocks.push_back(std::move(block));
                }
            }
            return blocks;
        }
    } // namespace

    ScadLibraryInterface::ScadLibraryInterface(NextEngine& engine, std::string startupAssemblyPath) :
        engine_(engine), startupAssemblyPath_(std::move(startupAssemblyPath))
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
            if (assemblyProcedural_)
            {
                ReloadTerrainProcess();
            }
            else if (!bench_.empty())
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
            const bool openedStartup = !startupAssemblyPath_.empty() &&
                std::filesystem::path(startupAssemblyPath_).extension() == ".scad" &&
                OpenAssembly(startupAssemblyPath_);
            if (!openedStartup)
            {
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
        else if (composeMode)
        {
            const ImVec2 sceneViewportPos(viewport->Pos.x + leftWidth, panelY);
            const ImVec2 sceneViewportSize(std::max(1.0f, viewport->Size.x - leftWidth - rightWidth), panelHeight);
            if (assemblyProcedural_)
            {
                DrawTerrainFeatureToolbar(sceneViewportPos);
                DrawTerrainFeatureOverlay(sceneViewportPos, sceneViewportSize);
            }
            else
            {
                DrawSceneGizmoToolbar(sceneViewportPos);
                DrawSceneObjectGizmo(sceneViewportPos, sceneViewportSize);
            }
        }

        // Deferred bench reload: wait until the drag/edit is released so the scene
        // is not rebuilt on every mouse-move.
        if (composeMode && assemblyProcedural_ && terrainProcessDirty_ && autoReload_ && !ImGui::IsAnyItemActive() &&
            !terrainFeatureDragging_ && !terrainRuleDragging_)
        {
            ReloadTerrainProcess();
        }
        else if (composeMode && benchDirty_ && autoReload_ && !ImGui::IsAnyItemActive())
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
            if (assemblyProcedural_)
            {
                ImGui::Text("过程场景  ·  %zu 特征 / %zu 规则", terrainProcess_.Terrain().features.size(),
                            terrainProcess_.ActiveRuleCount());
            }
            else
            {
                ImGui::Text("场景组装  ·  %zu 对象", bench_.size());
            }
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

    void ScadLibraryInterface::DrawTerrainProcessContent()
    {
        bool changed = false;
        Assets::Scad::FTerrainSpec& terrain = terrainProcess_.Terrain();

        ImGui::Checkbox("自动刷新", &autoReload_);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT " 刷新预览"))
        {
            ReloadTerrainProcess();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("只改写 TERR 与已识别的 ter_* 语句");

        for (const std::string& warning : terrainProcessWarnings_)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
            ImGui::TextWrapped("%s", warning.c_str());
            ImGui::PopStyleColor();
        }

        const auto editPoint = [&](const char* label, glm::dvec2& point, float speed = 0.5f)
        {
            double value[2] = {point.x, point.y};
            if (ImGui::DragScalarN(label, ImGuiDataType_Double, value, 2, speed))
            {
                point = glm::dvec2(value[0], value[1]);
                return true;
            }
            return false;
        };
        const auto editNumber = [](const char* label, double& value, float speed = 0.1f, const char* format = "%.3f")
        { return ImGui::DragScalar(label, ImGuiDataType_Double, &value, speed, nullptr, nullptr, format); };
        const auto editPoints = [&](std::vector<glm::dvec2>& points)
        {
            bool pointsChanged = false;
            int removePoint = -1;
            for (size_t pointIndex = 0; pointIndex < points.size(); ++pointIndex)
            {
                ImGui::PushID(static_cast<int>(pointIndex));
                ImGui::SetNextItemWidth(-34.0f);
                pointsChanged |= editPoint("##point", points[pointIndex]);
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK))
                {
                    removePoint = static_cast<int>(pointIndex);
                }
                ImGui::PopID();
            }
            if (removePoint >= 0 && points.size() > 2)
            {
                points.erase(points.begin() + removePoint);
                pointsChanged = true;
            }
            if (ImGui::SmallButton(ICON_FA_PLUS " 添加折点"))
            {
                points.push_back(points.empty() ? glm::dvec2(0.0) : points.back() + glm::dvec2(5.0, 0.0));
                pointsChanged = true;
            }
            return pointsChanged;
        };

        if (ImGui::CollapsingHeader("地形画布", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= editPoint("尺寸 XY", terrain.size, 1.0f);
            changed |= ImGui::DragInt2("网格 cells", &terrain.cells.x, 1.0f, 4, 256);
            changed |= ImGui::InputScalar("Seed", ImGuiDataType_U64, &terrain.seed);
            changed |= editNumber("基准高度", terrain.baseHeight);
            changed |= editNumber("基础起伏", terrain.relief);
            changed |= editNumber("噪声细节", terrain.roughness, 0.01f);
            changed |= ImGui::InputText("调色板", &terrain.palette);
            if (ImGui::Checkbox("全局水位", &terrain.hasWaterLevel))
            {
                changed = true;
            }
            if (terrain.hasWaterLevel)
            {
                changed |= editNumber("水位高度", terrain.waterLevel);
            }
        }

        if (ImGui::CollapsingHeader("Terrain Features（按顺序作用）", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int removeFeature = -1;
            int moveFeature = -1;
            int moveDirection = 0;
            for (size_t featureIndex = 0; featureIndex < terrain.features.size(); ++featureIndex)
            {
                Assets::Scad::FTerrainFeature& feature = terrain.features[featureIndex];
                ImGui::PushID(static_cast<int>(featureIndex));
                const std::string label =
                    fmt::format("{:02}  {}", featureIndex + 1, FTerrainProcessDocument::FeatureTypeName(feature.type));
                const bool selected =
                    !terrainSelectionIsRule_ && selectedTerrainFeature_ == static_cast<int>(featureIndex);
                ImGuiTreeNodeFlags featureFlags = ImGuiTreeNodeFlags_AllowOverlap;
                if (selected)
                {
                    featureFlags |= ImGuiTreeNodeFlags_Selected;
                }
                ImGui::SetNextItemOpen(selected, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx(label.c_str(), featureFlags);
                if (ImGui::IsItemClicked())
                {
                    selectedTerrainFeature_ = static_cast<int>(featureIndex);
                    terrainSelectionIsRule_ = false;
                }
                if (selected && scrollToSelectedTerrainItem_)
                {
                    ImGui::SetScrollHereY(0.35f);
                    scrollToSelectedTerrainItem_ = false;
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 76.0f);
                if (ImGui::SmallButton(ICON_FA_ANGLE_UP) && featureIndex > 0)
                {
                    moveFeature = static_cast<int>(featureIndex);
                    moveDirection = -1;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_ANGLE_DOWN) && featureIndex + 1 < terrain.features.size())
                {
                    moveFeature = static_cast<int>(featureIndex);
                    moveDirection = 1;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK))
                {
                    removeFeature = static_cast<int>(featureIndex);
                }
                if (open)
                {
                    using EType = Assets::Scad::FTerrainFeature::EType;
                    switch (feature.type)
                    {
                    case EType::Mountain:
                        changed |= editPoint("中心", feature.at);
                        changed |= editNumber("半径", feature.radius);
                        changed |= editNumber("高度", feature.height);
                        changed |= editNumber("扰动强度", feature.rugged, 0.01f);
                        break;
                    case EType::Ridge:
                        ImGui::TextDisabled("山脊折线");
                        changed |= editPoints(feature.pts);
                        changed |= editNumber("宽度", feature.width);
                        changed |= editNumber("高度", feature.height);
                        break;
                    case EType::Plateau:
                        changed |= editPoint("中心", feature.at);
                        changed |= editNumber("半径", feature.radius);
                        changed |= editNumber("高度", feature.height);
                        break;
                    case EType::Lake:
                        changed |= editPoint("中心", feature.at);
                        changed |= editNumber("半径", feature.radius);
                        changed |= editNumber("深度", feature.depth);
                        break;
                    case EType::River:
                        ImGui::TextDisabled("河流折线（上游 → 下游）");
                        changed |= editPoints(feature.pts);
                        changed |= editNumber("宽度", feature.width);
                        changed |= editNumber("深度", feature.depth);
                        break;
                    case EType::Road:
                        ImGui::TextDisabled("道路折线");
                        changed |= editPoints(feature.pts);
                        changed |= editNumber("宽度", feature.width);
                        break;
                    case EType::Pad:
                        changed |= editPoint("中心", feature.at);
                        changed |= editPoint("尺寸", feature.size);
                        changed |= editNumber("旋转", feature.rot, 1.0f, "%.1f°");
                        break;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (moveFeature >= 0)
            {
                std::swap(terrain.features[moveFeature], terrain.features[moveFeature + moveDirection]);
                if (selectedTerrainFeature_ == moveFeature)
                {
                    selectedTerrainFeature_ += moveDirection;
                }
                else if (selectedTerrainFeature_ == moveFeature + moveDirection)
                {
                    selectedTerrainFeature_ = moveFeature;
                }
                changed = true;
            }
            if (removeFeature >= 0)
            {
                terrain.features.erase(terrain.features.begin() + removeFeature);
                if (selectedTerrainFeature_ > removeFeature)
                {
                    --selectedTerrainFeature_;
                }
                else if (selectedTerrainFeature_ == removeFeature)
                {
                    selectedTerrainFeature_ = std::min(removeFeature, static_cast<int>(terrain.features.size()) - 1);
                }
                changed = true;
            }

            if (ImGui::Button(ICON_FA_PLUS " 添加 Feature"))
            {
                ImGui::OpenPopup("##add_terrain_feature");
            }
            if (ImGui::BeginPopup("##add_terrain_feature"))
            {
                using EType = Assets::Scad::FTerrainFeature::EType;
                const auto addFeature = [&](const char* label, EType type)
                {
                    if (!ImGui::MenuItem(label))
                    {
                        return;
                    }
                    Assets::Scad::FTerrainFeature feature;
                    feature.type = type;
                    feature.radius = 20.0;
                    feature.width = 6.0;
                    feature.height = 8.0;
                    feature.depth = 2.0;
                    feature.rugged = 0.5;
                    feature.size = glm::dvec2(24.0, 18.0);
                    if (type == EType::Ridge || type == EType::River || type == EType::Road)
                    {
                        feature.pts = {{-10.0, 0.0}, {10.0, 0.0}};
                    }
                    terrain.features.push_back(std::move(feature));
                    selectedTerrainFeature_ = static_cast<int>(terrain.features.size()) - 1;
                    terrainSelectionIsRule_ = false;
                    scrollToSelectedTerrainItem_ = true;
                    changed = true;
                };
                addFeature("山峰 mountain", EType::Mountain);
                addFeature("山脊 ridge", EType::Ridge);
                addFeature("台地 plateau", EType::Plateau);
                addFeature("湖泊 lake", EType::Lake);
                addFeature("河流 river", EType::River);
                addFeature("道路 road", EType::Road);
                addFeature("基座 pad", EType::Pad);
                ImGui::EndPopup();
            }
        }

        if (ImGui::CollapsingHeader("贴地过程规则", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int removeRule = -1;
            int duplicateRule = -1;
            std::vector<FTerrainProcessRule>& rules = terrainProcess_.Rules();
            for (size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex)
            {
                FTerrainProcessRule& rule = rules[ruleIndex];
                if (rule.removed)
                {
                    continue;
                }
                ImGui::PushID(static_cast<int>(ruleIndex));
                const std::string label =
                    fmt::format("{:02}  {}", ruleIndex + 1, FTerrainProcessDocument::RuleTypeName(rule.type));
                const bool selected = terrainSelectionIsRule_ && selectedTerrainRule_ == static_cast<int>(ruleIndex);
                ImGuiTreeNodeFlags ruleFlags = ImGuiTreeNodeFlags_AllowOverlap;
                if (selected)
                {
                    ruleFlags |= ImGuiTreeNodeFlags_Selected;
                }
                ImGui::SetNextItemOpen(selected, ImGuiCond_Always);
                const bool open = ImGui::TreeNodeEx(label.c_str(), ruleFlags);
                if (ImGui::IsItemClicked())
                {
                    selectedTerrainRule_ = static_cast<int>(ruleIndex);
                    terrainSelectionIsRule_ = true;
                }
                if (selected && scrollToSelectedTerrainItem_)
                {
                    ImGui::SetScrollHereY(0.35f);
                    scrollToSelectedTerrainItem_ = false;
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 52.0f);
                if (ImGui::SmallButton(ICON_FA_COPY))
                {
                    duplicateRule = static_cast<int>(ruleIndex);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_FA_XMARK))
                {
                    removeRule = static_cast<int>(ruleIndex);
                }
                if (open)
                {
                    if (rule.type == ETerrainProcessRuleType::HeightAnchor)
                    {
                        glm::dvec2 position(rule.x, rule.y);
                        if (editPoint("摆放位置 XY", position))
                        {
                            rule.x = position.x;
                            rule.y = position.y;
                            changed = true;
                        }
                        glm::dvec2 samplePoint(rule.sampleX, rule.sampleY);
                        if (editPoint("高度取样 XY", samplePoint))
                        {
                            rule.sampleX = samplePoint.x;
                            rule.sampleY = samplePoint.y;
                            changed = true;
                        }
                        changed |= editNumber("离地 dz", rule.dz);
                    }
                    else if (rule.type == ETerrainProcessRuleType::Place ||
                             rule.type == ETerrainProcessRuleType::PlaceTilt ||
                             rule.type == ETerrainProcessRuleType::Snap)
                    {
                        glm::dvec2 position(rule.x, rule.y);
                        if (editPoint(rule.type == ETerrainProcessRuleType::Snap ? "外层 at" : "位置 XY", position))
                        {
                            rule.x = position.x;
                            rule.y = position.y;
                            changed = true;
                        }
                        changed |= editNumber("离地 dz", rule.dz);
                    }
                    if (rule.type == ETerrainProcessRuleType::PlaceTilt)
                    {
                        changed |= editNumber("最大倾角", rule.maxTilt, 0.5f, "%.1f°");
                        changed |= editNumber("探针距离", rule.probe);
                    }
                    else if (rule.type == ETerrainProcessRuleType::Along)
                    {
                        ImGui::TextDisabled("沿线折点");
                        changed |= editPoints(rule.points);
                        changed |= editNumber("步距", rule.step);
                        changed |= ImGui::InputInt("Seed", &rule.seed);
                        changed |= editNumber("起始偏移", rule.offset);
                        changed |= editNumber("离地 dz", rule.dz);
                    }
                    else if (rule.type == ETerrainProcessRuleType::Scatter)
                    {
                        changed |= ImGui::InputInt("Seed", &rule.seed);
                        changed |= ImGui::DragInt("数量", &rule.count, 1.0f, 0, 100000);
                        if (ImGui::Checkbox("圆形区域", &rule.circularRegion))
                        {
                            changed = true;
                        }
                        if (rule.circularRegion)
                        {
                            changed |= editPoint("圆心 XY", rule.regionCenter);
                            changed |= editNumber("半径", rule.regionRadius, 0.5f);
                        }
                        else
                        {
                            double region[4] = {rule.region.x, rule.region.y, rule.region.z, rule.region.w};
                            if (ImGui::DragScalarN("区域 x0/y0/x1/y1", ImGuiDataType_Double, region, 4, 0.5f))
                            {
                                rule.region = glm::dvec4(region[0], region[1], region[2], region[3]);
                                changed = true;
                            }
                        }
                        changed |= editNumber("最低高度", rule.minHeight);
                        changed |= editNumber("最高高度", rule.maxHeight);
                        changed |= editNumber("最大坡度", rule.maxSlope, 0.5f, "%.1f°");
                        changed |= editNumber("避水距离", rule.avoidWater);
                        std::string biomeText = fmt::format("{}", fmt::join(rule.biomes, ", "));
                        if (ImGui::InputTextWithHint("Biome 白名单", "grass, grass_dark", &biomeText))
                        {
                            rule.biomes.clear();
                            std::istringstream input(biomeText);
                            std::string biome;
                            while (std::getline(input, biome, ','))
                            {
                                biome.erase(biome.begin(),
                                            std::find_if_not(biome.begin(), biome.end(),
                                                             [](unsigned char c) { return std::isspace(c); }));
                                biome.erase(std::find_if_not(biome.rbegin(), biome.rend(),
                                                             [](unsigned char c) { return std::isspace(c); })
                                                .base(),
                                            biome.end());
                                if (!biome.empty())
                                {
                                    rule.biomes.push_back(std::move(biome));
                                }
                            }
                            changed = true;
                        }
                        if (ImGui::Checkbox("随机旋转", &rule.randomRotation))
                        {
                            changed = true;
                        }
                        changed |= editNumber("离地 dz", rule.dz);
                    }

                    ImGui::TextDisabled("Child SCAD（模块调用、rotate 链或代码块）");
                    if (ImGui::InputTextMultiline("##terrain_rule_child", &rule.childSource, ImVec2(-1.0f, 76.0f),
                                                  ImGuiInputTextFlags_AllowTabInput))
                    {
                        changed = true;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (duplicateRule >= 0)
            {
                terrainProcess_.DuplicateRule(static_cast<size_t>(duplicateRule));
                selectedTerrainRule_ = static_cast<int>(terrainProcess_.Rules().size()) - 1;
                terrainSelectionIsRule_ = true;
                scrollToSelectedTerrainItem_ = true;
                changed = true;
            }
            if (removeRule >= 0)
            {
                terrainProcess_.RemoveRule(static_cast<size_t>(removeRule));
                selectedTerrainRule_ = std::clamp(selectedTerrainRule_, 0,
                                                  std::max(0, static_cast<int>(terrainProcess_.Rules().size()) - 1));
                changed = true;
            }

            if (ImGui::Button(ICON_FA_PLUS " 添加贴地规则"))
            {
                ImGui::OpenPopup("##add_terrain_rule");
            }
            if (ImGui::BeginPopup("##add_terrain_rule"))
            {
                const auto addRule = [&](const char* label, ETerrainProcessRuleType type)
                {
                    if (ImGui::MenuItem(label))
                    {
                        terrainProcess_.AddRule(type);
                        selectedTerrainRule_ = static_cast<int>(terrainProcess_.Rules().size()) - 1;
                        terrainSelectionIsRule_ = true;
                        scrollToSelectedTerrainItem_ = true;
                        changed = true;
                    }
                };
                addRule("高度锚点 gk_terrain_height", ETerrainProcessRuleType::HeightAnchor);
                addRule("单件贴地 ter_place", ETerrainProcessRuleType::Place);
                addRule("随坡贴地 ter_place_tilt", ETerrainProcessRuleType::PlaceTilt);
                addRule("布局贴地 ter_snap", ETerrainProcessRuleType::Snap);
                addRule("沿线放置 ter_along", ETerrainProcessRuleType::Along);
                addRule("过滤散布 ter_scatter", ETerrainProcessRuleType::Scatter);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("也可从左侧 Kit 浏览器“+”直接创建 ter_place");
        }

        if (changed)
        {
            terrain.size.x = std::max(1.0, terrain.size.x);
            terrain.size.y = std::max(1.0, terrain.size.y);
            terrain.cells.x = std::clamp(terrain.cells.x, 4, 256);
            terrain.cells.y = std::clamp(terrain.cells.y, 4, 256);
            terrain.relief = std::max(0.0, terrain.relief);
            terrain.roughness = std::clamp(terrain.roughness, 0.0, 1.0);
            for (Assets::Scad::FTerrainFeature& feature : terrain.features)
            {
                feature.radius = std::max(0.1, feature.radius);
                feature.width = std::max(0.1, feature.width);
                feature.depth = std::max(0.0, feature.depth);
                feature.rugged = std::clamp(feature.rugged, 0.0, 1.0);
                feature.size.x = std::max(0.1, feature.size.x);
                feature.size.y = std::max(0.1, feature.size.y);
            }
            for (FTerrainProcessRule& rule : terrainProcess_.Rules())
            {
                rule.step = std::max(0.01, rule.step);
                rule.probe = std::max(0.01, rule.probe);
                rule.maxTilt = std::clamp(rule.maxTilt, 0.0, 90.0);
                rule.count = std::max(0, rule.count);
                rule.regionRadius = std::max(0.1, rule.regionRadius);
                if (rule.minHeight > rule.maxHeight)
                {
                    std::swap(rule.minHeight, rule.maxHeight);
                }
                rule.maxSlope = std::clamp(rule.maxSlope, 0.0, 90.0);
            }
            MarkTerrainProcessDirty();
        }
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
            if (assemblyProcedural_ &&
                ImGui::BeginTabItem(fmt::format("过程 ({} + {})", terrainProcess_.Terrain().features.size(),
                                                terrainProcess_.ActiveRuleCount())
                                        .c_str()))
            {
                assemblyEditorTab_ = 2;
                DrawTerrainProcessContent();
                ImGui::EndTabItem();
            }

            if (!assemblyProcedural_)
            {
                const std::string objectTabLabel = assemblyTerrainSources_.empty()
                    ? fmt::format("对象 ({})", bench_.size())
                    : fmt::format("对象 ({}) · 地形 {}", bench_.size(), assemblyTerrainSources_.size());
                if (ImGui::BeginTabItem(objectTabLabel.c_str()))
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
                    if (assemblyEvaluated_)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Warning));
                        ImGui::TextWrapped(
                            "已按 SCAD 求值结果展开。循环、条件和 module 引用已实例化；请“另存为”可编辑副本。");
                        ImGui::PopStyleColor();
                    }
                    else if (!assemblyStructured_ && !assemblySource_.empty())
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
                        selectedBenchItem_ = -1;
                        benchDirty_ = true;
                    }
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::InputTextWithHint("##object_filter", "搜索对象模块或参数…", objectFilterBuf_,
                                             sizeof(objectFilterBuf_));

                    int removeIndex = -1;
                    int duplicateIndex = -1;
                    ImGui::BeginChild("##bench_list", ImVec2(0, -62.0f), ImGuiChildFlags_None);
                    for (size_t i = 0; i < bench_.size(); ++i)
                    {
                        FBenchItem& benchItem = bench_[i];
                        if (objectFilterBuf_[0] != '\0' &&
                            benchItem.moduleName.find(objectFilterBuf_) == std::string::npos &&
                            std::string_view(benchItem.args).find(objectFilterBuf_) == std::string_view::npos)
                        {
                            continue;
                        }
                        ImGui::PushID(static_cast<int>(i));
                        ImGuiTreeNodeFlags objectFlags =
                            ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
                        if (bench_.size() <= 64)
                        {
                            objectFlags |= ImGuiTreeNodeFlags_DefaultOpen;
                        }
                        if (selectedBenchItem_ == static_cast<int>(i))
                        {
                            objectFlags |= ImGuiTreeNodeFlags_Selected;
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.10f, 0.34f, 0.68f, 0.82f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.14f, 0.42f, 0.80f, 0.92f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.16f, 0.48f, 0.92f, 1.0f));
                        }
                        const std::string objectLabel = benchItem.sourceLine > 0
                            ? fmt::format("{} #{}  ·  L{}", benchItem.moduleName, i, benchItem.sourceLine)
                            : fmt::format("{} #{}", benchItem.moduleName, i);
                        const bool open = ImGui::TreeNodeEx(objectLabel.c_str(), objectFlags);
                        if (selectedBenchItem_ == static_cast<int>(i))
                        {
                            ImGui::PopStyleColor(3);
                            if (scrollToSelectedBenchItem_)
                            {
                                ImGui::SetScrollHereY(0.35f);
                                scrollToSelectedBenchItem_ = false;
                            }
                        }
                        if (ImGui::IsItemClicked())
                        {
                            selectedBenchItem_ = static_cast<int>(i);
                            if (Assets::Node* selectedNode =
                                    ResolveSceneObjectNode(benchItem, SceneObjectWorldMatrix(benchItem)))
                            {
                                engine_.GetScene().SetSelectedId(selectedNode->GetInstanceId());
                            }
                        }
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
                            float position[3] = {benchItem.x, benchItem.y, benchItem.z};
                            if (ImGui::DragFloat3("位置", position, 0.5f))
                            {
                                benchItem.x = position[0];
                                benchItem.y = position[1];
                                benchItem.z = position[2];
                                benchDirty_ = true;
                            }
                            float rotation[3] = {benchItem.rotX, benchItem.rotY, benchItem.rotZ};
                            if (ImGui::DragFloat3("旋转", rotation, 1.0f, -360.0f, 360.0f, "%.1f°"))
                            {
                                benchItem.rotX = rotation[0];
                                benchItem.rotY = rotation[1];
                                benchItem.rotZ = rotation[2];
                                benchDirty_ = true;
                            }
                            float scale[3] = {benchItem.scale, benchItem.scaleY, benchItem.scaleZ};
                            if (ImGui::DragFloat3("缩放", scale, 0.02f, 0.001f, 100.0f, "%.3f"))
                            {
                                benchItem.scale = scale[0];
                                benchItem.scaleY = scale[1];
                                benchItem.scaleZ = scale[2];
                                benchDirty_ = true;
                            }
                            if (benchItem.hasColor && ImGui::ColorEdit4("颜色", benchItem.color))
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
                        if (selectedBenchItem_ == removeIndex)
                        {
                            selectedBenchItem_ = -1;
                        }
                        else if (selectedBenchItem_ > removeIndex)
                        {
                            --selectedBenchItem_;
                        }
                        benchDirty_ = true;
                    }
                    if (duplicateIndex >= 0)
                    {
                        FBenchItem copy = bench_[duplicateIndex];
                        copy.x += kBenchGridStep * 0.5f;
                        copy.y += kBenchGridStep * 0.5f;
                        copy.runtimeNodeId = std::numeric_limits<uint32_t>::max();
                        bench_.push_back(copy);
                        selectedBenchItem_ = static_cast<int>(bench_.size()) - 1;
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
                    assemblyEvaluated_ = false;
                    assemblyProcedural_ = false;
                    terrainProcessDirty_ = false;
                    terrainProcessWarnings_.clear();
                    bench_.clear();
                    assemblyTerrainSources_.clear();
                    selectedBenchItem_ = -1;
                    benchDirty_ = false;
                    openedAssemblyKits_ = FindKitDependencies(assemblySource_);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if ((assemblyStructured_ || assemblyEvaluated_) && benchDirty_)
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
        if (assemblyProcedural_)
        {
            FTerrainProcessRule& rule =
                terrainProcess_.AddRule(ETerrainProcessRuleType::Place, fmt::format("{}();", moduleName));
            rule.x = benchCursorX_;
            rule.y = benchCursorY_;
            benchCursorX_ += kBenchGridStep;
            if (++benchColCount_ >= kBenchGridColumns)
            {
                benchCursorX_ = 0.0f;
                benchCursorY_ += kBenchGridStep;
                benchColCount_ = 0;
            }
            MarkTerrainProcessDirty();
            statusLine_ = fmt::format("已添加 {} 为 ter_place 规则", moduleName);
            statusError_ = false;
            return;
        }

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
        selectedBenchItem_ = static_cast<int>(bench_.size()) - 1;
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

        if (!assemblyTerrainSources_.empty())
        {
            source += "// preserved terrain payloads\n";
            for (const std::string& terrainSource : assemblyTerrainSources_)
            {
                source += terrainSource;
                source += "\n";
            }
            source += "\n";
        }

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
            if (benchItem.hasColor)
            {
                source += fmt::format("color([{:.5f}, {:.5f}, {:.5f}, {:.5f}]) ", benchItem.color[0],
                                      benchItem.color[1], benchItem.color[2], benchItem.color[3]);
            }
            source += fmt::format("translate([{:.4f}, {:.4f}, {:.4f}]) rotate([{:.4f}, {:.4f}, {:.4f}]) "
                                  "scale([{:.5f}, {:.5f}, {:.5f}]) {}({});\n",
                                  benchItem.x, benchItem.y, benchItem.z, benchItem.rotX, benchItem.rotY, benchItem.rotZ,
                                  benchItem.scale, benchItem.scaleY, benchItem.scaleZ, benchItem.moduleName, args);
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
        const std::regex itemRegex("^\\s*translate\\(\\[\\s*(" + number + ")\\s*,\\s*(" + number + ")\\s*,\\s*(" +
                                       number + ")\\s*\\]\\)\\s*rotate\\(\\[\\s*(" + number + ")\\s*,\\s*(" + number +
                                       ")\\s*,\\s*(" + number + ")\\s*\\]\\)\\s*scale\\(\\[\\s*(" + number +
                                       ")\\s*,\\s*(" + number + ")\\s*,\\s*(" + number +
                                       ")\\s*\\]\\)\\s*([A-Za-z_][A-Za-z0-9_]*)\\((.*)\\);\\s*$",
                                   std::regex_constants::icase);
        const std::regex colorRegex("^\\s*color\\(\\[\\s*(" + number + ")\\s*,\\s*(" + number + ")\\s*,\\s*(" + number +
                                        ")\\s*,\\s*(" + number + ")\\s*\\]\\)\\s*(.*)$",
                                    std::regex_constants::icase);
        const std::regex fnRegex(R"(\$fn\s*=\s*(\d+)\s*;)");
        std::smatch fnMatch;
        if (std::regex_search(source, fnMatch, fnRegex))
        {
            fnSegments_ = std::clamp(std::stoi(fnMatch[1].str()), 3, 128);
        }
        showFloor_ = source.find("cube([") != std::string::npos;
        bench_.clear();
        selectedBenchItem_ = -1;

        std::istringstream lines(source);
        std::string line;
        while (std::getline(lines, line))
        {
            bool hasColor = false;
            glm::vec4 color(0.78f, 0.78f, 0.78f, 1.0f);
            std::smatch colorMatch;
            if (std::regex_match(line, colorMatch, colorRegex))
            {
                hasColor = true;
                color = glm::vec4(std::stof(colorMatch[1].str()), std::stof(colorMatch[2].str()),
                                  std::stof(colorMatch[3].str()), std::stof(colorMatch[4].str()));
                line = colorMatch[5].str();
            }
            std::smatch match;
            if (!std::regex_match(line, match, itemRegex))
            {
                continue;
            }
            const std::string moduleName = match[10].str();
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
            item.z = std::stof(match[3].str());
            item.rotX = std::stof(match[4].str());
            item.rotY = std::stof(match[5].str());
            item.rotZ = std::stof(match[6].str());
            item.scale = std::stof(match[7].str());
            item.scaleY = std::stof(match[8].str());
            item.scaleZ = std::stof(match[9].str());
            item.hasColor = hasColor;
            item.color[0] = color.r;
            item.color[1] = color.g;
            item.color[2] = color.b;
            item.color[3] = color.a;
            std::snprintf(item.args, sizeof(item.args), "%s", match[11].str().c_str());
            bench_.push_back(std::move(item));
        }
        benchDirty_ = false;
        return true;
    }

    bool ScadLibraryInterface::ImportEvaluatedAssembly(const std::string& sourcePath)
    {
        Assets::Scad::ScadProgram program;
        std::string error;
        if (!Assets::Scad::LoadScadProgram(sourcePath, program, error))
        {
            SPDLOG_WARN("[ScadLibrary] evaluated import parse failed for {}: {}", sourcePath, error);
            return false;
        }

        showFloor_ = false;
        Assets::ScadLoadOptions options;
        Assets::Scad::SceneEvalResult result;
        if (!Assets::Scad::ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions,
                                                        options, result, error))
        {
            SPDLOG_WARN("[ScadLibrary] evaluated import failed for {}: {}", sourcePath, error);
            return false;
        }

        assemblyTerrainSources_ = ExtractTerrainSources(ReadAssemblyTextFile(sourcePath));

        const auto findKitIndex = [&](const std::string& moduleName)
        {
            for (const std::string& dependency : openedAssemblyKits_)
            {
                for (int kitIndex = 0; kitIndex < static_cast<int>(kits_.size()); ++kitIndex)
                {
                    if (kits_[kitIndex].name != dependency)
                    {
                        continue;
                    }
                    const bool found =
                        std::any_of(kits_[kitIndex].modules.begin(), kits_[kitIndex].modules.end(),
                                    [&](const FKitModuleInfo& module) { return module.name == moduleName; });
                    if (found)
                    {
                        return kitIndex;
                    }
                }
            }
            return -1;
        };

        constexpr size_t maxEditableObjects = 5000;
        const auto collect = [&](auto&& self, const Assets::Scad::SceneNode& node, const glm::dmat4& parent) -> void
        {
            if (bench_.size() >= maxEditableObjects)
            {
                return;
            }
            const glm::dmat4 world = parent * node.localTransform;
            const int kitIndex = findKitIndex(node.name);
            const bool layoutContainer = node.name.starts_with("lay_");
            if (kitIndex >= 0 && !layoutContainer)
            {
                glm::dvec3 scale(1.0);
                glm::dquat orientation(1.0, 0.0, 0.0, 0.0);
                glm::dvec3 translation(0.0);
                glm::dvec3 skew(0.0);
                glm::dvec4 perspective(0.0);
                if (!glm::decompose(world, scale, orientation, translation, skew, perspective))
                {
                    return;
                }
                const glm::dvec3 euler = glm::degrees(glm::eulerAngles(orientation));

                FBenchItem item;
                item.kitIndex = kitIndex;
                item.moduleName = node.name;
                item.x = static_cast<float>(translation.x);
                item.y = static_cast<float>(translation.y);
                item.z = static_cast<float>(translation.z);
                item.rotX = static_cast<float>(euler.x);
                item.rotY = static_cast<float>(euler.y);
                item.rotZ = static_cast<float>(euler.z);
                item.scale = static_cast<float>(scale.x);
                item.scaleY = static_cast<float>(scale.y);
                item.scaleZ = static_cast<float>(scale.z);
                item.hasColor = node.hasCallColor;
                item.color[0] = static_cast<float>(node.callColor.r);
                item.color[1] = static_cast<float>(node.callColor.g);
                item.color[2] = static_cast<float>(node.callColor.b);
                item.color[3] = static_cast<float>(node.callColor.a);
                item.evaluated = true;
                item.sourceLine = node.sourceLine;
                item.runtimeNodeId = static_cast<uint32_t>(node.instanceId);

                std::string arguments;
                for (const auto& [name, value] : node.parameters)
                {
                    if (!arguments.empty())
                    {
                        arguments += ", ";
                    }
                    arguments += fmt::format("{} = {}", name, ScadValueSource(value));
                }
                std::snprintf(item.args, sizeof(item.args), "%s", arguments.c_str());
                bench_.push_back(std::move(item));
                return;
            }

            for (const Assets::Scad::SceneNode& child : node.children)
            {
                self(self, child, world);
            }
        };

        bench_.clear();
        selectedBenchItem_ = -1;
        for (const Assets::Scad::SceneNode& root : result.roots)
        {
            collect(collect, root, glm::dmat4(1.0));
        }
        benchDirty_ = false;
        SPDLOG_INFO("[ScadLibrary] evaluated import {} -> {} editable kit objects ({} evaluated nodes)", sourcePath,
                    bench_.size(), result.roots.size());
        return !bench_.empty();
    }

    bool ScadLibraryInterface::ImportAssemblyTerrains(const std::string& source)
    {
        assemblyTerrainSources_ = ExtractTerrainSources(source);
        if (!assemblyTerrainSources_.empty())
        {
            SPDLOG_INFO("[ScadLibrary] preserved {} terrain source block(s)", assemblyTerrainSources_.size());
        }
        return !assemblyTerrainSources_.empty();
    }

    bool ScadLibraryInterface::ImportTerrainProcessAssembly(const std::string& sourcePath, const std::string& source)
    {
        terrainProcessWarnings_.clear();
        if (source.find("gk_terrain") == std::string::npos)
        {
            return false;
        }
        Assets::Scad::ScadProgram program;
        std::string error;
        if (!Assets::Scad::LoadScadProgram(sourcePath, program, error))
        {
            return false;
        }

        Assets::ScadLoadOptions options;
        Assets::Scad::SceneEvalResult result;
        if (!Assets::Scad::ScadEvaluator::EvaluateScene(program.mainTopLevel, program.modules, program.functions,
                                                        options, result, error))
        {
            return false;
        }

        if (!terrainProcess_.Parse(source, program.mainTopLevel, result.topLevelVariables, error,
                                   terrainProcessWarnings_))
        {
            SPDLOG_DEBUG("[ScadLibrary] terrain process import skipped for {}: {}", sourcePath, error);
            return false;
        }
        assemblyTerrainSources_ = ExtractTerrainSources(source);
        terrainProcessDirty_ = false;
        terrainFeatureOverlayCacheKey_.clear();
        terrainFeatureOverlayData_.reset();
        terrainFeatureDragging_ = false;
        selectedTerrainFeature_ = std::clamp(
            selectedTerrainFeature_, 0, std::max(0, static_cast<int>(terrainProcess_.Terrain().features.size()) - 1));
        SPDLOG_INFO("[ScadLibrary] terrain process import {} -> {} features / {} rules", sourcePath,
                    terrainProcess_.Terrain().features.size(), terrainProcess_.ActiveRuleCount());
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

        static const std::regex fnRegex(R"(\$fn\s*=\s*(\d+)\s*;)");
        std::smatch fnMatch;
        if (std::regex_search(source, fnMatch, fnRegex))
        {
            fnSegments_ = std::clamp(std::stoi(fnMatch[1].str()), 3, 128);
        }

        rigPreview_.SetActive(false);
        openedAssemblyPath_ = sourcePath.string();
        assemblySource_ = source;
        openedAssemblyKits_ = FindKitDependencies(source);
        assemblySourceDirty_ = false;
        bench_.clear();
        benchCursorX_ = 0.0f;
        benchCursorY_ = 0.0f;
        benchRowDepth_ = 0.0f;
        benchColCount_ = 0;
        // Flat ScadLibrary assemblies keep their object-editor round trip even
        // when they preserve a terrain payload. Hand-written/generated
        // kit_terrain programs use the dedicated process editor.
        assemblyStructured_ = ParseStructuredAssembly(source);
        assemblyProcedural_ = !assemblyStructured_ && ImportTerrainProcessAssembly(openedAssemblyPath_, source);
        if (assemblyProcedural_)
        {
            assemblyEvaluated_ = false;
            showFloor_ = false;
        }
        else if (assemblyStructured_)
        {
            assemblyEvaluated_ = false;
            ImportAssemblyTerrains(source);
        }
        else
        {
            assemblyEvaluated_ = ImportEvaluatedAssembly(openedAssemblyPath_);
        }
        const std::filesystem::path repoRoot = scadRoot.parent_path().parent_path();
        const std::string relativePath = sourcePath.lexically_relative(repoRoot).generic_string();
        if (assemblyEvaluated_ && !assemblyProcedural_)
        {
            const std::string suggested =
                fmt::format("assets/scad/scenes/{}_editable.scad", sourcePath.stem().string());
            std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", suggested.c_str());
        }
        else
        {
            std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", relativePath.c_str());
        }
        for (int index = 0; index < static_cast<int>(assemblies_.size()); ++index)
        {
            if (std::filesystem::path(assemblies_[index].absolutePath) == sourcePath)
            {
                selectedAssembly_ = index;
                break;
            }
        }
        preserveCameraOnNextSceneLoad_ = false;
        engine_.RequestLoadScene({.filename = openedAssemblyPath_});
        const char* editMode = assemblyStructured_
            ? " · 可视化对象编辑"
            : (assemblyProcedural_ ? " · Terrain 过程编辑"
                                   : (assemblyEvaluated_ ? " · 求值对象编辑（另存副本）" : " · 源码编辑"));
        statusLine_ = fmt::format("已打开 {} · {} 个 Kit{}", relativePath, openedAssemblyKits_.size(), editMode);
        statusError_ = openedAssemblyKits_.empty();
        return true;
    }

    std::string ScadLibraryInterface::BuildAssemblyPreviewSource() const
    {
        if (assemblyProcedural_)
        {
            const std::string source = BuildTerrainProcessSource();
            if (openedAssemblyPath_.empty())
            {
                return source;
            }
            return RewriteScadDependencyPaths(source, std::filesystem::path(openedAssemblyPath_).parent_path(), {},
                                              true);
        }
        if (assemblyStructured_ || assemblyEvaluated_)
        {
            return BuildBenchSource();
        }
        if (assemblySource_.empty() || openedAssemblyPath_.empty())
        {
            return assemblySource_;
        }

        return RewriteScadDependencyPaths(assemblySource_, std::filesystem::path(openedAssemblyPath_).parent_path(), {},
                                          true);
    }

    std::string ScadLibraryInterface::BuildTerrainProcessSource() const { return terrainProcess_.BuildSource(); }

    void ScadLibraryInterface::PreviewAssemblySource()
    {
        rigPreview_.SetActive(false);
        preserveCameraOnNextSceneLoad_ = assemblyProcedural_;
        if (WriteAndLoad("assembly_preview.scad", BuildAssemblyPreviewSource()))
        {
            statusLine_ = assemblyProcedural_
                ? fmt::format("预览 {} 个地形特征 / {} 条过程规则", terrainProcess_.Terrain().features.size(),
                              terrainProcess_.ActiveRuleCount())
                : ((assemblyStructured_ || assemblyEvaluated_) ? fmt::format("预览 {} 个场景对象", bench_.size())
                                                               : "预览未保存的 SCAD 源码");
            statusError_ = false;
            return;
        }
        preserveCameraOnNextSceneLoad_ = false;
    }

    void ScadLibraryInterface::SaveAssembly(bool saveAs, bool reloadScene)
    {
        if (assemblyEvaluated_ && !saveAs)
        {
            statusLine_ = "求值展开的对象不能覆盖原始逻辑；请使用“另存为”创建可编辑副本";
            statusError_ = true;
            return;
        }
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
        std::string source;
        if (assemblyProcedural_)
        {
            source = BuildTerrainProcessSource();
            if (saveAs && !openedAssemblyPath_.empty())
            {
                source = RewriteScadDependencyPaths(source, std::filesystem::path(openedAssemblyPath_).parent_path(),
                                                    targetPath.parent_path(), false);
            }
        }
        else
        {
            source = (assemblyStructured_ || assemblyEvaluated_) ? BuildBenchSource(targetPath) : assemblySource_;
        }
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
        terrainProcessDirty_ = false;
        assemblyStructured_ = !assemblyProcedural_ && (assemblyStructured_ || assemblyEvaluated_);
        assemblyEvaluated_ = false;
        if (assemblyProcedural_)
        {
            assemblyProcedural_ = ImportTerrainProcessAssembly(openedAssemblyPath_, source);
        }
        const std::filesystem::path repoRoot = scadRoot.parent_path().parent_path();
        const std::string relativePath = targetPath.lexically_relative(repoRoot).generic_string();
        std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", relativePath.c_str());
        if (reloadScene)
        {
            preserveCameraOnNextSceneLoad_ = assemblyProcedural_;
            engine_.RequestLoadScene({.filename = openedAssemblyPath_});
        }
        RescanAssemblies();
        statusLine_ = reloadScene ? fmt::format("已保存并重载 {}", relativePath)
                                  : fmt::format("已实时更新并写回 {}", relativePath);
        statusError_ = false;
        SPDLOG_INFO("[ScadLibrary] saved scene assembly -> {}", targetPath.string());
    }

    void ScadLibraryInterface::MarkTerrainProcessDirty()
    {
        terrainProcessDirty_ = true;
        assemblySourceDirty_ = true;
    }

    void ScadLibraryInterface::ReloadTerrainProcess()
    {
        if (!assemblyProcedural_)
        {
            return;
        }
        rigPreview_.SetActive(false);
        preserveCameraOnNextSceneLoad_ = true;
        if (WriteAndLoad("terrain_process.scad", BuildAssemblyPreviewSource()))
        {
            terrainProcessDirty_ = false;
            statusLine_ = fmt::format("已刷新 {} 个地形特征 / {} 条过程规则", terrainProcess_.Terrain().features.size(),
                                      terrainProcess_.ActiveRuleCount());
            statusError_ = false;
            return;
        }
        preserveCameraOnNextSceneLoad_ = false;
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

    void ScadLibraryInterface::DrawSceneGizmoToolbar(const ImVec2& viewportPos)
    {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 12.0f, viewportPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        if (ImGui::Begin("##SceneObjectGizmoToolbar", nullptr, flags))
        {
            if (ImGui::SmallButton(sceneGizmoOperation_ == 0 ? "[移动]" : "移动"))
            {
                sceneGizmoOperation_ = 0;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(sceneGizmoOperation_ == 1 ? "[旋转]" : "旋转"))
            {
                sceneGizmoOperation_ = 1;
            }
            ImGui::SameLine();
            if (selectedBenchItem_ >= 0 && selectedBenchItem_ < static_cast<int>(bench_.size()))
            {
                ImGui::TextDisabled("%s  ·  松手写回 SCAD", bench_[selectedBenchItem_].moduleName.c_str());
            }
            else
            {
                ImGui::TextDisabled("从右侧对象列表选择一个对象");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    bool ScadLibraryInterface::TerrainFeatureConsumesMouse(double x, double y) const
    {
        if (!IsTerrainProcessAssembly() || !showTerrainFeatureOverlay_)
        {
            return false;
        }
        if (terrainFeatureDragging_ || terrainRuleDragging_)
        {
            return true;
        }

        const glm::vec2 mouse(static_cast<float>(x), static_cast<float>(y));
        for (const FTerrainFeatureHandle& handle : terrainFeatureHandles_)
        {
            if (glm::distance2(mouse, handle.screen) <= 144.0f)
            {
                return true;
            }
        }
        for (const FTerrainRuleHandle& handle : terrainRuleHandles_)
        {
            if (glm::distance2(mouse, handle.screen) <= 144.0f)
            {
                return true;
            }
        }
        return false;
    }

    bool ScadLibraryInterface::ConsumePreserveCameraOnNextSceneLoad()
    {
        const bool preserve = preserveCameraOnNextSceneLoad_;
        preserveCameraOnNextSceneLoad_ = false;
        return preserve;
    }

    void ScadLibraryInterface::DrawTerrainFeatureToolbar(const ImVec2& viewportPos)
    {
        ImGui::SetNextWindowPos(ImVec2(viewportPos.x + 12.0f, viewportPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        if (ImGui::Begin("##TerrainFeatureToolbar", nullptr, flags))
        {
            ImGui::Checkbox("Feature 轮廓", &showTerrainFeatureOverlay_);
            ImGui::SameLine();
            const std::vector<Assets::Scad::FTerrainFeature>& features = terrainProcess_.Terrain().features;
            const std::vector<FTerrainProcessRule>& rules = terrainProcess_.Rules();
            if (terrainSelectionIsRule_ && selectedTerrainRule_ >= 0 &&
                selectedTerrainRule_ < static_cast<int>(rules.size()))
            {
                ImGui::TextColored(ImVec4(0.31f, 1.0f, 0.59f, 1.0f), "■ #%02d %s  ·  再次拖动方形手柄编辑",
                                   selectedTerrainRule_ + 1,
                                   FTerrainProcessDocument::RuleTypeName(rules[selectedTerrainRule_].type));
            }
            else if (!terrainSelectionIsRule_ && selectedTerrainFeature_ >= 0 &&
                     selectedTerrainFeature_ < static_cast<int>(features.size()))
            {
                const Assets::Scad::FTerrainFeature& feature = features[selectedTerrainFeature_];
                ImGui::TextDisabled("#%02d %s  ·  再次拖动圆点编辑", selectedTerrainFeature_ + 1,
                                    FTerrainProcessDocument::FeatureTypeName(feature.type));
            }
            else
            {
                ImGui::TextDisabled("从右侧 Feature 列表或视口圆点选择");
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ScadLibraryInterface::RefreshTerrainFeatureOverlayCache()
    {
        const Assets::Scad::FTerrainSpec& terrain = terrainProcess_.Terrain();
        const std::string cacheKey = Assets::Scad::ScadTerrain::SpecCacheKey(terrain);
        if (terrainFeatureOverlayData_ && terrainFeatureOverlayCacheKey_ == cacheKey)
        {
            return;
        }
        if (terrainFeatureOverlayData_ && (terrainFeatureDragging_ || ImGui::IsAnyItemActive()))
        {
            return;
        }

        terrainFeatureOverlayData_ = Assets::Scad::ScadTerrain::Build(terrain);
        terrainFeatureOverlayCacheKey_ = cacheKey;
    }

    void ScadLibraryInterface::DrawTerrainFeatureOverlay(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        terrainFeatureHandles_.clear();
        terrainRuleHandles_.clear();
        if (!showTerrainFeatureOverlay_ || viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        {
            terrainFeatureDragging_ = false;
            terrainRuleDragging_ = false;
            return;
        }

        Assets::Scad::FTerrainSpec& terrain = terrainProcess_.Terrain();
        if (terrain.features.empty())
        {
            selectedTerrainFeature_ = -1;
            terrainFeatureDragging_ = false;
            terrainRuleDragging_ = false;
            return;
        }
        selectedTerrainFeature_ = std::clamp(selectedTerrainFeature_, 0, static_cast<int>(terrain.features.size()) - 1);
        RefreshTerrainFeatureOverlayCache();

        const Assets::UniformBufferObject& ubo = engine_.GetLastUniformBufferObject();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->PushClipRect(viewportPos, ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y),
                               true);

        const auto projectWorld = [&](const glm::vec3& world, ImVec2& screen)
        {
            const glm::vec4 clip = ubo.ViewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= 0.001f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < 0.0f || ndc.z > 1.0f || std::abs(ndc.x) > 1.2f || std::abs(ndc.y) > 1.2f)
            {
                return false;
            }
            screen.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
            screen.y = viewportPos.y + (ndc.y * 0.5f + 0.5f) * viewportSize.y;
            return true;
        };
        const auto surfaceHeight = [&](const glm::dvec2& point)
        {
            return terrainFeatureOverlayData_ ? terrainFeatureOverlayData_->HeightAt(point.x, point.y)
                                              : terrain.baseHeight;
        };
        const auto worldAt = [&](const glm::dvec2& point, double lift = 0.28)
        { return Assets::Scad::ScadToWorldPos(glm::dvec3(point.x, point.y, surfaceHeight(point) + lift), 1.0); };
        const auto colorForFeature = [](Assets::Scad::FTerrainFeature::EType type, int alpha)
        {
            using EType = Assets::Scad::FTerrainFeature::EType;
            switch (type)
            {
            case EType::Mountain:
                return IM_COL32(255, 132, 48, alpha);
            case EType::Ridge:
                return IM_COL32(255, 184, 72, alpha);
            case EType::Plateau:
                return IM_COL32(190, 116, 255, alpha);
            case EType::Lake:
                return IM_COL32(66, 205, 255, alpha);
            case EType::River:
                return IM_COL32(44, 139, 255, alpha);
            case EType::Road:
                return IM_COL32(255, 218, 84, alpha);
            case EType::Pad:
                return IM_COL32(255, 108, 196, alpha);
            }
            return IM_COL32(255, 255, 255, alpha);
        };
        const auto drawLabel = [&](const ImVec2& position, ImU32 color, const std::string& text)
        {
            const ImVec2 labelPos(position.x + 8.0f, position.y - 18.0f);
            drawList->AddText(ImVec2(labelPos.x + 1.0f, labelPos.y + 1.0f), IM_COL32(0, 0, 0, 220), text.c_str());
            drawList->AddText(labelPos, color, text.c_str());
        };
        const auto drawTerrainSegment = [&](const glm::dvec2& from, const glm::dvec2& to, ImU32 color, float thickness)
        {
            const int subdivisions = std::clamp(static_cast<int>(std::ceil(glm::distance(from, to) / 3.0)), 1, 64);
            ImVec2 previous;
            bool hasPrevious = false;
            for (int step = 0; step <= subdivisions; ++step)
            {
                const double t = static_cast<double>(step) / subdivisions;
                const glm::dvec2 point = glm::mix(from, to, t);
                ImVec2 screen;
                if (projectWorld(worldAt(point), screen))
                {
                    if (hasPrevious)
                    {
                        drawList->AddLine(previous, screen, color, thickness);
                    }
                    previous = screen;
                    hasPrevious = true;
                }
                else
                {
                    hasPrevious = false;
                }
            }
        };
        const auto drawTerrainPolyline =
            [&](const std::vector<glm::dvec2>& points, bool closed, ImU32 color, float thickness)
        {
            if (points.size() < 2)
            {
                return;
            }
            const size_t segmentCount = closed ? points.size() : points.size() - 1;
            for (size_t segment = 0; segment < segmentCount; ++segment)
            {
                drawTerrainSegment(points[segment], points[(segment + 1) % points.size()], color, thickness);
            }
        };
        const auto circlePoints = [](const glm::dvec2& center, double radius)
        {
            std::vector<glm::dvec2> points;
            constexpr int segmentCount = 48;
            points.reserve(segmentCount);
            for (int segment = 0; segment < segmentCount; ++segment)
            {
                const double angle = glm::two_pi<double>() * static_cast<double>(segment) / segmentCount;
                points.emplace_back(center + radius * glm::dvec2(std::cos(angle), std::sin(angle)));
            }
            return points;
        };
        const auto addHandle = [&](int featureIndex, int pointIndex, const glm::dvec2& point)
        {
            ImVec2 screen;
            const glm::vec3 world = worldAt(point, 0.42);
            if (projectWorld(world, screen))
            {
                terrainFeatureHandles_.push_back({featureIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto addFeatureWorldHandle = [&](int featureIndex, int pointIndex, const glm::vec3& world)
        {
            ImVec2 screen;
            if (projectWorld(world, screen))
            {
                terrainFeatureHandles_.push_back({featureIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto drawFeatureVerticalRuler =
            [&](int featureIndex, const glm::dvec2& point, double value, const char* prefix, ImU32 color)
        {
            const double base = surfaceHeight(point) + 0.45;
            const double rulerHeight = std::max(0.1, value);
            const glm::vec3 rulerBottom = Assets::Scad::ScadToWorldPos(glm::dvec3(point.x, point.y, base), 1.0);
            const glm::vec3 rulerTop =
                Assets::Scad::ScadToWorldPos(glm::dvec3(point.x, point.y, base + rulerHeight), 1.0);
            ImVec2 bottomScreen;
            ImVec2 topScreen;
            if (projectWorld(rulerBottom, bottomScreen) && projectWorld(rulerTop, topScreen))
            {
                drawList->AddLine(bottomScreen, topScreen, color, 2.0f);
                drawList->AddCircleFilled(topScreen, 4.5f, color);
                drawLabel(topScreen, color, fmt::format("{}={:.1f}", prefix, value));
                addFeatureWorldHandle(featureIndex, -3, rulerTop);
            }
        };
        const auto drawWidthBand =
            [&](const std::vector<glm::dvec2>& points, double width, ImU32 lineColor, ImU32 fillColor, float thickness)
        {
            if (points.size() < 2)
            {
                return;
            }
            for (size_t segment = 0; segment + 1 < points.size(); ++segment)
            {
                const glm::dvec2 delta = points[segment + 1] - points[segment];
                const double length = glm::length(delta);
                if (length <= 1e-6)
                {
                    continue;
                }
                const glm::dvec2 side(-delta.y / length * width * 0.5, delta.x / length * width * 0.5);
                const glm::dvec2 corners[] = {
                    points[segment] + side,
                    points[segment + 1] + side,
                    points[segment + 1] - side,
                    points[segment] - side,
                };
                ImVec2 projected[4];
                bool allVisible = true;
                for (int corner = 0; corner < 4; ++corner)
                {
                    allVisible &= projectWorld(worldAt(corners[corner], 0.18), projected[corner]);
                }
                if (allVisible)
                {
                    drawList->AddConvexPolyFilled(projected, 4, fillColor);
                }
                drawTerrainSegment(corners[0], corners[1], lineColor, thickness);
                drawTerrainSegment(corners[3], corners[2], lineColor, thickness);
            }
            drawTerrainPolyline(points, false, lineColor, thickness + 0.5f);
        };

        using EType = Assets::Scad::FTerrainFeature::EType;
        for (int featureIndex = 0; featureIndex < static_cast<int>(terrain.features.size()); ++featureIndex)
        {
            Assets::Scad::FTerrainFeature& feature = terrain.features[featureIndex];
            const bool selected = !terrainSelectionIsRule_ && featureIndex == selectedTerrainFeature_;
            const ImU32 lineColor = colorForFeature(feature.type, selected ? 255 : 112);
            const ImU32 fillColor = colorForFeature(feature.type, selected ? 46 : 18);
            const float thickness = selected ? 2.4f : 1.25f;

            if (feature.type == EType::Mountain || feature.type == EType::Plateau || feature.type == EType::Lake)
            {
                const std::vector<glm::dvec2> outline = circlePoints(feature.at, feature.radius);
                drawTerrainPolyline(outline, true, lineColor, thickness);
                drawTerrainSegment(feature.at, feature.at + glm::dvec2(feature.radius, 0.0), lineColor, thickness);
                addHandle(featureIndex, -1, feature.at);
                if (selected)
                {
                    addHandle(featureIndex, -2, feature.at + glm::dvec2(feature.radius, 0.0));
                    ImVec2 centerScreen;
                    if (projectWorld(worldAt(feature.at, 0.45), centerScreen))
                    {
                        if (feature.type == EType::Mountain)
                        {
                            drawLabel(centerScreen, lineColor,
                                      fmt::format("山峰  r={:.1f}  h={:.1f}  rugged={:.2f}", feature.radius,
                                                  feature.height, feature.rugged));
                        }
                        else if (feature.type == EType::Plateau)
                        {
                            drawLabel(centerScreen, lineColor,
                                      fmt::format("台地  r={:.1f}  h={:.1f}", feature.radius, feature.height));
                        }
                        else
                        {
                            drawLabel(centerScreen, lineColor,
                                      fmt::format("湖泊  r={:.1f}  depth={:.1f}", feature.radius, feature.depth));
                        }
                    }
                }
                if (selected && (feature.type == EType::Mountain || feature.type == EType::Plateau))
                {
                    drawFeatureVerticalRuler(featureIndex, feature.at, feature.height, "h", lineColor);
                }
                else if (selected && feature.type == EType::Lake)
                {
                    drawFeatureVerticalRuler(featureIndex, feature.at, feature.depth, "depth", lineColor);
                }
            }
            else if (feature.type == EType::Ridge || feature.type == EType::River || feature.type == EType::Road)
            {
                drawWidthBand(feature.pts, feature.width, lineColor, fillColor, thickness);
                for (int pointIndex = 0; pointIndex < static_cast<int>(feature.pts.size()); ++pointIndex)
                {
                    addHandle(featureIndex, pointIndex, feature.pts[pointIndex]);
                }
                if (selected && !feature.pts.empty())
                {
                    ImVec2 firstScreen;
                    if (projectWorld(worldAt(feature.pts.front(), 0.45), firstScreen))
                    {
                        if (feature.type == EType::Ridge)
                        {
                            drawLabel(firstScreen, lineColor,
                                      fmt::format("山脊  width={:.1f}  h={:.1f}", feature.width, feature.height));
                        }
                        else if (feature.type == EType::River)
                        {
                            drawLabel(firstScreen, lineColor,
                                      fmt::format("河流  width={:.1f}  depth={:.1f}", feature.width, feature.depth));
                        }
                        else
                        {
                            drawLabel(firstScreen, lineColor, fmt::format("道路  width={:.1f}", feature.width));
                        }
                    }
                }
                if (selected && feature.pts.size() >= 2)
                {
                    const glm::dvec2 delta = feature.pts[1] - feature.pts[0];
                    const double length = glm::length(delta);
                    if (length > 1e-6)
                    {
                        const glm::dvec2 side(-delta.y / length, delta.x / length);
                        const glm::dvec2 midpoint = (feature.pts[0] + feature.pts[1]) * 0.5;
                        const glm::dvec2 widthPoint = midpoint + side * feature.width * 0.5;
                        drawTerrainSegment(midpoint, widthPoint, lineColor, 2.0f);
                        addHandle(featureIndex, -4, widthPoint);
                        ImVec2 widthScreen;
                        if (projectWorld(worldAt(widthPoint, 0.45), widthScreen))
                        {
                            drawLabel(widthScreen, lineColor, fmt::format("width={:.1f}", feature.width));
                        }
                    }
                    if (feature.type == EType::Ridge)
                    {
                        drawFeatureVerticalRuler(featureIndex, feature.pts.front(), feature.height, "h", lineColor);
                    }
                    else if (feature.type == EType::River)
                    {
                        drawFeatureVerticalRuler(featureIndex, feature.pts.front(), feature.depth, "depth", lineColor);
                    }
                }
            }
            else if (feature.type == EType::Pad)
            {
                const double angle = glm::radians(feature.rot);
                const glm::dvec2 axisX(std::cos(angle), std::sin(angle));
                const glm::dvec2 axisY(-axisX.y, axisX.x);
                const glm::dvec2 halfX = axisX * feature.size.x * 0.5;
                const glm::dvec2 halfY = axisY * feature.size.y * 0.5;
                const std::vector<glm::dvec2> outline = {
                    feature.at - halfX - halfY,
                    feature.at + halfX - halfY,
                    feature.at + halfX + halfY,
                    feature.at - halfX + halfY,
                };
                drawTerrainPolyline(outline, true, lineColor, thickness);
                addHandle(featureIndex, -1, feature.at);
                if (selected)
                {
                    ImVec2 centerScreen;
                    if (projectWorld(worldAt(feature.at, 0.45), centerScreen))
                    {
                        drawLabel(centerScreen, lineColor,
                                  fmt::format("基座  {:.1f} × {:.1f}  rot={:.1f}°", feature.size.x, feature.size.y,
                                              feature.rot));
                    }
                }
            }
        }

        const auto addRuleHandle = [&](int ruleIndex, int pointIndex, const glm::dvec2& point)
        {
            ImVec2 screen;
            const glm::vec3 world = worldAt(point, 0.65);
            if (projectWorld(world, screen))
            {
                terrainRuleHandles_.push_back({ruleIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto addRuleWorldHandle = [&](int ruleIndex, int pointIndex, const glm::vec3& world)
        {
            ImVec2 screen;
            if (projectWorld(world, screen))
            {
                terrainRuleHandles_.push_back({ruleIndex, pointIndex, glm::vec2(screen.x, screen.y), world.y});
            }
        };
        const auto drawRuleMarker = [&](const glm::dvec2& point, ImU32 color, float radius)
        {
            ImVec2 screen;
            if (projectWorld(worldAt(point, 0.62), screen))
            {
                drawList->AddCircleFilled(screen, radius, IM_COL32(15, 22, 28, 210));
                drawList->AddCircle(screen, radius, color, 0, 2.0f);
                drawList->AddLine(ImVec2(screen.x - radius - 3.0f, screen.y),
                                  ImVec2(screen.x + radius + 3.0f, screen.y), color, 1.4f);
                drawList->AddLine(ImVec2(screen.x, screen.y - radius - 3.0f),
                                  ImVec2(screen.x, screen.y + radius + 3.0f), color, 1.4f);
            }
        };
        const auto childSummary = [](const std::string& child)
        {
            std::string summary = child;
            summary.erase(std::remove(summary.begin(), summary.end(), '\n'), summary.end());
            if (summary.size() > 38)
            {
                summary.resize(38);
                summary += "…";
            }
            return summary;
        };
        const auto positiveMod = [](int64_t value, int64_t divisor)
        {
            const int64_t remainder = value % divisor;
            return remainder < 0 ? remainder + divisor : remainder;
        };
        const auto layHash = [&](int64_t value)
        {
            const auto square = [&](int64_t x)
            {
                x = positiveMod(x, 65521);
                return positiveMod(x * x + x * 587 + 41, 65521);
            };
            return square(square(positiveMod(value, 65521)) + 13);
        };
        const auto layRandF = [&](int64_t seed, int64_t index)
        {
            const int64_t value =
                positiveMod(layHash(seed + index * 131) * 65521 + layHash(seed * 3 + index * 977 + 5), 9973);
            return static_cast<double>(value) / 9972.0;
        };
        const auto biomeId = [](std::string_view name)
        {
            constexpr std::string_view names[] = {"grass",     "grass_dark", "dry_grass", "sand", "rock",
                                                  "rock_high", "snow",       "bed",       "road", "pad"};
            for (int index = 0; index < static_cast<int>(std::size(names)); ++index)
            {
                if (name == names[index])
                {
                    return index;
                }
            }
            return -1;
        };
        const auto scatterAccepts = [&](const FTerrainProcessRule& rule, const glm::dvec2& point)
        {
            if (!terrainFeatureOverlayData_)
            {
                return true;
            }
            double height = 0.0;
            double slope = 0.0;
            bool water = false;
            uint8_t biome = 0;
            terrainFeatureOverlayData_->InfoAt(point.x, point.y, height, slope, water, biome);
            const uint8_t pointBiome = biome;
            if (height < rule.minHeight || height > rule.maxHeight || slope > rule.maxSlope)
            {
                return false;
            }
            if (rule.avoidWater >= 0.0)
            {
                if (water)
                {
                    return false;
                }
                if (rule.avoidWater > 0.0)
                {
                    for (const glm::dvec2& offset :
                         {glm::dvec2(rule.avoidWater, 0.0), glm::dvec2(-rule.avoidWater, 0.0),
                          glm::dvec2(0.0, rule.avoidWater), glm::dvec2(0.0, -rule.avoidWater)})
                    {
                        bool nearbyWater = false;
                        terrainFeatureOverlayData_->InfoAt(point.x + offset.x, point.y + offset.y, height, slope,
                                                           nearbyWater, biome);
                        if (nearbyWater)
                        {
                            return false;
                        }
                    }
                }
            }
            if (rule.biomes.empty())
            {
                return true;
            }
            return std::any_of(rule.biomes.begin(), rule.biomes.end(),
                               [&](const std::string& name) { return biomeId(name) == pointBiome; });
        };

        std::vector<FTerrainProcessRule>& rules = terrainProcess_.Rules();
        selectedTerrainRule_ = std::clamp(selectedTerrainRule_, 0, std::max(0, static_cast<int>(rules.size()) - 1));
        for (int ruleIndex = 0; ruleIndex < static_cast<int>(rules.size()); ++ruleIndex)
        {
            FTerrainProcessRule& rule = rules[ruleIndex];
            if (rule.removed)
            {
                continue;
            }
            const bool selected = terrainSelectionIsRule_ && ruleIndex == selectedTerrainRule_;
            const ImU32 color = selected ? IM_COL32(80, 255, 150, 255) : IM_COL32(80, 255, 150, 105);
            const ImU32 sampleColor = selected ? IM_COL32(255, 255, 255, 245) : IM_COL32(190, 255, 215, 130);
            const float thickness = selected ? 2.2f : 1.1f;
            const glm::dvec2 position(rule.x, rule.y);

            if (rule.type == ETerrainProcessRuleType::HeightAnchor)
            {
                const glm::dvec2 sample(rule.sampleX, rule.sampleY);
                drawTerrainSegment(position, sample, color, thickness);
                drawRuleMarker(position, color, selected ? 7.0f : 5.0f);
                drawRuleMarker(sample, IM_COL32(255, 255, 255, selected ? 255 : 120), selected ? 6.0f : 4.0f);
                addRuleHandle(ruleIndex, -1, position);
                addRuleHandle(ruleIndex, -2, sample);
            }
            else if (rule.type == ETerrainProcessRuleType::Place || rule.type == ETerrainProcessRuleType::PlaceTilt ||
                     rule.type == ETerrainProcessRuleType::Snap)
            {
                drawRuleMarker(position, color, selected ? 7.0f : 5.0f);
                addRuleHandle(ruleIndex, -1, position);
                if (rule.type == ETerrainProcessRuleType::PlaceTilt)
                {
                    drawTerrainPolyline(circlePoints(position, rule.probe), true, color, thickness);
                }
            }
            else if (rule.type == ETerrainProcessRuleType::Along)
            {
                drawTerrainPolyline(rule.points, false, color, thickness);
                for (int pointIndex = 0; pointIndex < static_cast<int>(rule.points.size()); ++pointIndex)
                {
                    addRuleHandle(ruleIndex, pointIndex, rule.points[pointIndex]);
                }
                int sampleCount = 0;
                for (size_t segment = 0; selected && segment + 1 < rule.points.size() && sampleCount < 1000; ++segment)
                {
                    const glm::dvec2 delta = rule.points[segment + 1] - rule.points[segment];
                    const double length = glm::length(delta);
                    if (length <= 1e-6 || rule.step <= 0.0)
                    {
                        continue;
                    }
                    const int count = static_cast<int>(std::floor((length - rule.offset) / rule.step));
                    for (int sample = 0; sample <= count && sampleCount < 1000; ++sample, ++sampleCount)
                    {
                        const glm::dvec2 point =
                            rule.points[segment] + delta * ((rule.offset + sample * rule.step) / length);
                        ImVec2 screen;
                        if (projectWorld(worldAt(point, 0.72), screen))
                        {
                            drawList->AddNgon(screen, 4.0f, sampleColor, 4, 1.7f);
                        }
                    }
                }
            }
            else if (rule.type == ETerrainProcessRuleType::Scatter)
            {
                const glm::dvec2 regionCenter = rule.circularRegion
                    ? rule.regionCenter
                    : glm::dvec2((rule.region.x + rule.region.z) * 0.5, (rule.region.y + rule.region.w) * 0.5);
                if (rule.circularRegion)
                {
                    drawTerrainPolyline(circlePoints(rule.regionCenter, rule.regionRadius), true, color, thickness);
                    drawTerrainSegment(rule.regionCenter, rule.regionCenter + glm::dvec2(rule.regionRadius, 0.0), color,
                                       thickness);
                    addRuleHandle(ruleIndex, -3, rule.regionCenter);
                    addRuleHandle(ruleIndex, -4, rule.regionCenter + glm::dvec2(rule.regionRadius, 0.0));
                }
                else
                {
                    const std::vector<glm::dvec2> region = {{rule.region.x, rule.region.y},
                                                            {rule.region.z, rule.region.y},
                                                            {rule.region.z, rule.region.w},
                                                            {rule.region.x, rule.region.w}};
                    drawTerrainPolyline(region, true, color, thickness);
                    addRuleHandle(ruleIndex, -3, region[0]);
                    addRuleHandle(ruleIndex, -4, region[2]);
                }
                if (selected)
                {
                    constexpr double itemsPerWorldUnit = 10.0;
                    const double base = surfaceHeight(regionCenter) + 0.65;
                    const double rulerHeight =
                        std::clamp(static_cast<double>(rule.count) / itemsPerWorldUnit, 2.0, 60.0);
                    const glm::vec3 rulerBottom =
                        Assets::Scad::ScadToWorldPos(glm::dvec3(regionCenter.x, regionCenter.y, base), 1.0);
                    const glm::vec3 rulerTop = Assets::Scad::ScadToWorldPos(
                        glm::dvec3(regionCenter.x, regionCenter.y, base + rulerHeight), 1.0);
                    ImVec2 bottomScreen;
                    ImVec2 topScreen;
                    if (projectWorld(rulerBottom, bottomScreen) && projectWorld(rulerTop, topScreen))
                    {
                        drawList->AddLine(bottomScreen, topScreen, color, 2.0f);
                        const float halfSize = 4.5f;
                        drawList->AddRectFilled(ImVec2(topScreen.x - halfSize, topScreen.y - halfSize),
                                                ImVec2(topScreen.x + halfSize, topScreen.y + halfSize), color, 1.5f);
                        drawLabel(topScreen, color, fmt::format("count={}", rule.count));
                        addRuleWorldHandle(ruleIndex, -5, rulerTop);
                    }
                }
                int accepted = 0;
                const int candidateCount = selected ? std::min(std::max(0, rule.count) * 4, 20000) : 0;
                for (int candidate = 0; candidate < candidateCount && accepted < std::min(rule.count, 1000);
                     ++candidate)
                {
                    const int64_t candidateSeed = static_cast<int64_t>(rule.seed) * 8191 + candidate * 131;
                    glm::dvec2 point;
                    if (rule.circularRegion)
                    {
                        const double radius = std::sqrt(layRandF(candidateSeed, 1)) * rule.regionRadius;
                        const double angle = glm::two_pi<double>() * layRandF(candidateSeed, 2);
                        point = rule.regionCenter + radius * glm::dvec2(std::cos(angle), std::sin(angle));
                    }
                    else
                    {
                        point = {rule.region.x + (rule.region.z - rule.region.x) * layRandF(candidateSeed, 1),
                                 rule.region.y + (rule.region.w - rule.region.y) * layRandF(candidateSeed, 2)};
                    }
                    if (!scatterAccepts(rule, point))
                    {
                        continue;
                    }
                    ++accepted;
                    ImVec2 screen;
                    if (projectWorld(worldAt(point, 0.7), screen))
                    {
                        drawList->AddCircleFilled(screen, selected ? 3.2f : 2.2f, sampleColor);
                    }
                }
            }

            if (selected)
            {
                glm::dvec2 labelPoint = position;
                if (rule.type == ETerrainProcessRuleType::Along && !rule.points.empty())
                    labelPoint = rule.points.front();
                if (rule.type == ETerrainProcessRuleType::Scatter)
                    labelPoint = rule.circularRegion ? rule.regionCenter : glm::dvec2(rule.region.x, rule.region.y);
                ImVec2 screen;
                if (projectWorld(worldAt(labelPoint, 0.8), screen))
                {
                    drawLabel(screen, color,
                              fmt::format("{}  {}", FTerrainProcessDocument::RuleTypeName(rule.type),
                                          childSummary(rule.childSource)));
                }
            }
        }

        const glm::vec2 mouse(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
        int hoveredHandle = -1;
        float nearestDistance = 144.0f;
        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainFeatureHandles_.size()); ++handleIndex)
        {
            const FTerrainFeatureHandle& handle = terrainFeatureHandles_[handleIndex];
            const float distance = glm::distance2(mouse, handle.screen);
            if (distance <= nearestDistance)
            {
                hoveredHandle = handleIndex;
                nearestDistance = distance;
            }
        }

        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainFeatureHandles_.size()); ++handleIndex)
        {
            const FTerrainFeatureHandle& handle = terrainFeatureHandles_[handleIndex];
            const bool selected = !terrainSelectionIsRule_ && handle.featureIndex == selectedTerrainFeature_;
            const bool hovered = handleIndex == hoveredHandle;
            const Assets::Scad::FTerrainFeature& feature = terrain.features[handle.featureIndex];
            const ImU32 color = colorForFeature(feature.type, selected ? 255 : 150);
            const ImVec2 screen(handle.screen.x, handle.screen.y);
            drawList->AddCircleFilled(screen, hovered ? 7.0f : (selected ? 5.5f : 3.5f),
                                      hovered ? IM_COL32(255, 255, 255, 255) : color);
            drawList->AddCircle(screen, hovered ? 7.0f : (selected ? 5.5f : 3.5f), IM_COL32(12, 18, 26, 230), 0, 1.5f);
        }
        int hoveredRuleHandle = -1;
        nearestDistance = 144.0f;
        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainRuleHandles_.size()); ++handleIndex)
        {
            const float distance = glm::distance2(mouse, terrainRuleHandles_[handleIndex].screen);
            if (distance <= nearestDistance)
            {
                hoveredRuleHandle = handleIndex;
                nearestDistance = distance;
            }
        }
        for (int handleIndex = 0; handleIndex < static_cast<int>(terrainRuleHandles_.size()); ++handleIndex)
        {
            const FTerrainRuleHandle& handle = terrainRuleHandles_[handleIndex];
            const bool selected = terrainSelectionIsRule_ && handle.ruleIndex == selectedTerrainRule_;
            const bool hovered = handleIndex == hoveredRuleHandle;
            const ImVec2 screen(handle.screen.x, handle.screen.y);
            const float radius = hovered ? 7.0f : (selected ? 5.5f : 3.5f);
            drawList->AddRectFilled(
                ImVec2(screen.x - radius, screen.y - radius), ImVec2(screen.x + radius, screen.y + radius),
                hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 255, 150, selected ? 255 : 150), 1.5f);
            drawList->AddRect(ImVec2(screen.x - radius, screen.y - radius),
                              ImVec2(screen.x + radius, screen.y + radius), IM_COL32(12, 18, 26, 230), 1.5f, 0, 1.5f);
        }

        const bool mouseInside = mouse.x >= viewportPos.x && mouse.y >= viewportPos.y &&
            mouse.x < viewportPos.x + viewportSize.x && mouse.y < viewportPos.y + viewportSize.y;
        if (hoveredHandle >= 0 || hoveredRuleHandle >= 0 || terrainFeatureDragging_ || terrainRuleDragging_)
        {
            ImGui::GetIO().WantCaptureMouse = true;
        }
        if (!terrainFeatureDragging_ && !terrainRuleDragging_ && hoveredRuleHandle >= 0 && mouseInside &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const FTerrainRuleHandle& handle = terrainRuleHandles_[hoveredRuleHandle];
            const bool alreadySelected = terrainSelectionIsRule_ && selectedTerrainRule_ == handle.ruleIndex;
            selectedTerrainRule_ = handle.ruleIndex;
            terrainSelectionIsRule_ = true;
            scrollToSelectedTerrainItem_ = true;
            if (alreadySelected)
            {
                terrainRuleDragPoint_ = handle.pointIndex;
                terrainRuleDragPlaneHeight_ = handle.worldHeight;
                terrainDragStartMouse_ = mouse;
                if (handle.pointIndex == -5)
                {
                    terrainDragStartValue_ = rules[handle.ruleIndex].count;
                }
                terrainRuleDragging_ = true;
            }
        }
        if (!terrainFeatureDragging_ && hoveredRuleHandle < 0 && hoveredHandle >= 0 && mouseInside &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const FTerrainFeatureHandle& handle = terrainFeatureHandles_[hoveredHandle];
            const bool alreadySelected = !terrainSelectionIsRule_ && selectedTerrainFeature_ == handle.featureIndex;
            selectedTerrainFeature_ = handle.featureIndex;
            terrainSelectionIsRule_ = false;
            scrollToSelectedTerrainItem_ = true;
            if (alreadySelected)
            {
                terrainFeatureDragPoint_ = handle.pointIndex;
                terrainFeatureDragPlaneHeight_ = handle.worldHeight;
                terrainDragStartMouse_ = mouse;
                if (handle.pointIndex == -3)
                {
                    const Assets::Scad::FTerrainFeature& feature = terrain.features[handle.featureIndex];
                    terrainDragStartValue_ =
                        feature.type == EType::Lake || feature.type == EType::River ? feature.depth : feature.height;
                }
                terrainFeatureDragging_ = true;
            }
        }

        if (terrainFeatureDragging_)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && glm::distance2(mouse, terrainDragStartMouse_) > 4.0f)
            {
                if (selectedTerrainFeature_ >= 0 &&
                    selectedTerrainFeature_ < static_cast<int>(terrain.features.size()) &&
                    terrainFeatureDragPoint_ == -3)
                {
                    Assets::Scad::FTerrainFeature& feature = terrain.features[selectedTerrainFeature_];
                    const double value =
                        std::max(0.0, terrainDragStartValue_ + (terrainDragStartMouse_.y - mouse.y) * 0.2);
                    if (feature.type == EType::Lake || feature.type == EType::River)
                    {
                        feature.depth = value;
                    }
                    else
                    {
                        feature.height = value;
                    }
                    MarkTerrainProcessDirty();
                }
                else
                {
                    glm::vec3 rayOrigin;
                    glm::vec3 rayDirection;
                    Runtime::EngineHelper::GetScreenToWorldRay(mouse, rayOrigin, rayDirection);
                    if (std::abs(rayDirection.y) > 1e-5f)
                    {
                        const float distance = (terrainFeatureDragPlaneHeight_ - rayOrigin.y) / rayDirection.y;
                        if (distance > 0.0f && selectedTerrainFeature_ >= 0 &&
                            selectedTerrainFeature_ < static_cast<int>(terrain.features.size()))
                        {
                            const glm::vec3 hit = rayOrigin + rayDirection * distance;
                            const glm::dvec2 scadPoint(hit.x, -hit.z);
                            Assets::Scad::FTerrainFeature& feature = terrain.features[selectedTerrainFeature_];
                            if (terrainFeatureDragPoint_ == -2)
                            {
                                feature.radius = std::max(0.1, glm::distance(feature.at, scadPoint));
                            }
                            else if (terrainFeatureDragPoint_ == -1)
                            {
                                feature.at = scadPoint;
                            }
                            else if (terrainFeatureDragPoint_ == -4 && feature.pts.size() >= 2)
                            {
                                const glm::dvec2 delta = feature.pts[1] - feature.pts[0];
                                const double length = glm::length(delta);
                                if (length > 1e-6)
                                {
                                    const glm::dvec2 side(-delta.y / length, delta.x / length);
                                    const glm::dvec2 midpoint = (feature.pts[0] + feature.pts[1]) * 0.5;
                                    feature.width = std::max(0.1, 2.0 * std::abs(glm::dot(scadPoint - midpoint, side)));
                                }
                            }
                            else if (terrainFeatureDragPoint_ >= 0 &&
                                     terrainFeatureDragPoint_ < static_cast<int>(feature.pts.size()))
                            {
                                feature.pts[terrainFeatureDragPoint_] = scadPoint;
                            }
                            MarkTerrainProcessDirty();
                        }
                    }
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                terrainFeatureDragging_ = false;
            }
        }
        if (terrainRuleDragging_)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && glm::distance2(mouse, terrainDragStartMouse_) > 4.0f)
            {
                if (selectedTerrainRule_ >= 0 && selectedTerrainRule_ < static_cast<int>(rules.size()) &&
                    terrainRuleDragPoint_ == -5)
                {
                    FTerrainProcessRule& rule = rules[selectedTerrainRule_];
                    rule.count = std::clamp(static_cast<int>(std::lround(terrainDragStartValue_ +
                                                                         (terrainDragStartMouse_.y - mouse.y) * 2.0)),
                                            0, 100000);
                    MarkTerrainProcessDirty();
                }
                else
                {
                    glm::vec3 rayOrigin;
                    glm::vec3 rayDirection;
                    Runtime::EngineHelper::GetScreenToWorldRay(mouse, rayOrigin, rayDirection);
                    if (std::abs(rayDirection.y) > 1e-5f)
                    {
                        const float distance = (terrainRuleDragPlaneHeight_ - rayOrigin.y) / rayDirection.y;
                        if (distance > 0.0f && selectedTerrainRule_ >= 0 &&
                            selectedTerrainRule_ < static_cast<int>(rules.size()))
                        {
                            const glm::vec3 hit = rayOrigin + rayDirection * distance;
                            const glm::dvec2 point(hit.x, -hit.z);
                            FTerrainProcessRule& rule = rules[selectedTerrainRule_];
                            if (terrainRuleDragPoint_ == -1)
                            {
                                rule.x = point.x;
                                rule.y = point.y;
                            }
                            else if (terrainRuleDragPoint_ == -2)
                            {
                                rule.sampleX = point.x;
                                rule.sampleY = point.y;
                            }
                            else if (terrainRuleDragPoint_ == -3)
                            {
                                if (rule.circularRegion)
                                    rule.regionCenter = point;
                                else
                                {
                                    rule.region.x = point.x;
                                    rule.region.y = point.y;
                                }
                            }
                            else if (terrainRuleDragPoint_ == -4)
                            {
                                if (rule.circularRegion)
                                    rule.regionRadius = std::max(0.1, glm::distance(rule.regionCenter, point));
                                else
                                {
                                    rule.region.z = point.x;
                                    rule.region.w = point.y;
                                }
                            }
                            else if (terrainRuleDragPoint_ >= 0 &&
                                     terrainRuleDragPoint_ < static_cast<int>(rule.points.size()))
                            {
                                rule.points[terrainRuleDragPoint_] = point;
                            }
                            MarkTerrainProcessDirty();
                        }
                    }
                }
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                terrainRuleDragging_ = false;
            }
        }

        drawList->PopClipRect();
    }

    void ScadLibraryInterface::CommitSceneGizmoEdit()
    {
        engine_.GetScene().MarkDirty();
        benchDirty_ = false;
        assemblySourceDirty_ = true;
        if (assemblyEvaluated_)
        {
            SaveAssembly(true, false);
            return;
        }
        if (assemblyStructured_ && !openedAssemblyPath_.empty())
        {
            SaveAssembly(false, false);
            return;
        }

        assemblyStructured_ = true;
        if (std::string_view(assemblyPathBuf_).empty())
        {
            std::snprintf(assemblyPathBuf_, sizeof(assemblyPathBuf_), "%s", "assets/scad/scenes/my_scene.scad");
        }
        SaveAssembly(true, false);
    }

    bool ScadLibraryInterface::SelectSceneObjectFromViewport(uint32_t hitInstanceId)
    {
        if (workspaceMode_ != EWorkspaceMode::SceneAssembly || bench_.empty())
        {
            return false;
        }

        Assets::Scene& scene = engine_.GetScene();
        Assets::Node* hitNode = scene.GetNodeByInstanceId(hitInstanceId);
        if (hitNode == nullptr)
        {
            selectedBenchItem_ = -1;
            scene.ClearSelection();
            return false;
        }

        std::vector<Assets::Node*> ancestry;
        for (Assets::Node* node = hitNode; node != nullptr; node = node->GetParent())
        {
            ancestry.push_back(node);
        }
        std::string ancestryNames;
        for (const Assets::Node* ancestor : ancestry)
        {
            ancestryNames += ancestryNames.empty() ? ancestor->GetName() : fmt::format(" <- {}", ancestor->GetName());
        }
        SPDLOG_INFO("[ScadLibrary] viewport pick {} -> {}", hitInstanceId, ancestryNames);

        // Evaluated files already carry deterministic loader instance IDs. This
        // is the strongest match and handles Kit instances nested under layout
        // modules without relying on names.
        for (Assets::Node* ancestor : ancestry)
        {
            const auto found = std::find_if(bench_.begin(), bench_.end(), [&](const FBenchItem& item)
                                            { return item.runtimeNodeId == ancestor->GetInstanceId(); });
            if (found != bench_.end())
            {
                selectedBenchItem_ = static_cast<int>(std::distance(bench_.begin(), found));
                scrollToSelectedBenchItem_ = true;
                sceneGizmoAwaitingPickRelease_ = true;
                scene.SetSelectedId(hitInstanceId);
                statusLine_ = fmt::format("已从视口选择 {} #{}", found->moduleName, selectedBenchItem_);
                statusError_ = false;
                return true;
            }
        }

        // Structured flat scenes do not carry evaluator IDs in their text
        // parser. Walk from the root toward the hit render node, then identify
        // the nearest same-name instance by its world transform.
        for (auto ancestorIt = ancestry.rbegin(); ancestorIt != ancestry.rend(); ++ancestorIt)
        {
            Assets::Node* ancestor = *ancestorIt;
            int nearestIndex = -1;
            float nearestDistance = std::numeric_limits<float>::max();
            for (int index = 0; index < static_cast<int>(bench_.size()); ++index)
            {
                FBenchItem& item = bench_[index];
                if (item.moduleName != ancestor->GetName())
                {
                    continue;
                }

                const glm::mat4 expectedWorld = SceneObjectWorldMatrix(item);
                float distance = 0.0f;
                for (int column = 0; column < 4; ++column)
                {
                    const glm::vec4 delta = ancestor->WorldTransform()[column] - expectedWorld[column];
                    distance += glm::dot(delta, delta);
                }
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestIndex = index;
                }
            }

            if (nearestIndex >= 0)
            {
                FBenchItem& selected = bench_[nearestIndex];
                selected.runtimeNodeId = ancestor->GetInstanceId();
                selectedBenchItem_ = nearestIndex;
                scrollToSelectedBenchItem_ = true;
                sceneGizmoAwaitingPickRelease_ = true;
                scene.SetSelectedId(hitInstanceId);
                statusLine_ = fmt::format("已从视口选择 {} #{}", selected.moduleName, selectedBenchItem_);
                statusError_ = false;
                return true;
            }
        }

        selectedBenchItem_ = -1;
        scene.ClearSelection();
        statusLine_ = "点选内容不属于可编辑 Kit 实例";
        statusError_ = false;
        return false;
    }

    Assets::Node* ScadLibraryInterface::ResolveSceneObjectNode(FBenchItem& item, const glm::mat4& expectedWorld)
    {
        Assets::Scene& scene = engine_.GetScene();
        if (item.runtimeNodeId != std::numeric_limits<uint32_t>::max())
        {
            Assets::Node* resolved = scene.GetNodeByInstanceId(item.runtimeNodeId);
            if (resolved != nullptr && resolved->GetName() == item.moduleName)
            {
                return resolved;
            }
            item.runtimeNodeId = std::numeric_limits<uint32_t>::max();
        }

        Assets::Node* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (const std::shared_ptr<Assets::Node>& candidate : scene.Nodes())
        {
            if (candidate == nullptr || candidate->GetName() != item.moduleName)
            {
                continue;
            }

            float distance = 0.0f;
            const glm::mat4& candidateWorld = candidate->WorldTransform();
            for (int column = 0; column < 4; ++column)
            {
                const glm::vec4 delta = candidateWorld[column] - expectedWorld[column];
                distance += glm::dot(delta, delta);
            }
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = candidate.get();
            }
        }
        if (nearest != nullptr)
        {
            item.runtimeNodeId = nearest->GetInstanceId();
        }
        return nearest;
    }

    void ScadLibraryInterface::ApplySceneObjectTransform(FBenchItem& item, const glm::mat4& worldMatrix)
    {
        Assets::Node* node = ResolveSceneObjectNode(item, worldMatrix);
        if (node == nullptr)
        {
            return;
        }

        glm::mat4 parentWorld(1.0f);
        if (Assets::Node* parent = node->GetParent())
        {
            parentWorld = parent->WorldTransform();
        }
        const glm::mat4 localMatrix = glm::inverse(parentWorld) * worldMatrix;

        glm::vec3 scale(1.0f);
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 translation(0.0f);
        glm::vec3 skew(0.0f);
        glm::vec4 perspective(0.0f);
        if (!glm::decompose(localMatrix, scale, rotation, translation, skew, perspective))
        {
            return;
        }

        node->SetTranslation(translation);
        node->SetRotation(glm::normalize(rotation));
        node->SetScale(scale);
        engine_.GetScene().MarkTransformDirty();
    }

    void ScadLibraryInterface::DrawSceneObjectGizmo(const ImVec2& viewportPos, const ImVec2& viewportSize)
    {
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f || selectedBenchItem_ < 0 ||
            selectedBenchItem_ >= static_cast<int>(bench_.size()))
        {
            sceneGizmoWasUsing_ = false;
            return;
        }

        FBenchItem& item = bench_[selectedBenchItem_];
        glm::mat4 worldMatrix = SceneObjectWorldMatrix(item);
        ResolveSceneObjectNode(item, worldMatrix);
        if (sceneGizmoAwaitingPickRelease_)
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                sceneGizmoAwaitingPickRelease_ = false;
            }
            sceneGizmoWasUsing_ = false;
            return;
        }

        const Assets::UniformBufferObject& ubo = engine_.GetLastUniformBufferObject();
        const glm::mat4& view = ubo.ModelView;
        glm::mat4 projection = ubo.Projection;
        projection[1][1] *= -1.0f;

        constexpr ImGuizmo::OPERATION operations[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE};
        sceneGizmoOperation_ = std::clamp(sceneGizmoOperation_, 0, 1);
        ImGuizmo::SetAlternativeWindow(nullptr);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);
        ImGuizmo::GetStyle().Colors[ImGuizmo::COLOR::SELECTION] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operations[sceneGizmoOperation_],
                             ImGuizmo::LOCAL, glm::value_ptr(worldMatrix));

        const bool usingNow = ImGuizmo::IsUsing();
        const bool interacting = ImGuizmo::IsOver() || usingNow;
        ImGui::GetIO().WantCaptureMouse = ImGui::GetIO().WantCaptureMouse || interacting;
        if (usingNow)
        {
            const glm::dmat4 basis = Assets::Scad::ScadToWorldBasis(1.0);
            const glm::dmat4 editedScad = glm::inverse(basis) * glm::dmat4(worldMatrix) * basis;
            glm::dvec3 scale(1.0);
            glm::dquat orientation(1.0, 0.0, 0.0, 0.0);
            glm::dvec3 translation(0.0);
            glm::dvec3 skew(0.0);
            glm::dvec4 perspective(0.0);
            if (glm::decompose(editedScad, scale, orientation, translation, skew, perspective))
            {
                orientation = glm::normalize(orientation);
                double angleZ = 0.0;
                double angleY = 0.0;
                double angleX = 0.0;
                glm::extractEulerAngleZYX(glm::toMat4(orientation), angleZ, angleY, angleX);
                item.x = static_cast<float>(translation.x);
                item.y = static_cast<float>(translation.y);
                item.z = static_cast<float>(translation.z);
                item.rotX = static_cast<float>(glm::degrees(angleX));
                item.rotY = static_cast<float>(glm::degrees(angleY));
                item.rotZ = static_cast<float>(glm::degrees(angleZ));
                item.scale = static_cast<float>(scale.x);
                item.scaleY = static_cast<float>(scale.y);
                item.scaleZ = static_cast<float>(scale.z);
                ApplySceneObjectTransform(item, worldMatrix);
                sceneGizmoWasUsing_ = true;
            }
            return;
        }

        if (sceneGizmoWasUsing_)
        {
            sceneGizmoWasUsing_ = false;
            CommitSceneGizmoEdit();
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
