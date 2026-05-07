#include "Application/Flappy/FlappyCpp/FlappyCppGameInstance.hpp"

#include "Application/Flappy/FlappyConfig.hpp"
#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Loaders/FProcModel.h"
#include "Runtime/Scene/SceneBuilder.h"
#include "Runtime/Scene/NodeUtils.h"
#include "Runtime/Plugin/HotReloadState.hpp"
#include "Runtime/Plugin/PluginUi.hpp"
#include "Runtime/Subsystems/NextAudio.h"

#include <glm/ext.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace
{
    constexpr const char* gameplayConfigPath = "assets/configs/flappy/gameplay.json";
    constexpr const char* replayConfigPath = "assets/configs/flappy/replay.json";
    constexpr const char* flapSfx = "assets/sounds/flappy_flap.wav";
    constexpr const char* scoreSfx = "assets/sounds/flappy_score.wav";
    constexpr const char* hitSfx = "assets/sounds/flappy_hit.wav";

    std::shared_ptr<Assets::Node> CreateNode(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                             std::string_view name,
                                             const glm::vec3& translation,
                                             const glm::vec3& scale,
                                             uint32_t modelId,
                                             uint32_t materialId,
                                             bool visible = true)
    {
        auto node = SceneBuilder::CreateRenderNode(name,
                                                   translation,
                                                   scale,
                                                   static_cast<uint32_t>(nodes.size()),
                                                   modelId,
                                                   materialId,
                                                   visible);
        nodes.push_back(node);
        return node;
    }

    bool IsFlapKey(SDL_Keycode key)
    {
        return key == SDLK_SPACE;
    }

    bool IsGamepadSouth(const SDL_Event& event)
    {
        return (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
               event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH;
    }

    constexpr uint32_t UiColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8u) |
               (static_cast<uint32_t>(b) << 16u) | (static_cast<uint32_t>(a) << 24u);
    }

    std::filesystem::path FindProjectRoot()
    {
        std::filesystem::path cursor = std::filesystem::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            if (std::filesystem::exists(cursor / "AGENTS.md") && std::filesystem::exists(cursor / "assets"))
            {
                return cursor;
            }
            if (!cursor.has_parent_path())
            {
                break;
            }
            cursor = cursor.parent_path();
        }
        return std::filesystem::current_path();
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<FlappyCppGameInstance>(config, options, engine);
}

FlappyCppGameInstance::FlappyCppGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine),
    engine_(engine),
    replayMode_(options.FlappyReplay)
{
    ConfigureWindow(config, options, "FlappyCpp", 1280, 720, true);
}

