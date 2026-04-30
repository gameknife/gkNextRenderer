#include "KongLie3DUI.hpp"

#include <imgui.h>

#include "Assets/Core/Node.h"
#include "KongLie3DAudio.hpp"
#include "KongLie3DGameInstance.hpp"
#include "KongLie3DStyle.hpp"

namespace
{
    constexpr float BattleDurationSeconds = 30.0f;
    constexpr float OvertimeBannerDurationMs = 2000.0f;
    constexpr double StatsRefreshIntervalSeconds = 0.6;
    constexpr int BenchRow = 8;
    constexpr float BenchWorldZ = 8.5f;
    constexpr float LeftColumnX = KongLie3D::ScaleUi(8.0f);
    constexpr float LeftColumnY = KongLie3D::ScaleUi(60.0f);
    constexpr float LeftColumnWidth = KongLie3D::ScaleUi(248.0f);
    constexpr float LeftColumnGap = KongLie3D::ScaleUi(8.0f);
    constexpr float LeftColumnBottomMargin = KongLie3D::ScaleUi(12.0f);
    constexpr float BottomHeroBarMargin = KongLie3D::ScaleUi(12.0f);
    constexpr float BottomHeroBarHeight = KongLie3D::ScaleUi(208.0f);
    constexpr float BottomHeroCardGap = KongLie3D::ScaleUi(8.0f);
    constexpr float BottomHeroCardHeight = KongLie3D::ScaleUi(196.0f);
    constexpr float BottomHeroCardWidthMax = KongLie3D::ScaleUi(208.0f);
    constexpr float BattleStartBannerDurationMs = 800.0f;
    constexpr float ResultModalFadeDurationMs = 200.0f;

    struct FStatsRow
    {
        std::string pieceId;
        std::string name;
        int damageAD = 0;
        int damageAP = 0;
        int damageTaken = 0;
        int healing = 0;
        bool alive = true;
    };

    struct FStatsCache
    {
        double lastRefreshTime = -1.0;
        std::vector<FStatsRow> playerRows;
        std::vector<FStatsRow> enemyRows;
    };

    ImU32 ToImColor(const glm::vec3& color)
    {
        return IM_COL32(static_cast<int>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
                        static_cast<int>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
                        static_cast<int>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
                        255);
    }

    ImVec4 ToImVec4(const glm::vec3& color, float alpha = 1.0f)
    {
        return ImVec4(color.r, color.g, color.b, alpha);
    }

    ImVec4 GetHealthColor(float ratio)
    {
        const ImVec4 red(0.85f, 0.22f, 0.20f, 1.0f);
        const ImVec4 yellow(0.93f, 0.76f, 0.22f, 1.0f);
        const ImVec4 green(0.24f, 0.80f, 0.33f, 1.0f);
        const auto lerpColor = [](const ImVec4& lhs, const ImVec4& rhs, float t)
        {
            return ImVec4(lhs.x + (rhs.x - lhs.x) * t,
                          lhs.y + (rhs.y - lhs.y) * t,
                          lhs.z + (rhs.z - lhs.z) * t,
                          lhs.w + (rhs.w - lhs.w) * t);
        };

        if (ratio <= 0.5f)
        {
            const float t = std::clamp(ratio / 0.5f, 0.0f, 1.0f);
            return lerpColor(red, yellow, t);
        }

        const float t = std::clamp((ratio - 0.5f) / 0.5f, 0.0f, 1.0f);
        return lerpColor(yellow, green, t);
    }

    bool PushFontIfAvailable(ImFont* font)
    {
        if (!font)
        {
            return false;
        }

        ImGui::PushFont(font);
        return true;
    }

    void PopFontIfPushed(bool pushed)
    {
        if (pushed)
        {
            ImGui::PopFont();
        }
    }

    const char* GetRoleLabel(std::string_view role)
    {
        if (role == "tank")
        {
            return KongLie3D::U8Text(u8"坦克");
        }
        if (role == "adc")
        {
            return KongLie3D::U8Text(u8"射手");
        }
        if (role == "support")
        {
            return KongLie3D::U8Text(u8"辅助");
        }
        if (role == "atk_tank")
        {
            return KongLie3D::U8Text(u8"战士");
        }
        return KongLie3D::U8Text(u8"未知");
    }

    ImU32 ToImColor(const ImVec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    struct FAliveCounts
    {
        int player = 0;
        int enemy = 0;
    };

    struct FStatsSummary
    {
        int damage = 0;
        int damageAD = 0;
        int damageAP = 0;
        int taken = 0;
        int healing = 0;
    };

    FAliveCounts CountAlivePieces(const KongLie3DGameInstance& gameInstance)
    {
        FAliveCounts counts;
        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (piece.onBench || !piece.alive)
            {
                continue;
            }

            if (piece.def.team == "player")
            {
                ++counts.player;
            }
            else if (piece.def.team == "enemy")
            {
                ++counts.enemy;
            }
        }
        return counts;
    }

    FStatsSummary BuildStatsSummary(const std::vector<FStatsRow>& rows)
    {
        FStatsSummary summary;
        for (const auto& row : rows)
        {
            summary.damageAD += row.damageAD;
            summary.damageAP += row.damageAP;
            summary.damage += row.damageAD + row.damageAP;
            summary.taken += row.damageTaken;
            summary.healing += row.healing;
        }
        return summary;
    }

    std::string FormatBattleClock(float totalSeconds)
    {
        const int clampedSeconds = static_cast<int>(std::max(0.0f, std::ceil(totalSeconds)));
        const int minutes = clampedSeconds / 60;
        const int seconds = clampedSeconds % 60;
        return fmt::format("{:02d}:{:02d}", minutes, seconds);
    }

    std::string BuildActiveSynergySummary(const std::vector<KongLie3D::FSynergyStatus>& statuses)
    {
        std::vector<std::string> activeLines;
        for (const auto& status : statuses)
        {
            if (!status.active)
            {
                continue;
            }
            activeLines.push_back(fmt::format("{}×{}", status.name, status.count));
        }

        if (activeLines.empty())
        {
            return KongLie3D::U8Text(u8"已激活：无");
        }

        std::string joined;
        for (size_t index = 0; index < activeLines.size(); ++index)
        {
            if (index > 0)
            {
                joined += KongLie3D::U8Text(u8"、");
            }
            joined += activeLines[index];
        }
        return fmt::format(fmt::runtime(KongLie3D::U8Text(u8"已激活：{}")), joined);
    }

