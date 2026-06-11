#include "AirportSimUI.h"

#include "AgentSystem.h"
#include "AirportMap.h"
#include "AirportSimConfig.hpp"
#include "DecisionScheduler.h"
#include "FlightBoard.h"
#include "QueueSystem.h"
#include "TimeSystem.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

namespace AirportSim
{
    namespace
    {
        constexpr ImGuiWindowFlags kHudFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize;

        ImU32 ColorToImU32(const glm::vec3& c)
        {
            return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f),
                            static_cast<int>(c.b * 255.0f), 255);
        }

        ImVec4 FlightStateColor(EFlightState state)
        {
            switch (state)
            {
            case EFlightState::CheckinOpen: return {0.35f, 0.75f, 1.00f, 1.0f};
            case EFlightState::Boarding:    return {0.35f, 0.90f, 0.45f, 1.0f};
            case EFlightState::Final:       return {1.00f, 0.65f, 0.20f, 1.0f};
            case EFlightState::Departed:    return {0.55f, 0.55f, 0.55f, 1.0f};
            default:                        return {0.85f, 0.85f, 0.85f, 1.0f};
            }
        }

        bool ProjectWorld(const glm::mat4& viewProjection, const ImVec2& vpPos, const ImVec2& vpSize,
                          const glm::vec3& world, ImVec2& outScreen)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f)
            {
                return false;
            }
            outScreen =
                ImVec2(vpPos.x + (ndc.x * 0.5f + 0.5f) * vpSize.x, vpPos.y + (-ndc.y * 0.5f + 0.5f) * vpSize.y);
            return true;
        }

        void SwitchFollowAgent(AgentSystem& agents, int& followAgentId, int direction)
        {
            std::vector<int> activeIds;
            activeIds.reserve(agents.Agents().size());
            for (const auto& agent : agents.Agents())
            {
                if (agent.active)
                {
                    activeIds.push_back(agent.id);
                }
            }
            if (activeIds.empty())
            {
                followAgentId = -1;
                return;
            }

            const auto current = std::find(activeIds.begin(), activeIds.end(), followAgentId);
            if (current == activeIds.end())
            {
                followAgentId = direction < 0 ? activeIds.back() : activeIds.front();
                return;
            }

            const int currentIndex = static_cast<int>(std::distance(activeIds.begin(), current));
            const int count = static_cast<int>(activeIds.size());
            followAgentId = activeIds[static_cast<size_t>((currentIndex + direction + count) % count)];
        }

        std::string FormatLatency(double elapsedMs)
        {
            if (elapsedMs < 1000.0)
            {
                return fmt::format("{:.0f} ms", elapsedMs);
            }
            return fmt::format("{:.2f} s", elapsedMs / 1000.0);
        }
    }

    void AirportSimUI::Draw(const glm::mat4& viewProjection, double gameMinutes, TimeSystem& time,
                            const FlightBoard& flights, AgentSystem& agents, const AirportMap& map,
                            const QueueSystem& queues, const DecisionScheduler& scheduler, bool llmConnected)
    {
        DrawHud(time, flights, agents, scheduler, llmConnected);
        DrawFlightBoardHud(flights);
        DrawAgentPanel(flights, agents, scheduler);
        if (state_.showDebugPanel)
        {
            DrawDebugPanel(gameMinutes, time, agents, queues, scheduler);
        }
        if (state_.showOverlay)
        {
            DrawWorldOverlay(viewProjection, gameMinutes, agents, map, cameraEye_);
        }
        DrawDecisionDetail(scheduler);
    }

    void AirportSimUI::DrawHud(TimeSystem& time, const FlightBoard& flights, const AgentSystem& agents,
                               const DecisionScheduler& scheduler, bool llmConnected)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + 12.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (!ImGui::Begin("##AirportClock", nullptr, kHudFlags))
        {
            ImGui::End();
            return;
        }

        int hh = 0, mm = 0;
        MinutesToHHMM(time.DayMinutes(), hh, mm);
        ImGui::TextColored(ImVec4(0.78f, 0.88f, 1.00f, 1.0f), "Day %d  %02d:%02d", time.DayIndex() + 1, hh, mm);
        ImGui::SameLine();
        ImGui::TextDisabled(time.IsNight() ? "[夜]" : "[昼]");

        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("速度", &time.TimeScaleRef(), Config::kMinTimeScale, Config::kMaxTimeScale, "%.1f min/s");
        ImGui::SameLine();
        ImGui::Checkbox("暂停", &time.PausedRef());

        int passengers = 0, staff = 0;
        for (const auto& agent : agents.Agents())
        {
            if (!agent.active)
            {
                continue;
            }
            (agent.role == EAgentRole::Passenger ? passengers : staff)++;
        }
        ImGui::TextDisabled("旅客 %d  员工 %d  航班 %zu", passengers, staff, flights.Flights().size());
        ImGui::TextDisabled("LLM %s%s  决策 %d (规则 %d)", llmConnected ? "在线" : "离线",
                            scheduler.InFlight() ? "·思考中" : "", scheduler.DecisionsMade(),
                            scheduler.FallbacksUsed());
        ImGui::TextDisabled("点击角色查看状态 · 点击空白或 Esc 取消 · F8 调试");
        ImGui::End();
    }

    void AirportSimUI::DrawFlightBoardHud(const FlightBoard& flights)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f, viewport->WorkPos.y + 12.0f),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (!ImGui::Begin("##AirportFids", nullptr, kHudFlags))
        {
            ImGui::End();
            return;
        }
        ImGui::TextColored(ImVec4(0.78f, 0.88f, 1.00f, 1.0f), "DEPARTURES");
        ImGui::Separator();
        if (ImGui::BeginTable("##fids", 5, ImGuiTableFlags_SizingFixedFit))
        {
            for (const auto& flight : flights.Flights())
            {
                int hh = 0, mm = 0;
                MinutesToHHMM(flight.departMinutes, hh, mm);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(flight.number.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%02d:%02d", hh, mm);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(flight.gatePoi.c_str() + 5); // 只显示编号
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(FlightStateColor(flight.state), "%s", FlightStateName(flight.state));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("%d/%d", flight.paxBoarded, flight.paxTotal);
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }

    void AirportSimUI::DrawAgentPanel(const FlightBoard& flights, AgentSystem& agents,
                                      const DecisionScheduler& scheduler)
    {
        if (state_.followAgentId < 0)
        {
            return;
        }

        FAgent* agent = agents.FindById(state_.followAgentId);
        if (agent == nullptr)
        {
            state_.followAgentId = -1;
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + viewport->WorkSize.y - 12.0f),
            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(390.0f, 430.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);

        bool open = true;
        if (!ImGui::Begin("角色状态", &open,
                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing))
        {
            ImGui::End();
            if (!open)
            {
                state_.followAgentId = -1;
            }
            return;
        }

        const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button("< 上一位", ImVec2(buttonWidth, 0.0f)))
        {
            SwitchFollowAgent(agents, state_.followAgentId, -1);
        }
        ImGui::SameLine();
        if (ImGui::Button("下一位 >", ImVec2(buttonWidth, 0.0f)))
        {
            SwitchFollowAgent(agents, state_.followAgentId, 1);
        }

        agent = agents.FindById(state_.followAgentId);
        if (agent == nullptr)
        {
            ImGui::End();
            state_.followAgentId = -1;
            return;
        }

        ImGui::Spacing();
        ImGui::TextColored(ImColor(ColorToImU32(agent->color)), "%s", agent->name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("#%d", agent->id);
        ImGui::Separator();

        if (ImGui::BeginTable("##agentStatus", 2, ImGuiTableFlags_SizingStretchProp))
        {
            auto row = [](const char* label, const std::string& value)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", value.c_str());
            };

            row("职业", RoleLabelZh(agent->role));
            row("性格", agent->personality.empty() ? "-" : agent->personality);
            row("情绪", fmt::format("{} {}", MoodIcon(agent->mood), MoodLabelZh(agent->mood)));
            row("状态", agent->role == EAgentRole::Passenger ? PassengerStateName(agent->pstate)
                                                              : StaffStateName(agent->sstate));
            row("行动", agent->moving ? "移动中" : (agent->anim == EAgentAnimHint::Sit ? "坐下" : "停留"));

            std::string target = agent->targetPoi;
            if (target.empty())
            {
                target = !agent->queueId.empty() ? agent->queueId : agent->postPoi;
            }
            row("目标", target.empty() ? "-" : target);
            row("位置", fmt::format("X {:.1f}  Z {:.1f}", agent->position.x, agent->position.z));

            if (agent->role == EAgentRole::Passenger && agent->flightIdx >= 0 &&
                agent->flightIdx < static_cast<int>(flights.Flights().size()))
            {
                const FFlight& flight = flights.Flights()[static_cast<size_t>(agent->flightIdx)];
                row("航班", fmt::format("{} · {} · {}", flight.number, flight.gatePoi,
                                        FlightStateName(flight.state)));
            }
            else if (agent->role != EAgentRole::Passenger)
            {
                row("班次", ShiftName(agent->shift));
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.78f, 0.88f, 1.00f, 1.0f), "决策流水");
        ImGui::Separator();
        if (agent->decisionPending)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "正在思考... %s",
                               FormatLatency(scheduler.InFlightElapsedMs()).c_str());
        }
        DrawDecisionHistory(scheduler, agent->id, "##agentDecisionHistory");
        ImGui::End();

        if (!open)
        {
            state_.followAgentId = -1;
        }
    }

    void AirportSimUI::DrawDebugPanel(double gameMinutes, TimeSystem& time, AgentSystem& agents,
                                      const QueueSystem& queues, const DecisionScheduler& scheduler)
    {
        ImGui::SetNextWindowSize(ImVec2(520.0f, 460.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("AirportSim 调试 (F8)", &state_.showDebugPanel))
        {
            ImGui::End();
            return;
        }

        ImGui::Checkbox("名牌/气泡", &state_.showOverlay);
        ImGui::SameLine();
        ImGui::Checkbox("POI 标记", &state_.showPoiMarkers);
        ImGui::SameLine();
        ImGui::Checkbox("LLM 决策", &state_.llmEnabled);
        if (ImGui::Button("跳 1 小时"))
        {
            time.Skip(60.0);
        }
        ImGui::SameLine();
        if (state_.followAgentId >= 0 && ImGui::Button("取消跟踪"))
        {
            state_.followAgentId = -1;
        }

        if (ImGui::BeginTabBar("##dbgTabs"))
        {
            if (ImGui::BeginTabItem("角色"))
            {
                if (ImGui::BeginTable("##agents", 5,
                                      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("姓名", ImGuiTableColumnFlags_WidthFixed, 84.0f);
                    ImGui::TableSetupColumn("职业", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                    ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthFixed, 104.0f);
                    ImGui::TableSetupColumn("情绪", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                    ImGui::TableSetupColumn("目标", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (const auto& agent : agents.Agents())
                    {
                        if (!agent.active)
                        {
                            continue;
                        }
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::PushID(agent.id);
                        if (ImGui::Selectable(agent.name.c_str(), state_.followAgentId == agent.id))
                        {
                            state_.followAgentId = agent.id; // 点击 → 相机跟踪
                        }
                        ImGui::PopID();
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(ImColor(ColorToImU32(agent.color)), "%s", RoleLabelZh(agent.role));
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(agent.role == EAgentRole::Passenger
                                                   ? PassengerStateName(agent.pstate)
                                                   : StaffStateName(agent.sstate));
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(MoodName(agent.mood));
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextDisabled("%s%s", agent.targetPoi.empty() ? agent.queueId.c_str()
                                                                            : agent.targetPoi.c_str(),
                                            agent.decisionPending ? " (思考中)" : "");
                    }
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("队列"))
            {
                for (const auto& q : queues.Queues())
                {
                    ImGui::Text("%-12s %s 长度 %zu%s", q.id.c_str(), q.staffed ? "[开]" : "[关]", q.agents.size(),
                                q.serving ? " 服务中" : "");
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("决策日志"))
            {
                if (scheduler.InFlight())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), "LLM 请求进行中 · %s",
                                       FormatLatency(scheduler.InFlightElapsedMs()).c_str());
                    ImGui::Separator();
                }
                DrawDecisionHistory(scheduler, -1, "##declog");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void AirportSimUI::DrawDecisionHistory(const DecisionScheduler& scheduler, int agentId, const char* childId)
    {
        if (!ImGui::BeginChild(childId, ImVec2(0.0f, 0.0f), true))
        {
            ImGui::EndChild();
            return;
        }

        bool found = false;
        int shown = 0;
        for (auto it = scheduler.Log().rbegin(); it != scheduler.Log().rend(); ++it)
        {
            if (agentId >= 0 && it->agentId != agentId)
            {
                continue;
            }
            if (agentId >= 0 && shown >= 10)
            {
                break;
            }

            found = true;
            ++shown;
            ImGui::PushID(static_cast<int>(it->id));
            const std::string label = fmt::format("[{}] {}{}{}", it->timeLabel,
                                                  agentId < 0 ? fmt::format("{} · ", it->agentName) : "",
                                                  it->summary,
                                                  it->llmAttempted ? "  [详情]" : "");
            if (ImGui::Selectable(label.c_str(), selectedDecisionId_ == it->id,
                                  ImGuiSelectableFlags_AllowDoubleClick))
            {
                selectedDecisionId_ = it->id;
            }
            ImGui::PopID();
        }
        if (!found)
        {
            ImGui::TextDisabled("暂无决策记录");
        }
        ImGui::EndChild();
    }

    void AirportSimUI::DrawDecisionDetail(const DecisionScheduler& scheduler)
    {
        if (selectedDecisionId_ == 0)
        {
            return;
        }

        const DecisionScheduler::FDecisionLogEntry* selected = nullptr;
        for (const auto& entry : scheduler.Log())
        {
            if (entry.id == selectedDecisionId_)
            {
                selected = &entry;
                break;
            }
        }
        if (selected == nullptr)
        {
            selectedDecisionId_ = 0;
            return;
        }

        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(760.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("决策详情", &open, ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::End();
            if (!open)
            {
                selectedDecisionId_ = 0;
            }
            return;
        }

        ImGui::Text("%s · %s", selected->timeLabel.c_str(), selected->agentName.c_str());
        ImGui::TextWrapped("%s", selected->summary.c_str());
        if (selected->elapsedMs >= 0.0)
        {
            ImGui::TextDisabled("耗时 %s · %s", FormatLatency(selected->elapsedMs).c_str(),
                                selected->success ? "成功" : "失败/回退");
        }
        else
        {
            ImGui::TextDisabled("规则决策 · 未调用 LLM");
        }
        ImGui::Separator();

        if (ImGui::BeginTabBar("##decisionDetailTabs"))
        {
            if (ImGui::BeginTabItem("Prompt"))
            {
                if (ImGui::BeginChild("##decisionPrompt", ImVec2(0.0f, 0.0f), true,
                                      ImGuiWindowFlags_HorizontalScrollbar))
                {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(selected->prompt.empty() ? "（该记录没有 Prompt）"
                                                                    : selected->prompt.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Response"))
            {
                if (ImGui::BeginChild("##decisionResponse", ImVec2(0.0f, 0.0f), true,
                                      ImGuiWindowFlags_HorizontalScrollbar))
                {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(selected->response.empty() ? "（服务未返回响应）"
                                                                      : selected->response.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();

        if (!open)
        {
            selectedDecisionId_ = 0;
        }
    }

    void AirportSimUI::DrawWorldOverlay(const glm::mat4& viewProjection, double gameMinutes,
                                        const AgentSystem& agents, const AirportMap& map, const glm::vec3& cameraEye)
    {
        const ImVec2 vpSize = ImGui::GetMainViewport()->Size;
        const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
        if (vpSize.x <= 1.0f || vpSize.y <= 1.0f)
        {
            return;
        }
        auto* drawList = ImGui::GetBackgroundDrawList();
        ImVec2 screen;

        if (state_.showPoiMarkers)
        {
            for (const auto& poi : map.Points())
            {
                if (ProjectWorld(viewProjection, vpPos, vpSize, poi.worldPos + glm::vec3(0.0f, 0.6f, 0.0f), screen))
                {
                    drawList->AddCircleFilled(screen, 3.0f, IM_COL32(255, 210, 80, 220));
                    drawList->AddText(ImVec2(screen.x + 5.0f, screen.y - 6.0f), IM_COL32(255, 210, 80, 220),
                                      poi.name.c_str());
                }
            }
        }

        // 气泡按距相机排序，同屏上限 8（§7.5）。
        struct FBubbleEntry
        {
            const FAgent* agent;
            float dist;
        };
        std::vector<FBubbleEntry> bubbles;

        for (const auto& agent : agents.Agents())
        {
            if (!agent.active)
            {
                continue;
            }
            if (!ProjectWorld(viewProjection, vpPos, vpSize, agent.position + glm::vec3(0.0f, 2.1f, 0.0f), screen))
            {
                continue;
            }
            // 名牌 + 情绪图标。
            const std::string tag = fmt::format("{}{}", agent.name, MoodIcon(agent.mood));
            const ImVec2 tagSize = ImGui::CalcTextSize(tag.c_str());
            if (agent.id == state_.followAgentId)
            {
                drawList->AddCircle(screen, 12.0f, IM_COL32(255, 220, 80, 255), 0, 2.5f);
            }
            drawList->AddText(ImVec2(screen.x - tagSize.x * 0.5f, screen.y - tagSize.y),
                              ColorToImU32(agent.color), tag.c_str());

            if (!agent.bubbleText.empty() && gameMinutes < agent.bubbleUntil)
            {
                bubbles.push_back({&agent, glm::distance(cameraEye, agent.position)});
            }
        }

        std::sort(bubbles.begin(), bubbles.end(),
                  [](const FBubbleEntry& a, const FBubbleEntry& b) { return a.dist < b.dist; });
        if (bubbles.size() > static_cast<size_t>(Config::kMaxVisibleBubbles))
        {
            bubbles.resize(static_cast<size_t>(Config::kMaxVisibleBubbles));
        }

        for (const auto& entry : bubbles)
        {
            const FAgent& agent = *entry.agent;
            if (!ProjectWorld(viewProjection, vpPos, vpSize, agent.position + glm::vec3(0.0f, 2.1f, 0.0f), screen))
            {
                continue;
            }
            const double remain = agent.bubbleUntil - gameMinutes;
            const float alphaScale =
                static_cast<float>(std::clamp(remain / (Config::kBubbleDurationMinutes * 0.4), 0.0, 1.0));
            const auto alpha = [alphaScale](int v) { return static_cast<int>(static_cast<float>(v) * alphaScale); };

            constexpr float maxWidth = 220.0f;
            const ImVec2 padding(7.0f, 4.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(agent.bubbleText.c_str(), nullptr, false, maxWidth);
            const ImVec2 textPos(screen.x - textSize.x * 0.5f, screen.y + 6.0f);
            drawList->AddRectFilled(ImVec2(textPos.x - padding.x, textPos.y - padding.y),
                                    ImVec2(textPos.x + textSize.x + padding.x, textPos.y + textSize.y + padding.y),
                                    IM_COL32(20, 24, 28, alpha(220)), 6.0f);
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, IM_COL32(255, 255, 255, alpha(245)),
                              agent.bubbleText.c_str(), nullptr, maxWidth);
        }
    }
}
