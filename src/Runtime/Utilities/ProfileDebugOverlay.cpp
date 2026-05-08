#include "Common/CoreMinimal.hpp"

#include "Runtime/Utilities/ProfileDebugOverlay.hpp"

#include <string>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include "Assets/Core/Scene.hpp"
#include "Runtime/Editor/UserInterface.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Utilities/Math.hpp"
#include "Vulkan/SyncAndTiming.hpp"

namespace
{
    constexpr int maxCpuTimerRows = 18;

    std::string FormatCount(uint64_t value)
    {
        return Utilities::metricFormatter(static_cast<double>(value), "");
    }

    uint32_t SaturatingSubtract(uint32_t lhs, uint32_t rhs)
    {
        return lhs > rhs ? lhs - rhs : 0;
    }

    void DrawSectionHeader(const char* title)
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.96f, 0.82f, 0.42f, 1.0f), "%s", title);
        ImGui::Separator();
    }

    bool BeginStatTable(const char* id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp))
        {
            return false;
        }

        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 132.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    }

    void DrawValueRow(const char* label, const std::string& value,
                      const ImVec4& valueColor = ImVec4(0.93f, 0.96f, 1.0f, 1.0f))
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(valueColor, "%s", value.c_str());
    }

    void DrawCpuTimers(VulkanGpuTimer* gpuTimer)
    {
        if (gpuTimer == nullptr)
        {
            return;
        }

        const std::vector<VulkanGpuTimer::TimerStat> timers = gpuTimer->FetchAllCpuTimes(3);
        if (timers.empty())
        {
            ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "No CPU timer samples yet");
            return;
        }

        float rootTotalMs = 0.0f;
        for (const auto& timer : timers)
        {
            if (timer.depth == 0)
            {
                rootTotalMs += timer.milliseconds;
            }
        }

        if (ImGui::BeginTable("##CpuProfileTimers", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 58.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            ImGui::TableHeadersRow();

            int rowCount = 0;
            for (const auto& timer : timers)
            {
                if (rowCount >= maxCpuTimerRows)
                {
                    break;
                }

                const float ratio = rootTotalMs > 0.001f ? timer.milliseconds / rootTotalMs : 0.0f;
                const ImVec4 rowColor = timer.depth == 0
                    ? ImVec4(0.93f, 0.96f, 1.0f, 1.0f)
                    : ImVec4(0.72f, 0.76f, 0.82f, 1.0f);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Indent(static_cast<float>(timer.depth) * 12.0f);
                ImGui::TextColored(rowColor, "%s", timer.name.c_str());
                ImGui::Unindent(static_cast<float>(timer.depth) * 12.0f);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(rowColor, "%.2f", timer.milliseconds);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(rowColor, "%.0f", ratio * 100.0f);
                ++rowCount;
            }
            ImGui::EndTable();
        }
    }
}

void Runtime::DrawProfileDebugOverlay(NextEngine& engine, const Statistics& statistics, VulkanGpuTimer* gpuTimer,
                                      float topOffset)
{
    Assets::Scene& scene = engine.GetScene();
    const auto& gpuDrivenStat = scene.GetGpuDrivenStat();
    const uint32_t visibleDrawCount = SaturatingSubtract(gpuDrivenStat.ProcessedCount, gpuDrivenStat.CulledCount);
    const uint32_t visibleTriangleCount =
        SaturatingSubtract(gpuDrivenStat.TriangleCount, gpuDrivenStat.CulledTriangleCount);

    FNextPhysicsBodyStats physicsStats{};
    if (NextPhysics* physics = engine.GetPhysicsEngine(); physics != nullptr)
    {
        physicsStats = physics->GetBodyStats();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float margin = 12.0f;
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x - margin, viewport->Pos.y + topOffset + margin),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.90f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("CPU Profile", nullptr, flags))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));

        DrawSectionHeader("Frame");
        if (BeginStatTable("##FrameStats"))
        {
            DrawValueRow("Frame rate", fmt::format("{:.0f} fps", statistics.FrameRate));
            DrawValueRow("Frame time", fmt::format("{:.2f} ms", statistics.FrameTime));
            DrawValueRow("Engine time", fmt::format("{:.2f} s", engine.GetTime()));
            ImGui::EndTable();
        }

        DrawSectionHeader("Scene");
        if (BeginStatTable("##SceneStats"))
        {
            DrawValueRow("Nodes", FormatCount(scene.Nodes().size()));
            DrawValueRow("Instances", FormatCount(scene.GetNodeProxys().size()));
            DrawValueRow("Models", FormatCount(scene.Models().size()));
            DrawValueRow("Materials", FormatCount(scene.Materials().size()));
            DrawValueRow("Textures", FormatCount(statistics.TextureCount));
            DrawValueRow("Lights", FormatCount(scene.GetLightCount()));
            ImGui::EndTable();
        }

        DrawSectionHeader("Physics");
        if (BeginStatTable("##PhysicsStats"))
        {
            DrawValueRow("Bodies", FormatCount(physicsStats.total));
            DrawValueRow("Dynamic", FormatCount(physicsStats.dynamic));
            DrawValueRow("Kinematic", FormatCount(physicsStats.kinematic));
            DrawValueRow("Static", FormatCount(physicsStats.staticBodies));
            ImGui::EndTable();
        }

        DrawSectionHeader("Render / Cull");
        if (BeginStatTable("##RenderCullStats"))
        {
            DrawValueRow("Draws", fmt::format("{} / {}", FormatCount(visibleDrawCount),
                                              FormatCount(gpuDrivenStat.ProcessedCount)));
            DrawValueRow("Culled draws", FormatCount(gpuDrivenStat.CulledCount));
            DrawValueRow("Triangles", fmt::format("{} / {}", FormatCount(visibleTriangleCount),
                                                  FormatCount(gpuDrivenStat.TriangleCount)));
            DrawValueRow("Culled tris", FormatCount(gpuDrivenStat.CulledTriangleCount));
            DrawValueRow("Batches", FormatCount(scene.GetIndirectDrawBatchCount()));
            ImGui::EndTable();
        }

        DrawSectionHeader("CPU Timers");
        DrawCpuTimers(gpuTimer);

        ImGui::PopStyleVar();
    }
    ImGui::End();
}
