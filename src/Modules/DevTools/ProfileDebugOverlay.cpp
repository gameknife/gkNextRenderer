#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/DevTools/ProfileDebugOverlay.hpp"

#include <string>
#include <array>
#include <vector>

#include <fmt/format.h>
#include <imgui.h>

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Engine/Vulkan/Allocator.hpp"
#include "Modules/DevTools/UI/DiagnosticWidgets.hpp"

namespace
{
    using Utilities::FormatBytes;
    using Runtime::DevToolsUI::DrawSectionHeader;
    using Runtime::DevToolsUI::DrawValueRow;
    std::string FormatCount(uint64_t value)
    {
        return Utilities::metricFormatter(static_cast<double>(value), "");
    }

    uint32_t SaturatingSubtract(uint32_t lhs, uint32_t rhs)
    {
        return lhs > rhs ? lhs - rhs : 0;
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

    std::string FormatVisibleOverTotal(uint32_t visibleCount, uint32_t totalCount)
    {
        return fmt::format("{} / {}", FormatCount(visibleCount), FormatCount(totalCount));
    }

    // Triangles discrete LOD removed: what the drawn proxies would have cost at LOD0 minus what
    // they actually cost. Without this the win is invisible -- the Triangles row alone cannot say
    // whether a low count means LOD worked or simply that little was on screen.
    std::string FormatLodSavings(const Assets::GPUDrivenStat& stat)
    {
        if (stat.Lod0TriangleCount == 0)
        {
            return "n/a";
        }
        const uint32_t saved = SaturatingSubtract(stat.Lod0TriangleCount, stat.TriangleCount);
        const double percent = 100.0 * static_cast<double>(saved) / static_cast<double>(stat.Lod0TriangleCount);
        return fmt::format("{} ({:.0f}%)", FormatCount(saved), percent);
    }

    void DrawShadowCascadeStats(const std::array<Assets::GPUDrivenStat, Assets::Scene::kSunShadowCascadeCount>& stats)
    {
        if (!ImGui::BeginTable("##ShadowCascadeStats", 3,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                   ImGuiTableFlags_SizingFixedFit))
        {
            return;
        }

        ImGui::TableSetupColumn("Cascade", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("Tri", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();

        for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
        {
            const auto& stat = stats[cascade];
            const uint32_t visibleDrawCount = SaturatingSubtract(stat.ProcessedCount, stat.CulledCount);
            const uint32_t visibleTriangleCount = SaturatingSubtract(stat.TriangleCount, stat.CulledTriangleCount);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.82f, 1.0f), "C%u", cascade);
            ImGui::TableSetColumnIndex(1);
            const std::string drawText = FormatVisibleOverTotal(visibleDrawCount, stat.ProcessedCount);
            ImGui::TextColored(ImVec4(0.93f, 0.96f, 1.0f, 1.0f), "%s", drawText.c_str());
            ImGui::TableSetColumnIndex(2);
            const std::string triText = FormatVisibleOverTotal(visibleTriangleCount, stat.TriangleCount);
            ImGui::TextColored(ImVec4(0.93f, 0.96f, 1.0f, 1.0f), "%s", triText.c_str());
        }

        ImGui::EndTable();
    }

}

void Runtime::DrawProfileDebugOverlay(NextEngine& engine, const NextUI::Statistics& statistics, float topOffset)
{
    Assets::Scene& scene = engine.GetScene();
    // Smoothed, not raw: these counters are read back without a fence, so the raw values flicker
    // frame to frame even when nothing in the scene changed.
    const auto& gpuDrivenStat = scene.GetSmoothedGpuDrivenStat();
    const auto& shadowGpuDrivenStats = scene.GetShadowGpuDrivenStats();
    const uint32_t visibleDrawCount = SaturatingSubtract(gpuDrivenStat.ProcessedCount, gpuDrivenStat.CulledCount);
    const uint32_t visibleTriangleCount =
        SaturatingSubtract(gpuDrivenStat.TriangleCount, gpuDrivenStat.CulledTriangleCount);

    FNextPhysicsBodyStats physicsStats{};
    if (NextPhysics* physics = engine.GetPhysicsEngine(); physics != nullptr)
    {
        physicsStats = physics->GetBodyStats();
    }

    const Vulkan::MemoryStatsSnapshot memoryStats = engine.GetRenderer().Device().CaptureMemoryStats();

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

    if (ImGui::Begin("Statistics", nullptr, flags))
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
            DrawValueRow("Instances", FormatCount(scene.GetNodeProxies().size()));
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
            DrawValueRow("Draws", FormatVisibleOverTotal(visibleDrawCount, gpuDrivenStat.ProcessedCount));
            DrawValueRow("Culled draws", FormatCount(gpuDrivenStat.CulledCount));
            DrawValueRow("Triangles", FormatVisibleOverTotal(visibleTriangleCount, gpuDrivenStat.TriangleCount));
            DrawValueRow("Culled tris", FormatCount(gpuDrivenStat.CulledTriangleCount));
            DrawValueRow("LOD saved", FormatLodSavings(gpuDrivenStat));
            DrawValueRow("Batches", FormatCount(scene.GetIndirectDrawBatchCount()));
            ImGui::EndTable();
        }

        DrawSectionHeader("Shadow Cascades");
        DrawShadowCascadeStats(shadowGpuDrivenStats);

        DrawSectionHeader("Memory");
        if (BeginStatTable("##MemoryStats"))
        {
            DrawValueRow("VRAM used", fmt::format("{} / {}", FormatBytes(memoryStats.deviceLocalUsageBytes),
                                                  FormatBytes(memoryStats.deviceLocalBudgetBytes)));
            DrawValueRow("VMA managed", fmt::format("{} / {}", FormatBytes(memoryStats.deviceLocalAllocationBytes),
                                                    FormatBytes(memoryStats.deviceLocalBlockBytes)));
            DrawValueRow("All heaps", fmt::format("{} / {}", FormatBytes(memoryStats.totalAllocationBytes),
                                                  FormatBytes(memoryStats.totalBlockBytes)));
            DrawValueRow("Heaps", FormatCount(memoryStats.heaps.size()));
            ImGui::EndTable();
        }

        ImGui::PopStyleVar();
    }
    ImGui::End();
}
