#include "KongLie3DGameInstance.hpp"

#include <imgui.h>

#include "KongLie3DBoard.hpp"
#include "KongLie3DAudio.hpp"
#include "KongLie3DUI.hpp"
#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Loaders/FProcModel.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Runtime/Utilities/NextEngineHelper.h"
#include "KongLie3DDataLoader.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Utilities/Exception.hpp"
#include "Utilities/FileHelper.hpp"

#include <spdlog/spdlog.h>

namespace
{
    constexpr const char* PiecesConfigPath = "assets/configs/konglie/pieces.json";
    constexpr const char* PlacementConfigPath = "assets/configs/konglie/placement.json";
    constexpr const char* RelicsConfigPath = "assets/configs/konglie/relics.json";
    constexpr const char* SynergiesConfigPath = "assets/configs/konglie/synergies.json";
    constexpr int BoardCols = 7;
    constexpr int PlayerBoardRowMin = 4;
    constexpr int PlayerBoardRowMax = 7;
    constexpr int BenchRow = 8;
    constexpr int BenchSlots = 3;
    constexpr float BenchWorldZ = 8.5f;
    constexpr float DragLiftHeight = 0.4f;
    const glm::vec3 HiddenKnockoutProxyPosition(0.0f, -20.0f, 0.0f);

    [[noreturn]] void LogAndThrow(const std::string& message)
    {
        SPDLOG_ERROR("[KongLie3D] {}", message);
        Throw(std::runtime_error(message));
    }

    glm::vec3 GetPieceDimensions(const KongLie3D::FPieceDef& pieceDef)
    {
        glm::vec3 dimensions{};
        if (pieceDef.role == "tank")
        {
            dimensions = glm::vec3(0.7f, 0.5f, 0.7f);
        }
        else if (pieceDef.role == "adc")
        {
            dimensions = glm::vec3(0.4f, 0.9f, 0.4f);
        }
        else if (pieceDef.role == "support")
        {
            dimensions = glm::vec3(0.5f, 0.6f, 0.5f);
        }
        else if (pieceDef.role == "atk_tank")
        {
            dimensions = glm::vec3(0.55f, 0.7f, 0.55f);
        }
        else
        {
            LogAndThrow(fmt::format("Unsupported piece role '{}'", pieceDef.role));
        }

        if (pieceDef.isHero)
        {
            dimensions.y += 0.2f;
        }
        return dimensions;
    }

