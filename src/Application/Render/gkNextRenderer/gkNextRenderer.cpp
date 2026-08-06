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
#include "Engine/Runtime/Editor/FontLoader.hpp"
#include "Engine/Runtime/Editor/ImGuiScaling.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Vulkan/Allocator.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Application/Common/DemoScenes.hpp"


namespace
{
constexpr uint32_t dropSphereGridSize = 20;
constexpr uint32_t dropSphereCount = dropSphereGridSize * dropSphereGridSize;

uint32_t HashUint(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float HashToUnitFloat(uint32_t value)
{
    return static_cast<float>(HashUint(value) >> 8) * (1.0f / 16777216.0f);
}

glm::vec3 HsvToRgb(float hue, float saturation, float value)
{
    const float scaledHue = hue * 6.0f;
    const int sector = static_cast<int>(std::floor(scaledHue)) % 6;
    const float fraction = scaledHue - std::floor(scaledHue);
    const float low = value * (1.0f - saturation);
    const float falling = value * (1.0f - saturation * fraction);
    const float rising = value * (1.0f - saturation * (1.0f - fraction));

    switch (sector)
    {
    case 0: return {value, rising, low};
    case 1: return {falling, value, low};
    case 2: return {low, value, rising};
    case 3: return {low, falling, value};
    case 4: return {rising, low, value};
    default: return {value, low, falling};
    }
}

std::vector<uint32_t> SelectedNodeIds(Assets::Scene& scene)
{
    std::vector<uint32_t> ids = scene.GetSelectedIds();
    if (ids.empty())
    {
        const uint32_t selectedId = scene.GetSelectedId();
        if (selectedId != static_cast<uint32_t>(-1))
        {
            ids.push_back(selectedId);
        }
    }
    return ids;
}

enum class ESceneListGroup : uint8_t
{
    Procedural = 0,
    Gltf = 1,
    LDraw = 2,
    Other = 3,
};

ESceneListGroup GetSceneListGroup(std::string_view scenePath)
{
    const std::string extension = std::filesystem::path(scenePath).extension().string();
    if (extension == ".proc")
    {
        return ESceneListGroup::Procedural;
    }
    if (extension == ".glb" || extension == ".gltf")
    {
        return ESceneListGroup::Gltf;
    }
    if (extension == ".ldr" || extension == ".mpd")
    {
        return ESceneListGroup::LDraw;
    }
    return ESceneListGroup::Other;
}

const char* GetSceneListGroupLabel(ESceneListGroup group)
{
    switch (group)
    {
    case ESceneListGroup::Procedural:
        return "Procedural";
    case ESceneListGroup::Gltf:
        return "glTF";
    case ESceneListGroup::LDraw:
        return "OMR/LDraw";
    case ESceneListGroup::Other:
    default:
        return "Other";
    }
}

const char* GetPresentModeLabel(VkPresentModeKHR presentMode)
{
    switch (presentMode)
    {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "Immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "Mailbox";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "FIFO Relaxed";
    default:
        return "Unknown";
    }
}

template <typename T>
bool DrawSettingSliderRow(const char* label, ImGuiDataType dataType, T* value,
                          T minValue, T maxValue, const char* format, float dragSpeed,
                          float valueWidth = 84.0f)
{
    ImGui::PushID(label);
    NextUI::Theme::BeginFormRow(label);

    const float sliderWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - valueWidth - 6.0f);
    bool changed = false;

    ImGui::SetNextItemWidth(sliderWidth);
    changed |= ImGui::SliderScalar("##Slider", dataType, value, &minValue, &maxValue, format);

    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(valueWidth);
    changed |= ImGui::DragScalar("##Value", dataType, value, dragSpeed, &minValue, &maxValue, format);

    ImGui::PopID();
    return changed;
}

template <typename DrawControl>
bool DrawSettingRow(const char* label, DrawControl&& drawControl)
{
    ImGui::PushID(label);
    NextUI::Theme::BeginFormRow(label);

    const bool changed = drawControl();
    ImGui::PopID();
    return changed;
}

bool DrawSettingCheckboxRow(const char* label, bool* value)
{
    return DrawSettingRow(label,
                          [value]()
                          {
                              return ImGui::Checkbox("##Value", value);
                          });
}

template <typename DrawComboBody>
bool DrawSettingComboRow(const char* label, const char* preview, DrawComboBody&& drawComboBody)
{
    bool changed = false;
    DrawSettingRow(label,
                   [&]()
                   {
                       ImGui::SetNextItemWidth(-FLT_MIN);
                       if (ImGui::BeginCombo("##Value", preview))
                       {
                           changed = drawComboBody();
                           ImGui::EndCombo();
                       }
                       return changed;
                   });
    return changed;
}

void PushViewportToolbarStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, NextUI::Theme::Color(NextUI::Theme::EColor::Surface, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                          NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                          NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Surface, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.28f));
    ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.0f));
}

void PopViewportToolbarStyle()
{
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);
}

bool DrawFlatViewportButton(
    const char* label, const char* tooltip, bool active, const ImVec2 size)
{
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        active ? NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.28f)
               : NextUI::Theme::Color(NextUI::Theme::EColor::Surface, 0.0f));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        active ? NextUI::Theme::Color(NextUI::Theme::EColor::Text)
               : NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(2);
    NextUI::Theme::DrawTooltip(tooltip);
    return pressed;
}

void PushViewportPopupStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.96f));
    ImGui::PushStyleColor(
        ImGuiCol_Header, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.46f));
    ImGui::PushStyleColor(
        ImGuiCol_HeaderHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.78f));
    ImGui::PushStyleColor(
        ImGuiCol_HeaderActive, NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.26f));
    ImGui::PushStyleColor(
        ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.0f));
}

void PopViewportPopupStyle()
{
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);
}

bool DrawViewportComboOption(const char* label, const bool selected)
{
    return ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, ImVec2(0.0f, 28.0f));
}

std::string FormatBytes(VkDeviceSize bytes)
{
    static constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    size_t unitIndex = 0;
    while (value >= 1024.0 && unitIndex + 1 < (sizeof(units) / sizeof(units[0])))
    {
        value /= 1024.0;
        ++unitIndex;
    }
    return fmt::format("{:.2f} {}", value, units[unitIndex]);
}

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
} // namespace

// should use 1em instead of 1px
constexpr float constTitlebarSize = 44;
constexpr float constTitlebarRightInfoWidth = 0;
constexpr float constIconSize = 64;
constexpr float constPaletteSize = 46;
constexpr float constButtonSize = 36;
constexpr float constBuildBarWidth = 240;
constexpr float constSideBarWidth = 300;
constexpr float constShortcutSize = 10;
constexpr float constModeRailWidth = 56;
constexpr float constModeRailButtonSize = 40;

float TitlebarSize = constTitlebarSize;
float TitlebarRightInfoWidth = constTitlebarRightInfoWidth;
float IconSize = constIconSize;
float PaletteSize = constPaletteSize;
float ButtonSize = constButtonSize;
float BuildBarWidth = constBuildBarWidth;
float SideBarWidth = constSideBarWidth;
float ShortcutSize = constShortcutSize;
float ModeRailWidth = constModeRailWidth;
float ModeRailButtonSize = constModeRailButtonSize;

static void UpdateUiScaledMetrics()
{
    float scale = 1.0f;

    if (Vulkan::SwapChain::UiContentScale() < 1.0f)
    {
        scale *= 0.75f / Vulkan::SwapChain::UiContentScale();
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        const float fontSize = ImGui::GetFontSize();
        if (fontSize > 0.0f)
        {
            constexpr float referenceFontSize = 16.0f;
            scale *= fontSize / referenceFontSize;
        }
    }

    TitlebarSize = constTitlebarSize * scale;
    TitlebarRightInfoWidth = constTitlebarRightInfoWidth * scale;
    IconSize = constIconSize * scale;
    PaletteSize = constPaletteSize * scale;
    ButtonSize = constButtonSize * scale;
    BuildBarWidth = constBuildBarWidth * scale;
    SideBarWidth = constSideBarWidth * scale;
    ShortcutSize = constShortcutSize * scale;
    ModeRailWidth = constModeRailWidth * scale;
    ModeRailButtonSize = constModeRailButtonSize * scale;
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
    AppCommon::RegisterDemoScenes();
    return std::make_unique<NextRendererGameInstance>(config, options, engine);
}

NextRendererGameInstance::NextRendererGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    config.HideTitleBar = true;
}

void NextRendererGameInstance::OnInit()
{
    // Keep the viewport clean on startup. The Stats button remains available in
    // the bottom status bar for sessions that need the diagnostic overlay.
    GetEngine().GetUserSettings().ShowOverlay = false;
    GetEngine().GetShowFlags().DebugCVarPanel = false;

    std::string initializedScene = "CornellBox.proc";
    if (!GOption->SceneName.empty())
    {
        initializedScene = GOption->SceneName;
    }

    const std::filesystem::path initializedPath(initializedScene);
    for (int sceneIndex = 0;
         sceneIndex < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size());
         ++sceneIndex)
    {
        const std::filesystem::path candidatePath(Runtime::Scene::SceneList::AllScenes[sceneIndex]);
        if (candidatePath == initializedPath || candidatePath.filename() == initializedPath.filename())
        {
            GetEngine().GetUserSettings().SceneIndex = sceneIndex;
            break;
        }
    }

    GetEngine().RequestLoadScene({.filename = initializedScene});
}

