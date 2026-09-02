#pragma once

// ============================================================================
// PlayerRigVisual.hpp - The astro_bot.scad rig that stands in for the player,
// following the single-instance pattern from NextDayz::PlayerRigVisual:
//   OnInit LoadRig -> BeforeSceneRebuild InjectAssets -> OnSceneLoaded Instantiate
// OnSceneUnloaded only clears runtime pointers; the injected models/materials
// belong to the scene that is being built and must survive it.
// ============================================================================

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"

#include "Application/Game/NextAstrobot/Player/PlayerController.hpp"

namespace Assets
{
    class Node;
    class Scene;
    class Model;
    struct FMaterial;
}

namespace NextAstrobot
{
    class FPlayerRigVisual
    {
    public:
        bool LoadRig(const std::string& scadPath);
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void OnSceneLoaded(Assets::Scene& scene);
        void OnSceneUnloaded();

        void SetVisible(bool visible);
        /// Places the rig at the character's foot position and picks the clip for the state.
        void Update(const glm::vec3& footPosition, float yaw, ELocomotion state, float horizontalSpeed,
                    float runReferenceSpeed, float deltaSeconds);

        bool HasRig() const { return hasRig_; }
        const std::string& CurrentClip() const { return currentClip_; }

        // --- shared with FRescueRigVisual: a freed robot is the same rig in another
        //     colour, so it reuses the asset and the models injected for the player ---
        bool HasInjectedAssets() const { return hasRig_ && injected_; }
        const Assets::FRigAsset& Asset() const { return asset_; }
        /// Model and material ids as injected, with the tintable sections left alone so
        /// the caller can pick the body colour.
        NextGameplay::FRigInstanceDesc BaseInstanceDesc() const;
        /// Body colours available to instances: the player's blue first, then the spares
        /// the level's rescued robots cycle through.
        const std::vector<uint32_t>& TintMaterialIds() const { return tintMaterialIds_; }

    private:
        Assets::Node* BoneNode(std::string_view boneName);

        Assets::FRigAsset asset_{};
        bool hasRig_ = false;
        bool injected_ = false;

        std::vector<uint32_t> partModelIds_;
        std::vector<std::array<uint32_t, 16>> partMaterialIds_;
        std::vector<uint32_t> tintMaterialIds_;

        Assets::Scene* scene_ = nullptr;
        std::shared_ptr<Assets::Node> worldNode_;
        std::vector<Assets::Node*> boneNodes_;
        Assets::Node* jetBone_ = nullptr;
        NextGameplay::FRigAnimator animator_;
        bool bound_ = false;
        bool visible_ = true;
        bool wasAirborne_ = false;
        float landTimer_ = 0.0f;
        std::string currentClip_;
    };
}