    std::shared_ptr<Assets::Node> CreateRenderNode(const std::string& nodeName,
                                                   const glm::vec3& translation,
                                                   const glm::vec3& scale,
                                                   uint32_t instanceId,
                                                   uint32_t modelId,
                                                   uint32_t materialId)
    {
        auto node =
            Assets::Node::CreateNode(nodeName, translation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), scale, instanceId);
        auto renderComponent = std::make_shared<Runtime::RenderComponent>();
        renderComponent->SetModelId(modelId);
        renderComponent->SetMaterial({materialId});
        renderComponent->SetVisible(true);
        node->AddComponent(renderComponent);
        return node;
    }

    bool IsBoardCell(int col, int row)
    {
        return col >= 0 && col < BoardCols && row >= PlayerBoardRowMin && row <= PlayerBoardRowMax;
    }

    bool IsBenchCell(int col, int row)
    {
        return row == BenchRow && col >= 0 && col < BenchSlots;
    }

    bool IsPlayerDeployCell(int col, int row)
    {
        return IsBoardCell(col, row) || IsBenchCell(col, row);
    }

    float GetWorldZForLogicalRow(int row)
    {
        return row == BenchRow ? BenchWorldZ : static_cast<float>(row);
    }

    glm::vec3 GetDeploymentWorldPosition(const KongLie3D::FPieceRuntime& piece)
    {
        return glm::vec3(static_cast<float>(piece.col),
                         piece.dimensions.y * 0.5f * piece.visualScale,
                         GetWorldZForLogicalRow(piece.row));
    }

    const ImWchar* GetKongLieGlyphRanges(ImFontAtlas& fontAtlas)
    {
        static ImVector<ImWchar> glyphRanges;
        if (glyphRanges.empty())
        {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(fontAtlas.GetGlyphRangesChineseSimplifiedCommon());
            builder.AddText(KongLie3D::U8Text(u8"✓⚡◈◆◇★◎"));
            builder.BuildRanges(&glyphRanges);
        }

        return glyphRanges.Data;
    }

    glm::vec3 ClampColor(const glm::vec3& color)
    {
        return glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    }

    glm::vec3 BrightenColor(const glm::vec3& color, float scale, float add = 0.0f)
    {
        return ClampColor(color * scale + glm::vec3(add));
    }

    const char* GetRendererName(int rendererType)
    {
        switch (rendererType)
        {
        case 0:
            return "PathTracing";
        case 1:
            return "SoftTracing";
        case 2:
            return "PureAmbient";
        case 3:
            return "VoxelTracing";
        default:
            return "Unknown";
        }
    }

    uint32_t AddLambertMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color)
    {
        materials.push_back({Assets::Material::Lambertian(ClampColor(color))});
        return static_cast<uint32_t>(materials.size() - 1);
    }

    std::shared_ptr<Assets::Node> CreateAttachmentNode(const std::string& nodeName,
                                                       const glm::vec3& translation,
                                                       const glm::vec3& scale,
                                                       uint32_t instanceId,
                                                       uint32_t modelId,
                                                       uint32_t materialId)
    {
        return CreateRenderNode(nodeName, translation, scale, instanceId, modelId, materialId);
    }

    void RegisterPieceAttachment(KongLie3D::FPieceRuntime& runtime,
                                 size_t pieceIndex,
                                 const std::shared_ptr<Assets::Node>& childNode,
                                 uint32_t materialId,
                                 std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                 std::unordered_map<uint32_t, size_t>& pieceInstanceLookup)
    {
        runtime.visualAttachments.emplace_back(childNode, materialId);
        pieceInstanceLookup[childNode->GetInstanceId()] = pieceIndex;
        nodes.push_back(childNode);
    }

    void BuildPieceVisual(const KongLie3D::FPieceDef& pieceDef,
                          KongLie3D::FPieceRuntime& runtime,
                          size_t pieceIndex,
                          uint32_t attachmentBoxModelId,
                          uint32_t attachmentSphereModelId,
                          std::vector<std::shared_ptr<Assets::Node>>& nodes,
                          std::unordered_map<uint32_t, size_t>& pieceInstanceLookup)
    {
        if (!runtime.node)
        {
            return;
        }

        const float facingDir = pieceDef.team == "player" ? -1.0f : 1.0f;
        const glm::vec3& dims = runtime.dimensions;

        auto addBoxAttachment = [&](std::string_view suffix, const glm::vec3& translation, const glm::vec3& scale, uint32_t materialId)
        {
            auto childNode = CreateAttachmentNode(runtime.pieceId + "_" + std::string(suffix),
                                                  translation,
                                                  scale,
                                                  static_cast<uint32_t>(nodes.size()),
                                                  attachmentBoxModelId,
                                                  materialId);
            childNode->SetParent(runtime.node);
            RegisterPieceAttachment(runtime, pieceIndex, childNode, materialId, nodes, pieceInstanceLookup);
        };

        auto addSphereAttachment = [&](std::string_view suffix, const glm::vec3& translation, const glm::vec3& scale, uint32_t materialId)
        {
            auto childNode = CreateAttachmentNode(runtime.pieceId + "_" + std::string(suffix),
                                                  translation,
                                                  scale,
                                                  static_cast<uint32_t>(nodes.size()),
                                                  attachmentSphereModelId,
                                                  materialId);
            childNode->SetParent(runtime.node);
            RegisterPieceAttachment(runtime, pieceIndex, childNode, materialId, nodes, pieceInstanceLookup);
        };

        if (pieceDef.role == "tank")
        {
            if (pieceDef.team == "enemy")
            {
                addBoxAttachment("lance",
                                 glm::vec3(0.0f, 0.04f, facingDir * (dims.z * 0.5f + 0.28f)),
                                 glm::vec3(0.15f, 0.15f, 0.5f),
                                 runtime.accentMaterialId);
            }
            else
            {
                addBoxAttachment("shield",
                                 glm::vec3(0.0f, dims.y * 0.58f, 0.0f),
                                 glm::vec3(1.1f, 0.1f, 0.1f),
                                 runtime.accentMaterialId);
            }
        }
        else if (pieceDef.role == "adc")
        {
            addBoxAttachment("weapon",
                             glm::vec3(0.0f, 0.02f, facingDir * (dims.z * 0.5f + 0.34f)),
                             glm::vec3(0.10f, 0.15f, 0.6f),
                             runtime.accentMaterialId);
        }
        else if (pieceDef.role == "support")
        {
            addBoxAttachment("staff",
                             glm::vec3(dims.x * 0.34f, dims.y * 0.20f, 0.0f),
                             glm::vec3(0.08f, 0.5f, 0.08f),
                             runtime.accentMaterialId);
            addSphereAttachment("staffOrb",
                                glm::vec3(dims.x * 0.34f, dims.y * 0.72f, 0.0f),
                                glm::vec3(0.62f),
                                runtime.glowMaterialId);
        }
        else if (pieceDef.role == "atk_tank")
        {
            addBoxAttachment("blade",
                             glm::vec3(0.0f, dims.y * 0.58f, facingDir * 0.04f),
                             glm::vec3(0.08f, 0.4f, 0.08f),
                             runtime.accentMaterialId);
        }

        if (pieceDef.isHero)
        {
            addSphereAttachment("heroSphere",
                                glm::vec3(0.0f, dims.y * 0.70f, 0.0f),
                                glm::vec3(1.0f),
                                runtime.glowMaterialId);
        }
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<KongLie3DGameInstance>(config, options, engine);
}

KongLie3DGameInstance::KongLie3DGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine),
    engine_(engine)
{
    config.Title = "KongLie3D";
    config.Width = 1280;
    config.Height = 720;
}

void KongLie3DGameInstance::OnInit()
{
    pieceDefs_ = KongLie3D::LoadPieces(PiecesConfigPath);
    placement_ = KongLie3D::LoadPlacement(PlacementConfigPath);
    if (placement_.levels.empty())
    {
        LogAndThrow("Placement config must define at least one level");
    }
    battleSystem_.SetRelics(KongLie3D::LoadRelics(RelicsConfigPath));
    battleSystem_.SetSynergies(KongLie3D::LoadSynergies(SynergiesConfigPath));
    GetEngine().SetGraphicsDebugPanelVisible(false);
    GetEngine().SetPhysicsDebugOverlayVisible(false);
    GetEngine().GetUserSettings().ShowOverlay = false;

    GetEngine().RequestLoadScene("Empty.proc");
}