void NextRendererGameInstance::OnTick(double deltaSeconds)
{
    if (playbackPaused_ && !stepRequested_)
    {
        GetEngine().SetProgressiveRendering(GetEngine().GetUserSettings().ProgressiveRender);
        return;
    }

    const bool moving = modelViewController_.UpdateCamera(10.0f, deltaSeconds);
    GetEngine().SetProgressiveRendering(GetEngine().GetUserSettings().ProgressiveRender && !moving);
    stepRequested_ = false;
}

std::vector<Assets::FMaterial> MatPreparedForAdd;

void NextRendererGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
    std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
{
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0,0,0), 0.2f));
    modelId_ = static_cast<uint32_t>(models.size() - 1);
    
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.2,-0.2,-0.2), glm::vec3(0.2,0.2,0.2)));
    boxModelId_ = static_cast<uint32_t>(models.size() - 1);

    matIds_.clear();

    matIds_.push_back(Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1,1,1)));
    MatPreparedForAdd.push_back(materials.back());
    MatPreparedForAdd.push_back({Assets::Material::Metallic(glm::vec3(0.8,0.8,0.8), 0.1f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Dielectric(1.5f, 0.0f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Mixture(glm::vec3(1.0f, 0.3f, 0.3f), 0.1f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));

    dropSphereMatIds_.clear();
    dropSphereMatIds_.reserve(dropSphereCount);
    dropSphereSequence_ = 0;
    for (uint32_t index = 0; index < dropSphereCount; ++index)
    {
        const float hue = HashToUnitFloat(index ^ 0xa511e9b3u);
        const float saturation = 0.25f + 0.25f * HashToUnitFloat(index ^ 0x63d83595u);
        const float value = 0.55f + 0.43f * HashToUnitFloat(index ^ 0xc2b2ae35u);
        const float roughness = 0.04f + 0.92f * HashToUnitFloat(index ^ 0x27d4eb2fu);
        const glm::vec3 color = HsvToRgb(hue, saturation, value);

        Assets::Material material;
        switch (HashUint(index ^ 0x9e3779b9u) % 5)
        {
        case 0: material = Assets::Material::Lambertian(color); break;
        case 1: material = Assets::Material::Metallic(color, roughness); break;
        case 3: material = Assets::Material::Dielectric(1.5f, 0.0f); break;
        default: material = Assets::Material::Mixture(color, roughness); break;
        }

        materials.push_back({material, fmt::format("dropSphere_{:03}", index)});
        dropSphereMatIds_.push_back(static_cast<uint32_t>(materials.size() - 1));
    }
}

void NextRendererGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();
    modelViewController_.Reset( GetEngine().GetScene().GetRenderCamera() );

    GetEngine().GetScene().PlayAllTracks();
}

void NextRendererGameInstance::OnPreConfigUI()
{
    NextGameInstanceBase::OnPreConfigUI();
}

bool NextRendererGameInstance::OnRenderUI()
{
    FGameUiFrameContext context;
    const auto& swapChain = GetEngine().GetRenderer().SwapChain();
    context.surfaceKind = FGameUiFrameContext::ESurfaceKind::MainWindow;
    context.framebufferExtent = swapChain.OutputExtent();
    context.viewCamera = &GetEngine().GetScene().GetRenderCamera();
    context.allowWindowCommands = true;
    return DrawRendererUi(context, mainUiState_);
}

bool NextRendererGameInstance::OnRenderUI(const FGameUiFrameContext& context)
{
    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::RemoteView)
    {
        return DrawRendererUi(context, GetRemoteUiState(context.sessionId));
    }
    return OnRenderUI();
}

void NextRendererGameInstance::OnRemoteUiSessionClosed(std::string_view sessionId)
{
    remoteUiStates_.erase(std::string(sessionId));
}

NextRendererGameInstance::FRendererUiState& NextRendererGameInstance::GetRemoteUiState(std::string_view sessionId)
{
    return remoteUiStates_[std::string(sessionId)];
}

void NextRendererGameInstance::EnsureUiFonts(FRendererUiState& uiState, const bool allowLoad)
{
    if (!allowLoad)
    {
        uiState.bigFont = mainUiState_.bigFont;
        uiState.titleBarFont = mainUiState_.titleBarFont;
        return;
    }

    if (uiState.bigFont == nullptr)
    {
        uiState.bigFont = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/Roboto-BoldCondensed.ttf",
            .pixelSize = 24.0f,
            .includeChineseFull = false,
            .extraGlyphsUtf8 = "gkNextRenderer",
        });
    }

    if (uiState.titleBarFont == nullptr)
    {
        uiState.titleBarFont = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/Roboto-BoldCondensed.ttf",
            .pixelSize = 18.0f,
            .includeChineseFull = false,
            .extraGlyphsUtf8 = "gkNextRenderer",
        });
    }
}

bool NextRendererGameInstance::DrawRendererUi(const FGameUiFrameContext& context, FRendererUiState& uiState)
{
    if ((isTakingScreenshot_ || isRecordingVideo_) &&
        context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        return true;
    }

    EnsureUiFonts(uiState, context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow);
    UpdateUiScaledMetrics();

    if (uiState.workMode != uiState.lastWorkMode)
    {
        auto& showFlags = GetEngine().GetShowFlags();
        Runtime::Config::UserSettings& userSettings = GetEngine().GetUserSettings();
        switch (uiState.workMode)
        {
        case EWorkMode::Render:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            uiState.memoryStatisticsPanelOpen = false;
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::Detail:
            uiState.showSettings = true;
            uiState.showCheatSheet = false;
            uiState.memoryStatisticsPanelOpen = false;
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::Profile:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            uiState.memoryStatisticsPanelOpen = true;
            userSettings.ShowOverlay = true;
            showFlags.DebugCVarPanel = false;
            break;
        case EWorkMode::CVar:
            uiState.showSettings = false;
            uiState.showCheatSheet = false;
            uiState.memoryStatisticsPanelOpen = false;
            userSettings.ShowOverlay = false;
            showFlags.DebugCVarPanel = true;
            break;
        default: break;
        }
        uiState.lastWorkMode = uiState.workMode;
    }
    else if (uiState.workMode == EWorkMode::CVar &&
             !GetEngine().GetShowFlags().DebugCVarPanel)
    {
        uiState.workMode = EWorkMode::Render;
        uiState.lastWorkMode = EWorkMode::Count;
    }

    DrawTitleBar(context, uiState);
    DrawModeRail(uiState);
    DrawSettings(uiState);
    DrawViewportTopBar(context, uiState);
    DrawViewportCheatSheet(uiState);
    DrawBottomStatusBar(uiState);
    DrawMemoryStatisticsPanel(uiState);

    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow && ImGui::GetCurrentContext() != nullptr)
    {
        auto& swapChain = GetEngine().GetRenderer().SwapChain();
        const auto offset = swapChain.OutputOffset();
        const auto extent = swapChain.OutputExtent();
        const NextUI::Scaling::FViewportRect viewport = NextUI::Scaling::MainFramebufferToImGuiViewport(
            ImVec2(static_cast<float>(offset.x), static_cast<float>(offset.y)),
            ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
        uiState.gizmoController.Draw(GetEngine(),
            glm::vec2(viewport.Position.x, viewport.Position.y),
            glm::vec2(viewport.Size.x, viewport.Size.y));
    }
    if (GOption->ReferenceMode)
    {
        ImGuiIO& io = ImGui::GetIO();
        const auto viewport = io.DisplaySize;
        static constexpr std::array<const char*, 4> rendererNames{
            "SoftwareModern", "SoftwareTracing", "SoftwareModernNoAmbient", "PathTracing"};
        const std::array<ImVec2, rendererNames.size()> labelPositions{
            ImVec2(viewport.x * 0.25f, viewport.y * 0.45f),
            ImVec2(viewport.x * 0.75f, viewport.y * 0.45f),
            ImVec2(viewport.x * 0.25f, viewport.y * 0.95f),
            ImVec2(viewport.x * 0.75f, viewport.y * 0.95f)
        };
        const ImGuiWindowFlags windowFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoFocusOnAppearing;

        for (size_t index = 0; index < rendererNames.size(); ++index)
        {
            ImGui::SetNextWindowPos(labelPositions[index], ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.5f);

            auto windowName = fmt::format("RendererName{}", index);
            if (ImGui::Begin(windowName.c_str(), nullptr, windowFlags))
            {
                ImGui::TextUnformatted(rendererNames[index]);
            }
            ImGui::End();
        }
    }
    return false;
}

void NextRendererGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
    EnsureUiFonts(mainUiState_, true);
}

void NextRendererGameInstance::RequestScreenshot(bool openFolder, const std::string& tag)
{
    if (isTakingScreenshot_ || GetEngine().GetScreenShotService().IsBusy())
    {
        return;
    }

    isTakingScreenshot_ = true;
    Runtime::FScreenShotService::FRequest request;
    request.tag = tag;
    request.onCompleted = [this, openFolder](const std::string&) {
        if (openFolder)
        {
            const std::string folderPath = GetEngine().GetScreenShotService().GetDirectory();
            GetEngine().GetScreenShotService().EnsureDirectory();
            NextRenderer::OSCommand(folderPath.c_str());
        }
        isTakingScreenshot_ = false;
    };

    if (!GetEngine().GetScreenShotService().Request(std::move(request)))
    {
        isTakingScreenshot_ = false;
    }
}

