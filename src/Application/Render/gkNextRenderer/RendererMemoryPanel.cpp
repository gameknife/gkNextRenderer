#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <array>
#include <random>
#include <tuple>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/ScreenShotService.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Editor/ImGuiScaling.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"
#include "Modules/DevTools/UI/DeveloperStatusBar.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Engine/Utilities/AboutDialog.hpp"
#include "Engine/Utilities/ImGui.hpp"

#include <SDL3/SDL_misc.h>
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Vulkan/Allocator.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Application/Common/DemoScenes.hpp"


extern float TitlebarSize;
extern float ModeRailWidth;

namespace RendererMemoryDetail
{
using Utilities::FormatBytes;

float SafeFraction(VkDeviceSize numerator, VkDeviceSize denominator)
{
    return denominator > 0 ? static_cast<float>(static_cast<double>(numerator) / static_cast<double>(denominator)) : 0.0f;
}

void DrawMemoryMetricCard(const char* label, const std::string& value, const std::string& subValue, float width,
                          ImVec4 accentColor)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    constexpr float height = 82.0f;
    const ImVec2 size(width, height);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(pos, pos + size, NextUI::Theme::ColorU32(NextUI::Theme::EColor::Background, 0.86f), 7.0f);
    drawList->AddRect(pos, pos + size, NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, 0.82f), 7.0f);
    drawList->AddRectFilled(pos, ImVec2(pos.x + 3.0f, pos.y + size.y),
                            ImGui::GetColorU32(ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.82f)),
                            7.0f, ImDrawFlags_RoundCornersLeft);

    const ImVec2 pad(14.0f, 10.0f);
    const float textRight = pos.x + width - pad.x;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float valueY = pos.y + pad.y + lineHeight + 8.0f;
    const float subValueY = valueY + lineHeight + 6.0f;
    drawList->AddText(pos + pad, NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextMuted), label);
    drawList->PushClipRect(pos + pad, ImVec2(textRight, pos.y + size.y - pad.y), true);
    drawList->AddText(ImVec2(pos.x + pad.x, valueY), NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text), value.c_str());
    drawList->AddText(ImVec2(pos.x + pad.x, subValueY), NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextDim),
                      subValue.c_str());
    drawList->PopClipRect();
    ImGui::Dummy(size);
}

void DrawRightAlignedText(const std::string& text)
{
    const float columnWidth = ImGui::GetContentRegionAvail().x;
    const float textWidth = ImGui::CalcTextSize(text.c_str()).x;
    if (columnWidth > textWidth)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + columnWidth - textWidth);
    }
    ImGui::TextUnformatted(text.c_str());
}

bool IsAllocationType(const Vulkan::MemoryAllocationStats& allocation, std::string_view prefix)
{
    return std::string_view(allocation.type).starts_with(prefix);
}

void DrawAllocationTypeText(const Vulkan::MemoryAllocationStats& allocation)
{
    const NextUI::Theme::EColor color = allocation.free ? NextUI::Theme::EColor::TextDim
        : (IsAllocationType(allocation, "IMAGE") ? NextUI::Theme::EColor::Blue
                                                 : NextUI::Theme::EColor::Success);
    ImGui::TextColored(NextUI::Theme::Color(color), "%s", allocation.type.empty() ? "UNKNOWN" : allocation.type.c_str());
}

ImVec4 AllocationColor(const Vulkan::MemoryAllocationStats& allocation, float alpha = 1.0f)
{
    NextUI::Theme::EColor color = NextUI::Theme::EColor::TextDim;
    if (allocation.free)
    {
        color = NextUI::Theme::EColor::TextDim;
    }
    else if (IsAllocationType(allocation, "IMAGE"))
    {
        color = NextUI::Theme::EColor::Blue;
    }
    else if (IsAllocationType(allocation, "BUFFER"))
    {
        color = NextUI::Theme::EColor::Success;
    }
    else
    {
        color = NextUI::Theme::EColor::Warning;
    }
    return NextUI::Theme::Color(color, alpha);
}

struct FAllocationTile final
{
    const Vulkan::MemoryBlockStats* block{};
    const Vulkan::MemoryAllocationStats* allocation{};
};

