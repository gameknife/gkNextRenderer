#include "NextDayzHUD.hpp"

#include <algorithm>
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
            case EItemKind::Consumable: return "Consumable";
            case EItemKind::Melee: return "Melee";
            case EItemKind::Misc:
            default:                  return "Misc";
            }
        }

        void DrawCrosshair(const FHudContext& context)
        {
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            const ImU32 color = IM_COL32(235, 235, 235, 220);
            const ImU32 outline = IM_COL32(20, 20, 20, 210);
            const float gap = context.firstPerson && context.aiming ? 2.0f : 6.0f;
            const float len = context.firstPerson && context.aiming ? 5.0f : 10.0f;
            const auto line = [&](const ImVec2& a, const ImVec2& b) {
                draw->AddLine(a, b, outline, 4.0f);
                draw->AddLine(a, b, color, 1.8f);
            };
            line(ImVec2(center.x - gap - len, center.y), ImVec2(center.x - gap, center.y));
            line(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + len, center.y));
            line(ImVec2(center.x, center.y - gap - len), ImVec2(center.x, center.y - gap));
            line(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + len));
            draw->AddCircleFilled(center, 2.4f, outline);
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

        void DrawObjective(const FHudContext& context)
        {
            if (context.objective.empty())
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->Pos.y + 16.0f), ImGuiCond_Always,
                                    ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("##nd_objective", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            ImGui::TextDisabled("SURVIVAL GOAL");
            ImGui::SameLine();
            ImGui::TextUnformatted(context.objective.c_str());
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
            ImGui::TextUnformatted(context.interactionPrompt.c_str());
            ImGui::End();
        }

        void DrawSurvival(const FHudContext& context)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + viewport->Size.y - 16.0f),
                                    ImGuiCond_Always, ImVec2(0.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("##nd_survival", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
            const auto bar = [](const char* label, float value, const ImVec4& color)
            {
                ImGui::TextUnformatted(label);
                ImGui::SameLine(78.0f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
                ImGui::ProgressBar(value / 100.0f, ImVec2(145.0f, 12.0f), "");
                ImGui::PopStyleColor();
            };
            bar("Health", context.survival.health, ImVec4(0.78f, 0.18f, 0.16f, 1.0f));
            bar("Hunger", context.survival.hunger, ImVec4(0.82f, 0.60f, 0.16f, 1.0f));
            bar("Hydration", context.survival.hydration, ImVec4(0.18f, 0.52f, 0.88f, 1.0f));
            if (context.survival.lifeState == EPlayerLifeState::Dead)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.25f, 1.0f), "DEAD - %s",
                                   context.survival.lastDamageSource.c_str());
            }
            ImGui::End();
        }

        void DrawDebugPanel(const FHudContext& context)
        {
            if (!context.showDebugPanel)
            {
                return;
            }

            const FDebugHudState& debug = context.debug;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + 16.0f),
                                    ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.88f);
            ImGui::Begin("NextDayz Debug", nullptr,
                         ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::TextDisabled("F5 hide | F6 zombies | F7 loot | F8 hit proxies");

            if (ImGui::CollapsingHeader("Camera"))
            {
                ImGui::Text("View            %s", context.firstPerson ? "FPS" : "TPS");
                ImGui::Text("Position        %.2f  %.2f  %.2f",
                            debug.position.x, debug.position.y, debug.position.z);
                ImGui::Text("Eye             %.2f  %.2f  %.2f",
                            debug.eyePosition.x, debug.eyePosition.y, debug.eyePosition.z);
                ImGui::Text("Yaw / Pitch     %.1f / %.1f deg",
                            glm::degrees(debug.yawRadians), glm::degrees(debug.pitchRadians));
                ImGui::Text("FOV             %.1f deg", debug.fovDegrees);
                ImGui::Text("Camera recoil   %.2f / %.2f deg%s",
                            glm::degrees(debug.cameraRecoilRadians.x),
                            glm::degrees(debug.cameraRecoilRadians.y),
                            debug.cameraRecoilActive ? "  active" : "");
            }

            if (ImGui::CollapsingHeader("Character"))
            {
                ImGui::Text("Stance          %s -> %s",
                            debug.stance.c_str(), debug.desiredStance.c_str());
                ImGui::Text("Gait            %s%s", debug.gait.c_str(),
                            debug.sprinting ? "  (sprinting)" : "");
                ImGui::Text("Jump            %s  %.3f", debug.jumpPhase.c_str(),
                            debug.jumpPhaseTime);
                ImGui::Text("Traversal       %s  %.3f  height %.2f m",
                            debug.traversalAction.c_str(), debug.traversalTime,
                            debug.traversalHeight);
                ImGui::Text("Traversal probe %s",
                            debug.traversalProbeResult.c_str());
                ImGui::Text("Grounded        %s", debug.onGround ? "yes" : "no");
                ImGui::Text("Move input      %.2f  %.2f", debug.localMove.x, debug.localMove.y);
                ImGui::Text("Velocity        %.2f  %.2f  %.2f",
                            debug.velocity.x, debug.velocity.y, debug.velocity.z);
                ImGui::Text("Horizontal      %.2f m/s", debug.horizontalSpeed);
                ImGui::Text("Capsule height  %.2f m%s", debug.controllerHeight,
                            debug.standBlocked ? "  STAND BLOCKED" : "");
            }

            if (ImGui::CollapsingHeader("Animation"))
            {
                ImGui::Text("Base clip       %s",
                            debug.baseAnimation.empty() ? "-" : debug.baseAnimation.c_str());
                ImGui::Text("Aim layer       %.3f", debug.aimWeight);
                ImGui::ProgressBar(debug.aimWeight, ImVec2(-1.0f, 3.0f), "");
                ImGui::Text("Action          %s  %.3f%s", debug.action.c_str(), debug.actionTime,
                            debug.actionCommitted ? "  committed" : "");
                ImGui::SeparatorText("Weapon action layer");
                ImGui::Text("Clip            %s",
                            debug.weaponActionClip.empty() ? "-" : debug.weaponActionClip.c_str());
                ImGui::Text("State / Weight  %s / %.3f", debug.weaponAction.c_str(),
                            debug.weaponActionWeight);
                ImGui::ProgressBar(debug.weaponActionTime, ImVec2(-1.0f, 0.0f), "clip time");
                ImGui::Text("Recoil          camera:%s  rig:%s  view:%s",
                            debug.cameraRecoilActive ? "on" : "off",
                            debug.rigRecoilActive ? "on" : "off",
                            debug.viewModelRecoilActive ? "on" : "off");
            }

            if (ImGui::CollapsingHeader("Weapon", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Equipped        %s",
                            context.hasWeapon ? context.weaponName.c_str() : "Unarmed");
                ImGui::Text("Slot / Ammo     %d  |  %d / %d", context.activeSlot + 1,
                            context.ammoInMag, context.ammoReserve);
                ImGui::Text("ADS / Reload    %s / %s", context.aiming ? "on" : "off",
                            context.reloading ? "on" : "off");
                ImGui::Text("Weapon action   %s  %.3f", debug.weaponAction.c_str(),
                            debug.weaponActionTime);
                if (debug.switchingWeapon)
                {
                    ImGui::Text("Switch target   slot %d  %s",
                                debug.switchTargetSlot + 1,
                                debug.switchCommitted ? "committed" : "holstering");
                }
                else
                {
                    ImGui::TextDisabled("Reload starts only when magazine is not full and reserve ammo exists");
                }
                ImGui::Text("Shot sequence   %llu",
                            static_cast<unsigned long long>(debug.shotSequence));
                ImGui::Text("Recent traces   %d", debug.recentWeaponTraces);
                ImGui::Text("Last trace      %s  instance #%u",
                            debug.lastTraceResult.empty() ? "-" : debug.lastTraceResult.c_str(),
                            debug.lastTraceInstanceId);
            }

            if (ImGui::CollapsingHeader("Zombie AI", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Overlay         %s", debug.zombieOverlay ? "on (F6)" : "off (F6)");
                ImGui::Text("Active/alerted  %d / %d", debug.activeZombies, debug.alertedZombies);
                ImGui::Text("Kills           %d", debug.zombieKills);
                ImGui::Text("Path segments   %d", debug.zombiePathSegments);
                ImGui::Text("Hit proxies     %d registered / %d CPU-AS eligible",
                            debug.hitProxyRegistered, debug.hitProxyCpuEligible);
                if (debug.hitProxyRegistered > debug.hitProxyCpuEligible)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.28f, 0.20f, 1.0f));
                    ImGui::TextWrapped("WARNING: proxy render state excludes it from CPU raycast AS");
                    ImGui::PopStyleColor();
                }
            }

            if (ImGui::CollapsingHeader("Loot coverage", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static constexpr std::array<const char*, 6> categoryNames = {{
                    "Food/Water", "Medical", "Ammo", "Weapon", "Clothing", "Misc"
                }};
                ImGui::Text("Overlay         %s", debug.lootOverlay ? "on (F7)" : "off (F7)");
                ImGui::Text("Available       %d  reserved %d  cooldown %d",
                            debug.lootAvailable, debug.lootReserved, debug.lootCooldown);
                for (size_t index = 0; index < categoryNames.size(); ++index)
                {
                    const bool missing = debug.lootAvailableByCategory[index] == 0;
                    if (missing) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.30f, 0.20f, 1.0f));
                    ImGui::Text("%-12s %2d / %2d%s", categoryNames[index],
                                debug.lootAvailableByCategory[index], debug.lootTotalByCategory[index],
                                missing ? "  MISSING" : "");
                    if (missing) ImGui::PopStyleColor();
                }
                ImGui::SeparatorText("Critical world coverage");
                const auto critical = [](const char* name, int count)
                {
                    if (count <= 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.30f, 0.20f, 1.0f));
                    ImGui::Text("%-12s %d%s", name, count, count <= 0 ? "  MISSING" : "");
                    if (count <= 0) ImGui::PopStyleColor();
                };
                critical("Food", debug.criticalFood);
                critical("Medical", debug.criticalMedical);
                critical("Backpack", debug.criticalBackpack);
                critical("Weapons", debug.criticalWeapons);
                critical("Ammo", debug.criticalAmmo);
                critical("Wells", debug.criticalWaterSources);
            }

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

            ImGui::Text("Storage: %d / %d", context.inventory->UsedCapacity(),
                        context.inventory->TotalCapacity());
            for (const FInventoryContainer& container : context.inventory->Containers())
            {
                if (!container.active || container.weaponOnly)
                {
                    continue;
                }
                ImGui::TextDisabled("%s  %d/%d", container.displayName.c_str(),
                                    context.inventory->ContainerUsed(container.containerId), container.capacity);
            }
            ImGui::Separator();

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Items");
            const auto& items = context.inventory->Items();
            for (size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex)
            {
                const FItemStack& stack = items[itemIndex];
                ImGui::PushID(static_cast<int>(itemIndex));
                ImGui::PushID(stack.id.c_str());
                ImGui::Text("%-16s x%-3d (%s)", stack.displayName.c_str(), stack.count, KindLabel(stack.kind));

                if (stack.kind == EItemKind::Weapon && context.equipWeapon)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Equip P##equip_primary"))
                    {
                        context.equipWeapon(stack.id, 0);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Equip S##equip_secondary"))
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
                else if (stack.kind == EItemKind::Consumable && context.useItem)
                {
                    const auto instance = std::find_if(context.inventory->Instances().begin(),
                                                       context.inventory->Instances().end(),
                        [&stack](const FItemInstance& item) { return item.defId == stack.id; });
                    if (instance != context.inventory->Instances().end())
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Use##use"))
                        {
                            context.useItem(instance->instanceId);
                        }
                    }
                }
                ImGui::PopID();
                ImGui::PopID();
            }

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Tab/I close  |  1/2 switch  |  R reload  |  V view");
            ImGui::End();
        }

        void DrawSessionOverlay(const FHudContext& context)
        {
            if (!context.paused && context.survival.lifeState != EPlayerLifeState::Dead)
            {
                return;
            }
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.94f);
            ImGui::Begin("##nd_session", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
            if (context.survival.lifeState == EPlayerLifeState::Dead)
            {
                ImGui::SetWindowFontScale(1.35f);
                ImGui::TextUnformatted("YOU DIED");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Separator();
                ImGui::Text("Cause: %s", context.survival.lastDamageSource.empty()
                    ? "unknown" : context.survival.lastDamageSource.c_str());
                ImGui::Text("Survived: %02d:%02d", static_cast<int>(context.survivalSeconds) / 60,
                            static_cast<int>(context.survivalSeconds) % 60);
                if (context.restartSession && ImGui::Button("Restart", ImVec2(-1.0f, 36.0f)))
                {
                    context.restartSession();
                }
            }
            else
            {
                ImGui::SetWindowFontScale(1.25f);
                ImGui::TextUnformatted("PAUSED");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::TextDisabled("Press Esc to resume");
                if (context.restartSession && ImGui::Button("Restart run", ImVec2(-1.0f, 34.0f)))
                {
                    context.restartSession();
                }
            }
            ImGui::End();
        }
    }

    void Draw(const FHudContext& context)
    {
        if (context.hasWeapon)
        {
            DrawCrosshair(context);
        }
        DrawClock(context);
        DrawObjective(context);
        DrawWeaponReadout(context);
        DrawSurvival(context);
        DrawInteractionPrompt(context);
        DrawDebugPanel(context);
        DrawInventoryPanel(context);
        DrawSessionOverlay(context);
    }
}