void NextRendererGameInstance::RequestThreeSecondVideo(
    const Runtime::FScreenShotService::EVideoOutputScale outputScale)
{
    if (isTakingScreenshot_ || isRecordingVideo_ || GetEngine().GetScreenShotService().IsBusy())
    {
        return;
    }

    isRecordingVideo_ = true;
    Runtime::FScreenShotService::FThreeSecondVideoRequest request;
    request.format = Runtime::FScreenShotService::EAnimationFormat::Both;
    request.outputScale = outputScale;
    request.onCaptureFinished = [this]()
    {
        isRecordingVideo_ = false;
    };
    request.onCompleted = [](const std::string& path)
    {
        if (path.empty())
        {
            spdlog::error("Three-second video recording failed");
        }
        else
        {
            std::filesystem::path webpPath(path);
            webpPath.replace_extension(".webp");
            spdlog::info("Three-second GIF/WebP saved: {} and {}", path, webpPath.string());
        }
    };

    if (!GetEngine().GetScreenShotService().RequestThreeSecondVideo(std::move(request)))
    {
        isRecordingVideo_ = false;
    }
}

void NextRendererGameInstance::DrawVideoCaptureMenuItems()
{
    const auto outputScaleLabel = [](const Runtime::FScreenShotService::EVideoOutputScale outputScale)
    {
        switch (outputScale)
        {
        case Runtime::FScreenShotService::EVideoOutputScale::Half:
            return "50% Swapchain";
        case Runtime::FScreenShotService::EVideoOutputScale::Quarter:
            return "25% Swapchain";
        case Runtime::FScreenShotService::EVideoOutputScale::Full:
        default:
            return "100% Swapchain";
        }
    };

    const std::string outputScaleMenuLabel = fmt::format(
        "Recording size ({})", outputScaleLabel(videoOutputScale_));
    if (ImGui::BeginMenu(outputScaleMenuLabel.c_str()))
    {
        struct FVideoOutputScaleOption
        {
            Runtime::FScreenShotService::EVideoOutputScale scale;
            const char* label;
        };
        static constexpr std::array<FVideoOutputScaleOption, 3> options{{
            {Runtime::FScreenShotService::EVideoOutputScale::Full, "100% Swapchain"},
            {Runtime::FScreenShotService::EVideoOutputScale::Half, "50% Swapchain"},
            {Runtime::FScreenShotService::EVideoOutputScale::Quarter, "25% Swapchain"},
        }};

        for (const FVideoOutputScaleOption& option : options)
        {
            if (ImGui::MenuItem(option.label, nullptr, videoOutputScale_ == option.scale))
            {
                videoOutputScale_ = option.scale;
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Record 3s GIF + Animated WebP"))
    {
        RequestThreeSecondVideo(videoOutputScale_);
    }
}

bool NextRendererGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = modelViewController_.ModelView();
    outRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}

float NextRendererGameInstance::GetGraphicsDebugPanelTopOffset() const
{
    return TitlebarSize;
}

bool NextRendererGameInstance::OnKey(SDL_Event& event)
{
    // WASDQE camera movement (only active when right mouse is pressed)
    modelViewController_.OnKey(event);

    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.key)
        {
        case SDLK_ESCAPE:
            GetEngine().GetScene().SetSelectedId(-1);
            GetEngine().GetShowFlags().ShowEdge = false;
            return true;
        case SDLK_F:
            {
                glm::vec3 focusCenter;
                float radius;
                if (GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, radius))
                {
                    modelViewController_.Focus(focusCenter, radius);
                }
            }
            break;
        case SDLK_SPACE: CreateBoxAndPush(); return true;
        case SDLK_B: DropPhysicsSphereGrid(); return true;
        case SDLK_DELETE:
        case SDLK_BACKSPACE:
        {
            std::vector<uint32_t> ids = SelectedNodeIds(GetEngine().GetScene());
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DeleteNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            return true;
        }
        case SDLK_D:
        {
            if (!(event.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) break;
            std::vector<uint32_t> ids = SelectedNodeIds(GetEngine().GetScene());
            if (ids.empty()) break;
            auto cmd = std::make_unique<Runtime::Command::DuplicateNodesCommand>(GetEngine().GetScene(), std::move(ids));
            GetEngine().GetCommandHistory().Execute(std::move(cmd));
            return true;
        }
        default: break;
        }
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        switch (event.gbutton.button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            CreateSphereAndPush(); return true;
            break;
        default: break;
        }
    }
    return false;
}

bool NextRendererGameInstance::OnCursorPosition(double xpos, double ypos)
{
    // Update Controller Context
    bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    modelViewController_.SetAltPressed(alt);
    
    glm::vec3 center;
    float radius;
    if (GetEngine().GetScene().GetSelectedNodeBounds(center, radius))
    {
        modelViewController_.SetOrbitTarget(center);
    }
    else
    {
        modelViewController_.SetOrbitTarget(std::nullopt);
    }

    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnCursorPosition(xpos, ypos);
    }
    return true;
}

bool NextRendererGameInstance::OnMouseButton(SDL_Event& event)
{
    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnMouseButton(event);
    }
    else
    {
        return true;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        auto mousePos = GetEngine().GetMousePos();
        glm::vec3 org;
        glm::vec3 dir;
        Runtime::EngineHelper::GetScreenToWorldRay(mousePos, org, dir);
        GetEngine().RayCast( org, dir, [this](Assets::RayCastResult result)
        {
            if (result.Hit)
            {
                GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
                Runtime::EngineHelper::DrawAuxPoint( result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2, 60 );
                GetEngine().GetScene().SetSelectedId(result.InstanceId);
                GetEngine().GetShowFlags().ShowEdge = true;
            }
            else
            {
                GetEngine().GetScene().SetSelectedId(-1);
                GetEngine().GetShowFlags().ShowEdge = false;
            }
            return true;
        });
        return true;
    }

    return true;
}

bool NextRendererGameInstance::OnScroll(double xoffset, double yoffset)
{
    if (!mainUiState_.gizmoController.IsInteracting())
    {
        modelViewController_.OnScroll(xoffset, yoffset);
    }
    return true;
}

bool NextRendererGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
    int16_t leftTrigger, int16_t rightTrigger)
{
    return modelViewController_.OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
}

bool NextRendererGameInstance::OnRemoteViewAction(const FRemoteViewActionContext& context, std::string_view action)
{
    if (action != "space")
    {
        return false;
    }

    const std::string shortSession = context.sessionId.substr(0, std::min<size_t>(context.sessionId.size(), 8));
    CreateBoxAndPushFromView(FLaunchView{
        .position = context.position,
        .forward = context.forward,
        .right = context.right,
        .up = context.up,
        .debugName = fmt::format("remoteBox-{}", shortSession.empty() ? "client" : shortSession),
    });
    return true;
}

void NextRendererGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    //std::string error;
    //cvars.SetDefaultFromString("r.upscaler.qualityMode", "4", &error);
}


void NextRendererGameInstance::CreateSphereAndPush()
{
    glm::vec3 forward = modelViewController_.GetForward();
    glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
    glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 100.f;
    glm::vec3 shotDir = normalize((farTarget - center));
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());

    uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    std::shared_ptr<Assets::Node> newNode = Assets::SceneBuilder::CreateRenderNode("temp", center, glm::vec3(1), instanceId, modelId_, newMatId);
    
    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = GetEngine().GetPhysicsEngine()->CreateSphereBody(center, 0.2f, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void NextRendererGameInstance::CreateBoxAndPush()
{
    CreateBoxAndPushFromView(FLaunchView{
        .position = modelViewController_.GetPosition(),
        .forward = modelViewController_.GetForward(),
        .right = modelViewController_.GetRight(),
        .up = modelViewController_.GetUp(),
        .debugName = "tempBox",
    });
}

void NextRendererGameInstance::CreateBoxAndPushFromView(const FLaunchView& view)
{
    if (matIds_.empty())
    {
        SPDLOG_WARN("gkNextRenderer: ignored box launch before dynamic materials are ready");
        return;
    }
    if (boxModelId_ >= GetEngine().GetScene().Models().size())
    {
        SPDLOG_WARN("gkNextRenderer: ignored box launch before dynamic box model is ready");
        return;
    }

    glm::vec3 forward = glm::normalize(view.forward);
    glm::vec3 right = glm::normalize(view.right);
    glm::vec3 up = glm::normalize(view.up);
    glm::vec3 center = view.position + forward * 0.1f + right * 0.5f + up * -0.5f;
    glm::vec3 farTarget = view.position + forward * 1000.0f + up * 200.f;
    glm::vec3 shotDir = glm::normalize((farTarget - center));
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());

    uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    std::shared_ptr<Assets::Node> newNode =
        Assets::SceneBuilder::CreateRenderNode(view.debugName, center, glm::vec3(1), instanceId, boxModelId_, newMatId);
    
    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = GetEngine().GetPhysicsEngine()->CreateBoxBody(center, {0.4,0.4,0.4}, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 100000.f);
}

