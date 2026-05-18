#include "Voyage3DUI.hpp"

#include <imgui.h>

#include "Runtime/Utilities/NextEngineHelper.h"
#include "Voyage3DCommon.hpp"
#include "Voyage3DGameInstance.hpp"

namespace
{
    ImU32 Color(const glm::vec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
    }

    ImU32 Color3(const glm::vec3& color, int alpha = 255)
    {
        return IM_COL32(static_cast<int>(std::clamp(color.r, 0.0f, 1.0f) * 255.0f),
                        static_cast<int>(std::clamp(color.g, 0.0f, 1.0f) * 255.0f),
                        static_cast<int>(std::clamp(color.b, 0.0f, 1.0f) * 255.0f),
                        alpha);
    }

    ImVec4 HealthColor(float ratio)
    {
        if (ratio < 0.35f)
        {
            return ImVec4(0.90f, 0.18f, 0.14f, 1.0f);
        }
        if (ratio < 0.65f)
        {
            return ImVec4(0.92f, 0.68f, 0.18f, 1.0f);
        }
        return ImVec4(0.18f, 0.78f, 0.32f, 1.0f);
    }

    ImVec2 CenterWindowPos(const ImGuiViewport* viewport, const ImVec2& size)
    {
        return ImVec2(viewport->Pos.x + (viewport->Size.x - size.x) * 0.5f,
                      viewport->Pos.y + (viewport->Size.y - size.y) * 0.5f);
    }

