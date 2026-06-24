#include "Engine/Common/CoreMinimal.hpp"

#include "EditorUi.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/DevTools/CVarEditorPanel.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"

#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>

namespace Editor
{
    namespace
    {
        struct FSettingsItem
        {
            std::string cvar;
            std::string label;
            std::string widget;
            std::string tooltip;
            std::vector<std::string> options;
            float minValue = 0.0f;
            float maxValue = 1.0f;
            float step = 0.1f;
            std::string format = "%.3f";
            bool hasRange = false;
            bool advanced = false;
            bool requiresRestart = false;
        };

        struct FSettingsGroup
        {
            std::string label;
            bool advanced = false;
            std::vector<FSettingsItem> items;
        };

        struct FSettingsCategory
        {
            std::string id;
            std::string label;
            std::vector<FSettingsGroup> groups;
        };

        struct FSettingsLayout
        {
            std::vector<FSettingsCategory> categories;
            bool loaded = false;
        };

        constexpr const char* kFallbackLayout = R"json(
        {"version":1,"categories":[
          {"id":"rendering","label":"Rendering","groups":[{"label":"Quality","items":[
            {"cvar":"r.rendererType","label":"Renderer","widget":"combo","options":["Path Tracing","Software Tracing","Software Modern","Voxel Tracing","Modern No Ambient"]},
            {"cvar":"r.samples","label":"Samples","widget":"slider_int","min":1,"max":16}
          ]}]},
          {"id":"editor","label":"Editor","groups":[{"label":"Interaction","items":[
            {"cvar":"ed.hoverHighlight","label":"Hover Preview Highlight","widget":"checkbox"},
            {"cvar":"ed.outlinerAutoScroll","label":"Auto-scroll Outliner","widget":"checkbox"}
          ]}]}
        ]})json";

        FSettingsLayout ParseLayout(const nlohmann::json& root, NextCVar::FCVarSystem& cvars)
        {
            FSettingsLayout layout;
            for (const auto& categoryJson : root.value("categories", nlohmann::json::array()))
            {
                FSettingsCategory category;
                category.id = categoryJson.value("id", "");
                category.label = categoryJson.value("label", category.id);
                for (const auto& groupJson : categoryJson.value("groups", nlohmann::json::array()))
                {
                    FSettingsGroup group;
                    group.label = groupJson.value("label", "");
                    group.advanced = groupJson.value("advanced", false);
                    for (const auto& itemJson : groupJson.value("items", nlohmann::json::array()))
                    {
                        FSettingsItem item;
                        item.cvar = itemJson.value("cvar", "");
                        NextCVar::FCVarInfo info;
                        if (item.cvar.empty() || !cvars.TryGetInfo(item.cvar, info))
                        {
                            SPDLOG_WARN("Settings manifest references unknown CVar '{}'", item.cvar);
                            continue;
                        }
                        item.label = itemJson.value("label", item.cvar);
                        item.widget = itemJson.value("widget", "text");
                        const bool typeMatches =
                            (item.widget == "checkbox" && info.type == NextCVar::ECVarType::Bool) ||
                            ((item.widget == "combo" || item.widget == "slider_int" ||
                              item.widget == "drag_int") && info.type == NextCVar::ECVarType::Int) ||
                            ((item.widget == "slider_float" || item.widget == "drag_float") &&
                             info.type == NextCVar::ECVarType::Float) ||
                            item.widget == "text";
                        if (!typeMatches)
                        {
                            SPDLOG_WARN("Settings manifest widget '{}' does not match CVar '{}' type",
                                        item.widget, item.cvar);
                            continue;
                        }
                        item.tooltip = itemJson.value("tooltip", info.description);
                        item.options = itemJson.value("options", std::vector<std::string>{});
                        item.hasRange = itemJson.contains("min") && itemJson.contains("max");
                        item.minValue = itemJson.value("min", 0.0f);
                        item.maxValue = itemJson.value("max", 1.0f);
                        item.step = itemJson.value("step", 0.1f);
                        item.format = itemJson.value("format", "%.3f");
                        item.advanced = itemJson.value("advanced", false);
                        item.requiresRestart = itemJson.value("requiresRestart", false);
                        group.items.push_back(std::move(item));
                    }
                    if (!group.items.empty())
                    {
                        category.groups.push_back(std::move(group));
                    }
                }
                if (!category.groups.empty())
                {
                    layout.categories.push_back(std::move(category));
                }
            }
            layout.loaded = true;
            return layout;
        }