void NextRendererGameInstance::DropPhysicsSphereGrid()
{
    Assets::Scene& scene = GetEngine().GetScene();
    NextPhysics* physicsEngine = GetEngine().GetPhysicsEngine();
    if (physicsEngine == nullptr)
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop because physics is unavailable");
        return;
    }
    if (modelId_ >= scene.Models().size())
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop before dynamic sphere model is ready");
        return;
    }
    if (dropSphereMatIds_.size() != dropSphereCount)
    {
        SPDLOG_WARN("gkNextRenderer: ignored sphere drop before sphere materials are ready");
        return;
    }

    const glm::vec3 boundsMin = scene.GetSceneAABBMin() * 0.5f;
    const glm::vec3 boundsMax = scene.GetSceneAABBMax() * 0.5f;
    const glm::vec3 boundsSize = glm::max(boundsMax - boundsMin, glm::vec3(0.0f));
    const float fallbackSpan = std::max({boundsSize.x, boundsSize.y * 0.5f, boundsSize.z, 4.0f});
    const float spanX = boundsSize.x > 0.01f ? boundsSize.x : fallbackSpan;
    const float spanZ = boundsSize.z > 0.01f ? boundsSize.z : fallbackSpan;
    const float stepX = spanX / static_cast<float>(dropSphereGridSize);
    const float stepZ = spanZ / static_cast<float>(dropSphereGridSize);
    const float radius = std::max(0.05f, std::min(stepX, stepZ) * 0.3f);
    const float renderScale = radius / 0.2f;
    const float startX = boundsSize.x > 0.01f ? boundsMin.x : (boundsMin.x + boundsMax.x - spanX) * 0.5f;
    const float startZ = boundsSize.z > 0.01f ? boundsMin.z : (boundsMin.z + boundsMax.z - spanZ) * 0.5f;
    const float spawnY = boundsMax.y + radius + std::max(0.05f, boundsSize.y * 0.02f);
    std::vector<uint32_t> shuffledMaterialIds = dropSphereMatIds_;
    std::mt19937 randomEngine(HashUint(0x6d2b79f5u ^ dropSphereSequence_++));
    std::shuffle(shuffledMaterialIds.begin(), shuffledMaterialIds.end(), randomEngine);

    for (uint32_t row = 0; row < dropSphereGridSize; ++row)
    {
        for (uint32_t column = 0; column < dropSphereGridSize; ++column)
        {
            const uint32_t sphereIndex = row * dropSphereGridSize + column;
            const glm::vec3 position{
                startX + (static_cast<float>(column) + 0.5f) * stepX,
                spawnY,
                startZ + (static_cast<float>(row) + 0.5f) * stepZ,
            };
            const uint32_t instanceId = static_cast<uint32_t>(scene.Nodes().size());
            std::shared_ptr<Assets::Node> node = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("dropSphere_{:03}", sphereIndex),
                position,
                glm::vec3(renderScale),
                instanceId,
                modelId_,
                shuffledMaterialIds[sphereIndex]);

            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            physics->BindPhysicsBody(
                physicsEngine->CreateSphereBody(position, radius, NextMotionType::Dynamic));
            node->AddComponent(physics);
            scene.AddNode(std::move(node));
        }
    }

    scene.MarkDirty();
    SPDLOG_INFO("gkNextRenderer: dropped {} physics spheres above scene bounds", dropSphereCount);
}

