#pragma once

// ============================================================================
// PlayerRigVisual.hpp - ScadRig soldier visual for the player, following the
// FRigPreview / FScadRigVisual single-instance pattern. The rig lives under a
// world node whose TRS tracks the controller; the animator plays idle/walk/fire
// clips. Injection lifecycle mirrors AirportSim/FRigPreview:
//   OnInit LoadRig -> BeforeSceneRebuild InjectAssets -> OnSceneLoaded Instantiate
// OnSceneUnloaded only clears runtime pointers (never the injected assets).
// ============================================================================

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"

namespace Assets
{
    class Scene;
    class Node;
    struct Model;
    struct FMaterial;
}

namespace NextDayz
{
    enum class EAnimState
    {
        Idle,
        Walk,
        Run,
        Fire
    };

    class PlayerRigVisual
    {
    public:
        bool LoadRig(const std::string& scadPath);

        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void OnSceneLoaded(Assets::Scene& scene);
        void OnSceneUnloaded();

        void SetVisible(bool visible);
        void SetClothing(const std::string& clothingId, bool on);

        // Drives world transform + animation for the frame.
        void Update(const glm::vec3& feetPosition, float yaw, EAnimState state, float moveSpeed, float deltaSeconds);

        bool HasRig() const { return hasRig_; }

    private:
        Assets::Node* BoneNode(std::string_view boneName);

        Assets::FRigAsset asset_{};
        bool hasRig_ = false;

        // injection results (recorded in InjectAssets, valid for the next scene)
        std::vector<uint32_t> partModelIds_;
        std::vector<std::array<uint32_t, 16>> partMaterialIds_;
        uint32_t tintMaterialId_ = 0;
        uint32_t helmetModelId_ = 0;
        uint32_t backpackModelId_ = 0;
        uint32_t clothingMaterialId_ = 0;
        bool injected_ = false;

        // runtime scene state
        Assets::Scene* scene_ = nullptr;
        std::shared_ptr<Assets::Node> worldNode_;
        std::vector<Assets::Node*> boneNodes_;
        std::shared_ptr<Assets::Node> helmetNode_;
        std::shared_ptr<Assets::Node> backpackNode_;
        NextGameplay::FRigAnimator animator_;
        bool bound_ = false;

        EAnimState state_ = EAnimState::Idle;
        bool visible_ = true;
        float baseWalkClipSpeed_ = 1.5f; // metres/sec the walk clip is authored for
    };
}
