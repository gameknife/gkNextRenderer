#include "KongLie3DUI.hpp"

#include <imgui.h>

#include "Assets/Core/Node.h"
#include "KongLie3DAudio.hpp"
#include "KongLie3DGameInstance.hpp"

namespace
{
    constexpr float BattleDurationSeconds = 30.0f;
    constexpr float OvertimeBannerDurationMs = 2000.0f;
    constexpr double StatsRefreshIntervalSeconds = 0.6;
    constexpr int BenchRow = 8;
    constexpr float BenchWorldZ = 8.5f;

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
            const float width = 38.0f;
            const float height = 5.0f;
            const ImVec2 min(screenPos.x - width * 0.5f, screenPos.y - height * 0.5f);
            const ImVec2 max(screenPos.x + width * 0.5f, screenPos.y + height * 0.5f);
            const ImVec2 fillMax(min.x + width * healthRatio, max.y);

            drawList->AddRectFilled(min, max, IM_COL32(18, 18, 18, 180), 2.0f);
            drawList->AddRectFilled(min, fillMax, ImGui::ColorConvertFloat4ToU32(GetHealthColor(healthRatio)), 2.0f);
            drawList->AddRect(min, max, IM_COL32(255, 255, 255, 120), 2.0f);
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
            screenPos.y -= 22.0f * progress;
            drawList->AddText(ImVec2(screenPos.x - 10.0f, screenPos.y),
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

        ImFont* font = ImGui::GetFont();
        const char* primaryText = KongLie3D::U8Text(u8"拖拽棋子调整阵型，按 SPACE 开始战斗");
        const char* secondaryText = KongLie3D::U8Text(u8"按 F3 切换渲染管线，体验光追画质");
        const float primarySize = 22.0f;
        const float secondarySize = 18.0f;
        const ImVec2 primaryTextSize = font->CalcTextSizeA(primarySize, FLT_MAX, 0.0f, primaryText);
        const ImVec2 secondaryTextSize = font->CalcTextSizeA(secondarySize, FLT_MAX, 0.0f, secondaryText);
        const float centerX = viewport->Pos.x + viewport->Size.x * 0.5f;
        const float baseY = viewport->Pos.y + viewport->Size.y * 0.16f;

        const ImU32 primaryShadow = IM_COL32(8, 8, 12, static_cast<int>(alpha * 150.0f));
        const ImU32 primaryColor = IM_COL32(255, 255, 255, static_cast<int>(alpha * 200.0f));
        const ImU32 secondaryShadow = IM_COL32(8, 8, 12, static_cast<int>(alpha * 120.0f));
        const ImU32 secondaryColor = IM_COL32(210, 220, 240, static_cast<int>(alpha * 175.0f));

        const ImVec2 primaryPos(centerX - primaryTextSize.x * 0.5f, baseY);
        const ImVec2 secondaryPos(centerX - secondaryTextSize.x * 0.5f, baseY + primaryTextSize.y + 8.0f);
        drawList->AddText(font, primarySize, ImVec2(primaryPos.x + 2.0f, primaryPos.y + 2.0f), primaryShadow, primaryText);
        drawList->AddText(font, primarySize, primaryPos, primaryColor, primaryText);
        drawList->AddText(font,
                          secondarySize,
                          ImVec2(secondaryPos.x + 2.0f, secondaryPos.y + 2.0f),
                          secondaryShadow,
                          secondaryText);
        drawList->AddText(font, secondarySize, secondaryPos, secondaryColor, secondaryText);
    }

    void DrawRendererIndicator(const KongLie3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport)
        {
            return;
        }

