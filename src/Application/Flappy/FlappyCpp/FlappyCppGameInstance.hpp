#pragma once

#include "Application/Flappy/FlappyCommon.hpp"
#include "Application/Flappy/FlappyCpp/FlappyCppBird.hpp"
#include "Application/Flappy/FlappyCpp/FlappyCppPipes.hpp"
#include "Runtime/Engine.hpp"

class NextAudio;

namespace Flappy
{
    struct FFlappyTraceFrame
    {
        int frame = 0;
        float birdY = 0.0f;
        float birdVelocityY = 0.0f;
        int score = 0;
        EGameState state = EGameState::Ready;
    };
}

class FlappyCppGameInstance final : public NextGameInstanceBase
{
public:
    FlappyCppGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~FlappyCppGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OnKey(SDL_Event& event) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnGamepadInput(int16_t leftStickX,
                        int16_t leftStickY,
                        int16_t rightStickX,
                        int16_t rightStickY,
                        int16_t leftTrigger,
                        int16_t rightTrigger) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void SaveHotReloadState(FHotReloadState& state) const override;
    void LoadHotReloadState(const FHotReloadState& state) override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;

    const Flappy::FGameplayConfig& GetConfig() const { return config_; }
    Flappy::EGameState GetState() const { return state_; }
    int GetScore() const { return score_; }

private:
    NextEngine& GetEngine() const { return *engine_; }
    void ResetRuntime();
    void RestartScene();
    void StartOrFlap();
    void FixedStep(bool flapRequested);
    void Die();
    void PlaySfx(const char* path);
    void RunReplayToCompletion();
    void WriteReplayTrace(const std::vector<Flappy::FFlappyTraceFrame>& trace, int deathFrame) const;
    bool TryApplyPendingHotReloadState();

    NextEngine* engine_ = nullptr;
    Flappy::FGameplayConfig config_{};
    Flappy::FReplayConfig replayConfig_{};
    Flappy::FXorShift32 rng_{0x00C0FFEEu};
    Flappy::FFlappyCppBird bird_;
    Flappy::FFlappyCppPipes pipes_;
    Flappy::EGameState state_ = Flappy::EGameState::Ready;
    int score_ = 0;
    float fixedAccumulator_ = 0.0f;
    float deadTimer_ = 0.0f;
    bool pendingFlap_ = false;
    bool sceneReady_ = false;
    bool replayMode_ = false;
    bool replayDone_ = false;
    std::string pendingHotReloadState_;
};
