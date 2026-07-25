#include "AirportSimUI.h"

#include "AgentSystem.h"
#include "AirportMap.h"
#include "AirportSimConfig.hpp"
#include "AirportSimFormat.hpp"
#include "DecisionScheduler.h"
#include "FlightBoard.h"
#include "QueueSystem.h"
#include "TimeSystem.h"
#include "Gameplay/Sim/AnchorDebugOverlay.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

namespace AirportSim
{
    namespace
    {
        constexpr ImGuiWindowFlags kHudFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                               ImGuiWindowFlags_NoSavedSettings |
                                               ImGuiWindowFlags_NoFocusOnAppearing;

        constexpr ImVec4 kPanelBg{0.035f, 0.105f, 0.145f, 0.94f};
        constexpr ImVec4 kBorder{0.18f, 0.39f, 0.50f, 0.72f};
        constexpr ImVec4 kText{0.91f, 0.96f, 0.98f, 1.0f};
        constexpr ImVec4 kMuted{0.55f, 0.67f, 0.73f, 1.0f};
        constexpr ImVec4 kBlue{0.22f, 0.70f, 0.96f, 1.0f};
        constexpr ImVec4 kGreen{0.31f, 0.85f, 0.57f, 1.0f};
        constexpr ImVec4 kYellow{1.00f, 0.76f, 0.26f, 1.0f};
        constexpr ImVec4 kPurple{0.65f, 0.48f, 0.96f, 1.0f};

        float UiScale()
        {
            const ImVec2 size = ImGui::GetMainViewport()->Size;
            return std::clamp(std::min(size.x / 1920.0f, size.y / 1080.0f), 0.72f, 1.25f);
        }

