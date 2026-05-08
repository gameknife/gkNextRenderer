#include "Brotato3DUI.hpp"

#include <imgui.h>

#include "Brotato3DAudio.hpp"
#include "Brotato3DAssetPaths.hpp"
#include "Brotato3DGameInstance.hpp"
#include "Runtime/Editor/ImGuiPainter.h"
#include "Runtime/Editor/ImGuiScaling.h"
#include "Runtime/Editor/UserInterface.hpp"
#include "Runtime/Subsystems/NextLocalization.h"
#include "Runtime/Utilities/NextEngineHelper.h"

namespace
{
    float GetUiScale(const ImGuiViewport* viewport)
    {
        return NextUI::Scaling::GetViewportUiScale(viewport);
    }

    ImVec2 Scale(float x, float y, float scale)
    {
        return ImVec2(x * scale, y * scale);
    }

    ImU32 Color(const glm::vec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    }

    ImTextureID EmptyTexture()
    {
        return static_cast<ImTextureID>(0);
    }

    std::string Localize(const Brotato3DGameInstance& gameInstance, const std::string& key, const std::string& fallback = "")
    {
        if (const NextLocalization* localization = gameInstance.GetEngine().GetLocalization())
        {
            return localization->Get(key, fallback);
        }
        return fallback.empty() ? key : fallback;
    }

    ImTextureID LoadUiTexture(Brotato3DGameInstance& gameInstance, const std::string& path, bool srgb = true)
    {
        UserInterface* ui = gameInstance.GetEngine().GetUserInterface();
        if (!ui)
        {
            return EmptyTexture();
        }

        const UserInterface::FUiTextureHandle handle = ui->RequestUiTexture(path, srgb);
        return handle.valid ? handle.textureId : EmptyTexture();
    }

    ImTextureID LoadHudTexture(Brotato3DGameInstance& gameInstance, const std::string& relPath)
    {
        return LoadUiTexture(gameInstance, Brotato3D::PlaceholderAssets::Hud(relPath));
    }

    ImTextureID LoadMenuTexture(Brotato3DGameInstance& gameInstance, const std::string& relPath)
    {
        return LoadUiTexture(gameInstance, Brotato3D::PlaceholderAssets::Menu(relPath));
    }

    ImTextureID LoadIconTexture(Brotato3DGameInstance& gameInstance, const std::string& category, const std::string& id)
    {
        return LoadUiTexture(gameInstance, Brotato3D::PlaceholderAssets::Icon(category, id));
    }

    ImVec2 GetTexturePixelSize(Brotato3DGameInstance& gameInstance, const std::string& path)
    {
        UserInterface* ui = gameInstance.GetEngine().GetUserInterface();
        return ui ? ui->RequestUiTexture(path).pixelSize : ImVec2(0.0f, 0.0f);
    }

    ImVec2 CalcFontTextSize(ImFont* font, float fontSize, const std::string& text)
    {
        if (!font)
        {
            return ImGui::CalcTextSize(text.c_str());
        }
        return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    }

    using NextUI::Painter::DrawBar;
    using NextUI::Painter::DrawFullscreenDim;
    using NextUI::Painter::DrawImageContain;
    using NextUI::Painter::DrawImageCover;
    using NextUI::Painter::DrawPanel;
    using NextUI::Painter::DrawTexturedBar;

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

    const char* CharacterIconId(const std::string& characterId)
    {
        if (characterId == "soldier")
        {
            return "soldier";
        }
        if (characterId == "brawler")
        {
            return "brawler";
        }
        if (characterId == "marksman")
        {
            return "marksman";
        }
        return nullptr;
    }

    const char* EnemyIconId(const std::string& enemyId)
    {
        if (enemyId == "rat")
        {
            return "rat";
        }
        if (enemyId == "tank")
        {
            return "tank";
        }
        if (enemyId == "spitter")
        {
            return "spitter";
        }
        if (enemyId == "charger")
        {
            return "charger";
        }
        if (enemyId == "bomber")
        {
            return "bomber";
        }
        if (enemyId == "shaman")
        {
            return "shaman";
        }
        if (enemyId == "boss_warden")
        {
            return "boss_warden";
        }
        return nullptr;
    }

    const char* WeaponIconId(const std::string& weaponId)
    {
        if (weaponId == "smg")
        {
            return "smg";
        }
        if (weaponId == "shotgun")
        {
            return "shotgun";
        }
        if (weaponId == "sniper")
        {
            return "sniper";
        }
        if (weaponId == "rocket")
        {
            return "rocket";
        }
        if (weaponId == "laser")
        {
            return "laser";
        }
        if (weaponId == "flamethrower")
        {
            return "flamethrower";
        }
        return nullptr;
    }

    const char* StatIconId(const std::string& statKey)
    {
        if (statKey == "maxHpFlat")
        {
            return "max_hp";
        }
        if (statKey == "moveSpeedPct")
        {
            return "speed";
        }
        if (statKey == "atkSpeedPct")
        {
            return "attack_speed";
        }
        if (statKey == "critChancePct")
        {
            return "crit_chance";
        }
        if (statKey == "rangePct")
        {
            return "range";
        }
        if (statKey == "damagePct")
        {
            return "percent_damage";
        }
        if (statKey == "damageFlat")
        {
            return "ranged_damage";
        }
        if (statKey == "healPct")
        {
            return "lifesteal";
        }
        if (statKey == "critMultiplier")
        {
            return "crit_chance";
        }
        return nullptr;
    }

    std::string StatDisplayName(const Brotato3DGameInstance& gameInstance, const std::string& statKey)
    {
        if (statKey == "maxHpFlat")
        {
            return Localize(gameInstance, "character.hp", "生命");
        }
        if (statKey == "moveSpeedPct")
        {
            return Localize(gameInstance, "character.move", "移速");
        }
        if (statKey == "atkSpeedPct")
        {
            return Localize(gameInstance, "stat.attack_speed", "攻速");
        }
        if (statKey == "critChancePct")
        {
            return Localize(gameInstance, "character.crit", "暴击");
        }
        if (statKey == "rangePct")
        {
            return Localize(gameInstance, "character.range", "射程");
        }
        if (statKey == "damagePct" || statKey == "damageFlat")
        {
            return Localize(gameInstance, "character.damage", "伤害");
        }
        if (statKey == "healPct")
        {
            return Localize(gameInstance, "stat.heal", "治疗");
        }
        if (statKey == "critMultiplier")
        {
            return Localize(gameInstance, "stat.crit_damage", "暴伤");
        }
        return statKey;
    }

