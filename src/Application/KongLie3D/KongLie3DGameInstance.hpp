#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Assets/Core/Model.hpp"
#include "KongLie3DBattleSystem.hpp"
#include "KongLie3DDataLoader.hpp"
#include "KongLie3DPiece.hpp"

class KongLie3DGameInstance : public NextGameInstanceBase
{
public:
    KongLie3DGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~KongLie3DGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    void ResetBattle();
    KongLie3D::FBattleSystem& GetBattleSystem() { return battleSystem_; }
    const KongLie3D::FBattleSystem& GetBattleSystem() const { return battleSystem_; }
    const std::vector<KongLie3D::FPieceRuntime>& GetPieceRuntimes() const { return pieceRuntimes_; }
    const KongLie3D::FPieceRuntime* GetDraggingPiece() const { return draggingPiece_; }
    std::vector<glm::ivec2> GetValidDragCells() const;

private:
    NextEngine& GetEngine() const { return *engine_; }
    KongLie3D::FPieceRuntime* FindPieceByInstanceId(uint32_t instanceId);
    KongLie3D::FPieceRuntime* FindPlayerPieceAtCell(int col, int row, const KongLie3D::FPieceRuntime* exclude = nullptr);
    bool TryGetBoardIntersection(glm::dvec2 screenPos, glm::vec3& outHitPoint) const;
    bool TryGetHoveredCell(glm::ivec2& outCell) const;
    bool CanDropDraggedPieceAt(int col, int row) const;
    int CountDeployedPlayerPieces() const;
    int CountDeployedEnemyPieces() const;
    void BeginDraggingPiece(KongLie3D::FPieceRuntime& piece);
    void CancelDraggingPiece();
    void FinishDraggingPiece();
    void UpdatePieceDeploymentTransform(KongLie3D::FPieceRuntime& piece);

    NextEngine* engine_;
    bool showMvpWindow_ = true;
    std::map<std::string, KongLie3D::FPieceDef> pieceDefs_;
    KongLie3D::FPlacementData placement_;
    std::vector<KongLie3D::FPieceRuntime> pieceRuntimes_;
    std::unordered_map<uint32_t, size_t> pieceInstanceLookup_;
    KongLie3D::FBattleSystem battleSystem_;
    glm::dvec2 mousePos_ = glm::dvec2(0.0);
    bool hasMousePosition_ = false;
    KongLie3D::FPieceRuntime* draggingPiece_ = nullptr;
    int dragStartCol_ = 0;
    int dragStartRow_ = 0;
    bool dragStartOnBench_ = false;
};