void KongLie3DGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg{};
    cfg.MergeMode = false;

    const std::string fontPath = Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf");
    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, &cfg, GetKongLieGlyphRanges(*io.Fonts));
    if (font)
    {
        io.FontDefault = font;
    }
    else
    {
        SPDLOG_ERROR("[KongLie3D] Failed to load UI font '{}'", fontPath);
    }
}

void KongLie3DGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    cvars.RegisterFloat("audio.sfxVolume",
                        KongLie3D::KongLieSfxVolume,
                        &KongLie3D::KongLieSfxVolume,
                        NextCVar::ECVarFlags::Archive,
                        "KongLie3D sound effect volume");
    cvars.RegisterFloat("battle.speedMultiplier",
                        1.0f,
                        battleSystem_.GetSpeedMultiplierCVarPtr(),
                        NextCVar::ECVarFlags::Archive,
                        "KongLie3D battle simulation speed");
}

void KongLie3DGameInstance::OnTick(double deltaSeconds)
{
    if (battleSystem_.GetState() != KongLie3D::EBattleState::Deployment && draggingPiece_)
    {
        CancelDraggingPiece();
    }

    UpdateHoveredPieceTooltip(deltaSeconds);
    if (battleSystem_.GetState() == KongLie3D::EBattleState::Deployment && !deploymentHintDismissed_)
    {
        deploymentHintElapsedMs_ += static_cast<float>(deltaSeconds * 1000.0);
    }
    battleSystem_.Update(deltaSeconds);
    if (battleSystem_.ConsumeSceneDirty())
    {
        GetEngine().GetScene().MarkDirty();
    }
}

void KongLie3DGameInstance::OnDestroy()
{
    pieceRuntimes_.clear();
    pieceDefs_.clear();
}

bool KongLie3DGameInstance::OnRenderUI()
{
    if (showMvpWindow_)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 windowPivot(1.0f, 0.0f);
        const ImVec2 windowPadding(16.0f, 16.0f);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - windowPadding.x, viewport->WorkPos.y + windowPadding.y),
            ImGuiCond_Always,
            windowPivot);
        ImGui::SetNextWindowBgAlpha(0.85f);

        constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                                 ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin("KongLie3D MVP", &showMvpWindow_, windowFlags))
        {
            ImGui::Text("Phase 1: bootstrap OK");
            ImGui::Text("State: %s",
                        battleSystem_.GetState() == KongLie3D::EBattleState::Deployment ? "Deployment" :
                        (battleSystem_.GetState() == KongLie3D::EBattleState::Battle ? "Battle" : "Ended"));
        }
        ImGui::End();
    }

    KongLie3D::RenderHUD(*this);

    return false;
}

bool KongLie3DGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
    {
        return false;
    }

    switch (event.key.key)
    {
    case SDLK_SPACE:
        if (battleSystem_.GetState() != KongLie3D::EBattleState::Deployment)
        {
            return false;
        }

        if (draggingPiece_)
        {
            CancelDraggingPiece();
        }
        StartBattle();
        return true;
    case SDLK_F3:
    {
        auto& cvars = GetEngine().GetCVarSystem();
        bool found = false;
        const std::string value = cvars.GetValueString("r.rendererType", &found);
        const int current = found ? std::clamp(std::stoi(value), 0, 3) : 1;
        const int next = (current + 1) % 4;
        std::string error;
        cvars.SetValueFromString("r.rendererType", std::to_string(next), NextCVar::ECVarSetBy::Console, &error);
        if (!error.empty())
        {
            spdlog::warn("[KongLie3D] Failed to switch renderer: {}", error);
        }
        else
        {
            spdlog::info("[KongLie3D] Renderer switched to {}", GetRendererName(next));
        }
        return true;
    }
    case SDLK_P:
        if (battleSystem_.GetState() == KongLie3D::EBattleState::Battle)
        {
            battleSystem_.TogglePause();
            return true;
        }
        break;
    case SDLK_ESCAPE:
        if (draggingPiece_)
        {
            CancelDraggingPiece();
            return true;
        }
        if (battleSystem_.GetState() == KongLie3D::EBattleState::Ended)
        {
            ResetBattle();
            return true;
        }
        break;
    case SDLK_1:
        battleSystem_.SetSpeedMultiplier(1.0f);
        return true;
    case SDLK_2:
        battleSystem_.SetSpeedMultiplier(2.0f);
        return true;
    case SDLK_4:
        battleSystem_.SetSpeedMultiplier(4.0f);
        return true;
    default:
        break;
    }

    return false;
}

bool KongLie3DGameInstance::OnCursorPosition(double xpos, double ypos)
{
    const glm::dvec2 newMousePos(xpos, ypos);
    if (!hasMousePosition_ || glm::length(newMousePos - mousePos_) > 0.5)
    {
        hoverStableMousePos_ = newMousePos;
        ClearHoveredPieceTooltip();
    }

    mousePos_ = newMousePos;
    hasMousePosition_ = true;

    if (!draggingPiece_ || battleSystem_.GetState() != KongLie3D::EBattleState::Deployment)
    {
        return false;
    }

    glm::vec3 hitPoint{};
    if (!TryGetBoardIntersection(mousePos_, hitPoint))
    {
        return true;
    }

    const float liftedY = GetDeploymentWorldPosition(*draggingPiece_).y + DragLiftHeight;
    draggingPiece_->node->SetTranslation(glm::vec3(hitPoint.x, liftedY, hitPoint.z));
    draggingPiece_->node->RecalcTransform(true);
    GetEngine().GetScene().MarkTransformDirty();
    return true;
}

