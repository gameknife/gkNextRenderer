#pragma once
#include "Common/CoreMinimal.hpp"
#include "BrickPlayerLDrawShadow.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Utilities/NextEngineHelper.h"

class BrickPlayerUserInterface;
namespace Assets { class Node; }
namespace Runtime { class PhysicsComponent; }

class BrickPlayerGameInstance : public NextGameInstanceBase
{
public:
    BrickPlayerGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {}
    bool OnRenderUI() override;
    void OnInitUI() override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    void OnRayHitResponse(Assets::RayCastResult& result) override;
    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;
    void OnSceneLoaded() override;
    void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) override;

    NextEngine& GetEngine() { return *engine_; }

    // Timeline
    void SetCurrentStep(int32_t step);
    void StepForward();
    void StepBackward();
    int32_t GetCurrentStep() const { return currentStep_; }
    int32_t GetTotalSteps() const { return perPartMode_ ? totalParts_ : totalSteps_; }

    // Per-part mode
    bool IsPerPartMode() const { return perPartMode_; }
    bool HasBuildSteps() const { return totalSteps_ > 1; }
    void SetPerPartMode(bool enabled);

    // Auto-play
    bool IsAutoPlaying() const { return autoPlay_; }
    void ToggleAutoPlay();
    void CyclePlaySpeed();
    int32_t GetPlaySpeedIndex() const { return playSpeedIndex_; }
    const char* GetPlaySpeedLabel() const;

    // Disassemble
    void DisassembleSelected();
    void ResetAll();
    bool HasSelection() const { return selectedInstanceId_ != UINT32_MAX; }

    // Music
    void PlayNextBGM();
    bool IsBGMPaused() const;
    void PauseBGM(bool pause);
    std::string GetCurrentBGMName() const;

    // Physics debug
    bool IsShowPhysicsDebug() const { return showPhysicsDebug_; }
    void TogglePhysicsDebug() { showPhysicsDebug_ = !showPhysicsDebug_; }
    void DrawPhysicsDebug();
    void DrawSnapConfirmation() const;
    bool IsShowSnapDebug() const { return showSnapDebug_; }
    void ToggleSnapDebug() { showSnapDebug_ = !showSnapDebug_; }
    void DrawSnapDebug() const;

    // File dialog
    void OpenFileDialog();

    bool IsSceneLoaded() const { return sceneLoaded_; }

