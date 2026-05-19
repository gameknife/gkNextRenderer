#include "FlappyCpp/FlappyCppGameInstance.hpp"

#include "FlappyConfig.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Runtime/Scene/NodeUtils.h"
#include "Engine/Runtime/Subsystems/NextAudio.h"

#include <glm/ext.hpp>
#include <imgui.h>
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
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport)
    {
        return false;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const ImVec2 min = viewport->Pos;
    const ImVec2 size = viewport->Size;
    const ImVec2 center(min.x + size.x * 0.5f, min.y + size.y * 0.5f);

    auto drawText = [&](const std::string& text, const ImVec2& pos, float scale, ImU32 color)
    {
        drawList->AddText(nullptr, ImGui::GetFontSize() * scale, pos, color, text.c_str());
    };

    auto drawCenteredText = [&](const std::string& text, float y, float scale, ImU32 color)
    {
        const ImVec2 rawSize = ImGui::CalcTextSize(text.c_str());
        const ImVec2 textSize(rawSize.x * scale, rawSize.y * scale);
        const ImVec2 pos(min.x + (size.x - textSize.x) * 0.5f, y);
        drawText(text, ImVec2(pos.x + 2.0f, pos.y + 2.0f), scale, IM_COL32(20, 28, 35, 110));
        drawText(text, pos, scale, color);
    };

    auto drawPanel = [&](float width, float height, float y)
    {
        const float x = center.x - width * 0.5f;
        drawList->AddRectFilled(ImVec2(x + 6.0f, y + 8.0f),
                                ImVec2(x + width + 6.0f, y + height + 8.0f),
                                IM_COL32(16, 28, 38, 80),
                                18.0f);
        drawList->AddRectFilled(ImVec2(x, y),
                                ImVec2(x + width, y + height),
                                IM_COL32(18, 32, 43, 210),
                                18.0f);
        drawList->AddRect(ImVec2(x, y),
                          ImVec2(x + width, y + height),
                          IM_COL32(255, 255, 255, 46),
                          18.0f,
                          0,
                          1.5f);
        return ImVec2(x, y);
    };

    const float scoreWidth = 136.0f + static_cast<float>(std::max(0, score_ / 10)) * 18.0f;
    const float scoreX = center.x - scoreWidth * 0.5f;
    drawList->AddRectFilled(ImVec2(scoreX + 3.0f, min.y + 27.0f),
                            ImVec2(scoreX + scoreWidth + 3.0f, min.y + 83.0f),
                            IM_COL32(10, 24, 34, 80),
                            16.0f);
    drawList->AddRectFilled(ImVec2(scoreX, min.y + 24.0f),
                            ImVec2(scoreX + scoreWidth, min.y + 80.0f),
                            IM_COL32(24, 40, 52, 210),
                            16.0f);
    drawList->AddRect(ImVec2(scoreX, min.y + 24.0f),
                      ImVec2(scoreX + scoreWidth, min.y + 80.0f),
                      IM_COL32(255, 255, 255, 40),
                      16.0f,
                      0,
                      1.0f);
    drawCenteredText(fmt::format("{}", score_), min.y + 31.0f, 2.05f, IM_COL32(255, 255, 255, 255));

    if (state_ == Flappy::EGameState::Ready)
    {
        const float panelWidth = std::max(280.0f, std::min(520.0f, size.x - 48.0f));
        const ImVec2 panel = drawPanel(panelWidth, 178.0f, center.y - 96.0f);
        drawCenteredText("FLAPPY", panel.y + 26.0f, 2.0f, IM_COL32(255, 230, 102, 255));
        drawCenteredText("Thread the gap. Keep the rhythm.", panel.y + 76.0f, 1.0f, IM_COL32(211, 231, 236, 235));
        drawCenteredText("SPACE / CLICK / GAMEPAD A", panel.y + 121.0f, 1.1f, IM_COL32(124, 230, 168, 255));
    }
    else if (state_ == Flappy::EGameState::Dead)
    {
        const float panelWidth = std::max(280.0f, std::min(500.0f, size.x - 48.0f));
        const ImVec2 panel = drawPanel(panelWidth, 166.0f, center.y - 88.0f);
        drawCenteredText("GAME OVER", panel.y + 24.0f, 1.75f, IM_COL32(255, 134, 122, 255));
        drawCenteredText(fmt::format("Score {}", score_), panel.y + 73.0f, 1.25f, IM_COL32(255, 255, 255, 245));
        drawCenteredText("PRESS ANY KEY TO RESTART", panel.y + 116.0f, 1.0f, IM_COL32(124, 230, 168, 245));
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