void KongLie3DGameInstance::StartBattle()
{
    if (battleSystem_.GetState() != KongLie3D::EBattleState::Deployment)
    {
        return;
    }

    // Apply current level's enemy damage multiplier before starting battle
    const KongLie3D::FLevelDef* currentLevel = GetCurrentLevel();
    if (currentLevel)
    {
        battleSystem_.SetEnemyDamageMultiplier(currentLevel->enemyDmgMult);
        spdlog::info("[KongLie3D] Battle started: level='{}' (dmg mult: {:.2f}x)", 
                     currentLevel->name, currentLevel->enemyDmgMult);
    }
    else
    {
        spdlog::warn("[KongLie3D] StartBattle: No valid level found, using default damage multiplier");
    }

    DismissDeploymentHint();
    battleSystem_.Start();
}

void KongLie3DGameInstance::ResetBattle()
{
    if (draggingPiece_)
    {
        CancelDraggingPiece();
    }

    battleSystem_.Reset();
    ClearHoveredPieceTooltip();
    deploymentHintElapsedMs_ = 0.0f;
    deploymentHintDismissed_ = false;
    GetEngine().GetScene().MarkDirty();
}

const KongLie3D::FLevelDef* KongLie3DGameInstance::GetCurrentLevel() const
{
    if (currentLevelIndex_ >= placement_.levels.size())
    {
        return nullptr;
    }
    return &placement_.levels[currentLevelIndex_];
}

bool KongLie3DGameInstance::CanAdvanceToNextLevel() const
{
    return battleSystem_.GetWinnerTeam() == "player" && currentLevelIndex_ + 1 < placement_.levels.size();
}

void KongLie3DGameInstance::SelectLevel(size_t levelIndex)
{
    if (levelIndex >= placement_.levels.size() || levelIndex == currentLevelIndex_)
    {
        return;
    }

    currentLevelIndex_ = levelIndex;
    RebuildCurrentLevelScene();
}

void KongLie3DGameInstance::AdvanceToNextLevel()
{
    if (!CanAdvanceToNextLevel())
    {
        return;
    }

    ++currentLevelIndex_;
    RebuildCurrentLevelScene();
}

bool KongLie3DGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.button.button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    mousePos_ = glm::dvec2(event.button.x, event.button.y);
    hasMousePosition_ = true;

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (ImGui::GetIO().WantCaptureMouse)
        {
            return false;
        }

        if (battleSystem_.GetState() != KongLie3D::EBattleState::Deployment)
        {
            spdlog::info("[KongLie3D] Drag ignored: deployment locked");
            return false;
        }

        glm::vec3 rayOrigin{};
        glm::vec3 rayDir{};
        NextEngineHelper::GetScreenToWorldRay(glm::vec2(mousePos_), rayOrigin, rayDir);

        bool handled = false;
        GetEngine().RayCastGPU(rayOrigin, rayDir, [this, &handled](Assets::RayCastResult result)
        {
            if (!result.Hitted)
            {
                return true;
            }

            KongLie3D::FPieceRuntime* piece = FindPieceByInstanceId(result.InstanceId);
            if (!piece || piece->def.team != "player")
            {
                return true;
            }

            BeginDraggingPiece(*piece);
            handled = true;
            return true;
        });
        return handled;
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && draggingPiece_)
    {
        FinishDraggingPiece();
        return true;
    }

    return false;
}

