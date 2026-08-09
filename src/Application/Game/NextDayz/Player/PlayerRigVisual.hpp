#pragma once

// ============================================================================
// PlayerRigVisual.hpp - ScadRig soldier visual for the player, following the
// FRigPreview / FScadRigVisual single-instance pattern. The rig lives under a
// world node whose TRS tracks the controller; a layered animator combines
// locomotion, aim, recoil and action poses. Injection lifecycle mirrors:
//   OnInit LoadRig -> BeforeSceneRebuild InjectAssets -> OnSceneLoaded Instantiate
// OnSceneUnloaded only clears runtime pointers (never the injected assets).
// ============================================================================

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigLayeredAnimator.h"

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

namespace Assets
{
    class Scene;
    class Node;
    struct Model;
    struct FMaterial;
}

namespace NextDayz
{
    class PlayerRigVisual
    {
    public:
        bool LoadRig(const std::string& scadPath);
        void Configure(const FAnimationConfig& config) { animationConfig_ = config; }

        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        void SetWeaponAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                             const std::array<uint32_t, kWeapons.size()>& materialIds);
        void OnSceneLoaded(Assets::Scene& scene);
        void OnSceneUnloaded();

        void SetVisible(bool visible);
        void SetClothing(const std::string& clothingId, bool on);

        // Drives world transform + animation for the frame.
        void Update(const glm::vec3& feetPosition, float yaw, const FPlayerPresentationState& state,
                    float deltaSeconds);
        void TriggerRecoil(float scale);

        bool HasRig() const { return hasRig_; }
        const std::string& CurrentBaseClipName() const { return baseClipName_; }
        const std::string& CurrentWeaponActionClipName() const { return weaponActionClipName_; }
        float AimWeight() const { return aimWeight_; }
        float WeaponActionWeight() const { return weaponActionWeight_; }
        bool RecoilActive() const;

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
        std::shared_ptr<Assets::Node> weaponNode_;
        std::array<std::shared_ptr<Assets::Node>, 2> holsteredWeaponNodes_;
        NextGameplay::FRigLayeredAnimator animator_;
        NextGameplay::FRigLayerHandle locomotionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle aimLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle weaponActionLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle recoilLayer_ = NextGameplay::invalidRigLayerHandle;
        NextGameplay::FRigLayerHandle actionLayer_ = NextGameplay::invalidRigLayerHandle;
        bool bound_ = false;

        bool visible_ = true;
        FAnimationConfig animationConfig_{};
        std::string baseClipName_;
        std::string weaponActionClipName_;
        std::array<uint32_t, kWeapons.size()> weaponModelIds_{};
        std::array<uint32_t, kWeapons.size()> weaponMaterialIds_{};
        int weaponVisualIndex_ = -1;
        std::array<int, 2> holsteredWeaponVisualIndices_{{-1, -1}};
        bool weaponAssetsSet_ = false;
        bool weaponVisible_ = false;
        std::array<bool, 2> holsteredWeaponVisible_{{false, false}};
        float aimWeight_ = 0.0f;
        float weaponActionWeight_ = 0.0f;
        float actionWeight_ = 0.0f;
        bool recoilActive_ = false;
    };
}