    ImVec4 StatAccentColor(const std::string& statKey, float alpha)
    {
        if (statKey == "maxHpFlat" || statKey == "healPct")
        {
            return ImVec4(0.28f, 0.70f, 0.44f, alpha);
        }
        if (statKey == "moveSpeedPct" || statKey == "rangePct")
        {
            return ImVec4(0.26f, 0.66f, 0.86f, alpha);
        }
        if (statKey == "critChancePct" || statKey == "critMultiplier")
        {
            return ImVec4(0.92f, 0.72f, 0.22f, alpha);
        }
        if (statKey == "atkSpeedPct")
        {
            return ImVec4(0.88f, 0.48f, 0.20f, alpha);
        }
        return ImVec4(0.84f, 0.32f, 0.30f, alpha);
    }

    void MaybePlayHoverSfx(const std::string& hoverId)
    {
        static std::string lastHoverId;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (lastHoverId != hoverId)
            {
                Brotato3D::PlayUiHoverSfx();
                lastHoverId = hoverId;
            }
        }
        else
        {
            if (lastHoverId == hoverId)
            {
                lastHoverId.clear();
            }
        }
    }

    void DrawPlaceholderBadge(Brotato3DGameInstance& gameInstance)
    {
        if (!Brotato3D::PlaceholderAssets::Exists(Brotato3D::PlaceholderAssets::Sfx("fire_smg_01.wav")))
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float uiScale = GetUiScale(viewport);
        const std::string text = "PLACEHOLDER ASSETS";
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 padding(10.0f * uiScale, 6.0f * uiScale);
        const ImVec2 max(viewport->Pos.x + viewport->Size.x - 12.0f * uiScale, viewport->Pos.y + 18.0f * uiScale);
        const ImVec2 min(max.x - textSize.x - padding.x * 2.0f, max.y - textSize.y - padding.y * 2.0f);
        drawList->AddRectFilled(min, max, IM_COL32(166, 28, 28, 185), 6.0f * uiScale);
        drawList->AddRect(min, max, IM_COL32(255, 180, 180, 220), 6.0f * uiScale, 0, 1.5f * uiScale);
        drawList->AddText(ImVec2(min.x + padding.x, min.y + padding.y), IM_COL32(255, 240, 240, 255), text.c_str());
    }

    void DrawMenuBackdrop(Brotato3DGameInstance& gameInstance, float dimAlpha)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        const float t = static_cast<float>(ImGui::GetTime());
        const ImVec2 min = viewport->Pos;
        const ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
        const ImVec2 viewportSize(viewport->Size.x, viewport->Size.y);

        const std::string bgPath = Brotato3D::PlaceholderAssets::Menu("splash_bg.png");
        const std::string mistBackPath = Brotato3D::PlaceholderAssets::Menu("splash_mist_back.png");
        const std::string mistMidPath = Brotato3D::PlaceholderAssets::Menu("splash_mist_mid.png");
        const std::string mistFrontPath = Brotato3D::PlaceholderAssets::Menu("splash_mist_front.png");
        const std::string brotatoPath = Brotato3D::PlaceholderAssets::Menu("splash_brotato.png");
        const std::string postPath = Brotato3D::PlaceholderAssets::Menu("splash_post.png");

        const ImTextureID bg = LoadUiTexture(gameInstance, bgPath);
        const ImTextureID mistBack = LoadUiTexture(gameInstance, mistBackPath);
        const ImTextureID mistMid = LoadUiTexture(gameInstance, mistMidPath);
        const ImTextureID mistFront = LoadUiTexture(gameInstance, mistFrontPath);
        const ImTextureID brotato = LoadUiTexture(gameInstance, brotatoPath);
        const ImTextureID post = LoadUiTexture(gameInstance, postPath);

        if (bg)
        {
            DrawImageCover(drawList, bg, GetTexturePixelSize(gameInstance, bgPath), min, max);
        }
        else
        {
            drawList->AddRectFilled(min, max, IM_COL32(19, 22, 28, 255));
        }

        auto drawParallaxLayer = [drawList, &min, &max, &viewportSize](ImTextureID texture,
                                                                       const ImVec2& texSize,
                                                                       float shiftX,
                                                                       float shiftY,
                                                                       ImU32 tint)
        {
            if (!texture)
            {
                return;
            }
            DrawImageCover(drawList,
                           texture,
                           texSize,
                           ImVec2(min.x + shiftX, min.y + shiftY),
                           ImVec2(min.x + shiftX + viewportSize.x, min.y + shiftY + viewportSize.y),
                           tint);
        };

        drawParallaxLayer(mistBack, GetTexturePixelSize(gameInstance, mistBackPath), std::sin(t * 0.17f) * 18.0f, 0.0f, IM_COL32(255, 255, 255, 160));
        drawParallaxLayer(mistMid, GetTexturePixelSize(gameInstance, mistMidPath), std::sin(t * 0.23f) * 32.0f, 0.0f, IM_COL32(255, 255, 255, 180));
        drawParallaxLayer(mistFront, GetTexturePixelSize(gameInstance, mistFrontPath), std::sin(t * 0.31f) * 54.0f, 0.0f, IM_COL32(255, 255, 255, 205));
        if (brotato)
        {
            const float h = viewport->Size.y * 0.74f;
            const float w = h * 0.85f;
            DrawImageContain(drawList,
                             brotato,
                             GetTexturePixelSize(gameInstance, brotatoPath),
                             ImVec2(viewport->Pos.x + viewport->Size.x * 0.53f, viewport->Pos.y + viewport->Size.y * 0.18f),
                             ImVec2(viewport->Pos.x + viewport->Size.x * 0.53f + w,
                                    viewport->Pos.y + viewport->Size.y * 0.18f + h),
                             0.0f,
                             IM_COL32(255, 255, 255, 235));
        }

        DrawFullscreenDim(viewport, dimAlpha);
        if (post)
        {
            DrawImageCover(drawList, post, GetTexturePixelSize(gameInstance, postPath), min, max, IM_COL32(255, 255, 255, 160));
        }
    }

    int CountTierOneWeapons(const std::vector<Brotato3D::FWeaponRuntime>& weapons, const std::string& weaponId)
    {
        return static_cast<int>(std::count_if(weapons.begin(), weapons.end(),
                                              [&weaponId](const Brotato3D::FWeaponRuntime& weapon)
                                              {
                                                  return weapon.weaponId == weaponId && weapon.tier == 1;
                                              }));
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
            return fmt::format(fmt::runtime(Localize(gameInstance, "tooltip.heal", "当前 HP {0} -> {1}")),
                               player.currentHp,
                               healed);
        }
        const float current = GetStatValue(player, stats, statKey);
        return fmt::format(fmt::runtime(Localize(gameInstance, "tooltip.current_after", "当前 {0} -> 升级后 {1}")),
                           FormatStatValue(statKey, current),
                           FormatStatValue(statKey, current + delta));
    }

    std::string Tr(const Brotato3DGameInstance& gameInstance, const std::string& key, const std::string& fallback)
    {
        return Localize(gameInstance, key, fallback);
    }

    template <typename... Args>
    std::string TrFormat(const Brotato3DGameInstance& gameInstance,
                         const std::string& key,
                         const std::string& fallback,
                         Args&&... args)
    {
        return fmt::format(fmt::runtime(Localize(gameInstance, key, fallback)), std::forward<Args>(args)...);
    }
}