void DrawAllocationTooltip(const FAllocationTile& tile)
{
    if (!ImGui::IsItemHovered())
    {
        return;
    }

    const Vulkan::MemoryAllocationStats& allocation = *tile.allocation;
    const Vulkan::MemoryBlockStats& block = *tile.block;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(allocation.name.empty() ? "(unnamed allocation)" : allocation.name.c_str());
    ImGui::Separator();
    ImGui::Text("Block: Heap %u / Type %u / %s %u",
                block.heapIndex,
                block.memoryTypeIndex,
                block.dedicated ? "Dedicated" : "Block",
                block.blockId);
    ImGui::Text("Type: %s", allocation.type.empty() ? "UNKNOWN" : allocation.type.c_str());
    ImGui::Text("Size: %s", FormatBytes(allocation.sizeBytes).c_str());
    ImGui::Text("Offset: %s", FormatBytes(allocation.offsetBytes).c_str());
    if (!allocation.free)
    {
        ImGui::Text("Usage: 0x%llX", static_cast<unsigned long long>(allocation.usageFlags));
    }
    ImGui::EndTooltip();
    ImGui::PopStyleVar();
}

void DrawAllocationLegendItem(const char* label, ImVec4 color)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 swatchSize(10.0f, 10.0f);
    drawList->AddRectFilled(ImVec2(pos.x, pos.y + (lineHeight - swatchSize.y) * 0.5f),
                            ImVec2(pos.x + swatchSize.x, pos.y + (lineHeight + swatchSize.y) * 0.5f),
                            ImGui::GetColorU32(color), 2.0f);
    ImGui::Dummy(ImVec2(swatchSize.x + 4.0f, lineHeight));
    ImGui::SameLine(0.0f, 2.0f);
    ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", label);
}

void DrawMemoryAllocationTileGrid(const Vulkan::MemoryStatsSnapshot& memoryStats)
{
    std::vector<FAllocationTile> tiles;
    for (const Vulkan::MemoryBlockStats& block : memoryStats.blocks)
    {
        for (const Vulkan::MemoryAllocationStats& allocation : block.allocations)
        {
            tiles.push_back({&block, &allocation});
        }
    }

    std::sort(tiles.begin(), tiles.end(),
              [](const FAllocationTile& lhs, const FAllocationTile& rhs)
              {
                  if (lhs.allocation->sizeBytes != rhs.allocation->sizeBytes)
                  {
                      return lhs.allocation->sizeBytes > rhs.allocation->sizeBytes;
                  }
                  return std::tie(lhs.block->heapIndex, lhs.block->memoryTypeIndex, lhs.block->dedicated,
                                  lhs.block->blockId, lhs.allocation->offsetBytes) <
                      std::tie(rhs.block->heapIndex, rhs.block->memoryTypeIndex, rhs.block->dedicated,
                               rhs.block->blockId, rhs.allocation->offsetBytes);
              });

    if (tiles.empty())
    {
        ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "No allocation ranges in VMA details.");
        return;
    }

    constexpr float tileSize = 14.0f;
    constexpr float tileGap = 5.0f;
    const int columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + tileGap) / (tileSize + tileGap)));
    const int rowCount = static_cast<int>((tiles.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 gridSize(static_cast<float>(columns) * tileSize + static_cast<float>(columns - 1) * tileGap,
                          static_cast<float>(rowCount) * tileSize + static_cast<float>(rowCount - 1) * tileGap);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    uint32_t tileIndex = 0;
    for (const FAllocationTile& tile : tiles)
    {
        const int column = static_cast<int>(tileIndex % static_cast<uint32_t>(columns));
        const int row = static_cast<int>(tileIndex / static_cast<uint32_t>(columns));
        const ImVec2 rectMin(origin.x + static_cast<float>(column) * (tileSize + tileGap),
                             origin.y + static_cast<float>(row) * (tileSize + tileGap));
        const ImVec2 rectMax = rectMin + ImVec2(tileSize, tileSize);
        const Vulkan::MemoryAllocationStats& allocation = *tile.allocation;
        drawList->AddRectFilled(rectMin, rectMax,
                                ImGui::GetColorU32(AllocationColor(allocation, allocation.free ? 0.24f : 0.86f)),
                                3.0f);
        drawList->AddRect(rectMin, rectMax,
                          NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, allocation.free ? 0.22f : 0.44f),
                          3.0f);

        ImGui::SetCursorScreenPos(rectMin);
        ImGui::PushID(static_cast<int>(tileIndex));
        ImGui::InvisibleButton("##AllocationTile", ImVec2(tileSize, tileSize));
        DrawAllocationTooltip(tile);
        ImGui::PopID();
        ++tileIndex;
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(gridSize);
}

void DrawMemoryBlockDetails(const Vulkan::MemoryStatsSnapshot& memoryStats)
{
    if (memoryStats.blocks.empty())
    {
        ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted),
                           "No VMA block details available. Named allocations appear after DeviceMemory::SetName().");
        return;
    }

    DrawMemoryAllocationTileGrid(memoryStats);
}

} // namespace RendererMemoryDetail

