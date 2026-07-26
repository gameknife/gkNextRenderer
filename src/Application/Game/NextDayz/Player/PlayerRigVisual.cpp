#include "PlayerRigVisual.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Gameplay/Rig/RigInstance.h"
#include "Modules/ScadLoader/FScadRig.h"

namespace NextDayz
{
    namespace
    {
        constexpr float kParkedY = -1000.0f;
    }

    bool PlayerRigVisual::LoadRig(const std::string& scadPath)
    {
        std::string error;
        std::vector<std::string> warnings;
        hasRig_ = Assets::FScadRigLoader::LoadRig(scadPath, {}, asset_, error, &warnings);
        if (!hasRig_)
        {
            SPDLOG_ERROR("[NextDayz] failed to load soldier rig '{}': {}", scadPath, error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[NextDayz] rig: {}", warning);
        }
        return hasRig_;
    }

    void PlayerRigVisual::SetWeaponAssets(uint32_t modelId, uint32_t materialId)
    {
        weaponModelId_ = modelId;
        weaponMaterialId_ = materialId;
        weaponAssetsSet_ = true;
    }

    void PlayerRigVisual::InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        if (!hasRig_)
        {
            return;
        }

        partModelIds_.clear();
        partMaterialIds_.clear();
        for (const Assets::FRigPart& part : asset_.parts)
        {
            std::array<uint32_t, 16> sectionMaterials = {0};
            for (size_t section = 0; section < part.sectionColors.size() && section < sectionMaterials.size();
                 ++section)
            {
                if (!part.sectionTintable[section])
                {
                    sectionMaterials[section] =
                        Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(part.sectionColors[section]));
                }
            }
            partMaterialIds_.push_back(sectionMaterials);
            models.push_back(asset_.partModels[part.modelIndex]);
            partModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
        }

