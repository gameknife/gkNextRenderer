#include "KongLie3DGameInstance.hpp"

#include <imgui.h>

#include "KongLie3DBoard.hpp"
#include "KongLie3DUI.hpp"
#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Loaders/FProcModel.h"
#include "Runtime/Utilities/NextEngineHelper.h"
#include "KongLie3DDataLoader.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Utilities/Exception.hpp"

#include <spdlog/spdlog.h>

namespace
{
    constexpr const char* BootstrapScene = "assets/models/playground.glb";
    constexpr const char* PiecesConfigPath = "assets/configs/konglie/pieces.json";
    constexpr const char* PlacementConfigPath = "assets/configs/konglie/placement.json";
    constexpr const char* RelicsConfigPath = "assets/configs/konglie/relics.json";
    constexpr int BoardCols = 7;
    constexpr int PlayerBoardRowMin = 4;
    constexpr int PlayerBoardRowMax = 7;
    constexpr int BenchRow = 8;
    constexpr int BenchSlots = 3;
    constexpr float BenchWorldZ = 8.5f;

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
    battleSystem_.SetRelics(KongLie3D::LoadRelics(RelicsConfigPath));
    GetEngine().SetGraphicsDebugPanelVisible(false);
    GetEngine().SetPhysicsDebugOverlayVisible(false);
    GetEngine().GetUserSettings().ShowOverlay = false;

    // The engine only uploads application-owned procedural content through the scene rebuild path.
    // Use a lightweight bootstrap scene, then replace its nodes with the M1 procedural board.
    GetEngine().RequestLoadScene(BootstrapScene);
}

void KongLie3DGameInstance::OnTick(double deltaSeconds)
{
    if (battleSystem_.GetState() != KongLie3D::EBattleState::Deployment && draggingPiece_)
    {
        CancelDraggingPiece();
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
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_SPACE &&
        battleSystem_.GetState() == KongLie3D::EBattleState::Deployment)
    {
        if (draggingPiece_)
        {
            CancelDraggingPiece();
        }
        battleSystem_.Start();
        return true;
    }
    return false;
}

bool KongLie3DGameInstance::OnCursorPosition(double xpos, double ypos)
{
    mousePos_ = glm::dvec2(xpos, ypos);
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

    draggingPiece_->node->SetTranslation(glm::vec3(hitPoint.x, draggingPiece_->node->Translation().y, hitPoint.z));
    draggingPiece_->node->RecalcTransform(true);
    GetEngine().GetScene().MarkDirty();
    return true;
}

void KongLie3DGameInstance::ResetBattle()
{
    if (draggingPiece_)
    {
        CancelDraggingPiece();
    }

    battleSystem_.Reset();
    GetEngine().GetScene().MarkDirty();
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

    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.16f));
    const uint32_t heroSphereModelId = static_cast<uint32_t>(models.size() - 1);

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

        materials.push_back({Assets::Material::Lambertian(pieceDef.color)});
        const uint32_t pieceMaterialId = static_cast<uint32_t>(materials.size() - 1);
        materials.push_back({Assets::Material::Lambertian(pieceDef.color * 0.4f)});
        const uint32_t pieceDarkMaterialId = static_cast<uint32_t>(materials.size() - 1);

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
        runtime.darkMaterialId = pieceDarkMaterialId;
        runtime.dimensions = dimensions;
        runtime.visualScale = scale;
        runtime.prevWorldPos = translation;
        runtime.targetWorldPos = translation;
        pieceRuntimes_.push_back(runtime);
        const size_t pieceIndex = pieceRuntimes_.size() - 1;
        pieceInstanceLookup_[pieceNode->GetInstanceId()] = pieceIndex;

        if (pieceDef.isHero)
        {
            auto heroSphereNode =
                CreateRenderNode(pieceId + "_heroSphere",
                                 glm::vec3(0.0f, halfExtents.y + 0.18f, 0.0f),
                                 glm::vec3(1.0f),
                                 static_cast<uint32_t>(nodes.size()),
                                 heroSphereModelId,
                                 pieceMaterialId);
            heroSphereNode->SetParent(pieceNode);
            pieceInstanceLookup_[heroSphereNode->GetInstanceId()] = pieceIndex;
            nodes.push_back(heroSphereNode);
        }
    };

    for (const auto& playerEntry : placement_.player)
    {
        appendPiece(playerEntry.pieceId, static_cast<float>(playerEntry.col), static_cast<float>(playerEntry.row), false, 1.0f);
    }

    for (const auto& enemyEntry : placement_.enemy)
    {
        appendPiece(enemyEntry.pieceId, static_cast<float>(enemyEntry.col), static_cast<float>(enemyEntry.row), false, 1.0f);
    }

    for (size_t benchIndex = 0; benchIndex < placement_.bench.size(); ++benchIndex)
    {
        appendPiece(placement_.bench[benchIndex], static_cast<float>(benchIndex), 8.5f, true, 0.82f);
    }

    battleSystem_.BindPieces(&pieceRuntimes_);
}

bool KongLie3DGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    const glm::vec3 cameraPosition(3.5f, 8.0f, 11.0f);
    const glm::vec3 cameraTarget(3.5f, 0.0f, 3.5f);
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

KongLie3D::FPieceRuntime* KongLie3DGameInstance::FindPieceByInstanceId(uint32_t instanceId)
{
    const auto it = pieceInstanceLookup_.find(instanceId);
    if (it == pieceInstanceLookup_.end() || it->second >= pieceRuntimes_.size())
    {
        return nullptr;
    }
    return &pieceRuntimes_[it->second];
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
    draggingPiece_ = &piece;
    dragStartCol_ = piece.col;
    dragStartRow_ = piece.row;
    dragStartOnBench_ = piece.onBench;
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
