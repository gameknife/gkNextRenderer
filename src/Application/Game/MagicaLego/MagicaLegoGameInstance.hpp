#pragma once
// ReSharper disable once CppUnusedIncludeDirective
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define MAGICALEGO_SAVE_VERSION 1

namespace MagicaLego
{
    struct FCursor;
}

enum class ELegoMode : uint8_t
{
    ELM_Dig,
    ELM_Place,
    ELM_Select,
};

enum class EBasePlane : uint8_t
{
    EBP_Big,
    EBP_Mid,
    EBP_Small,
};

enum class EOrientation : uint8_t
{
    EO_North,
    EO_East,
    EO_South,
    EO_West,
};

static constexpr glm::mat4 GetOrientationMatrix(EOrientation orientation)
{
    switch (orientation)
    {
    case EOrientation::EO_North: return glm::mat4(1.0f);
    case EOrientation::EO_East: return glm::rotate(glm::mat4(1.0f), -glm::pi<float>() * 0.5f, glm::vec3(0, 1, 0));
    case EOrientation::EO_South: return glm::rotate(glm::mat4(1.0f), -glm::pi<float>() * 1.0f, glm::vec3(0, 1, 0));
    case EOrientation::EO_West: return glm::rotate(glm::mat4(1.0f), -glm::pi<float>() * 1.5f, glm::vec3(0, 1, 0));
    }
    return glm::mat4(1.0f);
};

template <>
struct fmt::formatter<EOrientation>
{
public:
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template <typename Context>
    constexpr auto format(EOrientation const& foo, Context& ctx) const
    {
        switch (foo)
        {
        case EOrientation::EO_North: return format_to(ctx.out(), "{}", "NORTH");
        case EOrientation::EO_East: return format_to(ctx.out(), "{}", "EAST");
        case EOrientation::EO_South: return format_to(ctx.out(), "{}", "SOUTH");
        case EOrientation::EO_West: return format_to(ctx.out(), "{}", "WEST");
        }
        return format_to(ctx.out(), "{}", "UNKNOWN");
    }
};

struct FBasicBlock
{
    int16_t brushId_;
    uint32_t modelId_;
    uint32_t matType;
    glm::vec4 color;
    char name[128];
    char type[128];
};

struct FPlacedBlock
{
    glm::i16vec3 location;
    EOrientation orientation;
    uint8_t reserved0;
    int16_t modelId_;
    uint8_t reserved1;
    uint8_t reserved2;
};

using FBasicBlockIndicatorLibrary = std::map<std::string, std::tuple<glm::vec3, glm::vec3>>;
using FBasicBlockStack = std::vector<FBasicBlock>;
using FBasicBlockLibrary = std::map<std::string, FBasicBlockStack>;
using FPlacedBlockDatabase = std::unordered_map<uint32_t, FPlacedBlock>;
using FPlacedRecords = std::vector<FPlacedBlock>;

struct FMagicaLegoSave
{
    int version = MAGICALEGO_SAVE_VERSION;
    FBasicBlockStack brushs;
    std::vector<FPlacedBlock> records;

    void Save(std::string filename);
    void Load(std::string filename);
};

class MagicaLegoGameInstance : public NextGameInstanceBase
{
public:
    MagicaLegoGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~MagicaLegoGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;

    void OnDestroy() override
    {
    }

    bool OnRenderUI() override;
    void OnInitUI() override;
    void OnRayHitResponse(Assets::RayCastResult& result) override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    
    void TestSpawnPhysicsBlock();

    // save and load
    void CleanUp();
    void SaveRecord(std::string filename);
    void LoadRecord(std::string filename);

    // modes
    ELegoMode GetBuildMode() const { return currentMode_; }
    void SetBuildMode(ELegoMode mode);

    // Library Access
    FBasicBlockLibrary& GetBasicNodeLibrary() { return BasicBlockTypeMap; }

    // Brush
    int16_t ConvertBrushIdxToNextType(const std::string& prefix, int idx) const;
    void TryChangeSelectionBrushIdx(int16_t idx);

    int16_t GetCurrentBrushIdx() const { return currentBlockIdx_; }
    void SetCurrentBrushIdx(int16_t idx);

    // Replay
    int GetCurrentStep() const { return currentPreviewStep; }
    int GetMaxStep() const { return std::max(0, static_cast<int>(BlockRecords.size())); }
    void SetPlayStep(int step);
    void DumpReplayStep(int step);

    bool IsPlayReview() const { return playReview_; }
    void SetPlayReview(bool Enable);

    // Blocks
    bool PlaceDynamicBlock(FPlacedBlock Block);
    bool CanPlaceBlock(const FPlacedBlock& Block, std::string* reason = nullptr) const;
    bool HasPlacementConflictReason() const { return !placementConflictReason_.empty(); }
    const std::string& GetPlacementConflictReason() const { return placementConflictReason_; }

    // Command System Support
    FBasicBlock* GetBasicBlockBySpec(const std::string& type, const std::string& color);
    bool HasBlockAt(glm::i16vec3 location) const;
    std::vector<std::string> GetAllBlockTypes() const;
    std::vector<std::string> GetAllBlockColors(const std::string& type) const;

    // Scene description for AI context
    std::string GetCurrentSceneDescription() const;
    int GetPlacedBlockCount() const { return static_cast<int>(BlocksDynamics.size()); }

