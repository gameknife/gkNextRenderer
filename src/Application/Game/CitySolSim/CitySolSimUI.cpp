#include "CitySolSimUI.h"

#include "CitizenSystem.h"
#include "CitySolSimConfig.hpp"
#include "CityTimeSystem.h"
#include "TrafficSystem.h"
#include "Gameplay/Sim/AnchorDebugOverlay.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include <imgui.h>

namespace CitySolSim
{
    namespace
    {
        constexpr ImGuiWindowFlags kCityHudFlags =
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

        bool ProjectCityPoint(const glm::mat4& viewProjection, const ImGuiViewport& viewport,
                              const glm::vec3& world, ImVec2& outScreen)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.0f || std::abs(ndc.y) > 1.0f)
            {
                return false;
            }
            outScreen = {
                viewport.Pos.x + (ndc.x * 0.5f + 0.5f) * viewport.Size.x,
                viewport.Pos.y + (-ndc.y * 0.5f + 0.5f) * viewport.Size.y,
            };
            return true;
        }

        const std::vector<NextGameplay::Sim::FAnchorDebugPoint>& CityDebugPoints()
        {
            static const std::vector<NextGameplay::Sim::FAnchorDebugPoint> points = []
            {
                std::vector<NextGameplay::Sim::FAnchorDebugPoint> result;
                result.reserve(Config::kHomes.size() + Config::kWorkplaces.size() + Config::kLeisure.size());
                for (const Config::FZone& zone : Config::kHomes)
                {
                    result.push_back({zone.label, "home", zone.position});
                }
                for (const Config::FZone& zone : Config::kWorkplaces)
                {
                    result.push_back({zone.label, "workplace", zone.position});
                }
                for (const Config::FZone& zone : Config::kLeisure)
                {
                    result.push_back({zone.label, "leisure", zone.position});
                }
                return result;
            }();
            return points;
        }
    }

    void CitySolSimUI::Draw(const glm::mat4& viewProjection, const glm::vec3& cameraEye,
                            CityTimeSystem& time, const TrafficSystem& traffic,
                            const CitizenSystem& citizens)
    {
        DrawHud(time, traffic, citizens);
        DrawSelection(traffic, citizens);
        if (state_.showPoiMarkers)
        {
            NextGameplay::Sim::FAnchorDebugDrawConfig config;
            config.labelHeight = 3.0f;
            NextGameplay::Sim::DrawAnchorDebugOverlay(viewProjection, CityDebugPoints(), config);
        }
        if (state_.showCitizenLabels)
        {
            DrawWorldLabels(viewProjection, cameraEye, citizens);
        }
    }

    void CitySolSimUI::DrawHud(CityTimeSystem& time, const TrafficSystem& traffic,
                               const CitizenSystem& citizens)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({viewport->WorkPos.x + 16.0f, viewport->WorkPos.y + 16.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({470.0f, state_.showHelp ? 245.0f : 205.0f}, ImGuiCond_Always);
        if (!ImGui::Begin("CitySolSim 城市控制台", nullptr, kCityHudFlags))
        {
            ImGui::End();
            return;
        }

        const int hours = static_cast<int>(time.DayMinutes() / 60.0);
        const int minutes = static_cast<int>(time.DayMinutes()) % 60;
        ImGui::Text("第 %d 天   %02d:%02d", time.DayIndex() + 1, hours, minutes);
        ImGui::SameLine();
        ImGui::TextColored(time.IsNight() ? ImVec4(0.45f, 0.65f, 1.0f, 1.0f)
                                          : ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                           "%s", time.IsNight() ? "夜间 · 路灯开启" : "白昼 · 日照运行");

        if (ImGui::Button(time.PausedRef() ? "继续" : "暂停"))
        {
            time.PausedRef() = !time.PausedRef();
        }
        const float speeds[] = {Config::kDefaultTimeScale, Config::kDefaultTimeScale * 4.0f,
                                Config::kDefaultTimeScale * 16.0f};
        const char* speedLabels[] = {"1x", "4x", "16x"};
        for (int i = 0; i < 3; ++i)
        {
            ImGui::SameLine();
            const bool active = std::abs(time.TimeScaleRef() - speeds[i]) < 0.1f;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.48f, 0.72f, 1.0f));
            }
            if (ImGui::Button(speedLabels[i]))
            {
                time.TimeScaleRef() = speeds[i];
                time.PausedRef() = false;
            }
            if (active)
            {
                ImGui::PopStyleColor();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("+1 小时"))
        {
            time.Skip(60.0);
        }

        ImGui::Separator();
        ImGui::Text("动态车辆  %d", static_cast<int>(traffic.Vehicles().size()));
        ImGui::SameLine(150.0f);
        ImGui::Text("市民  %d", static_cast<int>(citizens.Citizens().size()));
        ImGui::SameLine(270.0f);
        ImGui::Text("步行中  %d", citizens.MovingCount());
        ImGui::Text("全城车流里程  %.1f km", traffic.TotalDistanceKm());

        if (ImGui::Button("跟随车辆"))
        {
            FollowNextVehicle(traffic);
        }
        ImGui::SameLine();
        if (ImGui::Button("跟随市民"))
        {
            FollowNextCitizen(citizens);
        }
        ImGui::SameLine();
        if (ImGui::Button("城市总览"))
        {
            state_.followVehicleId = -1;
            state_.followCitizenId = -1;
        }
        ImGui::SameLine();
        ImGui::Checkbox("市民名牌", &state_.showCitizenLabels);

        ImGui::Checkbox("调试城市点位 (F5)", &state_.showPoiMarkers);
        ImGui::SameLine();
        ImGui::Checkbox("操作提示", &state_.showHelp);
        if (state_.showHelp)
        {
            ImGui::TextDisabled("方向键平移 · 滚轮缩放 · 点击车辆/市民跟随 · C/V 切换跟随 · R 总览");
        }
        ImGui::End();
    }

    void CitySolSimUI::DrawSelection(const TrafficSystem& traffic, const CitizenSystem& citizens)
    {
        const FVehicle* vehicle = traffic.FindById(state_.followVehicleId);
        const FCitizen* citizen = citizens.FindById(state_.followCitizenId);
        if (vehicle == nullptr && citizen == nullptr)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - 276.0f,
                                 viewport->WorkPos.y + 16.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({260.0f, 126.0f}, ImGuiCond_Always);
        if (ImGui::Begin("观察对象", nullptr, kCityHudFlags))
        {
            if (vehicle != nullptr)
            {
                ImGui::Text("车辆 #%02d", vehicle->id);
                ImGui::Text("类型：%s", VehicleKindLabel(vehicle->kind));
                ImGui::Text("速度：%.0f km/h", vehicle->speed * 3.6f);
                ImGui::Text("里程：%.2f km", vehicle->distanceMeters / 1000.0);
            }
            else if (citizen != nullptr)
            {
                ImGui::Text("市民：%s", citizen->name.c_str());
                ImGui::Text("状态：%s", CitizenActivityLabel(citizen->activity));
                ImGui::Text("目的地：%s", citizen->destination.c_str());
                ImGui::Text("通勤累计：%.0f m", citizen->commuteMeters);
            }
        }
        ImGui::End();
    }

    void CitySolSimUI::DrawWorldLabels(const glm::mat4& viewProjection, const glm::vec3& cameraEye,
                                       const CitizenSystem& citizens) const
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
        for (const FCitizen& citizen : citizens.Citizens())
        {
            const glm::vec3 position = citizens.PositionOf(citizen);
            if (glm::distance(position, cameraEye) > 155.0f && citizen.id != state_.followCitizenId)
            {
                continue;
            }
            ImVec2 screen;
            if (!ProjectCityPoint(viewProjection, *viewport, position + glm::vec3(0.0f, 2.35f, 0.0f), screen))
            {
                continue;
            }
            const bool selected = citizen.id == state_.followCitizenId;
            const ImU32 color = selected ? IM_COL32(255, 220, 80, 255) : IM_COL32(235, 245, 255, 215);
            const ImVec2 textSize = ImGui::CalcTextSize(citizen.name.c_str());
            drawList->AddRectFilled({screen.x - textSize.x * 0.5f - 4.0f, screen.y - 2.0f},
                                    {screen.x + textSize.x * 0.5f + 4.0f, screen.y + textSize.y + 3.0f},
                                    IM_COL32(10, 20, 30, 150), 4.0f);
            drawList->AddText({screen.x - textSize.x * 0.5f, screen.y}, color, citizen.name.c_str());
        }
    }

    void CitySolSimUI::FollowNextVehicle(const TrafficSystem& traffic)
    {
        const auto& vehicles = traffic.Vehicles();
        if (vehicles.empty())
        {
            return;
        }
        const auto it = std::find_if(vehicles.begin(), vehicles.end(),
                                     [this](const FVehicle& item) { return item.id == state_.followVehicleId; });
        state_.followVehicleId = it == vehicles.end() || std::next(it) == vehicles.end()
                                     ? vehicles.front().id : std::next(it)->id;
        state_.followCitizenId = -1;
    }

    void CitySolSimUI::FollowNextCitizen(const CitizenSystem& citizens)
    {
        const auto& people = citizens.Citizens();
        if (people.empty())
        {
            return;
        }
        const auto it = std::find_if(people.begin(), people.end(),
                                     [this](const FCitizen& item) { return item.id == state_.followCitizenId; });
        state_.followCitizenId = it == people.end() || std::next(it) == people.end()
                                     ? people.front().id : std::next(it)->id;
        state_.followVehicleId = -1;
    }
}