namespace Brotato3D
{
    void RenderMainMenu(Brotato3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float uiScale = GetUiScale(viewport);
        DrawMenuBackdrop(gameInstance, 0.24f);
        DrawPlaceholderBadge(gameInstance);

        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        ImGui::Begin("MainMenu", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::SetWindowFontScale(uiScale);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const std::string logoPath = Brotato3D::PlaceholderAssets::Menu("logo.png");
        const ImTextureID logo = LoadUiTexture(gameInstance, logoPath);
        const ImTextureID panelTexture = LoadHudTexture(gameInstance, "panel_transparent.png");
        const ImVec2 panelMin(viewport->Pos.x + viewport->Size.x * 0.075f, viewport->Pos.y + viewport->Size.y * 0.15f);
        const ImVec2 panelMax(panelMin.x + 488.0f * uiScale, panelMin.y + 628.0f * uiScale);
        DrawPanel(drawList, panelTexture, panelMin, panelMax, IM_COL32(8, 12, 18, 230), 17.0f * uiScale, IM_COL32(78, 92, 110, 240));
        drawList->AddRect(panelMin, panelMax, IM_COL32(232, 182, 96, 110), 16.0f * uiScale, 0, 1.5f * uiScale);

        if (logo)
        {
            const ImVec2 logoMin(panelMin.x + 28.0f * uiScale, panelMin.y + 16.0f * uiScale);
            DrawImageContain(drawList,
                             logo,
                             GetTexturePixelSize(gameInstance, logoPath),
                             logoMin,
                             ImVec2(logoMin.x + 416.0f * uiScale, logoMin.y + 148.0f * uiScale),
                             0.0f,
                             IM_COL32(255, 255, 255, 255));
        }
        else
        {
            const std::string title = "BROTATO 3D";
            const float fontSize = 62.0f * uiScale;
            const ImVec2 textSize = CalcFontTextSize(gameInstance.GetBigFont(), fontSize, title);
            drawList->AddText(gameInstance.GetBigFont(),
                              fontSize,
                              ImVec2(panelMin.x + 28.0f * uiScale, panelMin.y + 34.0f * uiScale),
                              IM_COL32(255, 244, 214, 255),
                              title.c_str());
            drawList->AddRectFilled(ImVec2(panelMin.x + 28.0f * uiScale, panelMin.y + 38.0f * uiScale + textSize.y),
                                    ImVec2(panelMin.x + 28.0f * uiScale + textSize.x, panelMin.y + 46.0f * uiScale + textSize.y),
                                    IM_COL32(232, 92, 50, 215),
                                    4.0f * uiScale);
        }

        ImGui::SetCursorScreenPos(ImVec2(panelMin.x + 34.0f * uiScale, panelMin.y + 176.0f * uiScale));
        ImGui::BeginGroup();
        ImGui::SetWindowFontScale(1.1f * uiScale);
        const std::string subtitle = Tr(gameInstance, "main.subtitle", "幸存 10 波");
        ImGui::TextColored(ImVec4(0.94f, 0.90f, 0.78f, 1.0f), "%s", subtitle.c_str());
        ImGui::SetWindowFontScale(uiScale);
        ImGui::TextColored(ImVec4(0.88f, 0.79f, 0.55f, 1.0f), "%s", "P1-P10 playable build");
        ImGui::Dummy(Scale(0.0f, 18.0f, uiScale));
        const Brotato3D::FBestRecord& bestRecord = gameInstance.GetBestRecord();
        const std::string bestText = TrFormat(gameInstance,
                                              "main.best",
                                              "最佳记录：通关 {0} 次 / 击杀 {1}",
                                              bestRecord.totalWins,
                                              bestRecord.totalKills);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(226, 230, 235, 255));
        ImGui::TextWrapped("%s", bestText.c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(Scale(0.0f, 18.0f, uiScale));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.78f, 0.22f, 0.16f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.32f, 0.20f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.16f, 0.12f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * uiScale);
        const ImVec2 buttonSize = Scale(296.0f, 48.0f, uiScale);
        if (ImGui::Button(Tr(gameInstance, "main.start", "开始游戏").c_str(), buttonSize))
        {
            gameInstance.GoToCharacterSelect();
        }
        MaybePlayHoverSfx("main.start");
        ImGui::Dummy(Scale(0.0f, 8.0f, uiScale));
        ImGui::BeginDisabled();
        ImGui::Button(Tr(gameInstance, "main.continue", "继续上次").c_str(), buttonSize);
        ImGui::EndDisabled();
        ImGui::Dummy(Scale(0.0f, 8.0f, uiScale));
        if (ImGui::Button(Tr(gameInstance, "main.settings", "设置").c_str(), buttonSize))
        {
            Brotato3D::PlayUiClickSfx();
            ImGui::OpenPopup("Settings");
        }
        MaybePlayHoverSfx("main.settings");
        ImGui::Dummy(Scale(0.0f, 8.0f, uiScale));
        if (ImGui::Button(Tr(gameInstance, "main.exit", "退出").c_str(), buttonSize))
        {
            gameInstance.ExitGame();
        }
        MaybePlayHoverSfx("main.exit");
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::EndGroup();
        RenderSettingsModal(gameInstance);
        ImGui::End();
    }

