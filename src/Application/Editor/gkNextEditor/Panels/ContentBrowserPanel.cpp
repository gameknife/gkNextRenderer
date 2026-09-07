#include "EditorUi.hpp"

#include "EditorDragDrop.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "EditorActionDispatcher.hpp"
#include "Application/Editor/Common/Preview/AssetThumbnailRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/SceneContent/SceneList.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/FileHelper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fmt/format.h>
#include <functional>
#include <limits>
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
        // Left mode rail: same widget the renderer's rail uses, so the two read as one system.
        constexpr float kContentModeRailButtonSize = 30.0f;
        constexpr float kContentModeRailWidth = 42.0f;

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

        enum class EContentAssetSource
        {
            Filesystem,
            Pak,
            Mixed,
        };

        struct BrowserEntry
        {
            std::filesystem::path browserPath;
            std::string assetPath;
            std::string name;
            bool isDirectory = false;
            EContentAssetSource source = EContentAssetSource::Filesystem;
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

        using DirectoryEntries = std::vector<BrowserEntry>;
        using DirectoryCache = std::unordered_map<std::filesystem::path, DirectoryEntries, FilesystemPathHash>;
        using DirectoryVisibilityCache = std::unordered_map<std::filesystem::path, bool, FilesystemPathHash>;

        enum class EBrowserSection
        {
            Content,
            Material,
            Texture,
            Mesh,
        };

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
            static const ContentAssetVisual kAudio{ICON_FA_VOLUME_HIGH, IM_COL32(168, 85, 247, 255),
                                                   EContentAssetKind::Unsupported};

            struct ExtensionVisual
            {
                std::string_view extension;
                const ContentAssetVisual* visual = nullptr;
            };

            static const std::array<ExtensionVisual, 18> kVisuals{
                ExtensionVisual{".hdr", &kHdri},
                ExtensionVisual{".js", &kScript},
                ExtensionVisual{".cs", &kScript},
                ExtensionVisual{".mlscript", &kScript},
                ExtensionVisual{".slang", &kScript},
                ExtensionVisual{".hlsl", &kScript},
                ExtensionVisual{".glsl", &kScript},
                ExtensionVisual{".png", &kTexture},
                ExtensionVisual{".jpg", &kTexture},
                ExtensionVisual{".jpeg", &kTexture},
                ExtensionVisual{".tga", &kTexture},
                ExtensionVisual{".bmp", &kTexture},
                ExtensionVisual{".ldr", &kLDraw},
                ExtensionVisual{".mpd", &kLDraw},
                ExtensionVisual{".json", &kConfig},
                ExtensionVisual{".wav", &kAudio},
                ExtensionVisual{".mp3", &kAudio},
                ExtensionVisual{".ogg", &kAudio},
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

        ContentAssetVisual ResolveAssetVisual(const BrowserEntry& entry)
        {
            static const ContentAssetVisual kDirectory{};

            if (entry.isDirectory)
            {
                return kDirectory;
            }

            const std::string ext = std::filesystem::path(entry.assetPath).extension().string();
            return ResolveAssetVisualForExtension(ext);
        }

        std::string GetAssetPathForBrowserDirectory(const std::filesystem::path& rootPath,
                                                    const std::filesystem::path& path)
        {
            const std::filesystem::path rel = path.lexically_relative(rootPath);
            if (rel.empty() || rel == ".")
            {
                return "assets";
            }

            return Utilities::FileHelper::NormalizePathString(std::filesystem::path("assets") / rel);
        }

        std::string GetAssetPathForFilesystemEntry(const std::filesystem::path& rootPath,
                                                   const std::filesystem::path& path)
        {
            const std::filesystem::path rel = path.lexically_relative(rootPath);
            return Utilities::FileHelper::NormalizePathString(std::filesystem::path("assets") / rel);
        }

        void MergeBrowserEntry(DirectoryEntries& entries, BrowserEntry entry)
        {
            auto it = std::find_if(entries.begin(), entries.end(),
                                   [&](const BrowserEntry& existing)
                                   {
                                       return existing.assetPath == entry.assetPath &&
                                           existing.isDirectory == entry.isDirectory;
                                   });
            if (it == entries.end())
            {
                entries.push_back(std::move(entry));
                return;
            }

            if (it->source != entry.source)
            {
                it->source = EContentAssetSource::Mixed;
            }
        }

        DirectoryEntries& GetCachedDirectoryEntries(const std::filesystem::path& rootPath,
                                                    const std::filesystem::path& path,
                                                    DirectoryCache& directoryCache)
        {
            auto it = directoryCache.find(path);
            if (it != directoryCache.end())
            {
                return it->second;
            }

            DirectoryEntries entries;

            std::error_code existsError;
            if (std::filesystem::exists(path, existsError) && std::filesystem::is_directory(path, existsError))
            {
                std::error_code error;
                std::filesystem::directory_iterator dirIt(path, error);
                if (error)
                {
                    SPDLOG_WARN("Failed to read content browser directory '{}': {}", path.string(), error.message());
                }
                else
                {
                    for (const auto& entry : dirIt)
                    {
                        BrowserEntry browserEntry;
                        browserEntry.browserPath = entry.path();
                        browserEntry.assetPath = GetAssetPathForFilesystemEntry(rootPath, entry.path());
                        browserEntry.name = entry.path().filename().string();
                        browserEntry.isDirectory = entry.is_directory();
                        browserEntry.source = EContentAssetSource::Filesystem;
                        MergeBrowserEntry(entries, std::move(browserEntry));
                    }
                }
            }

            const std::string assetDirectoryPath = GetAssetPathForBrowserDirectory(rootPath, path);
            if (auto* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance())
            {
                const std::string prefix = assetDirectoryPath + "/";
                for (const auto& mountedEntry : pakSystem->ListMountedEntries(prefix))
                {
                    if (mountedEntry.size() <= prefix.size())
                    {
                        continue;
                    }

                    const std::string_view remainder(mountedEntry.c_str() + prefix.size(),
                                                     mountedEntry.size() - prefix.size());
                    const size_t slashPos = remainder.find('/');

                    BrowserEntry browserEntry;
                    if (slashPos == std::string_view::npos)
                    {
                        browserEntry.name = std::string(remainder);
                        browserEntry.assetPath = mountedEntry;
                        browserEntry.isDirectory = false;
                    }
                    else
                    {
                        browserEntry.name = std::string(remainder.substr(0, slashPos));
                        browserEntry.assetPath =
                            Utilities::FileHelper::NormalizePathString(std::filesystem::path(assetDirectoryPath) /
                                                                       browserEntry.name);
                        browserEntry.isDirectory = true;
                    }

                    browserEntry.browserPath = path / browserEntry.name;
                    browserEntry.source = EContentAssetSource::Pak;
                    MergeBrowserEntry(entries, std::move(browserEntry));
                }
            }

            std::sort(entries.begin(), entries.end(),
                      [](const BrowserEntry& lhs, const BrowserEntry& rhs)
                      {
                          if (lhs.isDirectory != rhs.isDirectory)
                          {
                              return lhs.isDirectory;
                          }
                          return lhs.name < rhs.name;
                      });

            auto [insertedIt, _] = directoryCache.emplace(path, std::move(entries));
            return insertedIt->second;
        }

        bool DirectoryHasVisibleContent(const std::filesystem::path& rootPath, const std::filesystem::path& path,
                                        DirectoryCache& directoryCache,
                                        DirectoryVisibilityCache& visibilityCache)
        {
            auto it = visibilityCache.find(path);
            if (it != visibilityCache.end())
            {
                return it->second;
            }

            auto& entries = GetCachedDirectoryEntries(rootPath, path, directoryCache);
            bool hasVisibleContent = false;
            for (const auto& entry : entries)
            {
                if (entry.isDirectory)
                {
                    if (DirectoryHasVisibleContent(rootPath, entry.browserPath, directoryCache, visibilityCache))
                    {
                        hasVisibleContent = true;
                        break;
                    }
                    continue;
                }

                if (ResolveAssetVisual(entry).kind != EContentAssetKind::Unsupported)
                {
                    hasVisibleContent = true;
                    break;
                }
            }

            visibilityCache.emplace(path, hasVisibleContent);
            return hasVisibleContent;
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
            const std::filesystem::path& rootPath,
            const std::filesystem::path& directoryPath,
            std::filesystem::path& currentPath,
            DirectoryCache& directoryCache,
            DirectoryVisibilityCache& visibilityCache,
            bool isRoot)
        {
            auto& entries = GetCachedDirectoryEntries(rootPath, directoryPath, directoryCache);
            bool hasDirectoryChildren = false;
            for (const auto& entry : entries)
            {
                if (entry.isDirectory &&
                    DirectoryHasVisibleContent(rootPath, entry.browserPath, directoryCache, visibilityCache))
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
                    if (!entry.isDirectory ||
                        !DirectoryHasVisibleContent(rootPath, entry.browserPath, directoryCache, visibilityCache))
                    {
                        continue;
                    }
                    DrawDirectoryTreeNode(rootPath, entry.browserPath, currentPath, directoryCache, visibilityCache,
                                          false);
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
                                       DirectoryCache& directoryCache,
                                       DirectoryVisibilityCache& visibilityCache)
        {
            (void)ui;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.40f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
            ImGui::BeginChild("ContentBrowserSidebar", ImVec2(200.0f, 0.0f), false);
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::TextDisabled("Favorites");
            DrawQuickAccessDirectory(ICON_FA_STAR " Assets", rootPath, currentPath);
            DrawQuickAccessDirectory(ICON_FA_FOLDER " Models", rootPath / "models", currentPath);
            DrawQuickAccessDirectory(ICON_FA_FOLDER " Materials", rootPath / "materials", currentPath);
            DrawQuickAccessDirectory(ICON_FA_FOLDER " Textures", rootPath / "textures", currentPath);
            ImGui::Spacing();
            ImGui::TextDisabled("Project");
            DrawDirectoryTreeNode(rootPath, rootPath, currentPath, directoryCache, visibilityCache, true);
            ImGui::EndChild();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(1);
        }

        void DrawContentBrowserNavigation(const std::filesystem::path& rootPath, std::filesystem::path& currentPath,
                                          DirectoryCache& directoryCache,
                                          DirectoryVisibilityCache& visibilityCache)
        {
            const std::string rootStr = rootPath.string();
            const std::string curStr = currentPath.string();
            if (curStr.rfind(rootStr, 0) != 0)
            {
                currentPath = rootPath;
            }

            // 返回上一级
            const bool hasParent = currentPath != rootPath;
            ImGui::BeginDisabled(!hasParent);
            if (NextUI::Theme::GhostButton(ICON_FA_ARROW_UP, "Go Up One Directory"))
            {
                currentPath = currentPath.parent_path();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (NextUI::Theme::GhostButton(ICON_FA_HOUSE, "Assets Root"))
            {
                currentPath = rootPath;
            }

            ImGui::SameLine();
            if (NextUI::Theme::GhostButton(ICON_FA_ROTATE, "Refresh Directory Cache"))
            {
                directoryCache.clear();
                visibilityCache.clear();
            }

            const std::filesystem::path rel = currentPath.lexically_relative(rootPath);
            if (!rel.empty() && rel != ".")
            {
                std::filesystem::path crumbPath = rootPath;
                for (const auto& part : rel)
                {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted, 0.7f));
                    ImGui::TextUnformatted(ICON_FA_CHEVRON_RIGHT);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();

                    std::string label = part.string();
                    if (!label.empty())
                    {
                        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                    }

                    crumbPath /= part;
                    ImGui::PushID(label.c_str());
                    const bool isCurrentLeaf = (crumbPath == currentPath);
                    if (isCurrentLeaf)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Accent));
                    }
                    if (NextUI::Theme::GhostButton(label.c_str()))
                    {
                        currentPath = crumbPath;
                    }
                    if (isCurrentLeaf)
                    {
                        ImGui::PopStyleColor();
                    }
                    ImGui::PopID();
                }
            }
        }

        void DrawContentBrowserStatus(int itemCount, int selectedCount)
        {
            static constexpr std::array<const char*, 3> thumbnailLabels = {"Small", "Medium", "Large"};
            static constexpr std::array<float, 3> thumbnailSizes = {56.0f, 82.0f, 108.0f};
            const char* settingsButtonLabel = ICON_FA_GEAR " Settings##ContentBrowserSettings";

            int thumbnailIndex = 0;
            float closestDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < static_cast<int>(thumbnailSizes.size()); ++i)
            {
                const float distance = std::abs(GContentBrowserIconSize - thumbnailSizes[i]);
                if (distance < closestDistance)
                {
                    thumbnailIndex = i;
                    closestDistance = distance;
                }
            }

            ImGui::Text("%d items (%d selected)", itemCount, selectedCount);
            const ImGuiStyle& style = ImGui::GetStyle();
            const float settingsButtonWidth =
                ImGui::CalcTextSize(" Settings").x + style.FramePadding.x * 2.0f;
            const float statusRight =
                ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - style.WindowPadding.x;
            const ImVec2 settingsPosition(
                statusRight - settingsButtonWidth,
                ImGui::GetItemRectMin().y);
            ImGui::SetCursorScreenPos(settingsPosition);
            if (NextUI::Theme::GhostButton(settingsButtonLabel, "Content Browser Settings"))
            {
                ImGui::OpenPopup("ContentBrowserSettingsPopup");
            }
            if (ImGui::BeginPopup("ContentBrowserSettingsPopup"))
            {
                if (ImGui::BeginMenu("Thumbnail Size"))
                {
                    for (int i = 0; i < static_cast<int>(thumbnailSizes.size()); ++i)
                    {
                        if (ImGui::MenuItem(thumbnailLabels[i], nullptr, thumbnailIndex == i))
                        {
                            GContentBrowserIconSize = thumbnailSizes[i];
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
        }

        void DrawBrowserSectionSidebar(EBrowserSection& section)
        {
            struct FSectionEntry
            {
                EBrowserSection section;
                const char* icon;
                const char* tooltip;
            };

            static const std::array<FSectionEntry, 4> entries{{
                {EBrowserSection::Content, ICON_FA_FOLDER_TREE, "Content Browser (Assets)"},
                {EBrowserSection::Material, ICON_FA_CIRCLE_HALF_STROKE, "Material Library"},
                {EBrowserSection::Texture, ICON_FA_IMAGE, "Texture Pool"},
                {EBrowserSection::Mesh, ICON_FA_BOXES_PACKING, "Mesh Buffers"},
            }};

            ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.40f));
            ImGui::BeginChild("ContentBrowserModeSidebar", ImVec2(kContentModeRailWidth, 0.0f), false);

            for (const auto& entry : entries)
            {
                if (NextUI::Theme::ModeRailButton(entry.icon, entry.tooltip, section == entry.section,
                                                  kContentModeRailButtonSize))
                {
                    section = entry.section;
                }
                ImGui::Spacing();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(1);
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

        const char* GetContentSourceBadge(EContentAssetSource source)
        {
            switch (source)
            {
            case EContentAssetSource::Filesystem:
                return "FS";
            case EContentAssetSource::Pak:
                return "PAK";
            case EContentAssetSource::Mixed:
                return "MIX";
            }

            return "";
        }

        ImU32 GetContentSourceBadgeColor(EContentAssetSource source)
        {
            switch (source)
            {
            case EContentAssetSource::Filesystem:
                return IM_COL32(49, 124, 255, 220);
            case EContentAssetSource::Pak:
                return IM_COL32(255, 145, 0, 220);
            case EContentAssetSource::Mixed:
                return IM_COL32(90, 190, 90, 220);
            }

            return IM_COL32(64, 64, 64, 220);
        }

        ImTextureID RequestMaterialPreviewTexture(EditorContext& ctx, uint32_t materialIndex, const Assets::FMaterial& material)
        {
            const uint32_t sampleSlot =
                EditorPreview::AssetThumbnails(ctx.engine.GetRenderer()).RequestMaterialThumbnail(
                    materialIndex, material);
            return sampleSlot == std::numeric_limits<uint32_t>::max()
                ? 0
                : ctx.ui.RequestImTextureIdRaw(sampleSlot);
        }

        ImTextureID RequestMeshPreviewTexture(EditorContext& ctx, uint32_t modelIndex, const Assets::Model& model)
        {
            const uint32_t sampleSlot =
                EditorPreview::AssetThumbnails(ctx.engine.GetRenderer()).RequestMeshThumbnail(modelIndex, model);
            return sampleSlot == std::numeric_limits<uint32_t>::max()
                ? 0
                : ctx.ui.RequestImTextureIdRaw(sampleSlot);
        }

        void DrawGeneralContentBrowser(EditorContext& ctx, EditorUiState& ui, uint32_t& selectionId, bool iconOrTex,
                                       uint32_t globalId, const std::string& name, const char* icon, ImU32 color,
                                       const ContentBrowserCallbacks& callbacks,
                                       const char* sourceBadge = nullptr,
                                       ImU32 sourceBadgeColor = IM_COL32(0, 0, 0, 0),
                                       ImTextureID thumbnailTextureId = 0)
        {
            ImGui::BeginGroup();
            
            const ImVec2 cardMin = ImGui::GetCursorScreenPos();
            const ImVec2 cardMid = cardMin + ImVec2(0, GContentBrowserIconSize);
            const ImVec2 cardMax = cardMin + ImVec2(GContentBrowserIconSize, GContentBrowserIconSize * kCardAspect);
            const bool selected = (selectionId == globalId);

            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // 卡片整体底色
            drawList->AddRectFilled(
                cardMin, cardMax,
                NextUI::Theme::ColorU32(NextUI::Theme::EColor::Background, 0.84f),
                6.0f);

            // 预览按钮/图标
            ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Background));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
            ImGui::PushID(static_cast<int>(globalId));
            
            ImTextureID textureId = thumbnailTextureId != 0 ? thumbnailTextureId : ctx.ui.RequestImTextureId(globalId);
            if ((iconOrTex && thumbnailTextureId == 0) || textureId == 0)
            {
                if (ui.bigIcon)
                {
                    ImGui::PushFont(ui.bigIcon);
                }
                // Logo 呈现纯白色，类型颜色展示在中间装饰条上
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(245, 245, 245, 240));
                ImGui::Button(icon, ImVec2(GContentBrowserIconSize, GContentBrowserIconSize));
                ImGui::PopStyleColor();
                if (ui.bigIcon)
                {
                    ImGui::PopFont();
                }
            }
            else
            {
                constexpr float pad = 4.0f;
                const ImVec2 imgMin = cardMin + ImVec2(pad, pad);
                const ImVec2 imgSize(GContentBrowserIconSize - pad * 2.0f, GContentBrowserIconSize - pad * 2.0f);
                ImGui::SetCursorScreenPos(imgMin);
                ImGui::Image(textureId, imgSize);

                ImGui::SetCursorScreenPos(cardMin);
                ImGui::InvisibleButton("##CardImgHit", ImVec2(GContentBrowserIconSize, GContentBrowserIconSize));
            }

            if (callbacks.onDragSource && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                callbacks.onDragSource();
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
            ImGui::PopStyleColor(3);

            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_None);
            if (hovered)
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    selectionId = InvalidId;
                    if (callbacks.onDoubleClick)
                    {
                        callbacks.onDoubleClick();
                    }
                }
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    selectionId = globalId;
                }
            }

            // 卡片下半部底色（文字托盘）
            drawList->AddRectFilled(
                cardMid, cardMax,
                selected ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Accent, 0.72f)
                         : hovered ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::SurfaceHover)
                                   : NextUI::Theme::ColorU32(NextUI::Theme::EColor::SurfaceElevated),
                6.0f, ImDrawFlags_RoundCornersBottom);

            // 中间装饰条：根据类型的颜色展示
            drawList->AddLine(
                ImVec2(cardMin.x, cardMid.y),
                ImVec2(cardMax.x, cardMid.y),
                color, 2.0f);

            // 选中或悬停时的焦点轮廓
            if (selected)
            {
                drawList->AddRect(cardMin, cardMax, NextUI::Theme::ColorU32(NextUI::Theme::EColor::AccentHover, 0.95f), 6.0f, 0, 1.4f);
            }
            else if (hovered)
            {
                drawList->AddRect(cardMin, cardMax, IM_COL32(255, 255, 255, 30), 6.0f, 0, 1.0f);
            }

            // 核心修复：文字严格截断并限制在卡片内部，绝对不溢出破坏 Grid
            constexpr float textPadX = 5.0f;
            const float maxTextW = GContentBrowserIconSize - textPadX * 2.0f;
            const std::string truncatedName = NextUI::Theme::TruncateWithEllipsis(name, maxTextW);
            const ImVec2 textSize = ImGui::CalcTextSize(truncatedName.c_str());
            const float textX = cardMin.x + std::floor((GContentBrowserIconSize - textSize.x) * 0.5f);
            const float textY = cardMid.y + 7.0f;

            drawList->PushClipRect(cardMid, cardMax, true);
            drawList->AddText(ImVec2(textX, textY),
                              selected ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text)
                                       : hovered ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text)
                                                 : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text, 0.90f),
                              truncatedName.c_str());
            drawList->PopClipRect();

            if (hovered)
            {
                NextUI::Theme::DrawTooltip(name.c_str());
            }

            // 恢复光标到卡片精确尺寸，绝不对外溢出任何像素
            ImGui::SetCursorScreenPos(cardMin);
            ImGui::Dummy(ImVec2(GContentBrowserIconSize, GContentBrowserIconSize * kCardAspect));
            
            // 右上角来源 Badge
            if (sourceBadge != nullptr && sourceBadge[0] != '\0')
            {
                ImFont* badgeFont = ImGui::GetFont();
                const float badgeFontSize = ImGui::GetFontSize() * 0.70f;
                const ImVec2 badgePadding(5.0f, 1.5f);
                const ImVec2 badgeTextSize = badgeFont->CalcTextSizeA(badgeFontSize, FLT_MAX, 0.0f, sourceBadge);
                const ImVec2 badgeMin(cardMax.x - badgeTextSize.x - badgePadding.x * 2.0f - 4.0f, cardMin.y + 4.0f);
                const ImVec2 badgeMax(cardMax.x - 4.0f, badgeMin.y + badgeTextSize.y + badgePadding.y * 2.0f);
                drawList->AddRectFilled(badgeMin, badgeMax, sourceBadgeColor, 3.0f);
                drawList->AddText(badgeFont, badgeFontSize, badgeMin + badgePadding,
                                  IM_COL32(255, 255, 255, 250), sourceBadge);
            }
            
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

            const ImTextureID previewTexture = RequestMeshPreviewTexture(ctx, i, model);
            DrawGeneralContentBrowser(ctx, ui, ui.selectedMeshId, true, i, name, ICON_FA_BOXES_PACKING,
                                      IM_COL32(132, 182, 255, 255),
                                      ContentBrowserCallbacks{},
                                      nullptr,
                                      IM_COL32(0, 0, 0, 0),
                                      previewTexture);
            if (ui.contentBrowserState.pendingRevealMeshId == i)
            {
                ImGui::SetScrollHereY(0.5f);
                ui.contentBrowserState.pendingRevealMeshId = InvalidId;
            }
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

            const ImTextureID previewTexture = RequestMaterialPreviewTexture(ctx, i, mat);
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
                                      },
                                      nullptr,
                                      IM_COL32(0, 0, 0, 0),
                                      previewTexture);
            if (ui.contentBrowserState.pendingRevealMaterialId == i)
            {
                ImGui::SetScrollHereY(0.5f);
                ui.contentBrowserState.pendingRevealMaterialId = InvalidId;
            }

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
                                      ContentBrowserCallbacks{
                                          .onDragSource =
                                              [&]()
                                          {
                                              EditorDragDropPayload payload{};
                                              payload.type = EEditorDragPayloadType::Texture;
                                              payload.textureId = textureGroup.second.GlobalIdx_;
                                              ImGui::SetDragDropPayload(kEditorDragDropPayload, &payload,
                                                                        sizeof(payload));
                                              ImGui::TextUnformatted(textureGroup.first.c_str());
                                          },
                                      });
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
                Utilities::FileHelper::GetRuntimeFilePath("assets");
            static DirectoryCache directoryCache;
            static DirectoryVisibilityCache visibilityCache;
            auto& browserState = ui.contentBrowserState;
            if (!browserState.initialized)
            {
                browserState.currentPath = rootPath;
                browserState.initialized = true;
            }
            browserState.currentSection = std::clamp(browserState.currentSection, 0, 3);
            EBrowserSection currentSection = static_cast<EBrowserSection>(browserState.currentSection);
            int itemCount = 0;
            int selectedCount = ui.selectedContentItemId != InvalidId ? 1 : 0;

            DrawBrowserSectionSidebar(currentSection);
            browserState.currentSection = static_cast<int>(currentSection);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.28f));
            {
                DrawContentBrowserSidebar(ui, rootPath, browserState.currentPath, directoryCache, visibilityCache);
                ImGui::SameLine();
                ImGui::BeginChild("ContentRightFrame", ImVec2(0.0f, 0.0f), 0, ImGuiWindowFlags_NoBackground);
                if (currentSection == EBrowserSection::Content)
                {
                    ImGui::BeginChild("ContentBrowserMain", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);

                    // 突出式导航与搜索工具栏容器
                    const float navBarHeight = ImGui::GetFrameHeight() + 8.0f;
                    const ImVec2 navBarMin = ImGui::GetCursorScreenPos();
                    const float availWidth = ImGui::GetContentRegionAvail().x;
                    const ImVec2 navBarMax = navBarMin + ImVec2(availWidth, navBarHeight);

                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(navBarMin, navBarMax,
                        NextUI::Theme::ColorU32(NextUI::Theme::EColor::SurfaceElevated, 1.0f), 6.0f);

                    ImGui::SetCursorScreenPos(navBarMin + ImVec2(6.0f, 4.0f));
                    ImGui::BeginGroup();
                    DrawContentBrowserNavigation(rootPath, browserState.currentPath, directoryCache, visibilityCache);
                    ImGui::EndGroup();

                    const float searchWidth = 200.0f;
                    const float searchX = navBarMax.x - searchWidth - 6.0f;
                    if (searchX > ImGui::GetItemRectMax().x + 8.0f)
                    {
                        ImGui::SameLine();
                        ImGui::SetCursorScreenPos(ImVec2(searchX, navBarMin.y + 4.0f));
                    }
                    else
                    {
                        ImGui::SameLine();
                    }
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.75f));
                    ImGui::SetNextItemWidth(searchWidth);
                    browserState.contentFilter.Draw("##ContentBrowserFilter", searchWidth);
                    if (!ImGui::IsItemActive() && browserState.contentFilter.InputBuf[0] == '\0')
                    {
                        const ImVec2 hintMin = ImGui::GetItemRectMin() + ImVec2(7.0f, 3.5f);
                        dl->AddText(hintMin, NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextDim), ICON_FA_MAGNIFYING_GLASS " Search...");
                    }
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar();

                    ImGui::SetCursorScreenPos(ImVec2(navBarMin.x, navBarMax.y + 6.0f));
                    ImGui::BeginChild("Content Items", ImVec2(0.0f, 0.0f), false);

                    auto& entries = GetCachedDirectoryEntries(rootPath, browserState.currentPath, directoryCache);
                    ContentGridLayout grid = BeginContentGrid();
                    for (auto& entry : entries)
                    {
                        const std::string assetPath = entry.assetPath;
                        const std::string name = entry.name;

                        const ContentAssetVisual visual = ResolveAssetVisual(entry);
                        if (visual.kind == EContentAssetKind::Unsupported)
                        {
                            continue;
                        }
                        if (visual.kind == EContentAssetKind::Directory &&
                            !DirectoryHasVisibleContent(rootPath, entry.browserPath, directoryCache, visibilityCache))
                        {
                            continue;
                        }
                        if (browserState.contentFilter.IsActive() &&
                            !browserState.contentFilter.PassFilter(name.c_str()))
                        {
                            continue;
                        }

                        const uint32_t stableId = Fnv1a32(assetPath);
                        ImTextureID thumbnailTextureId = 0;
                        if (visual.kind == EContentAssetKind::Texture)
                        {
                            const NextUI::IUserInterface::FUiTextureHandle texture = ctx.ui.RequestUiTexture(assetPath);
                            if (texture.valid)
                            {
                                thumbnailTextureId = texture.textureId;
                            }
                        }

                        DrawGeneralContentBrowser(
                            ctx, ui, ui.selectedContentItemId, true, stableId, name, visual.icon, visual.color,
                            ContentBrowserCallbacks{
                                .onDoubleClick =
                                    [&]()
                                {
                                    if (visual.kind == EContentAssetKind::Directory)
                                    {
                                        browserState.currentPath = entry.browserPath;
                                    }
                                    else if (visual.kind == EContentAssetKind::Scene)
                                    {
                                        ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, assetPath);
                                    }
                                    else if (visual.kind == EContentAssetKind::Hdri)
                                    {
                                        ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadHDRI, assetPath);
                                    }
                                },
                                .onContextMenu =
                                    [&]()
                                {
                                    if (visual.kind == EContentAssetKind::Scene)
                                    {
                                        if (ImGui::MenuItem("Open Scene"))
                                        {
                                            ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, assetPath);
                                        }
                                        if (ImGui::MenuItem("Add As Reference"))
                                        {
                                            ctx.actions.Dispatch(ctx, EEditorAction::IO_AddSceneReference, assetPath);
                                        }
                                        if (ImGui::MenuItem("Merge Into Scene"))
                                        {
                                            ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadSceneAdd, assetPath);
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
                                        std::snprintf(payload.path, sizeof(payload.path), "%s", assetPath.c_str());
                                        ImGui::SetDragDropPayload(kEditorDragDropPayload, &payload, sizeof(payload));
                                        ImGui::TextUnformatted(name.c_str());
                                    }
                                    else if (visual.kind == EContentAssetKind::Texture)
                                    {
                                        EditorDragDropPayload payload{};
                                        payload.type = EEditorDragPayloadType::Texture;
                                        std::snprintf(payload.path, sizeof(payload.path), "%s", assetPath.c_str());
                                        ImGui::SetDragDropPayload(kEditorDragDropPayload, &payload, sizeof(payload));
                                        ImGui::TextUnformatted(name.c_str());
                                    }
                                },
                            },
                            GetContentSourceBadge(entry.source),
                            GetContentSourceBadgeColor(entry.source),
                            thumbnailTextureId);

                        grid.Next();
                        ++itemCount;
                    }
                    ImGui::EndChild();
                    
                    ImGui::EndChild();
                    selectedCount = ui.selectedContentItemId != InvalidId ? 1 : 0;
                    DrawContentBrowserStatus(itemCount, selectedCount);
                }
                else if (currentSection == EBrowserSection::Material)
                {
                    ImGui::SetNextItemWidth(220.0f);
                    browserState.materialFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##MaterialBrowserFilter", 220.0f);
                    ImGui::Spacing();
                    ImGui::BeginChild("Material Items", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);
                    itemCount = DrawMaterialBrowserContents(ctx, ui, &browserState.materialFilter);
                    ImGui::EndChild();
                    selectedCount = ui.selectedMaterialId != InvalidId ? 1 : 0;
                    DrawContentBrowserStatus(itemCount, selectedCount);
                }
                else if (currentSection == EBrowserSection::Texture)
                {
                    ImGui::SetNextItemWidth(220.0f);
                    browserState.textureFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##TextureBrowserFilter", 220.0f);
                    ImGui::Spacing();
                    ImGui::BeginChild("Texture Items", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);
                    itemCount = DrawTextureBrowserContents(ctx, ui, &browserState.textureFilter);
                    ImGui::EndChild();
                    selectedCount = ui.selectedTextureId != InvalidId ? 1 : 0;
                    DrawContentBrowserStatus(itemCount, selectedCount);
                }
                else if (currentSection == EBrowserSection::Mesh)
                {
                    ImGui::SetNextItemWidth(220.0f);
                    browserState.meshFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##MeshBrowserFilter", 220.0f);
                    ImGui::Spacing();
                    ImGui::BeginChild("Mesh Items", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), false);
                    itemCount = DrawMeshBrowserContents(ctx, ui, &browserState.meshFilter);
                    ImGui::EndChild();
                    selectedCount = ui.selectedMeshId != InvalidId ? 1 : 0;
                    DrawContentBrowserStatus(itemCount, selectedCount);
                }
                ImGui::EndChild();
            }
            ImGui::PopStyleColor(1);
        }
        ImGui::End();
    }
} // namespace Editor