void FlappyCppGameInstance::OnInit()
{
    config_ = Flappy::LoadGameplayConfig(gameplayConfigPath);
    rng_.Reset(config_.rngSeed);
    pipes_.Reset(config_.pipe);
    if (replayMode_)
    {
        replayConfig_ = Flappy::LoadReplayConfig(replayConfigPath);
    }

    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetShowFlags().DebugPhysicsOverlay = false;
    GetEngine().GetUserSettings().ShowOverlay = false;
    ResetRuntime();
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void FlappyCppGameInstance::OnTick(double deltaSeconds)
{
    if (replayMode_)
    {
        if (sceneReady_ && !replayDone_)
        {
            RunReplayToCompletion();
        }
        return;
    }

    if (state_ == Flappy::EGameState::Dead)
    {
        deadTimer_ += static_cast<float>(deltaSeconds);
    }

    fixedAccumulator_ += static_cast<float>(std::min(deltaSeconds, 0.25));
    while (fixedAccumulator_ >= config_.fixedDeltaSeconds)
    {
        const bool flap = pendingFlap_;
        pendingFlap_ = false;
        FixedStep(flap);
        fixedAccumulator_ -= config_.fixedDeltaSeconds;
    }

    if (sceneReady_)
    {
        GetEngine().GetScene().MarkTransformDirty();
    }
}

void FlappyCppGameInstance::OnDestroy() {}

bool FlappyCppGameInstance::OnRenderUI()
{
    glm::vec2 min;
    glm::vec2 size;
    if (!Runtime::PluginUi::GetMainViewportRect(min, size))
    {
        return false;
    }

    const glm::vec2 center(min.x + size.x * 0.5f, min.y + size.y * 0.5f);

    auto drawText = [](const std::string& text, const glm::vec2& pos, float scale, uint32_t color)
    {
        Runtime::PluginUi::AddText(text, pos, scale, color);
    };

    auto drawCenteredText = [&](const std::string& text, float y, float scale, uint32_t color)
    {
        const glm::vec2 rawSize = Runtime::PluginUi::CalcTextSize(text);
        const glm::vec2 textSize(rawSize.x * scale, rawSize.y * scale);
        const glm::vec2 pos(min.x + (size.x - textSize.x) * 0.5f, y);
        drawText(text, glm::vec2(pos.x + 2.0f, pos.y + 2.0f), scale, UiColor(20, 28, 35, 110));
        drawText(text, pos, scale, color);
    };

    auto drawPanel = [&](float width, float height, float y)
    {
        const float x = center.x - width * 0.5f;
        Runtime::PluginUi::AddRectFilled(glm::vec2(x + 6.0f, y + 8.0f),
                                         glm::vec2(x + width + 6.0f, y + height + 8.0f),
                                         UiColor(16, 28, 38, 80),
                                         18.0f);
        Runtime::PluginUi::AddRectFilled(glm::vec2(x, y),
                                         glm::vec2(x + width, y + height),
                                         UiColor(18, 32, 43, 210),
                                         18.0f);
        Runtime::PluginUi::AddRect(glm::vec2(x, y),
                                   glm::vec2(x + width, y + height),
                                   UiColor(255, 255, 255, 46),
                                   18.0f,
                                   1.5f);
        return glm::vec2(x, y);
    };

    const float scoreWidth = 136.0f + static_cast<float>(std::max(0, score_ / 10)) * 18.0f;
    const float scoreX = center.x - scoreWidth * 0.5f;
    Runtime::PluginUi::AddRectFilled(glm::vec2(scoreX + 3.0f, min.y + 27.0f),
                                     glm::vec2(scoreX + scoreWidth + 3.0f, min.y + 83.0f),
                                     UiColor(10, 24, 34, 80),
                                     16.0f);
    Runtime::PluginUi::AddRectFilled(glm::vec2(scoreX, min.y + 24.0f),
                                     glm::vec2(scoreX + scoreWidth, min.y + 80.0f),
                                     UiColor(24, 40, 52, 210),
                                     16.0f);
    Runtime::PluginUi::AddRect(glm::vec2(scoreX, min.y + 24.0f),
                               glm::vec2(scoreX + scoreWidth, min.y + 80.0f),
                               UiColor(255, 255, 255, 40),
                               16.0f,
                               1.0f);
    drawCenteredText(fmt::format("{}", score_), min.y + 31.0f, 2.05f, UiColor(255, 255, 255, 255));

    if (state_ == Flappy::EGameState::Ready)
    {
        const float panelWidth = std::max(280.0f, std::min(520.0f, size.x - 48.0f));
        const glm::vec2 panel = drawPanel(panelWidth, 178.0f, center.y - 96.0f);
        drawCenteredText("FLAPPY", panel.y + 26.0f, 2.0f, UiColor(255, 230, 102, 255));
        drawCenteredText("Thread the gap. Keep the rhythm.", panel.y + 76.0f, 1.0f, UiColor(211, 231, 236, 235));
        drawCenteredText("SPACE / CLICK / GAMEPAD A", panel.y + 121.0f, 1.1f, UiColor(124, 230, 168, 255));
    }
    else if (state_ == Flappy::EGameState::Dead)
    {
        const float panelWidth = std::max(280.0f, std::min(500.0f, size.x - 48.0f));
        const glm::vec2 panel = drawPanel(panelWidth, 166.0f, center.y - 88.0f);
        drawCenteredText("GAME OVER", panel.y + 24.0f, 1.75f, UiColor(255, 134, 122, 255));
        drawCenteredText(fmt::format("Score {}", score_), panel.y + 73.0f, 1.25f, UiColor(255, 255, 255, 245));
        drawCenteredText("PRESS ANY KEY TO RESTART", panel.y + 116.0f, 1.0f, UiColor(124, 230, 168, 245));
    }

    return false;
}

bool FlappyCppGameInstance::OnKey(SDL_Event& event)
{
    const bool pressed = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    if (!pressed)
    {
        return false;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)
    {
        return false;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
    {
        GetEngine().RequestClose();
        return true;
    }

    if (state_ == Flappy::EGameState::Dead && deadTimer_ >= config_.deadHitStopSeconds)
    {
        RestartScene();
        return true;
    }

    if ((event.type == SDL_EVENT_KEY_DOWN && IsFlapKey(event.key.key)) || IsGamepadSouth(event))
    {
        StartOrFlap();
        return true;
    }

    return false;
}

bool FlappyCppGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN || event.button.button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    if (state_ == Flappy::EGameState::Dead && deadTimer_ >= config_.deadHitStopSeconds)
    {
        RestartScene();
        return true;
    }

    StartOrFlap();
    return true;
}

bool FlappyCppGameInstance::OnGamepadInput(int16_t,
                                           int16_t,
                                           int16_t,
                                           int16_t,
                                           int16_t,
                                           int16_t)
{
    return false;
}

void FlappyCppGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>&,
                                               std::vector<Assets::AnimationTrack>&)
{
    sceneReady_ = false;
    pipes_.Reset(config_.pipe);

    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), config_.bird.radius));
    const uint32_t birdModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f), glm::vec3(0.5f)));
    const uint32_t boxModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f, -0.5f, -0.02f), glm::vec3(0.5f, 0.5f, 0.02f)));
    const uint32_t backgroundModelId = static_cast<uint32_t>(models.size() - 1);

    const uint32_t birdMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.82f, 0.12f));
    const uint32_t pipeMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.52f, 0.58f, 0.62f));
    const uint32_t boundaryMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.30f, 0.78f, 0.32f));
    const uint32_t backgroundMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.30f, 0.62f, 0.94f));

    auto birdNode = CreateNode(nodes,
                               "FlappyCpp_Bird",
                               config_.bird.initialPosition,
                               glm::vec3(1.0f),
                               birdModelId,
                               birdMaterialId);
    bird_.SetNode(birdNode);
    bird_.Reset(config_.bird);

    const float worldWidth = config_.world.maxX - config_.world.minX;
    CreateNode(nodes,
               "FlappyCpp_Floor",
               glm::vec3(0.0f, config_.world.minY - 0.2f, config_.world.gameplayZ),
               glm::vec3(worldWidth + 8.0f, 0.4f, 0.35f),
               boxModelId,
               boundaryMaterialId);
    CreateNode(nodes,
               "FlappyCpp_Ceiling",
               glm::vec3(0.0f, config_.world.maxY + 0.2f, config_.world.gameplayZ),
               glm::vec3(worldWidth + 8.0f, 0.4f, 0.35f),
               boxModelId,
               boundaryMaterialId);
    CreateNode(nodes,
               "FlappyCpp_Background",
               glm::vec3(0.0f, 0.0f, -2.0f),
               glm::vec3(worldWidth + 8.0f, config_.world.maxY - config_.world.minY + 2.0f, 1.0f),
               backgroundModelId,
               backgroundMaterialId);

    for (int index = 0; index < std::max(1, config_.pipe.poolSize); ++index)
    {
        auto topNode = CreateNode(nodes,
                                  fmt::format("FlappyCpp_PipeTop_{}", index),
                                  glm::vec3(0.0f, -40.0f, config_.world.gameplayZ),
                                  glm::vec3(config_.pipe.width, 1.0f, 0.35f),
                                  boxModelId,
                                  pipeMaterialId,
                                  false);
        auto bottomNode = CreateNode(nodes,
                                     fmt::format("FlappyCpp_PipeBottom_{}", index),
                                     glm::vec3(0.0f, -40.0f, config_.world.gameplayZ),
                                     glm::vec3(config_.pipe.width, 1.0f, 0.35f),
                                     boxModelId,
                                     pipeMaterialId,
                                     false);
        pipes_.SetNodes(static_cast<size_t>(index), topNode, bottomNode);
    }
}

