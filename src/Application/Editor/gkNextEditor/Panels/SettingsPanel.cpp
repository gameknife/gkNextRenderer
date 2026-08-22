#include "Engine/Common/CoreMinimal.hpp"

#include "EditorUi.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/JsonHelpers.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/DevTools/CVarEditorPanel.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiWidgets.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

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
            std::string optionsProvider;
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
            {"cvar":"r.rendererType","label":"Renderer","widget":"combo","optionsProvider":"renderer"},
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
                        item.optionsProvider = itemJson.value("optionsProvider", "");
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
                nlohmann::json root;
                if (NextJson::TryLoadFile("assets/configs/ui/settings_panel.json", root))
                {
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

        struct FSettingsOption
        {
            int value = 0;
            const char* label = "";
        };

        std::vector<FSettingsOption> ResolveOptions(NextEngine& engine, const FSettingsItem& item)
        {
            std::vector<FSettingsOption> result;
            if (item.optionsProvider == "renderer")
            {
                const Rendering::FRendererChoiceCapabilities capabilities =
                    engine.GetRenderer().RendererChoiceCapabilities();
                for (const Rendering::FRendererChoice* choice : Rendering::AvailableRendererChoices(capabilities))
                {
                    result.push_back({static_cast<int>(choice->type), choice->displayName});
                }
                return result;
            }
            if (item.optionsProvider == "upscaler.type")
            {
                using namespace Rendering::Upscaler;
                for (uint32_t rawType = 0; rawType < static_cast<uint32_t>(EUpscalerType::Count); ++rawType)
                {
                    const auto type = static_cast<EUpscalerType>(rawType);
                    if (type == EUpscalerType::None || engine.GetRenderer().SupportsUpscaler(type))
                    {
                        result.push_back({static_cast<int>(rawType), GetUpscalerTypeInfo(rawType).name});
                    }
                }
                return result;
            }
            if (item.optionsProvider == "upscaler.quality")
            {
                using namespace Rendering::Upscaler;
                for (uint32_t rawMode = 0; rawMode <= static_cast<uint32_t>(EUpscaleMode::Auto); ++rawMode)
                {
                    result.push_back({static_cast<int>(rawMode), GetUpscaleModeInfo(rawMode).name});
                }
                return result;
            }

            for (int index = 0; index < static_cast<int>(item.options.size()); ++index)
            {
                result.push_back({index, item.options[index].c_str()});
            }
            return result;
        }

        void DrawItem(NextEngine& engine, NextCVar::FCVarSystem& cvars, const FSettingsItem& item)
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
            else if (item.widget == "combo" && info.type == NextCVar::ECVarType::Int)
            {
                int value = std::stoi(valueText);
                const std::vector<FSettingsOption> options = ResolveOptions(engine, item);
                const auto current = std::find_if(options.begin(), options.end(),
                                                  [value](const FSettingsOption& option)
                                                  { return option.value == value; });
                const char* preview = current != options.end() ? current->label : "Unavailable";
                if (ImGui::BeginCombo("##value", preview))
                {
                    for (const FSettingsOption& option : options)
                    {
                        if (ImGui::Selectable(option.label, value == option.value))
                        {
                            SetValue(cvars, item, std::to_string(option.value));
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

            NextUI::Foundation::Tooltip(item.tooltip.c_str());
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
        auto& panelState = ui.settings;

        auto& cvars = ctx.engine.GetCVarSystem();
        if (!layout.loaded)
        {
            layout = LoadLayout(cvars);
        }
        if (layout.categories.empty())
        {
            return;
        }
        panelState.selectedCategory =
            std::clamp(panelState.selectedCategory, 0, static_cast<int>(layout.categories.size()) - 1);

        ImGui::SetNextWindowSize(ImVec2(920.0f, 650.0f), ImGuiCond_FirstUseEver);
        NextUI::Theme::PushToolWindowStyle();
        if (!ImGui::Begin("Settings / Preferences", &ui.settingsPanel))
        {
            ImGui::End();
            NextUI::Theme::PopToolWindowStyle();
            return;
        }
        NextUI::Theme::PushToolWindowContentStyle();

        NextUI::Theme::DrawPanelHeader(
            ICON_FA_GEAR, "Preferences", "Editor and renderer settings apply immediately");

        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputTextWithHint("##SettingsSearch",
                                  "Search settings or CVar names",
                                  panelState.search,
                                  sizeof(panelState.search));
        ImGui::SameLine();
        ImGui::Checkbox("Advanced", &panelState.showAdvanced);
        ImGui::SameLine();
        if (ImGui::Button("All CVars"))
        {
            panelState.showAllCVars = true;
        }

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 10.0f;
        NextUI::Theme::BeginInsetPanel("##SettingsCategories", ImVec2(190.0f, -footerHeight));
        for (int i = 0; i < static_cast<int>(layout.categories.size()); ++i)
        {
            if (ImGui::Selectable(layout.categories[i].label.c_str(), panelState.selectedCategory == i,
                                  ImGuiSelectableFlags_None, ImVec2(0.0f, 30.0f)))
            {
                panelState.selectedCategory = i;
            }
        }
        NextUI::Theme::EndInsetPanel();
        ImGui::SameLine();

        NextUI::Theme::BeginInsetPanel("##SettingsContent", ImVec2(0.0f, -footerHeight));
        const FSettingsCategory& category = layout.categories[panelState.selectedCategory];
        for (const FSettingsGroup& group : category.groups)
        {
            if (group.advanced && !panelState.showAdvanced)
            {
                continue;
            }
            if (!NextUI::Theme::BeginSection(nullptr, group.label.c_str()))
            {
                continue;
            }
            for (const FSettingsItem& item : group.items)
            {
                if (item.advanced && !panelState.showAdvanced)
                {
                    continue;
                }
                if (panelState.search[0] != '\0' &&
                    !ContainsCaseInsensitive(item.label, panelState.search) &&
                    !ContainsCaseInsensitive(item.cvar, panelState.search))
                {
                    continue;
                }
                DrawItem(ctx.engine, cvars, item);
            }
            NextUI::Theme::EndSection();
        }
        NextUI::Theme::EndInsetPanel();

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
        NextUI::Theme::PopToolWindowContentStyle();
        ImGui::End();
        NextUI::Theme::PopToolWindowStyle();

        if (panelState.showAllCVars)
        {
            DevTools::DrawCVarEditorPanel(ctx.engine, panelState.showAllCVars);
        }
    }
}
