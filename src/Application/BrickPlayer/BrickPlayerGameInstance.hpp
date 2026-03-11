#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Utilities/NextEngineHelper.h"

class BrickPlayerUserInterface;

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

    // Physics debug
    bool IsShowPhysicsDebug() const { return showPhysicsDebug_; }
    void TogglePhysicsDebug() { showPhysicsDebug_ = !showPhysicsDebug_; }
    void DrawPhysicsDebug();

    // File dialog
    void OpenFileDialog();

    bool IsSceneLoaded() const { return sceneLoaded_; }

private:
    void UpdateVisibilityForStep(int32_t step);
    void BuildPerPartOrder();
    void CreateFloorPhysicsBody();
    void PerformRaycast();

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
    int32_t totalSteps_ = 0;
    int32_t totalParts_ = 0;
    int32_t currentStep_ = 0;
    bool perPartMode_ = false;

    // Auto-play
    bool autoPlay_ = false;
    int32_t playSpeedIndex_ = 2;
    float autoPlayTimer_ = 0.0f;

    // Disassemble
    struct DisassembledInfo
    {
        glm::vec3 halfExtent;
    };
    std::unordered_map<uint32_t, DisassembledInfo> disassembledNodes_;
    uint32_t selectedInstanceId_ = UINT32_MAX;
    glm::vec3 selectedHitNormal_{0.0f, 1.0f, 0.0f};
    bool showPhysicsDebug_ = false;

    // State
    bool sceneLoaded_ = false;
    bool mouseCapturedByUI_ = false;
    std::string currentScenePath_;

    // UI
    std::unique_ptr<BrickPlayerUserInterface> userInterface_;

    friend class BrickPlayerUserInterface;
};
