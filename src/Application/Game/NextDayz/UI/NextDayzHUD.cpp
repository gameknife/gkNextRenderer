#include "NextDayzHUD.hpp"

#include <cstdio>

#include <imgui.h>

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"

namespace NextDayz::NextDayzHUD
{
    namespace
    {
        const char* KindLabel(EItemKind kind)
        {
            switch (kind)
            {
            case EItemKind::Weapon:   return "Weapon";
            case EItemKind::Ammo:     return "Ammo";
            case EItemKind::Clothing: return "Clothing";
            case EItemKind::Misc:
            default:                  return "Misc";
            }
        }

        void DrawCrosshair(const FHudContext& context)
        {
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            const ImU32 color = IM_COL32(235, 235, 235, 220);
            const float gap = context.aiming ? 2.0f : 5.0f;
            const float len = context.aiming ? 4.0f : 8.0f;
            const float thickness = 1.6f;
            draw->AddLine(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y), color, thickness);
            draw->AddLine(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y), color, thickness);
            draw->AddLine(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap), color, thickness);
            draw->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len), color, thickness);
            draw->AddCircleFilled(center, 1.2f, color);
        }

        void DrawClock(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 16.0f, vp->Pos.y + 16.0f), ImGuiCond_Always,
                                    ImVec2(1.0f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("##nd_clock", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::Text("%02d:%02d", context.hour, context.minute);
            ImGui::SameLine();
            ImGui::TextColored(context.overcast ? ImVec4(0.7f, 0.75f, 0.8f, 1.0f) : ImVec4(1.0f, 0.9f, 0.5f, 1.0f),
                               context.overcast ? "Overcast" : "Clear");
            ImGui::End();
        }

        void DrawWeaponReadout(const FHudContext& context)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 16.0f, vp->Pos.y + vp->Size.y - 16.0f),
                                    ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("##nd_weapon", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            if (!context.hasWeapon)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Unarmed");
            }
            else if (context.reloading)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s  Reloading...", context.weaponName.c_str());
            }
            else
            {
                ImGui::Text("%s", context.weaponName.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.9f, 0.95f, 1.0f, 1.0f), "  %d / %d", context.ammoInMag,
                                   context.ammoReserve);
            }
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Slot %d  [%s]", context.activeSlot + 1,
                               context.firstPerson ? "FPS" : "TPS");
            ImGui::End();
        }

        void DrawInteractionPrompt(const FHudContext& context)
        {
            if (context.interactionPrompt.empty())
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.62f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("##nd_interact", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::Text("[E] Pick up %s", context.interactionPrompt.c_str());
            ImGui::End();
        }

        void DrawInventoryPanel(const FHudContext& context)
        {
            if (!context.showInventory || context.inventory == nullptr)
            {
                return;
            }
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(420.0f, 460.0f), ImGuiCond_Appearing);
            ImGui::Begin("Backpack", nullptr, ImGuiWindowFlags_NoCollapse);

            if (context.weapons != nullptr)
            {
                const std::string& primary = context.weapons->SlotWeaponId(0);
                const std::string& secondary = context.weapons->SlotWeaponId(1);
                ImGui::Text("Primary: %s", primary.empty() ? "-" : primary.c_str());
                ImGui::Text("Secondary: %s", secondary.empty() ? "-" : secondary.c_str());
            }
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Items");
            for (const FItemStack& stack : context.inventory->Items())
            {
                ImGui::PushID(stack.id.c_str());
                ImGui::Text("%-16s x%-3d (%s)", stack.displayName.c_str(), stack.count, KindLabel(stack.kind));

                if (stack.kind == EItemKind::Weapon && context.equipWeapon)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Primary"))
                    {
                        context.equipWeapon(stack.id, 0);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Secondary"))
                    {
                        context.equipWeapon(stack.id, 1);
                    }
                }
                else if (stack.kind == EItemKind::Clothing && context.toggleClothing)
                {
                    const bool worn = context.inventory->IsClothingEquipped(stack.id);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(worn ? "Take off" : "Wear"))
                    {
                        context.toggleClothing(stack.id, !worn);
                    }
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tab/I close  |  1/2 switch  |  R reload  |  V view");
            ImGui::End();
        }
    }

    void Draw(const FHudContext& context)
    {
        if (context.firstPerson)
        {
            DrawCrosshair(context);
        }
        DrawClock(context);
        DrawWeaponReadout(context);
        DrawInteractionPrompt(context);
        DrawInventoryPanel(context);
    }
}