        void PushProductTheme()
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 13.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, kPanelBg);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.04f, 0.12f, 0.16f, 0.74f));
            ImGui::PushStyleColor(ImGuiCol_Border, kBorder);
            ImGui::PushStyleColor(ImGuiCol_Text, kText);
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, kMuted);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.18f, 0.25f, 0.96f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.09f, 0.31f, 0.43f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.43f, 0.62f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.08f, 0.35f, 0.52f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.10f, 0.44f, 0.64f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.12f, 0.50f, 0.72f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(0.11f, 0.22f, 0.28f, 0.40f));
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.39f, 0.48f, 0.66f));
        }

        void PopProductTheme()
        {
            ImGui::PopStyleColor(13);
            ImGui::PopStyleVar(7);
        }

        bool AccentButton(const char* label, const ImVec2& size, bool active, const ImVec4& accent = kBlue)
        {
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x * 0.45f, accent.y * 0.60f,
                                                              accent.z * 0.70f, 0.96f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x * 0.58f, accent.y * 0.76f,
                                                                     accent.z * 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Border, accent);
            }
            const bool pressed = ImGui::Button(label, size);
            if (active)
            {
                ImGui::PopStyleColor(3);
            }
            return pressed;
        }

        const char* FlightStateLabelZh(EFlightState state)
        {
            switch (state)
            {
            case EFlightState::CheckinOpen: return "办理登机";
            case EFlightState::Boarding:    return "正在登机";
            case EFlightState::Final:       return "最后召集";
            case EFlightState::Departed:    return "已经起飞";
            default:                        return "计划中";
            }
        }

        int GateNumber(const std::string& gatePoi)
        {
            const size_t underscore = gatePoi.find_last_of('_');
            if (underscore == std::string::npos)
            {
                return 0;
            }
            return std::atoi(gatePoi.c_str() + underscore + 1);
        }

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

    }

    void AirportSimUI::Draw(const glm::mat4& viewProjection, double gameMinutes, TimeSystem& time,
                            const FlightBoard& flights, AgentSystem& agents, const AirportMap& map,
                            const QueueSystem& queues, const DecisionScheduler& scheduler, bool llmConnected)
    {
        PushProductTheme();
        DrawHud(time, flights, agents, scheduler, llmConnected);
        DrawAnnouncement(flights);
        DrawFlightBoardHud(flights);
        DrawAgentPanel(flights, agents, scheduler);
        DrawBottomNavigation();
        DrawPlaceholderPanel();
        if (state_.showDebugPanel)
        {
            DrawDebugPanel(gameMinutes, time, agents, queues, scheduler);
        }
        if (state_.showOverlay)
        {
            DrawWorldOverlay(viewProjection, gameMinutes, agents, map, cameraEye_);
        }
        DrawDecisionDetail(scheduler);
        PopProductTheme();
    }

    void AirportSimUI::DrawHud(TimeSystem& time, const FlightBoard& flights, const AgentSystem& agents,
                               const DecisionScheduler& scheduler, bool llmConnected)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float scale = UiScale();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 14.0f * scale, viewport->WorkPos.y + 14.0f * scale),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(550.0f * scale, 198.0f * scale), ImGuiCond_Always);
        if (!ImGui::Begin("##AirportClock", nullptr, kHudFlags))
        {
            ImGui::End();
            return;
        }

        int hh = 0;
        int mm = 0;
        MinutesToHHMM(time.DayMinutes(), hh, mm);
        int passengers = 0;
        int staff = 0;
        int positiveMood = 0;
        for (const auto& agent : agents.Agents())
        {
            if (!agent.active)
            {
                continue;
            }
            if (agent.role == EAgentRole::Passenger)
            {
                ++passengers;
            }
            else
            {
                ++staff;
            }
            positiveMood += agent.mood == EMood::Happy || agent.mood == EMood::Excited ? 1 : 0;
        }
        const int activeAgents = passengers + staff;
        const int satisfaction =
            activeAgents > 0 ? std::clamp(76 + positiveMood * 24 / activeAgents - scheduler.FallbacksUsed(), 68, 98)
                             : 87;
        int boarded = 0;
        for (const auto& flight : flights.Flights())
        {
            boarded += flight.paxBoarded;
        }
        const int revenue = 48200 + passengers * 620 + boarded * 1380 + time.DayIndex() * 12600;

        ImGui::Text("Day %d", time.DayIndex() + 1);
        ImGui::SameLine(90.0f * scale);
        ImGui::TextColored(kBlue, ICON_FA_CLOCK);
        ImGui::SameLine();
        ImGui::Text("%02d:%02d", hh, mm);

        const float controlWidth = 46.0f * scale;
        const float controlsStart = ImGui::GetWindowContentRegionMax().x - controlWidth * 4.0f - 18.0f * scale;
        ImGui::SameLine(controlsStart);
        const bool paused = time.PausedRef();
        if (AccentButton(paused ? ICON_FA_PLAY : ICON_FA_PAUSE, ImVec2(controlWidth, 32.0f * scale), paused))
        {
            time.PausedRef() = !time.PausedRef();
        }
        const float speeds[] = {Config::kDefaultTimeScale, Config::kDefaultTimeScale * 2.0f,
                                Config::kDefaultTimeScale * 4.0f};
        const char* labels[] = {"1x", "2x", "4x"};
        for (int i = 0; i < 3; ++i)
        {
            ImGui::SameLine();
            const bool active = !time.PausedRef() && std::abs(time.TimeScaleRef() - speeds[i]) < 0.1f;
            if (AccentButton(labels[i], ImVec2(controlWidth, 32.0f * scale), active))
            {
                time.TimeScaleRef() = speeds[i];
                time.PausedRef() = false;
            }
        }

        ImGui::Separator();
        const float statsTop = ImGui::GetCursorPosY() + 2.0f * scale;
        const float columnWidth =
            (ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x) / 4.0f;
        const char* icons[] = {ICON_FA_USERS, ICON_FA_USER_TIE, ICON_FA_PLANE_DEPARTURE, ICON_FA_FACE_SMILE};
        const char* summaryLabels[] = {"旅客", "员工", "航班", "满意度"};
        const ImVec4 colors[] = {kBlue, kGreen, kYellow, kPurple};
        const int values[] = {passengers, staff, static_cast<int>(flights.Flights().size()), satisfaction};
        for (int i = 0; i < 4; ++i)
        {
            ImGui::SetCursorPos(ImVec2(14.0f + columnWidth * static_cast<float>(i), statsTop));
            if (i > 0)
            {
                const ImVec2 divider = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(divider.x - 10.0f * scale, divider.y),
                                                    ImVec2(divider.x - 10.0f * scale, divider.y + 48.0f * scale),
                                                    ImGui::ColorConvertFloat4ToU32(kBorder));
            }
            ImGui::TextColored(colors[i], "%s", icons[i]);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", summaryLabels[i]);
            ImGui::SetCursorPosX(14.0f + columnWidth * static_cast<float>(i));
            if (i == 3)
            {
                ImGui::Text("  %d%%", values[i]);
            }
            else
            {
                ImGui::Text("  %d", values[i]);
            }
        }

        ImGui::SetCursorPosY(statsTop + 56.0f * scale);
        ImGui::Separator();
        ImGui::TextColored(kGreen, ICON_FA_SACK_DOLLAR);
        ImGui::SameLine();
        ImGui::Text("收入   ¥ %d", revenue);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 120.0f * scale);
        ImGui::TextColored(llmConnected ? kGreen : kMuted, "AI %s%s", llmConnected ? "在线" : "离线",
                           scheduler.InFlight() ? " · 思考中" : "");
        ImGui::End();
    }

    void AirportSimUI::DrawFlightBoardHud(const FlightBoard& flights)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float scale = UiScale();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 14.0f * scale,
                                      viewport->WorkPos.y + 18.0f * scale),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(370.0f * scale, 520.0f * scale), ImGuiCond_Always);
        if (!ImGui::Begin("##AirportFids", nullptr, kHudFlags))
        {
            ImGui::End();
            return;
        }

        ImGui::SetWindowFontScale(1.14f);
        ImGui::TextColored(kBlue, ICON_FA_PLANE_DEPARTURE);
        ImGui::SameLine();
        ImGui::Text("出发航班");
        ImGui::SetWindowFontScale(0.78f);
        ImGui::SameLine();
        ImGui::TextDisabled("DEPARTURES");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
        ImGui::Separator();
        if (ImGui::BeginTable("##fids", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_BordersInnerH,
                              ImVec2(0.0f, 405.0f * scale)))
        {
            ImGui::TableSetupColumn("航班号", ImGuiTableColumnFlags_WidthFixed, 78.0f * scale);
            ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 64.0f * scale);
            ImGui::TableSetupColumn("登机口", ImGuiTableColumnFlags_WidthFixed, 60.0f * scale);
            ImGui::TableSetupColumn("状态", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& flight : flights.Flights())
            {
                int hh = 0, mm = 0;
                MinutesToHHMM(flight.departMinutes, hh, mm);
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 34.0f * scale);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(FlightStateColor(flight.state), ICON_FA_CIRCLE);
                ImGui::SameLine();
                ImGui::TextUnformatted(flight.number.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%02d:%02d", hh, mm);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%02d", GateNumber(flight.gatePoi));
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(FlightStateColor(flight.state), "%s", FlightStateLabelZh(flight.state));
            }
            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextDisabled("%zu/%zu", flights.Flights().size(), flights.Flights().size());
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 145.0f * scale);
        if (ImGui::Button("查看全部航班  " ICON_FA_ARROW_RIGHT, ImVec2(145.0f * scale, 30.0f * scale)))
        {
            activeNavigation_ = 2;
            toastText_ = "已打开航班运营总览（占位）";
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        ImGui::End();
    }

    void AirportSimUI::DrawAgentPanel(const FlightBoard& flights, AgentSystem& agents,
                                      const DecisionScheduler& scheduler)
    {
        if (state_.followAgentId < 0)
        {
            inspectedAgentId_ = -1;
            return;
        }
        inspectedAgentId_ = state_.followAgentId;

        FAgent* agent = agents.FindById(inspectedAgentId_);
        if (agent == nullptr)
        {
            state_.followAgentId = -1;
            inspectedAgentId_ = -1;
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float scale = UiScale();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 14.0f * scale,
                   viewport->WorkPos.y + viewport->WorkSize.y - 72.0f * scale),
            ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(460.0f * scale, 438.0f * scale), ImGuiCond_Always);
        if (!ImGui::Begin("##AirportAgentCard", nullptr, kHudFlags))
        {
            ImGui::End();
            return;
        }

        const ImVec2 avatarCenter(ImGui::GetCursorScreenPos().x + 25.0f * scale,
                                  ImGui::GetCursorScreenPos().y + 25.0f * scale);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(avatarCenter, 24.0f * scale, IM_COL32(37, 89, 113, 255));
        drawList->AddCircleFilled(ImVec2(avatarCenter.x, avatarCenter.y - 5.0f * scale), 8.0f * scale,
                                  ColorToImU32(agent->color));
        drawList->AddRectFilled(ImVec2(avatarCenter.x - 11.0f * scale, avatarCenter.y + 5.0f * scale),
                                ImVec2(avatarCenter.x + 11.0f * scale, avatarCenter.y + 18.0f * scale),
                                ColorToImU32(agent->color), 7.0f * scale);
        ImGui::Dummy(ImVec2(58.0f * scale, 48.0f * scale));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::SetWindowFontScale(1.12f);
        ImGui::TextUnformatted(agent->name.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("#%d  %s", agent->id, RoleLabelZh(agent->role));
        ImGui::EndGroup();

        const float navigationWidth = 92.0f * scale;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - navigationWidth * 2.0f - 8.0f * scale);
        if (ImGui::Button(ICON_FA_CHEVRON_LEFT " 上一位", ImVec2(navigationWidth, 36.0f * scale)))
        {
            SwitchFollowAgent(agents, inspectedAgentId_, -1);
            state_.followAgentId = inspectedAgentId_;
        }
        ImGui::SameLine();
        if (ImGui::Button("下一位 " ICON_FA_CHEVRON_RIGHT, ImVec2(navigationWidth, 36.0f * scale)))
        {
            SwitchFollowAgent(agents, inspectedAgentId_, 1);
            state_.followAgentId = inspectedAgentId_;
        }

        agent = agents.FindById(inspectedAgentId_);
        if (agent == nullptr)
        {
            ImGui::End();
            return;
        }

        ImGui::Separator();
        const char* tabLabels[] = {"概览", "需求", "行程", "记录"};
        const float tabWidth = (ImGui::GetContentRegionAvail().x - 3.0f * ImGui::GetStyle().ItemSpacing.x) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }
            if (AccentButton(tabLabels[i], ImVec2(tabWidth, 34.0f * scale), activeAgentTab_ == i))
            {
                activeAgentTab_ = i;
            }
        }

        ImGui::Separator();
        if (activeAgentTab_ == 0)
        {
            if (ImGui::BeginTable("##agentStatus", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp,
                                  ImVec2(0.0f, 242.0f * scale)))
            {
                auto row = [](const char* label, const std::string& value, const ImVec4* color = nullptr)
                {
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 31.0f);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", label);
                    ImGui::TableSetColumnIndex(1);
                    if (color != nullptr)
                    {
                        ImGui::TextColored(*color, "%s", value.c_str());
                    }
                    else
                    {
                        ImGui::TextWrapped("%s", value.c_str());
                    }
                };

                row("职业", RoleLabelZh(agent->role));
                row("性格", agent->personality.empty() ? "档案未录入" : agent->personality);
                const std::string mood = fmt::format("{} {}", MoodIcon(agent->mood), MoodLabelZh(agent->mood));
                row("情绪", mood, &kGreen);
                row("状态", agent->role == EAgentRole::Passenger ? PassengerStateName(agent->pstate)
                                                                  : StaffStateName(agent->sstate));
                row("行动", agent->moving ? ICON_FA_PERSON_WALKING " 移动中" : "停留");
                std::string target = agent->targetPoi;
                if (target.empty())
                {
                    target = !agent->queueId.empty() ? agent->queueId : agent->postPoi;
                }
                row("目标", target.empty() ? "等待下一项安排" : target);
                row("位置", fmt::format(ICON_FA_LOCATION_DOT "  X {:.1f}   Z {:.1f}", agent->position.x,
                                        agent->position.z));

                std::string task = "机场自由活动";
                if (agent->role == EAgentRole::Passenger && agent->flightIdx >= 0 &&
                    agent->flightIdx < static_cast<int>(flights.Flights().size()))
                {
                    const FFlight& flight = flights.Flights()[static_cast<size_t>(agent->flightIdx)];
                    task = fmt::format(ICON_FA_PLANE " 前往登机口 {}（{}）", GateNumber(flight.gatePoi),
                                       flight.number);
                }
                else if (agent->role != EAgentRole::Passenger)
                {
                    task = fmt::format("{} · {}", agent->postPoi.empty() ? "岗位待命" : agent->postPoi,
                                       ShiftName(agent->shift));
                }
                row("当前任务", task, &kGreen);
                ImGui::EndTable();
            }
        }
        else if (activeAgentTab_ == 1)
        {
            ImGui::TextColored(kBlue, ICON_FA_FACE_SMILE " 旅客需求");
            ImGui::TextDisabled("需求系统尚未接入，以下为产品占位数据");
            ImGui::ProgressBar(0.78f, ImVec2(-1.0f, 24.0f * scale), "舒适度 78%");
            ImGui::ProgressBar(0.62f, ImVec2(-1.0f, 24.0f * scale), "时间余量 62%");
            ImGui::ProgressBar(0.86f, ImVec2(-1.0f, 24.0f * scale), "服务体验 86%");
        }
        else if (activeAgentTab_ == 2)
        {
            ImGui::TextColored(kYellow, ICON_FA_SUITCASE_ROLLING " 行程追踪");
            if (agent->flightIdx >= 0 && agent->flightIdx < static_cast<int>(flights.Flights().size()))
            {
                const FFlight& flight = flights.Flights()[static_cast<size_t>(agent->flightIdx)];
                int hh = 0, mm = 0;
                MinutesToHHMM(flight.departMinutes, hh, mm);
                ImGui::Text("航班  %s", flight.number.c_str());
                ImGui::Text("登机口  %02d", GateNumber(flight.gatePoi));
                ImGui::Text("计划起飞  %02d:%02d", hh, mm);
                ImGui::TextColored(FlightStateColor(flight.state), "当前状态  %s", FlightStateLabelZh(flight.state));
            }
            else
            {
                ImGui::TextDisabled("该角色暂无关联航班");
            }
        }
        else
        {
            if (agent->decisionPending)
            {
                ImGui::TextColored(kYellow, "正在思考... %s", FormatLatency(scheduler.InFlightElapsedMs()).c_str());
            }
            DrawDecisionHistory(scheduler, agent->id, "##agentDecisionHistory");
        }
        ImGui::End();
    }

    void AirportSimUI::DrawAnnouncement(const FlightBoard& flights)
    {
        const FFlight* announcementFlight = nullptr;
        int announcementPriority = 0;
        for (const auto& flight : flights.Flights())
        {
            int priority = 0;
            switch (flight.state)
            {
            case EFlightState::Final:       priority = 3; break;
            case EFlightState::Boarding:    priority = 2; break;
            case EFlightState::CheckinOpen: priority = 1; break;
            default:                        break;
            }
            if (priority > announcementPriority ||
                (priority == announcementPriority && priority > 0 && announcementFlight != nullptr &&
                 flight.departMinutes < announcementFlight->departMinutes))
            {
                announcementFlight = &flight;
                announcementPriority = priority;
            }
        }
        if (announcementFlight == nullptr)
        {
            return;
        }

        std::string message;
        switch (announcementFlight->state)
        {
        case EFlightState::Final:
            message = fmt::format("{} 航班正在最后召集，请立即前往 {:02d} 号登机口。",
                                  announcementFlight->number, GateNumber(announcementFlight->gatePoi));
            break;
        case EFlightState::Boarding:
            message = fmt::format("{} 航班已在 {:02d} 号登机口开始登机，请旅客准备登机。",
                                  announcementFlight->number, GateNumber(announcementFlight->gatePoi));
            break;
        case EFlightState::CheckinOpen:
        {
            int hh = 0;
            int mm = 0;
            MinutesToHHMM(announcementFlight->departMinutes, hh, mm);
            message = fmt::format("{} 航班现已开放值机，计划 {:02d}:{:02d} 从 {:02d} 号登机口出发。",
                                  announcementFlight->number, hh, mm, GateNumber(announcementFlight->gatePoi));
            break;
        }
        default:
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float scale = UiScale();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 18.0f * scale),
            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(530.0f * scale, 64.0f * scale), ImGuiCond_Always);
        if (!ImGui::Begin("##AirportAnnouncement", nullptr, kHudFlags))
        {
            ImGui::End();
            return;
        }

        ImGui::SetWindowFontScale(1.24f);
        ImGui::TextColored(announcementPriority == 3 ? kYellow : kBlue, ICON_FA_BULLHORN);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 50.0f * scale);
        ImGui::TextDisabled("广播");
        ImGui::End();
    }

    void AirportSimUI::DrawBottomNavigation()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float scale = UiScale();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - 58.0f * scale),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 58.0f * scale), ImGuiCond_Always);
        if (!ImGui::Begin("##AirportBottomNavigation", nullptr, kHudFlags))
        {
            ImGui::End();
            ImGui::PopStyleVar(2);
            return;
        }

        struct FNavigationItem
        {
            const char* label;
            const char* placeholder;
        };
        constexpr FNavigationItem items[] = {
            {ICON_FA_USERS " 旅客", nullptr},
            {ICON_FA_USER_TIE " 员工", "员工排班与岗位管理"},
            {ICON_FA_PLANE_DEPARTURE " 航班", "航班运营总览"},
            {ICON_FA_BUILDING " 设施", "机场设施建设与维护"},
            {ICON_FA_COINS " 财务", "机场财务与经营报表"},
            {ICON_FA_FILE_LINES " 报告", "运营数据报告中心"},
            {ICON_FA_TRIANGLE_EXCLAMATION " 告警", "机场事件与告警中心"},
        };

        for (int i = 0; i < static_cast<int>(std::size(items)); ++i)
        {
            if (i > 0)
            {
                ImGui::SameLine();
            }
            if (AccentButton(items[i].label, ImVec2(92.0f * scale, 34.0f * scale), activeNavigation_ == i,
                             i == 6 ? ImVec4(1.0f, 0.40f, 0.42f, 1.0f) : kBlue))
            {
                activeNavigation_ = i;
                if (items[i].placeholder != nullptr)
                {
                    toastText_ = fmt::format("{}已打开（占位）", items[i].placeholder);
                    toastUntil_ = ImGui::GetTime() + 3.0;
                }
            }
        }

        const float rightControls = 100.0f * scale;
        const float hintStart = std::max(720.0f * scale, viewport->WorkSize.x - 650.0f * scale);
        ImGui::SameLine(hintStart);
        ImGui::BeginChild("##AirportHint", ImVec2(510.0f * scale, 34.0f * scale), true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::TextColored(kYellow, ICON_FA_LIGHTBULB);
        ImGui::SameLine();
        if (ImGui::GetTime() < toastUntil_ && !toastText_.empty())
        {
            ImGui::TextUnformatted(toastText_.c_str());
        }
        else
        {
            ImGui::TextDisabled("提示：点击旅客可查看详情，滚轮缩放视角。");
        }
        ImGui::EndChild();

        ImGui::SameLine(viewport->WorkSize.x - rightControls);
        if (ImGui::Button(ICON_FA_QUESTION, ImVec2(38.0f * scale, 34.0f * scale)))
        {
            toastText_ = "帮助中心尚未接入";
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_GEAR, ImVec2(38.0f * scale, 34.0f * scale)))
        {
            toastText_ = "设置中心尚未接入";
            toastUntil_ = ImGui::GetTime() + 3.0;
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void AirportSimUI::DrawPlaceholderPanel()
    {
        if (activeNavigation_ <= 0)
        {
            return;
        }

        constexpr const char* titles[] = {
            "", "员工管理", "航班运营", "设施建设", "财务中心", "运营报告", "告警中心",
        };
        constexpr const char* descriptions[] = {
            "",
            "排班、岗位分配、培训与员工满意度将在这里管理。",
            "航线计划、登机口分配、延误处置与吞吐分析将在这里管理。",
            "商店、候机区、服务设施的建设升级功能将在这里提供。",
            "收入、成本、预算和现金流功能将在这里提供。",
            "客流、准点率、满意度和设施效率报告将在这里提供。",
            "航班延误、拥堵、设施故障和服务异常将在这里汇总。",
        };

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float scale = UiScale();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(470.0f * scale, 210.0f * scale), ImGuiCond_Always);
        if (!ImGui::Begin("##AirportPlaceholder", nullptr, kHudFlags))
        {
            ImGui::End();
            return;
        }

        ImGui::SetWindowFontScale(1.28f);
        ImGui::TextColored(kBlue, "%s", titles[activeNavigation_]);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", descriptions[activeNavigation_]);
        ImGui::Spacing();
        ImGui::TextDisabled("当前版本提供完整界面入口与交互反馈，业务逻辑将在后续版本接入。");
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 49.0f * scale);
        if (AccentButton("返回机场视图", ImVec2(-1.0f, 34.0f * scale), true))
        {
            activeNavigation_ = 0;
        }
        ImGui::End();
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
        ImGui::Checkbox("调试 POI 点位 (F5)", &state_.showPoiMarkers);
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
            NextGameplay::Sim::DrawAnchorDebugOverlay(viewProjection, map.Points());
        }

        // 气泡按距相机排序，同屏上限 8。
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
            const std::string groupTag =
                agent.IsGroupedPassenger() ? fmt::format(" G{}", agent.groupId) : std::string();
            const std::string tag = fmt::format("{}{}{}", agent.name, groupTag, MoodIcon(agent.mood));
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