        FSettingsLayout LoadLayout(NextCVar::FCVarSystem& cvars)
        {
            try
            {
                const std::string path =
                    Utilities::FileHelper::GetPlatformFilePath("assets/configs/ui/settings_panel.json");
                std::ifstream file(path);
                if (file.is_open())
                {
                    nlohmann::json root;
                    file >> root;
                    FSettingsLayout layout = ParseLayout(root, cvars);
                    if (!layout.categories.empty())
                    {
                        return layout;
                    }
                }
                SPDLOG_WARN("Settings manifest unavailable or empty; using built-in fallback");
            }
            catch (const std::exception& error)
            {
                SPDLOG_WARN("Failed to load settings manifest: {}; using built-in fallback", error.what());
            }
            return ParseLayout(nlohmann::json::parse(kFallbackLayout), cvars);
        }

        bool ContainsCaseInsensitive(std::string_view value, std::string_view query)
        {
            return std::search(value.begin(), value.end(), query.begin(), query.end(),
                               [](char lhs, char rhs)
                               {
                                   return std::tolower(static_cast<unsigned char>(lhs)) ==
                                       std::tolower(static_cast<unsigned char>(rhs));
                               }) != value.end();
        }

        void SetValue(NextCVar::FCVarSystem& cvars, const FSettingsItem& item, const std::string& value)
        {
            std::string error;
            if (!cvars.SetValueFromString(item.cvar, value, NextCVar::ECVarSetBy::Console, &error))
            {
                SPDLOG_WARN("Failed to set setting '{}': {}", item.cvar, error);
            }
        }

