#include "Brotato3DUI.hpp"

#include <imgui.h>

#include "Brotato3DGameInstance.hpp"

namespace
{
    float GetUiScale(const ImGuiViewport* viewport)
    {
        return std::max(0.75f, std::min(viewport->Size.x / 1280.0f, viewport->Size.y / 720.0f));
    }

    ImVec2 Scale(float x, float y, float scale)
    {
        return ImVec2(x * scale, y * scale);
    }

    ImU32 Color(const glm::vec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    }

    void DrawBar(const ImVec2& pos, const ImVec2& size, float ratio, ImU32 fillColor, const char* text, float scale)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(28, 30, 34, 230), 3.0f * scale);
        drawList->AddRectFilled(pos, ImVec2(pos.x + size.x * std::clamp(ratio, 0.0f, 1.0f), pos.y + size.y), fillColor,
                                3.0f * scale);
        drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 90), 3.0f * scale);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        drawList->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f), IM_COL32_WHITE,
                          text);
    }

    ImU32 HpColor(float ratio)
    {
        if (ratio > 0.6f)
        {
            return IM_COL32(86, 210, 92, 255);
        }
        if (ratio > 0.3f)
        {
            return IM_COL32(230, 196, 64, 255);
        }
        return IM_COL32(220, 68, 58, 255);
    }

    std::string FormatTime(float seconds)
    {
        const int total = std::max(0, static_cast<int>(std::ceil(seconds)));
        return fmt::format("{:02}:{:02}", total / 60, total % 60);
    }
}