void NextRendererGameInstance::DrawMemoryStatisticsPanel(FRendererUiState& uiState)
{
    using namespace RendererMemoryDetail;
    const bool profilerMode = uiState.workMode == EWorkMode::Profile;
    if (!profilerMode && !uiState.memoryStatisticsPanelOpen)
    {
        return;
    }

    bool keepOpen = true;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    constexpr float profilerPanelWidth = 380.0f;
    constexpr float profilerPanelMargin = 12.0f;
    const float profilerLeftEdge = viewport->Pos.x + viewport->Size.x - profilerPanelMargin - profilerPanelWidth;

    float panelWidth;
    float panelHeight;
    ImVec2 panelPos;
    ImVec2 panelPivot;

    if (profilerMode)
    {
        const float gap = 12.0f;
        const float rightEdge = profilerLeftEdge - gap;
        const float leftEdge = viewport->Pos.x + ModeRailWidth + 16.0f;
        panelWidth = std::max(520.0f, rightEdge - leftEdge);
        const float availablePanelHeight = viewport->Size.y - TitlebarSize - 30.0f - 28.0f;
        panelHeight = std::clamp(availablePanelHeight, 480.0f, 800.0f);
        panelPos = ImVec2(rightEdge, viewport->Pos.y + TitlebarSize + profilerPanelMargin);
        panelPivot = ImVec2(1.0f, 0.0f);
    }
    else
    {
        panelWidth = std::clamp(viewport->Size.x - 24.0f, 640.0f, 820.0f);
        const float availablePanelHeight = viewport->Size.y - TitlebarSize - 30.0f - 28.0f;
        panelHeight = std::clamp(availablePanelHeight, 430.0f, 680.0f);
        panelPos = ImVec2(viewport->Pos.x + viewport->Size.x - 16.0f,
                          viewport->Pos.y + viewport->Size.y - 30.0f - 12.0f);
        panelPivot = ImVec2(1.0f, 1.0f);
    }

    const ImVec2 panelSize(panelWidth, panelHeight);

    if (!NextUI::Theme::BeginFloatingPanel("##RendererMemoryStats", ICON_FA_CHART_COLUMN, "Memory Statistics",
                                              &keepOpen, panelPos, panelSize, panelPivot))
    {
        if (!keepOpen)
        {
            uiState.memoryStatisticsPanelOpen = false;
            if (profilerMode)
            {
                uiState.workMode = EWorkMode::Render;
                uiState.lastWorkMode = EWorkMode::Count;
            }
        }
        return;
    }

    NextUI::Theme::BeginInsetPanel("##MemoryStatsBody", ImVec2(0, 0), false, 0, ImVec2(12.0f, 12.0f), 0.0f);

    const Vulkan::MemoryStatsSnapshot memoryStats = GetEngine().GetRenderer().Device().CaptureMemoryStats(true);

    const float vramUsageFraction =
        SafeFraction(memoryStats.deviceLocalUsageBytes, memoryStats.deviceLocalBudgetBytes);
    const float managedFraction =
        SafeFraction(memoryStats.deviceLocalAllocationBytes, memoryStats.deviceLocalBlockBytes);
    const float cardGap = 12.0f;
    const float cardWidth = (ImGui::GetContentRegionAvail().x - cardGap * 2.0f) / 3.0f;
    const ImVec4 vramColor = vramUsageFraction > 0.85f
        ? NextUI::Theme::Color(NextUI::Theme::EColor::Danger)
        : (vramUsageFraction > 0.70f ? NextUI::Theme::Color(NextUI::Theme::EColor::Warning)
                                     : NextUI::Theme::Color(NextUI::Theme::EColor::Blue));

    DrawMemoryMetricCard("VRAM usage",
                         fmt::format("{} / {}", FormatBytes(memoryStats.deviceLocalUsageBytes),
                                     FormatBytes(memoryStats.deviceLocalBudgetBytes)),
                         fmt::format("{:.1f}% of budget", vramUsageFraction * 100.0f),
                         cardWidth, vramColor);
    ImGui::SameLine(0.0f, cardGap);
    DrawMemoryMetricCard("VMA managed",
                         fmt::format("{} / {}", FormatBytes(memoryStats.deviceLocalAllocationBytes),
                                     FormatBytes(memoryStats.deviceLocalBlockBytes)),
                         fmt::format("{:.1f}% committed", managedFraction * 100.0f),
                         cardWidth, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover));
    ImGui::SameLine(0.0f, cardGap);
    DrawMemoryMetricCard("Heaps",
                         fmt::format("{}", memoryStats.heaps.size()),
                         fmt::format("{} total", FormatBytes(memoryStats.totalHeapSizeBytes)),
                         cardWidth, NextUI::Theme::Color(NextUI::Theme::EColor::Success));

    ImGui::Dummy(ImVec2(0.0f, 14.0f));
    NextUI::Theme::DrawProgressBar(vramUsageFraction, vramColor, ImVec2(ImGui::GetContentRegionAvail().x, 9.0f));
    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    NextUI::Theme::DrawThinSeparator();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "Heap summary");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0f, 8.0f));
    if (ImGui::BeginTable("##MemoryHeapTable", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
                              ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Heap", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Budget", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Managed", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Blocks", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableHeadersRow();

        for (const Vulkan::MemoryHeapStats& heap : memoryStats.heaps)
        {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Heap %u", heap.heapIndex);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(NextUI::Theme::Color(heap.deviceLocal
                                   ? NextUI::Theme::EColor::Success
                                   : NextUI::Theme::EColor::TextMuted),
                               "%s", heap.deviceLocal ? "Local" : "Shared");

            ImGui::TableSetColumnIndex(2);
            DrawRightAlignedText(FormatBytes(heap.usageBytes));

            ImGui::TableSetColumnIndex(3);
            DrawRightAlignedText(FormatBytes(heap.budgetBytes));

            ImGui::TableSetColumnIndex(4);
            DrawRightAlignedText(fmt::format("{} / {}", FormatBytes(heap.allocationBytes), FormatBytes(heap.blockBytes)));

            ImGui::TableSetColumnIndex(5);
            DrawRightAlignedText(fmt::format("{}", heap.blockCount));
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0.0f, 12.0f));
    ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "Allocation map");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    DrawAllocationLegendItem("Buffer", NextUI::Theme::Color(NextUI::Theme::EColor::Success, 0.82f));
    ImGui::SameLine(0.0f, 14.0f);
    DrawAllocationLegendItem("Image", NextUI::Theme::Color(NextUI::Theme::EColor::Blue, 0.82f));
    ImGui::SameLine(0.0f, 14.0f);
    DrawAllocationLegendItem("Other", NextUI::Theme::Color(NextUI::Theme::EColor::Warning, 0.82f));
    ImGui::SameLine(0.0f, 14.0f);
    DrawAllocationLegendItem("Free", NextUI::Theme::Color(NextUI::Theme::EColor::TextDim, 0.28f));
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (NextUI::Theme::BeginInsetPanel("##MemoryBlockDetailsPanel", ImVec2(0.0f, 0.0f), true,
                                       ImGuiWindowFlags_AlwaysVerticalScrollbar, ImVec2(12.0f, 10.0f), 0.18f))
    {
        DrawMemoryBlockDetails(memoryStats);
    }
    NextUI::Theme::EndInsetPanel();

    NextUI::Theme::EndInsetPanel();

    NextUI::Theme::EndFloatingPanel();

    if (!keepOpen)
    {
        uiState.memoryStatisticsPanelOpen = false;
        if (profilerMode)
        {
            uiState.workMode = EWorkMode::Render;
            uiState.lastWorkMode = EWorkMode::Count;
        }
    }
}