void NextRendererGameInstance::DrawSettings(FRendererUiState& uiState)
{
    Runtime::Config::UserSettings& userSetting = GetEngine().GetUserSettings();

    if (!uiState.showSettings)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float panelWidth = 360.0f;
    constexpr float panelMargin = 10.0f;
    const ImVec2 panelPos = viewport->Pos + ImVec2(ModeRailWidth + panelMargin, TitlebarSize + panelMargin);
    const ImVec2 panelSize(panelWidth,
                           viewport->Size.y - TitlebarSize - 50.0f - panelMargin);

    if (!NextUI::Theme::BeginFloatingPanel("##RendererSettingsPanel", ICON_FA_SLIDERS,
                                              "Renderer Settings", &uiState.showSettings,
                                              panelPos, panelSize))
    {
        return;
    }

    // Scrollable body
    NextUI::Theme::BeginInsetPanel("##SettingsBody", ImVec2(0, 0), true, 0, ImVec2(10.0f, 10.0f), 0.30f);

    auto DrawFloatSetting = [&](const char* label, float* value, float minValue, float maxValue,
                                const char* format, float dragSpeed)
    {
        return DrawSettingSliderRow(label, ImGuiDataType_Float, value, minValue, maxValue, format, dragSpeed);
    };

    auto DrawIntSetting = [&](const char* label, int* value, int minValue, int maxValue,
                              const char* format = "%d")
    {
        return DrawSettingSliderRow(label, ImGuiDataType_S32, value, minValue, maxValue, format, 1.0f);
    };

    auto DrawFloat3Setting = [&](const char* label, const char* id, glm::vec3* value,
                                 float dragSpeed, float minValue, float maxValue,
                                 const char* format)
    {
        return DrawSettingRow(label,
                              [&]()
                              {
                                  ImGui::SetNextItemWidth(-FLT_MIN);
                                  return ImGui::DragFloat3(
                                      id, &value->x, dragSpeed, minValue, maxValue, format);
                              });
    };

    auto DrawColorSetting = [&](const char* label, const char* id, glm::vec3* value)
    {
        return DrawSettingRow(label,
                              [&]()
                              {
                                  ImGui::SetNextItemWidth(-FLT_MIN);
                                  return ImGui::ColorEdit3(id, &value->x);
                              });
    };

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Renderer"), true))
    {
        DrawSettingRow(LOCTEXT("Renderer"),
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           Runtime::GraphicsDebugPanel::DrawRendererSelector(GetEngine(), userSetting, "##RendererList");
                           return false;
                       });
        DrawSettingRow(LOCTEXT("Present Mode"),
                       [&]()
                       {
                           static constexpr VkPresentModeKHR presentModes[] = {
                               VK_PRESENT_MODE_IMMEDIATE_KHR,
                               VK_PRESENT_MODE_MAILBOX_KHR,
                               VK_PRESENT_MODE_FIFO_KHR,
                               VK_PRESENT_MODE_FIFO_RELAXED_KHR,
                           };

                           int selectedMode = 0;
                           const VkPresentModeKHR requestedPresentMode =
                               static_cast<VkPresentModeKHR>(userSetting.PresentMode);
                           for (int i = 0; i < static_cast<int>(std::size(presentModes)); ++i)
                           {
                               if (presentModes[i] == requestedPresentMode)
                               {
                                   selectedMode = i;
                                   break;
                               }
                           }

                           const char* labels[] = {"Immediate", "Mailbox", "FIFO", "FIFO Relaxed"};
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::Combo("##PresentMode", &selectedMode, labels, IM_ARRAYSIZE(labels)))
                           {
                               const VkPresentModeKHR nextPresentMode = presentModes[selectedMode];
                               userSetting.PresentMode = static_cast<uint32_t>(nextPresentMode);
                               GetEngine().GetRenderer().SetRequestedPresentMode(nextPresentMode);
                               return true;
                           }
                           return false;
                       });
        if (GetEngine().GetRenderer().HasSwapChain())
        {
            const VkPresentModeKHR actualPresentMode = GetEngine().GetRenderer().SwapChain().PresentMode();
            if (actualPresentMode != static_cast<VkPresentModeKHR>(userSetting.PresentMode))
            {
                ImGui::TextDisabled("Actual present mode: %s", GetPresentModeLabel(actualPresentMode));
            }
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Scene"), true))
    {
        std::vector<std::string> sceneNames;
        sceneNames.reserve(Runtime::Scene::SceneList::AllScenes.size());
        for (const auto& scene : Runtime::Scene::SceneList::AllScenes)
        {
            sceneNames.push_back(std::filesystem::path(scene).filename().string());
        }

        const char* currentScenePreview =
            (userSetting.SceneIndex >= 0 && userSetting.SceneIndex < static_cast<int>(sceneNames.size()))
                ? sceneNames[userSetting.SceneIndex].c_str()
                : "";
        DrawSettingComboRow(LOCTEXT("Scene"), currentScenePreview,
                            [&]() -> bool
                            {
                                bool changed = false;
                                ESceneListGroup currentGroup = ESceneListGroup::Other;
                                bool hasGroup = false;
                                for (int sceneIdx = 0; sceneIdx < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size()); ++sceneIdx)
                                {
                                    const ESceneListGroup sceneGroup = GetSceneListGroup(Runtime::Scene::SceneList::AllScenes[sceneIdx]);
                                    if (!hasGroup || sceneGroup != currentGroup)
                                    {
                                        if (hasGroup)
                                        {
                                            ImGui::Separator();
                                        }
                                        currentGroup = sceneGroup;
                                        hasGroup = true;
                                        ImGui::TextDisabled("%s", GetSceneListGroupLabel(sceneGroup));
                                    }

                                    const bool selected = (sceneIdx == userSetting.SceneIndex);
                                    if (ImGui::Selectable(sceneNames[sceneIdx].c_str(), selected))
                                    {
                                        userSetting.SceneIndex = sceneIdx;
                                        GetEngine().RequestLoadScene({.filename = Runtime::Scene::SceneList::AllScenes[userSetting.SceneIndex]});
                                        changed = true;
                                    }
                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                return changed;
                            });
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Camera"), true))
    {
        std::vector<const char*> camerasList;
        for (const auto& cam : GetEngine().GetScene().GetEnvSettings().cameras)
        {
            camerasList.emplace_back(cam.name.c_str());
        }

        const int prevCameraIdx = userSetting.CameraIdx;
        DrawSettingRow(LOCTEXT("Camera"),
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           return ImGui::Combo("##CameraList", &userSetting.CameraIdx, camerasList.data(),
                                               static_cast<int>(camerasList.size()));
                       });
        if (prevCameraIdx != userSetting.CameraIdx)
        {
            GetEngine().GetScene().GetRenderCamera() =
                GetEngine().GetScene().GetEnvSettings().cameras[userSetting.CameraIdx];
            modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
        }

        auto& camera = GetEngine().GetScene().GetRenderCamera();
        DrawFloatSetting(LOCTEXT("Aperture"), &camera.Aperture, 0.0f, 1.0f, "%.2f", 0.01f);
        DrawFloatSetting(LOCTEXT("Focus(cm)"), &camera.FocalDistance, 0.001f, 1000.0f, "%.3f", 0.05f);
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Environment"), true))
    {
        auto& scene = GetEngine().GetScene();
        auto& environment = scene.GetEnvSettings();
        auto& atmosphere = environment.Atmosphere;
        bool environmentChanged = false;

        ImGui::SeparatorText(LOCTEXT("Lighting"));
        environmentChanged |= DrawSettingCheckboxRow(LOCTEXT("HasSky"), &environment.HasSky);
        if (environment.HasSky)
        {
            environmentChanged |= DrawIntSetting(LOCTEXT("SkyIdx"), &environment.SkyIdx, 0, 10);
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SkyRotation"), &environment.SkyRotation, 0.0f, 2.0f, "%.2f", 0.01f);
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SkyLum"), &environment.SkyIntensity, 0.0f, 1000.0f, "%.0f", 1.0f);
            environmentChanged |= DrawColorSetting(
                LOCTEXT("SkyColor"), "##EnvironmentSkyColor", &environment.SkyColor);
        }

        environmentChanged |= DrawSettingCheckboxRow(LOCTEXT("HasSun"), &environment.HasSun);
        if (environment.HasSun)
        {
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SunRotation"), &environment.SunRotation, 0.0f, 2.0f, "%.2f", 0.01f);
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SunLum"), &environment.SunIntensity, 0.0f, 2000.0f, "%.0f", 1.0f);
            environmentChanged |= DrawColorSetting(
                LOCTEXT("SunColor"), "##EnvironmentSunColor", &environment.SunColor);
        }
        DrawFloatSetting(
            LOCTEXT("PaperWhitNit"), &userSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f", 1.0f);

        ImGui::SeparatorText("Atmosphere");
        environmentChanged |= DrawSettingCheckboxRow(
            "Enabled", &environment.AtmosphereEnabled);

        float sunElevationDegrees = glm::degrees(environment.SunElevation);
        if (DrawFloatSetting(
                "Sun Elevation", &sunElevationDegrees, -24.0f, 90.0f, "%.1f deg", 0.25f))
        {
            environment.SunElevation = glm::radians(sunElevationDegrees);
            environmentChanged = true;
        }
        environmentChanged |= DrawFloatSetting(
            "Sky Luminance", &atmosphere.SkyLuminanceScale, 0.0f, 10.0f, "%.2f", 0.01f);
        environmentChanged |= DrawFloatSetting(
            "Sky LUT Scale", &userSetting.AtmosphereSkyViewLutScale,
            0.25f, 2.0f, "%.2fx", 0.05f);

        static constexpr const char* debugModes[] = {
            "Off", "In-Scatter", "Transmittance", "SkyView LUT"};
        environmentChanged |= DrawSettingRow(
            "Debug View",
            [&]()
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                return ImGui::Combo(
                    "##AtmosphereDebug", &userSetting.AtmosphereDebugMode,
                    debugModes, IM_ARRAYSIZE(debugModes));
            });

        ImGui::SeparatorText("Aerial Perspective");
        ImGui::PushID("AerialPerspective");
        environmentChanged |= DrawSettingCheckboxRow(
            "Enabled", &environment.AerialPerspectiveEnabled);
        ImGui::PopID();
        ImGui::BeginDisabled(!environment.AtmosphereEnabled ||
                             !environment.AerialPerspectiveEnabled);
        environmentChanged |= DrawFloatSetting(
            "Max Distance", &atmosphere.AerialPerspectiveMaxDistance,
            10.0f, 50000.0f, "%.0f", 10.0f);
        ImGui::EndDisabled();

        ImGui::SeparatorText("Height Fog");
        ImGui::PushID("HeightFog");
        environmentChanged |= DrawSettingCheckboxRow(
            "Enabled", &environment.HeightFogEnabled);
        ImGui::PopID();
        ImGui::BeginDisabled(!environment.HeightFogEnabled);
        environmentChanged |= DrawColorSetting(
            "Fog Color", "##AtmosphereFogColor", &atmosphere.FogInscatteringColor);
        environmentChanged |= DrawFloatSetting(
            "Density", &atmosphere.FogDensity, 0.0f, 0.1f, "%.4f", 0.0001f);
        environmentChanged |= DrawFloatSetting(
            "Height Falloff", &atmosphere.FogHeightFalloff, 0.0f, 2.0f, "%.3f", 0.005f);
        environmentChanged |= DrawFloatSetting(
            "Base Height", &atmosphere.FogBaseHeight, -2000.0f, 2000.0f, "%.1f", 0.5f);
        environmentChanged |= DrawFloatSetting(
            "Start Distance", &atmosphere.FogStartDistance, 0.0f, 10000.0f, "%.1f", 1.0f);
        environmentChanged |= DrawFloatSetting(
            "Max Opacity", &atmosphere.FogMaxOpacity, 0.0f, 1.0f, "%.2f", 0.01f);
        ImGui::EndDisabled();

        if (ImGui::TreeNodeEx("Advanced Atmosphere", ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            environmentChanged |= DrawFloat3Setting(
                "Rayleigh", "##AtmosphereRayleigh", &atmosphere.RayleighScattering,
                0.00001f, 0.0f, 0.1f, "%.6f");
            environmentChanged |= DrawFloatSetting(
                "Rayleigh Height", &atmosphere.RayleighDensityH,
                0.1f, 32.0f, "%.2f km", 0.05f);
            environmentChanged |= DrawFloat3Setting(
                "Mie Scatter", "##AtmosphereMieScatter", &atmosphere.MieScattering,
                0.00001f, 0.0f, 0.1f, "%.6f");
            environmentChanged |= DrawFloat3Setting(
                "Mie Absorption", "##AtmosphereMieAbsorption", &atmosphere.MieAbsorption,
                0.00001f, 0.0f, 0.1f, "%.6f");
            environmentChanged |= DrawFloatSetting(
                "Mie Height", &atmosphere.MieDensityH,
                0.1f, 16.0f, "%.2f km", 0.05f);
            environmentChanged |= DrawFloatSetting(
                "Mie Anisotropy", &atmosphere.MiePhaseG,
                0.0f, 0.99f, "%.3f", 0.005f);
            environmentChanged |= DrawFloat3Setting(
                "Ozone Absorption", "##AtmosphereOzone", &atmosphere.OzoneAbsorption,
                0.00001f, 0.0f, 0.05f, "%.6f");
            environmentChanged |= DrawFloatSetting(
                "Ozone Center", &atmosphere.OzoneCenterAltitude,
                0.0f, 60.0f, "%.1f km", 0.1f);
            environmentChanged |= DrawFloatSetting(
                "Ozone Width", &atmosphere.OzoneWidth,
                0.1f, 40.0f, "%.1f km", 0.1f);
            environmentChanged |= DrawColorSetting(
                "Ground Albedo", "##AtmosphereGroundAlbedo", &atmosphere.GroundAlbedo);
            environmentChanged |= DrawFloatSetting(
                "World Units / km", &atmosphere.WorldUnitsPerKm,
                0.001f, 10000.0f, "%.3f", 1.0f);
            environmentChanged |= DrawFloatSetting(
                "Origin Altitude", &atmosphere.WorldOriginAltitude,
                -10.0f, 100.0f, "%.2f km", 0.05f);
            environmentChanged |= DrawSettingRow(
                "Preset",
                [&]()
                {
                    if (!ImGui::Button("Earth Defaults", ImVec2(-FLT_MIN, 0.0f)))
                    {
                        return false;
                    }
                    atmosphere.Reset();
                    return true;
                });
            ImGui::TreePop();
        }

        if (userSetting.TickAnimation)
        {
            ImGui::TextDisabled("Disable Tick Animation to hold a manual sun angle.");
        }
        if (environmentChanged)
        {
            scene.MarkDirty();
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Ray Tracing"), true))
    {
        DrawSettingCheckboxRow(LOCTEXT("Progressive Render"), &userSetting.ProgressiveRender);
        DrawIntSetting(LOCTEXT("Samples"), &userSetting.NumberOfSamples, 1, 16);
        DrawSettingCheckboxRow(LOCTEXT("Exit After First"), &userSetting.ExitAfterFirst);
        DrawIntSetting(LOCTEXT("Ambient Speed"), &userSetting.BakeSpeedLevel, 0, 2);
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Upscaling"), true))
    {
        int upscaleMethod = userSetting.UpscalerType;
        const char* methods[] = {
            "None", "DLSS", "DLSS Ray Reconstruction", "FidelityFX FSR",
            "Native TAAU", "SGSR2 (2-pass CS)"};
        DrawSettingRow("Method",
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::Combo("##UpscaleMethod", &upscaleMethod, methods, IM_ARRAYSIZE(methods)))
                           {
                               userSetting.UpscalerType = upscaleMethod;
                               const auto type = Rendering::Upscaler::GetUpscalerTypeInfo(
                                   static_cast<uint32_t>(upscaleMethod)).type;
                               if (!GetEngine().GetRenderer().SupportsFrameGeneration(type))
                               {
                                   userSetting.FrameGeneration = false;
                               }
                               GetEngine().GetRenderer().RequestRecreateSwapChain();
                               return true;
                           }
                           return false;
                       });

        const char* qualities[] = {"Quality", "Balanced", "Performance", "Ultra Performance", "Native", "Auto"};
        DrawSettingRow("Quality",
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::Combo("##UpscaleQuality", (int*)&userSetting.SuperResolution, qualities,
                                            IM_ARRAYSIZE(qualities)))
                           {
                               GetEngine().GetRenderer().RequestRecreateSwapChain();
                               return true;
                           }
                           return false;
                       });

        const auto& upscalerTypeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
            static_cast<uint32_t>(upscaleMethod));
        if (upscalerTypeInfo.type != Rendering::Upscaler::EUpscalerType::None &&
            !GetEngine().GetRenderer().SupportsUpscaler(upscalerTypeInfo.type))
        {
            ImGui::TextDisabled("%s is not supported on this hardware.", upscalerTypeInfo.name);
        }
        if (upscalerTypeInfo.type == Rendering::Upscaler::EUpscalerType::NativeTAAU)
        {
            DrawFloatSetting("History Weight", &userSetting.NativeTAAUHistoryWeight,
                             0.5f, 0.98f, "%.2f", 0.01f);
            DrawFloatSetting("Sharpness", &userSetting.NativeTAAUSharpness,
                             0.0f, 1.0f, "%.2f", 0.01f);
        }

        const bool supportsTemporalNoiseFilter = upscalerTypeInfo.supportsTemporalPostFilter &&
            GetEngine().GetRenderer().SupportsUpscaler(upscalerTypeInfo.type);
        if (supportsTemporalNoiseFilter)
        {
            DrawSettingCheckboxRow("Noise Filter", &userSetting.TemporalUpscalerPostFilter);
            ImGui::BeginDisabled(!userSetting.TemporalUpscalerPostFilter);
            int filterPasses = static_cast<int>(userSetting.TemporalUpscalerPostFilterPasses);
            if (DrawIntSetting("A-Trous Passes", &filterPasses, 1, 4))
            {
                userSetting.TemporalUpscalerPostFilterPasses = static_cast<uint32_t>(filterPasses);
            }
            DrawFloatSetting("Filter Strength", &userSetting.TemporalUpscalerPostFilterStrength, 0.0f, 1.0f, "%.2f", 0.01f);
            DrawFloatSetting("Edge Sigma", &userSetting.TemporalUpscalerPostFilterLumaSigma, 0.01f, 0.5f, "%.2f", 0.01f);
            ImGui::EndDisabled();
        }
        DrawFloatSetting("Firefly Sigma", &userSetting.TemporalUpscalerFireflySigma, 0.1f, 16.0f, "%.1f", 0.1f);

        const bool canUseFrameGeneration =
            GetEngine().GetRenderer().SupportsFrameGeneration(upscalerTypeInfo.type);
        DrawSettingRow("Frame Generation",
                       [&]()
                       {
                           bool enabled = userSetting.FrameGeneration;
                           ImGui::BeginDisabled(!canUseFrameGeneration);
                           const bool changed = ImGui::Checkbox("##FrameGeneration", &enabled);
                           ImGui::EndDisabled();
                           if (changed)
                           {
                               userSetting.FrameGeneration = enabled && canUseFrameGeneration;
                               GetEngine().GetRenderer().RequestRecreateSwapChain();
                               return true;
                           }
                           return false;
                       });

        int frameMultiplier = static_cast<int>(std::clamp(userSetting.FrameGenerationMultiplier, 2u, 4u));
        ImGui::BeginDisabled(!canUseFrameGeneration);
        if (DrawIntSetting("FG Multiplier", &frameMultiplier, 2, 4))
        {
            userSetting.FrameGenerationMultiplier = static_cast<uint32_t>(std::clamp(frameMultiplier, 2, 4));
            GetEngine().GetRenderer().RequestRecreateSwapChain();
        }
        ImGui::EndDisabled();

        int frameLimitFps = static_cast<int>(std::min(userSetting.FrameGenerationFrameLimitFps, 1000u));
        if (DrawIntSetting("FG Base FPS Limit", &frameLimitFps, 0, 1000))
        {
            userSetting.FrameGenerationFrameLimitFps = static_cast<uint32_t>(std::clamp(frameLimitFps, 0, 1000));
        }

        const auto frameGenerationState = GetEngine().GetRenderer().GetFrameGenerationState();
        if (userSetting.FrameGeneration && frameGenerationState.valid)
        {
            ImGui::TextDisabled("Frame Generation presented x%u, status 0x%X",
                                frameGenerationState.numFramesActuallyPresented,
                                frameGenerationState.statusMask);
        }
        if (userSetting.FrameGeneration && !canUseFrameGeneration)
        {
            ImGui::TextDisabled("Frame generation is unavailable for the selected upscaler.");
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Animation"), false))
    {
        DrawSettingCheckboxRow(LOCTEXT("Tick Animation"), &userSetting.TickAnimation);
        DrawSettingCheckboxRow(LOCTEXT("TickPhysics"), &userSetting.TickPhysics);
        
        ImGui::Separator();
        for (auto* skinnedMesh : GetEngine().GetScene().Components<Runtime::SkinnedMeshComponent>())
        {
            if (Assets::Node* node = skinnedMesh->GetOwner())
            {
                ImGui::PushID(node->GetName().c_str());
                ImGui::Text("%s", node->GetName().c_str());
                auto animNames = skinnedMesh->GetAnimationNames();
                if (!animNames.empty())
                {
                    std::string current = skinnedMesh->GetCurrentAnimationName();
                    int selectedAnim = -1;
                    for (int i = 0; i < static_cast<int>(animNames.size()); ++i)
                    {
                        if (animNames[i] == current)
                        {
                            selectedAnim = i;
                            break;
                        }
                    }

                    std::vector<const char*> animPtrs;
                    for (const auto& name : animNames) animPtrs.push_back(name.c_str());

                    if (ImGui::Combo("##AnimList", &selectedAnim, animPtrs.data(),
                                     static_cast<int>(animPtrs.size())))
                    {
                        skinnedMesh->PlayAnimation(animNames[selectedAnim]);
                    }

                    float speed = skinnedMesh->GetPlaySpeed();
                    if (ImGui::SliderFloat("Speed", &speed, -2.0f, 2.0f, "%.2f"))
                    {
                        skinnedMesh->SetPlaySpeed(speed);
                    }
                }
                else
                {
                    ImGui::TextDisabled("No animations");
                }
                ImGui::PopID();
            }
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Misc"), false))
    {
        ImGui::SliderFloat(LOCTEXT("Time Scaling"), &userSetting.HeatmapScale, 0.10f, 2.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::Spacing();
        uint32_t tmin = 8, tmax = 32;
        ImGui::SliderScalar(LOCTEXT("Motion History"), ImGuiDataType_U32, &userSetting.TemporalFrames, &tmin,
                            &tmax);
        NextUI::Theme::EndPanelSection();
    }

    NextUI::Theme::EndInsetPanel();
    NextUI::Theme::EndFloatingPanel();
}

void NextRendererGameInstance::DrawModeRail(FRendererUiState& uiState)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 railPos = viewport->Pos + ImVec2(0.0f, TitlebarSize);
    const ImVec2 railSize = ImVec2(ModeRailWidth, viewport->Size.y - TitlebarSize - 30.0f);

    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled(railPos, railPos + railSize,
                              NextUI::Theme::ColorU32(NextUI::Theme::EColor::Background));
    background->AddLine(ImVec2(railPos.x + railSize.x - 1.0f, railPos.y),
                        ImVec2(railPos.x + railSize.x - 1.0f, railPos.y + railSize.y),
                        NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border));

    ImGui::SetNextWindowPos(railPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(railSize, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2((ModeRailWidth - ModeRailButtonSize) * 0.5f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##ModeRail", nullptr, flags))
    {
        struct ModeEntry
        {
            EWorkMode mode;
            const char* icon;
            const char* tooltip;
        };
        const ModeEntry topEntries[] = {
            {EWorkMode::Render,  ICON_FA_EYE,        "Render - Hide All Panels"},
            {EWorkMode::Detail,  ICON_FA_SLIDERS,    "Detail - Renderer Settings"},
            {EWorkMode::Profile, ICON_FA_CHART_LINE, "Profile - Memory & Stats"},
        };

        for (const auto& entry : topEntries)
        {
            const bool active = (entry.mode == uiState.workMode);
            if (NextUI::Theme::ModeRailButton(entry.icon, entry.tooltip, active, ModeRailButtonSize))
            {
                uiState.workMode = entry.mode;
                uiState.lastWorkMode = EWorkMode::Count;
            }
        }

        // Push the CVar editor button to the bottom.
        const float cvarButtonSize = ModeRailButtonSize;
        const float spaceUntilBottom = ImGui::GetContentRegionAvail().y - cvarButtonSize - 6.0f;
        if (spaceUntilBottom > 0.0f)
        {
            ImGui::Dummy(ImVec2(0.0f, spaceUntilBottom));
        }
        const bool cvarActive = (uiState.workMode == EWorkMode::CVar) &&
            GetEngine().GetShowFlags().DebugCVarPanel;
        if (NextUI::Theme::ModeRailButton(
                ICON_FA_TERMINAL, "CVars - Runtime Configuration", cvarActive, cvarButtonSize))
        {
            uiState.workMode = cvarActive ? EWorkMode::Render : EWorkMode::CVar;
            uiState.lastWorkMode = EWorkMode::Count;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void NextRendererGameInstance::DrawViewportTopBar(
    const FGameUiFrameContext&, FRendererUiState& uiState)
{
    Runtime::Config::UserSettings& userSetting = GetEngine().GetUserSettings();
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    constexpr float panelMargin = 10.0f;
    constexpr float toolbarHeight = 38.0f;
    constexpr float rightToolbarWidth = 168.0f;
    const float leftEdge = viewport->Pos.x + ModeRailWidth +
        (uiState.showSettings ? (360.0f + panelMargin * 2.0f) : panelMargin);
    const float rightEdge = viewport->Pos.x + viewport->Size.x - panelMargin;
    const float topEdge = viewport->Pos.y + TitlebarSize + panelMargin;
    const float availableWidth = std::max(0.0f, rightEdge - leftEdge - rightToolbarWidth - panelMargin);
    const bool showSceneSelector = availableWidth >= 650.0f;
    const bool showUpscalerSelector = availableWidth >= 500.0f;

    std::string sceneLabel = "Scene";
    if (userSetting.SceneIndex >= 0 &&
        userSetting.SceneIndex < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size()))
    {
        sceneLabel = std::filesystem::path(
            Runtime::Scene::SceneList::AllScenes[userSetting.SceneIndex]).stem().string();
    }

    const auto& upscalerInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
        static_cast<uint32_t>(std::max(0, userSetting.UpscalerType)));
    const auto& upscaleModeInfo = Rendering::Upscaler::GetUpscaleModeInfo(userSetting.SuperResolution);
    const std::string upscalerLabel = fmt::format("{} · {}", upscalerInfo.name, upscaleModeInfo.name);

    const float sceneWidth = showSceneSelector ? 150.0f : 0.0f;
    constexpr float rendererWidth = 154.0f;
    constexpr float renderModeWidth = 88.0f;
    constexpr float samplesWidth = 108.0f;
    const float upscalerWidth = showUpscalerSelector ? 188.0f : 0.0f;
    const float leftToolbarWidth = 8.0f + sceneWidth + rendererWidth + renderModeWidth +
        samplesWidth + upscalerWidth +
        (showSceneSelector ? 4.0f : 0.0f) + (showUpscalerSelector ? 4.0f : 0.0f) + 12.0f;

    NextUI::Theme::FOverlayPanelConfig leftConfig{};
    leftConfig.WindowId = "##ViewportRenderToolbar";
    leftConfig.Position = ImVec2(leftEdge, topEdge);
    leftConfig.Size = ImVec2(std::min(leftToolbarWidth, availableWidth), toolbarHeight);
    leftConfig.Padding = ImVec2(4.0f, 4.0f);
    leftConfig.ItemSpacing = ImVec2(4.0f, 0.0f);
    leftConfig.Rounding = 5.0f;
    leftConfig.BackgroundAlpha = 0.74f;

    if (NextUI::Theme::BeginOverlayPanel(leftConfig))
    {
        PushViewportToolbarStyle();
        if (showSceneSelector)
        {
            ImGui::SetNextItemWidth(sceneWidth);
            PushViewportPopupStyle();
            if (ImGui::BeginCombo("##ViewportScene", sceneLabel.c_str()))
            {
                ESceneListGroup currentGroup = ESceneListGroup::Other;
                bool hasGroup = false;
                for (int sceneIdx = 0;
                     sceneIdx < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size());
                     ++sceneIdx)
                {
                    const std::string& scenePath = Runtime::Scene::SceneList::AllScenes[sceneIdx];
                    const ESceneListGroup sceneGroup = GetSceneListGroup(scenePath);
                    if (!hasGroup || sceneGroup != currentGroup)
                    {
                        if (hasGroup)
                        {
                            ImGui::Separator();
                        }
                        currentGroup = sceneGroup;
                        hasGroup = true;
                        ImGui::Dummy(ImVec2(0.0f, 2.0f));
                        ImGui::TextDisabled("%s", GetSceneListGroupLabel(sceneGroup));
                    }

                    const bool selected = sceneIdx == userSetting.SceneIndex;
                    const std::string label = std::filesystem::path(scenePath).filename().string();
                    if (DrawViewportComboOption(label.c_str(), selected))
                    {
                        userSetting.SceneIndex = sceneIdx;
                        GetEngine().RequestLoadScene({.filename = scenePath});
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            PopViewportPopupStyle();
            NextUI::Theme::DrawTooltip("Scene");
            ImGui::SameLine();
        }

        const int rendererOptionCount = Runtime::GraphicsDebugPanel::GetRendererOptionCount(GetEngine());
        int currentRendererIndex =
            Runtime::GraphicsDebugPanel::ResolveRendererOptionIndex(userSetting, rendererOptionCount);
        if (currentRendererIndex < 0)
        {
            currentRendererIndex = 0;
            userSetting.RendererType = static_cast<int32_t>(
                Runtime::GraphicsDebugPanel::RendererOptions[currentRendererIndex].type);
        }
        ImGui::SetNextItemWidth(rendererWidth);
        PushViewportPopupStyle();
        if (ImGui::BeginCombo(
                "##ViewportRenderer",
                Runtime::GraphicsDebugPanel::RendererOptions[currentRendererIndex].label))
        {
            for (int rendererIndex = 0; rendererIndex < rendererOptionCount; ++rendererIndex)
            {
                const bool selected = rendererIndex == currentRendererIndex;
                if (DrawViewportComboOption(
                        Runtime::GraphicsDebugPanel::RendererOptions[rendererIndex].label, selected))
                {
                    userSetting.RendererType = static_cast<int32_t>(
                        Runtime::GraphicsDebugPanel::RendererOptions[rendererIndex].type);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        PopViewportPopupStyle();
        NextUI::Theme::DrawTooltip("Renderer");
        ImGui::SameLine();

        const char* renderModeLabel = userSetting.ProgressiveRender ? "Progressive" : "Realtime";
        if (DrawFlatViewportButton(
                renderModeLabel, "Toggle realtime / progressive rendering",
                userSetting.ProgressiveRender, ImVec2(renderModeWidth, 22.0f)))
        {
            userSetting.ProgressiveRender = !userSetting.ProgressiveRender;
        }
        ImGui::SameLine();

        ImGui::SetNextItemWidth(samplesWidth);
        const std::string sampleLabel = fmt::format("{} spp/frame", userSetting.NumberOfSamples);
        PushViewportPopupStyle();
        if (ImGui::BeginCombo("##ViewportSamples", sampleLabel.c_str()))
        {
            for (int samples = 1; samples <= 16; ++samples)
            {
                const bool selected = samples == userSetting.NumberOfSamples;
                if (DrawViewportComboOption(
                        fmt::format("{} spp/frame", samples).c_str(), selected))
                {
                    userSetting.NumberOfSamples = samples;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        PopViewportPopupStyle();
        NextUI::Theme::DrawTooltip("Samples traced per pixel, per rendered frame");

        if (showUpscalerSelector)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(upscalerWidth);
            PushViewportPopupStyle();
            if (ImGui::BeginCombo("##ViewportUpscaler", upscalerLabel.c_str()))
            {
                ImGui::TextDisabled("Upscaler");
                for (uint32_t rawType = 0;
                     rawType < static_cast<uint32_t>(Rendering::Upscaler::EUpscalerType::Count);
                     ++rawType)
                {
                    const auto& typeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(rawType);
                    const bool supported = typeInfo.type == Rendering::Upscaler::EUpscalerType::None ||
                        GetEngine().GetRenderer().SupportsUpscaler(typeInfo.type);
                    const bool selected = rawType == static_cast<uint32_t>(userSetting.UpscalerType);
                    ImGui::BeginDisabled(!supported);
                    if (DrawViewportComboOption(typeInfo.name, selected))
                    {
                        userSetting.UpscalerType = static_cast<int32_t>(rawType);
                        if (!GetEngine().GetRenderer().SupportsFrameGeneration(typeInfo.type))
                        {
                            userSetting.FrameGeneration = false;
                        }
                        GetEngine().GetRenderer().RequestRecreateSwapChain();
                    }
                    ImGui::EndDisabled();
                }

                ImGui::Separator();
                ImGui::TextDisabled("Quality");
                for (uint32_t rawMode = 0;
                     rawMode <= static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Auto);
                     ++rawMode)
                {
                    const auto& modeInfo = Rendering::Upscaler::GetUpscaleModeInfo(rawMode);
                    const bool selected = rawMode == userSetting.SuperResolution;
                    if (DrawViewportComboOption(modeInfo.name, selected))
                    {
                        userSetting.SuperResolution = rawMode;
                        GetEngine().GetRenderer().RequestRecreateSwapChain();
                    }
                }
                ImGui::EndCombo();
            }
            PopViewportPopupStyle();
            NextUI::Theme::DrawTooltip("Upscaler and quality mode");
        }
        PopViewportToolbarStyle();
    }
    NextUI::Theme::EndOverlayPanel();

    NextUI::Theme::FOverlayPanelConfig rightConfig{};
    rightConfig.WindowId = "##ViewportActionToolbar";
    rightConfig.Position = ImVec2(rightEdge - rightToolbarWidth, topEdge);
    rightConfig.Size = ImVec2(rightToolbarWidth, toolbarHeight);
    rightConfig.Padding = ImVec2(4.0f, 4.0f);
    rightConfig.ItemSpacing = ImVec2(4.0f, 0.0f);
    rightConfig.Rounding = 5.0f;
    rightConfig.BackgroundAlpha = 0.74f;

    if (NextUI::Theme::BeginOverlayPanel(rightConfig))
    {
        PushViewportToolbarStyle();
        if (DrawFlatViewportButton(
                ICON_FA_ROTATE_LEFT, "Reset camera to the scene view", false, ImVec2(28.0f, 22.0f)))
        {
            modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
        }
        ImGui::SameLine();

        glm::vec3 focusCenter;
        float focusRadius = 0.0f;
        const bool hasSelection = GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, focusRadius);
        ImGui::BeginDisabled(!hasSelection);
        if (DrawFlatViewportButton(
                ICON_FA_CROSSHAIRS, "Focus selected object", false, ImVec2(28.0f, 22.0f)))
        {
            modelViewController_.Focus(focusCenter, focusRadius);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        if (DrawFlatViewportButton(
                ICON_FA_CAMERA, "Take screenshot", false, ImVec2(28.0f, 22.0f)))
        {
            RequestScreenshot(false, "");
        }
        ImGui::SameLine();
        if (DrawFlatViewportButton(
                ICON_FA_CHEVRON_DOWN, "More capture options", false, ImVec2(24.0f, 22.0f)))
        {
            ImGui::OpenPopup("##ViewportCaptureMenu");
        }
        ImGui::SameLine();
        if (DrawFlatViewportButton(
                ICON_FA_KEYBOARD, "Toggle shortcut cheat sheet",
                uiState.showCheatSheet, ImVec2(28.0f, 22.0f)))
        {
            uiState.showCheatSheet = !uiState.showCheatSheet;
        }

        PushViewportPopupStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        if (ImGui::BeginPopup("##ViewportCaptureMenu"))
        {
            if (ImGui::MenuItem("Screenshot and Open Folder"))
            {
                RequestScreenshot(true, "");
            }
            ImGui::Separator();
            DrawVideoCaptureMenuItems();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        PopViewportPopupStyle();
        PopViewportToolbarStyle();
    }
    NextUI::Theme::EndOverlayPanel();
}

void NextRendererGameInstance::DrawViewportCheatSheet(FRendererUiState& uiState)
{
    if (!uiState.showCheatSheet)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float panelMargin = 10.0f;
    constexpr float toolbarHeight = 38.0f;
    constexpr float panelGap = 8.0f;
    constexpr float panelWidth = 390.0f;
    constexpr float panelHeight = 450.0f;
    const float rightEdge = viewport->Pos.x + viewport->Size.x - panelMargin;
    const float topEdge = viewport->Pos.y + TitlebarSize + panelMargin + toolbarHeight + panelGap;

    NextUI::Theme::FOverlayPanelConfig config{};
    config.WindowId = "##ViewportShortcutCheatSheet";
    config.Position = ImVec2(rightEdge - panelWidth, topEdge);
    config.Size = ImVec2(panelWidth, panelHeight);
    config.Padding = ImVec2(14.0f, 10.0f);
    config.ItemSpacing = ImVec2(6.0f, 5.0f);
    config.Rounding = 6.0f;
    config.BackgroundAlpha = 0.90f;

    if (NextUI::Theme::BeginOverlayPanel(config))
    {
        ImGui::TextColored(
            NextUI::Theme::Color(NextUI::Theme::EColor::Text),
            "%s  Keyboard & Mouse", ICON_FA_KEYBOARD);

        const float closeButtonWidth = 24.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - closeButtonWidth);
        PushViewportToolbarStyle();
        if (DrawFlatViewportButton(
                ICON_FA_XMARK, "Close shortcut cheat sheet", false, ImVec2(closeButtonWidth, 22.0f)))
        {
            uiState.showCheatSheet = false;
        }
        PopViewportToolbarStyle();

        NextUI::Theme::DrawThinSeparator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        const auto DrawSection = [](const char* title)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                NextUI::Theme::ColorU32(NextUI::Theme::EColor::Surface, 0.55f));
            ImGui::TextColored(
                NextUI::Theme::Color(NextUI::Theme::EColor::TextDim), "%s", title);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted("");
        };

        const auto DrawShortcut = [](const char* shortcut, const char* action)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(
                NextUI::Theme::Color(NextUI::Theme::EColor::Text), "%s", shortcut);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(
                NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", action);
        };

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 4.0f));
        if (ImGui::BeginTable(
                "##ViewportShortcuts", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 136.0f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

            DrawSection("NAVIGATION");
            DrawShortcut("RMB + Drag", "Look around");
            DrawShortcut("RMB + W A S D", "Move camera");
            DrawShortcut("RMB + Q / E", "Move up / down");
            DrawShortcut("Mouse Wheel", "Dolly forward / back");
            DrawShortcut("Alt + RMB", "Orbit selected object");

            DrawSection("SCENE & SELECTION");
            DrawShortcut("LMB", "Select object / set focus");
            DrawShortcut("F", "Focus selected object");
            DrawShortcut("Space", "Launch a physics cube");
            DrawShortcut("B", "Drop 400 physics spheres");
            DrawShortcut("Esc", "Clear selection");
            DrawShortcut("Ctrl / Cmd + D", "Duplicate selection");
            DrawShortcut("Delete / Backspace", "Delete selection");

            DrawSection("TRANSFORM GIZMO");
            DrawShortcut("W / E / R", "Move / Rotate / Scale");
            DrawShortcut("Q", "Toggle Local / World");

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    NextUI::Theme::EndOverlayPanel();
}

void NextRendererGameInstance::DrawTitleBar(const FGameUiFrameContext& context, FRendererUiState& uiState)
{
    NextUI::Theme::FAppTitleBarConfig config{};
    config.BrandWindowId = "RendererBrand";
    config.MenuWindowId = "RendererMenuBar";
    config.RightWindowId = "RendererWindowControls";
    config.AppName = "gkNextRenderer";
    config.Height = TitlebarSize;
    config.RightContentWidth = TitlebarRightInfoWidth;
    config.TitleFont = uiState.titleBarFont;
    config.IsMaximized = GetEngine().IsMaximized();
    config.DrawMenuBar = [&]() -> float
    {
        float menuRight = ImGui::GetCursorScreenPos().x;

        const auto UpdateMenuRight = [&menuRight]()
        {
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
        };

        if (ImGui::BeginMenu("File"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Project Page"))
            {
                NextRenderer::OSCommand("https://github.com/gameknife/gkNextRenderer");
            }
            if (ImGui::MenuItem("Open Screenshot Folder"))
            {
                GetEngine().GetScreenShotService().EnsureDirectory();
                const std::string folderPath = GetEngine().GetScreenShotService().GetDirectory();
                NextRenderer::OSCommand(folderPath.c_str());
            }
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("View"))
        {
            UpdateMenuRight();
            auto& showFlags = GetEngine().GetShowFlags();
            Utilities::UI::DrawShowFlagsCommon(showFlags);
            ImGui::MenuItem("Profiler Overlay", nullptr, &GetEngine().GetUserSettings().ShowOverlay);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Screenshot"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Screenshot"))
            {
                RequestScreenshot(false, "");
            }
            if (ImGui::MenuItem("Screenshot and Open Folder"))
            {
                RequestScreenshot(true, "");
            }
            ImGui::Separator();
            DrawVideoCaptureMenuItems();
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Renderer"))
        {
            UpdateMenuRight();
            Runtime::GraphicsDebugPanel::DrawRendererSelector(GetEngine(), GetEngine().GetUserSettings(),
                                                              "##RendererMenuSelector", 180.0f);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Settings"))
        {
            UpdateMenuRight();
            ImGui::MenuItem("Render Settings", nullptr, &uiState.showSettings);
            ImGui::MenuItem("Stats Overlay", nullptr, &GetEngine().GetUserSettings().ShowOverlay);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Help"))
        {
            UpdateMenuRight();
            ImGui::MenuItem("Documentation", nullptr, false, false);
            ImGui::MenuItem("About gkNextRenderer", nullptr, false, false);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        return menuRight;
    };
    config.OnMinimize = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().RequestMinimize(); })
                                                     : std::function<void()>();
    config.OnToggleMaximize = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().ToggleMaximize(); })
                                                          : std::function<void()>();
    config.OnClose = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().RequestClose(); })
                                                 : std::function<void()>();
    NextUI::Theme::DrawAppTitleBar(GetEngine(), config);
}

void NextRendererGameInstance::DrawBottomStatusBar(FRendererUiState& uiState)
{
    NextUI::Theme::DrawStandardBottomBar(GetEngine(), "RendererStatusBar", 30.0f,
                                         [&]()
                                         {
                                             uiState.memoryStatisticsPanelOpen = !uiState.memoryStatisticsPanelOpen;
                                         },
                                         uiState.memoryStatisticsPanelOpen,
                                         []() { Modules::LiveCoding::RequestCppReload(); },
                                         Modules::LiveCoding::IsCppLiveCodingAvailable(),
                                         [this]() { RequestScreenshot(false, ""); });
}

void NextRendererGameInstance::DrawMemoryStatisticsPanel(FRendererUiState& uiState)
{
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