    // Base
    void SwitchBasePlane(EBasePlane Type);
    EBasePlane GetCurrentBasePlane() const { return currentBaseSize_; }

    // Thumbnail
    void GenerateThumbnail();

    // Orientation
    EOrientation GetCurrentOrientation() const { return currentOrientation_; }
    void ChangeOrientation() { currentOrientation_ = static_cast<EOrientation>((static_cast<uint8_t>(currentOrientation_) + 1) % 4); }

    // Music Stuff
    void PlayNextBGM();
    bool IsBGMPaused();
    void PauseBGM(bool pause);
    std::string GetCurrentBGMName();

    // Camera Handle
    float& GetCameraRotX() { return cameraRotX_; }

    // Status
    void SetCapturing(bool b) { bCapturing_ = b; }
    void SetMouseCapturedByUI(bool captured) { mouseCapturedByUI_ = captured; }

    // Cursor for script/AI control
    MagicaLego::FCursor& GetCursor();
    const MagicaLego::FCursor& GetCursor() const;

protected:
    void AddBlockGroup(std::string typeName);
    void AddBasicBlock(std::string blockName, std::string typeName);
    FBasicBlock* GetBasicBlock(uint32_t BlockIdx);

    void RebuildScene(FPlacedBlockDatabase& Source, uint32_t newhash);
    void RebuildFromRecord(int timelapse);

    void CleanDynamicBlocks();

    void CPURaycast();

    void PerformLeftClickCheck();
    void UpdateFocusToScreenCenter();
    void UpdateMouseCursor();

    std::vector<glm::i16vec3> GetOccupiedCellsForBlock(const FPlacedBlock& Block) const;
    uint32_t GetOccupancyOwnerHash(glm::i16vec3 location) const;
    bool CanPlaceBlockInternal(const FPlacedBlock& Block, uint32_t ignoredOwnerHash, std::string* reason) const;
    bool ResolvePlacementLocationFromRay(const Assets::RayCastResult& rayResult, glm::i16vec3& outBlockLocation, std::string* reason = nullptr);
    void RebuildOccupancyIndex();
    void RegisterOccupancy(uint32_t ownerHash, const std::vector<glm::i16vec3>& occupiedCells);
    void UnregisterOccupancy(uint32_t ownerHash);

private:
    ELegoMode currentMode_{};
    EBasePlane currentBaseSize_{};
    EOrientation currentOrientation_{};

    // base blocks library
    FBasicBlockStack BasicNodes;
    FBasicBlockLibrary BasicBlockTypeMap;
    FBasicBlockIndicatorLibrary BasicNodeIndicatorMap;

    int16_t currentBlockIdx_{};
    int currentPreviewStep{};

    // 起始的方块位置，之后的instance都是rebuild出来的
    uint32_t instanceCountBeforeDynamics_{};

    bool playReview_{};

    // 基础加速结构，location -> uint64_t，存储已经放置的方块
    FPlacedBlockDatabase BlocksDynamics;
    FPlacedRecords BlockRecords;
    std::unordered_map<uint32_t, uint32_t> OccupiedCellOwnerMap;
    std::unordered_map<uint32_t, std::vector<glm::i16vec3>> OwnerOccupiedCellsMap;

    bool resetMouse_{};

    glm::dvec2 mousePos_{};

    glm::vec3 realCameraCenter_{};
    glm::vec3 cameraCenter_{};
    mutable glm::vec3 panForward_{};
    mutable glm::vec3 panLeft_{};
    float cameraRotX_{};
    float cameraRotY_{};
    float cameraArm_{};
    float cameraMultiplier_{};
    float cameraFOV_{};

    bool isOrbitDragging_{};
    glm::vec3 focusTarget_{};
    bool mouseRightPressed_{};
    bool isTracingObject_{};

    bool bMouseLeftDown_{};
    int lastDownFrameNum_{};
    std::vector<uint32_t> oneLinePlacedInstance_{};
    glm::i16vec3 lastPlacedLocation_{};
    glm::i16vec3 lastSelectLocation_{};
    uint32_t lastSelectIndex_{};
    std::unique_ptr<class MagicaLegoUserInterface> UserInterface_;

    // Indicator Stuff
    bool indicatorDrawRequest_{};
    glm::vec4 indicatorColor_ = glm::vec4(0.5f, 0.65f, 1.0f, 0.75f);
    glm::vec3 indicatorMinTarget_{};
    glm::vec3 indicatorMaxTarget_{};
    glm::vec3 indicatorMinCurrent_{};
    glm::vec3 indicatorMaxCurrent_{};
    glm::vec3 currentBlockPosTarget_{};
    glm::vec3 currentBlockPosCurrent_{};
    std::shared_ptr<Assets::Node> previewNode_;
    bool hasValidPlacementTarget_ = false;
    bool previewWasVisible_ = false;
    std::string placementConflictReason_;

    // BGM Stuff
    uint32_t currentBGM_ = 0;
    std::vector<std::tuple<std::string, std::string>> bgmArray_;

    // Status
    bool bCapturing_ = false;
    bool mouseCapturedByUI_ = false;

    // cpu hit
    mutable glm::vec3 cachedCameraPos_;
    glm::vec3 cpuHit;
    uint32_t basementInstanceId_ = 0;

    // Cursor for script/AI control
    std::unique_ptr<MagicaLego::FCursor> cursor_;
};