void KongLie3DGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>& lights,
                                               std::vector<Assets::AnimationTrack>& tracks)
{
    nodes.clear();
    models.clear();
    materials.clear();
    lights.clear();
    tracks.clear();

    KongLie3D::BuildBoard(models, materials, nodes);

    pieceRuntimes_.clear();
    pieceInstanceLookup_.clear();

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f), glm::vec3(0.5f)));
    const uint32_t attachmentBoxModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.16f));
    const uint32_t attachmentSphereModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.08f));
    const uint32_t projectileSphereModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.02f), glm::vec3(0.02f)));
    const uint32_t debrisModelId = static_cast<uint32_t>(models.size() - 1);

    const uint32_t hitFlashMaterialId = AddLambertMaterial(materials, glm::vec3(1.0f));
    const uint32_t projectileAdMaterialId = AddLambertMaterial(materials, glm::vec3(1.0f, 0.58f, 0.20f));
    const uint32_t projectileApMaterialId = AddLambertMaterial(materials, glm::vec3(0.30f, 0.62f, 1.0f));
    const uint32_t projectileHealMaterialId = AddLambertMaterial(materials, glm::vec3(0.30f, 1.0f, 0.40f));

    auto appendPiece = [&](const std::string& pieceId, float worldX, float worldZ, bool onBench, float scale)
    {
        const auto pieceIt = pieceDefs_.find(pieceId);
        if (pieceIt == pieceDefs_.end())
        {
            LogAndThrow(fmt::format("Placement references unknown piece '{}'", pieceId));
        }

        const KongLie3D::FPieceDef& pieceDef = pieceIt->second;
        const glm::vec3 dimensions = GetPieceDimensions(pieceDef);
        const glm::vec3 halfExtents = dimensions * 0.5f;

        models.push_back(Assets::FProcModel::CreateBox(-halfExtents, halfExtents));
        const uint32_t pieceModelId = static_cast<uint32_t>(models.size() - 1);

        const uint32_t pieceMaterialId = AddLambertMaterial(materials, pieceDef.color);
        const uint32_t pieceAccentMaterialId = AddLambertMaterial(materials, BrightenColor(pieceDef.color, 1.30f));
        const uint32_t pieceGlowMaterialId = AddLambertMaterial(materials, BrightenColor(pieceDef.color, 1.45f, 0.06f));
        const uint32_t pieceDarkMaterialId = AddLambertMaterial(materials, pieceDef.color * 0.4f);

        const glm::vec3 pieceScale(scale);
        const glm::vec3 translation(worldX, halfExtents.y * scale, worldZ);
        auto pieceNode = CreateRenderNode(pieceId, translation, pieceScale, static_cast<uint32_t>(nodes.size()), pieceModelId,
                                          pieceMaterialId);
        nodes.push_back(pieceNode);

        KongLie3D::FPieceRuntime runtime{};
        runtime.pieceId = pieceId;
        runtime.baseDef = pieceDef;
        runtime.def = pieceDef;
        runtime.currentHp = pieceDef.hp;
        runtime.currentMana = 0;
        runtime.col = static_cast<int>(std::lround(worldX));
        runtime.row = onBench ? 8 : static_cast<int>(std::lround(worldZ));
        runtime.initialCol = runtime.col;
        runtime.initialRow = runtime.row;
        runtime.alive = true;
        runtime.onBench = onBench;
        runtime.initialOnBench = onBench;
        runtime.node = pieceNode;
        runtime.modelId = pieceModelId;
        runtime.materialId = pieceMaterialId;
        runtime.accentMaterialId = pieceAccentMaterialId;
        runtime.glowMaterialId = pieceGlowMaterialId;
        runtime.darkMaterialId = pieceDarkMaterialId;
        runtime.dimensions = dimensions;
        runtime.visualScale = scale;
        runtime.prevWorldPos = translation;
        runtime.targetWorldPos = translation;
        runtime.lastAttackerPos = translation;
        pieceRuntimes_.push_back(runtime);
        const size_t pieceIndex = pieceRuntimes_.size() - 1;
        pieceInstanceLookup_[pieceNode->GetInstanceId()] = pieceIndex;

        auto knockoutNode = CreateRenderNode(pieceId + "_Knockout",
                                             HiddenKnockoutProxyPosition,
                                             dimensions * scale,
                                             static_cast<uint32_t>(nodes.size()),
                                             attachmentBoxModelId,
                                             pieceDarkMaterialId);
        if (auto renderComponent = knockoutNode->GetComponent<Runtime::RenderComponent>())
        {
            renderComponent->SetVisible(false);
        }
        auto knockoutPhysics = std::make_shared<Runtime::PhysicsComponent>();
        knockoutPhysics->SetMobility(Runtime::ENodeMobility::Dynamic);
        if (NextPhysics* physics = GetEngine().GetPhysicsEngine())
        {
            const glm::vec3 knockoutHalfExtents = glm::max(halfExtents * scale, glm::vec3(0.28f, 0.24f, 0.28f));
            const NextBodyID knockoutBodyId =
                physics->CreateBoxBody(HiddenKnockoutProxyPosition, knockoutHalfExtents, NextMotionType::Dynamic);
            knockoutPhysics->BindPhysicsBody(knockoutBodyId);
            physics->SetBodyActive(knockoutBodyId, false);
            pieceRuntimes_.back().knockoutBodyId = knockoutBodyId;
        }
        knockoutNode->AddComponent(knockoutPhysics);
        nodes.push_back(knockoutNode);
        pieceRuntimes_.back().knockoutNode = knockoutNode;

        BuildPieceVisual(pieceDef,
                         pieceRuntimes_.back(),
                         pieceIndex,
                         attachmentBoxModelId,
                         attachmentSphereModelId,
                         nodes,
                         pieceInstanceLookup_);
    };

    for (const auto& playerEntry : placement_.player)
    {
        appendPiece(playerEntry.pieceId, static_cast<float>(playerEntry.col), static_cast<float>(playerEntry.row), false, 1.0f);
    }

    // Use current level's enemy configuration
    const KongLie3D::FLevelDef* currentLevel = GetCurrentLevel();
    if (!currentLevel)
    {
        spdlog::error("[KongLie3D] Invalid level index: {}, total levels: {}", currentLevelIndex_, placement_.levels.size());
        currentLevelIndex_ = 0;
        currentLevel = GetCurrentLevel();
    }
    
    if (currentLevel)
    {
        for (const auto& enemyEntry : currentLevel->enemy)
        {
            appendPiece(enemyEntry.pieceId, static_cast<float>(enemyEntry.col), static_cast<float>(enemyEntry.row), false, 1.0f);
        }
        for (size_t benchIndex = 0; benchIndex < currentLevel->bench.size(); ++benchIndex)
        {
            appendPiece(currentLevel->bench[benchIndex], static_cast<float>(benchIndex), 8.5f, true, 0.82f);
        }
    }
    else
    {
        spdlog::error("[KongLie3D] Failed to load level after retry");
    }

    std::vector<KongLie3D::FProjectilePoolEntry> projectilePool;
    projectilePool.reserve(21);
    auto appendProjectilePool = [&](KongLie3D::EProjectileKind kind, uint32_t materialId, const char* namePrefix)
    {
        for (int index = 0; index < 7; ++index)
        {
            auto node = CreateRenderNode(fmt::format("{}_{}", namePrefix, index),
                                         glm::vec3(0.0f, -20.0f, 0.0f),
                                         glm::vec3(1.0f),
                                         static_cast<uint32_t>(nodes.size()),
                                         projectileSphereModelId,
                                         materialId);
            if (auto renderComponent = node->GetComponent<Runtime::RenderComponent>())
            {
                renderComponent->SetVisible(false);
            }
            nodes.push_back(node);
            projectilePool.push_back(KongLie3D::FProjectilePoolEntry{
                .kind = kind,
                .modelId = projectileSphereModelId,
                .materialId = materialId,
                .node = node,
            });
        }
    };
    appendProjectilePool(KongLie3D::EProjectileKind::AttackAD, projectileAdMaterialId, "ProjectileAd");
    appendProjectilePool(KongLie3D::EProjectileKind::AttackAP, projectileApMaterialId, "ProjectileAp");
    appendProjectilePool(KongLie3D::EProjectileKind::Heal, projectileHealMaterialId, "ProjectileHeal");

    std::vector<KongLie3D::FImpactDebrisPoolEntry> debrisPool;
    debrisPool.reserve(30);
    for (int index = 0; index < 30; ++index)
    {
        auto node = CreateRenderNode(fmt::format("HitDebris_{}", index),
                                     glm::vec3(0.0f, -20.0f, 0.0f),
                                     glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()),
                                     debrisModelId,
                                     hitFlashMaterialId);
        if (auto renderComponent = node->GetComponent<Runtime::RenderComponent>())
        {
            renderComponent->SetVisible(false);
        }
        nodes.push_back(node);
        debrisPool.push_back(KongLie3D::FImpactDebrisPoolEntry{.node = node});
    }

    battleSystem_.ConfigureVisualResources(hitFlashMaterialId, std::move(projectilePool), std::move(debrisPool));
    battleSystem_.BindPieces(&pieceRuntimes_);
}