    size_t GetPlayerHeroCount(const KongLie3DGameInstance& gameInstance)
    {
        size_t heroCount = 0;
        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (piece.def.isHero && piece.def.team == "player")
            {
                ++heroCount;
            }
        }
        return heroCount;
    }

    float GetBottomHeroBarTopY(const ImGuiViewport& viewport)
    {
        return viewport.Pos.y + viewport.Size.y - BottomHeroBarHeight - BottomHeroBarMargin;
    }

    float GetBottomHeroBarRightReserve(KongLie3D::EBattleState /*state*/)
    {
        return BottomHeroBarMargin;
    }

    float GetRightInfoPanelWidth()
    {
        return KongLie3D::ScaleUi(190.0f);
    }

    float GetRightInfoPanelX(const ImGuiViewport& viewport)
    {
        return viewport.Pos.x + viewport.Size.x - KongLie3D::ScaleUi(200.0f);
    }

    float GetRightInfoPanelY(const ImGuiViewport& viewport, KongLie3D::EBattleState state)
    {
        const float controlBottom =
            viewport.Pos.y +
            KongLie3D::ScaleUi(state == KongLie3D::EBattleState::Deployment ? 254.0f
                                                                            : (state == KongLie3D::EBattleState::Battle ? 178.0f : 126.0f));
        return controlBottom + LeftColumnGap;
    }

    float GetRightInfoPanelHeight(const ImGuiViewport& viewport, KongLie3D::EBattleState state)
    {
        const float panelBottom = GetBottomHeroBarTopY(viewport) - LeftColumnGap;
        return std::max(KongLie3D::ScaleUi(220.0f), panelBottom - GetRightInfoPanelY(viewport, state));
    }

    float GetRightInfoPanelReserveWidth()
    {
        return GetRightInfoPanelWidth() + KongLie3D::ScaleUi(22.0f);
    }

    std::string BuildStatsRowLine(const FStatsRow& row)
    {
        return fmt::format("{}  物{} 法{} 承{} 治{}", row.name, row.damageAD, row.damageAP, row.damageTaken, row.healing);
    }

    float Ui(float value)
    {
        return KongLie3D::ScaleUi(value);
    }

    ImVec2 Ui(float x, float y)
    {
        return KongLie3D::ScaleUi(x, y);
    }

    void DrawBattleStartBanner(const KongLie3DGameInstance& gameInstance);
    void DrawResultMetricRow(const char* label, int playerValue, int enemyValue);
    void DrawResultStatsTable(const FStatsSummary& playerSummary, const FStatsSummary& enemySummary);

    bool ProjectWorldToScreen(const KongLie3DGameInstance& gameInstance, const glm::vec3& worldPos, ImVec2& screenPos)
    {
        Assets::Camera camera{};
        if (!gameInstance.OverrideRenderCamera(camera))
        {
            return false;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
        {
            return false;
        }

        const float aspect = viewport->Size.x / viewport->Size.y;
        const float fov = camera.FieldOfView > 1.0f ? camera.FieldOfView : 60.0f;
        const glm::mat4 projection =
            glm::perspective(glm::radians(fov), aspect, std::max(0.05f, camera.NearPlane), camera.FarPlane);
        const glm::mat4 viewProjection = projection * camera.ModelView;
        const glm::vec4 clip = viewProjection * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < -1.0f || ndc.z > 1.0f || ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
        {
            return false;
        }

        screenPos.x = viewport->Pos.x + (ndc.x * 0.5f + 0.5f) * viewport->Size.x;
        screenPos.y = viewport->Pos.y + (-ndc.y * 0.5f + 0.5f) * viewport->Size.y;
        return true;
    }

    float GetCellWorldZ(int row)
    {
        return row == BenchRow ? BenchWorldZ : static_cast<float>(row);
    }

    bool ProjectWorldCircleRadius(const KongLie3DGameInstance& gameInstance,
                                  const glm::vec3& centerWorld,
                                  float radiusWorld,
                                  ImVec2& outCenter,
                                  float& outRadius)
    {
        ImVec2 centerScreen{};
        ImVec2 edgeScreen{};
        if (!ProjectWorldToScreen(gameInstance, centerWorld, centerScreen) ||
            !ProjectWorldToScreen(gameInstance, centerWorld + glm::vec3(radiusWorld, 0.0f, 0.0f), edgeScreen))
        {
            return false;
        }

        outCenter = centerScreen;
        outRadius = std::max(2.0f, std::abs(edgeScreen.x - centerScreen.x));
        return true;
    }

    bool ProjectGroundRing(const KongLie3DGameInstance& gameInstance,
                           const glm::vec3& centerWorld,
                           float radiusWorld,
                           int segments,
                           std::vector<ImVec2>& outPoints)
    {
        outPoints.clear();
        outPoints.reserve(static_cast<size_t>(segments));
        for (int index = 0; index < segments; ++index)
        {
            const float angle = glm::two_pi<float>() * (static_cast<float>(index) / static_cast<float>(segments));
            const glm::vec3 pointWorld = centerWorld + glm::vec3(std::cos(angle) * radiusWorld, 0.0f, std::sin(angle) * radiusWorld);
            ImVec2 pointScreen{};
            if (!ProjectWorldToScreen(gameInstance, pointWorld, pointScreen))
            {
                outPoints.clear();
                return false;
            }
            outPoints.push_back(pointScreen);
        }

        return outPoints.size() >= 3;
    }

    bool ProjectGroundQuad(const KongLie3DGameInstance& gameInstance,
                           const std::array<glm::vec3, 4>& corners,
                           std::array<ImVec2, 4>& outScreenCorners)
    {
        for (size_t index = 0; index < corners.size(); ++index)
        {
            if (!ProjectWorldToScreen(gameInstance, corners[index], outScreenCorners[index]))
            {
                return false;
            }
        }
        return true;
    }

    std::string FormatPercentBonus(std::string_view label, float bonus)
    {
        return fmt::format("{} +{}%", label, static_cast<int>(std::lround(bonus * 100.0f)));
    }

    std::string FormatSynergyBonus(const KongLie3D::FSynergyTier& tier)
    {
        std::vector<std::string> bonusParts;
        if (tier.atkBonus > 0.0f)
        {
            bonusParts.push_back(FormatPercentBonus("攻击", tier.atkBonus));
        }
        if (tier.hpBonus > 0.0f)
        {
            bonusParts.push_back(FormatPercentBonus("生命", tier.hpBonus));
        }
        if (tier.spdBonus > 0.0f)
        {
            bonusParts.push_back(FormatPercentBonus("攻速", tier.spdBonus));
        }
        if (tier.apBonus > 0.0f)
        {
            bonusParts.push_back(FormatPercentBonus("法强", tier.apBonus));
        }

        if (bonusParts.empty())
        {
            return "无加成";
        }

        std::string joinedBonuses;
        for (size_t index = 0; index < bonusParts.size(); ++index)
        {
            if (index > 0)
            {
                joinedBonuses += " / ";
            }
            joinedBonuses += bonusParts[index];
        }
        return joinedBonuses;
    }

    bool ButtonWithClick(const char* label, const ImVec2& size)
    {
        if (!ImGui::Button(label, size))
        {
            return false;
        }

        KongLie3D::PlayUiClickSfx();
        return true;
    }

    void DrawRelicSummary(const KongLie3D::FRelicDef* relic, const char* emptyText)
    {
        if (!relic)
        {
            ImGui::TextDisabled("%s", emptyText);
            return;
        }

        ImGui::TextColored(ToImVec4(relic->color), "%s %s", relic->icon.c_str(), relic->name.c_str());
        if (relic->buffs.empty())
        {
            ImGui::TextDisabled("%s", KongLie3D::U8Text(u8"本局暂无额外效果说明"));
            return;
        }

        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& buff : relic->buffs)
        {
            ImGui::TextWrapped("  %s", buff.c_str());
        }
        ImGui::PopTextWrapPos();
    }

    void DrawRelicSelectionList(KongLie3DGameInstance& gameInstance, const char* childId)
    {
        auto& battleSystem = gameInstance.GetBattleSystem();
        const auto& relics = battleSystem.GetRelics();
        const KongLie3D::FRelicDef* selectedRelic = battleSystem.GetSelectedRelic();
        if (relics.empty())
        {
            ImGui::TextDisabled("%s", KongLie3D::U8Text(u8"暂无可选圣物"));
            return;
        }

        if (ImGui::BeginChild(childId, ImVec2(0.0f, 0.0f), false))
        {
            for (const auto& relic : relics)
            {
                ImGui::PushID(relic.id.c_str());
                const bool isSelected = selectedRelic && selectedRelic->id == relic.id;
                const ImVec4 tint = ToImVec4(relic.color, isSelected ? 0.92f : 0.72f);
                const ImVec4 hovered = ToImVec4(relic.color, 1.0f);
                const ImVec4 active = ToImVec4(relic.color * 0.85f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, tint);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
                if (ButtonWithClick(fmt::format("{} {}", relic.icon, relic.name).c_str(), ImVec2(-1.0f, 0.0f)))
                {
                    gameInstance.SelectRelic(relic.id);
                    selectedRelic = battleSystem.GetSelectedRelic();
                }
                ImGui::PopStyleColor(3);

                ImGui::PushTextWrapPos(0.0f);
                for (const std::string& buff : relic.buffs)
                {
                    ImGui::TextWrapped("  %s", buff.c_str());
                }
                ImGui::PopTextWrapPos();

                if (selectedRelic && selectedRelic->id == relic.id)
                {
                    ImGui::TextColored(KongLie3D::Style::Highlight, "%s", KongLie3D::U8Text(u8"已携带"));
                }
                if (&relic != &relics.back())
                {
                    ImGui::Separator();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    void RefreshStatsCache(const KongLie3DGameInstance& gameInstance, FStatsCache& cache)
    {
        cache.playerRows.clear();
        cache.enemyRows.clear();

        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (piece.onBench)
            {
                continue;
            }

            FStatsRow row{};
            row.pieceId = piece.pieceId;
            row.name = piece.def.name;
            row.damageAD = piece.statDmgAD;
            row.damageAP = piece.statDmgAP;
            row.damageTaken = piece.statDmgTaken;
            row.healing = piece.statHeal;
            row.alive = piece.alive;

            auto& rows = piece.def.team == "enemy" ? cache.enemyRows : cache.playerRows;
            rows.push_back(row);
        }

        auto sortRows = [](std::vector<FStatsRow>& rows)
        {
            std::sort(rows.begin(), rows.end(), [](const FStatsRow& lhs, const FStatsRow& rhs)
            {
                const int lhsTotalDamage = lhs.damageAD + lhs.damageAP;
                const int rhsTotalDamage = rhs.damageAD + rhs.damageAP;
                if (lhsTotalDamage != rhsTotalDamage)
                {
                    return lhsTotalDamage > rhsTotalDamage;
                }
                return lhs.name < rhs.name;
            });
        };

        sortRows(cache.playerRows);
        sortRows(cache.enemyRows);
        cache.lastRefreshTime = ImGui::GetTime();
    }

    void DrawUnitHealthBars(const KongLie3DGameInstance& gameInstance)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (!piece.alive || piece.onBench || !piece.node || piece.def.hp <= 0)
            {
                continue;
            }

            ImVec2 screenPos{};
            const float headOffset = piece.dimensions.y * piece.visualScale + 0.28f;
            if (!ProjectWorldToScreen(gameInstance, piece.node->WorldTranslation() + glm::vec3(0.0f, headOffset, 0.0f), screenPos))
            {
                continue;
            }

            const float healthRatio = std::clamp(static_cast<float>(piece.currentHp) / static_cast<float>(piece.def.hp), 0.0f, 1.0f);
            const float width = Ui(38.0f);
            const float height = Ui(5.0f);
            const ImVec2 min(screenPos.x - width * 0.5f, screenPos.y - height * 0.5f);
            const ImVec2 max(screenPos.x + width * 0.5f, screenPos.y + height * 0.5f);
            const ImVec2 fillMax(min.x + width * healthRatio, max.y);

            drawList->AddRectFilled(min, max, IM_COL32(18, 18, 18, 180), Ui(2.0f));
            drawList->AddRectFilled(min, fillMax, ImGui::ColorConvertFloat4ToU32(GetHealthColor(healthRatio)), Ui(2.0f));
            drawList->AddRect(min, max, IM_COL32(255, 255, 255, 120), Ui(2.0f));
        }
    }

    void DrawAttackTraces(const KongLie3DGameInstance& gameInstance)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (const auto& attackTrace : gameInstance.GetBattleSystem().GetAttackTraces())
        {
            ImVec2 from{};
            ImVec2 to{};
            if (!ProjectWorldToScreen(gameInstance, attackTrace.from, from) ||
                !ProjectWorldToScreen(gameInstance, attackTrace.to, to))
            {
                continue;
            }

            const float alpha = std::clamp(attackTrace.remainingMs / 100.0f, 0.0f, 1.0f);
            drawList->AddLine(from,
                              to,
                              ImGui::ColorConvertFloat4ToU32(ImVec4(attackTrace.color.r, attackTrace.color.g, attackTrace.color.b, alpha)),
                              3.0f);
        }
    }

    void DrawDamagePopups(const KongLie3DGameInstance& gameInstance)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (const auto& popup : gameInstance.GetBattleSystem().GetDamagePopups())
        {
            ImVec2 screenPos{};
            if (!ProjectWorldToScreen(gameInstance, popup.worldPos, screenPos))
            {
                continue;
            }

            const float progress = 1.0f - std::clamp(popup.remainingMs / std::max(popup.durationMs, 1.0f), 0.0f, 1.0f);
            screenPos.y -= Ui(22.0f) * progress;
            drawList->AddText(ImVec2(screenPos.x - Ui(10.0f), screenPos.y),
                              ImGui::ColorConvertFloat4ToU32(ImVec4(popup.color.r,
                                                                    popup.color.g,
                                                                    popup.color.b,
                                                                    1.0f - progress * 0.8f)),
                              popup.text.c_str());
        }
    }

    void DrawSkillEffects(const KongLie3DGameInstance& gameInstance)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (!piece.alive || piece.onBench || !piece.node)
            {
                continue;
            }

            ImVec2 center{};
            float radius = 0.0f;
            const glm::vec3 auraCenter = piece.node->WorldTranslation() + glm::vec3(0.0f, piece.dimensions.y * 0.55f, 0.0f);
            if (piece.shieldTimerMs > 0.0f &&
                ProjectWorldCircleRadius(gameInstance, auraCenter, 0.55f * piece.visualScale, center, radius))
            {
                drawList->AddCircle(center, radius, IM_COL32(70, 140, 255, 220), 48, 2.0f);
            }

            if (piece.furyTimerMs > 0.0f &&
                ProjectWorldCircleRadius(gameInstance, auraCenter, 0.60f * piece.visualScale, center, radius))
            {
                const float pulse = 0.85f + 0.15f * std::sin(static_cast<float>(ImGui::GetTime() * 12.0));
                drawList->AddCircle(center, radius * pulse, IM_COL32(255, 155, 55, 220), 48, 2.0f);
                drawList->AddCircle(center, radius * (pulse + 0.18f), IM_COL32(255, 120, 45, 120), 48, 1.5f);
            }
        }

        for (const auto& effect : gameInstance.GetBattleSystem().GetSkillEffects())
        {
            const float progress = 1.0f - std::clamp(effect.remainingMs / std::max(effect.durationMs, 1.0f), 0.0f, 1.0f);
            if (effect.type == KongLie3D::ESkillEffectType::Beam)
            {
                ImVec2 from{};
                ImVec2 to{};
                if (ProjectWorldToScreen(gameInstance, effect.from, from) &&
                    ProjectWorldToScreen(gameInstance, effect.to, to))
                {
                    drawList->AddLine(from, to, ImGui::ColorConvertFloat4ToU32(ImVec4(effect.color.r, effect.color.g, effect.color.b, 1.0f)),
                                      5.0f);
                }
            }
            else if (effect.type == KongLie3D::ESkillEffectType::ExpandingRing)
            {
                ImVec2 center{};
                float radius = 0.0f;
                if (ProjectWorldCircleRadius(gameInstance, effect.center, effect.maxRadiusCells * progress, center, radius))
                {
                    drawList->AddCircle(center, radius, ImGui::ColorConvertFloat4ToU32(ToImVec4(glm::vec3(effect.color), 1.0f)), 64,
                                        3.0f);
                }
            }
        }
    }

    void DrawDragHighlights(const KongLie3DGameInstance& gameInstance)
    {
        const KongLie3D::FPieceRuntime* draggingPiece = gameInstance.GetDraggingPiece();
        if (!draggingPiece)
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (const glm::ivec2& cell : gameInstance.GetValidDragCells())
        {
            const float centerZ = GetCellWorldZ(cell.y);
            const std::array<glm::vec3, 4> corners = {
                glm::vec3(static_cast<float>(cell.x) - 0.47f, 0.03f, centerZ - 0.47f),
                glm::vec3(static_cast<float>(cell.x) + 0.47f, 0.03f, centerZ - 0.47f),
                glm::vec3(static_cast<float>(cell.x) + 0.47f, 0.03f, centerZ + 0.47f),
                glm::vec3(static_cast<float>(cell.x) - 0.47f, 0.03f, centerZ + 0.47f),
            };

            std::array<ImVec2, 4> screenCorners{};
            if (!ProjectGroundQuad(gameInstance, corners, screenCorners))
            {
                continue;
            }

            drawList->AddConvexPolyFilled(screenCorners.data(), static_cast<int>(screenCorners.size()), IM_COL32(255, 255, 255, 36));
            drawList->AddPolyline(screenCorners.data(), static_cast<int>(screenCorners.size()), IM_COL32(255, 255, 255, 132),
                                  ImDrawFlags_Closed, 1.5f);
        }

        glm::ivec2 invalidCell{};
        const bool invalidHover = gameInstance.GetInvalidDragHoverCell(invalidCell);
        if (draggingPiece->node)
        {
            const glm::vec3 piecePos = draggingPiece->node->Translation();
            const glm::vec3 groundCenter(piecePos.x, 0.03f, piecePos.z);
            std::vector<ImVec2> outerRing;
            std::vector<ImVec2> innerRing;
            if (ProjectGroundRing(gameInstance, groundCenter, 0.36f, 40, outerRing) &&
                ProjectGroundRing(gameInstance, groundCenter, 0.20f, 28, innerRing))
            {
                const ImU32 fillColor = invalidHover ? IM_COL32(255, 88, 88, 48) : IM_COL32(255, 222, 140, 60);
                const ImU32 ringColor = invalidHover ? IM_COL32(255, 110, 110, 220) : IM_COL32(255, 230, 160, 230);
                drawList->AddConvexPolyFilled(outerRing.data(), static_cast<int>(outerRing.size()), fillColor);
                drawList->AddPolyline(outerRing.data(), static_cast<int>(outerRing.size()), ringColor, ImDrawFlags_Closed, 2.5f);
                drawList->AddPolyline(innerRing.data(), static_cast<int>(innerRing.size()), ringColor, ImDrawFlags_Closed, 1.2f);
            }
        }

        if (invalidHover)
        {
            const float centerZ = GetCellWorldZ(invalidCell.y);
            const std::array<glm::vec3, 4> corners = {
                glm::vec3(static_cast<float>(invalidCell.x) - 0.47f, 0.03f, centerZ - 0.47f),
                glm::vec3(static_cast<float>(invalidCell.x) + 0.47f, 0.03f, centerZ - 0.47f),
                glm::vec3(static_cast<float>(invalidCell.x) + 0.47f, 0.03f, centerZ + 0.47f),
                glm::vec3(static_cast<float>(invalidCell.x) - 0.47f, 0.03f, centerZ + 0.47f),
            };

            std::array<ImVec2, 4> screenCorners{};
            if (ProjectGroundQuad(gameInstance, corners, screenCorners))
            {
                drawList->AddConvexPolyFilled(screenCorners.data(), static_cast<int>(screenCorners.size()), IM_COL32(255, 64, 64, 80));
                drawList->AddPolyline(screenCorners.data(),
                                      static_cast<int>(screenCorners.size()),
                                      IM_COL32(255, 96, 96, 200),
                                      ImDrawFlags_Closed,
                                      1.5f);
            }
        }
    }

    void DrawDeploymentZoneGuidance(const KongLie3DGameInstance& gameInstance)
    {
        if (gameInstance.GetBattleSystem().GetState() != KongLie3D::EBattleState::Deployment)
        {
            return;
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (int row = 0; row < 8; ++row)
        {
            const ImU32 fillColor = row < 4 ? IM_COL32(220, 88, 88, 30) : IM_COL32(80, 140, 220, 30);
            const float centerZ = static_cast<float>(row);
            for (int col = 0; col < 7; ++col)
            {
                const std::array<glm::vec3, 4> corners = {
                    glm::vec3(static_cast<float>(col) - 0.47f, 0.025f, centerZ - 0.47f),
                    glm::vec3(static_cast<float>(col) + 0.47f, 0.025f, centerZ - 0.47f),
                    glm::vec3(static_cast<float>(col) + 0.47f, 0.025f, centerZ + 0.47f),
                    glm::vec3(static_cast<float>(col) - 0.47f, 0.025f, centerZ + 0.47f),
                };
                std::array<ImVec2, 4> screenCorners{};
                if (!ProjectGroundQuad(gameInstance, corners, screenCorners))
                {
                    continue;
                }
                drawList->AddConvexPolyFilled(screenCorners.data(), static_cast<int>(screenCorners.size()), fillColor);
            }
        }
    }

    void DrawDeploymentHintOverlay(const KongLie3DGameInstance& gameInstance)
    {
        const float alpha = gameInstance.GetDeploymentHintAlpha();
        if (alpha <= 0.0f)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!viewport || !drawList)
        {
            return;
        }

        const char* primaryText = KongLie3D::U8Text(u8"拖拽棋子调整阵型，按 SPACE 开始战斗");
        const char* secondaryText = KongLie3D::U8Text(u8"按 F3 切换渲染管线，体验光追画质");
        ImFont* primaryFont = KongLie3D::KongLieFonts::Title ? KongLie3D::KongLieFonts::Title : ImGui::GetFont();
        ImFont* secondaryFont = KongLie3D::KongLieFonts::Body ? KongLie3D::KongLieFonts::Body : ImGui::GetFont();
        const float primarySize = primaryFont ? primaryFont->FontSize : 22.0f;
        const float secondarySize = secondaryFont ? secondaryFont->FontSize : 18.0f;
        const ImVec2 primaryTextSize = primaryFont->CalcTextSizeA(primarySize, FLT_MAX, 0.0f, primaryText);
        const ImVec2 secondaryTextSize = secondaryFont->CalcTextSizeA(secondarySize, FLT_MAX, 0.0f, secondaryText);
        const float centerX = viewport->Pos.x + viewport->Size.x * 0.5f;
        const float baseY = viewport->Pos.y + viewport->Size.y * 0.16f;

        const ImU32 primaryShadow = IM_COL32(8, 8, 12, static_cast<int>(alpha * 150.0f));
        const ImU32 primaryColor = IM_COL32(255, 255, 255, static_cast<int>(alpha * 200.0f));
        const ImU32 secondaryShadow = IM_COL32(8, 8, 12, static_cast<int>(alpha * 120.0f));
        const ImU32 secondaryColor = IM_COL32(210, 220, 240, static_cast<int>(alpha * 175.0f));

        const ImVec2 primaryPos(centerX - primaryTextSize.x * 0.5f, baseY);
        const ImVec2 secondaryPos(centerX - secondaryTextSize.x * 0.5f, baseY + primaryTextSize.y + Ui(8.0f));
        drawList->AddText(primaryFont, primarySize, ImVec2(primaryPos.x + Ui(2.0f), primaryPos.y + Ui(2.0f)), primaryShadow, primaryText);
        drawList->AddText(primaryFont, primarySize, primaryPos, primaryColor, primaryText);
        drawList->AddText(secondaryFont,
                          secondarySize,
                          ImVec2(secondaryPos.x + Ui(2.0f), secondaryPos.y + Ui(2.0f)),
                          secondaryShadow,
                          secondaryText);
        drawList->AddText(secondaryFont, secondarySize, secondaryPos, secondaryColor, secondaryText);
    }

    void DrawRendererIndicator(const KongLie3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
        {
            return;
        }

        ImGui::SetNextWindowBgAlpha(0.40f);
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - Ui(18.0f), viewport->Pos.y + viewport->Size.y - Ui(18.0f)),
                                ImGuiCond_Always,
                                ImVec2(1.0f, 1.0f));
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        if (!ImGui::Begin("##KongLie3DRendererIndicator", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(gameInstance.GetRendererLabel().c_str());
        if (gameInstance.GetBattleSystem().GetState() == KongLie3D::EBattleState::Deployment)
        {
            ImGui::TextDisabled("%s", KongLie3D::U8Text(u8"F3 切换渲染管线"));
        }
        ImGui::End();
    }

    void DrawHeroPanel(KongLie3DGameInstance& gameInstance)
    {
        auto& battleSystem = gameInstance.GetBattleSystem();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
        {
            return;
        }

        const size_t heroCount = GetPlayerHeroCount(gameInstance);
        if (heroCount == 0)
        {
            return;
        }

        const auto state = battleSystem.GetState();
        const float panelX = viewport->Pos.x + BottomHeroBarMargin;
        const float panelY = GetBottomHeroBarTopY(*viewport);
        const float panelWidth =
            std::max(Ui(360.0f), viewport->Size.x - BottomHeroBarMargin * 2.0f - GetBottomHeroBarRightReserve(state));

        ImGui::SetNextWindowPos(ImVec2(panelX, panelY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelWidth, BottomHeroBarHeight), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool showWindow = ImGui::Begin("##KongLie3DHeroes", nullptr, flags);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        if (!showWindow)
        {
            ImGui::End();
            return;
        }

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float totalGapWidth = BottomHeroCardGap * static_cast<float>(heroCount > 0 ? heroCount - 1 : 0);
        const float cardWidth =
            std::max(Ui(96.0f), std::min(BottomHeroCardWidthMax, (availableWidth - totalGapWidth) / static_cast<float>(heroCount)));
        const float totalCardsWidth = cardWidth * static_cast<float>(heroCount) + totalGapWidth;
        if (totalCardsWidth < availableWidth)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - totalCardsWidth) * 0.5f);
        }

        size_t renderedHeroCount = 0;
        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (!piece.def.isHero || piece.def.team != "player")
            {
                continue;
            }

            ImGui::PushID(piece.pieceId.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, piece.alive ? 1.0f : 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Ui(10.0f, 8.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Ui(6.0f, 5.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.08f, 0.14f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(piece.def.color, piece.alive ? 0.95f : 0.45f));
            if (ImGui::BeginChild("HeroCard###KongLie3DHeroCard",
                                  ImVec2(cardWidth, BottomHeroCardHeight),
                                  true,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImVec2 cardMin = ImGui::GetWindowPos();
                const ImVec2 cardMax(cardMin.x + ImGui::GetWindowSize().x, cardMin.y + ImGui::GetWindowSize().y);
                const ImU32 accent = ToImColor(ToImVec4(piece.def.color, 0.95f));
                drawList->AddRect(cardMin, cardMax, accent, Ui(10.0f), 0, Ui(1.2f));
                drawList->AddRectFilled(cardMin,
                                        ImVec2(cardMax.x, cardMin.y + Ui(34.0f)),
                                        ToImColor(ToImVec4(piece.def.color, 0.18f)),
                                        Ui(10.0f),
                                        ImDrawFlags_RoundCornersTop);

                ImGui::TextColored(piece.alive ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : KongLie3D::Style::TextDim, "%s", piece.def.name.c_str());
                ImGui::TextDisabled("%s", GetRoleLabel(piece.def.role));
                ImGui::Separator();

                const float hpRatio =
                    piece.def.hp > 0 ? std::clamp(static_cast<float>(piece.currentHp) / static_cast<float>(piece.def.hp), 0.0f, 1.0f) : 0.0f;
                const std::string hpLabel = fmt::format("{} / {}", piece.currentHp, piece.def.hp);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GetHealthColor(hpRatio));
                ImGui::ProgressBar(hpRatio, ImVec2(-1.0f, Ui(18.0f)), hpLabel.c_str());
                ImGui::PopStyleColor();

                const float manaRatio =
                    piece.def.maxMana > 0 ? std::clamp(static_cast<float>(piece.currentMana) / static_cast<float>(piece.def.maxMana), 0.0f, 1.0f)
                                          : 0.0f;
                const std::string manaLabel = fmt::format("{} / {}", piece.currentMana, piece.def.maxMana);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, KongLie3D::Style::Accent);
                ImGui::ProgressBar(manaRatio, ImVec2(-1.0f, Ui(18.0f)), manaLabel.c_str());
                ImGui::PopStyleColor();

                const std::string wName = piece.def.skillWName.empty() ? piece.def.skillW : piece.def.skillWName;
                ImGui::Spacing();
                ImGui::TextDisabled(KongLie3D::U8Text(u8"W·%s"), wName.empty() ? "-" : wName.c_str());
                if (ImGui::IsItemHovered() && !piece.def.skillWDesc.empty())
                {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.0f);
                    ImGui::TextWrapped("%s", piece.def.skillWDesc.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }

                const float buttonY = ImGui::GetWindowHeight() - Ui(44.0f);
                if (ImGui::GetCursorPosY() < buttonY)
                {
                    ImGui::SetCursorPosY(buttonY);
                }

                if (!piece.alive)
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        ImVec4(KongLie3D::Style::Hostile.x, KongLie3D::Style::Hostile.y, KongLie3D::Style::Hostile.z, 0.55f));
                    ImGui::Button(KongLie3D::U8Text(u8"已阵亡"), ImVec2(-1.0f, 0.0f));
                    ImGui::PopStyleColor();
                }
                else if (piece.ultimateUsed)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                    ImGui::Button(KongLie3D::U8Text(u8"✓ 已释放"), ImVec2(-1.0f, 0.0f));
                    ImGui::PopStyleColor();
                }
                else
                {
                    const bool ready = piece.def.maxMana > 0 && piece.currentMana >= piece.def.maxMana &&
                                       piece.ultimateCooldownMs <= 0.0f && !piece.onBench;
                    if (ready)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, KongLie3D::Style::Highlight);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.88f, 0.42f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.89f, 0.74f, 0.24f, 1.0f));
                        if (ButtonWithClick(KongLie3D::U8Text(u8"R 终极技就绪"), ImVec2(-1.0f, Ui(32.0f))))
                        {
                            battleSystem.RequestUltimate(piece.pieceId);
                        }
                        ImGui::PopStyleColor(3);
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.20f, 0.32f, 0.95f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.24f, 0.38f, 0.95f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.16f, 0.28f, 0.95f));
                        ImGui::BeginDisabled();
                        ImGui::Button(KongLie3D::U8Text(u8"R 蓄力中"), ImVec2(-1.0f, Ui(32.0f)));
                        ImGui::EndDisabled();
                        ImGui::PopStyleColor(3);
                    }

                    if (ImGui::IsItemHovered() && !piece.def.skillUltimateDesc.empty())
                    {
                        ImGui::BeginTooltip();
                        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.0f);
                        ImGui::TextWrapped("%s: %s", piece.def.skillUltimateName.c_str(), piece.def.skillUltimateDesc.c_str());
                        ImGui::PopTextWrapPos();
                        ImGui::EndTooltip();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleVar();
            ++renderedHeroCount;
            if (renderedHeroCount < heroCount)
            {
                ImGui::SameLine(0.0f, BottomHeroCardGap);
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    void DrawLeftInfoPanel(const KongLie3DGameInstance& gameInstance)
    {
        const auto& battleSystem = gameInstance.GetBattleSystem();
        const auto state = battleSystem.GetState();
        const bool deploymentState = state == KongLie3D::EBattleState::Deployment;
        const std::vector<KongLie3D::FSynergyStatus> displayedStatuses =
            deploymentState ? battleSystem.BuildSynergyPreview() : battleSystem.GetActiveSynergies();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
        {
            return;
        }

        const float panelY = GetRightInfoPanelY(*viewport, state);
        const float panelHeight = GetRightInfoPanelHeight(*viewport, state);
        ImGui::SetNextWindowPos(ImVec2(GetRightInfoPanelX(*viewport), panelY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(GetRightInfoPanelWidth(), panelHeight), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        KongLie3D::PushPanelStyle();
        const bool showWindow = ImGui::Begin(KongLie3D::U8Text(u8"战术卡###KongLie3DLeftInfo"), nullptr, flags);
        KongLie3D::PopPanelStyle();
        if (!showWindow)
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(deploymentState ? KongLie3D::U8Text(u8"已选圣物") : KongLie3D::U8Text(u8"圣物"));
        ImGui::Separator();
        const KongLie3D::FRelicDef* selectedRelic = battleSystem.GetSelectedRelic();
        DrawRelicSummary(selectedRelic,
                         deploymentState ? KongLie3D::U8Text(u8"请在左侧选择一件圣物") : KongLie3D::U8Text(u8"未携带圣物"));

        ImGui::Spacing();
        ImGui::TextUnformatted(deploymentState ? KongLie3D::U8Text(u8"羁绊预览") : KongLie3D::U8Text(u8"羁绊"));
        ImGui::Separator();
        if (displayedStatuses.empty())
        {
            ImGui::TextDisabled("%s", KongLie3D::U8Text(u8"当前阵容暂无羁绊"));
        }
        else
        {
            if (ImGui::BeginChild("SynergyList###KongLie3DLeftInfoList", ImVec2(0.0f, 0.0f), false))
            {
                for (const auto& status : displayedStatuses)
                {
                    const std::string line = status.active ? fmt::format("{} {} ×{}  {}",
                                                                         KongLie3D::U8Text(u8"✓"),
                                                                         status.name,
                                                                         status.count,
                                                                         FormatSynergyBonus(status.activeTier))
                                                           : fmt::format("{} ×{}/{}",
                                                                         status.name,
                                                                         status.count,
                                                                         status.nextTierCount > 0 ? status.nextTierCount : status.count);
                    ImGui::PushTextWrapPos(0.0f);
                    if (status.active)
                    {
                        ImGui::TextColored(KongLie3D::Style::Highlight, "%s", line.c_str());
                    }
                    else if (deploymentState && status.count > 0)
                    {
                        ImGui::Text("%s", line.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("%s", line.c_str());
                    }
                    ImGui::PopTextWrapPos();
                    if (&status != &displayedStatuses.back())
                    {
                        ImGui::Spacing();
                    }
                }
            }
            ImGui::EndChild();
        }

        ImGui::End();
    }

    void DrawBattleTimer(const KongLie3DGameInstance& gameInstance)
    {
        const auto& battleSystem = gameInstance.GetBattleSystem();
        const auto state = battleSystem.GetState();
        if (state == KongLie3D::EBattleState::Ended)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!viewport || !drawList)
        {
            return;
        }

        const ImVec2 cardSize = Ui(280.0f, 52.0f);
        const ImVec2 cardMin(viewport->Pos.x + (viewport->Size.x - cardSize.x) * 0.5f, viewport->Pos.y + Ui(12.0f));
        const ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);

        ImVec4 backgroundColor = KongLie3D::Style::SurfaceAlt;
        ImVec4 borderColor = KongLie3D::Style::Border;
        if (battleSystem.IsOvertimeActive())
        {
            const float pulse = 0.75f + 0.25f * (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime() * 7.0)));
            backgroundColor = ImVec4(0.24f, 0.11f, 0.12f, 0.95f);
            borderColor = ImVec4(KongLie3D::Style::Hostile.x, KongLie3D::Style::Hostile.y, KongLie3D::Style::Hostile.z, pulse);
        }

        drawList->AddRectFilled(ImVec2(cardMin.x + Ui(2.0f), cardMin.y + Ui(6.0f)),
                                ImVec2(cardMax.x + Ui(2.0f), cardMax.y + Ui(8.0f)),
                                IM_COL32(0, 0, 0, 85),
                                Ui(14.0f));
        drawList->AddRectFilled(cardMin, cardMax, ToImColor(backgroundColor), Ui(14.0f));
        drawList->AddRect(cardMin, cardMax, ToImColor(borderColor), Ui(14.0f), 0, Ui(1.8f));

        const ImVec2 iconCenter(cardMin.x + Ui(34.0f), cardMin.y + cardSize.y * 0.5f);
        drawList->AddCircle(iconCenter, Ui(10.0f), ToImColor(KongLie3D::Style::TextDim), 24, Ui(1.8f));
        drawList->AddLine(iconCenter, ImVec2(iconCenter.x, iconCenter.y - Ui(5.0f)), ToImColor(KongLie3D::Style::TextDim), Ui(1.6f));
        drawList->AddLine(iconCenter, ImVec2(iconCenter.x + Ui(4.0f), iconCenter.y), ToImColor(KongLie3D::Style::TextDim), Ui(1.6f));

        ImFont* bodyFont = KongLie3D::KongLieFonts::Body ? KongLie3D::KongLieFonts::Body : ImGui::GetFont();
        ImFont* titleFont = KongLie3D::KongLieFonts::Title ? KongLie3D::KongLieFonts::Title : ImGui::GetFont();
        const float bodyFontSize = bodyFont ? bodyFont->FontSize : 18.0f;
        const float titleFontSize = titleFont ? titleFont->FontSize : 32.0f;

        if (state == KongLie3D::EBattleState::Deployment)
        {
            const char* prefix = KongLie3D::U8Text(u8"准备就绪 · 按 ");
            const char* key = "SPACE";
            const char* suffix = KongLie3D::U8Text(u8" 开战");
            const ImVec2 prefixSize = bodyFont->CalcTextSizeA(bodyFontSize, FLT_MAX, 0.0f, prefix);
            const ImVec2 keySize = bodyFont->CalcTextSizeA(bodyFontSize, FLT_MAX, 0.0f, key);
            const ImVec2 suffixSize = bodyFont->CalcTextSizeA(bodyFontSize, FLT_MAX, 0.0f, suffix);
            const float keyPaddingX = Ui(8.0f);
            const float totalWidth = prefixSize.x + keySize.x + suffixSize.x + keyPaddingX * 2.0f + Ui(10.0f);
            const float baseX = cardMin.x + Ui(68.0f) + std::max(0.0f, (cardSize.x - Ui(88.0f) - totalWidth) * 0.5f);
            const float textY = cardMin.y + (cardSize.y - bodyFontSize) * 0.5f - Ui(1.0f);
            drawList->AddText(bodyFont, bodyFontSize, ImVec2(baseX, textY), ToImColor(KongLie3D::Style::TextDim), prefix);

            const ImVec2 keyMin(baseX + prefixSize.x, cardMin.y + Ui(12.0f));
            const ImVec2 keyMax(keyMin.x + keySize.x + keyPaddingX * 2.0f, keyMin.y + Ui(26.0f));
            drawList->AddRectFilled(keyMin, keyMax, ToImColor(KongLie3D::Style::Highlight), Ui(6.0f));
            drawList->AddText(bodyFont, bodyFontSize, ImVec2(keyMin.x + keyPaddingX, textY), IM_COL32(20, 20, 28, 235), key);
            drawList->AddText(bodyFont, bodyFontSize, ImVec2(keyMax.x + Ui(10.0f), textY), ToImColor(KongLie3D::Style::TextDim), suffix);
            return;
        }

        if (battleSystem.IsOvertimeActive())
        {
            const float overtimeElapsed = std::max(0.0f, (battleSystem.GetElapsedMs() - battleSystem.GetOvertimeStartMs()) / 1000.0f);
            const std::string overtimeText = fmt::format("+{:.1f}s", overtimeElapsed);
            drawList->AddText(bodyFont,
                              bodyFontSize,
                              ImVec2(cardMin.x + Ui(70.0f), cardMin.y + Ui(7.0f)),
                              ToImColor(KongLie3D::Style::TextDim),
                              KongLie3D::U8Text(u8"加时"));
            const ImVec2 overtimeSize = titleFont->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, overtimeText.c_str());
            drawList->AddText(titleFont,
                              titleFontSize,
                              ImVec2(cardMax.x - overtimeSize.x - Ui(16.0f), cardMin.y + Ui(10.0f)),
                              ToImColor(KongLie3D::Style::Hostile),
                              overtimeText.c_str());
            return;
        }

        const float remaining = std::max(0.0f, BattleDurationSeconds - battleSystem.GetElapsedMs() / 1000.0f);
        const std::string timeText = FormatBattleClock(remaining);
        float timeAlpha = 1.0f;
        if (remaining <= 5.0f)
        {
            timeAlpha = 0.8f + 0.2f * std::sin(static_cast<float>(ImGui::GetTime()) * glm::pi<float>());
        }

        drawList->AddText(bodyFont,
                          bodyFontSize,
                           ImVec2(cardMin.x + Ui(70.0f), cardMin.y + Ui(7.0f)),
                          ToImColor(KongLie3D::Style::TextDim),
                          KongLie3D::U8Text(u8"战斗倒计时"));
        const ImVec2 timeSize = titleFont->CalcTextSizeA(titleFontSize, FLT_MAX, 0.0f, timeText.c_str());
        drawList->AddText(titleFont,
                          titleFontSize,
                           ImVec2(cardMax.x - timeSize.x - Ui(16.0f), cardMin.y + Ui(10.0f)),
                          ToImColor(ImVec4(0.98f, 0.99f, 1.0f, timeAlpha)),
                          timeText.c_str());
    }

    void DrawSideControlPanel(KongLie3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
        {
            return;
        }

        const FAliveCounts counts = CountAlivePieces(gameInstance);
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - Ui(200.0f), viewport->Pos.y + Ui(8.0f)), ImGuiCond_Always);
        ImGui::SetNextWindowSize(Ui(190.0f, 36.0f), ImGuiCond_Always);
        constexpr ImGuiWindowFlags statusFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        KongLie3D::PushPanelStyle();
        const bool showStatus = ImGui::Begin("##KongLie3DTopStatusStrip", nullptr, statusFlags);
        KongLie3D::PopPanelStyle();
        if (showStatus)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 playerBadgePos = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(playerBadgePos,
                                    ImVec2(playerBadgePos.x + Ui(12.0f), playerBadgePos.y + Ui(12.0f)),
                                    ToImColor(KongLie3D::Style::Accent),
                                    Ui(3.0f));
            ImGui::Dummy(Ui(12.0f, 12.0f));
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text(KongLie3D::U8Text(u8"我方 %d/6"), counts.player);
            ImGui::SameLine(0.0f, Ui(12.0f));
            ImGui::TextDisabled("%s", "|");
            ImGui::SameLine(0.0f, Ui(12.0f));
            const ImVec2 enemyBadgePos = ImGui::GetCursorScreenPos();
            drawList->AddRectFilled(enemyBadgePos,
                                    ImVec2(enemyBadgePos.x + Ui(12.0f), enemyBadgePos.y + Ui(12.0f)),
                                    ToImColor(KongLie3D::Style::Hostile),
                                    Ui(3.0f));
            ImGui::Dummy(Ui(12.0f, 12.0f));
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::Text(KongLie3D::U8Text(u8"敌方 %d/6"), counts.enemy);
        }
        ImGui::End();

        auto& battleSystem = gameInstance.GetBattleSystem();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - Ui(200.0f), viewport->Pos.y + Ui(52.0f)), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(Ui(190.0f), 0.0f), ImVec2(Ui(190.0f), FLT_MAX));
        constexpr ImGuiWindowFlags controlFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
        KongLie3D::PushPanelStyle();
        const bool showControl = ImGui::Begin("##KongLie3DSidePhaseControls", nullptr, controlFlags);
        KongLie3D::PopPanelStyle();
        if (!showControl)
        {
            ImGui::End();
            return;
        }

        const auto drawThreeButtons = [&](const std::array<const char*, 3>& labels,
                                          const std::array<bool, 3>& selectedStates,
                                          const std::function<void(size_t)>& onClick)
        {
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float buttonWidth = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
            for (size_t index = 0; index < labels.size(); ++index)
            {
                if (selectedStates[index])
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, KongLie3D::Style::Highlight);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.88f, 0.42f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.89f, 0.74f, 0.24f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Border, KongLie3D::Style::Highlight);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                }

                if (ButtonWithClick(labels[index], ImVec2(buttonWidth, Ui(28.0f))))
                {
                    onClick(index);
                }

                if (selectedStates[index])
                {
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(4);
                }

                if (index + 1 < labels.size())
                {
                    ImGui::SameLine();
                }
            }
        };

        if (battleSystem.GetState() == KongLie3D::EBattleState::Deployment)
        {
            ImGui::TextUnformatted(KongLie3D::U8Text(u8"难度"));
            const bool pushedTitleFont = PushFontIfAvailable(KongLie3D::KongLieFonts::Title);
            if (const auto* currentLevel = gameInstance.GetCurrentLevel())
            {
                ImGui::TextColored(KongLie3D::Style::Accent, "%s", currentLevel->name.c_str());
            }
            PopFontIfPushed(pushedTitleFont);

            const auto& levels = gameInstance.GetLevels();
            std::array<const char*, 3> labels = {"I", "II", "III"};
            std::array<bool, 3> selectedStates = {false, false, false};
            for (size_t index = 0; index < std::min<size_t>(3, levels.size()); ++index)
            {
                labels[index] = levels[index].name.c_str();
                selectedStates[index] = index == gameInstance.GetCurrentLevelIndex();
            }
            drawThreeButtons(labels, selectedStates, [&](size_t index)
            {
                if (index < levels.size())
                {
                    gameInstance.SelectLevel(index);
                }
            });

            ImGui::Separator();
            if (ButtonWithClick(KongLie3D::U8Text(u8"开始战斗"), ImVec2(-1.0f, Ui(36.0f))))
            {
                gameInstance.StartBattle();
            }
        }
        else if (battleSystem.GetState() == KongLie3D::EBattleState::Battle)
        {
            if (ButtonWithClick(battleSystem.IsPaused() ? KongLie3D::U8Text(u8"继续") : KongLie3D::U8Text(u8"暂停"), ImVec2(-1.0f, Ui(36.0f))))
            {
                battleSystem.TogglePause();
            }

            ImGui::Separator();
            ImGui::TextUnformatted(KongLie3D::U8Text(u8"节奏"));
            const float currentSpeed = battleSystem.GetSpeedMultiplier();
            const std::array<const char*, 3> speedLabels = {KongLie3D::U8Text(u8"1x"),
                                                            KongLie3D::U8Text(u8"2x"),
                                                            KongLie3D::U8Text(u8"4x")};
            const std::array<bool, 3> speedStates = {currentSpeed < 1.5f,
                                                     currentSpeed >= 1.5f && currentSpeed < 3.0f,
                                                     currentSpeed >= 3.0f};
            drawThreeButtons(speedLabels,
                             speedStates,
                              [&](size_t index)
                              {
                                  static constexpr std::array<float, 3> SpeedValues = {1.0f, 2.0f, 4.0f};
                                  gameInstance.SetBattleSpeedMultiplier(SpeedValues[index]);
                              });
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::Button(KongLie3D::U8Text(u8"战斗结束"), ImVec2(-1.0f, Ui(36.0f)));
            ImGui::EndDisabled();
        }

        ImGui::End();
    }

    void DrawStatsTable(const char* tableId, const std::vector<FStatsRow>& rows)
    {
        if (!ImGui::BeginTable(tableId,
                               5,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                                   ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
        {
            return;
        }

        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"名"), ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"物"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"法"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"承"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"治"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableHeadersRow();

        for (const auto& row : rows)
        {
            ImGui::TableNextRow(ImGuiTableRowFlags_None, Ui(22.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, row.alive ? 1.0f : 0.45f);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", row.damageAD);
            ImGui::TableNextColumn();
            ImGui::Text("%d", row.damageAP);
            ImGui::TableNextColumn();
            ImGui::Text("%d", row.damageTaken);
            ImGui::TableNextColumn();
            ImGui::Text("%d", row.healing);

            ImGui::PopStyleVar();
        }

        ImGui::EndTable();
    }

    void DrawStatsListSection(const char* header, const ImVec4& headerColor, const char* childId, const std::vector<FStatsRow>& rows)
    {
        ImGui::TextColored(headerColor, "%s", header);
        DrawStatsTable((std::string(childId) + "Table").c_str(), rows);
    }

    void DrawStatsPanel(KongLie3DGameInstance& gameInstance)
    {
        static FStatsCache cache;
        const auto state = gameInstance.GetBattleSystem().GetState();
        const bool deploymentState = state == KongLie3D::EBattleState::Deployment;
        if (!deploymentState)
        {
            const double now = ImGui::GetTime();
            if (cache.lastRefreshTime < 0.0 || (now - cache.lastRefreshTime) >= StatsRefreshIntervalSeconds)
            {
                RefreshStatsCache(gameInstance, cache);
            }
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
        {
            return;
        }
        const float heroBarTopY = GetBottomHeroBarTopY(*viewport);
        const float statsPosY = LeftColumnY;
        const float statsBottomY = heroBarTopY - LeftColumnGap;
        const float statsWidth = LeftColumnWidth + Ui(36.0f);
        const float statsHeight = std::max(Ui(140.0f), statsBottomY - statsPosY);
        ImGui::SetNextWindowPos(ImVec2(LeftColumnX, statsPosY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(statsWidth, statsHeight), ImGuiCond_Always);

        const bool ended = state == KongLie3D::EBattleState::Ended;
        const ImVec4 endedBorder =
            gameInstance.GetBattleSystem().GetWinnerTeam() == "enemy" ? KongLie3D::Style::Hostile : KongLie3D::Style::Highlight;
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        KongLie3D::PushPanelStyle();
        if (ended)
        {
            ImGui::PushStyleColor(ImGuiCol_Border, endedBorder);
        }
        const bool showWindow =
            ImGui::Begin(deploymentState ? KongLie3D::U8Text(u8"圣物选择###KongLie3DStats") : KongLie3D::U8Text(u8"数据###KongLie3DStats"),
                         nullptr,
                         flags);
        if (ended)
        {
            ImGui::PopStyleColor();
        }
        KongLie3D::PopPanelStyle();
        if (!showWindow)
        {
            ImGui::End();
            return;
        }

        if (deploymentState)
        {
            ImGui::TextDisabled("%s", KongLie3D::U8Text(u8"准备阶段：在这里选择本局圣物"));
            ImGui::Separator();
            DrawRelicSelectionList(gameInstance, "##KongLie3DRelicSelectionList");
            ImGui::End();
            return;
        }

        const FStatsSummary playerSummary = BuildStatsSummary(cache.playerRows);
        const FStatsSummary enemySummary = BuildStatsSummary(cache.enemyRows);
        const std::string summaryLine = fmt::format(fmt::runtime(KongLie3D::U8Text(u8"我方 {} / {} · 敌方 {} / {}")),
                                                    playerSummary.damage,
                                                    playerSummary.taken,
                                                    enemySummary.damage,
                                                    enemySummary.taken);

        if (ended)
        {
            ImGui::TextColored(endedBorder, "%s", KongLie3D::U8Text(u8"战况详情"));
            ImGui::SameLine();
        }

        ImGui::TextDisabled("%s", summaryLine.c_str());
        ImGui::Separator();
        if (ImGui::BeginChild("##KongLie3DStatsSections", ImVec2(0.0f, 0.0f), false))
        {
            DrawStatsListSection(KongLie3D::U8Text(u8"我方"), KongLie3D::Style::Accent, "##KongLie3DPlayerStatsList", cache.playerRows);
            ImGui::Spacing();
            DrawStatsListSection(KongLie3D::U8Text(u8"敌方"), KongLie3D::Style::Hostile, "##KongLie3DEnemyStatsList", cache.enemyRows);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void DrawOvertimeOverlay(const KongLie3DGameInstance& gameInstance)
    {
        const auto& battleSystem = gameInstance.GetBattleSystem();
        if (!battleSystem.IsOvertimeActive())
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!viewport || !drawList)
        {
            return;
        }

        const float pulse = 0.35f + 0.25f * (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime() * 7.0)));
        const ImU32 borderColor = IM_COL32(255, 40, 32, static_cast<int>(pulse * 255.0f));
        drawList->AddRect(viewport->Pos,
                          ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                          borderColor,
                          0.0f,
                          0,
                          Ui(8.0f));

        const float bannerElapsedMs = battleSystem.GetElapsedMs() - battleSystem.GetOvertimeStartMs();
        if (bannerElapsedMs >= 0.0f && bannerElapsedMs <= OvertimeBannerDurationMs)
        {
            const float fade = bannerElapsedMs < 500.0f ? bannerElapsedMs / 500.0f
                                                        : std::clamp((OvertimeBannerDurationMs - bannerElapsedMs) / 1000.0f, 0.0f, 1.0f);
            const char* text = KongLie3D::U8Text(u8"⚡ 加 时 ⚡");
            ImFont* bannerFont = KongLie3D::KongLieFonts::Title ? KongLie3D::KongLieFonts::Title : ImGui::GetFont();
            const float bannerFontSize = bannerFont ? bannerFont->FontSize : ImGui::GetFontSize();
            const ImVec2 textSize = bannerFont->CalcTextSizeA(bannerFontSize, FLT_MAX, 0.0f, text);
            const ImVec2 textPos(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f,
                                 viewport->Pos.y + viewport->Size.y * 0.18f);
            drawList->AddText(bannerFont,
                              bannerFontSize,
                              textPos,
                              IM_COL32(255, 90, 65, static_cast<int>(fade * 255.0f)),
                              text);
        }
    }

    void DrawResultModal(KongLie3DGameInstance& gameInstance)
    {
        const auto& battleSystem = gameInstance.GetBattleSystem();
        if (battleSystem.GetState() != KongLie3D::EBattleState::Ended)
        {
            return;
        }

        ImGui::OpenPopup("KongLie3DResultModal");
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
                                ImGuiCond_Always,
                                ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(Ui(560.0f, 360.0f), ImGuiCond_Always);

        ImVec4 borderColor = KongLie3D::Style::Highlight;
        ImVec4 backgroundColor(0.06f, 0.09f, 0.14f, 0.96f);
        ImVec4 bannerTop(0.20f, 0.16f, 0.06f, 1.0f);
        ImVec4 bannerBottom(0.45f, 0.36f, 0.10f, 1.0f);
        const char* title = KongLie3D::U8Text(u8"胜利");

        int playerAlive = 0;
        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (piece.def.team == "player" && !piece.onBench && piece.alive)
            {
                ++playerAlive;
            }
        }

        const int totalSeconds = static_cast<int>(std::round(battleSystem.GetElapsedMs() / 1000.0f));
        const int minutes = totalSeconds / 60;
        const int seconds = totalSeconds % 60;
        const bool overtimeTriggered = battleSystem.GetOvertimeStartMs() > 0.0f;
        const float overtimeSeconds =
            overtimeTriggered ? std::max(0.0f, (battleSystem.GetElapsedMs() - battleSystem.GetOvertimeStartMs()) / 1000.0f) : 0.0f;

        FStatsCache statsCache;
        RefreshStatsCache(gameInstance, statsCache);
        const FStatsSummary playerSummary = BuildStatsSummary(statsCache.playerRows);
        const FStatsSummary enemySummary = BuildStatsSummary(statsCache.enemyRows);

        if (battleSystem.GetWinnerTeam() == "enemy")
        {
            borderColor = KongLie3D::Style::Hostile;
            backgroundColor = ImVec4(0.15f, 0.06f, 0.08f, 0.96f);
            bannerTop = ImVec4(0.22f, 0.06f, 0.06f, 1.0f);
            bannerBottom = ImVec4(0.48f, 0.12f, 0.10f, 1.0f);
            title = KongLie3D::U8Text(u8"失败");
        }
        else if (battleSystem.GetWinnerTeam() == "draw")
        {
            borderColor = ImVec4(0.93f, 0.80f, 0.28f, 1.0f);
            backgroundColor = ImVec4(0.18f, 0.14f, 0.05f, 0.96f);
            bannerTop = ImVec4(0.20f, 0.17f, 0.05f, 1.0f);
            bannerBottom = ImVec4(0.45f, 0.38f, 0.08f, 1.0f);
            title = KongLie3D::U8Text(u8"平局");
        }

        const float fade = std::clamp(gameInstance.GetResultModalAppearMs() / ResultModalFadeDurationMs, 0.0f, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Ui(14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, Ui(1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(backgroundColor.x, backgroundColor.y, backgroundColor.z, backgroundColor.w * fade));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(borderColor.x, borderColor.y, borderColor.z, 0.9f * fade));
        if (ImGui::BeginPopupModal("KongLie3DResultModal", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 bannerMin = ImGui::GetCursorScreenPos();
            const ImVec2 bannerMax(bannerMin.x + ImGui::GetContentRegionAvail().x, bannerMin.y + Ui(90.0f));
            const ImU32 topColor =
                ImGui::ColorConvertFloat4ToU32(ImVec4(bannerTop.x, bannerTop.y, bannerTop.z, bannerTop.w * fade));
            const ImU32 bottomColor =
                ImGui::ColorConvertFloat4ToU32(ImVec4(bannerBottom.x, bannerBottom.y, bannerBottom.z, bannerBottom.w * fade));
            drawList->AddRectFilledMultiColor(bannerMin, bannerMax, topColor, topColor, bottomColor, bottomColor);
            drawList->AddTriangleFilled(ImVec2(bannerMin.x + Ui(20.0f), bannerMin.y + Ui(45.0f)),
                                        ImVec2(bannerMin.x + Ui(42.0f), bannerMin.y + Ui(28.0f)),
                                        ImVec2(bannerMin.x + Ui(42.0f), bannerMin.y + Ui(62.0f)),
                                        ImGui::ColorConvertFloat4ToU32(ImVec4(borderColor.x, borderColor.y, borderColor.z, fade)));
            drawList->AddTriangleFilled(ImVec2(bannerMax.x - Ui(20.0f), bannerMin.y + Ui(45.0f)),
                                        ImVec2(bannerMax.x - Ui(42.0f), bannerMin.y + Ui(28.0f)),
                                        ImVec2(bannerMax.x - Ui(42.0f), bannerMin.y + Ui(62.0f)),
                                        ImGui::ColorConvertFloat4ToU32(ImVec4(borderColor.x, borderColor.y, borderColor.z, fade)));

            ImFont* bannerFont = KongLie3D::KongLieFonts::Display ? KongLie3D::KongLieFonts::Display : ImGui::GetFont();
            const float bannerFontSize = bannerFont ? bannerFont->FontSize : 56.0f;
            const ImVec2 titleSize = bannerFont->CalcTextSizeA(bannerFontSize, FLT_MAX, 0.0f, title);
            const ImVec2 titlePos(bannerMin.x + (bannerMax.x - bannerMin.x - titleSize.x) * 0.5f,
                                  bannerMin.y + (Ui(90.0f) - titleSize.y) * 0.5f - Ui(4.0f));
            drawList->AddText(bannerFont,
                              bannerFontSize,
                              ImVec2(titlePos.x + Ui(2.0f), titlePos.y + Ui(2.0f)),
                              IM_COL32(10, 10, 14, static_cast<int>(fade * 150.0f)),
                              title);
            drawList->AddText(bannerFont,
                              bannerFontSize,
                              titlePos,
                              ImGui::ColorConvertFloat4ToU32(ImVec4(borderColor.x, borderColor.y, borderColor.z, fade)),
                              title);
            ImGui::Dummy(ImVec2(0.0f, Ui(102.0f)));

            if (ImGui::BeginTable("##KongLie3DResultLayout", 2, ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, Ui(280.0f));
                ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::TextColored(borderColor, "%s", KongLie3D::U8Text(u8"本局摘要"));
                ImGui::Spacing();
                ImGui::Text(KongLie3D::U8Text(u8"本局耗时  %d:%02d"), minutes, seconds);
                if (battleSystem.GetWinnerTeam() == "enemy")
                {
                    ImGui::TextUnformatted(KongLie3D::U8Text(u8"全军覆没"));
                }
                else
                {
                    ImGui::Text(KongLie3D::U8Text(u8"存活单位  %d/6"), playerAlive);
                }
                if (const auto* relic = gameInstance.GetBattleSystem().GetSelectedRelic())
                {
                    ImGui::TextWrapped(KongLie3D::U8Text(u8"携带圣物：%s"), relic->name.c_str());
                }
                else
                {
                    ImGui::TextWrapped("%s", KongLie3D::U8Text(u8"携带圣物：未携带"));
                }
                if (const auto* currentLevel = gameInstance.GetCurrentLevel())
                {
                    ImGui::TextWrapped(KongLie3D::U8Text(u8"当前难度：%s"), currentLevel->name.c_str());
                }
                if (overtimeTriggered)
                {
                    ImGui::Text(KongLie3D::U8Text(u8"加时 %.1f 秒"), overtimeSeconds);
                }
                else
                {
                    ImGui::TextUnformatted(KongLie3D::U8Text(u8"本局未触发加时"));
                }

                ImGui::TableNextColumn();
                ImGui::TextColored(borderColor, "%s", KongLie3D::U8Text(u8"战斗统计"));
                ImGui::Spacing();
                DrawResultStatsTable(playerSummary, enemySummary);

                ImGui::EndTable();
            }

            ImGui::SetCursorPosY(ImGui::GetWindowHeight() - Ui(66.0f));
            if (battleSystem.GetWinnerTeam() == "player")
            {
                if (ImGui::BeginTable("##KongLie3DResultButtonsPlayer", 3, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    if (ButtonWithClick(KongLie3D::U8Text(u8"重来当前关"), ImVec2(-1.0f, Ui(44.0f))))
                    {
                        gameInstance.ResetBattle();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::TableNextColumn();
                    if (gameInstance.CanAdvanceToNextLevel())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, KongLie3D::Style::Highlight);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.88f, 0.42f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.89f, 0.74f, 0.24f, 1.0f));
                        if (ButtonWithClick(KongLie3D::U8Text(u8"下一关"), ImVec2(-1.0f, Ui(44.0f))))
                        {
                            gameInstance.AdvanceToNextLevel();
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopStyleColor(3);
                    }
                    else
                    {
                        ImGui::BeginDisabled();
                        ImGui::PushStyleColor(ImGuiCol_Button, KongLie3D::Style::Highlight);
                        ImGui::Button(KongLie3D::U8Text(u8"通关！"), ImVec2(-1.0f, Ui(44.0f)));
                        ImGui::PopStyleColor();
                        ImGui::EndDisabled();
                    }

                    ImGui::TableNextColumn();
                    if (ButtonWithClick(KongLie3D::U8Text(u8"回主菜单"), ImVec2(-1.0f, Ui(44.0f))))
                    {
                        // TODO: 主菜单 stub
                        gameInstance.ResetBattle();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndTable();
                }
            }
            else
            {
                if (ImGui::BeginTable("##KongLie3DResultButtonsOther", 2, ImGuiTableFlags_SizingStretchSame))
                {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    if (ButtonWithClick(KongLie3D::U8Text(u8"重来当前关"), ImVec2(-1.0f, Ui(44.0f))))
                    {
                        gameInstance.ResetBattle();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::TableNextColumn();
                    if (ButtonWithClick(KongLie3D::U8Text(u8"回主菜单"), ImVec2(-1.0f, Ui(44.0f))))
                    {
                        // TODO: 主菜单 stub
                        gameInstance.ResetBattle();
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndTable();
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void DrawUltimatePresentation(const KongLie3DGameInstance& gameInstance)
    {
        const auto& presentation = gameInstance.GetBattleSystem().GetUltimatePresentation();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!viewport || !drawList)
        {
            return;
        }

        if (presentation.flashRemainingMs > 0.0f && presentation.flashDurationMs > 0.0f)
        {
            const float alpha = 0.4f * std::clamp(presentation.flashRemainingMs / presentation.flashDurationMs, 0.0f, 1.0f);
            drawList->AddRectFilled(viewport->Pos,
                                    ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y),
                                    ImGui::ColorConvertFloat4ToU32(ImVec4(
                                        presentation.flashColor.r, presentation.flashColor.g, presentation.flashColor.b, alpha)));
        }

        if (!presentation.title.empty() && presentation.titleRemainingMs > 0.0f && presentation.titleDurationMs > 0.0f)
        {
            const float elapsed = presentation.titleDurationMs - presentation.titleRemainingMs;
            float alpha = 1.0f;
            if (elapsed < 200.0f)
            {
                alpha = std::clamp(elapsed / 200.0f, 0.0f, 1.0f);
            }
            else if (elapsed > 600.0f)
            {
                alpha = std::clamp((presentation.titleDurationMs - elapsed) / 400.0f, 0.0f, 1.0f);
            }

            ImFont* font = KongLie3D::KongLieFonts::Display ? KongLie3D::KongLieFonts::Display : ImGui::GetFont();
            const float fontSize = font ? font->FontSize : 56.0f;
            const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, presentation.title.c_str());
            const ImVec2 textPos(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f,
                                 viewport->Pos.y + viewport->Size.y * 0.22f);
            const ImU32 shadowColor = IM_COL32(8, 8, 12, static_cast<int>(alpha * 160.0f));
            const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(presentation.titleColor.r, presentation.titleColor.g, presentation.titleColor.b, alpha));
            drawList->AddText(font, fontSize, ImVec2(textPos.x + 2.0f, textPos.y + 2.0f), shadowColor, presentation.title.c_str());
            drawList->AddText(font, fontSize, textPos, textColor, presentation.title.c_str());
        }
    }

    void DrawBattleStartBanner(const KongLie3DGameInstance& gameInstance)
    {
        const float elapsedMs = gameInstance.GetBattleStartBannerElapsedMs();
        if (elapsedMs < 0.0f || elapsedMs > BattleStartBannerDurationMs)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!viewport || !drawList)
        {
            return;
        }

        float alpha = 1.0f;
        if (elapsedMs < 150.0f)
        {
            alpha = std::clamp(elapsedMs / 150.0f, 0.0f, 1.0f);
        }
        else if (elapsedMs > 450.0f)
        {
            alpha = std::clamp((BattleStartBannerDurationMs - elapsedMs) / 350.0f, 0.0f, 1.0f);
        }

        const char* text = KongLie3D::U8Text(u8"开战！");
        ImFont* font = KongLie3D::KongLieFonts::Display ? KongLie3D::KongLieFonts::Display : ImGui::GetFont();
        const float fontSize = font ? font->FontSize : 56.0f;
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
        const ImVec2 textPos(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f,
                             viewport->Pos.y + viewport->Size.y * 0.16f);

        const ImVec2 bannerMin(textPos.x - Ui(48.0f), textPos.y - Ui(18.0f));
        const ImVec2 bannerMax(textPos.x + textSize.x + Ui(48.0f), textPos.y + textSize.y + Ui(18.0f));
        drawList->AddRectFilled(bannerMin,
                                bannerMax,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(0.08f, 0.13f, 0.22f, 0.80f * alpha)),
                                Ui(12.0f));
        drawList->AddRect(bannerMin,
                          bannerMax,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(KongLie3D::Style::Accent.x,
                                                                KongLie3D::Style::Accent.y,
                                                                KongLie3D::Style::Accent.z,
                                                                0.85f * alpha)),
                          Ui(12.0f),
                          0,
                          Ui(2.0f));
        drawList->AddText(font,
                          fontSize,
                          ImVec2(textPos.x + Ui(2.0f), textPos.y + Ui(2.0f)),
                          IM_COL32(8, 12, 20, static_cast<int>(alpha * 160.0f)),
                          text);
        drawList->AddText(font,
                          fontSize,
                          textPos,
                          ImGui::ColorConvertFloat4ToU32(ImVec4(KongLie3D::Style::Accent.x,
                                                                KongLie3D::Style::Accent.y,
                                                                KongLie3D::Style::Accent.z,
                                                                alpha)),
                          text);
    }

    void DrawResultMetricRow(const char* label, int playerValue, int enemyValue)
    {
        ImGui::TableNextRow(ImGuiTableRowFlags_None, Ui(26.0f));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        ImGui::Text("%d", playerValue);
        ImGui::TableNextColumn();
        ImGui::Text("%d", enemyValue);
    }

    void DrawResultStatsTable(const FStatsSummary& playerSummary, const FStatsSummary& enemySummary)
    {
        if (!ImGui::BeginTable("##KongLie3DResultStats", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
        {
            return;
        }

        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"项目"), ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"我方"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableSetupColumn(KongLie3D::U8Text(u8"敌方"), ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableHeadersRow();
        DrawResultMetricRow(KongLie3D::U8Text(u8"总伤"), playerSummary.damage, enemySummary.damage);
        DrawResultMetricRow(KongLie3D::U8Text(u8"物伤"), playerSummary.damageAD, enemySummary.damageAD);
        DrawResultMetricRow(KongLie3D::U8Text(u8"法伤"), playerSummary.damageAP, enemySummary.damageAP);
        DrawResultMetricRow(KongLie3D::U8Text(u8"承伤"), playerSummary.taken, enemySummary.taken);
        DrawResultMetricRow(KongLie3D::U8Text(u8"治疗"), playerSummary.healing, enemySummary.healing);
        ImGui::EndTable();
    }

    void DrawHoveredPieceTooltip(const KongLie3DGameInstance& gameInstance)
    {
        if (gameInstance.GetDraggingPiece() || ImGui::GetIO().WantCaptureMouse)
        {
            return;
        }

        const KongLie3D::FPieceRuntime* piece = gameInstance.GetHoveredTooltipPiece();
        if (!piece || !piece->alive || piece->onBench)
        {
            return;
        }

        ImGui::BeginTooltip();
        const bool pushedTitleFont = PushFontIfAvailable(KongLie3D::KongLieFonts::Title);
        ImGui::Text("%s  [%s]", piece->def.name.c_str(), GetRoleLabel(piece->def.role));
        PopFontIfPushed(pushedTitleFont);
        ImGui::Separator();
        ImGui::Text(KongLie3D::U8Text(u8"生命 %d/%d"), piece->currentHp, piece->def.hp);
        ImGui::Text(KongLie3D::U8Text(u8"攻击 %d  攻速 %.2f"), piece->def.atk, piece->def.atkSpeed * piece->GetAttackSpeedMultiplier());
        ImGui::Text(KongLie3D::U8Text(u8"射程 %d"), piece->def.range);
        ImGui::TextUnformatted(piece->def.team == "player" ? KongLie3D::U8Text(u8"我方") : KongLie3D::U8Text(u8"敌方"));
        ImGui::EndTooltip();
    }
}

namespace KongLie3D
{
    void RenderHUD(KongLie3DGameInstance& gameInstance)
    {
        DrawDeploymentZoneGuidance(gameInstance);
        DrawDragHighlights(gameInstance);
        DrawAttackTraces(gameInstance);
        DrawSkillEffects(gameInstance);
        DrawDamagePopups(gameInstance);
        DrawUnitHealthBars(gameInstance);
        DrawHeroPanel(gameInstance);
        DrawBattleTimer(gameInstance);
        DrawBattleStartBanner(gameInstance);
        DrawSideControlPanel(gameInstance);
        DrawStatsPanel(gameInstance);
        DrawLeftInfoPanel(gameInstance);
        DrawOvertimeOverlay(gameInstance);
        DrawUltimatePresentation(gameInstance);
        DrawDeploymentHintOverlay(gameInstance);
        DrawRendererIndicator(gameInstance);
        DrawHoveredPieceTooltip(gameInstance);
        DrawResultModal(gameInstance);
        gameInstance.GetNotificationCenter().Render(BottomHeroBarHeight + BottomHeroBarMargin + Ui(16.0f),
                                                   GetRightInfoPanelReserveWidth());
    }
}
