#include "Editor/EditorUi.hpp"

#include "Editor/EditorDragDrop.hpp"

#include "Assets/Core/Scene.hpp"
#include "Assets/GPU/TextureImage.hpp"
#include "Editor/EditorActionDispatcher.hpp"
#include "Runtime/Scene/SceneList.hpp"
#include "Runtime/Editor/UserInterface.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Utilities/FileHelper.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fmt/format.h>
#include <functional>
#include <string_view>
#include <vector>

namespace Editor
{
    namespace
    {
        constexpr int kIconSize = 96;
        constexpr int kIconPadding = 20;

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

        ContentGridLayout BeginContentGrid()
        {
            const float windowWidth = ImGui::GetContentRegionAvail().x;
            const int itemsPerRow =
                std::max(1, static_cast<int>(windowWidth / (kIconSize + ImGui::GetStyle().ItemSpacing.x)));
            return ContentGridLayout{itemsPerRow, 0};
        }

        const ContentAssetVisual& ResolveAssetVisualForExtension(std::string_view extension)
        {
            static const ContentAssetVisual kUnsupported{ICON_FA_FOLDER, IM_COL32(0, 172, 255, 255),
                                                         EContentAssetKind::Unsupported};
            static const ContentAssetVisual kScene{ICON_FA_CUBE, IM_COL32(255, 172, 0, 255), EContentAssetKind::Scene};
            static const ContentAssetVisual kHdri{ICON_FA_FILE_IMAGE, IM_COL32(200, 64, 64, 255),
                                                  EContentAssetKind::Hdri};

            struct ExtensionVisual
            {
                std::string_view extension;
                const ContentAssetVisual* visual = nullptr;
            };

            static const std::array<ExtensionVisual, 1> kVisuals{
                ExtensionVisual{".hdr", &kHdri},
            };

            if (SceneList::IsSupportedSceneExtension(extension))
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

        void DrawContentBrowserNavigation(EditorUiState& ui, const std::filesystem::path& rootPath,
                                          std::filesystem::path& currentPath)
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
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 32, 32, 255));
            ImGui::PushID(static_cast<int>(globalId));

            VkDescriptorSet textureId = ctx.ui.RequestImTextureId(globalId);
            if (iconOrTex || (VK_NULL_HANDLE == textureId))
            {
                ImGui::Button(icon, ImVec2(kIconSize, kIconSize));
            }
            else
            {
                ImGui::Image((ImTextureID)(intptr_t)textureId, ImVec2(kIconSize, kIconSize));
            }

            if (callbacks.onDragSource && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                callbacks.onDragSource();
                ImGui::EndDragDropSource();
            }

            ImGui::PopID();
            ImGui::PopStyleColor();
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

            auto cursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0, 4 + ImGui::GetScrollY());
            const bool selected = selectionId == globalId;
            ImGui::GetWindowDrawList()->AddRectFilled(cursorPos, cursorPos + ImVec2(kIconSize, kIconSize / 5.0f * 3.0f),
                                                      selected ? ActiveColor : IM_COL32(64, 64, 64, 255), 4);
            ImGui::GetWindowDrawList()->AddLine(cursorPos, cursorPos + ImVec2(kIconSize, 0), color, 2);

            ImGui::PushItemWidth(kIconSize);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kIconSize);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
            ImGui::Text("%s", name.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopItemWidth();

            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kIconPadding);
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

    void DrawMeshBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Mesh Browser", nullptr);
        {
            auto& allModels = ctx.scene.Models();
            ContentGridLayout grid = BeginContentGrid();
            for (uint32_t i = 0; i < allModels.size(); ++i)
            {
                auto& model = allModels[i];
                const std::string name = fmt::format("{}_#{}", model.Name(), i);
                uint32_t dummySelection = InvalidId;
                DrawGeneralContentBrowser(ctx, ui, dummySelection, true, i, name, ICON_FA_BOXES_PACKING,
                                          IM_COL32(132, 182, 255, 255),
                                          ContentBrowserCallbacks{.onDoubleClick = []() {}});
                grid.Next();
            }
        }
        ImGui::End();
    }

    void DrawMaterialBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Material Browser", nullptr);
        {
            auto& allMaterials = ctx.scene.Materials();
            ContentGridLayout grid = BeginContentGrid();
            for (uint32_t i = 0; i < allMaterials.size(); ++i)
            {
                auto& mat = allMaterials[i];
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
            }
        }
        ImGui::End();
    }

    void DrawTextureBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Texture Browser", nullptr);
        {
            auto& totalTextureMap = Assets::GlobalTexturePool::GetInstance()->TotalTextureMap();
            ContentGridLayout grid = BeginContentGrid();
            for (auto& textureGroup : totalTextureMap)
            {
                DrawGeneralContentBrowser(ctx, ui, ui.selectedTextureId, false, textureGroup.second.GlobalIdx_,
                                          textureGroup.first, ICON_FA_LINK_SLASH, IM_COL32(255, 72, 72, 255),
                                          ContentBrowserCallbacks{.onDoubleClick = []() {}});
                grid.Next();
            }
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

            DrawContentBrowserNavigation(ui, rootPath, currentPath);

            auto cursorPos = ImGui::GetWindowPos() + ImVec2(0, ImGui::GetCursorPos().y + 2);
            ImGui::NewLine();
            ImGui::GetWindowDrawList()->AddLine(cursorPos + ImVec2(0, 1),
                                                cursorPos + ImVec2(ImGui::GetWindowSize().x, 1),
                                                IM_COL32(20, 20, 20, 128), 1);
            ImGui::GetWindowDrawList()->AddLine(cursorPos, cursorPos + ImVec2(ImGui::GetWindowSize().x, 0),
                                                IM_COL32(20, 20, 20, 255), 1);

            ImGui::BeginChild("Content Items");

            std::filesystem::directory_iterator it(currentPath);
            ContentGridLayout grid = BeginContentGrid();
            for (auto& entry : it)
            {
                const std::string abspath = absolute(entry.path()).string();
                const std::string name = entry.path().filename().string();

                const ContentAssetVisual visual = ResolveAssetVisual(entry);
                if (visual.kind == EContentAssetKind::Unsupported)
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
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
} // namespace Editor