    void DrawPortLabels(Voyage3DGameInstance& gameInstance)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (const Voyage3D::FPortRuntime& port : gameInstance.GetPorts())
        {
            ImVec2 screen{};
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, port.worldPos + glm::vec3(0.0f, 2.8f, 0.0f), screen))
            {
                continue;
            }
            const std::string marker = port.visited ? Voyage3D::U8Text(u8"✓ ") : "? ";
            const std::string label = marker + port.def.name;
            const ImU32 textColor = port.visited ? IM_COL32(245, 205, 82, 255) : IM_COL32(185, 185, 185, 220);
            drawList->AddText(ImVec2(screen.x + 1.0f, screen.y + 1.0f), IM_COL32(0, 0, 0, 180), label.c_str());
            drawList->AddText(screen, textColor, label.c_str());
        }
    }

    void DrawWorldEffects(Voyage3DGameInstance& gameInstance)
    {
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        for (const Voyage3D::FMuzzleFlash& flash : gameInstance.GetMuzzleFlashes())
        {
            ImVec2 center{};
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, flash.worldPos, center))
            {
                continue;
            }
            const float alpha = std::clamp(flash.remainingMs / flash.lifeMs, 0.0f, 1.0f);
            drawList->AddCircleFilled(center, 10.0f * alpha + 3.0f, Color(glm::vec4(flash.color, alpha)));
        }

        for (const Voyage3D::FFloatingText& text : gameInstance.GetFloatingTexts())
        {
            const float progress = 1.0f - std::clamp(text.remainingMs / text.lifeMs, 0.0f, 1.0f);
            ImVec2 screen{};
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, text.worldPos + glm::vec3(0.0f, progress * 1.0f, 0.0f), screen))
            {
                continue;
            }
            const float alpha = std::clamp(text.remainingMs / text.lifeMs, 0.0f, 1.0f);
            const ImU32 shadow = IM_COL32(0, 0, 0, static_cast<int>(alpha * 180.0f));
            const ImU32 foreground = Color(glm::vec4(text.color.r, text.color.g, text.color.b, text.color.a * alpha));
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * text.fontScale, ImVec2(screen.x + 1.0f, screen.y + 1.0f), shadow, text.text.c_str());
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * text.fontScale, screen, foreground, text.text.c_str());
        }

        for (const Voyage3D::FExpandingRing& ring : gameInstance.GetExplosionRings())
        {
            const float progress = 1.0f - std::clamp(ring.remainingMs / ring.lifeMs, 0.0f, 1.0f);
            ImVec2 center{};
            ImVec2 edge{};
            if (!NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, ring.worldPos, center) ||
                !NextEngineHelper::TryProjectWorldToScreenForGame(gameInstance, ring.worldPos + glm::vec3(ring.maxRadius * progress, 0.0f, 0.0f), edge))
            {
                continue;
            }
            const float radius = std::max(4.0f, std::abs(edge.x - center.x));
            const float alpha = std::clamp(ring.remainingMs / ring.lifeMs, 0.0f, 1.0f);
            drawList->AddCircle(center, radius, Color(glm::vec4(ring.color.r, ring.color.g, ring.color.b, ring.color.a * alpha)), 48, 3.0f);
        }
    }

    void RenderTopHud(Voyage3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 12.0f, viewport->Pos.y + 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - 24.0f, 46.0f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.05f, 0.08f, 0.72f));
        if (ImGui::Begin("Voyage3DTopHud", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings))
        {
            const Voyage3D::FShipRuntime& ship = gameInstance.GetPlayerShip();
            ImGui::Text(Voyage3D::U8Text(u8"金币 %d   船型 %s   HP %d/%d   货舱 %d/%d"),
                        gameInstance.GetGold(),
                        ship.def.name.c_str(),
                        ship.currentHp,
                        ship.def.hp,
                        ship.cargoUsed,
                        ship.def.cargoMax);
            ImGui::SameLine(viewport->Size.x * 0.46f);
            ImGui::Text("%s", gameInstance.FormatGameDate().c_str());
            ImGui::SameLine(viewport->Size.x * 0.72f);
            ImGui::Text("X %.1f  Z %.1f", ship.worldPos.x, ship.worldPos.z);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    void RenderBottomHints(Voyage3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        if (!drawList)
        {
            return;
        }

        std::string hint;
        if (gameInstance.GetAppState() == Voyage3D::EAppState::Sailing && gameInstance.GetNearestPort())
        {
            hint = fmt::format(fmt::runtime(Voyage3D::U8Text(u8"按 SPACE 进入 {}")), gameInstance.GetNearestPort()->def.name);
        }
        else if (gameInstance.GetAppState() == Voyage3D::EAppState::Sailing)
        {
            hint = Voyage3D::U8Text(u8"W/S 调速  A/D 转向  F5 测试海盗  J 航海日志");
        }
        else if (gameInstance.GetAppState() == Voyage3D::EAppState::NavalCombat)
        {
            hint = Voyage3D::U8Text(u8"Q 左舷齐射  E 右舷齐射  Shift 长按 3 秒撤退");
        }
        if (hint.empty())
        {
            return;
        }

        const ImVec2 textSize = ImGui::CalcTextSize(hint.c_str());
        const ImVec2 pos(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f, viewport->Pos.y + viewport->Size.y - 52.0f);
        drawList->AddRectFilled(ImVec2(pos.x - 14.0f, pos.y - 8.0f), ImVec2(pos.x + textSize.x + 14.0f, pos.y + textSize.y + 8.0f), IM_COL32(8, 12, 18, 185), 6.0f);
        drawList->AddText(pos, IM_COL32(238, 242, 248, 255), hint.c_str());
    }

    void RenderToast(Voyage3DGameInstance& gameInstance)
    {
        if (gameInstance.GetToastMs() <= 0.0f || gameInstance.GetToastText().empty())
        {
            return;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const std::string& text = gameInstance.GetToastText();
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 pos(viewport->Pos.x + (viewport->Size.x - textSize.x) * 0.5f, viewport->Pos.y + 74.0f);
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        drawList->AddRectFilled(ImVec2(pos.x - 18.0f, pos.y - 9.0f), ImVec2(pos.x + textSize.x + 18.0f, pos.y + textSize.y + 9.0f), IM_COL32(16, 34, 48, 220), 6.0f);
        drawList->AddText(pos, IM_COL32(245, 238, 194, 255), text.c_str());
    }

    void RenderCombatBars(Voyage3DGameInstance& gameInstance)
    {
        if (gameInstance.GetAppState() != Voyage3D::EAppState::NavalCombat)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0f, viewport->Pos.y + 64.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(330.0f, 88.0f), ImGuiCond_Always);
        if (ImGui::Begin("Voyage3DCombatBars", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings))
        {
            const Voyage3D::FShipRuntime& player = gameInstance.GetPlayerShip();
            const float playerRatio = player.def.hp > 0 ? static_cast<float>(player.currentHp) / static_cast<float>(player.def.hp) : 0.0f;
            ImGui::Text(Voyage3D::U8Text(u8"旗舰"));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, HealthColor(playerRatio));
            ImGui::ProgressBar(playerRatio, ImVec2(-1.0f, 16.0f), fmt::format("{}/{}", player.currentHp, player.def.hp).c_str());
            ImGui::PopStyleColor();

            const auto& enemies = gameInstance.GetEnemyShips();
            if (!enemies.empty() && enemies.front().active)
            {
                const Voyage3D::FShipRuntime& enemy = enemies.front();
                const float enemyRatio = enemy.def.hp > 0 ? static_cast<float>(enemy.currentHp) / static_cast<float>(enemy.def.hp) : 0.0f;
                ImGui::Text(Voyage3D::U8Text(u8"海盗"));
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, HealthColor(enemyRatio));
                ImGui::ProgressBar(enemyRatio, ImVec2(-1.0f, 16.0f), fmt::format("{}/{}", enemy.currentHp, enemy.def.hp).c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();
    }

    void RenderMainMenu(Voyage3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 windowSize(380.0f, 260.0f);
        ImGui::SetNextWindowPos(CenterWindowPos(viewport, windowSize), ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        if (ImGui::Begin("Voyage3DMainMenu", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
            if (gameInstance.GetTitleFont())
            {
                ImGui::PushFont(gameInstance.GetTitleFont());
            }
            ImGui::TextUnformatted("Voyage3D");
            if (gameInstance.GetTitleFont())
            {
                ImGui::PopFont();
            }
            ImGui::TextUnformatted(Voyage3D::U8Text(u8"地中海跑商冒险"));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
            if (ImGui::Button(Voyage3D::U8Text(u8"新游戏"), ImVec2(-1.0f, 42.0f)))
            {
                gameInstance.StartNewGame();
            }
            if (ImGui::Button(Voyage3D::U8Text(u8"退出"), ImVec2(-1.0f, 38.0f)))
            {
                gameInstance.GetEngine().RequestClose();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void RenderPortMenu(Voyage3DGameInstance& gameInstance)
    {
        Voyage3D::FPortRuntime* port = gameInstance.GetCurrentPort();
        if (!port)
        {
            return;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 windowSize(430.0f, 330.0f);
        ImGui::SetNextWindowPos(CenterWindowPos(viewport, windowSize), ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        if (ImGui::Begin("Voyage3DPortMenu", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::Text("%s - %s", port->def.name.c_str(), port->def.nation.c_str());
            ImGui::Separator();
            const auto loreIt = gameInstance.GetPortLore().find(port->def.id);
            ImGui::TextWrapped("%s", loreIt != gameInstance.GetPortLore().end() ? loreIt->second.c_str() : Voyage3D::U8Text(u8"港口商会正在更新航线情报。"));
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            if (ImGui::Button(Voyage3D::U8Text(u8"市场"), ImVec2(-1.0f, 34.0f)))
            {
                gameInstance.OpenTrade();
            }
            ImGui::BeginDisabled();
            ImGui::Button(Voyage3D::U8Text(u8"酒馆"), ImVec2(-1.0f, 34.0f));
            ImGui::EndDisabled();
            if (ImGui::Button(Voyage3D::U8Text(u8"造船厂"), ImVec2(-1.0f, 34.0f)))
            {
                gameInstance.OpenShipUpgrade();
            }
            if (ImGui::Button(Voyage3D::U8Text(u8"出港"), ImVec2(-1.0f, 34.0f)))
            {
                gameInstance.LeavePort();
            }
            ImGui::BeginDisabled();
            ImGui::Button(Voyage3D::U8Text(u8"保存"), ImVec2(-1.0f, 34.0f));
            ImGui::EndDisabled();
        }
        ImGui::End();
    }

    void RenderTradePanel(Voyage3DGameInstance& gameInstance)
    {
        Voyage3D::FPortRuntime* port = gameInstance.GetCurrentPort();
        if (!port)
        {
            return;
        }

        static std::map<std::string, int> buyQty;
        static std::map<std::string, int> sellQty;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 windowSize(760.0f, 520.0f);
        ImGui::SetNextWindowPos(CenterWindowPos(viewport, windowSize), ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        if (ImGui::Begin("Voyage3DTrade", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            const Voyage3D::FShipRuntime& ship = gameInstance.GetPlayerShip();
            const std::string goldLabel = fmt::format(fmt::runtime(Voyage3D::U8Text(u8"金币 {}")), gameInstance.GetGold());
            ImGui::Text("%s - %s - %s", port->def.name.c_str(), gameInstance.FormatGameDate().c_str(), goldLabel.c_str());
            ImGui::Text(Voyage3D::U8Text(u8"货舱 %d/%d"), ship.cargoUsed, ship.def.cargoMax);
            if (gameInstance.GetTradeMessageMs() > 0.0f && !gameInstance.GetTradeMessage().empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.20f, 1.0f), "%s", gameInstance.GetTradeMessage().c_str());
            }
            ImGui::Separator();
            if (ImGui::BeginTable("trade", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"商品"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"当前价"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"买入"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"持有"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"数量"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"卖出"));
                ImGui::TableHeadersRow();

                for (const Voyage3D::FGoodsDef& good : gameInstance.GetGoodsDefs())
                {
                    ImGui::PushID(good.id.c_str());
                    const int price = port->currentPrices.contains(good.id) ? port->currentPrices.at(good.id) : good.basePrice;
                    int& buy = buyQty[good.id];
                    int& sell = sellQty[good.id];
                    const int held = ship.cargo.contains(good.id) ? ship.cargo.at(good.id) : 0;
                    if (buy <= 0)
                    {
                        buy = std::max(1, ship.def.cargoMax - ship.cargoUsed);
                    }
                    if (sell <= 0)
                    {
                        sell = std::max(1, held);
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(good.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", price);
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton(Voyage3D::U8Text(u8"买 1")))
                    {
                        gameInstance.TryBuyGood(good.id, 1);
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(58.0f);
                    ImGui::InputInt("##buyQty", &buy, 0, 0);
                    buy = std::max(1, buy);
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Voyage3D::U8Text(u8"买 N")))
                    {
                        gameInstance.TryBuyGood(good.id, buy);
                    }
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", held);
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(58.0f);
                    ImGui::InputInt("##sellQty", &sell, 0, 0);
                    sell = std::max(1, sell);
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton(Voyage3D::U8Text(u8"卖 1")))
                    {
                        gameInstance.TrySellGood(good.id, 1);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton(Voyage3D::U8Text(u8"卖 N")))
                    {
                        gameInstance.TrySellGood(good.id, sell);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button(Voyage3D::U8Text(u8"返回港口"), ImVec2(150.0f, 34.0f)))
            {
                gameInstance.ReturnToPortMenu();
            }
        }
        ImGui::End();
    }

    void RenderShipUpgradePanel(Voyage3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 windowSize(700.0f, 340.0f);
        ImGui::SetNextWindowPos(CenterWindowPos(viewport, windowSize), ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        if (ImGui::Begin("Voyage3DShipyard", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted(Voyage3D::U8Text(u8"造船厂"));
            ImGui::Separator();
            if (ImGui::BeginTable("ships", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"船型"));
                ImGui::TableSetupColumn("HP");
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"货舱"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"炮位"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"速度"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"价格"));
                ImGui::TableSetupColumn(Voyage3D::U8Text(u8"购买"));
                ImGui::TableHeadersRow();
                for (const Voyage3D::FShipDef& shipDef : gameInstance.GetShipDefs())
                {
                    const bool current = shipDef.id == gameInstance.GetPlayerShip().def.id;
                    ImGui::PushID(shipDef.id.c_str());
                    ImGui::TableNextRow();
                    if (current)
                    {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 82, 130, 150));
                    }
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(shipDef.name.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%d", shipDef.hp);
                    ImGui::TableNextColumn(); ImGui::Text("%d", shipDef.cargoMax);
                    ImGui::TableNextColumn(); ImGui::Text("%d", shipDef.cannonCount);
                    ImGui::TableNextColumn(); ImGui::Text("%.1f", shipDef.speedKnots);
                    ImGui::TableNextColumn(); ImGui::Text("%d", shipDef.price);
                    ImGui::TableNextColumn();
                    const bool disabled = current || gameInstance.GetGold() < shipDef.price;
                    if (disabled)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::SmallButton(current ? Voyage3D::U8Text(u8"当前") : Voyage3D::U8Text(u8"购买")))
                    {
                        gameInstance.TryBuyShip(shipDef.id);
                    }
                    if (disabled)
                    {
                        ImGui::EndDisabled();
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (ImGui::Button(Voyage3D::U8Text(u8"返回港口"), ImVec2(150.0f, 34.0f)))
            {
                gameInstance.ReturnToPortMenu();
            }
        }
        ImGui::End();
    }

    void RenderPirateModal(Voyage3DGameInstance& gameInstance)
    {
        if (!gameInstance.IsPirateEncounterPending())
        {
            return;
        }
        ImGui::OpenPopup("Voyage3DPirate");
        ImGui::SetNextWindowSize(ImVec2(330.0f, 150.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Voyage3DPirate", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted(Voyage3D::U8Text(u8"海盗来袭！"));
            ImGui::TextWrapped("%s", Voyage3D::U8Text(u8"远处的黑帆船正在逼近，是否进入侧舷炮战？"));
            if (ImGui::Button(Voyage3D::U8Text(u8"开战"), ImVec2(130.0f, 34.0f)))
            {
                gameInstance.StartPirateCombat();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(Voyage3D::U8Text(u8"逃跑"), ImVec2(130.0f, 34.0f)))
            {
                gameInstance.TryFleePirates();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void RenderEventLog(Voyage3DGameInstance& gameInstance)
    {
        if (!gameInstance.IsEventLogOpen())
        {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(360.0f, 250.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(Voyage3D::U8Text(u8"航海日志")))
        {
            const auto& log = gameInstance.GetEventLog();
            if (log.empty())
            {
                ImGui::TextDisabled("%s", Voyage3D::U8Text(u8"暂无事件"));
            }
            for (const std::string& item : log)
            {
                ImGui::BulletText("%s", item.c_str());
            }
        }
        ImGui::End();
    }

    void RenderResult(Voyage3DGameInstance& gameInstance)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 windowSize(440.0f, 280.0f);
        ImGui::SetNextWindowPos(CenterWindowPos(viewport, windowSize), ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        if (ImGui::Begin("Voyage3DResult", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            const bool defeated = gameInstance.GetPlayerShip().currentHp <= 0;
            ImGui::TextUnformatted(defeated ? Voyage3D::U8Text(u8"全军覆没") : Voyage3D::U8Text(u8"破产"));
            ImGui::Separator();
            ImGui::Text(Voyage3D::U8Text(u8"航行天数：%d"), gameInstance.GetSailingDays());
            ImGui::Text(Voyage3D::U8Text(u8"港口足迹：%d/%zu"), gameInstance.GetVisitedPortCount(), gameInstance.GetPorts().size());
            ImGui::Text(Voyage3D::U8Text(u8"战斗胜场：%d"), gameInstance.GetCombatWins());
            ImGui::Dummy(ImVec2(0.0f, 22.0f));
            if (ImGui::Button(Voyage3D::U8Text(u8"重新开始"), ImVec2(160.0f, 38.0f)))
            {
                gameInstance.StartNewGame();
            }
            ImGui::SameLine();
            if (ImGui::Button(Voyage3D::U8Text(u8"退出"), ImVec2(160.0f, 38.0f)))
            {
                gameInstance.GetEngine().RequestClose();
            }
        }
        ImGui::End();
    }
}

namespace Voyage3D
{
    void Render(Voyage3DGameInstance& gameInstance)
    {
        if (gameInstance.GetAppState() == EAppState::MainMenu)
        {
            DrawPortLabels(gameInstance);
            RenderMainMenu(gameInstance);
            return;
        }

        RenderTopHud(gameInstance);
        DrawPortLabels(gameInstance);
        DrawWorldEffects(gameInstance);
        RenderCombatBars(gameInstance);
        RenderBottomHints(gameInstance);
        RenderToast(gameInstance);
        RenderPirateModal(gameInstance);
        RenderEventLog(gameInstance);

        if (gameInstance.GetAppState() == EAppState::InPort)
        {
            RenderPortMenu(gameInstance);
        }
        else if (gameInstance.GetAppState() == EAppState::Trading)
        {
            RenderTradePanel(gameInstance);
        }
        else if (gameInstance.GetAppState() == EAppState::ShipUpgrade)
        {
            RenderShipUpgradePanel(gameInstance);
        }
        else if (gameInstance.GetAppState() == EAppState::Result)
        {
            RenderResult(gameInstance);
        }
    }
}