private:
    struct OriginalAssemblyState
    {
        uint32_t parentInstanceId = UINT32_MAX;
        glm::vec3 localTranslation{0.0f};
        glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 localScale{1.0f};
        glm::vec3 worldTranslation{0.0f};
        glm::quat worldRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 worldScale{1.0f};
        glm::vec3 worldAabbMin{0.0f};
        glm::vec3 worldAabbMax{0.0f};
    };

    struct DragSnapCandidate
    {
        bool valid = false;
        uint32_t targetInstanceId = UINT32_MAX;
        glm::vec3 desiredTranslation{0.0f};
        glm::quat desiredRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 desiredBodyPosition{0.0f};
        bool restoreOriginalHierarchy = false;
        int32_t draggedConnectorIndex = -1;
        int32_t targetConnectorIndex = -1;
    };

    struct WorldSnapConnector
    {
        const BrickPlayer::Shadow::FSnapConnector* connector = nullptr;
        uint32_t ownerInstanceId = UINT32_MAX;
        glm::vec3 worldPosition{0.0f};
        glm::quat worldRotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 worldAxis{0.0f, 1.0f, 0.0f};
    };

    void UpdateVisibilityForStep(int32_t step, bool playPlacementSound = false);
    void BuildPerPartOrder();
    void CaptureOriginalAssemblyState();
    void CreateFloorPhysicsBody();
    void PerformRaycast();
    bool UpdateHitStateFromRaycast(const Assets::RayCastResult& result);
    bool StartDraggingHoveredPart();
    void StopDraggingPart();
    void UpdateDraggedPart();
    bool TryBuildSnapCandidate(Assets::Node* node,
                               const std::shared_ptr<Runtime::PhysicsComponent>& physComp,
                               const glm::vec3& freeBodyPosition,
                               DragSnapCandidate& outCandidate);
    bool TryBuildShadowSnapCandidate(Assets::Node* node,
                                     const std::shared_ptr<Runtime::PhysicsComponent>& physComp,
                                     const glm::vec3& freeBodyPosition,
                                     DragSnapCandidate& outCandidate);
    bool TryBuildOriginalSnapCandidate(const std::shared_ptr<Runtime::PhysicsComponent>& physComp,
                                       const glm::vec3& freeBodyPosition,
                                       DragSnapCandidate& outCandidate);
    std::vector<WorldSnapConnector> BuildWorldConnectors(uint32_t instanceId) const;
    bool AreConnectorsCompatible(const BrickPlayer::Shadow::FSnapConnector& dragged,
                                 const BrickPlayer::Shadow::FSnapConnector& target) const;
    int ScoreShadowCandidate(uint32_t draggedId,
                             const glm::quat& desiredRotation,
                             const glm::vec3& desiredTranslation,
                             const BrickPlayer::Shadow::FSnapConnector& anchorDragged,
                             const WorldSnapConnector& anchorTarget) const;
    bool AreNodesOriginallyConnectable(uint32_t draggedId, uint32_t targetId) const;
    float GetSnapDistanceThreshold(uint32_t draggedId) const;
    void SetDraggedPartRayCastVisible(bool visible);
    bool ReattachDraggedPart();
    void PlayRandomPutSound();
    int32_t FindDraggedConnectorLock(uint32_t instanceId, const glm::vec3& hitPoint, const glm::vec3& hitNormal) const;
    const BrickPlayer::Shadow::FSnapConnector* GetLockedDraggedConnector(uint32_t instanceId) const;
    bool IntersectDragPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDir, glm::vec3& outPoint) const;
    void ApplyPhysicsPoseToNode(Assets::Node* node, const glm::vec3& bodyPosition, const glm::quat& bodyRotation);
    bool GetDraggedBodyMinimumY(Assets::Node* node, const glm::quat& bodyRotation, float& outMinBodyY) const;
    bool ClampDraggedBodyPositionAboveFloor(Assets::Node* node,
                                            const glm::quat& bodyRotation,
                                            glm::vec3& inOutBodyPosition) const;

    NextEngine* engine_;

    // Camera orbit
    float cameraRotX_ = 45.0f;
    float cameraRotY_ = 30.0f;
    float cameraArm_ = 5.0f;
    float cameraFOV_ = 30.0f;
    glm::vec3 cameraCenter_{0.0f};
    glm::vec3 realCameraCenter_{0.0f};
    mutable glm::vec3 cachedCameraPos_{0.0f};
    glm::vec3 panForward_{0.0f, 0.0f, 1.0f};
    glm::vec3 panLeft_{1.0f, 0.0f, 0.0f};
    float cameraMultiplier_ = 0.0f;

    // Mouse
    glm::dvec2 mousePos_{0.0};
    bool mouseLeftDown_ = false;
    bool mouseRightDown_ = false;
    bool isOrbitDragging_ = false;
    bool resetMouse_ = true;

    // Timeline
    std::unordered_map<uint32_t, int32_t> nodeStepMap_;
    std::unordered_map<uint32_t, int32_t> nodePartOrder_;
    std::unordered_map<uint32_t, std::string> nodePartFileMap_;
    int32_t totalSteps_ = 0;
    int32_t totalParts_ = 0;
    int32_t currentStep_ = 0;
    bool perPartMode_ = false;

    // Auto-play
    bool autoPlay_ = false;
    int32_t playSpeedIndex_ = 2;
    float autoPlayTimer_ = 0.0f;
    uint32_t currentBGM_ = 0;
    std::vector<std::tuple<std::string, std::string>> bgmArray_;

    // Disassemble
    struct DisassembledInfo
    {
        glm::vec3 halfExtent;
    };
    std::unordered_map<uint32_t, DisassembledInfo> disassembledNodes_;
    std::unordered_map<uint32_t, OriginalAssemblyState> originalAssemblyStates_;
    uint32_t selectedInstanceId_ = UINT32_MAX;
    glm::vec3 selectedHitNormal_{0.0f, 1.0f, 0.0f};
    uint32_t hoveredDisassembledInstanceId_ = UINT32_MAX;
    glm::vec3 hoveredHitPoint_{0.0f};
    glm::vec3 hoveredHitNormal_{0.0f, 1.0f, 0.0f};
    uint32_t hoveredAssemblyInstanceId_ = UINT32_MAX;
    glm::vec3 hoveredAssemblyHitPoint_{0.0f};
    glm::vec3 hoveredAssemblyHitNormal_{0.0f, 1.0f, 0.0f};
    bool isDraggingPart_ = false;
    uint32_t draggedInstanceId_ = UINT32_MAX;
    glm::vec3 dragPlanePoint_{0.0f};
    glm::vec3 dragPlaneNormal_{0.0f, 0.0f, 1.0f};
    glm::vec3 dragBodyOffset_{0.0f};
    int32_t lockedDraggedConnectorIndex_ = -1;
    DragSnapCandidate activeSnapCandidate_{};
    float snapFeedbackPulseUntil_ = 0.0f;
    bool showPhysicsDebug_ = false;
    bool showSnapDebug_ = false;
    bool hasFloorPlane_ = false;
    float floorPlaneY_ = 0.0f;
    float floorSurfaceY_ = 0.0f;

    // State
    bool sceneLoaded_ = false;
    bool mouseCapturedByUI_ = false;
    std::string currentScenePath_;

    // UI
    std::unique_ptr<BrickPlayerUserInterface> userInterface_;
    BrickPlayer::Shadow::FShadowLibrary shadowLibrary_;

    friend class BrickPlayerUserInterface;
};
