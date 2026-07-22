#include "Engine/Common/CoreMinimal.hpp"

#include "TruckHud.hpp"

#include <imgui.h>

#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"

namespace TruckerDemo
{
    namespace
    {
        void DrawGauge(ImDrawList* drawList, const ImVec2 center, float radius, float ratio,
                       const char* label, const char* value, ImU32 color)
        {
            constexpr float start = glm::radians(135.0f);
            constexpr float sweep = glm::radians(270.0f);
            drawList->AddCircleFilled(center, radius, IM_COL32(9, 14, 18, 220), 48);
            drawList->AddCircle(center, radius - 2.0f, IM_COL32(115, 130, 140, 210), 48, 2.0f);
            for (int tick = 0; tick <= 10; ++tick)
            {
                const float angle = start + sweep * static_cast<float>(tick) / 10.0f;
                const ImVec2 direction(std::cos(angle), std::sin(angle));
                drawList->AddLine(center + direction * (radius - 12.0f), center + direction * (radius - 5.0f),
                                  IM_COL32(210, 220, 225, 220), 1.5f);
            }
            const float needle = start + sweep * glm::clamp(ratio, 0.0f, 1.0f);
            drawList->AddLine(center, center + ImVec2(std::cos(needle), std::sin(needle)) * (radius - 17.0f), color, 3.0f);
            const ImVec2 valueSize = ImGui::CalcTextSize(value);
            drawList->AddText({center.x - valueSize.x * 0.5f, center.y + 12.0f}, IM_COL32_WHITE, value);
            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            drawList->AddText({center.x - labelSize.x * 0.5f, center.y + 31.0f}, IM_COL32(160, 175, 185, 255), label);
        }

        void DrawBar(ImDrawList* drawList, ImVec2 min, ImVec2 max, float ratio, ImU32 color, const char* label)
        {
            drawList->AddRectFilled(min, max, IM_COL32(12, 18, 22, 220), 4.0f);
            const float fillTop = max.y - (max.y - min.y) * glm::clamp(ratio, 0.0f, 1.0f);
            drawList->AddRectFilled({min.x + 3.0f, fillTop}, {max.x - 3.0f, max.y - 3.0f}, color, 3.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            drawList->AddText({(min.x + max.x - textSize.x) * 0.5f, max.y + 5.0f}, IM_COL32_WHITE, label);
        }
    }

