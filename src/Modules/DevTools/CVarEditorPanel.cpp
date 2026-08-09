#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/DevTools/CVarEditorPanel.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"

#include <imgui.h>

namespace DevTools
{
    namespace
    {
        bool ContainsCaseInsensitive(std::string_view value, std::string_view query)
        {
            return std::search(value.begin(), value.end(), query.begin(), query.end(),
                               [](char lhs, char rhs)
                               {
                                   return std::tolower(static_cast<unsigned char>(lhs)) ==
                                       std::tolower(static_cast<unsigned char>(rhs));
                               }) != value.end();
        }

        std::string NamespaceOf(const std::string& name)
        {
            const size_t dot = name.find('.');
            return dot == std::string::npos ? "other" : name.substr(0, dot);
        }

        struct FCVarNamespacePresentation
        {
            std::string_view title;
            std::string_view description;
        };

        FCVarNamespacePresentation NamespacePresentation(std::string_view nameSpace)
        {
            if (nameSpace == "r")
            {
                return {"Rendering", "Renderer quality, lighting, tracing, upscaling and presentation."};
            }
            if (nameSpace == "sys")
            {
                return {"System", "Engine runtime, platform, simulation and asset-system behavior."};
            }
            if (nameSpace == "debug")
            {
                return {"Debug Tools", "Developer diagnostics, inspectors and debug overlays."};
            }
            if (nameSpace == "show")
            {
                return {"Viewport Display", "Scene visibility, helpers and viewport visualization."};
            }
            if (nameSpace == "ui")
            {
                return {"User Interface", "Application interface and overlay preferences."};
            }
            if (nameSpace == "game")
            {
                return {"Gameplay", "Game-specific runtime and simulation options."};
            }
            if (nameSpace == "physics")
            {
                return {"Physics", "Physics simulation and collision behavior."};
            }
            if (nameSpace == "audio")
            {
                return {"Audio", "Audio playback, mixing and diagnostics."};
            }
            if (nameSpace == "ai")
            {
                return {"AI", "Artificial-intelligence runtime and diagnostics."};
            }
            if (nameSpace == "net")
            {
                return {"Networking", "Network transport, replication and diagnostics."};
            }
            if (nameSpace == "editor")
            {
                return {"Editor", "Editor tools, interaction and authoring preferences."};
            }
            if (nameSpace == "camera")
            {
                return {"Camera", "Camera navigation, optics and view behavior."};
            }
            if (nameSpace == "other")
            {
                return {"General", "Runtime options without a dedicated namespace."};
            }
            return {nameSpace, "Module-specific runtime configuration."};
        }

        void SetValue(NextCVar::FCVarSystem& cvars, const std::string& name, const std::string& value)
        {
            std::string error;
            if (!cvars.SetValueFromString(name, value, NextCVar::ECVarSetBy::Console, &error))
            {
                SPDLOG_WARN("Failed to set CVar '{}': {}", name, error);
            }
        }

        void DrawValueEditor(NextCVar::FCVarSystem& cvars, const NextCVar::FCVarInfo& info)
        {
            const std::string valueText = cvars.GetValueString(info.name);
            const bool readOnly = NextCVar::HasFlag(info.flags, NextCVar::ECVarFlags::ReadOnly);
            ImGui::BeginDisabled(readOnly);
            ImGui::SetNextItemWidth(-1.0f);
            const std::string id = "##value_" + info.name;

            if (info.type == NextCVar::ECVarType::Bool)
            {
                bool value = valueText == "true";
                if (ImGui::Checkbox(id.c_str(), &value))
                {
                    SetValue(cvars, info.name, value ? "true" : "false");
                }
            }
            else if (info.type == NextCVar::ECVarType::Int)
            {
                int64_t value = std::stoll(valueText);
                bool changed = false;
                if (info.minValue && info.maxValue)
                {
                    const int64_t minValue = static_cast<int64_t>(*info.minValue);
                    const int64_t maxValue = static_cast<int64_t>(*info.maxValue);
                    changed = ImGui::SliderScalar(id.c_str(), ImGuiDataType_S64, &value, &minValue, &maxValue);
                }
                else
                {
                    const int64_t step = 1;
                    changed = ImGui::InputScalar(id.c_str(), ImGuiDataType_S64, &value, &step);
                }
                if (changed)
                {
                    SetValue(cvars, info.name, std::to_string(value));
                }
            }
            else if (info.type == NextCVar::ECVarType::Float)
            {
                float value = std::stof(valueText);
                bool changed = false;
                if (info.minValue && info.maxValue)
                {
                    changed = ImGui::SliderFloat(id.c_str(), &value, static_cast<float>(*info.minValue),
                                                 static_cast<float>(*info.maxValue), "%.3f");
                }
                else
                {
                    changed = ImGui::DragFloat(id.c_str(), &value, 0.01f, 0.0f, 0.0f, "%.4f");
                }
                if (changed)
                {
                    SetValue(cvars, info.name, fmt::format("{:.6f}", value));
                }
            }
            else
            {
                static std::unordered_map<std::string, std::array<char, 256>> buffers;
                auto [it, inserted] = buffers.try_emplace(info.name);
                if (inserted || !ImGui::IsAnyItemActive())
                {
                    std::snprintf(it->second.data(), it->second.size(), "%s", valueText.c_str());
                }
                if (ImGui::InputText(id.c_str(), it->second.data(), it->second.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    SetValue(cvars, info.name, it->second.data());
                }
            }
            ImGui::EndDisabled();
        }
    }