void FlappyCppGameInstance::OnSceneLoaded()
{
    sceneReady_ = true;
    ResetRuntime();
    TryApplyPendingHotReloadState();
}

void FlappyCppGameInstance::SaveHotReloadState(FHotReloadState& state) const
{
    nlohmann::json flappyState;
    flappyState["score"] = score_;
    flappyState["state"] = static_cast<int>(state_);
    flappyState["fixedAccumulator"] = fixedAccumulator_;
    flappyState["deadTimer"] = deadTimer_;
    flappyState["pendingFlap"] = pendingFlap_;
    flappyState["rng"] = rng_.GetState();
    flappyState["bird"] = {
        {"x", bird_.GetPosition().x},
        {"y", bird_.GetPosition().y},
        {"z", bird_.GetPosition().z},
        {"velocityY", bird_.GetVelocityY()},
    };
    flappyState["pipeSpawnTimer"] = pipes_.GetSpawnTimer();
    flappyState["pipes"] = nlohmann::json::array();
    for (const Flappy::FPipeRuntime& pipe : pipes_.GetPipes())
    {
        flappyState["pipes"].push_back({
            {"x", pipe.x},
            {"gapCenterY", pipe.gapCenterY},
            {"active", pipe.active},
            {"scored", pipe.scored},
        });
    }
    state.Raw()["FlappyCpp"] = std::move(flappyState);
}