    FTruckHudActions FTruckHud::Draw(const NextGameInstanceBase& game, const FTruckHudState& state) const
    {
        FTruckHudActions actions;
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
        const float bottom = viewport->Pos.y + viewport->Size.y;
        const float left = viewport->Pos.x;
        const float right = viewport->Pos.x + viewport->Size.x;

        char speed[32]; fmt::format_to_n(speed, sizeof(speed) - 1, "{:.0f}", state.speedKmh).out[0] = '\0';
        char rpm[32]; fmt::format_to_n(rpm, sizeof(rpm) - 1, "{:.0f}", state.rpm).out[0] = '\0';
        DrawGauge(drawList, {left + 92.0f, bottom - 95.0f}, 72.0f, state.speedKmh / 80.0f, "KM/H", speed,
                  IM_COL32(80, 205, 255, 255));
        DrawGauge(drawList, {left + 240.0f, bottom - 95.0f}, 72.0f, state.rpm / std::max(1.0f, state.maxRPM), "RPM", rpm,
                  state.rpm > state.maxRPM * 0.88f ? IM_COL32(255, 70, 50, 255) : IM_COL32(255, 190, 55, 255));

        const char* gear = state.gear < 0 ? "R" : state.gear == 0 ? "N" : nullptr;
        char gearNumber[8];
        if (!gear) { fmt::format_to_n(gearNumber, sizeof(gearNumber) - 1, "{}", state.gear).out[0] = '\0'; gear = gearNumber; }
        drawList->AddText(nullptr, 38.0f, {left + 310.0f, bottom - 125.0f}, IM_COL32_WHITE, gear);

        DrawBar(drawList, {right - 150.0f, bottom - 172.0f}, {right - 120.0f, bottom - 42.0f}, state.throttle,
                IM_COL32(75, 215, 100, 255), "GAS");
        DrawBar(drawList, {right - 105.0f, bottom - 172.0f}, {right - 75.0f, bottom - 42.0f}, state.brake,
                IM_COL32(240, 80, 70, 255), "BRK");
        DrawBar(drawList, {right - 60.0f, bottom - 172.0f}, {right - 30.0f, bottom - 42.0f},
                state.fuel / std::max(1.0f, state.fuelCapacity), state.fuel < state.fuelCapacity * 0.15f
                    ? IM_COL32(255, 70, 45, 255) : IM_COL32(245, 190, 55, 255), "FUEL");

        ImGui::SetNextWindowPos({viewport->GetCenter().x, viewport->Pos.y + 14.0f}, ImGuiCond_Always, {0.5f, 0.0f});
        ImGui::SetNextWindowBgAlpha(0.74f);
        ImGui::Begin("##truck_status", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration |
                                                  ImGuiWindowFlags_NoInputs);
        ImGui::Text("%s  |  %s  |  slope %+.1f deg  |  target %.0fm", state.mission, state.surface,
                    state.slopeDegrees, state.targetDistance);
        ImGui::SameLine(); ImGui::TextColored(state.handbrake ? ImVec4(1, .25f, .2f, 1) : ImVec4(.3f, .35f, .38f, 1), " PARK");
        ImGui::SameLine(); ImGui::TextColored(state.diffLock ? ImVec4(1, .75f, .15f, 1) : ImVec4(.3f, .35f, .38f, 1), " DIFF");
        ImGui::SameLine(); ImGui::TextColored(state.allWheelDrive ? ImVec4(.2f, .85f, 1, 1) : ImVec4(.3f, .35f, .38f, 1), " 6x6");
        ImGui::End();

        ImVec2 targetScreen;
        if (!state.missionAvailable && !state.missionComplete &&
            Runtime::EngineHelper::TryProjectWorldToScreenForGame(game, state.targetPosition + glm::vec3(0, 2.5f, 0), targetScreen))
        {
            drawList->AddCircleFilled(targetScreen, 13.0f, IM_COL32(255, 190, 35, 80), 24);
            drawList->AddCircle(targetScreen, 13.0f, IM_COL32(255, 220, 80, 255), 24, 3.0f);
            const std::string distance = fmt::format("{:.0f} m", state.targetDistance);
            drawList->AddText({targetScreen.x + 18.0f, targetScreen.y - 8.0f}, IM_COL32_WHITE, distance.c_str());
        }

        if (state.recoverySuggested)
            drawList->AddText(nullptr, 24.0f, {viewport->GetCenter().x - 170.0f, bottom - 220.0f},
                              IM_COL32(255, 125, 70, 255), "STUCK / ROLLED OVER - PRESS R TO RECOVER");

        if (state.missionPanel || state.missionComplete)
        {
            ImGui::SetNextWindowPos({right - 330.0f, viewport->Pos.y + 62.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({310.0f, 0.0f});
            ImGui::Begin("JOB BOARD", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
            if (state.missionComplete)
            {
                ImGui::TextColored({.25f, 1, .4f, 1}, "DELIVERY COMPLETE");
                ImGui::Text("Reward  $2,400");
                ImGui::Text("Time    %.1f s", state.elapsed);
                if (ImGui::Button("Take another job", {-1, 0})) actions.restartMission = true;
            }
            else
            {
                ImGui::Text("Overhill Supply Run");
                ImGui::Separator();
                ImGui::TextWrapped("Garage -> West Camp | Cargo: machine parts");
                ImGui::Text("Distance  ~70 m");
                ImGui::Text("Reward    $2,400");
                if (state.missionAvailable)
                {
                    if (ImGui::Button("Accept contract", {-1, 0})) actions.acceptMission = true;
                    ImGui::TextDisabled("Or press F to accept");
                }
                else
                {
                    ImGui::TextColored({.3f, .9f, 1, 1}, "Contract active");
                }
            }
            ImGui::End();
        }
        return actions;
    }
}