        // Faction tint for magenta placeholder sections (olive drab soldier).
        tintMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.28f, 0.32f, 0.24f));

        // Clothing attachment meshes (helmet, backpack) + a shared dark material.
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.14f, -0.02f, -0.15f), glm::vec3(0.14f, 0.14f, 0.16f)));
        helmetModelId_ = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.15f, -0.20f, -0.18f), glm::vec3(0.15f, 0.22f, -0.02f)));
        backpackModelId_ = static_cast<uint32_t>(models.size() - 1);
        clothingMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.10f, 0.11f, 0.10f));

        injected_ = true;
    }

    Assets::Node* PlayerRigVisual::BoneNode(std::string_view boneName)
    {
        const int32_t index = asset_.FindBone(boneName);
        if (index < 0 || index >= static_cast<int32_t>(boneNodes_.size()))
        {
            return nullptr;
        }
        return boneNodes_[index];
    }

    void PlayerRigVisual::OnSceneLoaded(Assets::Scene& scene)
    {
        if (!hasRig_ || !injected_)
        {
            return;
        }
        scene_ = &scene;

        // World node the whole rig hangs under; a Dynamic physics component makes
        // the engine re-upload its transform each frame (no collider bound).
        worldNode_ = Assets::Node::CreateNode("nd_player_rig", glm::vec3(0.0f, kParkedY, 0.0f),
                                              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f),
                                              scene.GenerateInstanceId());
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        // Resolve tint sections to the faction material.
        NextGameplay::FRigInstanceDesc desc;
        desc.namePrefix = "nd_player";
        desc.partModelIds = partModelIds_;
        desc.partMaterialIds = partMaterialIds_;
        for (size_t partIndex = 0; partIndex < asset_.parts.size(); ++partIndex)
        {
            const Assets::FRigPart& part = asset_.parts[partIndex];
            for (size_t section = 0; section < part.sectionTintable.size() && section < 16; ++section)
            {
                if (part.sectionTintable[section])
                {
                    desc.partMaterialIds[partIndex][section] = tintMaterialId_;
                }
            }
        }

        boneNodes_.clear();
        auto rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset_, desc, boneNodes_);
        if (rigRoot)
        {
            rigRoot->SetParent(worldNode_);
        }

        // Clothing attachment nodes (hidden until worn).
        std::array<uint32_t, 16> clothingMats = {0};
        clothingMats[0] = clothingMaterialId_;
        if (Assets::Node* head = BoneNode("bone_head"))
        {
            helmetNode_ = Assets::SceneBuilder::CreateRenderNode("nd_player/helmet", glm::vec3(0.0f, 0.16f, 0.0f),
                                                                 glm::vec3(1.0f), scene.GenerateInstanceId(),
                                                                 helmetModelId_, clothingMats, false);
            helmetNode_->SetParent(head->shared_from_this());
        }
        if (Assets::Node* torso = BoneNode("bone_torso"))
        {
            backpackNode_ = Assets::SceneBuilder::CreateRenderNode("nd_player/backpack", glm::vec3(0.0f, 0.30f, -0.12f),
                                                                   glm::vec3(1.0f), scene.GenerateInstanceId(),
                                                                   backpackModelId_, clothingMats, false);
            backpackNode_->SetParent(torso->shared_from_this());
        }
        if (weaponAssetsSet_)
        {
            if (Assets::Node* socket = BoneNode("bone_weapon_socket"))
            {
                std::array<uint32_t, 16> weaponMats = {0};
                weaponMats[0] = weaponMaterialId_;
                weaponNode_ = Assets::SceneBuilder::CreateRenderNode(
                    "nd_player/weapon", glm::vec3(0.0f), glm::vec3(1.0f), scene.GenerateInstanceId(),
                    weaponModelId_, weaponMats, false, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
                weaponNode_->SetParent(socket->shared_from_this());
                scene.AddNode(weaponNode_);
                weaponVisible_ = true;
            }
        }

        worldNode_->RecalcTransform(true);
        animator_.Bind(&asset_, boneNodes_, worldNode_.get());
        locomotionLayer_ = animator_.CreateLayer("locomotion", NextGameplay::ERigLayerBlendMode::Override,
                                                  NextGameplay::FRigBoneMask::FullBody(asset_));

        NextGameplay::FRigBoneMask upperBody = NextGameplay::FRigBoneMask::FromSubtree(asset_, "bone_torso");
        upperBody.SetBoneWeight(asset_, "bone_torso", 0.65f);
        upperBody.SetBoneWeight(asset_, "bone_head", 0.25f);
        aimLayer_ = animator_.CreateLayer("aim", NextGameplay::ERigLayerBlendMode::Override, upperBody);

        NextGameplay::FRigBoneMask recoilMask = upperBody;
        recoilMask.SetBoneWeight(asset_, "bone_head", 0.0f);
        recoilLayer_ = animator_.CreateLayer("recoil", NextGameplay::ERigLayerBlendMode::Additive, recoilMask);
        actionLayer_ = animator_.CreateLayer("action", NextGameplay::ERigLayerBlendMode::Override,
                                             NextGameplay::FRigBoneMask::FullBody(asset_));

        animator_.SetLayerWeight(aimLayer_, 0.0f);
        animator_.SetLayerWeight(recoilLayer_, 0.0f);
        animator_.SetLayerWeight(actionLayer_, 0.0f);
        if (const Assets::FRigClip* idle = asset_.FindClip("stand_idle"))
        {
            animator_.SetLoopBlend(locomotionLayer_, {{idle, 1.0f}}, "locomotion", 1.0f, 0.0f);
            baseClipName_ = idle->name;
        }
        animator_.Update(0.0f);
        bound_ = true;
        aimWeight_ = 0.0f;
        actionWeight_ = 0.0f;
        recoilActive_ = false;
        scene.MarkDirty();
    }

    void PlayerRigVisual::OnSceneUnloaded()
    {
        // Only clear per-scene runtime state; injected models/materials belong to
        // the incoming scene rebuild and must survive (the AirportSim lesson).
        animator_ = NextGameplay::FRigLayeredAnimator{};
        locomotionLayer_ = NextGameplay::invalidRigLayerHandle;
        aimLayer_ = NextGameplay::invalidRigLayerHandle;
        recoilLayer_ = NextGameplay::invalidRigLayerHandle;
        actionLayer_ = NextGameplay::invalidRigLayerHandle;
        bound_ = false;
        baseClipName_.clear();
        scene_ = nullptr;
        worldNode_.reset();
        helmetNode_.reset();
        backpackNode_.reset();
        weaponNode_.reset();
        weaponVisible_ = false;
        boneNodes_.clear();
        aimWeight_ = 0.0f;
        actionWeight_ = 0.0f;
        recoilActive_ = false;
    }

    void PlayerRigVisual::SetVisible(bool visible)
    {
        visible_ = visible;
    }

    void PlayerRigVisual::SetClothing(const std::string& clothingId, bool on)
    {
        std::shared_ptr<Assets::Node> node;
        if (clothingId == "helmet")
        {
            node = helmetNode_;
        }
        else if (clothingId == "backpack")
        {
            node = backpackNode_;
        }
        if (!node)
        {
            return;
        }
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetVisible(on);
        }
        if (scene_)
        {
            scene_->MarkDirty();
        }
    }

    void PlayerRigVisual::TriggerRecoil(float scale)
    {
        if (!bound_)
        {
            return;
        }
        animator_.SetLayerWeight(recoilLayer_, glm::clamp(scale, 0.0f, 1.0f));
        animator_.PlayOneShot(recoilLayer_, asset_.FindClip("recoil_rifle"), 1.0f, 0.0f,
                              animationConfig_.RecoilFadeOutSeconds, true);
        recoilActive_ = true;
    }

    bool PlayerRigVisual::RecoilActive() const
    {
        return recoilActive_;
    }

    void PlayerRigVisual::Update(const glm::vec3& feetPosition, float yaw, const FPlayerPresentationState& state,
                                 float deltaSeconds)
    {
        if (!bound_ || !worldNode_)
        {
            return;
        }

        const glm::vec3 pos = visible_ ? feetPosition : glm::vec3(feetPosition.x, kParkedY, feetPosition.z);
        worldNode_->Translation() = pos;
        worldNode_->Rotation() = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        worldNode_->RecalcTransform(true);

        const float aimTarget =
            state.hasWeapon && state.action == EPlayerAction::None ? glm::clamp(state.aimWeight, 0.0f, 1.0f) : 0.0f;
        const float aimRate = animationConfig_.AimFadeSeconds > 0.0f ? 4.0f / animationConfig_.AimFadeSeconds : 1000.0f;
        const float aimT = 1.0f - std::exp(-aimRate * std::max(deltaSeconds, 0.0f));
        aimWeight_ = glm::mix(aimWeight_, aimTarget, aimT);
        animator_.SetLayerWeight(aimLayer_, aimWeight_);
        const float pitchLimit = glm::radians(std::max(animationConfig_.AimPitchLimitDegrees, 1.0f));
        const float pitch = glm::clamp(state.aimPitchRadians / pitchLimit, -1.0f, 1.0f);
        const float downWeight = std::max(pitch, 0.0f);
        const float upWeight = std::max(-pitch, 0.0f);
        animator_.SetStaticBlend(
            aimLayer_,
            {
                {asset_.FindClip("aim_rifle_down"), downWeight},
                {asset_.FindClip("aim_rifle_center"), 1.0f - std::abs(pitch)},
                {asset_.FindClip("aim_rifle_up"), upWeight},
            });

        const float actionTarget = state.action == EPlayerAction::LootGround ? 1.0f : 0.0f;
        const float actionRate =
            animationConfig_.LocomotionFadeSeconds > 0.0f ? 4.0f / animationConfig_.LocomotionFadeSeconds : 1000.0f;
        const float actionT = 1.0f - std::exp(-actionRate * std::max(deltaSeconds, 0.0f));
        actionWeight_ = glm::mix(actionWeight_, actionTarget, actionT);
        animator_.SetLayerWeight(actionLayer_, actionWeight_);
        if (state.action == EPlayerAction::LootGround)
        {
            animator_.SetManualBlend(actionLayer_, {{asset_.FindClip("loot_ground"), 1.0f}}, state.actionTime01);
            animator_.SetLayerWeight(recoilLayer_, 0.0f);
            recoilActive_ = false;
        }

        if (weaponNode_)
        {
            const bool showWeapon = visible_ && state.hasWeapon;
            if (showWeapon != weaponVisible_)
            {
                weaponVisible_ = showWeapon;
                if (auto render = weaponNode_->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(showWeapon);
                }
                if (scene_)
                {
                    scene_->MarkDirty();
                }
            }
        }

        const FPlayerLocomotionState& locomotion = state.locomotion;
        const bool moving = locomotion.horizontalSpeed >= animationConfig_.MoveThreshold &&
                            glm::dot(locomotion.localMove, locomotion.localMove) > 0.001f;

        std::vector<NextGameplay::FRigClipBlendSample> samples;
        float authoredSpeed = 1.0f;
        if (!moving)
        {
            const char* idleName =
                locomotion.actualStance == EPlayerStance::Crouched ? "crouch_idle" : "stand_idle";
            samples.push_back({asset_.FindClip(idleName), 1.0f});
        }
        else
        {
            const char* prefix = "stand_run";
            if (locomotion.actualStance == EPlayerStance::Crouched)
            {
                prefix = "crouch_walk";
                authoredSpeed = animationConfig_.CrouchWalkAuthoredSpeed;
            }
            else if (locomotion.gait == EPlayerGait::Walk)
            {
                prefix = "stand_walk";
                authoredSpeed = animationConfig_.StandWalkAuthoredSpeed;
            }
            else if (locomotion.gait == EPlayerGait::Sprint)
            {
                prefix = "stand_sprint";
                authoredSpeed = animationConfig_.StandSprintAuthoredSpeed;
            }
            else
            {
                authoredSpeed = animationConfig_.StandRunAuthoredSpeed;
            }

            const float right = std::max(locomotion.localMove.x, 0.0f);
            const float left = std::max(-locomotion.localMove.x, 0.0f);
            const float forward = std::max(locomotion.localMove.y, 0.0f);
            const float backward = std::max(-locomotion.localMove.y, 0.0f);
            const float sum = std::max(right + left + forward + backward, 0.001f);
            samples = {
                {asset_.FindClip(fmt::format("{}_f", prefix)), forward / sum},
                {asset_.FindClip(fmt::format("{}_b", prefix)), backward / sum},
                {asset_.FindClip(fmt::format("{}_l", prefix)), left / sum},
                {asset_.FindClip(fmt::format("{}_r", prefix)), right / sum},
            };
        }

        const float playRate = moving
                                   ? glm::clamp(locomotion.horizontalSpeed / std::max(authoredSpeed, 0.01f),
                                                animationConfig_.MinPlayRate, animationConfig_.MaxPlayRate)
                                   : 1.0f;
        animator_.SetLoopBlend(locomotionLayer_, std::move(samples), "locomotion", playRate,
                               animationConfig_.LocomotionFadeSeconds);
        if (const Assets::FRigClip* clip = animator_.DominantClip(locomotionLayer_))
        {
            baseClipName_ = clip->name;
        }
        animator_.Update(deltaSeconds);
        if (recoilActive_ && animator_.IsOneShotComplete(recoilLayer_))
        {
            recoilActive_ = false;
        }
        if (scene_)
        {
            scene_->MarkTransformDirty();
        }
    }
}