namespace Brotato3D
{
    void RenderHUD(Brotato3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float uiScale = GetUiScale(viewport);
        const FPlayerRuntime& player = gameInstance.GetPlayer();
        const float hpRatio = player.maxHp > 0 ? static_cast<float>(player.currentHp) / static_cast<float>(player.maxHp) : 0.0f;
        const int xpToNext = gameInstance.GetXpToNextLevel();

        ImGui::SetNextWindowPos(Scale(8.0f, 8.0f, uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(280.0f, 90.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("PlayerPanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        DrawBar(ImGui::GetCursorScreenPos(), Scale(260.0f, 24.0f, uiScale), hpRatio, HpColor(hpRatio),
                fmt::format("HP {} / {}", player.currentHp, player.maxHp).c_str(), uiScale);
        ImGui::Dummy(Scale(0.0f, 30.0f, uiScale));
        DrawBar(ImGui::GetCursorScreenPos(), Scale(260.0f, 20.0f, uiScale),
                xpToNext > 0 ? static_cast<float>(player.currentXp) / static_cast<float>(xpToNext) : 0.0f,
                IM_COL32(72, 135, 245, 255), fmt::format("Lv {}  XP {}/{}", player.level, player.currentXp, xpToNext).c_str(),
                uiScale);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2((viewport->Size.x - 260.0f * uiScale) * 0.5f, 8.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(260.0f, 56.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("WavePanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        const FWaveSystem& waveSystem = gameInstance.GetWaveSystem();
        ImGui::Text("Wave %d / %d", std::min(waveSystem.GetCurrentWaveIndex() + 1, waveSystem.GetWaveCount()),
                    waveSystem.GetWaveCount());
        if (waveSystem.GetState() == EWaveState::Active)
        {
            const float remaining = waveSystem.GetWaveTimeRemainingSec();
            if (remaining < 5.0f)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.22f, 0.18f, 1.0f));
            }
            ImGui::Text("Remaining %.0fs", std::ceil(remaining));
            if (remaining < 5.0f)
            {
                ImGui::PopStyleColor();
            }
        }
        else if (gameInstance.GetAppState() == EAppState::Shopping)
        {
            ImGui::Text("Shop phase");
        }
        else
        {
            ImGui::Text("Ready");
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(viewport->Size.x - 160.0f * uiScale, 8.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(150.0f, 50.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("ResourcePanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        ImGui::Text("Materials: %d", player.materials);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(8.0f * uiScale, viewport->Size.y - 90.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(160.0f, 80.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("WeaponPanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        for (const FWeaponRuntime& weapon : gameInstance.GetWeapons())
        {
            if (!weapon.def)
            {
                continue;
            }
            ImGui::ColorButton(weapon.def->name.c_str(),
                               ImVec4(weapon.def->projectileColor.r, weapon.def->projectileColor.g,
                                      weapon.def->projectileColor.b, 1.0f),
                               ImGuiColorEditFlags_NoTooltip, Scale(20.0f, 20.0f, uiScale));
            ImGui::SameLine();
            ImGui::Text("%s", weapon.def->name.c_str());
            const float maxCooldown = 1000.0f / std::max(0.01f, weapon.def->atkSpeedHz * (1.0f + player.stats.atkSpeedPct));
            ImGui::ProgressBar(1.0f - std::clamp(weapon.cooldownMs / maxCooldown, 0.0f, 1.0f), Scale(128.0f, 8.0f, uiScale),
                               "");
        }
        ImGui::End();

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        for (const FEnemyRuntime& enemy : gameInstance.GetEnemies())
        {
            if (!enemy.alive || !enemy.def || enemy.maxHp <= 0 || enemy.currentHp >= enemy.maxHp * 0.7f)
            {
                continue;
            }
            ImVec2 screen{};
            if (!gameInstance.WorldToScreen(enemy.worldPos + glm::vec3(0.0f, enemy.def->size.y + 0.2f, 0.0f), screen))
            {
                continue;
            }
            const float ratio = static_cast<float>(enemy.currentHp) / static_cast<float>(enemy.maxHp);
            drawList->AddRectFilled(ImVec2(screen.x - 15.0f * uiScale, screen.y),
                                    ImVec2(screen.x + 15.0f * uiScale, screen.y + 3.0f * uiScale),
                                    IM_COL32(80, 18, 18, 220));
            drawList->AddRectFilled(ImVec2(screen.x - 15.0f * uiScale, screen.y),
                                    ImVec2(screen.x - 15.0f * uiScale + 30.0f * uiScale * ratio,
                                           screen.y + 3.0f * uiScale),
                                    IM_COL32(230, 48, 42, 255));
        }

        for (const FFloatingText& text : gameInstance.GetFloatingTexts())
        {
            ImVec2 screen{};
            if (!gameInstance.WorldToScreen(text.worldPos, screen))
            {
                continue;
            }
            const float alpha = text.lifeMs > 0.0f ? std::clamp(text.remainingMs / text.lifeMs, 0.0f, 1.0f) : 0.0f;
            const float rise = (1.0f - alpha) * 30.0f * uiScale;
            glm::vec4 color = text.color;
            color.a *= alpha;
            drawList->AddText(ImVec2(screen.x, screen.y - rise), Color(color), text.text.c_str());
        }

        if (gameInstance.GetDamageFlashMs() > 0.0f)
        {
            const float alpha = std::clamp(gameInstance.GetDamageFlashMs() / 180.0f, 0.0f, 1.0f) * 0.16f;
            drawList->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.0f, 0.0f, alpha)));
        }
    }

    void RenderUpgradeModal(Brotato3DGameInstance& gameInstance)
    {
        const float uiScale = GetUiScale(ImGui::GetMainViewport());
        ImGui::OpenPopup("Upgrade");
        ImGui::SetNextWindowSize(Scale(700.0f, 330.0f, uiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Upgrade", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImGui::Text("Choose one upgrade");
            const auto& choices = gameInstance.GetCurrentUpgradeChoices();
            for (size_t index = 0; index < choices.size(); ++index)
            {
                if (index > 0)
                {
                    ImGui::SameLine();
                }
                ImGui::BeginChild(fmt::format("UpgradeCard{}", index).c_str(), Scale(210.0f, 250.0f, uiScale), true);
                ImGui::TextWrapped("%s", choices[index].name.c_str());
                ImGui::Separator();
                ImGui::TextWrapped("%s %+g", choices[index].stat.c_str(), choices[index].delta);
                ImGui::SetCursorPosY(205.0f * uiScale);
                if (ImGui::Button(fmt::format("Select##{}", index).c_str(), Scale(180.0f, 32.0f, uiScale)))
                {
                    gameInstance.SelectUpgrade(index);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndChild();
            }
            ImGui::EndPopup();
        }
    }

    void RenderShopModal(Brotato3DGameInstance& gameInstance)
    {
        const float uiScale = GetUiScale(ImGui::GetMainViewport());
        ImGui::OpenPopup("Shop");
        ImGui::SetNextWindowSize(Scale(760.0f, 420.0f, uiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Shop", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(uiScale);
            const FPlayerRuntime& player = gameInstance.GetPlayer();
            const FWaveSystem& waveSystem = gameInstance.GetWaveSystem();
            ImGui::Text("Shop phase - Wave %d / %d ended", waveSystem.GetCurrentWaveIndex() + 1, waveSystem.GetWaveCount());
            ImGui::SameLine();
            ImGui::Text("Materials: %d", player.materials);
            ImGui::Separator();

            const auto& offers = gameInstance.GetShopOffers();
            for (size_t index = 0; index < offers.size(); ++index)
            {
                if (index > 0)
                {
                    ImGui::SameLine();
                }
                ImGui::BeginChild(fmt::format("ShopCard{}", index).c_str(), Scale(175.0f, 260.0f, uiScale), true);
                ImGui::TextWrapped("%s", offers[index].name.c_str());
                ImGui::Separator();
                ImGui::TextWrapped("%s %+g", offers[index].stat.c_str(), offers[index].delta);
                ImGui::Text("Cost: %d", offers[index].cost);
                ImGui::SetCursorPosY(210.0f * uiScale);
                const bool canBuy = player.materials >= offers[index].cost;
                if (!canBuy)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button(fmt::format("Buy##{}", index).c_str(), Scale(140.0f, 32.0f, uiScale)))
                {
                    gameInstance.BuyShopItem(index);
                }
                if (!canBuy)
                {
                    ImGui::EndDisabled();
                }
                ImGui::EndChild();
            }

            ImGui::Separator();
            if (player.materials < gameInstance.GetRerollCost())
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(fmt::format("Reroll ({})", gameInstance.GetRerollCost()).c_str(), Scale(150.0f, 34.0f, uiScale)))
            {
                gameInstance.RerollShop();
            }
            if (player.materials < gameInstance.GetRerollCost())
            {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button("Next wave", Scale(150.0f, 34.0f, uiScale)))
            {
                gameInstance.ContinueFromShop();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void RenderResultModal(Brotato3DGameInstance& gameInstance)
    {
        const float uiScale = GetUiScale(ImGui::GetMainViewport());
        ImGui::OpenPopup("Result");
        ImGui::SetNextWindowSize(Scale(500.0f, 300.0f, uiScale), ImGuiCond_Appearing);
        const bool defeated = gameInstance.IsPlayerDead();
        ImGui::PushStyleColor(ImGuiCol_PopupBg, defeated ? ImVec4(0.18f, 0.04f, 0.04f, 0.96f) :
                                                           ImVec4(0.03f, 0.08f, 0.18f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Border, defeated ? ImVec4(0.9f, 0.15f, 0.12f, 1.0f) :
                                                          ImVec4(0.95f, 0.72f, 0.22f, 1.0f));
        if (ImGui::BeginPopupModal("Result", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImGui::Text("%s", defeated ? "DEFEAT" : "VICTORY");
            if (defeated)
            {
                ImGui::Text("Died on Wave %d", gameInstance.GetWaveSystem().GetCurrentWaveIndex() + 1);
            }
            else
            {
                ImGui::Text("Survived 5 waves");
            }
            ImGui::Separator();
            ImGui::Text("Run Time: %s", FormatTime(gameInstance.GetRunElapsedSec()).c_str());
            ImGui::Text("Highest Level: %d", gameInstance.GetPlayer().level);
            ImGui::Text("Kills: %d", gameInstance.GetKillCount());
            ImGui::Text("Materials Gained: %d", gameInstance.GetTotalMaterialsGained());
            ImGui::Dummy(Scale(0.0f, 24.0f, uiScale));
            if (ImGui::Button("Restart", Scale(150.0f, 36.0f, uiScale)))
            {
                gameInstance.RestartGame();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Exit", Scale(150.0f, 36.0f, uiScale)))
            {
                gameInstance.ExitGame();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
    }
}