        ImGui::SetNextWindowBgAlpha(0.40f);
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 18.0f, viewport->Pos.y + viewport->Size.y - 18.0f),
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
        ImGui::SetNextWindowPos(ImVec2(8.0f, 60.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(220.0f, 500.0f), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (!ImGui::Begin("Heroes###KongLie3DHeroes", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        if (const auto* relic = battleSystem.GetSelectedRelic())
        {
            ImGui::TextColored(ToImVec4(relic->color), "%s %s", relic->icon.c_str(), relic->name.c_str());
            ImGui::Separator();
        }
        else
        {
            ImGui::TextDisabled("Relic: None");
            ImGui::Separator();
        }

        const bool previewMode = battleSystem.GetState() == KongLie3D::EBattleState::Deployment;
        const std::vector<KongLie3D::FSynergyStatus> previewStatuses = previewMode ? battleSystem.BuildSynergyPreview()
                                                                                   : std::vector<KongLie3D::FSynergyStatus>{};
        const auto& displayedStatuses = previewMode ? previewStatuses : battleSystem.GetActiveSynergies();
        ImGui::TextUnformatted(previewMode ? KongLie3D::U8Text(u8"羁绊预览") : KongLie3D::U8Text(u8"当前羁绊"));
        if (displayedStatuses.empty())
        {
            ImGui::TextDisabled("%s", KongLie3D::U8Text(u8"当前阵容暂无羁绊"));
        }
        else
        {
            for (const auto& status : displayedStatuses)
            {
                const std::string line = status.active
                                             ? fmt::format("{} {} ×{}  {}",
                                                           KongLie3D::U8Text(u8"✓"),
                                                           status.name,
                                                           status.count,
                                                           FormatSynergyBonus(status.activeTier))
                                             : fmt::format("{} ×{}/{}",
                                                           status.name,
                                                           status.count,
                                                           status.nextTierCount > 0 ? status.nextTierCount : status.count);
                if (status.active)
                {
                    ImGui::TextColored(ImVec4(0.97f, 0.83f, 0.33f, 1.0f), "%s", line.c_str());
                }
                else
                {
                    ImGui::TextDisabled("%s", line.c_str());
                }
            }
        }
        ImGui::Separator();

        ImGui::BeginChild("HeroRoster###KongLie3DHeroRoster", ImVec2(0.0f, 0.0f), false);

        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (!piece.def.isHero || piece.def.team != "player")
            {
                continue;
            }

            ImGui::PushID(piece.pieceId.c_str());

            const ImVec2 avatarMin = ImGui::GetCursorScreenPos();
            const ImVec2 avatarMax(avatarMin.x + 34.0f, avatarMin.y + 34.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(avatarMin, avatarMax, ToImColor(piece.def.color), 4.0f);
            ImGui::Dummy(ImVec2(34.0f, 34.0f));

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(piece.def.name.c_str());

            const float hpRatio =
                piece.def.hp > 0 ? std::clamp(static_cast<float>(piece.currentHp) / static_cast<float>(piece.def.hp), 0.0f, 1.0f) : 0.0f;
            ImGui::TextUnformatted("HP");
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, GetHealthColor(hpRatio));
            ImGui::ProgressBar(hpRatio, ImVec2(-1.0f, 0.0f));
            ImGui::PopStyleColor();

            const float manaRatio = piece.def.maxMana > 0
                                        ? std::clamp(static_cast<float>(piece.currentMana) / static_cast<float>(piece.def.maxMana), 0.0f, 1.0f)
                                        : 0.0f;
            ImGui::TextUnformatted("MP");
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
            ImGui::ProgressBar(manaRatio, ImVec2(-1.0f, 0.0f));
            ImGui::PopStyleColor();

            const std::string wText = piece.def.skillWName.empty() ? piece.def.skillW : piece.def.skillWName;
            ImGui::Text("W: %s", wText.empty() ? "-" : wText.c_str());
            if (!piece.def.skillWDesc.empty())
            {
                ImGui::Indent(12.0f);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("%s", piece.def.skillWDesc.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Unindent(12.0f);
            }

            if (!piece.alive)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.12f, 0.12f, 1.0f));
                ImGui::Button("X Fallen", ImVec2(-1.0f, 0.0f));
                ImGui::PopStyleColor();
            }
            else if (piece.ultimateUsed)
            {
                const char* usedLabel = KongLie3D::U8Text(u8"✓ 已释放");
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                ImGui::Button(usedLabel, ImVec2(-1.0f, 0.0f));
                ImGui::PopStyleColor();
            }
            else
            {
                const bool ready = piece.def.maxMana > 0 && piece.currentMana >= piece.def.maxMana &&
                                   piece.ultimateCooldownMs <= 0.0f && !piece.onBench;
                if (ready)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.73f, 0.12f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.98f, 0.80f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.67f, 0.10f, 1.0f));
                    if (ButtonWithClick("R Ready", ImVec2(-1.0f, 0.0f)))
                    {
                        battleSystem.RequestUltimate(piece.pieceId);
                    }
                    ImGui::PopStyleColor(3);
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.30f, 0.34f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.38f, 0.38f, 0.42f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.26f, 0.30f, 1.0f));
                    if (ButtonWithClick("R Charging", ImVec2(-1.0f, 0.0f)))
                    {
                        battleSystem.RequestUltimate(piece.pieceId);
                    }
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

            ImGui::EndGroup();
            ImGui::Separator();
            ImGui::PopID();
        }

        ImGui::EndChild();
        ImGui::End();
    }

    void DrawBattleTimer(const KongLie3DGameInstance& gameInstance)
    {
        const auto& battleSystem = gameInstance.GetBattleSystem();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + (viewport->Size.x - 200.0f) * 0.5f, viewport->Pos.y + 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(200.0f, 36.0f), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
        if (!ImGui::Begin("###KongLie3DTimer", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        if (battleSystem.GetState() == KongLie3D::EBattleState::Battle)
        {
            if (battleSystem.IsOvertimeActive())
            {
                const float overtimeElapsed = std::max(0.0f, (battleSystem.GetElapsedMs() - battleSystem.GetOvertimeStartMs()) / 1000.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.20f, 1.0f), "Overtime +%.1f s", overtimeElapsed);
            }
            else
            {
                const float remaining = std::max(0.0f, BattleDurationSeconds - battleSystem.GetElapsedMs() / 1000.0f);
                const int remainingSeconds = static_cast<int>(std::ceil(remaining));
                if (remaining <= 5.0f)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.20f, 1.0f), "Remaining %d s", remainingSeconds);
                }
                else
                {
                    ImGui::Text("Remaining %d s", remainingSeconds);
                }
            }
        }
        else if (battleSystem.GetState() == KongLie3D::EBattleState::Ended)
        {
            ImGui::Text("Battle Ended");
        }
        else
        {
            ImGui::Text("Waiting to Start");
        }

        ImGui::End();
    }

    void DrawSideControlPanel(KongLie3DGameInstance& gameInstance)
    {
        auto& battleSystem = gameInstance.GetBattleSystem();
        int playerAlive = 0;
        int enemyAlive = 0;
        for (const auto& piece : gameInstance.GetPieceRuntimes())
        {
            if (piece.onBench || !piece.alive)
            {
                continue;
            }

            if (piece.def.team == "player")
            {
                ++playerAlive;
            }
            else if (piece.def.team == "enemy")
            {
                ++enemyAlive;
            }
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - 230.0f, viewport->Pos.y + 60.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(220.0f, 235.0f), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (!ImGui::Begin("Battle###KongLie3DControls", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Player: %d/6", playerAlive);
        ImGui::Text("Enemy: %d/6", enemyAlive);
        if (const auto* currentLevel = gameInstance.GetCurrentLevel())
        {
            ImGui::Separator();
            ImGui::Text("%s", KongLie3D::U8Text(u8"难度"));
            ImGui::TextColored(ImVec4(0.74f, 0.86f, 1.0f, 1.0f), "%s", currentLevel->name.c_str());
            ImGui::TextDisabled("%s", fmt::format("Enemy DMG x{:.2f}", currentLevel->enemyDmgMult).c_str());

            if (battleSystem.GetState() == KongLie3D::EBattleState::Deployment)
            {
                const auto& levels = gameInstance.GetLevels();
                for (size_t index = 0; index < levels.size(); ++index)
                {
                    const bool selected = index == gameInstance.GetCurrentLevelIndex();
                    if (selected)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.42f, 0.78f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.50f, 0.88f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.20f, 0.36f, 0.72f, 1.0f));
                    }

                    if (ButtonWithClick(levels[index].name.c_str(), ImVec2(-1.0f, 0.0f)))
                    {
                        gameInstance.SelectLevel(index);
                    }

                    if (selected)
                    {
                        ImGui::PopStyleColor(3);
                    }
                }
            }
        }

        if (battleSystem.GetState() == KongLie3D::EBattleState::Deployment)
        {
            ImGui::Separator();
            if (ButtonWithClick("Start Battle", ImVec2(-1.0f, 0.0f)))
            {
                gameInstance.StartBattle();
            }
        }
        else if (battleSystem.GetState() == KongLie3D::EBattleState::Battle)
        {
            ImGui::Separator();
            if (ButtonWithClick(battleSystem.IsPaused() ? "Resume" : "Pause", ImVec2(-1.0f, 0.0f)))
            {
                battleSystem.TogglePause();
            }
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::Button("Battle Ended", ImVec2(-1.0f, 0.0f));
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        const int speedValue = static_cast<int>(std::lround(battleSystem.GetSpeedMultiplier()));
        const std::string speedLabel = fmt::format("Speed: {}x", speedValue);
        if (ButtonWithClick(speedLabel.c_str(), ImVec2(-1.0f, 0.0f)))
        {
            battleSystem.CycleSpeedMultiplier();
        }

        ImGui::End();
    }

    void DrawStatsTable(const char* tableId, const std::vector<FStatsRow>& rows)
    {
        if (!ImGui::BeginTable(tableId, 5,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
        {
            return;
        }

        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("AD");
        ImGui::TableSetupColumn("AP");
        ImGui::TableSetupColumn("Taken");
        ImGui::TableSetupColumn("Heal");
        ImGui::TableHeadersRow();

        for (const auto& row : rows)
        {
            ImGui::TableNextRow();
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

    void DrawStatsPanel(const KongLie3DGameInstance& gameInstance)
    {
        static FStatsCache cache;
        const double now = ImGui::GetTime();
        if (cache.lastRefreshTime < 0.0 || (now - cache.lastRefreshTime) >= StatsRefreshIntervalSeconds)
        {
            RefreshStatsCache(gameInstance, cache);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 8.0f, viewport->Pos.y + viewport->Size.y - 140.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - 16.0f, 130.0f), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (!ImGui::Begin("Stats###KongLie3DStats", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("###KongLie3DStatTabs"))
        {
            if (ImGui::BeginTabItem("Player"))
            {
                DrawStatsTable("##KongLie3DPlayerStats", cache.playerRows);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Enemy"))
            {
                DrawStatsTable("##KongLie3DEnemyStats", cache.enemyRows);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void DrawRelicPanel(KongLie3DGameInstance& gameInstance)
    {
        auto& battleSystem = gameInstance.GetBattleSystem();
        if (battleSystem.GetState() != KongLie3D::EBattleState::Deployment || battleSystem.GetRelics().empty())
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - 292.0f, viewport->Pos.y + viewport->Size.y - 330.0f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(284.0f, 320.0f), ImGuiCond_Always);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        if (!ImGui::Begin("Relics###KongLie3DRelics", nullptr, flags))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Choose one relic");
        ImGui::Separator();

        const KongLie3D::FRelicDef* selectedRelic = battleSystem.GetSelectedRelic();
        for (const auto& relic : battleSystem.GetRelics())
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
                battleSystem.SelectRelic(relic.id);
            }
            ImGui::PopStyleColor(3);

            for (const std::string& buff : relic.buffs)
            {
                ImGui::TextWrapped("  %s", buff.c_str());
            }
            if (isSelected)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.92f, 0.45f, 1.0f), "Selected");
            }
            if (&relic != &battleSystem.GetRelics().back())
            {
                ImGui::Separator();
            }
            ImGui::PopID();
        }

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
                          8.0f);

        const float bannerElapsedMs = battleSystem.GetElapsedMs() - battleSystem.GetOvertimeStartMs();
        if (bannerElapsedMs >= 0.0f && bannerElapsedMs <= OvertimeBannerDurationMs)
        {
            const float fade = bannerElapsedMs < 500.0f ? bannerElapsedMs / 500.0f
                                                        : std::clamp((OvertimeBannerDurationMs - bannerElapsedMs) / 1000.0f, 0.0f, 1.0f);
            const char* text = KongLie3D::U8Text(u8"⚡ 加 时 ⚡");
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 textPos(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f,
                                 viewport->Pos.y + viewport->Size.y * 0.18f);
            drawList->AddText(textPos, IM_COL32(255, 90, 65, static_cast<int>(fade * 255.0f)), text);
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
        ImGui::SetNextWindowSize(ImVec2(420.0f, 220.0f), ImGuiCond_Always);

        ImVec4 borderColor(0.85f, 0.76f, 0.18f, 1.0f);
        ImVec4 backgroundColor(0.08f, 0.13f, 0.24f, 0.92f);
        const char* title = KongLie3D::U8Text(u8"战斗胜利");
        std::string summary;

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

        if (battleSystem.GetWinnerTeam() == "enemy")
        {
            borderColor = ImVec4(0.78f, 0.22f, 0.18f, 1.0f);
            backgroundColor = ImVec4(0.22f, 0.07f, 0.08f, 0.92f);
            title = KongLie3D::U8Text(u8"战斗失败");
            summary = fmt::format(fmt::runtime(KongLie3D::U8Text(u8"全军覆没 · 耗时 {}:{:02d}")), minutes, seconds);
        }
        else if (battleSystem.GetWinnerTeam() == "draw")
        {
            borderColor = ImVec4(0.90f, 0.78f, 0.25f, 1.0f);
            backgroundColor = ImVec4(0.25f, 0.20f, 0.07f, 0.92f);
            title = KongLie3D::U8Text(u8"超时平局");
            summary = fmt::format(fmt::runtime(KongLie3D::U8Text(u8"加时赛结束未分胜负 · 耗时 {}:{:02d}")), minutes, seconds);
        }
        else
        {
            summary = fmt::format(fmt::runtime(KongLie3D::U8Text(u8"全歼敌方 · 存活 {}/6 · 耗时 {}:{:02d}")),
                                  playerAlive,
                                  minutes,
                                  seconds);
        }

        ImGui::PushStyleColor(ImGuiCol_PopupBg, backgroundColor);
        ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
        if (ImGui::BeginPopupModal("KongLie3DResultModal", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        {
            ImGui::TextColored(borderColor, "%s", title);
            ImGui::Spacing();
            ImGui::TextWrapped("%s", summary.c_str());
            ImGui::Spacing();
            if (const auto* relic = gameInstance.GetBattleSystem().GetSelectedRelic())
            {
                ImGui::TextWrapped(KongLie3D::U8Text(u8"携带圣物：%s"), relic->name.c_str());
            }
            else
            {
                ImGui::TextWrapped("%s", KongLie3D::U8Text(u8"携带圣物：未选择"));
            }
            if (const auto* currentLevel = gameInstance.GetCurrentLevel())
            {
                ImGui::TextWrapped(KongLie3D::U8Text(u8"当前难度：%s"), currentLevel->name.c_str());
            }
            ImGui::Spacing();
            ImGui::Spacing();

            if (battleSystem.GetWinnerTeam() == "player" && gameInstance.CanAdvanceToNextLevel())
            {
                if (ButtonWithClick(KongLie3D::U8Text(u8"下一关"), ImVec2(120.0f, 36.0f)))
                {
                    gameInstance.AdvanceToNextLevel();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
            }

            if (battleSystem.GetWinnerTeam() == "player" && !gameInstance.CanAdvanceToNextLevel())
            {
                ImGui::BeginDisabled();
                ImGui::Button(KongLie3D::U8Text(u8"通关！"), ImVec2(120.0f, 36.0f));
                ImGui::EndDisabled();
                ImGui::SameLine();
            }

            if (ButtonWithClick(KongLie3D::U8Text(u8"重来当前关"), ImVec2(140.0f, 36.0f)))
            {
                gameInstance.ResetBattle();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
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

            constexpr float fontSize = 56.0f;
            ImFont* font = ImGui::GetFont();
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
        ImGui::Text("%s  [%s]", piece->def.name.c_str(), piece->def.role.c_str());
        ImGui::Separator();
        ImGui::Text("HP %d/%d", piece->currentHp, piece->def.hp);
        ImGui::Text("ATK %d  ATK_SPD %.2f", piece->def.atk, piece->def.atkSpeed * piece->GetAttackSpeedMultiplier());
        ImGui::Text("Range %d", piece->def.range);
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
        DrawSideControlPanel(gameInstance);
        DrawRelicPanel(gameInstance);
        DrawStatsPanel(gameInstance);
        DrawOvertimeOverlay(gameInstance);
        DrawUltimatePresentation(gameInstance);
        DrawDeploymentHintOverlay(gameInstance);
        DrawRendererIndicator(gameInstance);
        DrawHoveredPieceTooltip(gameInstance);
        DrawResultModal(gameInstance);
    }
}
