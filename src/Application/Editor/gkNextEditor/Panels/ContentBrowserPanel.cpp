#include "EditorUi.hpp"

#include "EditorDragDrop.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "EditorActionDispatcher.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/FileHelper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fmt/format.h>
#include <functional>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <string_view>
#include <vector>

namespace Editor
{
    namespace
    {
        float GContentBrowserIconSize = 82.0f;
        constexpr float kCardAspect = 1.65f;

        struct ContentBrowserCallbacks
        {
            std::function<void()> onDoubleClick;
            std::function<void()> onContextMenu;
            std::function<void()> onDragSource;
        };

        enum class EContentAssetKind
        {
            Directory,
            Scene,
            Hdri,
            Script,
            Texture,
            LDraw,
            Config,
            Unsupported,
        };

        struct ContentAssetVisual
        {
            const char* icon = ICON_FA_FOLDER;
            ImU32 color = IM_COL32(0, 172, 255, 255);
            EContentAssetKind kind = EContentAssetKind::Directory;
        };

        struct ContentGridLayout
        {
            int itemsPerRow = 1;
            int index = 0;

            void Next()
            {
                if ((index + 1) % itemsPerRow != 0)
                {
                    ImGui::SameLine();
                }
                ++index;
            }
        };

        struct FilesystemPathHash
        {
            size_t operator()(const std::filesystem::path& path) const
            {
                return std::hash<std::string>{}(path.string());
            }
        };

        using DirectoryEntries = std::vector<std::filesystem::directory_entry>;
        using DirectoryCache = std::unordered_map<std::filesystem::path, DirectoryEntries, FilesystemPathHash>;

        ContentGridLayout BeginContentGrid()
        {
            const float windowWidth = ImGui::GetContentRegionAvail().x;
            const int itemsPerRow =
                std::max(1, static_cast<int>(windowWidth / (GContentBrowserIconSize + ImGui::GetStyle().ItemSpacing.x)));
            return ContentGridLayout{itemsPerRow, 0};
        }

        const ContentAssetVisual& ResolveAssetVisualForExtension(std::string_view extension)
        {
            static const ContentAssetVisual kUnsupported{ICON_FA_FOLDER, IM_COL32(0, 172, 255, 255),
                                                         EContentAssetKind::Unsupported};
            static const ContentAssetVisual kScene{ICON_FA_CUBE, IM_COL32(255, 172, 0, 255), EContentAssetKind::Scene};
            static const ContentAssetVisual kHdri{ICON_FA_FILE_IMAGE, IM_COL32(200, 64, 64, 255),
                                                  EContentAssetKind::Hdri};
            static const ContentAssetVisual kScript{ICON_FA_CODE, IM_COL32(84, 180, 255, 255),
                                                    EContentAssetKind::Script};
            static const ContentAssetVisual kTexture{ICON_FA_IMAGE, IM_COL32(88, 210, 132, 255),
                                                     EContentAssetKind::Texture};
            static const ContentAssetVisual kLDraw{ICON_FA_CUBES, IM_COL32(186, 130, 255, 255),
                                                   EContentAssetKind::LDraw};
            static const ContentAssetVisual kConfig{ICON_FA_FILE_LINES, IM_COL32(230, 205, 80, 255),
                                                    EContentAssetKind::Config};

            struct ExtensionVisual
            {
                std::string_view extension;
                const ContentAssetVisual* visual = nullptr;
            };

            static const std::array<ExtensionVisual, 9> kVisuals{
                ExtensionVisual{".hdr", &kHdri},
                ExtensionVisual{".js", &kScript},
                ExtensionVisual{".png", &kTexture},
                ExtensionVisual{".jpg", &kTexture},
                ExtensionVisual{".jpeg", &kTexture},
                ExtensionVisual{".tga", &kTexture},
                ExtensionVisual{".ldr", &kLDraw},
                ExtensionVisual{".mpd", &kLDraw},
                ExtensionVisual{".json", &kConfig},
            };

            if (Runtime::Scene::SceneList::IsSupportedSceneExtension(extension))
            {
                return kScene;
            }

            for (const auto& entry : kVisuals)
            {
                if (entry.extension == extension)
                {
                    return *entry.visual;
                }
            }

            return kUnsupported;
        }

