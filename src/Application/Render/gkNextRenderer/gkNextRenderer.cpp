#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <array>
#include <tuple>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Editor/FontLoader.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/ScreenShot.hpp"
#include "Engine/Utilities/FileHelper.hpp"
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
    std::string initializedScene = Runtime::Scene::SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex];
    if (!GOption->SceneName.empty())
    {
        initializedScene = GOption->SceneName;
    }
    GetEngine().RequestLoadScene({.filename = initializedScene});
    // GetEngine().GetUserSettings().SceneEpsilonScale = 0.01f;
    // GetEngine().GetUserSettings().AmbientCubeUnit = 0.02f;
    // GetEngine().GetUserSettings().AmbientCubeOffsetX = 0.0f;
    // GetEngine().GetUserSettings().AmbientCubeOffsetZ = 0.0f;
}

void NextRendererGameInstance::OnTick(double deltaSeconds)
{
    if (playbackPaused_ && !stepRequested_)
    {
        return;
    }
    modelViewController_.UpdateCamera(10.0f, deltaSeconds);
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
    MatPreparedForAdd.push_back({Assets::Material::Metallic(glm::vec3(0.5,0.5,0.5), 0.4f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Dielectric(1.5f, 0.0f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
    MatPreparedForAdd.push_back({Assets::Material::Mixture(glm::vec3(1.0f, 0.3f, 0.3f), 0.01f)});
    materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
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
    if (isTakingScreenshot_ && context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow)
    {
        return true;
    }

    EnsureUiFonts(uiState, context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow);
    UpdateUiScaledMetrics();

    if (uiState.workMode != uiState.lastWorkMode)
    {
        switch (uiState.workMode)
        {
        case EWorkMode::Renderer:
            uiState.showSettings = true;
            uiState.showOverlay = false;
            uiState.memoryStatisticsPanelOpen = false;
            break;
        case EWorkMode::Profiler:
            uiState.showSettings = false;
            uiState.showOverlay = true;
            uiState.memoryStatisticsPanelOpen = true;
            break;
        case EWorkMode::Settings:
            uiState.showSettings = true;
            uiState.showOverlay = false;
            uiState.memoryStatisticsPanelOpen = false;
            break;
        default:
            uiState.showSettings = false;
            uiState.showOverlay = false;
            uiState.memoryStatisticsPanelOpen = false;
            break;
        }
        uiState.lastWorkMode = uiState.workMode;
    }
    else if (uiState.workMode == EWorkMode::Profiler && !uiState.showOverlay)
    {
        uiState.workMode = EWorkMode::Renderer;
        uiState.lastWorkMode = uiState.workMode;
        uiState.showSettings = true;
        uiState.showOverlay = false;
        uiState.memoryStatisticsPanelOpen = false;
    }

    DrawTitleBar(context, uiState);
    DrawModeRail(uiState);
    DrawSettings(uiState);
    DrawViewportTopBar(context, uiState);
    DrawViewportBottomBar(context);
    DrawBottomStatusBar(uiState);
    DrawMemoryStatisticsPanel(uiState);

    if (context.surfaceKind == FGameUiFrameContext::ESurfaceKind::MainWindow && ImGui::GetCurrentContext() != nullptr)
    {
        auto& swapChain = GetEngine().GetRenderer().SwapChain();
        const auto offset = swapChain.OutputOffset();
        const auto extent = swapChain.OutputExtent();
        const ImVec2 viewportOrigin = ImGui::GetMainViewport()->Pos;
        uiState.gizmoController.Draw(GetEngine(),
            glm::vec2(viewportOrigin.x + static_cast<float>(offset.x), viewportOrigin.y + static_cast<float>(offset.y)),
            glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
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
    if (isTakingScreenshot_)
    {
        return;
    }

    std::string folderPath = Utilities::FileHelper::GetPlatformFilePath("screenshots");
    Utilities::FileHelper::EnsureDirectoryExists(folderPath);

    auto now = std::chrono::system_clock::now();
    std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_ptr = std::localtime(&in_time_t);
    std::string timestamp = fmt::format("{:%Y-%m-%d_%H-%M-%S}", *tm_ptr);
    std::string suffix = tag.empty() ? "" : "_" + tag;
    std::string filename = (std::filesystem::path(folderPath) / (timestamp + suffix)).string();

    isTakingScreenshot_ = true;

    GetEngine().AddTimerTask(0.2, [this, filename, folderPath, openFolder]() {
        Runtime::ScreenShot::SaveSwapChainToFile(&GetEngine().GetRenderer(), filename, 0, 0, 0, 0);
        if (openFolder)
        {
            NextRenderer::OSCommand(folderPath.c_str());
        }
        isTakingScreenshot_ = false;
        return true;
    });
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
            break;
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
    //cvars.SetDefaultFromString("r.superResolution", "4", &error);
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
        for (const auto& cam : GetEngine().GetScene().GetCameras())
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
            GetEngine().GetScene().SetRenderCamera(GetEngine().GetScene().GetCameras()[userSetting.CameraIdx]);
            modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
        }

        auto& camera = GetEngine().GetScene().GetRenderCamera();
        DrawFloatSetting(LOCTEXT("Aperture"), &camera.Aperture, 0.0f, 1.0f, "%.2f", 0.01f);
        DrawFloatSetting(LOCTEXT("Focus(cm)"), &camera.FocalDistance, 0.001f, 1000.0f, "%.3f", 0.05f);
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Ray Tracing"), true))
    {
        DrawSettingCheckboxRow(LOCTEXT("AntiAlias"), &userSetting.TAA);
        DrawIntSetting(LOCTEXT("Samples"), &userSetting.NumberOfSamples, 1, 16);
        DrawSettingCheckboxRow(LOCTEXT("FastGather"), &userSetting.FastGather);
        DrawIntSetting(LOCTEXT("Ambient Speed"), &userSetting.BakeSpeedLevel, 0, 2);
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Denoiser"), true))
    {
        // Variance-guided a-trous wavelet denoiser. Diffuse/specular run in one fused pass;
        // specular often needs fewer iterations to preserve glossy detail.
        DrawSettingCheckboxRow(LOCTEXT("Enable"), &userSetting.Denoiser);
        DrawIntSetting(LOCTEXT("Diffuse Iterations"), &userSetting.DenoiseAtrousIterations, 1, 6);
        DrawIntSetting(LOCTEXT("Specular Iterations"), &userSetting.DenoiseAtrousSpecularIterations, 0, 6);
        DrawFloatSetting(LOCTEXT("Sigma Luma"), &userSetting.DenoiseAtrousSigmaLuma, 0.5f, 16.0f, "%.2f", 0.1f);
        DrawFloatSetting(LOCTEXT("Normal Power"), &userSetting.DenoiseAtrousNormalPower, 1.0f, 128.0f, "%.0f", 1.0f);
        DrawFloatSetting(LOCTEXT("Sigma Depth"), &userSetting.DenoiseSigmaDepth, 0.0f, 8.0f, "%.2f", 0.05f);
        DrawFloatSetting(LOCTEXT("Spec Footprint"), &userSetting.DenoiseSpecFootprint, 0.0f, 64.0f, "%.1f", 0.5f);
        NextUI::Theme::EndPanelSection();
    }
    
    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Upscaling"), true))
    {
        int upscaleMethod = userSetting.DLSS ? 1 : userSetting.FSR ? 2 : 0;
        const char* methods[] = {"None", "DLSS", "FSR"};
        DrawSettingRow("Method",
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::Combo("##UpscaleMethod", &upscaleMethod, methods, IM_ARRAYSIZE(methods)))
                           {
                               userSetting.DLSS = upscaleMethod == 1 && GetEngine().GetRenderer().SupportDLSS();
                               userSetting.FSR = upscaleMethod == 2;
                               if (!userSetting.DLSS)
                               {
                                   userSetting.DLSSG = false;
                               }
                               GetEngine().GetRenderer().RequestRecreateSwapChain();
                               return true;
                           }
                           return false;
                       });

        const char* qualities[] = {"Quality", "Balanced", "Performance", "Ultra Performance", "Native"};
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

        if (upscaleMethod == 1 && !GetEngine().GetRenderer().SupportDLSS())
        {
            ImGui::TextDisabled("DLSS not supported on this hardware.");
        }

        const bool canUseFrameGeneration =
            upscaleMethod == 1 &&
            GetEngine().GetRenderer().SupportDLSSG() &&
            GetEngine().GetRenderer().SupportReflex();
        DrawSettingRow("Frame Generation",
                       [&]()
                       {
                           bool enabled = userSetting.DLSSG;
                           ImGui::BeginDisabled(!canUseFrameGeneration);
                           const bool changed = ImGui::Checkbox("##DLSSG", &enabled);
                           ImGui::EndDisabled();
                           if (changed)
                           {
                               userSetting.DLSSG = enabled && canUseFrameGeneration;
                               GetEngine().GetRenderer().RequestRecreateSwapChain();
                               return true;
                           }
                           return false;
                       });

        int frameMultiplier = static_cast<int>(std::clamp(userSetting.DLSSGFrameMultiplier, 2u, 4u));
        if (DrawIntSetting("FG Multiplier", &frameMultiplier, 2, 4))
        {
            userSetting.DLSSGFrameMultiplier = static_cast<uint32_t>(std::clamp(frameMultiplier, 2, 4));
            GetEngine().GetRenderer().RequestRecreateSwapChain();
        }

        int frameLimitFps = static_cast<int>(std::min(userSetting.DLSSGFrameLimitFps, 1000u));
        if (DrawIntSetting("FG Base FPS Limit", &frameLimitFps, 0, 1000))
        {
            userSetting.DLSSGFrameLimitFps = static_cast<uint32_t>(std::clamp(frameLimitFps, 0, 1000));
        }

        const auto frameGenerationState = GetEngine().GetRenderer().GetFrameGenerationState();
        if (userSetting.DLSSG && frameGenerationState.valid)
        {
            ImGui::TextDisabled("DLSS-G presented x%u, status 0x%X",
                                frameGenerationState.numFramesActuallyPresented,
                                frameGenerationState.statusMask);
        }
        if (upscaleMethod == 1 && !canUseFrameGeneration)
        {
            ImGui::TextDisabled("DLSS Frame Generation requires Streamline DLSS-G and Reflex support.");
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Lighting"), false))
    {
        DrawSettingCheckboxRow(LOCTEXT("HasSky"), &GetEngine().GetScene().GetEnvSettings().HasSky);
        if (GetEngine().GetScene().GetEnvSettings().HasSky)
        {
            ImGui::SliderInt(LOCTEXT("SkyIdx"), &GetEngine().GetScene().GetEnvSettings().SkyIdx, 0, 10);
            ImGui::SliderFloat(LOCTEXT("SkyRotation"), &GetEngine().GetScene().GetEnvSettings().SkyRotation, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat(LOCTEXT("SkyLum"), &GetEngine().GetScene().GetEnvSettings().SkyIntensity, 0.0f, 1000.0f, "%.0f");
        }

        DrawSettingCheckboxRow(LOCTEXT("HasSun"), &GetEngine().GetScene().GetEnvSettings().HasSun);
        if (GetEngine().GetScene().GetEnvSettings().HasSun)
        {
            ImGui::SliderFloat(LOCTEXT("SunRotation"), &GetEngine().GetScene().GetEnvSettings().SunRotation, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat(LOCTEXT("SunLum"), &GetEngine().GetScene().GetEnvSettings().SunIntensity, 0.0f, 2000.0f, "%.0f");
        }

        ImGui::SliderFloat(LOCTEXT("PaperWhitNit"), &userSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f");
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Animation"), false))
    {
        DrawSettingCheckboxRow(LOCTEXT("Tick Animation"), &userSetting.TickAnimation);

        ImGui::Separator();
        for (auto& node : GetEngine().GetScene().Nodes())
        {
            if (auto skinnedMesh = node->GetComponent<Runtime::SkinnedMeshComponent>())
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
        DrawSettingCheckboxRow(LOCTEXT("TickPhysics"), &userSetting.TickPhysics);

        ImGui::SliderFloat(LOCTEXT("Time Scaling"), &userSetting.HeatmapScale, 0.10f, 2.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::Spacing();
        uint32_t tmin = 8, tmax = 32;
        ImGui::SliderScalar(LOCTEXT("Temporal Frames"), ImGuiDataType_U32, &userSetting.TemporalFrames, &tmin,
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
            {EWorkMode::Renderer, ICON_FA_CAMERA_RETRO, "Renderer"},
            {EWorkMode::Camera,   ICON_FA_CAMERA,       "Camera"},
            {EWorkMode::World,    ICON_FA_GLOBE,        "World / Lighting"},
            {EWorkMode::Mesh,     ICON_FA_CUBE,         "Scene Outliner"},
            {EWorkMode::Profiler, ICON_FA_CHART_LINE,   "Profiler"},
        };

        for (const auto& entry : topEntries)
        {
            const bool active = (entry.mode == uiState.workMode);
            if (NextUI::Theme::ModeRailButton(entry.icon, entry.tooltip, active, ModeRailButtonSize))
            {
                uiState.workMode = entry.mode;
            }
        }

        // Push the gear button to the bottom.
        const float gearSize = ModeRailButtonSize;
        const float spaceUntilBottom = ImGui::GetContentRegionAvail().y - gearSize - 6.0f;
        if (spaceUntilBottom > 0.0f)
        {
            ImGui::Dummy(ImVec2(0.0f, spaceUntilBottom));
        }
        const bool settingsActive = (uiState.workMode == EWorkMode::Settings);
        if (NextUI::Theme::ModeRailButton(ICON_FA_GEAR, "Settings", settingsActive, gearSize))
        {
            uiState.workMode = EWorkMode::Settings;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void NextRendererGameInstance::DrawViewportTopBar(const FGameUiFrameContext& context, const FRendererUiState& uiState)
{
    Runtime::Config::UserSettings& userSetting = GetEngine().GetUserSettings();
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    const float panelMargin = 10.0f;
    const float leftEdge = viewport->Pos.x + ModeRailWidth +
        (uiState.showSettings ? (360.0f + panelMargin * 2.0f) : panelMargin);
    const float topEdge = viewport->Pos.y + TitlebarSize + 10.0f;

    // Left badge: "Path Tracing | Live"
    {
        const char* rendererLabel = Runtime::GraphicsDebugPanel::GetCurrentRendererLabel(GetEngine(), userSetting);
        const std::string rendererText = rendererLabel;
        constexpr const char* liveText = "Live";
        const float badgeHeight = 30.0f;
        const float rendererWidth = ImGui::CalcTextSize(rendererText.c_str()).x + 24.0f;
        const float liveWidth = ImGui::CalcTextSize(liveText).x + 20.0f;
        const float badgeWidth = rendererWidth + liveWidth + 26.0f;

        NextUI::Theme::FOverlayPanelConfig config{};
        config.WindowId = "##ViewportTopLeftBadge";
        config.Position = ImVec2(leftEdge, topEdge);
        config.Size = ImVec2(badgeWidth, badgeHeight);
        config.Padding = ImVec2(12.0f, 8.0f);
        config.ItemSpacing = ImVec2(8.0f, 0.0f);
        config.BackgroundColor = NextUI::Theme::EColor::Background;
        config.BackgroundAlpha = 0.82f;
        config.ExtraFlags = ImGuiWindowFlags_NoScrollbar;

        if (NextUI::Theme::BeginOverlayPanel(config))
        {
            ImGui::TextUnformatted(rendererText.c_str());
            ImGui::SameLine(0.0f, 12.0f);
            NextUI::Theme::DrawVerticalSeparator(16.0f, 0.0f, 0.8f);
            NextUI::Theme::DrawStatusDot(liveText, true);
        }
        NextUI::Theme::EndOverlayPanel();
    }

    // Right cluster: screenshot / focus / 1:1
    {
        const float clusterWidth = 138.0f;
        const float rightEdge = viewport->Pos.x + viewport->Size.x - panelMargin - clusterWidth;
        NextUI::Theme::FOverlayPanelConfig config{};
        config.WindowId = "##ViewportTopRightCluster";
        config.Position = ImVec2(rightEdge, topEdge);
        config.Size = ImVec2(clusterWidth, 32.0f);
        config.Padding = ImVec2(4.0f, 2.0f);
        config.ItemSpacing = ImVec2(4.0f, 0.0f);
        config.BackgroundAlpha = 0.80f;
        if (NextUI::Theme::BeginOverlayPanel(config))
        {
            if (NextUI::Theme::ToolbarButton(ICON_FA_CAMERA, "Take Screenshot", false, ImVec2(28.0f, 26.0f)))
            {
                RequestScreenshot(false, "");
            }
            ImGui::SameLine();
            if (NextUI::Theme::ToolbarButton(ICON_FA_EXPAND, "Focus Selected", false, ImVec2(28.0f, 26.0f)))
            {
                glm::vec3 focusCenter;
                float radius;
                if (GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, radius))
                {
                    modelViewController_.Focus(focusCenter, radius);
                }
            }
            ImGui::SameLine();
            NextUI::Theme::ToolbarButton("1:1 " ICON_FA_CHEVRON_DOWN, "Native Resolution", false, ImVec2(46.0f, 26.0f));
        }
        NextUI::Theme::EndOverlayPanel();
    }
}

void NextRendererGameInstance::DrawViewportBottomBar(const FGameUiFrameContext& context)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float bottomStatusBar = 30.0f;

    const auto extent = context.framebufferExtent.width > 0 && context.framebufferExtent.height > 0
        ? context.framebufferExtent
        : GetEngine().GetRenderer().SwapChain().OutputExtent();
    const std::string frameText = fmt::format("Frame {}", GetEngine().GetTotalFrames());
    const std::string sampleText = fmt::format("Samples {} spp", GetEngine().GetUserSettings().NumberOfSamples);
    const std::string resolutionText = fmt::format("{} x {}", extent.width, extent.height);
    const float textWidth = ImGui::CalcTextSize(frameText.c_str()).x +
        ImGui::CalcTextSize(sampleText.c_str()).x +
        ImGui::CalcTextSize(resolutionText.c_str()).x + 72.0f;
    const ImVec2 padding(16.0f, 6.0f);
    const ImVec2 windowSize(textWidth + padding.x * 2.0f + 18.0f,
                            ImGui::GetTextLineHeight() + padding.y * 2.0f);
    const ImVec2 windowPos(viewport->Pos.x + (viewport->Size.x - windowSize.x) * 0.5f,
                           viewport->Pos.y + viewport->Size.y - bottomStatusBar - windowSize.y - 8.0f);

    NextUI::Theme::FOverlayPanelConfig config{};
    config.WindowId = "##ViewportFrameInfo";
    config.Position = windowPos;
    config.Size = windowSize;
    config.Padding = padding;
    config.ItemSpacing = ImVec2(4.0f, 0.0f);
    config.BackgroundAlpha = 0.80f;

    if (NextUI::Theme::BeginOverlayPanel(config))
    {
        ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", frameText.c_str());
        NextUI::Theme::DrawVerticalSeparator(14.0f, 10.0f, 0.72f);
        ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", sampleText.c_str());
        NextUI::Theme::DrawVerticalSeparator(14.0f, 10.0f, 0.72f);
        ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", resolutionText.c_str());
        NextUI::Theme::DrawVerticalSeparator(14.0f, 10.0f, 0.72f);
        NextUI::Theme::ToolbarButton(ICON_FA_EXPAND, "Viewport Display Options", false, ImVec2(22.0f, 18.0f));
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
                RequestScreenshot(true, "");
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
            ImGui::MenuItem("Profiler Overlay", nullptr, &uiState.showOverlay);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Capture"))
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
            ImGui::MenuItem("Stats Overlay", nullptr, &uiState.showOverlay);
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
                                         uiState.memoryStatisticsPanelOpen);
}

void NextRendererGameInstance::DrawMemoryStatisticsPanel(FRendererUiState& uiState)
{
    const bool profilerMode = uiState.workMode == EWorkMode::Profiler;
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
                uiState.workMode = EWorkMode::Renderer;
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
            uiState.workMode = EWorkMode::Renderer;
        }
    }
}
