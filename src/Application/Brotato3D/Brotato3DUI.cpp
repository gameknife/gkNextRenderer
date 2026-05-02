#include "Brotato3DUI.hpp"

#include <imgui.h>

#include "Brotato3DAudio.hpp"
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

    ImVec4 RarityColor(const std::string& rarity, float alpha)
    {
        if (rarity == "rare")
        {
            return ImVec4(0.42f, 0.18f, 0.7f, alpha);
        }
        if (rarity == "uncommon")
        {
            return ImVec4(0.12f, 0.42f, 0.26f, alpha);
        }
        return ImVec4(0.22f, 0.24f, 0.28f, alpha);
    }

    const char* ItemInitial(const std::string& itemId)
    {
        if (itemId == "vampire_fang")
        {
            return "V";
        }
        if (itemId == "regen_charm")
        {
            return "R";
        }
        if (itemId == "magnet")
        {
            return "M";
        }
        if (itemId == "fury_core")
        {
            return "F";
        }
        if (itemId == "shrapnel")
        {
            return "S";
        }
        if (itemId == "speed_boots")
        {
            return "B";
        }
        return "?";
    }

    const char* WeaponShortName(const std::string& weaponId)
    {
        if (weaponId == "smg")
        {
            return "SMG";
        }
        if (weaponId == "shotgun")
        {
            return "SHT";
        }
        if (weaponId == "sniper")
        {
            return "SNP";
        }
        if (weaponId == "flamethrower")
        {
            return "FLM";
        }
        if (weaponId == "rocket")
        {
            return "RKT";
        }
        if (weaponId == "laser")
        {
            return "LSR";
        }
        return "???";
    }

    int CountTierOneWeapons(const std::vector<Brotato3D::FWeaponRuntime>& weapons, const std::string& weaponId)
    {
        return static_cast<int>(std::count_if(weapons.begin(), weapons.end(),
                                              [&weaponId](const Brotato3D::FWeaponRuntime& weapon)
                                              {
                                                  return weapon.weaponId == weaponId && weapon.tier == 1;
                                              }));
    }

    void DrawFullscreenDim(const ImGuiViewport* viewport, float alpha)
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(viewport->Pos,
                                                       ImVec2(viewport->Pos.x + viewport->Size.x,
                                                              viewport->Pos.y + viewport->Size.y),
                                                       IM_COL32(0, 0, 0, static_cast<int>(alpha * 255.0f)));
    }

    void DrawWideTooltip(const std::string& text, float uiScale)
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360.0f * uiScale);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    void RenderSettingsPlaceholder(float uiScale)
    {
        ImGui::SetNextWindowSize(Scale(360.0f, 180.0f, uiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImGui::Text("Settings");
            ImGui::Separator();
            ImGui::TextWrapped("Settings controls will be implemented in P9.");
            ImGui::Dummy(Scale(0.0f, 28.0f, uiScale));
            if (ImGui::Button("Close", Scale(120.0f, 34.0f, uiScale)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    float GetStatValue(const Brotato3D::FPlayerRuntime& player,
                       const Brotato3D::FPlayerStats& stats,
                       const std::string& statKey)
    {
        if (statKey == "damagePct")
        {
            return stats.damagePct;
        }
        if (statKey == "damageFlat")
        {
            return stats.damageFlat;
        }
        if (statKey == "atkSpeedPct")
        {
            return stats.atkSpeedPct;
        }
        if (statKey == "rangePct")
        {
            return stats.rangePct;
        }
        if (statKey == "moveSpeedPct")
        {
            return stats.moveSpeedPct;
        }
        if (statKey == "pickupRadiusPct")
        {
            return stats.pickupRadiusPct;
        }
        if (statKey == "critChancePct")
        {
            return stats.critChancePct;
        }
        if (statKey == "critMultiplier")
        {
            return stats.critMultiplier;
        }
        if (statKey == "maxHpFlat")
        {
            return static_cast<float>(player.maxHp);
        }
        return 0.0f;
    }

    bool IsPercentStat(const std::string& statKey)
    {
        return statKey.find("Pct") != std::string::npos;
    }

    std::string FormatStatValue(const std::string& statKey, float value)
    {
        if (IsPercentStat(statKey))
        {
            return fmt::format("{:+.0f}%", value * 100.0f);
        }
        if (statKey == "critMultiplier")
        {
            return fmt::format("{:.2f}x", value);
        }
        return fmt::format("{:.0f}", value);
    }

    std::string BuildStatPreview(const Brotato3D::FPlayerRuntime& player,
                                 const Brotato3DGameInstance& gameInstance,
                                 const Brotato3D::FPlayerStats& stats,
                                 const std::string& statKey,
                                 float delta)
    {
        if (statKey == "healPct")
        {
            const int healed = std::min(player.maxHp, player.currentHp + static_cast<int>(std::round(player.maxHp * delta)));
            return fmt::format(fmt::runtime(gameInstance.Localize("tooltip.heal", "当前 HP {0} -> {1}")),
                               player.currentHp,
                               healed);
        }
        const float current = GetStatValue(player, stats, statKey);
        return fmt::format(fmt::runtime(gameInstance.Localize("tooltip.current_after", "当前 {0} -> 升级后 {1}")),
                           FormatStatValue(statKey, current),
                           FormatStatValue(statKey, current + delta));
    }

    std::string Tr(const Brotato3DGameInstance& gameInstance, const std::string& key, const std::string& fallback)
    {
        return gameInstance.Localize(key, fallback);
    }

    template <typename... Args>
    std::string TrFormat(const Brotato3DGameInstance& gameInstance,
                         const std::string& key,
                         const std::string& fallback,
                         Args&&... args)
    {
        return fmt::format(fmt::runtime(gameInstance.Localize(key, fallback)), std::forward<Args>(args)...);
    }
}

namespace Brotato3D
{
    void RenderMainMenu(Brotato3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float uiScale = GetUiScale(viewport);
        DrawFullscreenDim(viewport, 0.62f);

        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        ImGui::Begin("MainMenu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);

        const float centerX = viewport->Size.x * 0.5f;
        ImGui::SetCursorPosY(viewport->Size.y * 0.23f);
        ImGui::SetWindowFontScale(3.0f * uiScale);
        const char* title = "BROTATO 3D";
        ImGui::SetCursorPosX(centerX - ImGui::CalcTextSize(title).x * 0.5f);
        ImGui::Text("%s", title);

        ImGui::SetWindowFontScale(1.2f * uiScale);
        const std::string subtitle = Tr(gameInstance, "main.subtitle", "幸存 10 波");
        ImGui::SetCursorPosX(centerX - ImGui::CalcTextSize(subtitle.c_str()).x * 0.5f);
        ImGui::Text("%s", subtitle.c_str());

        ImGui::SetWindowFontScale(uiScale);
        ImGui::Dummy(Scale(0.0f, 36.0f, uiScale));
        const Brotato3D::FBestRecord& bestRecord = gameInstance.GetBestRecord();
        const std::string bestText = TrFormat(gameInstance,
                                              "main.best",
                                              "最佳记录：通关 {0} 次 / 击杀 {1}",
                                              bestRecord.totalWins,
                                              bestRecord.totalKills);
        ImGui::SetCursorPosX(centerX - ImGui::CalcTextSize(bestText.c_str()).x * 0.5f);
        ImGui::Text("%s", bestText.c_str());
        ImGui::Dummy(Scale(0.0f, 14.0f, uiScale));
        const ImVec2 buttonSize = Scale(220.0f, 42.0f, uiScale);
        ImGui::SetCursorPosX(centerX - buttonSize.x * 0.5f);
        if (ImGui::Button(Tr(gameInstance, "main.start", "开始游戏").c_str(), buttonSize))
        {
            gameInstance.GoToCharacterSelect();
        }
        ImGui::Dummy(Scale(0.0f, 8.0f, uiScale));
        ImGui::SetCursorPosX(centerX - buttonSize.x * 0.5f);
        ImGui::BeginDisabled();
        ImGui::Button(Tr(gameInstance, "main.continue", "继续上次").c_str(), buttonSize);
        ImGui::EndDisabled();
        ImGui::Dummy(Scale(0.0f, 8.0f, uiScale));
        ImGui::SetCursorPosX(centerX - buttonSize.x * 0.5f);
        if (ImGui::Button(Tr(gameInstance, "main.settings", "设置").c_str(), buttonSize))
        {
            Brotato3D::PlayUiClickSfx();
            ImGui::OpenPopup("Settings");
        }
        ImGui::Dummy(Scale(0.0f, 8.0f, uiScale));
        ImGui::SetCursorPosX(centerX - buttonSize.x * 0.5f);
        if (ImGui::Button(Tr(gameInstance, "main.exit", "退出").c_str(), buttonSize))
        {
            gameInstance.ExitGame();
        }
        RenderSettingsModal(gameInstance);
        ImGui::End();
    }

    void RenderCharacterSelect(Brotato3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float uiScale = GetUiScale(viewport);
        DrawFullscreenDim(viewport, 0.62f);

        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        ImGui::Begin("CharacterSelect", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);

        if (ImGui::Button(Tr(gameInstance, "character.back", "返回").c_str(), Scale(100.0f, 34.0f, uiScale)))
        {
            gameInstance.GoToMainMenu();
        }
        ImGui::SetWindowFontScale(2.0f * uiScale);
        const std::string title = Tr(gameInstance, "character.title", "选择角色");
        ImGui::SameLine();
        ImGui::SetCursorPosX((viewport->Size.x - ImGui::CalcTextSize(title.c_str()).x) * 0.5f);
        ImGui::Text("%s", title.c_str());
        ImGui::SetWindowFontScale(uiScale);

        const auto& characters = gameInstance.GetCharacterDefs();
        const float cardWidth = 250.0f * uiScale;
        const float cardHeight = 400.0f * uiScale;
        const float gap = 24.0f * uiScale;
        const float totalWidth = static_cast<float>(characters.size()) * cardWidth +
                                 std::max(0.0f, static_cast<float>(characters.size() - 1)) * gap;
        ImGui::SetCursorPosY(viewport->Size.y * 0.24f);
        ImGui::SetCursorPosX((viewport->Size.x - totalWidth) * 0.5f);
        for (size_t index = 0; index < characters.size(); ++index)
        {
            if (index > 0)
            {
                ImGui::SameLine(0.0f, gap);
            }
            const FCharacterDef& character = characters[index];
            const bool selected = gameInstance.GetSelectedCharacterId() == character.id;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(0.95f, 0.72f, 0.22f, 1.0f) :
                                                              ImVec4(0.35f, 0.38f, 0.44f, 1.0f));
            ImGui::BeginChild(fmt::format("CharacterCard{}", character.id).c_str(), ImVec2(cardWidth, cardHeight), true);
            const ImVec2 swatchMin = ImGui::GetCursorScreenPos();
            const ImVec2 swatchMax(swatchMin.x + 200.0f * uiScale, swatchMin.y + 150.0f * uiScale);
            ImGui::GetWindowDrawList()->AddRectFilled(swatchMin, swatchMax,
                                                      IM_COL32(static_cast<int>(character.color.r * 255.0f),
                                                               static_cast<int>(character.color.g * 255.0f),
                                                               static_cast<int>(character.color.b * 255.0f),
                                                               255),
                                                      4.0f * uiScale);
            ImGui::Dummy(Scale(200.0f, 160.0f, uiScale));
            ImGui::SetWindowFontScale(1.35f * uiScale);
            ImGui::Text("%s", gameInstance.Localize("character." + character.id + ".name", character.name).c_str());
            ImGui::SetWindowFontScale(uiScale);
            ImGui::TextWrapped("%s", gameInstance.Localize("character." + character.id + ".tagline", character.tagline).c_str());
            ImGui::Separator();
            ImGui::Text("%s: %s", Tr(gameInstance, "character.weapon", "武器").c_str(), character.startWeapon.c_str());
            ImGui::Text("%s: %.0f", Tr(gameInstance, "character.hp", "生命").c_str(), character.startStats.maxHpFlat);
            if (std::abs(character.startStats.damagePct) > 0.001f)
            {
                ImGui::Text("%s: %+g%%", Tr(gameInstance, "character.damage", "伤害").c_str(), character.startStats.damagePct * 100.0f);
            }
            if (std::abs(character.startStats.rangePct) > 0.001f)
            {
                ImGui::Text("%s: %+g%%", Tr(gameInstance, "character.range", "射程").c_str(), character.startStats.rangePct * 100.0f);
            }
            if (std::abs(character.startStats.moveSpeedPct) > 0.001f)
            {
                ImGui::Text("%s: %+g%%", Tr(gameInstance, "character.move", "移速").c_str(), character.startStats.moveSpeedPct * 100.0f);
            }
            if (std::abs(character.startStats.critChancePct) > 0.001f)
            {
                ImGui::Text("%s: %+g%%", Tr(gameInstance, "character.crit", "暴击").c_str(), character.startStats.critChancePct * 100.0f);
            }
            ImGui::SetCursorPosY(cardHeight - 48.0f * uiScale);
            if (ImGui::Button(Tr(gameInstance, "character.select", "选择").c_str(), Scale(210.0f, 34.0f, uiScale)))
            {
                gameInstance.SelectCharacter(character.id);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
        }

        const auto& arenas = gameInstance.GetArenaDefs();
        if (!arenas.empty())
        {
            ImGui::SetCursorPosY(viewport->Size.y - 150.0f * uiScale);
            const std::string arenaTitle = Tr(gameInstance, "arena.title", "选择场地");
            ImGui::SetCursorPosX((viewport->Size.x - ImGui::CalcTextSize(arenaTitle.c_str()).x) * 0.5f);
            ImGui::Text("%s", arenaTitle.c_str());
            const float swatchSize = 46.0f * uiScale;
            const float arenaGap = 14.0f * uiScale;
            const float arenaTotalWidth = static_cast<float>(arenas.size()) * swatchSize +
                                          std::max(0.0f, static_cast<float>(arenas.size() - 1)) * arenaGap;
            ImGui::SetCursorPosX((viewport->Size.x - arenaTotalWidth) * 0.5f);
            for (size_t arenaIndex = 0; arenaIndex < arenas.size(); ++arenaIndex)
            {
                if (arenaIndex > 0)
                {
                    ImGui::SameLine(0.0f, arenaGap);
                }
                const Brotato3D::FArenaDef& arena = arenas[arenaIndex];
                const bool selectedArena = gameInstance.GetSelectedArenaId() == arena.id;
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(arena.groundColor.r, arena.groundColor.g, arena.groundColor.b, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(arena.borderColor.r, arena.borderColor.g, arena.borderColor.b, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(arena.borderColor.r, arena.borderColor.g, arena.borderColor.b, 1.0f));
                if (ImGui::Button(fmt::format("{}##Arena{}", selectedArena ? "*" : "", arena.id).c_str(), ImVec2(swatchSize, swatchSize)))
                {
                    gameInstance.SelectArena(arena.id);
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                {
                    DrawWideTooltip(arena.name, uiScale);
                }
            }
        }
        ImGui::End();
    }

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
        ImGui::Text("%s", TrFormat(gameInstance,
                                    "hud.wave",
                                    "第 {0} / {1} 波",
                                    std::min(waveSystem.GetCurrentWaveIndex() + 1, waveSystem.GetWaveCount()),
                                    waveSystem.GetWaveCount()).c_str());
        if (waveSystem.GetState() == EWaveState::Active)
        {
            const float remaining = waveSystem.GetWaveTimeRemainingSec();
            if (remaining < 5.0f)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.22f, 0.18f, 1.0f));
            }
            ImGui::Text("%s", TrFormat(gameInstance,
                                        "hud.remaining",
                                        "剩余 {0} 秒",
                                        static_cast<int>(std::ceil(remaining))).c_str());
            if (remaining < 5.0f)
            {
                ImGui::PopStyleColor();
            }
        }
        else if (gameInstance.GetAppState() == EAppState::Shopping)
        {
            ImGui::Text("%s", Tr(gameInstance, "hud.shop_phase", "商店阶段").c_str());
        }
        else
        {
            ImGui::Text("%s", Tr(gameInstance, "hud.ready", "准备").c_str());
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(viewport->Size.x - 160.0f * uiScale, 8.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(150.0f, 50.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("ResourcePanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        ImGui::Text("%s", TrFormat(gameInstance, "hud.materials", "材料：{0}", player.materials).c_str());
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(viewport->Size.x - 246.0f * uiScale, viewport->Size.y - 58.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(236.0f, 48.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("ItemSlots", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        const auto& ownedItemIds = gameInstance.GetOwnedItemIds();
        for (size_t slotIndex = 0; slotIndex < 6; ++slotIndex)
        {
            if (slotIndex > 0)
            {
                ImGui::SameLine();
            }

            const Brotato3D::FItemDef* item = slotIndex < ownedItemIds.size() ? gameInstance.GetItemDef(ownedItemIds[slotIndex]) : nullptr;
            const ImVec4 slotColor = item ? RarityColor(item->rarity, 0.82f) : ImVec4(0.12f, 0.13f, 0.15f, 0.82f);
            ImGui::PushStyleColor(ImGuiCol_Button, slotColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, item ? RarityColor(item->rarity, 0.95f) : ImVec4(0.18f, 0.19f, 0.22f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, slotColor);
            const std::string slotLabel = fmt::format("{}##ItemSlot{}", item ? ItemInitial(item->id) : "-", slotIndex);
            ImGui::Button(slotLabel.c_str(), Scale(30.0f, 30.0f, uiScale));
            ImGui::PopStyleColor(3);
            if (item && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("%s", gameInstance.Localize("item." + item->id + ".name", item->name).c_str());
                ImGui::TextWrapped("%s", gameInstance.Localize("item." + item->id + ".desc", item->description).c_str());
                ImGui::EndTooltip();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(8.0f * uiScale, viewport->Size.y - 86.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(374.0f, 76.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("WeaponPanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        const auto& weapons = gameInstance.GetWeapons();
        const FPlayerStats effectiveStats = gameInstance.GetEffectivePlayerStats();
        ImGui::Text("%s", Tr(gameInstance, "hud.weapons", "武器").c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Tr(gameInstance, "hud.merge_hint", "3 把 T1 -> T2").c_str());
        for (size_t slotIndex = 0; slotIndex < 6; ++slotIndex)
        {
            if (slotIndex > 0)
            {
                ImGui::SameLine();
            }
            const FWeaponRuntime* weapon = slotIndex < weapons.size() ? &weapons[slotIndex] : nullptr;
            const bool hasWeapon = weapon && weapon->def;
            const ImVec4 buttonColor =
                hasWeapon ? ImVec4(weapon->def->projectileColor.r, weapon->def->projectileColor.g, weapon->def->projectileColor.b, 0.9f) :
                            ImVec4(0.12f, 0.13f, 0.15f, 0.82f);
            ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonColor);
            ImGui::Button(fmt::format("{}##WeaponSlot{}", hasWeapon ? WeaponShortName(weapon->weaponId) : "-", slotIndex).c_str(),
                          Scale(52.0f, 32.0f, uiScale));
            ImGui::PopStyleColor(3);
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(min, max, hasWeapon && weapon->tier == 2 ? IM_COL32(255, 210, 70, 255) :
                                                IM_COL32(180, 185, 195, 180), 3.0f * uiScale, 0, 2.0f * uiScale);
            if (hasWeapon && ImGui::IsItemHovered())
            {
                const int damage = static_cast<int>(std::round(weapon->def->damage * (1.0f + effectiveStats.damagePct) +
                                                               effectiveStats.damageFlat));
                const float atkSpeed = weapon->def->atkSpeedHz * (1.0f + effectiveStats.atkSpeedPct);
                const float range = weapon->def->rangeMeters * (1.0f + effectiveStats.rangePct);
                ImGui::BeginTooltip();
                ImGui::Text("%s", weapon->def->name.c_str());
                ImGui::Text("Tier: %d", weapon->tier);
                ImGui::Text("Damage: %d", damage);
                ImGui::Text("Atk Speed: %.2f/s", atkSpeed);
                ImGui::Text("Range: %.1fm", range);
                ImGui::EndTooltip();
            }
        }
        ImGui::End();

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        for (const FEnemyRuntime& enemy : gameInstance.GetEnemies())
        {
            if (enemy.alive && enemy.def && enemy.def->heal.enabled)
            {
                ImVec2 center{};
                ImVec2 edge{};
                if (gameInstance.WorldToScreen(enemy.worldPos, center) &&
                    gameInstance.WorldToScreen(enemy.worldPos + glm::vec3(enemy.def->heal.radiusMeters, 0.0f, 0.0f), edge))
                {
                    const float radiusPx = std::abs(edge.x - center.x);
                    drawList->AddCircle(center, radiusPx, IM_COL32(160, 70, 230, 130), 48, std::max(1.5f, 2.0f * uiScale));
                }
            }
            if (!enemy.alive || !enemy.def || enemy.maxHp <= 0 || enemy.currentHp >= enemy.maxHp * 0.7f ||
                !Brotato3D::ShowEnemyHpBars)
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

        for (const FProjectileRuntime& projectile : gameInstance.GetProjectiles())
        {
            if (!projectile.active)
            {
                continue;
            }

            ImVec2 from{};
            ImVec2 to{};
            if (!gameInstance.WorldToScreen(projectile.lastWorldPos, from) || !gameInstance.WorldToScreen(projectile.worldPos, to))
            {
                continue;
            }

            drawList->AddLine(from, to, Color(glm::vec4(projectile.color, 0.6f)), std::max(1.5f, 2.0f * uiScale));
        }

        for (const FMuzzleFlash& flash : gameInstance.GetMuzzleFlashes())
        {
            ImVec2 center{};
            if (!gameInstance.WorldToScreen(flash.worldPos, center))
            {
                continue;
            }

            const float ratio = flash.lifeMs > 0.0f ? std::clamp(flash.remainingMs / flash.lifeMs, 0.0f, 1.0f) : 0.0f;
            const float radius = 18.0f * uiScale * ratio;
            const ImU32 flashColor = Color(glm::vec4(flash.color, ratio));
            drawList->AddCircleFilled(center, radius * 0.5f, flashColor, 16);
            drawList->AddLine(center, ImVec2(center.x + radius, center.y), flashColor, 3.0f * uiScale);
            drawList->AddLine(center, ImVec2(center.x - radius, center.y), flashColor, 3.0f * uiScale);
            drawList->AddLine(center, ImVec2(center.x, center.y + radius), flashColor, 3.0f * uiScale);
            drawList->AddLine(center, ImVec2(center.x, center.y - radius), flashColor, 3.0f * uiScale);
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
            drawList->AddText(nullptr, ImGui::GetFontSize() * text.fontScale,
                              ImVec2(screen.x, screen.y - rise), Color(color), text.text.c_str());
        }

        for (const FExpandingRing& ring : gameInstance.GetExplosionRings())
        {
            ImVec2 center{};
            ImVec2 edge{};
            if (!gameInstance.WorldToScreen(ring.worldPos, center) ||
                !gameInstance.WorldToScreen(ring.worldPos + glm::vec3(ring.maxRadius, 0.0f, 0.0f), edge))
            {
                continue;
            }
            const float progress = 1.0f - std::clamp(ring.remainingMs / ring.durationMs, 0.0f, 1.0f);
            const float radiusPx = std::abs(edge.x - center.x) * progress;
            glm::vec4 color = ring.color;
            color.a *= 1.0f - progress;
            drawList->AddCircle(center, radiusPx, Color(color), 48, std::max(2.0f, 3.0f * uiScale));
        }

        for (const FLaserBeam& beam : gameInstance.GetLaserBeams())
        {
            ImVec2 from{};
            ImVec2 to{};
            if (!gameInstance.WorldToScreen(beam.from, from) || !gameInstance.WorldToScreen(beam.to, to))
            {
                continue;
            }
            const float alpha = std::clamp(beam.remainingMs / beam.durationMs, 0.0f, 1.0f);
            glm::vec4 color = beam.color;
            color.a *= alpha;
            drawList->AddLine(from, to, Color(color), std::max(2.0f, beam.width * 24.0f * uiScale));
        }

        if (gameInstance.GetDamageFlashMs() > 0.0f)
        {
            const float alpha = std::clamp(gameInstance.GetDamageFlashMs() / 180.0f, 0.0f, 1.0f) * 0.16f;
            drawList->AddRectFilled(viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.0f, 0.0f, alpha)));
        }

        if (gameInstance.GetWaveBannerMs() > 0.0f)
        {
            const float progress = 1.0f - std::clamp(gameInstance.GetWaveBannerMs() / 1000.0f, 0.0f, 1.0f);
            const float alpha = std::sin(progress * glm::pi<float>());
            const std::string& bannerText = gameInstance.GetWaveBannerText();
            const ImVec2 textSize = ImGui::CalcTextSize(bannerText.c_str());
            const float fontScale = 2.6f * uiScale;
            const ImVec2 pos(viewport->Pos.x + (viewport->Size.x - textSize.x * fontScale) * 0.5f,
                             viewport->Pos.y + viewport->Size.y * 0.36f);
            const bool bossBanner = bannerText.find("BOSS") != std::string::npos;
            drawList->AddText(gameInstance.GetBigFont(), ImGui::GetFontSize() * fontScale, pos,
                              bossBanner ? IM_COL32(255, 62, 42, static_cast<int>(alpha * 255.0f)) :
                                           IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)),
                              bannerText.c_str());
        }

        if (gameInstance.GetWeaponMergeBannerMs() > 0.0f)
        {
            const float alpha = std::clamp(gameInstance.GetWeaponMergeBannerMs() / 1500.0f, 0.0f, 1.0f);
            const std::string& bannerText = gameInstance.GetWeaponMergeBannerText();
            const float fontScale = 2.2f * uiScale;
            const ImVec2 textSize = ImGui::CalcTextSize(bannerText.c_str());
            const ImVec2 pos(viewport->Pos.x + (viewport->Size.x - textSize.x * fontScale) * 0.5f,
                             viewport->Pos.y + viewport->Size.y * 0.44f);
            drawList->AddText(nullptr,
                              ImGui::GetFontSize() * fontScale,
                              pos,
                              IM_COL32(255, 214, 70, static_cast<int>(alpha * 255.0f)),
                              bannerText.c_str());
        }
    }

    void RenderUpgradeModal(Brotato3DGameInstance& gameInstance)
    {
        const float uiScale = GetUiScale(ImGui::GetMainViewport());
        const FPlayerRuntime& player = gameInstance.GetPlayer();
        const FPlayerStats effectiveStats = gameInstance.GetEffectivePlayerStats();
        ImGui::OpenPopup("Upgrade");
        ImGui::SetNextWindowSize(Scale(700.0f, 330.0f, uiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Upgrade", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImGui::Text("%s", Tr(gameInstance, "upgrade.title", "选择 1 个升级").c_str());
            const auto& choices = gameInstance.GetCurrentUpgradeChoices();
            for (size_t index = 0; index < choices.size(); ++index)
            {
                if (index > 0)
                {
                    ImGui::SameLine();
                }
                ImGui::BeginChild(fmt::format("UpgradeCard{}", index).c_str(), Scale(210.0f, 250.0f, uiScale), true);
                ImGui::TextWrapped("%s", gameInstance.Localize("upgrade." + choices[index].id + ".name", choices[index].name).c_str());
                ImGui::Separator();
                ImGui::TextWrapped("%s %+g", choices[index].stat.c_str(), choices[index].delta);
                ImGui::SetCursorPosY(205.0f * uiScale);
                if (ImGui::Button(fmt::format("{}##{}", Tr(gameInstance, "upgrade.select", "选择"), index).c_str(),
                                  Scale(180.0f, 32.0f, uiScale)))
                {
                    gameInstance.SelectUpgrade(index);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsWindowHovered())
                {
                    DrawWideTooltip(BuildStatPreview(player, gameInstance, effectiveStats, choices[index].stat, choices[index].delta), uiScale);
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
            const FPlayerStats effectiveStats = gameInstance.GetEffectivePlayerStats();
            const FWaveSystem& waveSystem = gameInstance.GetWaveSystem();
            ImGui::Text("%s", TrFormat(gameInstance,
                                        "shop.title",
                                        "商店阶段 - 第 {0} / {1} 波结束",
                                        waveSystem.GetCurrentWaveIndex() + 1,
                                        waveSystem.GetWaveCount()).c_str());
            ImGui::SameLine();
            ImGui::Text("%s", TrFormat(gameInstance, "hud.materials", "材料：{0}", player.materials).c_str());
            ImGui::Separator();

            const auto& offers = gameInstance.GetShopOffers();
            for (size_t index = 0; index < offers.size(); ++index)
            {
                if (index > 0)
                {
                    ImGui::SameLine();
                }
                const bool passiveItem = offers[index].isPassiveItem;
                const bool weaponCard = offers[index].isWeaponCard;
                if (passiveItem)
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, RarityColor(offers[index].rarity, 0.55f));
                    ImGui::PushStyleColor(ImGuiCol_Border, RarityColor(offers[index].rarity, 0.95f));
                }
                else if (weaponCard)
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.17f, 0.10f, 0.72f));
                    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.72f, 0.22f, 1.0f));
                }
                ImGui::BeginChild(fmt::format("ShopCard{}", index).c_str(), Scale(175.0f, 260.0f, uiScale), true);
                const std::string offerName =
                    gameInstance.Localize((passiveItem ? "item." : "shop.") + offers[index].id + ".name", offers[index].name);
                const std::string offerDesc =
                    gameInstance.Localize((passiveItem ? "item." : "shop.") + offers[index].id + ".desc", offers[index].description);
                ImGui::TextWrapped("%s", offerName.c_str());
                ImGui::Separator();
                if (weaponCard)
                {
                    const int ownedTierOne = CountTierOneWeapons(gameInstance.GetWeapons(), offers[index].weaponId);
                    ImGui::Text("%s", Tr(gameInstance, "shop.weapon", "武器").c_str());
                    ImGui::Text("%s", TrFormat(gameInstance, "shop.merge", "合成：{0} / 3", std::min(ownedTierOne, 3)).c_str());
                    ImGui::TextWrapped("%s",
                                       ownedTierOne >= 2 ? Tr(gameInstance, "shop.merge_ready", "再买一把会合成 T2").c_str() :
                                                           Tr(gameInstance, "shop.merge_need", "需要 3 把相同 T1").c_str());
                }
                else if (passiveItem)
                {
                    ImGui::Text("%s", offers[index].rarity.c_str());
                    ImGui::TextWrapped("%s", offerDesc.c_str());
                }
                else
                {
                    ImGui::TextWrapped("%s %+g", offers[index].stat.c_str(), offers[index].delta);
                }
                ImGui::Text("%s", TrFormat(gameInstance, "shop.cost", "价格：{0}", offers[index].cost).c_str());
                ImGui::SetCursorPosY(210.0f * uiScale);
                const bool canBuy = gameInstance.CanBuyShopOffer(index);
                if (!canBuy)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button(fmt::format("{}##{}", Tr(gameInstance, "shop.buy", "购买"), index).c_str(),
                                  Scale(140.0f, 32.0f, uiScale)))
                {
                    gameInstance.BuyShopItem(index);
                }
                if (!canBuy)
                {
                    ImGui::EndDisabled();
                }
                if (!canBuy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    const std::string reason = gameInstance.GetShopOfferUnavailableReason(index);
                    if (!reason.empty())
                    {
                        DrawWideTooltip(reason, uiScale);
                    }
                }
                else if (ImGui::IsWindowHovered())
                {
                    if (passiveItem)
                    {
                        DrawWideTooltip(fmt::format("{}\n\n{}", offerDesc, Tr(gameInstance, "tooltip.item_active", "拥有后激活效果")),
                                        uiScale);
                    }
                    else if (weaponCard)
                    {
                        DrawWideTooltip(fmt::format("{}: {}\n{}",
                                                    Tr(gameInstance, "shop.weapon", "武器"),
                                                    offerName,
                                                    Tr(gameInstance, "hud.merge_hint", "3 把 T1 -> T2")),
                                        uiScale);
                    }
                    else
                    {
                        DrawWideTooltip(BuildStatPreview(player, gameInstance, effectiveStats, offers[index].stat, offers[index].delta),
                                        uiScale);
                    }
                }
                ImGui::EndChild();
                if (passiveItem || weaponCard)
                {
                    ImGui::PopStyleColor(2);
                }
            }

            ImGui::Separator();
            if (player.materials < gameInstance.GetRerollCost())
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button(TrFormat(gameInstance, "shop.reroll", "刷新（{0}）", gameInstance.GetRerollCost()).c_str(),
                              Scale(150.0f, 34.0f, uiScale)))
            {
                gameInstance.RerollShop();
            }
            if (player.materials < gameInstance.GetRerollCost())
            {
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr(gameInstance, "shop.next_wave", "下一波").c_str(), Scale(150.0f, 34.0f, uiScale)))
            {
                gameInstance.ContinueFromShop();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void RenderSettingsModal(Brotato3DGameInstance& gameInstance)
    {
        const float uiScale = 1.0f;
        ImGui::SetNextWindowSize(Scale(420.0f, 330.0f, uiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Settings",
                                   nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImGui::Text("%s", Tr(gameInstance, "settings.title", "设置").c_str());
            ImGui::Separator();
            if (ImGui::SliderFloat("SfxVolume", &Brotato3D::SfxVolume, 0.0f, 1.0f, "%.2f"))
            {
                Brotato3D::SfxVolume = std::clamp(Brotato3D::SfxVolume, 0.0f, 1.0f);
            }
            if (ImGui::SliderFloat("MusicVolume", &Brotato3D::MusicVolume, 0.0f, 1.0f, "%.2f"))
            {
                Brotato3D::MusicVolume = std::clamp(Brotato3D::MusicVolume, 0.0f, 1.0f);
                Brotato3D::RefreshBgmVolume();
            }
            ImGui::Checkbox(Tr(gameInstance, "settings.shake", "启用屏幕震动").c_str(), &Brotato3D::ScreenShakeEnabled);
            ImGui::Checkbox(Tr(gameInstance, "settings.hp_bars", "显示敌人 HP 条").c_str(), &Brotato3D::ShowEnemyHpBars);
            ImGui::SliderFloat("MasterDifficulty", &Brotato3D::MasterDifficulty, 0.5f, 1.5f, "%.2f");
            Brotato3D::MasterDifficulty = std::clamp(Brotato3D::MasterDifficulty, 0.5f, 1.5f);
            ImGui::Dummy(Scale(0.0f, 20.0f, uiScale));
            if (ImGui::Button(Tr(gameInstance, "settings.apply", "应用并关闭").c_str(), Scale(150.0f, 34.0f, uiScale)))
            {
                Brotato3D::PlayUiClickSfx();
                Brotato3D::RefreshBgmVolume();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr(gameInstance, "settings.defaults", "恢复默认").c_str(), Scale(130.0f, 34.0f, uiScale)))
            {
                Brotato3D::PlayUiClickSfx();
                Brotato3D::SfxVolume = 0.7f;
                Brotato3D::MusicVolume = 0.5f;
                Brotato3D::ScreenShakeEnabled = true;
                Brotato3D::ShowEnemyHpBars = true;
                Brotato3D::MasterDifficulty = 1.0f;
                Brotato3D::RefreshBgmVolume();
            }
            ImGui::EndPopup();
        }
    }

    void RenderPauseModal(Brotato3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float uiScale = GetUiScale(viewport);
        DrawFullscreenDim(viewport, 0.55f);
        ImGui::OpenPopup("Pause");
        ImGui::SetNextWindowSize(Scale(300.0f, 360.0f, uiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Pause", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImGui::Text("%s", Tr(gameInstance, "pause.title", "游戏暂停").c_str());
            ImGui::Separator();
            const ImVec2 buttonSize = Scale(220.0f, 38.0f, uiScale);
            if (ImGui::Button(Tr(gameInstance, "pause.resume", "继续").c_str(), buttonSize))
            {
                gameInstance.ResumeGame();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button(Tr(gameInstance, "main.settings", "设置").c_str(), buttonSize))
            {
                Brotato3D::PlayUiClickSfx();
                ImGui::OpenPopup("Settings");
            }
            if (ImGui::Button(Tr(gameInstance, "pause.restart", "重新开始（同角色）").c_str(), buttonSize))
            {
                gameInstance.StartNewRun();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button(Tr(gameInstance, "pause.menu", "退出到主菜单").c_str(), buttonSize))
            {
                gameInstance.GoToMainMenu();
                ImGui::CloseCurrentPopup();
            }
            RenderSettingsModal(gameInstance);
            ImGui::EndPopup();
        }
    }

    void RenderResultModal(Brotato3DGameInstance& gameInstance)
    {
        const float uiScale = GetUiScale(ImGui::GetMainViewport());
        ImGui::OpenPopup("Result");
        ImGui::SetNextWindowSize(Scale(560.0f, 440.0f, uiScale), ImGuiCond_Appearing);
        const bool defeated = gameInstance.IsPlayerDead();
        ImGui::PushStyleColor(ImGuiCol_PopupBg, defeated ? ImVec4(0.18f, 0.04f, 0.04f, 0.96f) :
                                                           ImVec4(0.03f, 0.08f, 0.18f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Border, defeated ? ImVec4(0.9f, 0.15f, 0.12f, 1.0f) :
                                                          ImVec4(0.95f, 0.72f, 0.22f, 1.0f));
        if (ImGui::BeginPopupModal("Result", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImGui::Text("%s", defeated ? Tr(gameInstance, "result.defeat", "失败").c_str() :
                                          Tr(gameInstance, "result.victory", "胜利").c_str());
            if (defeated)
            {
                ImGui::Text("%s", TrFormat(gameInstance,
                                            "result.died_wave",
                                            "倒在第 {0} 波",
                                            gameInstance.GetWaveSystem().GetCurrentWaveIndex() + 1).c_str());
            }
            else
            {
                ImGui::Text("%s", TrFormat(gameInstance,
                                            "result.survived",
                                            "完成 {0} 波",
                                            gameInstance.GetWaveSystem().GetWaveCount()).c_str());
            }
            ImGui::Separator();
            ImGui::Text("%s", TrFormat(gameInstance, "result.time", "用时：{0}", FormatTime(gameInstance.GetRunElapsedSec())).c_str());
            ImGui::Text("%s", TrFormat(gameInstance, "result.level", "最高等级：{0}", gameInstance.GetPlayer().level).c_str());
            ImGui::Text("%s", TrFormat(gameInstance, "result.kills", "击杀：{0}", gameInstance.GetKillCount()).c_str());
            ImGui::Text("%s", TrFormat(gameInstance,
                                        "result.materials",
                                        "获得材料：{0}",
                                        gameInstance.GetTotalMaterialsGained()).c_str());
            if (const FCharacterDef* character = gameInstance.GetSelectedCharacterDef())
            {
                ImGui::Text("%s", TrFormat(gameInstance,
                                            "result.character",
                                            "角色：{0}",
                                            gameInstance.Localize("character." + character->id + ".name", character->name)).c_str());
            }
            std::string itemList = Tr(gameInstance, "result.none", "无");
            const auto& ownedItemIds = gameInstance.GetOwnedItemIds();
            if (!ownedItemIds.empty())
            {
                itemList.clear();
                for (const std::string& itemId : ownedItemIds)
                {
                    const FItemDef* item = gameInstance.GetItemDef(itemId);
                    if (!item)
                    {
                        continue;
                    }
                    if (!itemList.empty())
                    {
                        itemList += " / ";
                    }
                    itemList += ItemInitial(item->id);
                }
            }
            ImGui::TextWrapped("%s", TrFormat(gameInstance, "result.items", "道具：{0}", itemList).c_str());
            const Brotato3D::FBestRecord& bestRecord = gameInstance.GetBestRecord();
            ImGui::Text("%s", Tr(gameInstance, "best.title", "最佳记录").c_str());
            ImGui::Text("%s", TrFormat(gameInstance, "best.wins", "通关次数：{0}", bestRecord.totalWins).c_str());
            ImGui::Text("%s", TrFormat(gameInstance, "best.kills", "累计击杀：{0}", bestRecord.totalKills).c_str());
            ImGui::Text("%s", TrFormat(gameInstance,
                                        "best.fastest",
                                        "最快通关：{0}",
                                        bestRecord.fastestCompletionSec > 0.0f ? FormatTime(bestRecord.fastestCompletionSec) :
                                                                                 Tr(gameInstance, "result.none", "无")).c_str());
            ImGui::Dummy(Scale(0.0f, 24.0f, uiScale));
            if (ImGui::Button(Tr(gameInstance, "result.restart", "再来一局（同角色）").c_str(), Scale(190.0f, 36.0f, uiScale)))
            {
                gameInstance.RestartGame();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Tr(gameInstance, "result.menu", "回主菜单").c_str(), Scale(150.0f, 36.0f, uiScale)))
            {
                gameInstance.GoToMainMenu();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
    }
}