        ContentAssetVisual ResolveAssetVisual(const std::filesystem::directory_entry& entry)
        {
            static const ContentAssetVisual kDirectory{};
            static const ContentAssetVisual kUnsupported{ICON_FA_FOLDER, IM_COL32(0, 172, 255, 255),
                                                         EContentAssetKind::Unsupported};

            if (entry.is_directory())
            {
                return kDirectory;
            }

            if (!entry.is_regular_file())
            {
                return kUnsupported;
            }

            const std::string ext = entry.path().extension().string();
            return ResolveAssetVisualForExtension(ext);
        }

        DirectoryEntries& GetCachedDirectoryEntries(const std::filesystem::path& path, DirectoryCache& directoryCache)
        {
            auto it = directoryCache.find(path);
            if (it != directoryCache.end())
            {
                return it->second;
            }

            DirectoryEntries entries;
            std::error_code error;
            std::filesystem::directory_iterator dirIt(path, error);
            if (error)
            {
                SPDLOG_WARN("Failed to read content browser directory '{}': {}", path.string(), error.message());
                auto [insertedIt, _] = directoryCache.emplace(path, std::move(entries));
                return insertedIt->second;
            }

            for (const auto& entry : dirIt)
            {
                entries.push_back(entry);
            }

            std::sort(entries.begin(), entries.end(),
                      [](const std::filesystem::directory_entry& lhs,
                         const std::filesystem::directory_entry& rhs)
                      {
                          const bool lhsDir = lhs.is_directory();
                          const bool rhsDir = rhs.is_directory();
                          if (lhsDir != rhsDir)
                          {
                              return lhsDir;
                          }
                          return lhs.path().filename().string() < rhs.path().filename().string();
                      });

            auto [insertedIt, _] = directoryCache.emplace(path, std::move(entries));
            return insertedIt->second;
        }

        bool IsSameOrParentPath(const std::filesystem::path& parent, const std::filesystem::path& child)
        {
            const std::string parentStr = parent.lexically_normal().string();
            const std::string childStr = child.lexically_normal().string();
            if (childStr == parentStr)
            {
                return true;
            }

            if (childStr.size() <= parentStr.size())
            {
                return false;
            }

            return childStr.rfind(parentStr + std::string(1, std::filesystem::path::preferred_separator), 0) == 0;
        }

        void DrawDirectoryTreeNode(
            const std::filesystem::path& directoryPath,
            std::filesystem::path& currentPath,
            DirectoryCache& directoryCache,
            bool isRoot)
        {
            auto& entries = GetCachedDirectoryEntries(directoryPath, directoryCache);
            bool hasDirectoryChildren = false;
            for (const auto& entry : entries)
            {
                if (entry.is_directory())
                {
                    hasDirectoryChildren = true;
                    break;
                }
            }

            std::string name = isRoot ? "Assets" : directoryPath.filename().string();
            if (!name.empty())
            {
                name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
            }

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow;
            if (!hasDirectoryChildren)
            {
                flags |= ImGuiTreeNodeFlags_Leaf;
            }
            if (currentPath == directoryPath)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (IsSameOrParentPath(directoryPath, currentPath))
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            }

            const std::string treeId = directoryPath.string();
            const std::string label = fmt::format("{} {}", ICON_FA_FOLDER, name);
            const bool open = ImGui::TreeNodeEx(treeId.c_str(), flags, "%s", label.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                currentPath = directoryPath;
            }

            if (open)
            {
                for (const auto& entry : entries)
                {
                    if (!entry.is_directory())
                    {
                        continue;
                    }
                    DrawDirectoryTreeNode(entry.path(), currentPath, directoryCache, false);
                }
                ImGui::TreePop();
            }
        }