bool KongLie3DGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    glm::vec3 cameraPosition(3.5f, 10.0f, 13.3f);
    glm::vec3 cameraTarget(3.5f, -0.25f, 5.4f);
    if (const auto& presentation = battleSystem_.GetUltimatePresentation();
        presentation.cameraFocusRemainingMs > 0.0f && presentation.cameraFocusDurationMs > 0.0f)
    {
        const float progress =
            1.0f - std::clamp(presentation.cameraFocusRemainingMs / presentation.cameraFocusDurationMs, 0.0f, 1.0f);
        const float pushAlpha = std::sin(progress * glm::pi<float>());
        const glm::vec3 focusCameraPosition = presentation.cameraFocusPos + glm::vec3(3.0f, 5.0f, 5.0f);
        cameraPosition = glm::mix(cameraPosition, focusCameraPosition, pushAlpha);
        cameraTarget = glm::mix(cameraTarget, presentation.cameraFocusPos, pushAlpha);
    }
    if (const float screenShakeMs = battleSystem_.GetScreenShakeMs(); screenShakeMs > 0.0f)
    {
        const float strength = 0.05f * std::clamp(screenShakeMs / 200.0f, 0.0f, 1.0f);
        const float t = battleSystem_.GetElapsedMs() * 0.07f + screenShakeMs * 0.011f;
        const glm::vec3 jitter(std::sin(t * 1.13f), std::cos(t * 1.71f), std::sin(t * 1.47f + 0.8f));
        cameraPosition += jitter * strength;
        cameraTarget += jitter * (strength * 0.35f);
    }
    outRenderCamera.ModelView = glm::lookAtRH(cameraPosition, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    outRenderCamera.FieldOfView = 45.0f;
    return true;
}

std::vector<glm::ivec2> KongLie3DGameInstance::GetValidDragCells() const
{
    std::vector<glm::ivec2> cells;
    if (!draggingPiece_ || battleSystem_.GetState() != KongLie3D::EBattleState::Deployment)
    {
        return cells;
    }

    for (int row = PlayerBoardRowMin; row <= PlayerBoardRowMax; ++row)
    {
        for (int col = 0; col < BoardCols; ++col)
        {
            if (CanDropDraggedPieceAt(col, row))
            {
                cells.emplace_back(col, row);
            }
        }
    }

    for (int col = 0; col < BenchSlots; ++col)
    {
        if (CanDropDraggedPieceAt(col, BenchRow))
        {
            cells.emplace_back(col, BenchRow);
        }
    }

    return cells;
}

bool KongLie3DGameInstance::GetInvalidDragHoverCell(glm::ivec2& outCell) const
{
    if (!draggingPiece_ || !TryGetHoveredCell(outCell))
    {
        return false;
    }

    const std::vector<glm::ivec2> validCells = GetValidDragCells();
    return std::find(validCells.begin(), validCells.end(), outCell) == validCells.end();
}

const KongLie3D::FPieceRuntime* KongLie3DGameInstance::GetHoveredTooltipPiece() const
{
    if (hoveredTooltipPieceId_.empty())
    {
        return nullptr;
    }

    return FindPieceById(hoveredTooltipPieceId_);
}