void FlappyCppGameInstance::LoadHotReloadState(const FHotReloadState& state)
{
    const auto it = state.Raw().find("FlappyCpp");
    if (it == state.Raw().end())
    {
        return;
    }

    pendingHotReloadState_ = it->dump();
    TryApplyPendingHotReloadState();
}

bool FlappyCppGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView =
        glm::lookAtRH(config_.camera.position, config_.camera.target, config_.camera.up);
    outRenderCamera.FieldOfView = config_.camera.fieldOfView;
    return true;
}

void FlappyCppGameInstance::ResetRuntime()
{
    state_ = Flappy::EGameState::Ready;
    score_ = 0;
    fixedAccumulator_ = 0.0f;
    deadTimer_ = 0.0f;
    pendingFlap_ = false;
    rng_.Reset(config_.rngSeed);
    bird_.Reset(config_.bird);
    pipes_.Reset(config_.pipe);
}

bool FlappyCppGameInstance::TryApplyPendingHotReloadState()
{
    if (pendingHotReloadState_.empty() || !sceneReady_)
    {
        return false;
    }

    nlohmann::json flappyState;
    try
    {
        flappyState = nlohmann::json::parse(pendingHotReloadState_);
    }
    catch (const std::exception& e)
    {
        SPDLOG_WARN("[HotReload] Failed to parse FlappyCpp hot reload state: {}", e.what());
        pendingHotReloadState_.clear();
        return false;
    }

    score_ = flappyState.value("score", score_);
    state_ = static_cast<Flappy::EGameState>(flappyState.value("state", static_cast<int>(state_)));
    fixedAccumulator_ = flappyState.value("fixedAccumulator", fixedAccumulator_);
    deadTimer_ = flappyState.value("deadTimer", deadTimer_);
    pendingFlap_ = flappyState.value("pendingFlap", pendingFlap_);
    rng_.Reset(flappyState.value("rng", rng_.GetState()));

    if (flappyState.contains("bird"))
    {
        const nlohmann::json& birdState = flappyState["bird"];
        bird_.RestoreRuntime(glm::vec3(birdState.value("x", bird_.GetPosition().x),
                                       birdState.value("y", bird_.GetPosition().y),
                                       birdState.value("z", bird_.GetPosition().z)),
                             birdState.value("velocityY", bird_.GetVelocityY()));
    }

    std::vector<Flappy::FPipeRuntime> pipeRuntime;
    if (flappyState.contains("pipes") && flappyState["pipes"].is_array())
    {
        for (const nlohmann::json& pipeState : flappyState["pipes"])
        {
            Flappy::FPipeRuntime pipe;
            pipe.x = pipeState.value("x", pipe.x);
            pipe.gapCenterY = pipeState.value("gapCenterY", pipe.gapCenterY);
            pipe.active = pipeState.value("active", pipe.active);
            pipe.scored = pipeState.value("scored", pipe.scored);
            pipeRuntime.push_back(pipe);
        }
    }
    pipes_.RestoreRuntime(flappyState.value("pipeSpawnTimer", pipes_.GetSpawnTimer()),
                          pipeRuntime,
                          config_.pipe,
                          config_.world);

    GetEngine().GetScene().MarkTransformDirty();
    pendingHotReloadState_.clear();
    SPDLOG_INFO("[HotReload] Restored FlappyCpp runtime state after plugin reload.");
    return true;
}