        void DrawItem(NextCVar::FCVarSystem& cvars, const FSettingsItem& item)
        {
            NextCVar::FCVarInfo info;
            if (!cvars.TryGetInfo(item.cvar, info))
            {
                return;
            }
            const std::string valueText = cvars.GetValueString(item.cvar);
            NextUI::Theme::BeginFormRow(item.label.c_str(), 0.45f, 150.0f, 220.0f);
            ImGui::PushID(item.cvar.c_str());
            ImGui::SetNextItemWidth(-1.0f);

            if (item.widget == "checkbox" && info.type == NextCVar::ECVarType::Bool)
            {
                bool value = valueText == "true";
                if (ImGui::Checkbox("##value", &value))
                {
                    SetValue(cvars, item, value ? "true" : "false");
                }
            }
            else if (item.widget == "combo" && info.type == NextCVar::ECVarType::Int && !item.options.empty())
            {
                int value = std::stoi(valueText);
                const char* preview = value >= 0 && value < static_cast<int>(item.options.size())
                    ? item.options[value].c_str() : "Unknown";
                if (ImGui::BeginCombo("##value", preview))
                {
                    for (int i = 0; i < static_cast<int>(item.options.size()); ++i)
                    {
                        if (ImGui::Selectable(item.options[i].c_str(), value == i))
                        {
                            SetValue(cvars, item, std::to_string(i));
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else if ((item.widget == "slider_int" || item.widget == "drag_int") &&
                     info.type == NextCVar::ECVarType::Int)
            {
                int value = std::stoi(valueText);
                const bool changed = item.widget == "slider_int"
                    ? ImGui::SliderInt("##value", &value, static_cast<int>(item.minValue),
                                       static_cast<int>(item.maxValue))
                    : ImGui::DragInt("##value", &value, item.step, static_cast<int>(item.minValue),
                                     static_cast<int>(item.maxValue));
                if (changed)
                {
                    SetValue(cvars, item, std::to_string(value));
                }
            }
            else if ((item.widget == "slider_float" || item.widget == "drag_float") &&
                     info.type == NextCVar::ECVarType::Float)
            {
                float value = std::stof(valueText);
                const bool changed = item.widget == "slider_float"
                    ? ImGui::SliderFloat("##value", &value, item.minValue, item.maxValue, item.format.c_str())
                    : ImGui::DragFloat("##value", &value, item.step, item.minValue, item.maxValue,
                                       item.format.c_str());
                if (changed)
                {
                    SetValue(cvars, item, fmt::format("{:.6f}", value));
                }
            }
            else
            {
                ImGui::TextUnformatted(valueText.c_str());
            }

            if (ImGui::IsItemHovered() && !item.tooltip.empty())
            {
                ImGui::SetTooltip("%s", item.tooltip.c_str());
            }
            if (item.requiresRestart)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("restart");
            }
            ImGui::PopID();
        }
    }

    void DrawSettingsPanel(EditorContext& ctx, EditorUiState& ui)
    {
        static FSettingsLayout layout;
        static int selectedCategory = 0;
        static bool showAdvanced = false;
        static bool showAllCVars = false;
        static char search[128]{};

        auto& cvars = ctx.engine.GetCVarSystem();
        if (!layout.loaded)
        {
            layout = LoadLayout(cvars);
        }
        if (layout.categories.empty())
        {
            return;
        }
        selectedCategory = std::clamp(selectedCategory, 0, static_cast<int>(layout.categories.size()) - 1);

        ImGui::SetNextWindowSize(ImVec2(920.0f, 650.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Settings / Preferences", &ui.settingsPanel))
        {
            ImGui::End();
            return;
        }

        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##SettingsSearch", "Search settings or CVar names", search, sizeof(search));
        ImGui::SameLine();
        ImGui::Checkbox("Advanced", &showAdvanced);
        ImGui::SameLine();
        if (ImGui::Button("All CVars"))
        {
            showAllCVars = true;
        }

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 10.0f;
        ImGui::BeginChild("##SettingsCategories", ImVec2(190.0f, -footerHeight), true);
        for (int i = 0; i < static_cast<int>(layout.categories.size()); ++i)
        {
            if (ImGui::Selectable(layout.categories[i].label.c_str(), selectedCategory == i))
            {
                selectedCategory = i;
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##SettingsContent", ImVec2(0.0f, -footerHeight), true);
        const FSettingsCategory& category = layout.categories[selectedCategory];
        for (const FSettingsGroup& group : category.groups)
        {
            if (group.advanced && !showAdvanced)
            {
                continue;
            }
            if (!NextUI::Theme::BeginSection(nullptr, group.label.c_str()))
            {
                continue;
            }
            for (const FSettingsItem& item : group.items)
            {
                if (item.advanced && !showAdvanced)
                {
                    continue;
                }
                if (search[0] != '\0' &&
                    !ContainsCaseInsensitive(item.label, search) &&
                    !ContainsCaseInsensitive(item.cvar, search))
                {
                    continue;
                }
                DrawItem(cvars, item);
            }
            NextUI::Theme::EndSection();
        }
        ImGui::EndChild();

        if (ImGui::Button("Reset Category"))
        {
            for (const FSettingsGroup& group : category.groups)
            {
                for (const FSettingsItem& item : group.items)
                {
                    cvars.ResetToDefault(item.cvar);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply & Save"))
        {
            if (!cvars.SaveUserFiles())
            {
                SPDLOG_ERROR("Failed to save editor settings");
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Changes apply immediately");
        ImGui::End();

        if (showAllCVars)
        {
            DevTools::DrawCVarEditorPanel(ctx.engine, showAllCVars);
        }
    }
}