KongLie3D::FPieceRuntime* KongLie3DGameInstance::FindPieceByInstanceId(uint32_t instanceId)
{
    const auto it = pieceInstanceLookup_.find(instanceId);
    if (it == pieceInstanceLookup_.end() || it->second >= pieceRuntimes_.size())
    {
        return nullptr;
    }
    return &pieceRuntimes_[it->second];
}

const KongLie3D::FPieceRuntime* KongLie3DGameInstance::FindPieceById(const std::string& pieceId) const
{
    for (const auto& piece : pieceRuntimes_)
    {
        if (piece.pieceId == pieceId)
        {
            return &piece;
        }
    }

    return nullptr;
}

void KongLie3DGameInstance::RebuildCurrentLevelScene()
{
    if (draggingPiece_)
    {
        CancelDraggingPiece();
    }

    ClearHoveredPieceTooltip();
    deploymentHintElapsedMs_ = 0.0f;
    deploymentHintDismissed_ = false;
    battleSystem_.Reset();
    
    // Trigger scene reload to apply new enemy configuration
    GetEngine().RequestLoadScene("Empty.proc");
}

float KongLie3DGameInstance::GetDeploymentHintAlpha() const
{
    if (deploymentHintDismissed_ || battleSystem_.GetState() != KongLie3D::EBattleState::Deployment)
    {
        return 0.0f;
    }

    if (deploymentHintElapsedMs_ <= 4000.0f)
    {
        return 1.0f;
    }
    if (deploymentHintElapsedMs_ >= 5000.0f)
    {
        return 0.0f;
    }
    return 1.0f - ((deploymentHintElapsedMs_ - 4000.0f) / 1000.0f);
}

std::string KongLie3DGameInstance::GetRendererLabel() const
{
    bool found = false;
    const std::string value = GetEngine().GetCVarSystem().GetValueString("r.rendererType", &found);
    const int rendererType = found ? std::clamp(std::stoi(value), 0, 3) : 1;
    return fmt::format("Render: {}", GetRendererName(rendererType));
}

void KongLie3DGameInstance::DismissDeploymentHint()
{
    deploymentHintDismissed_ = true;
    deploymentHintElapsedMs_ = 5000.0f;
}

KongLie3D::FPieceRuntime* KongLie3DGameInstance::FindPlayerPieceAtCell(int col, int row, const KongLie3D::FPieceRuntime* exclude)
{
    for (auto& piece : pieceRuntimes_)
    {
        if (&piece == exclude || piece.def.team != "player")
        {
            continue;
        }

        if (piece.col == col && piece.row == row)
        {
            return &piece;
        }
    }
    return nullptr;
}

bool KongLie3DGameInstance::TryGetBoardIntersection(glm::dvec2 screenPos, glm::vec3& outHitPoint) const
{
    glm::vec3 rayOrigin{};
    glm::vec3 rayDir{};
    NextEngineHelper::GetScreenToWorldRay(glm::vec2(screenPos), rayOrigin, rayDir);
    if (std::abs(rayDir.y) < 0.0001f)
    {
        return false;
    }

    const float t = -rayOrigin.y / rayDir.y;
    if (t < 0.0f)
    {
        return false;
    }

    outHitPoint = rayOrigin + rayDir * t;
    return true;
}

bool KongLie3DGameInstance::TryGetHoveredCell(glm::ivec2& outCell) const
{
    if (!hasMousePosition_)
    {
        return false;
    }

    glm::vec3 hitPoint{};
    if (!TryGetBoardIntersection(mousePos_, hitPoint))
    {
        return false;
    }

    outCell.x = static_cast<int>(std::lround(hitPoint.x));
    outCell.y = static_cast<int>(std::lround(hitPoint.z));
    return true;
}

bool KongLie3DGameInstance::CanDropDraggedPieceAt(int col, int row) const
{
    if (!draggingPiece_ || !IsPlayerDeployCell(col, row))
    {
        return false;
    }

    if (col == dragStartCol_ && row == dragStartRow_)
    {
        return true;
    }

    for (const auto& piece : pieceRuntimes_)
    {
        if (&piece != draggingPiece_ && piece.def.team == "player" && piece.col == col && piece.row == row)
        {
            return true;
        }
    }

    const bool targetOnBench = row == BenchRow;
    if (dragStartOnBench_ && !targetOnBench)
    {
        return CountDeployedPlayerPieces() < CountDeployedEnemyPieces();
    }

    return true;
}

int KongLie3DGameInstance::CountDeployedPlayerPieces() const
{
    int count = 0;
    for (const auto& piece : pieceRuntimes_)
    {
        if (piece.def.team == "player" && !piece.onBench)
        {
            ++count;
        }
    }
    return count;
}

int KongLie3DGameInstance::CountDeployedEnemyPieces() const
{
    int count = 0;
    for (const auto& piece : pieceRuntimes_)
    {
        if (piece.def.team == "enemy" && !piece.onBench)
        {
            ++count;
        }
    }
    return count;
}

