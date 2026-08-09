#include "Engine/Common/CoreMinimal.hpp"

#include "UiCatalog.hpp"

#include "Engine/Runtime/Editor/UI/UiContainers.hpp"
#include "Engine/Runtime/Editor/UI/UiScopes.hpp"
#include "Engine/Runtime/Editor/UI/UiTheme.hpp"
#include "Engine/Runtime/Editor/UI/UiWidgets.hpp"

namespace Runtime::DevToolsUI
{
    void DrawUiCatalog(bool& open)
    {
        if (!open)
        {
            return;
        }
        NextUI::Foundation::FScopedWindow window("UI Foundation Catalog###UiFoundationCatalog", &open);
        if (!window)
        {
            return;
        }

        ImGui::TextUnformatted("Semantic buttons");
        for (const auto variant : {
                 NextUI::Foundation::EButtonVariant::Primary,
                 NextUI::Foundation::EButtonVariant::Secondary,
                 NextUI::Foundation::EButtonVariant::Ghost,
                 NextUI::Foundation::EButtonVariant::Toolbar,
                 NextUI::Foundation::EButtonVariant::Danger})
        {
            NextUI::Foundation::FButtonOptions options;
            options.variant = variant;
            options.tooltip = "Unified tooltip, including disabled hover";
            NextUI::Foundation::Button("Button", options);
            ImGui::SameLine();
        }
        ImGui::NewLine();
        NextUI::Foundation::Button("Active", {.variant = NextUI::Foundation::EButtonVariant::Toolbar,
                                               .active = true});
        ImGui::SameLine();
        NextUI::Foundation::Button("Disabled", {.tooltip = "Disabled reason", .disabled = true});

        ImGui::SeparatorText("Containers");
        {
            NextUI::Foundation::FScopedToolbar toolbar("##CatalogToolbar", ImVec2(0.0f, 38.0f));
            if (toolbar)
            {
                NextUI::Foundation::IconButton("A", "Toolbar icon");
                ImGui::SameLine();
                NextUI::Foundation::IconButton("B", "Selected toolbar icon", true);
            }
        }
        {
            NextUI::Foundation::FScopedInsetPanel inset("##CatalogInset", ImVec2(0.0f, 90.0f));
            if (inset)
            {
                NextUI::Foundation::LabeledRow("Labeled row");
                ImGui::SetNextItemWidth(-1.0f);
                static char catalogText[32] = "Value";
                ImGui::InputText("##CatalogText", catalogText, sizeof(catalogText));
            }
        }

        ImGui::SeparatorText("Combo option");
        if (ImGui::BeginCombo("##CatalogCombo", "Selected"))
        {
            NextUI::Foundation::ComboOption("Selected", true);
            NextUI::Foundation::ComboOption("Alternative", false);
            ImGui::EndCombo();
        }
    }
}