        void DrawQuickAccessDirectory(const char* label, const std::filesystem::path& path,
                                      std::filesystem::path& currentPath)
        {
            if (!std::filesystem::exists(path))
            {
                return;
            }

            const bool selected = currentPath == path;
            if (ImGui::Selectable(label, selected))
            {
                currentPath = path;
            }
        }

        void DrawContentBrowserSidebar(EditorUiState& ui, const std::filesystem::path& rootPath,
                                       std::filesystem::path& currentPath,
                                       DirectoryCache& directoryCache)
        {
            (void)ui;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.48f));
            ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.82f));
            ImGui::BeginChild("ContentBrowserSidebar", ImVec2(200.0f, 0.0f), true);
            ImGui::TextDisabled("Favorites");
            DrawQuickAccessDirectory(ICON_FA_STAR " Assets", rootPath, currentPath);
            DrawQuickAccessDirectory(ICON_FA_FOLDER " Models", rootPath / "models", currentPath);
            DrawQuickAccessDirectory(ICON_FA_FOLDER " Materials", rootPath / "materials", currentPath);
            DrawQuickAccessDirectory(ICON_FA_FOLDER " Textures", rootPath / "textures", currentPath);
            NextUI::Theme::DrawThinSeparator(0.70f);
            ImGui::TextDisabled("Project");
            DrawDirectoryTreeNode(rootPath, currentPath, directoryCache, true);
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        void DrawContentBrowserNavigation(EditorUiState& ui, const std::filesystem::path& rootPath,
                                          std::filesystem::path& currentPath,
                                          DirectoryCache& directoryCache)
        {
            const std::string rootStr = rootPath.string();
            const std::string curStr = currentPath.string();
            if (curStr.rfind(rootStr, 0) != 0)
            {
                currentPath = rootPath;
            }

            if (ui.fontIcon)
            {
                ImGui::PushFont(ui.fontIcon);
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));

            if (ImGui::Button(ICON_FA_HOUSE))
            {
                currentPath = rootPath;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Assets Root");
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_ROTATE))
            {
                directoryCache.erase(currentPath);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Refresh Current Folder");
            }

            const std::filesystem::path rel = currentPath.lexically_relative(rootPath);
            if (!rel.empty() && rel != ".")
            {
                std::filesystem::path crumbPath = rootPath;
                for (const auto& part : rel)
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(">");
                    ImGui::SameLine();

                    std::string label = part.string();
                    if (!label.empty())
                    {
                        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                    }

                    crumbPath /= part;
                    ImGui::PushID(label.c_str());
                    if (ImGui::Button(label.c_str()))
                    {
                        currentPath = crumbPath;
                    }
                    ImGui::PopID();
                }
            }

            ImGui::PopStyleVar();
            if (ui.fontIcon)
            {
                ImGui::PopFont();
            }
        }

        uint32_t Fnv1a32(std::string_view s)
        {
            uint32_t hash = 2166136261u;
            for (unsigned char c : s)
            {
                hash ^= static_cast<uint32_t>(c);
                hash *= 16777619u;
            }
            return hash;
        }

        void DrawGeneralContentBrowser(EditorContext& ctx, EditorUiState& ui, uint32_t& selectionId, bool iconOrTex,
                                       uint32_t globalId, const std::string& name, const char* icon, ImU32 color,
                                       const ContentBrowserCallbacks& callbacks)
        {
            ImGui::BeginGroup();
            if (ui.bigIcon)
            {
                ImGui::PushFont(ui.bigIcon);
            }
            
            const ImVec2 cardMin = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0, ImGui::GetScrollY());
            const ImVec2 cardMid = cardMin + ImVec2(0, GContentBrowserIconSize);
            const ImVec2 cardMax = cardMin + ImVec2(GContentBrowserIconSize, GContentBrowserIconSize * kCardAspect);
            
            ImGui::GetWindowDrawList()->AddRectFilled(
            cardMin, cardMax,NextUI::Theme::ColorU32(NextUI::Theme::EColor::Background, 0.84f), 6);
                    
            ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Background));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
            ImGui::PushID(static_cast<int>(globalId));
            
            ImTextureID textureId = ctx.ui.RequestImTextureId(globalId);
            if (iconOrTex || textureId == 0)
            {
                ImGui::Button(icon, ImVec2(GContentBrowserIconSize, GContentBrowserIconSize));
            }
            else
            {
                ImGui::Image(textureId, ImVec2(GContentBrowserIconSize, GContentBrowserIconSize));
            }

            if (callbacks.onDragSource && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                callbacks.onDragSource();
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
            ImGui::PopStyleColor(2);
            if (ui.bigIcon)
            {
                ImGui::PopFont();
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_None))
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    selectionId = InvalidId;
                    if (callbacks.onDoubleClick)
                    {
                        callbacks.onDoubleClick();
                    }
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    selectionId = globalId;
                }
            }
            
            const bool selected = selectionId == globalId;
            
            ImGui::GetWindowDrawList()->AddRect(
                cardMin, cardMax,
                selected ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::AccentHover, 0.92f)
                         : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, 0.84f),
                6, 0, selected ? 1.4f : 1.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                cardMid, cardMax,
                selected ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Accent, 0.72f)
                         : NextUI::Theme::ColorU32(NextUI::Theme::EColor::SurfaceElevated),
                6);
            ImGui::GetWindowDrawList()->AddLine(cardMid, cardMid + ImVec2(GContentBrowserIconSize, 0), color, 2);

            float cursorPosY = ImGui::GetCursorPosY();
            ImGui::PushItemWidth(GContentBrowserIconSize);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + GContentBrowserIconSize);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
            ImGui::Text("%s", name.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopItemWidth();
            
            ImGui::SetCursorPosY(cursorPosY);

            ImGui::Dummy(ImVec2(0.0f, GContentBrowserIconSize * (kCardAspect - 1.0f)));
            ImGui::EndGroup();

            if (callbacks.onContextMenu)
            {
                if (ImGui::BeginPopupContextItem("Context"))
                {
                    callbacks.onContextMenu();
                    ImGui::EndPopup();
                }
            }
        }
    } // namespace

    int DrawMeshBrowserContents(EditorContext& ctx, EditorUiState& ui, ImGuiTextFilter* filter)
    {
        auto& allModels = ctx.scene.Models();
        int itemCount = 0;
        ContentGridLayout grid = BeginContentGrid();
        for (uint32_t i = 0; i < allModels.size(); ++i)
        {
            auto& model = allModels[i];
            const std::string name = fmt::format("{}_#{}", model.Name(), i);
            if (filter != nullptr && filter->IsActive() && !filter->PassFilter(name.c_str()))
            {
                continue;
            }

            uint32_t dummySelection = InvalidId;
            DrawGeneralContentBrowser(ctx, ui, dummySelection, true, i, name, ICON_FA_BOXES_PACKING,
                                      IM_COL32(132, 182, 255, 255),
                                      ContentBrowserCallbacks{});
            grid.Next();
            ++itemCount;
        }
        return itemCount;
    }

    int DrawMaterialBrowserContents(EditorContext& ctx, EditorUiState& ui, ImGuiTextFilter* filter)
    {
        auto& allMaterials = ctx.scene.Materials();
        int itemCount = 0;
        ContentGridLayout grid = BeginContentGrid();
        for (uint32_t i = 0; i < allMaterials.size(); ++i)
        {
            auto& mat = allMaterials[i];
            if (filter != nullptr && filter->IsActive() && !filter->PassFilter(mat.name_.c_str()))
            {
                continue;
            }

            DrawGeneralContentBrowser(ctx, ui, ui.selectedMaterialId, true, i, mat.name_, ICON_FA_BOWLING_BALL,
                                      IM_COL32(132, 255, 132, 255),
                                      ContentBrowserCallbacks{
                                          .onDoubleClick =
                                              [&]()
                                          {
                                              ui.selected_material = &(ctx.scene.Materials()[i]);
                                              ui.ed_material = true;
                                              OpenMaterialEditor(ctx, ui);
                                          },
                                          .onDragSource =
                                              [&]()
                                          {
                                              EditorDragDropPayload payload{};
                                              payload.type = EEditorDragPayloadType::Material;
                                              payload.materialId = i;
                                              ImGui::SetDragDropPayload(kEditorDragDropPayload, &payload,
                                                                        sizeof(payload));
                                              ImGui::TextUnformatted(mat.name_.c_str());
                                          },
                                      });

            grid.Next();
            ++itemCount;
        }
        return itemCount;
    }

    int DrawTextureBrowserContents(EditorContext& ctx, EditorUiState& ui, ImGuiTextFilter* filter)
    {
        auto& totalTextureMap = Assets::GlobalTexturePool::GetInstance()->TotalTextureMap();
        int itemCount = 0;
        ContentGridLayout grid = BeginContentGrid();
        for (auto& textureGroup : totalTextureMap)
        {
            if (filter != nullptr && filter->IsActive() && !filter->PassFilter(textureGroup.first.c_str()))
            {
                continue;
            }

            DrawGeneralContentBrowser(ctx, ui, ui.selectedTextureId, false, textureGroup.second.GlobalIdx_,
                                      textureGroup.first, ICON_FA_LINK_SLASH, IM_COL32(255, 72, 72, 255),
                                      ContentBrowserCallbacks{});
            grid.Next();
            ++itemCount;
        }
        return itemCount;
    }

    void DrawMeshBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Mesh Browser", nullptr);
        {
            NextUI::Theme::DrawPanelHeader(ICON_FA_BOXES_PACKING, "Meshes", "Scene model buffers");
            DrawMeshBrowserContents(ctx, ui, nullptr);
        }
        ImGui::End();
    }

    void DrawMaterialBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Material Browser", nullptr);
        {
            NextUI::Theme::DrawPanelHeader(ICON_FA_CIRCLE_HALF_STROKE, "Materials", "Drag materials onto viewport objects");
            DrawMaterialBrowserContents(ctx, ui, nullptr);
        }
        ImGui::End();
    }

    void DrawTextureBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Texture Browser", nullptr);
        {
            NextUI::Theme::DrawPanelHeader(ICON_FA_IMAGE, "Textures", "Loaded GPU textures");
            DrawTextureBrowserContents(ctx, ui, nullptr);
        }
        ImGui::End();
    }

    void DrawContentBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Content Browser", nullptr);
        {
            static const std::filesystem::path rootPath =
                std::filesystem::path(Utilities::FileHelper::GetPlatformFilePath("assets"));
            static std::filesystem::path currentPath = rootPath;
            static DirectoryCache directoryCache;
            static ImGuiTextFilter contentFilter;
            int itemCount = 0;
            int selectedCount = ui.selectedContentItemId != InvalidId ? 1 : 0;

            if (ImGui::BeginTabBar("ContentBrowserTabs"))
            {
                if (ImGui::BeginTabItem("Content Browser"))
                {
                    DrawContentBrowserSidebar(ui, rootPath, currentPath, directoryCache);
                    ImGui::SameLine();
                    
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.28f));
                    ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.82f));

                    ImGui::BeginChild("ContentRightFrame", ImVec2(0.0f, 0.0f));
                    ImGui::BeginChild("ContentBrowserMain", ImVec2(0.0f, -24.0f), true);

                    if (ImGui::Button(ICON_FA_PLUS " Add"))
                    {
                        ImGui::OpenPopup("ContentAddPopup");
                    }
                    if (ImGui::BeginPopup("ContentAddPopup"))
                    {
                        ImGui::MenuItem("Material", nullptr, false, false);
                        ImGui::MenuItem("Texture", nullptr, false, false);
                        ImGui::MenuItem("Scene", nullptr, false, false);
                        ImGui::MenuItem("Script", nullptr, false, false);
                        ImGui::EndPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_FILE_IMPORT " Import"))
                    {
                        SPDLOG_INFO("Content Browser import placeholder");
                    }
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save All"))
                    {
                        SPDLOG_INFO("Content Browser save all placeholder");
                    }
                    ImGui::SameLine();
                    DrawContentBrowserNavigation(ui, rootPath, currentPath, directoryCache);

                    NextUI::Theme::DrawThinSeparator();
                    ImGui::SetNextItemWidth(200.0f);
                    contentFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##ContentBrowserFilter", 200.0f);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(118.0f);
                    ImGui::SliderFloat("Thumbnail", &GContentBrowserIconSize, 52.0f, 108.0f, "%.0f");

                    NextUI::Theme::DrawThinSeparator(0.70f);
                    ImGui::BeginChild("Content Items", ImVec2(0.0f, 0.0f));

                    auto& entries = GetCachedDirectoryEntries(currentPath, directoryCache);
                    ContentGridLayout grid = BeginContentGrid();
                    for (auto& entry : entries)
                    {
                        const std::string abspath = absolute(entry.path()).string();
                        const std::string name = entry.path().filename().string();

                        const ContentAssetVisual visual = ResolveAssetVisual(entry);
                        if (visual.kind == EContentAssetKind::Unsupported)
                        {
                            continue;
                        }
                        if (contentFilter.IsActive() && !contentFilter.PassFilter(name.c_str()))
                        {
                            continue;
                        }

                        const uint32_t stableId = Fnv1a32(abspath);
                        DrawGeneralContentBrowser(
                            ctx, ui, ui.selectedContentItemId, true, stableId, name, visual.icon, visual.color,
                            ContentBrowserCallbacks{
                                .onDoubleClick =
                                    [&]()
                                {
                                    if (visual.kind == EContentAssetKind::Directory)
                                    {
                                        currentPath = entry.path();
                                    }
                                    else if (visual.kind == EContentAssetKind::Scene)
                                    {
                                        ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, abspath);
                                    }
                                    else if (visual.kind == EContentAssetKind::Hdri)
                                    {
                                        ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadHDRI, abspath);
                                    }
                                },
                                .onContextMenu =
                                    [&]()
                                {
                                    if (visual.kind == EContentAssetKind::Scene)
                                    {
                                        if (ImGui::MenuItem("Open Scene"))
                                        {
                                            ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, abspath);
                                        }
                                        if (ImGui::MenuItem("Add To Scene"))
                                        {
                                            ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadSceneAdd, abspath);
                                        }
                                    }
                                },
                                .onDragSource =
                                    [&]()
                                {
                                    if (visual.kind == EContentAssetKind::Scene)
                                    {
                                        EditorDragDropPayload payload{};
                                        payload.type = EEditorDragPayloadType::Scene;
                                        std::snprintf(payload.path, sizeof(payload.path), "%s", abspath.c_str());
                                        ImGui::SetDragDropPayload(kEditorDragDropPayload, &payload, sizeof(payload));
                                        ImGui::TextUnformatted(name.c_str());
                                    }
                                },
                            });

                        grid.Next();
                        ++itemCount;
                    }
                    ImGui::EndChild();
                    ImGui::EndChild();
                    selectedCount = ui.selectedContentItemId != InvalidId ? 1 : 0;
                    ImGui::Text("%d items (%d selected)", itemCount, selectedCount);
                    ImGui::EndChild();
                    ImGui::PopStyleColor(2);
                    
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Material Browser"))
                {
                    ImGui::BeginChild("Material Items", ImVec2(0.0f, -24.0f));
                    DrawMaterialBrowserContents(ctx, ui, &contentFilter);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Texture Browser"))
                {
                    ImGui::BeginChild("Texture Items", ImVec2(0.0f, -24.0f));
                    DrawTextureBrowserContents(ctx, ui, &contentFilter);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Mesh Browser"))
                {
                    ImGui::BeginChild("Mesh Items", ImVec2(0.0f, -24.0f));
                     DrawMeshBrowserContents(ctx, ui, &contentFilter);
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }
} // namespace Editor
