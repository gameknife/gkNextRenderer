#include "Editor/EditorUi.hpp"

#include "Assets/Scene.hpp"
#include "Assets/TextureImage.hpp"
#include "Editor/EditorActionDispatcher.hpp"
#include "Runtime/UserInterface.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Utilities/FileHelper.hpp"

#include <algorithm>
#include <cctype>
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
                                       const std::function<void()>& doubleclickAction,
                                       const std::function<void()>& contextMenuAction)
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
                    doubleclickAction();
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

            if (contextMenuAction)
            {
                if (ImGui::BeginPopupContextItem("Context"))
                {
                    contextMenuAction();
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

            const float windowWidth = ImGui::GetContentRegionAvail().x;
            const int itemsPerRow =
                std::max(1, static_cast<int>(windowWidth / (kIconSize + ImGui::GetStyle().ItemSpacing.x)));

            for (uint32_t i = 0; i < allModels.size(); ++i)
            {
                auto& model = allModels[i];
                const std::string name = fmt::format("{}_#{}", model.Name(), i);
                uint32_t dummySelection = InvalidId;
                DrawGeneralContentBrowser(
                    ctx, ui, dummySelection, true, i, name, ICON_FA_BOXES_PACKING, IM_COL32(132, 182, 255, 255),
                    []() {}, nullptr);
                if ((i + 1) % itemsPerRow != 0)
                {
                    ImGui::SameLine();
                }
            }
        }
        ImGui::End();
    }

    void DrawMaterialBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Material Browser", nullptr);
        {
            auto& allMaterials = ctx.scene.Materials();

            const float windowWidth = ImGui::GetContentRegionAvail().x;
            const int itemsPerRow =
                std::max(1, static_cast<int>(windowWidth / (kIconSize + ImGui::GetStyle().ItemSpacing.x)));

            for (uint32_t i = 0; i < allMaterials.size(); ++i)
            {
                auto& mat = allMaterials[i];
                DrawGeneralContentBrowser(
                    ctx, ui, ui.selectedMaterialId, true, i, mat.name_, ICON_FA_BOWLING_BALL,
                    IM_COL32(132, 255, 132, 255),
                    [&]()
                    {
                        ui.selected_material = &(ctx.scene.Materials()[i]);
                        ui.ed_material = true;
                        OpenMaterialEditor(ctx, ui);
                    },
                    nullptr);

                if ((i + 1) % itemsPerRow != 0)
                {
                    ImGui::SameLine();
                }
            }
        }
        ImGui::End();
    }

    void DrawTextureBrowserPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Texture Browser", nullptr);
        {
            auto& totalTextureMap = Assets::GlobalTexturePool::GetInstance()->TotalTextureMap();

            const float windowWidth = ImGui::GetContentRegionAvail().x;
            const int itemsPerRow =
                std::max(1, static_cast<int>(windowWidth / (kIconSize + ImGui::GetStyle().ItemSpacing.x)));

            int itemIndex = 0;
            for (auto& textureGroup : totalTextureMap)
            {
                DrawGeneralContentBrowser(
                    ctx, ui, ui.selectedTextureId, false, textureGroup.second.GlobalIdx_, textureGroup.first,
                    ICON_FA_LINK_SLASH, IM_COL32(255, 72, 72, 255), []() {}, nullptr);

                if ((itemIndex++ + 1) % itemsPerRow != 0)
                {
                    ImGui::SameLine();
                }
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

            // Safety: keep browsing rooted under assets.
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
                currentPath = rootPath;

            const std::filesystem::path rel = currentPath.lexically_relative(rootPath);
            std::vector<std::filesystem::path> parts;
            if (!rel.empty() && rel != ".")
            {
                for (const auto& p : rel)
                {
                    parts.push_back(p);
                }
            }

            for (int i = 0; i < static_cast<int>(parts.size()); ++i)
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(">");
                ImGui::SameLine();

                std::string label = parts[i].string();
                if (!label.empty())
                {
                    label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
                }

                if (ImGui::Button(label.c_str()))
                {
                    std::filesystem::path newPath = rootPath;
                    for (int j = 0; j <= i; ++j)
                    {
                        newPath /= parts[j];
                    }
                    currentPath = newPath;
                }
            }
            ImGui::PopStyleVar();
            if (ui.fontIcon)
            {
                ImGui::PopFont();
            }

            auto cursorPos = ImGui::GetWindowPos() + ImVec2(0, ImGui::GetCursorPos().y + 2);
            ImGui::NewLine();
            ImGui::GetWindowDrawList()->AddLine(cursorPos + ImVec2(0, 1),
                                                cursorPos + ImVec2(ImGui::GetWindowSize().x, 1),
                                                IM_COL32(20, 20, 20, 128), 1);
            ImGui::GetWindowDrawList()->AddLine(cursorPos, cursorPos + ImVec2(ImGui::GetWindowSize().x, 0),
                                                IM_COL32(20, 20, 20, 255), 1);

            ImGui::BeginChild("Content Items");

            std::filesystem::directory_iterator it(currentPath);
            const float windowWidth = ImGui::GetContentRegionAvail().x;
            const int itemsPerRow =
                std::max(1, static_cast<int>(windowWidth / (kIconSize + ImGui::GetStyle().ItemSpacing.x)));

            uint32_t elementIdx = 0;
            for (auto& entry : it)
            {
                const std::string abspath = absolute(entry.path()).string();
                const std::string name = entry.path().filename().string();
                const std::string ext = entry.path().extension().string();

                const char* icon = ICON_FA_FOLDER;
                ImU32 color = IM_COL32(0, 172, 255, 255);
                if (entry.is_regular_file())
                {
                    if (ext == ".glb")
                    {
                        icon = ICON_FA_CUBE;
                        color = IM_COL32(255, 172, 0, 255);
                    }
                    else if (ext == ".hdr")
                    {
                        icon = ICON_FA_FILE_IMAGE;
                        color = IM_COL32(200, 64, 64, 255);
                    }
                    else
                    {
                        continue;
                    }
                }

                const uint32_t stableId = Fnv1a32(abspath);
                DrawGeneralContentBrowser(
                    ctx, ui, ui.selectedContentItemId, true, stableId, name, icon, color,
                    [&]()
                    {
                        if (entry.is_directory())
                        {
                            currentPath = entry.path();
                        }
                        else
                        {
                            if (ext == ".glb")
                            {
                                ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, abspath);
                            }
                            else if (ext == ".hdr")
                            {
                                ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadHDRI, abspath);
                            }
                        }
                    },
                    [&]()
                    {
                        if (ext == ".glb")
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
                    });

                if ((elementIdx + 1) % itemsPerRow != 0)
                {
                    ImGui::SameLine();
                }
                elementIdx++;
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
} // namespace Editor
