#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "KongLie3DBattleSystem.hpp"
#include "KongLie3DDataLoader.hpp"
#include "KongLie3DNotifications.hpp"
#include "KongLie3DPiece.hpp"

class KongLie3DGameInstance : public NextGameInstanceBase
{
public:
    KongLie3DGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~KongLie3DGameInstance() override = default;

    void OnInit() override;
    void OnInitUI() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    void StartBattle();
    void ResetBattle();
    void SelectLevel(size_t levelIndex);
    void SelectRelic(const std::string& relicId);
    void SetBattleSpeedMultiplier(float speedMultiplier);
    void AdvanceToNextLevel();
    KongLie3D::FBattleSystem& GetBattleSystem() { return battleSystem_; }
    const KongLie3D::FBattleSystem& GetBattleSystem() const { return battleSystem_; }
    KongLie3D::FNotificationCenter& GetNotificationCenter() { return notificationCenter_; }
    const KongLie3D::FNotificationCenter& GetNotificationCenter() const { return notificationCenter_; }
    const std::vector<KongLie3D::FPieceRuntime>& GetPieceRuntimes() const { return pieceRuntimes_; }
    const std::vector<KongLie3D::FLevelDef>& GetLevels() const { return placement_.levels; }
    size_t GetCurrentLevelIndex() const { return currentLevelIndex_; }
    const KongLie3D::FLevelDef* GetCurrentLevel() const;
    bool CanAdvanceToNextLevel() const;
    const KongLie3D::FPieceRuntime* GetDraggingPiece() const { return draggingPiece_; }
    std::vector<glm::ivec2> GetValidDragCells() const;
    bool GetInvalidDragHoverCell(glm::ivec2& outCell) const;
    const KongLie3D::FPieceRuntime* GetHoveredTooltipPiece() const;
    float GetDeploymentHintAlpha() const;
    std::string GetRendererLabel() const;
    float GetResultModalAppearMs() const { return resultModalAppearMs_; }
    float GetBattleStartBannerElapsedMs() const { return battleStartBannerElapsedMs_; }
    void PushNotification(std::string text, KongLie3D::ENotificationKind kind, float durationMs = 2500.0f);

private:
    KongLie3D::FPieceRuntime* FindPieceByInstanceId(uint32_t instanceId);
    KongLie3D::FPieceRuntime* FindPlayerPieceAtCell(int col, int row, const KongLie3D::FPieceRuntime* exclude = nullptr);
    const KongLie3D::FPieceRuntime* FindPieceById(const std::string& pieceId) const;
    void RebuildCurrentLevelScene();
    bool TryGetBoardIntersection(glm::dvec2 screenPos, glm::vec3& outHitPoint) const;
    bool TryGetHoveredCell(glm::ivec2& outCell) const;
    bool CanDropDraggedPieceAt(int col, int row) const;
    int CountDeployedPlayerPieces() const;
    int CountDeployedEnemyPieces() const;
    void BeginDraggingPiece(KongLie3D::FPieceRuntime& piece);
    void CancelDraggingPiece();
    void FinishDraggingPiece();
    void UpdatePieceDeploymentTransform(KongLie3D::FPieceRuntime& piece);
    void UpdateHoveredPieceTooltip(double deltaSeconds);
    void ClearHoveredPieceTooltip();
    void DismissDeploymentHint();

    std::map<std::string, KongLie3D::FPieceDef> pieceDefs_;
    KongLie3D::FPlacementData placement_;
    size_t currentLevelIndex_ = 0;
    std::vector<KongLie3D::FPieceRuntime> pieceRuntimes_;
    std::unordered_map<uint32_t, size_t> pieceInstanceLookup_;
    KongLie3D::FBattleSystem battleSystem_;
    glm::dvec2 mousePos_ = glm::dvec2(0.0);
    bool hasMousePosition_ = false;
    KongLie3D::FPieceRuntime* draggingPiece_ = nullptr;
    int dragStartCol_ = 0;
    int dragStartRow_ = 0;
    bool dragStartOnBench_ = false;
    glm::dvec2 hoverStableMousePos_ = glm::dvec2(0.0);
    float hoverStillMs_ = 0.0f;
    float hoverRaycastCooldownMs_ = 0.0f;
    std::string hoveredTooltipPieceId_;
    float deploymentHintElapsedMs_ = 0.0f;
    bool deploymentHintDismissed_ = false;
    KongLie3D::FNotificationCenter notificationCenter_;
    KongLie3D::EBattleState previousBattleState_ = KongLie3D::EBattleState::Deployment;
    bool previousOvertimeActive_ = false;
    float resultModalAppearMs_ = 0.0f;
    float battleStartBannerElapsedMs_ = -1.0f;
};