    void RenderCharacterSelect(Brotato3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float uiScale = GetUiScale(viewport);
        DrawMenuBackdrop(gameInstance, 0.34f);
        DrawPlaceholderBadge(gameInstance);

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
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImVec4(0.95f, 0.72f, 0.22f, 1.0f) :
                                                              ImVec4(0.35f, 0.38f, 0.44f, 1.0f));
            ImGui::BeginChild(fmt::format("CharacterCard{}", character.id).c_str(), ImVec2(cardWidth, cardHeight), true);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            DrawPanel(drawList,
                      LoadHudTexture(gameInstance, "panel_normal.png"),
                      ImGui::GetWindowPos(),
                      ImVec2(ImGui::GetWindowPos().x + cardWidth, ImGui::GetWindowPos().y + cardHeight),
                      IM_COL32(12, 16, 20, 235),
                      10.0f * uiScale,
                      selected ? IM_COL32(108, 98, 72, 255) : IM_COL32(72, 82, 96, 240));
            const char* characterIcon = CharacterIconId(character.id);
            const ImTextureID portrait = characterIcon ? LoadIconTexture(gameInstance, "characters", characterIcon) : EmptyTexture();
            const ImVec2 portraitMin = ImGui::GetCursorScreenPos();
            const ImVec2 portraitMax(portraitMin.x + 200.0f * uiScale, portraitMin.y + 150.0f * uiScale);
            if (portrait)
            {
                drawList->AddRectFilled(portraitMin, portraitMax, IM_COL32(10, 14, 18, 220), 6.0f * uiScale);
                DrawImageContain(drawList,
                                 portrait,
                                 GetTexturePixelSize(gameInstance, Brotato3D::PlaceholderAssets::Icon("characters", characterIcon)),
                                 portraitMin,
                                 portraitMax,
                                 4.0f * uiScale);
            }
            else
            {
                drawList->AddRectFilled(portraitMin, portraitMax,
                                        IM_COL32(static_cast<int>(character.color.r * 255.0f),
                                                 static_cast<int>(character.color.g * 255.0f),
                                                 static_cast<int>(character.color.b * 255.0f),
                                                 255),
                                        4.0f * uiScale);
            }
            drawList->AddRect(portraitMin, portraitMax, IM_COL32(255, 214, 132, 72), 6.0f * uiScale);
            ImGui::Dummy(Scale(200.0f, 162.0f, uiScale));
            ImGui::SetWindowFontScale(1.35f * uiScale);
            ImGui::Text("%s", Localize(gameInstance, "character." + character.id + ".name", character.name).c_str());
            ImGui::SetWindowFontScale(uiScale);
            ImGui::TextWrapped("%s", Localize(gameInstance, "character." + character.id + ".tagline", character.tagline).c_str());
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
            MaybePlayHoverSfx(fmt::format("character.{}", character.id));
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
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImVec4(arena.baseGroundColor.r, arena.baseGroundColor.g, arena.baseGroundColor.b, 1.0f));
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
        const FWaveSystem& waveSystem = gameInstance.GetWaveSystem();
        const FWaveDef* waveDef = waveSystem.GetCurrentWaveDef();
        const ImTextureID panelNormal = LoadHudTexture(gameInstance, "panel_normal.png");
        const ImTextureID panelFlat = LoadHudTexture(gameInstance, "panel_flat.png");

        ImGui::SetNextWindowPos(Scale(8.0f, 8.0f, uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(280.0f, 90.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("PlayerPanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        DrawPanel(ImGui::GetWindowDrawList(),
                  panelNormal,
                  ImGui::GetWindowPos(),
                  ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                  IM_COL32(14, 16, 22, 210),
                  8.0f * uiScale,
                  IM_COL32(78, 92, 110, 245));
        ImGui::SetWindowFontScale(uiScale);
        ImGui::SetCursorPos(Scale(18.0f, 14.0f, uiScale));
        DrawBar(ImGui::GetCursorScreenPos(),
                Scale(244.0f, 24.0f, uiScale),
                hpRatio,
                IM_COL32(210, 68, 58, 255),
                fmt::format("HP {} / {}", player.currentHp, player.maxHp).c_str(),
                uiScale);
        ImGui::SetCursorPos(Scale(18.0f, 44.0f, uiScale));
        DrawBar(ImGui::GetCursorScreenPos(),
                Scale(244.0f, 18.0f, uiScale),
                xpToNext > 0 ? static_cast<float>(player.currentXp) / static_cast<float>(xpToNext) : 0.0f,
                IM_COL32(72, 135, 245, 255),
                fmt::format("Lv {}  XP {}/{}", player.level, player.currentXp, xpToNext).c_str(),
                uiScale);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2((viewport->Size.x - 260.0f * uiScale) * 0.5f, 8.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(320.0f, 88.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("WavePanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        DrawPanel(ImGui::GetWindowDrawList(),
                  panelFlat,
                  ImGui::GetWindowPos(),
                  ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                  IM_COL32(12, 16, 20, 180),
                  8.0f * uiScale,
                  IM_COL32(62, 74, 90, 235));
        ImGui::SetWindowFontScale(uiScale);
        ImGui::SetCursorPos(Scale(22.0f, 14.0f, uiScale));
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
        float iconX = ImGui::GetWindowPos().x + 22.0f * uiScale;
        const float iconY = ImGui::GetWindowPos().y + 60.0f * uiScale;
        if (waveDef)
        {
            std::unordered_set<std::string> seenEnemyIds;
            for (const FSpawnEntry& spawn : waveDef->spawns)
            {
                if (!seenEnemyIds.insert(spawn.enemyId).second)
                {
                    continue;
                }
                const char* iconId = EnemyIconId(spawn.enemyId);
                const ImTextureID icon = iconId ? LoadIconTexture(gameInstance, "enemies", iconId) : EmptyTexture();
                const ImVec2 min(iconX, iconY);
                const ImVec2 max(iconX + 24.0f * uiScale, iconY + 24.0f * uiScale);
                DrawPanel(ImGui::GetWindowDrawList(), EmptyTexture(), min, max, IM_COL32(20, 24, 28, 200), 4.0f * uiScale);
                if (icon)
                {
                    ImGui::GetWindowDrawList()->AddImage(icon, min, max);
                }
                iconX += 30.0f * uiScale;
                if (iconX > ImGui::GetWindowPos().x + 296.0f * uiScale)
                {
                    break;
                }
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(viewport->Size.x - 160.0f * uiScale, 8.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(150.0f, 50.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("ResourcePanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        DrawPanel(ImGui::GetWindowDrawList(),
                  panelFlat,
                  ImGui::GetWindowPos(),
                  ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                  IM_COL32(16, 18, 22, 190),
                  8.0f * uiScale,
                  IM_COL32(64, 72, 84, 235));
        ImGui::SetWindowFontScale(uiScale);
        ImGui::SetCursorPos(Scale(22.0f, 16.0f, uiScale));
        ImGui::Text("%s", TrFormat(gameInstance, "hud.materials", "材料：{0}", player.materials).c_str());
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(viewport->Size.x - 246.0f * uiScale, viewport->Size.y - 58.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(236.0f, 48.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("ItemSlots", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        DrawPanel(ImGui::GetWindowDrawList(),
                  panelFlat,
                  ImGui::GetWindowPos(),
                  ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                  IM_COL32(14, 15, 20, 180),
                  8.0f * uiScale,
                  IM_COL32(58, 68, 84, 235));
        ImGui::SetWindowFontScale(uiScale);
        ImGui::SetCursorPos(Scale(18.0f, 12.0f, uiScale));
        const auto& ownedItemIds = gameInstance.GetOwnedItemIds();
        for (size_t slotIndex = 0; slotIndex < 6; ++slotIndex)
        {
            if (slotIndex > 0)
            {
                ImGui::SameLine();
            }

            const Brotato3D::FItemDef* item = slotIndex < ownedItemIds.size() ? gameInstance.GetItemDef(ownedItemIds[slotIndex]) : nullptr;
            const ImTextureID itemIcon = item ? LoadIconTexture(gameInstance, "items", item->id) : EmptyTexture();
            ImGui::InvisibleButton(fmt::format("##ItemSlot{}", slotIndex).c_str(), Scale(30.0f, 30.0f, uiScale));
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const ImVec4 slotColor = item ? RarityColor(item->rarity, 0.85f) : ImVec4(0.12f, 0.13f, 0.15f, 0.82f);
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, Color(glm::vec4(slotColor.x, slotColor.y, slotColor.z, slotColor.w)), 4.0f * uiScale);
            ImGui::GetWindowDrawList()->AddRect(min, max,
                                                item ? Color(glm::vec4(RarityColor(item->rarity, 1.0f).x,
                                                                       RarityColor(item->rarity, 1.0f).y,
                                                                       RarityColor(item->rarity, 1.0f).z,
                                                                       1.0f)) :
                                                       IM_COL32(100, 108, 118, 180),
                                                4.0f * uiScale,
                                                0,
                                                1.5f * uiScale);
            if (itemIcon)
            {
                DrawImageContain(ImGui::GetWindowDrawList(),
                                 itemIcon,
                                 GetTexturePixelSize(gameInstance, Brotato3D::PlaceholderAssets::Icon("items", item->id)),
                                 min,
                                 max,
                                 2.0f * uiScale);
            }
            else
            {
                ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 9.0f * uiScale, min.y + 6.0f * uiScale),
                                                    IM_COL32_WHITE,
                                                    item ? ItemInitial(item->id) : "-");
            }
            if (item && ImGui::IsItemHovered())
            {
                MaybePlayHoverSfx(fmt::format("itemslot.{}", item->id));
                ImGui::BeginTooltip();
                ImGui::Text("%s", Localize(gameInstance, "item." + item->id + ".name", item->name).c_str());
                ImGui::TextWrapped("%s", Localize(gameInstance, "item." + item->id + ".desc", item->description).c_str());
                ImGui::EndTooltip();
            }
        }
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(8.0f * uiScale, viewport->Size.y - 86.0f * uiScale), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Scale(374.0f, 76.0f, uiScale), ImGuiCond_Always);
        ImGui::Begin("WeaponPanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
        DrawPanel(ImGui::GetWindowDrawList(),
                  panelNormal,
                  ImGui::GetWindowPos(),
                  ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                  IM_COL32(14, 16, 20, 215),
                  8.0f * uiScale,
                  IM_COL32(78, 90, 104, 245));
        ImGui::SetWindowFontScale(uiScale);
        const auto& weapons = gameInstance.GetWeapons();
        const FPlayerStats effectiveStats = gameInstance.GetEffectivePlayerStats();
        ImGui::SetCursorPos(Scale(20.0f, 12.0f, uiScale));
        ImGui::Text("%s", Tr(gameInstance, "hud.weapons", "武器").c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", Tr(gameInstance, "hud.merge_hint", "3 把 T1 -> T2").c_str());
        ImGui::SetCursorPos(Scale(18.0f, 32.0f, uiScale));
        for (size_t slotIndex = 0; slotIndex < 6; ++slotIndex)
        {
            if (slotIndex > 0)
            {
                ImGui::SameLine();
            }
            const FWeaponRuntime* weapon = slotIndex < weapons.size() ? &weapons[slotIndex] : nullptr;
            const bool hasWeapon = weapon && weapon->def;
            const char* weaponIconId = hasWeapon ? WeaponIconId(weapon->weaponId) : nullptr;
            const ImTextureID icon = weaponIconId ? LoadIconTexture(gameInstance, "weapons", weaponIconId) : EmptyTexture();
            ImGui::InvisibleButton(fmt::format("##WeaponSlot{}", slotIndex).c_str(), Scale(52.0f, 32.0f, uiScale));
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(min, max, hasWeapon ? IM_COL32(24, 28, 32, 240) : IM_COL32(18, 20, 24, 160), 5.0f * uiScale);
            ImGui::GetWindowDrawList()->AddRect(min, max, hasWeapon && weapon->tier == 2 ? IM_COL32(255, 210, 70, 255) :
                                                IM_COL32(180, 185, 195, 180), 5.0f * uiScale, 0, 2.0f * uiScale);
            if (icon)
            {
                DrawImageContain(ImGui::GetWindowDrawList(),
                                 icon,
                                 GetTexturePixelSize(gameInstance, Brotato3D::PlaceholderAssets::Icon("weapons", weaponIconId)),
                                 min,
                                 max,
                                 2.0f * uiScale);
            }
            else
            {
                ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 7.0f * uiScale, min.y + 7.0f * uiScale),
                                                    IM_COL32_WHITE,
                                                    hasWeapon ? WeaponShortName(weapon->weaponId) : "-");
            }
            if (hasWeapon && weapon->tier == 2)
            {
                ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - 12.0f * uiScale, min.y + 2.0f * uiScale),
                                                    IM_COL32(255, 226, 120, 255),
                                                    "2");
            }
            if (hasWeapon && ImGui::IsItemHovered())
            {
                MaybePlayHoverSfx(fmt::format("weaponslot.{}", weapon->weaponId));
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
                if (NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, enemy.worldPos, center) &&
                    NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, enemy.worldPos + glm::vec3(enemy.def->heal.radiusMeters, 0.0f, 0.0f), edge))
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
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, enemy.worldPos + glm::vec3(0.0f, enemy.def->size.y + 0.2f, 0.0f), screen))
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
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, projectile.lastWorldPos, from) || !NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, projectile.worldPos, to))
            {
                continue;
            }

            drawList->AddLine(from, to, Color(glm::vec4(projectile.color, 0.6f)), std::max(1.5f, 2.0f * uiScale));
        }

        for (const FMuzzleFlash& flash : gameInstance.GetMuzzleFlashes())
        {
            ImVec2 center{};
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, flash.worldPos, center))
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
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, text.worldPos, screen))
            {
                continue;
            }
            const float alpha = text.lifeMs > 0.0f ? std::clamp(text.remainingMs / text.lifeMs, 0.0f, 1.0f) : 0.0f;
            const float rise = (1.0f - alpha) * 30.0f * uiScale;
            glm::vec4 color = text.color;
            color.a *= alpha;
            drawList->AddText(gameInstance.GetBigFont(),
                              std::max(18.0f, 24.0f * uiScale * text.fontScale),
                              ImVec2(screen.x, screen.y - rise),
                              Color(color),
                              text.text.c_str());
        }

        for (const FExpandingRing& ring : gameInstance.GetExplosionRings())
        {
            ImVec2 center{};
            ImVec2 edge{};
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, ring.worldPos, center) ||
                !NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, ring.worldPos + glm::vec3(ring.maxRadius, 0.0f, 0.0f), edge))
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
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, beam.from, from) || !NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, beam.to, to))
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
            const float fontScale = 2.6f * uiScale;
            const float fontSize = ImGui::GetFontSize() * fontScale;
            const ImVec2 textSize = CalcFontTextSize(gameInstance.GetBigFont(), fontSize, bannerText);
            const bool bossBanner = bannerText.find("BOSS") != std::string::npos;
            const char* waveEnemyIconId = bossBanner ? "boss_warden" :
                ((waveDef && !waveDef->spawns.empty()) ? EnemyIconId(waveDef->spawns.front().enemyId) : nullptr);
            const ImTextureID waveEnemyIcon = waveEnemyIconId ? LoadIconTexture(gameInstance, "enemies", waveEnemyIconId) : EmptyTexture();
            const float iconSize = bossBanner ? 80.0f * uiScale : 52.0f * uiScale;
            const float iconGap = waveEnemyIcon ? 18.0f * uiScale : 0.0f;
            const float totalWidth = textSize.x + (waveEnemyIcon ? iconSize + iconGap : 0.0f);
            const float originX = viewport->Pos.x + (viewport->Size.x - totalWidth) * 0.5f;
            const ImVec2 pos(originX + (waveEnemyIcon ? iconSize + iconGap : 0.0f), viewport->Pos.y + viewport->Size.y * 0.36f);
            const ImVec2 bgMin(originX - 18.0f * uiScale, pos.y - 12.0f * uiScale);
            const ImVec2 bgMax(originX + totalWidth + 18.0f * uiScale, pos.y + textSize.y + 14.0f * uiScale);
                drawList->AddRectFilled(bgMin,
                                        bgMax,
                                        bossBanner ? IM_COL32(60, 10, 10, static_cast<int>(alpha * 190.0f)) :
                                                     IM_COL32(14, 18, 22, static_cast<int>(alpha * 170.0f)),
                                        10.0f * uiScale);
            drawList->AddRect(bgMin,
                              bgMax,
                              bossBanner ? IM_COL32(255, 84, 64, static_cast<int>(alpha * 255.0f)) :
                                           IM_COL32(255, 220, 120, static_cast<int>(alpha * 180.0f)),
                              10.0f * uiScale,
                              0,
                              2.0f * uiScale);
            if (waveEnemyIcon)
            {
                const ImVec2 iconMin(originX, pos.y - (iconSize - textSize.y) * 0.2f);
                const ImVec2 iconMax(originX + iconSize, iconMin.y + iconSize);
                DrawImageContain(drawList,
                                 waveEnemyIcon,
                                 GetTexturePixelSize(gameInstance, Brotato3D::PlaceholderAssets::Icon("enemies", waveEnemyIconId ? waveEnemyIconId : "")),
                                 iconMin,
                                 iconMax,
                                 0.0f,
                                 IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
            }
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
            const float fontSize = ImGui::GetFontSize() * fontScale;
            const ImVec2 textSize = CalcFontTextSize(gameInstance.GetBigFont(), fontSize, bannerText);
            const ImVec2 pos(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f,
                             viewport->Pos.y + viewport->Size.y * 0.44f);
            drawList->AddText(gameInstance.GetBigFont(),
                              fontSize,
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
        ImGui::SetNextWindowSize(Scale(820.0f, 410.0f, uiScale), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Upgrade",
                                   nullptr,
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::SetWindowFontScale(uiScale);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 windowMin = ImGui::GetWindowPos();
            const ImVec2 windowMax(windowMin.x + ImGui::GetWindowSize().x, windowMin.y + ImGui::GetWindowSize().y);
            const ImTextureID shopBackground = LoadMenuTexture(gameInstance, "shop_background.png");
            const ImTextureID modalPanel = LoadHudTexture(gameInstance, "panel_flat.png");
            if (shopBackground)
            {
                DrawImageCover(drawList,
                               shopBackground,
                               GetTexturePixelSize(gameInstance, Brotato3D::PlaceholderAssets::Menu("shop_background.png")),
                               windowMin,
                               windowMax,
                               IM_COL32(255, 255, 255, 110));
            }
            drawList->AddRectFilled(windowMin, windowMax, IM_COL32(8, 10, 16, 164), 12.0f * uiScale);
            DrawPanel(drawList, modalPanel, windowMin, windowMax, IM_COL32(12, 14, 20, 236), 12.0f * uiScale, IM_COL32(74, 86, 102, 246));
            drawList->AddRect(windowMin, windowMax, IM_COL32(232, 182, 96, 102), 12.0f * uiScale, 0, 1.5f * uiScale);

            const std::string title = Tr(gameInstance, "upgrade.title", "选择 1 个升级");
            drawList->AddText(gameInstance.GetBigFont(),
                              std::max(22.0f, ImGui::GetFontSize() * 1.55f),
                              ImVec2(windowMin.x + 28.0f * uiScale, windowMin.y + 18.0f * uiScale),
                              IM_COL32(255, 244, 214, 255),
                              title.c_str());
            drawList->AddText(ImVec2(windowMin.x + 30.0f * uiScale, windowMin.y + 58.0f * uiScale),
                              IM_COL32(215, 220, 226, 230),
                              Localize(gameInstance, "upgrade.subtitle", "本波结束后的战利品，请择其一").c_str());

            const auto& choices = gameInstance.GetCurrentUpgradeChoices();
            const float cardWidth = 236.0f * uiScale;
            const float cardHeight = 250.0f * uiScale;
            const float gap = 18.0f * uiScale;
            const float totalWidth = static_cast<float>(choices.size()) * cardWidth +
                                     std::max(0.0f, static_cast<float>(choices.size() - 1)) * gap;
            ImGui::SetCursorPos(ImVec2((ImGui::GetWindowSize().x - totalWidth) * 0.5f, 102.0f * uiScale));
            for (size_t index = 0; index < choices.size(); ++index)
            {
                if (index > 0)
                {
                    ImGui::SameLine(0.0f, gap);
                }
                const FUpgradeCardDef& choice = choices[index];
                const std::string choiceName = Localize(gameInstance, "upgrade." + choice.id + ".name", choice.name);
                const std::string statLabel = StatDisplayName(gameInstance, choice.stat);
                const std::string deltaText = choice.stat == "healPct" ?
                    fmt::format("{} {:+.0f}%", statLabel, choice.delta * 100.0f) :
                    fmt::format("{} {}", statLabel, FormatStatValue(choice.stat, choice.delta));
                const std::string previewText = BuildStatPreview(player, gameInstance, effectiveStats, choice.stat, choice.delta);
                const char* statIconId = StatIconId(choice.stat);
                const ImTextureID cardIcon = statIconId ? LoadIconTexture(gameInstance, "stats", statIconId) : EmptyTexture();
                const ImVec4 accentColor = StatAccentColor(choice.stat, 1.0f);

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.05f));
                ImGui::PushStyleColor(ImGuiCol_Border, accentColor);
                ImGui::BeginChild(fmt::format("UpgradeCard{}", index).c_str(), ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar);
                DrawPanel(ImGui::GetWindowDrawList(),
                          LoadHudTexture(gameInstance, "panel_normal.png"),
                          ImGui::GetWindowPos(),
                          ImVec2(ImGui::GetWindowPos().x + cardWidth, ImGui::GetWindowPos().y + cardHeight),
                          IM_COL32(16, 20, 26, 232),
                          10.0f * uiScale,
                          IM_COL32(66, 78, 94, 244));
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos(),
                                                          ImVec2(ImGui::GetWindowPos().x + cardWidth,
                                                                 ImGui::GetWindowPos().y + 5.0f * uiScale),
                                                          ImGui::ColorConvertFloat4ToU32(ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.85f)),
                                                          8.0f * uiScale);

                const ImVec2 iconMin = ImGui::GetCursorScreenPos();
                const ImVec2 iconMax(iconMin.x + 64.0f * uiScale, iconMin.y + 64.0f * uiScale);
                ImGui::GetWindowDrawList()->AddRectFilled(iconMin, iconMax, IM_COL32(8, 10, 14, 205), 8.0f * uiScale);
                if (cardIcon)
                {
                    DrawImageContain(ImGui::GetWindowDrawList(),
                                     cardIcon,
                                     GetTexturePixelSize(gameInstance, Brotato3D::PlaceholderAssets::Icon("stats", statIconId ? statIconId : "")),
                                     iconMin,
                                     iconMax,
                                     5.0f * uiScale);
                }
                else
                {
                    ImGui::GetWindowDrawList()->AddText(ImVec2(iconMin.x + 22.0f * uiScale, iconMin.y + 22.0f * uiScale),
                                                        IM_COL32_WHITE,
                                                        "+");
                }

                ImGui::SetCursorPos(Scale(82.0f, 16.0f, uiScale));
                ImGui::PushTextWrapPos((236.0f - 18.0f) * uiScale);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(248, 239, 212, 255));
                ImGui::TextWrapped("%s", choiceName.c_str());
                ImGui::PopStyleColor();
                ImGui::SetCursorPos(Scale(82.0f, 52.0f, uiScale));
                ImGui::PushStyleColor(ImGuiCol_Text, accentColor);
                ImGui::TextWrapped("%s", deltaText.c_str());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
                ImGui::SetCursorPos(Scale(18.0f, 90.0f, uiScale));
                ImGui::Separator();
                ImGui::SetCursorPosX(18.0f * uiScale);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 194.0f * uiScale);
                ImGui::TextWrapped("%s", previewText.c_str());
                ImGui::PopTextWrapPos();
                ImGui::SetCursorPos(Scale(18.0f, 178.0f, uiScale));
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(198, 204, 212, 255));
                ImGui::TextWrapped("%s", Localize(gameInstance, "upgrade.instant", "选择后立即生效").c_str());
                ImGui::PopStyleColor();
                ImGui::SetCursorPos(Scale(18.0f, 200.0f, uiScale));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accentColor.x * 0.72f, accentColor.y * 0.72f, accentColor.z * 0.72f, 0.96f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(std::min(accentColor.x + 0.08f, 1.0f),
                                                                     std::min(accentColor.y + 0.08f, 1.0f),
                                                                     std::min(accentColor.z + 0.08f, 1.0f),
                                                                     1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accentColor.x * 0.56f, accentColor.y * 0.56f, accentColor.z * 0.56f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * uiScale);
                if (ImGui::Button(fmt::format("{}##{}", Tr(gameInstance, "upgrade.select", "选择"), index).c_str(),
                                  Scale(208.0f, 34.0f, uiScale)))
                {
                    gameInstance.SelectUpgrade(index);
                    ImGui::CloseCurrentPopup();
                }
                MaybePlayHoverSfx(fmt::format("upgrade.{}", choice.id));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                if (ImGui::IsWindowHovered())
                {
                    DrawWideTooltip(previewText, uiScale);
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(2);
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
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImTextureID shopBackground = LoadMenuTexture(gameInstance, "shop_background.png");
            if (shopBackground)
            {
                DrawImageCover(drawList,
                               shopBackground,
                               GetTexturePixelSize(gameInstance, Brotato3D::PlaceholderAssets::Menu("shop_background.png")),
                               ImGui::GetWindowPos(),
                               ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                      ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                               IM_COL32(255, 255, 255, 132));
            }
            drawList->AddRectFilled(ImGui::GetWindowPos(),
                                    ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                           ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                                    IM_COL32(10, 12, 18, 150),
                                    10.0f * uiScale);
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
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.10f));
                ImGui::PushStyleColor(ImGuiCol_Border, passiveItem ? RarityColor(offers[index].rarity, 0.95f) :
                                                            (weaponCard ? ImVec4(0.95f, 0.72f, 0.22f, 1.0f) :
                                                                          ImVec4(0.45f, 0.49f, 0.58f, 0.95f)));
                ImGui::BeginChild(fmt::format("ShopCard{}", index).c_str(), Scale(175.0f, 260.0f, uiScale), true,
                                  ImGuiWindowFlags_NoScrollbar);
                DrawPanel(ImGui::GetWindowDrawList(),
                          LoadHudTexture(gameInstance, "panel_normal.png"),
                          ImGui::GetWindowPos(),
                          ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                 ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                          passiveItem ? Color(glm::vec4(RarityColor(offers[index].rarity, 0.66f).x,
                                                        RarityColor(offers[index].rarity, 0.66f).y,
                                                        RarityColor(offers[index].rarity, 0.66f).z,
                                                        RarityColor(offers[index].rarity, 0.66f).w)) :
                                        (weaponCard ? IM_COL32(58, 48, 22, 210) : IM_COL32(20, 24, 30, 210)),
                          8.0f * uiScale,
                          passiveItem ? IM_COL32(86, 98, 116, 245) :
                          (weaponCard ? IM_COL32(112, 94, 52, 245) : IM_COL32(72, 82, 94, 240)));
                ImGui::SetCursorPos(Scale(22.0f, 18.0f, uiScale));
                const std::string offerName =
                    Localize(gameInstance, (passiveItem ? "item." : "shop.") + offers[index].id + ".name", offers[index].name);
                const std::string offerDesc =
                    Localize(gameInstance, (passiveItem ? "item." : "shop.") + offers[index].id + ".desc", offers[index].description);
                ImTextureID cardIcon = EmptyTexture();
                std::string cardIconPath;
                if (passiveItem)
                {
                    cardIcon = LoadIconTexture(gameInstance, "items", offers[index].id);
                    cardIconPath = Brotato3D::PlaceholderAssets::Icon("items", offers[index].id);
                }
                else if (weaponCard)
                {
                    const char* weaponIconId = WeaponIconId(offers[index].weaponId);
                    cardIcon = weaponIconId ? LoadIconTexture(gameInstance, "weapons", weaponIconId) : EmptyTexture();
                    if (weaponIconId)
                    {
                        cardIconPath = Brotato3D::PlaceholderAssets::Icon("weapons", weaponIconId);
                    }
                }
                else
                {
                    const char* statIconId = StatIconId(offers[index].stat);
                    cardIcon = statIconId ? LoadIconTexture(gameInstance, "stats", statIconId) : EmptyTexture();
                    if (statIconId)
                    {
                        cardIconPath = Brotato3D::PlaceholderAssets::Icon("stats", statIconId);
                    }
                }
                const ImVec2 iconMin = ImGui::GetCursorScreenPos();
                const ImVec2 iconMax(iconMin.x + 56.0f * uiScale, iconMin.y + 56.0f * uiScale);
                ImGui::GetWindowDrawList()->AddRectFilled(iconMin, iconMax, IM_COL32(8, 10, 14, 200), 6.0f * uiScale);
                if (cardIcon)
                {
                    DrawImageContain(ImGui::GetWindowDrawList(),
                                     cardIcon,
                                     GetTexturePixelSize(gameInstance, cardIconPath),
                                     iconMin,
                                     iconMax,
                                     4.0f * uiScale);
                }
                else
                {
                    ImGui::GetWindowDrawList()->AddText(ImVec2(iconMin.x + 17.0f * uiScale, iconMin.y + 15.0f * uiScale),
                                                        IM_COL32_WHITE,
                                                        passiveItem ? ItemInitial(offers[index].id) :
                                                        (weaponCard ? WeaponShortName(offers[index].weaponId) : "+"));
                }
                ImGui::SetCursorPos(Scale(22.0f, 82.0f, uiScale));
                ImGui::PushTextWrapPos((175.0f - 22.0f) * uiScale);
                ImGui::TextWrapped("%s", offerName.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Separator();
                ImGui::PushTextWrapPos((175.0f - 22.0f) * uiScale);
                if (weaponCard)
                {
                    const int ownedTierOne = CountTierOneWeapons(gameInstance.GetWeapons(), offers[index].weaponId);
                    ImGui::Text("%s", Tr(gameInstance, "shop.weapon", "武器").c_str());
                    ImGui::Text("%s", TrFormat(gameInstance, "shop.merge", "合成：{0} / 2", std::min(ownedTierOne, 2)).c_str());
                    ImGui::TextWrapped("%s",
                                       ownedTierOne >= 1 ? Tr(gameInstance, "shop.merge_ready", "再买一把会合成 T2").c_str() :
                                                           Tr(gameInstance, "shop.merge_need", "需要 2 把相同 T1").c_str());
                }
                else if (passiveItem)
                {
                    ImGui::Text("%s", offers[index].rarity.c_str());
                    ImGui::TextWrapped("%s", offerDesc.c_str());
                }
                else
                {
                    const std::string statLabel = StatDisplayName(gameInstance, offers[index].stat);
                    ImGui::PushStyleColor(ImGuiCol_Text, StatAccentColor(offers[index].stat, 1.0f));
                    if (offers[index].stat == "healPct")
                    {
                        ImGui::TextWrapped("%s %+.0f%%", statLabel.c_str(), offers[index].delta * 100.0f);
                    }
                    else
                    {
                        ImGui::TextWrapped("%s %s", statLabel.c_str(), FormatStatValue(offers[index].stat, offers[index].delta).c_str());
                    }
                    ImGui::PopStyleColor();
                }
                ImGui::PopTextWrapPos();
                ImGui::Text("%s", TrFormat(gameInstance, "shop.cost", "价格：{0}", offers[index].cost).c_str());
                ImGui::SetCursorPos(ImVec2((175.0f - 140.0f) * 0.5f * uiScale, 210.0f * uiScale));
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
                ImGui::PopStyleColor(2);
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
            MaybePlayHoverSfx("shop.reroll");
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
            MaybePlayHoverSfx("shop.next_wave");
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
            DrawPanel(ImGui::GetWindowDrawList(),
                      LoadHudTexture(gameInstance, "panel_transparent.png"),
                      ImGui::GetWindowPos(),
                      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                             ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
                      defeated ? IM_COL32(40, 10, 12, 235) : IM_COL32(10, 22, 40, 235),
                      10.0f * uiScale);
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
                                            Localize(gameInstance, "character." + character->id + ".name", character->name)).c_str());
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
                    itemList += Localize(gameInstance, "item." + item->id + ".name", item->name);
                }
            }
            if (!ownedItemIds.empty())
            {
                ImGui::Dummy(Scale(0.0f, 4.0f, uiScale));
                for (size_t index = 0; index < ownedItemIds.size(); ++index)
                {
                    if (index > 0)
                    {
                        ImGui::SameLine();
                    }
                    const Brotato3D::FItemDef* item = gameInstance.GetItemDef(ownedItemIds[index]);
                    if (!item)
                    {
                        continue;
                    }
                    const ImTextureID icon = LoadIconTexture(gameInstance, "items", item->id);
                    ImGui::InvisibleButton(fmt::format("##ResultItem{}", index).c_str(), Scale(42.0f, 42.0f, uiScale));
                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(min, max,
                                                              Color(glm::vec4(RarityColor(item->rarity, 0.85f).x,
                                                                               RarityColor(item->rarity, 0.85f).y,
                                                                               RarityColor(item->rarity, 0.85f).z,
                                                                               RarityColor(item->rarity, 0.85f).w)),
                                                              6.0f * uiScale);
                    if (icon)
                    {
                        ImGui::GetWindowDrawList()->AddImage(icon, min, max);
                    }
                    else
                    {
                        ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 12.0f * uiScale, min.y + 12.0f * uiScale),
                                                            IM_COL32_WHITE,
                                                            ItemInitial(item->id));
                    }
                    if (ImGui::IsItemHovered())
                    {
                        MaybePlayHoverSfx(fmt::format("result.{}", item->id));
                        DrawWideTooltip(Localize(gameInstance, "item." + item->id + ".desc", item->description), uiScale);
                    }
                }
            }
            ImGui::Dummy(Scale(0.0f, 10.0f, uiScale));
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

