#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/DevTools/CVarEditorPanel.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"

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
        ImGui::SetNextWindowSize(ImVec2(860.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("CVar Editor", &panelVisible))
        {
            ImGui::End();
            return;
        }

        auto& cvars = engine.GetCVarSystem();
        static char search[128]{};
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##CVarSearch", "Search name or description", search, sizeof(search));
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            if (!cvars.SaveUserFiles())
            {
                SPDLOG_ERROR("Failed to save user CVar files");
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("F4 / cvar.editor");

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

        ImGui::Separator();
        for (auto& [nameSpace, entries] : groups)
        {
            const std::string header = fmt::format("{} ({})", nameSpace, entries.size());
            if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                continue;
            }
            const std::string tableId = "##CVarTable_" + nameSpace;
            if (!ImGui::BeginTable(tableId.c_str(), 3,
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
            {
                continue;
            }
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.43f);
            ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, 62.0f);
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
        }
        ImGui::End();
    }
}