void KongLie3DGameInstance::BeginDraggingPiece(KongLie3D::FPieceRuntime& piece)
{
    ClearHoveredPieceTooltip();
    DismissDeploymentHint();
    draggingPiece_ = &piece;
    dragStartCol_ = piece.col;
    dragStartRow_ = piece.row;
    dragStartOnBench_ = piece.onBench;
    if (piece.node)
    {
        const glm::vec3 liftedPosition = GetDeploymentWorldPosition(piece) + glm::vec3(0.0f, DragLiftHeight, 0.0f);
        piece.node->SetTranslation(liftedPosition);
        piece.node->RecalcTransform(true);
        GetEngine().GetScene().MarkTransformDirty();
    }
    spdlog::info("[KongLie3D] Drag begin: {} at ({}, {}, {})", piece.pieceId, dragStartCol_, dragStartRow_,
                 dragStartOnBench_ ? "bench" : "board");
}

void KongLie3DGameInstance::CancelDraggingPiece()
{
    if (!draggingPiece_)
    {
        return;
    }

    spdlog::info("[KongLie3D] Drag revert: {} back to ({}, {}, {})", draggingPiece_->pieceId, dragStartCol_, dragStartRow_,
                 dragStartOnBench_ ? "bench" : "board");
    draggingPiece_->col = dragStartCol_;
    draggingPiece_->row = dragStartRow_;
    draggingPiece_->onBench = dragStartOnBench_;
    UpdatePieceDeploymentTransform(*draggingPiece_);
    draggingPiece_ = nullptr;
    ClearHoveredPieceTooltip();
    GetEngine().GetScene().MarkDirty();
}

void KongLie3DGameInstance::FinishDraggingPiece()
{
    if (!draggingPiece_)
    {
        return;
    }

    glm::ivec2 hoveredCell{};
    if (!TryGetHoveredCell(hoveredCell) || !CanDropDraggedPieceAt(hoveredCell.x, hoveredCell.y))
    {
        CancelDraggingPiece();
        return;
    }

    KongLie3D::FPieceRuntime* targetPiece = FindPlayerPieceAtCell(hoveredCell.x, hoveredCell.y, draggingPiece_);
    if (targetPiece)
    {
        spdlog::info("[KongLie3D] Drag swap: {} <-> {} at ({}, {})", draggingPiece_->pieceId, targetPiece->pieceId,
                     hoveredCell.x, hoveredCell.y);
        const int targetCol = targetPiece->col;
        const int targetRow = targetPiece->row;
        const bool targetOnBench = targetPiece->onBench;

        targetPiece->col = dragStartCol_;
        targetPiece->row = dragStartRow_;
        targetPiece->onBench = dragStartOnBench_;
        UpdatePieceDeploymentTransform(*targetPiece);

        draggingPiece_->col = targetCol;
        draggingPiece_->row = targetRow;
        draggingPiece_->onBench = targetOnBench;
        UpdatePieceDeploymentTransform(*draggingPiece_);
    }
    else
    {
        draggingPiece_->col = hoveredCell.x;
        draggingPiece_->row = hoveredCell.y;
        draggingPiece_->onBench = hoveredCell.y == BenchRow;
        spdlog::info("[KongLie3D] Drag place: {} -> ({}, {}, {})", draggingPiece_->pieceId, draggingPiece_->col,
                     draggingPiece_->row, draggingPiece_->onBench ? "bench" : "board");
        UpdatePieceDeploymentTransform(*draggingPiece_);
    }

    draggingPiece_ = nullptr;
    ClearHoveredPieceTooltip();
    GetEngine().GetScene().MarkDirty();
}

void KongLie3DGameInstance::UpdatePieceDeploymentTransform(KongLie3D::FPieceRuntime& piece)
{
    const glm::vec3 worldPosition = GetDeploymentWorldPosition(piece);
    piece.prevWorldPos = worldPosition;
    piece.targetWorldPos = worldPosition;
    piece.moveDurationMs = 0.0f;
    piece.moveElapsedMs = 0.0f;

    if (piece.node)
    {
        piece.node->SetTranslation(worldPosition);
        piece.node->RecalcTransform(true);
    }
}

void KongLie3DGameInstance::UpdateHoveredPieceTooltip(double deltaSeconds)
{
    if (!hasMousePosition_ || draggingPiece_)
    {
        ClearHoveredPieceTooltip();
        return;
    }

    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    hoverStillMs_ += deltaMs;
    hoverRaycastCooldownMs_ = std::max(0.0f, hoverRaycastCooldownMs_ - deltaMs);
    if (hoverStillMs_ < 200.0f || hoverRaycastCooldownMs_ > 0.0f)
    {
        return;
    }

    hoverRaycastCooldownMs_ = 200.0f;

    glm::vec3 rayOrigin{};
    glm::vec3 rayDir{};
    NextEngineHelper::GetScreenToWorldRay(glm::vec2(mousePos_), rayOrigin, rayDir);

    std::string hoveredPieceId;
    GetEngine().RayCastGPU(rayOrigin, rayDir, [this, &hoveredPieceId](Assets::RayCastResult result)
    {
        if (!result.Hitted)
        {
            return true;
        }

        if (KongLie3D::FPieceRuntime* piece = FindPieceByInstanceId(result.InstanceId);
            piece && piece->alive && !piece->onBench)
        {
            hoveredPieceId = piece->pieceId;
        }
        return true;
    });

    hoveredTooltipPieceId_ = hoveredPieceId;
}

void KongLie3DGameInstance::ClearHoveredPieceTooltip()
{
    hoveredTooltipPieceId_.clear();
    hoverStillMs_ = 0.0f;
    hoverRaycastCooldownMs_ = 0.0f;
}
