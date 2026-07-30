#pragma once

#include "Battle/FormationLayout.h"
#include "Battle/BattleState.h"
#include "Battle/CombatSystem.h"
#include "NextTotalwarCombatConfig.hpp"
#include "NextTotalwarTypes.h"
#include "Render/BattleCamera.h"
#include "Render/CombatFx.h"

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Gameplay/AI/NavGrid.h"

#include <glm/vec2.hpp>

namespace Runtime
{
    class TerrainComponent;
}

namespace NextTotalwar
{
    class FGameInstance final : public NextGameInstanceBase
    {
    public:
        FGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);

        void OnInit() override;
        void OnTick(double deltaSeconds) override;
        void OnDestroy() override;
        bool OnRenderUI() override;
        bool ShouldRenderUiDuringScreenshot() const override { return true; }
        void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
        void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& registry) override;
        bool OverrideRenderCamera(Assets::Camera& camera) const override;
        void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                std::vector<Assets::Model>& models,
                                std::vector<Assets::FMaterial>& materials,
                                std::vector<Assets::LightObject>& lights,
                                std::vector<Assets::AnimationTrack>& tracks) override;
        void OnSceneLoaded() override;
        void OnSceneUnloaded() override;
        bool OnKey(SDL_Event& event) override;
        bool OnCursorPosition(double x, double y) override;
        bool OnMouseButton(SDL_Event& event) override;
        bool OnScroll(double x, double y) override;

    private:
        void DeployArmies();
        void CreateSoldierVisuals();
        void TickRegiments(float deltaSeconds);
        void TickSoldiers(float deltaSeconds);
        float GroundHeight(float x, float z) const;
        float UiScale() const;
        glm::dvec2 ToLogicalMouse(double x, double y) const;
        glm::vec2 ToFramebufferMouse(const glm::dvec2& logicalMouse) const;
        bool TryGroundHit(const glm::dvec2& screen, glm::vec3& hit) const;
        bool TryProjectRegimentBounds(const FRegiment& regiment,
                                      std::array<glm::vec2, 4>& projected) const;
        FRegiment* PickRegimentAt(const glm::dvec2& screen);
        void SelectAt(const glm::dvec2& screen, bool additive);
        void SelectAllTypeAt(const glm::dvec2& screen);
        void SelectRect(const glm::dvec2& a, const glm::dvec2& b, bool additive);
        void ClearSelection();
        void RefreshSelectionFeedback();
        bool TrySelectedCenter(glm::vec3& center) const;
        float ResolveOrderFacing(const glm::vec3& target, const glm::vec3& facingPoint) const;
        std::vector<glm::vec3> BuildOrderPath(const FRegiment& regiment,
                                              const glm::vec3& destination,
                                              float facing) const;
        void IssueMoveOrders(const glm::vec3& target, float facing);
        void DrawWorldOverlay() const;
        size_t SelectedCount() const;
        size_t MarchingCount() const;
        static const char* StateName(ERegimentState state);

        FBattleCamera camera_;
        NextGameplay::FNavGrid navGrid_;
        Runtime::TerrainComponent* terrain_ = nullptr;
        std::array<FUnitDef, 3> unitDefs_{};
        std::vector<FRegiment> regiments_;
        std::vector<std::vector<FSoldierVisual>> soldierVisuals_;
        std::array<Assets::FRigAsset, 3> soldierRigAssets_{};
        std::array<std::vector<uint32_t>, 3> soldierPartModelIds_{};
        std::array<std::vector<std::array<uint32_t, 16>>, 3> soldierPartMaterialIds_{};
        std::array<std::array<uint32_t, 6>, 2> regimentMaterialIds_{};
        bool sceneInjected_ = false;
        bool sceneReady_ = false;
        bool showDebug_ = true;
        glm::dvec2 mousePos_{};
        glm::dvec2 leftStart_{};
        glm::dvec2 rightStart_{};
        glm::dvec2 middleLast_{};
        glm::vec3 rightTarget_{};
        bool leftDown_ = false;
        bool rightDown_ = false;
        bool middleDown_ = false;
        bool hasMouse_ = false;
        uint64_t frameIndex_ = 0;
        int animatorUpdates_ = 0;
        float lastOrderDistance_ = 0.0f;
        FCombatTuning combatTuning_;
        FBattleState battleState_;
        FCombatSystem combatSystem_;
        FCombatGrid movementGrid_;
        FCombatFx combatFx_;
        float combatAccumulator_ = 0.0f;
        int battleSeed_ = 1337;
        float battleDeployDistance_ = 125.0f;
        bool deterministicCombat_ = false;
        int combatTicksThisFrame_ = 0;
        int lastCombatEventCount_ = 0;
        float combatCpuMilliseconds_ = 0.0f;
    };
}