void FlappyCppGameInstance::RestartScene()
{
    ResetRuntime();
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void FlappyCppGameInstance::StartOrFlap()
{
    if (state_ == Flappy::EGameState::Ready)
    {
        state_ = Flappy::EGameState::Playing;
        pendingFlap_ = true;
        PlaySfx(flapSfx);
        return;
    }

    if (state_ == Flappy::EGameState::Playing)
    {
        pendingFlap_ = true;
        PlaySfx(flapSfx);
    }
}

void FlappyCppGameInstance::FixedStep(bool flapRequested)
{
    if (state_ == Flappy::EGameState::Ready)
    {
        if (flapRequested)
        {
            state_ = Flappy::EGameState::Playing;
            bird_.Flap(config_.bird);
        }
        else
        {
            bird_.SyncVisual();
            return;
        }
    }

    if (state_ != Flappy::EGameState::Playing)
    {
        return;
    }

    if (flapRequested)
    {
        bird_.Flap(config_.bird);
    }

    bird_.Update(config_.fixedDeltaSeconds, config_.bird);
    pipes_.Update(config_.fixedDeltaSeconds, config_.pipe, config_.world, rng_);

    const int scored = pipes_.ConsumeScoreEvents(config_.bird.initialPosition.x);
    if (scored > 0)
    {
        score_ += scored;
        PlaySfx(scoreSfx);
    }

    const float minBirdY = config_.world.minY + config_.bird.radius;
    const float maxBirdY = config_.world.maxY - config_.bird.radius;
    const glm::vec3& birdPosition = bird_.GetPosition();
    if (birdPosition.y < minBirdY || birdPosition.y > maxBirdY ||
        pipes_.CheckCollision(birdPosition, config_.bird.radius, config_.pipe))
    {
        Die();
    }
}

void FlappyCppGameInstance::Die()
{
    if (state_ == Flappy::EGameState::Dead)
    {
        return;
    }
    state_ = Flappy::EGameState::Dead;
    deadTimer_ = 0.0f;
    PlaySfx(hitSfx);
}

void FlappyCppGameInstance::PlaySfx(const char* path)
{
    if (NextAudio* audio = GetEngine().GetAudio())
    {
        audio->PlaySfx(path, 1.0f);
    }
}

void FlappyCppGameInstance::RunReplayToCompletion()
{
    replayDone_ = true;
    ResetRuntime();
    std::unordered_set<int> flapFrames(replayConfig_.flapFrames.begin(), replayConfig_.flapFrames.end());
    std::vector<Flappy::FFlappyTraceFrame> trace;
    trace.reserve(static_cast<size_t>(std::max(0, replayConfig_.maxFrames)));
    int deathFrame = -1;

    for (int frame = 0; frame < replayConfig_.maxFrames; ++frame)
    {
        const bool flap = flapFrames.contains(frame);
        FixedStep(flap);
        trace.push_back(Flappy::FFlappyTraceFrame{
            .frame = frame,
            .birdY = bird_.GetPosition().y,
            .birdVelocityY = bird_.GetVelocityY(),
            .score = score_,
            .state = state_,
        });

        if (deathFrame < 0 && state_ == Flappy::EGameState::Dead)
        {
            deathFrame = frame;
        }
    }

    WriteReplayTrace(trace, deathFrame);
    GetEngine().RequestClose();
}

void FlappyCppGameInstance::WriteReplayTrace(const std::vector<Flappy::FFlappyTraceFrame>& trace, int deathFrame) const
{
    nlohmann::json document;
    document["implementation"] = "FlappyCpp";
    document["fixedDeltaSeconds"] = config_.fixedDeltaSeconds;
    document["rngSeed"] = config_.rngSeed;
    document["deathFrame"] = deathFrame;
    document["frames"] = nlohmann::json::array();
    for (const Flappy::FFlappyTraceFrame& frame : trace)
    {
        document["frames"].push_back({
            {"frame", frame.frame},
            {"birdY", frame.birdY},
            {"birdVelocityY", frame.birdVelocityY},
            {"score", frame.score},
            {"state", Flappy::ToString(frame.state)},
        });
    }

    const std::filesystem::path outputDir = FindProjectRoot() / "out";
    std::filesystem::create_directories(outputDir);
    const std::filesystem::path outputPath = outputDir / "flappy_cpp_trace.json";
    std::ofstream output(outputPath);
    output << document.dump(2) << '\n';
    SPDLOG_INFO("[FlappyCpp] replay trace written to {}", outputPath.string());
}