    void DrawCVarEditorPanel(NextEngine& engine, bool& panelVisible)
    {
        ImGui::SetNextWindowSize(ImVec2(980.0f, 700.0f), ImGuiCond_FirstUseEver);
        NextUI::Theme::PushToolWindowStyle();
        if (!ImGui::Begin("CVar Editor", &panelVisible))
        {
            ImGui::End();
            NextUI::Theme::PopToolWindowStyle();
            return;
        }
        // Keep the roomier title bar while using medium-sized controls in the content area.
        NextUI::Theme::PushToolWindowContentStyle();

        auto& cvars = engine.GetCVarSystem();
        static char search[128]{};
        const float toolbarActionsWidth = 210.0f;
        ImGui::SetNextItemWidth(std::max(260.0f, ImGui::GetContentRegionAvail().x - toolbarActionsWidth));
        ImGui::InputTextWithHint("##CVarSearch", "Search CVars by name or description", search, sizeof(search));
        ImGui::SameLine();
        if (ImGui::Button("Save User Settings"))
        {
            if (!cvars.SaveUserFiles())
            {
                SPDLOG_ERROR("Failed to save user CVar files");
            }
        }
        ImGui::TextDisabled("Runtime configuration  /  F4 to toggle");

        std::map<std::string, std::vector<NextCVar::FCVarInfo>> groups;
        cvars.ForEach(
            [&](const NextCVar::FCVarInfo& info)
            {
                if (search[0] != '\0' &&
                    !ContainsCaseInsensitive(info.name, search) &&
                    !ContainsCaseInsensitive(info.description, search))
                {
                    return;
                }
                groups[NamespaceOf(info.name)].push_back(info);
            });

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        for (auto& [nameSpace, entries] : groups)
        {
            const FCVarNamespacePresentation presentation = NamespacePresentation(nameSpace);
            const std::string header =
                fmt::format("{}  \xC2\xB7  {}    ({})", presentation.title, nameSpace, entries.size());
            if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                continue;
            }
            ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%.*s",
                               static_cast<int>(presentation.description.size()), presentation.description.data());
            ImGui::Dummy(ImVec2(0.0f, 2.0f));

            const std::string tableId = "##CVarTable_" + nameSpace;
            if (!ImGui::BeginTable(tableId.c_str(), 3,
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody))
            {
                continue;
            }
            ImGui::TableSetupColumn("Variable", ImGuiTableColumnFlags_WidthStretch, 0.40f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.48f);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 72.0f);
            ImGui::TableHeadersRow();

            for (const NextCVar::FCVarInfo& info : entries)
            {
                ImGui::PushID(info.name.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (!info.isDefault)
                {
                    ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::Accent), "%s", info.name.c_str());
                }
                else
                {
                    ImGui::TextUnformatted(info.name.c_str());
                }
                if (ImGui::IsItemHovered() && !info.description.empty())
                {
                    ImGui::SetTooltip("%s", info.description.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                DrawValueEditor(cvars, info);
                ImGui::TableSetColumnIndex(2);
                ImGui::BeginDisabled(info.isDefault);
                if (ImGui::SmallButton("Reset"))
                {
                    cvars.ResetToDefault(info.name);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTable();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        }
        NextUI::Theme::PopToolWindowContentStyle();
        ImGui::End();
        NextUI::Theme::PopToolWindowStyle();
    }
}
