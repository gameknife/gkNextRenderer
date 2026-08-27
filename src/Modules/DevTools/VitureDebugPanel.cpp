#include "Engine/Common/CoreMinimal.hpp"

#include "VitureDebugPanel.hpp"

#include <imgui.h>

#include <array>
#include <cfloat>
#include <cstddef>

namespace DevTools
{
    namespace
    {
        constexpr size_t maxEulerHistorySamples = 240;

        struct FAngleHistory
        {
            std::array<float, maxEulerHistorySamples> samples{};
            size_t next = 0;
            size_t count = 0;
            bool hasLast = false;
            float last = 0.0f;

            void Reset()
            {
                next = 0;
                count = 0;
                hasLast = false;
                last = 0.0f;
            }

            void Push(float value)
            {
                if (hasLast)
                {
                    while (value - last > 180.0f)
                    {
                        value -= 360.0f;
                    }
                    while (value - last < -180.0f)
                    {
                        value += 360.0f;
                    }
                }

                samples[next] = value;
                next = (next + 1) % maxEulerHistorySamples;
                count = std::min(count + 1, maxEulerHistorySamples);
                last = value;
                hasLast = true;
            }

            void CopyChronological(std::array<float, maxEulerHistorySamples>& output) const
            {
                const size_t first = (next + maxEulerHistorySamples - count) % maxEulerHistorySamples;
                for (size_t index = 0; index < count; ++index)
                {
                    output[index] = samples[(first + index) % maxEulerHistorySamples];
                }
            }
        };

        struct FEulerHistory
        {
            FAngleHistory x;
            FAngleHistory y;
            FAngleHistory z;

            void Reset()
            {
                x.Reset();
                y.Reset();
                z.Reset();
            }

            void Push(const glm::vec3& eulerDegrees)
            {
                x.Push(eulerDegrees.x);
                y.Push(eulerDegrees.y);
                z.Push(eulerDegrees.z);
            }
        };

        FEulerHistory eulerHistory;

        void DrawStringValue(const char* label, const std::string_view value)
        {
            ImGui::Text("%s: %.*s", label, static_cast<int>(value.size()), value.data());
        }

        void DrawVectorValue(const char* label, const glm::vec3& value)
        {
            ImGui::Text("%s: %.4f, %.4f, %.4f", label, value.x, value.y, value.z);
        }

        void DrawEulerHistoryPlot(const char* label, const ImVec4& color, const FAngleHistory& history)
        {
            if (history.count == 0)
            {
                return;
            }

            std::array<float, maxEulerHistorySamples> chronological{};
            history.CopyChronological(chronological);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
            ImGui::PlotLines(label, chronological.data(), static_cast<int>(history.count), 0, nullptr,
                             FLT_MAX, FLT_MAX, ImVec2(-1.0f, 52.0f));
            ImGui::PopStyleColor();
        }
    }

    void DrawVitureDebugPanel(bool& visible, const FVitureDebugPanelData& data, const float topOffset)
    {
        if (data.cameraEulerDegrees != nullptr)
        {
            eulerHistory.Push(*data.cameraEulerDegrees);
        }
        else
        {
            eulerHistory.Reset();
        }

        if (!visible)
        {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + topOffset + 12.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("VITURE AR Debug", &visible))
        {
            if (data.tracker != nullptr)
            {
                DrawStringValue("Device", data.tracker->Name());
                DrawStringValue("Status", data.tracker->Status());
            }
            else
            {
                ImGui::TextUnformatted("Device: unavailable");
                ImGui::TextUnformatted("Status: tracker not created");
            }

            ImGui::Separator();
            ImGui::Text("Tracking mode: %s", data.sixDof ? "6DOF" : "3DOF");
            const bool tracking = data.pose != nullptr && data.pose->isTracked;
            const char* poseStability = !tracking ? "no sample" : data.pose->isStable ? "stable" : "unstable";
            ImGui::Text("Pose: %s (%s)", tracking ? "tracked" : "waiting for pose", poseStability);

            if (data.pose != nullptr)
            {
                DrawVectorValue("Position (m)", data.pose->positionMeters);
                ImGui::Text("Orientation (wxyz): %.4f, %.4f, %.4f, %.4f",
                            data.pose->orientation.w,
                            data.pose->orientation.x,
                            data.pose->orientation.y,
                            data.pose->orientation.z);
            }
            else
            {
                ImGui::TextUnformatted("Position: unavailable");
                ImGui::TextUnformatted("Orientation: unavailable");
            }

            ImGui::Separator();
            ImGui::Text("World units / meter: %.4f", data.worldUnitsPerMeter);
            ImGui::Text("Prediction: %.2f ms", data.predictionMs);
            ImGui::Text("Polling: %.0f Hz", data.pollHz);
            ImGui::Text("Smoothing: %.2f Hz", data.smoothingHz);

            ImGui::SeparatorText("Camera Rotation Euler (degrees)");
            if (data.cameraEulerDegrees != nullptr)
            {
                ImGui::Text("X: %.2f    Y: %.2f    Z: %.2f",
                            data.cameraEulerDegrees->x,
                            data.cameraEulerDegrees->y,
                            data.cameraEulerDegrees->z);
                ImGui::TextDisabled("Latest %zu rendered-frame samples", eulerHistory.x.count);
                DrawEulerHistoryPlot("Euler X", ImVec4(0.95f, 0.35f, 0.35f, 1.0f), eulerHistory.x);
                DrawEulerHistoryPlot("Euler Y", ImVec4(0.35f, 0.95f, 0.45f, 1.0f), eulerHistory.y);
                DrawEulerHistoryPlot("Euler Z", ImVec4(0.35f, 0.65f, 1.0f, 1.0f), eulerHistory.z);
            }
            else
            {
                ImGui::TextDisabled("Waiting for a tracked camera pose");
            }

            const bool canRecenter = data.recenter && tracking;
            ImGui::BeginDisabled(!canRecenter);
            if (ImGui::Button("Recenter"))
            {
                data.recenter();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Restart Tracker") && data.restart)
            {
                data.restart();
            }
            ImGui::TextDisabled("R: recenter tracking origin");
        }
        ImGui::End();
    }
}
