#include "Application/Game/NextAstrobot/Player/PlayerRigVisual.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Modules/ScadLoader/FScadRig.h"

namespace NextAstrobot
{
    namespace
    {
        // Parked far below the level until the first Update places the rig.
        constexpr float kParkedY = -1000.0f;
        constexpr float kLandClipSeconds = 0.3f;
        // The kit's own blue for the player, so they match the NPC robots around them;
        // the rest are for rescued robots, which read better as a crowd when they are not
        // all the same colour. Same values as kit_astro's ab_BLUE / TEAL / ORANGE / PINK.
        constexpr std::array<glm::vec3, 4> kTintColors{
            glm::vec3{0.08f, 0.30f, 0.66f},
            glm::vec3{0.10f, 0.54f, 0.52f},
            glm::vec3{0.84f, 0.42f, 0.08f},
            glm::vec3{0.84f, 0.44f, 0.55f},
        };

        void SetSubtreeVisible(const std::shared_ptr<Assets::Node>& node, bool visible)
        {
            Assets::NodeUtils::SetVisibleRecursive(node, visible);
        }
    }

    bool FPlayerRigVisual::LoadRig(const std::string& scadPath)
    {
        std::string error;
        std::vector<std::string> warnings;
        hasRig_ = Assets::FScadRigLoader::LoadRig(scadPath, {}, asset_, error, &warnings);
        if (!hasRig_)
        {
            SPDLOG_ERROR("[NextAstrobot] failed to load player rig '{}': {}", scadPath, error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[NextAstrobot] rig: {}", warning);
        }
        return hasRig_;
    }

    void FPlayerRigVisual::InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        if (!hasRig_)
        {
            return;
        }
        partModelIds_.clear();
        partMaterialIds_.clear();
        for (const Assets::FRigPart& part : asset_.parts)
        {
            std::array<uint32_t, 16> sectionMaterials{};
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
        tintMaterialIds_.clear();
        for (const glm::vec3& color : kTintColors)
        {
            tintMaterialIds_.push_back(Assets::SceneBuilder::AddLambertianMaterial(materials, color));
        }
        injected_ = true;
    }

    NextGameplay::FRigInstanceDesc FPlayerRigVisual::BaseInstanceDesc() const
    {
        NextGameplay::FRigInstanceDesc desc;
        desc.partModelIds = partModelIds_;
        desc.partMaterialIds = partMaterialIds_;
        return desc;
    }

    Assets::Node* FPlayerRigVisual::BoneNode(std::string_view boneName)
    {
        const int32_t index = asset_.FindBone(boneName);
        if (index < 0 || index >= static_cast<int32_t>(boneNodes_.size()))
        {
            return nullptr;
        }
        return boneNodes_[index];
    }

    void FPlayerRigVisual::OnSceneLoaded(Assets::Scene& scene)
    {
        if (!hasRig_ || !injected_)
        {
            return;
        }
        scene_ = &scene;

        worldNode_ = Assets::Node::CreateNode("astro_player_rig", glm::vec3(0.0f, kParkedY, 0.0f),
                                              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f),
                                              scene.GenerateInstanceId());
        // Dynamic mobility makes the engine re-upload the subtree transform every frame
        // without binding a collider (the character controller owns collision).
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        NextGameplay::FRigInstanceDesc desc = BaseInstanceDesc();
        desc.namePrefix = "astro_player";
        const uint32_t playerTint = tintMaterialIds_.empty() ? 0 : tintMaterialIds_.front();
        for (size_t partIndex = 0; partIndex < asset_.parts.size(); ++partIndex)
        {
            const Assets::FRigPart& part = asset_.parts[partIndex];
            for (size_t section = 0; section < part.sectionTintable.size() && section < 16; ++section)
            {
                if (part.sectionTintable[section])
                {
                    desc.partMaterialIds[partIndex][section] = playerTint;
                }
            }
        }

        boneNodes_.clear();
        std::shared_ptr<Assets::Node> rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset_, desc, boneNodes_);
        if (rigRoot)
        {
            rigRoot->SetParent(worldNode_);
            // The rig is a visual only: it must never take part in ground raycasts or it
            // would become its own implicit static collider.
            Assets::NodeUtils::SetRayCastVisibleRecursive(worldNode_, false);
            animator_.Bind(&asset_, boneNodes_, rigRoot.get());
            animator_.Play("idle", 0.0f);
            currentClip_ = "idle";
            bound_ = true;
        }
        jetBone_ = BoneNode("bone_jet");
        if (jetBone_)
        {
            Assets::NodeUtils::SetVisibleRecursive(jetBone_->shared_from_this(), false);
        }
        wasAirborne_ = false;
        landTimer_ = 0.0f;
    }

    void FPlayerRigVisual::OnSceneUnloaded()
    {
        // Runtime pointers only: the injected models/materials belong to the scene being
        // rebuilt, and clearing them here would throw away the new world's declarations.
        scene_ = nullptr;
        worldNode_.reset();
        boneNodes_.clear();
        jetBone_ = nullptr;
        bound_ = false;
        currentClip_.clear();
    }

    void FPlayerRigVisual::SetVisible(bool visible)
    {
        if (visible_ == visible)
        {
            return;
        }
        visible_ = visible;
        if (worldNode_)
        {
            SetSubtreeVisible(worldNode_, visible);
        }
    }

    void FPlayerRigVisual::Update(const glm::vec3& footPosition, float yaw, ELocomotion state, float horizontalSpeed,
                                  float runReferenceSpeed, float deltaSeconds)
    {
        if (!bound_ || !worldNode_)
        {
            return;
        }

        worldNode_->SetTransform(footPosition, glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)),
                                 worldNode_->Scale());

        const bool airborne = state == ELocomotion::Jump || state == ELocomotion::Fall ||
                              state == ELocomotion::Hover;
        if (wasAirborne_ && !airborne && state != ELocomotion::Dead && state != ELocomotion::Zip)
        {
            landTimer_ = kLandClipSeconds;
        }
        wasAirborne_ = airborne;
        landTimer_ = std::max(0.0f, landTimer_ - deltaSeconds);

        const char* clip = "idle";
        float playSpeed = 1.0f;
        switch (state)
        {
        case ELocomotion::Idle: clip = landTimer_ > 0.0f ? "land" : "idle"; break;
        case ELocomotion::Run:
            clip = landTimer_ > 0.0f ? "land" : "run";
            // The run clip is authored at the config's run speed; scale it with the actual one.
            playSpeed = runReferenceSpeed > 0.1f ? std::clamp(horizontalSpeed / runReferenceSpeed, 0.4f, 1.6f) : 1.0f;
            break;
        case ELocomotion::Jump: clip = "jump"; break;
        case ELocomotion::Fall: clip = "fall"; break;
        case ELocomotion::Hover: clip = "hover"; break;
        case ELocomotion::Zip: clip = "zip"; break;
        case ELocomotion::Punch: clip = "punch"; break;
        case ELocomotion::Dead: clip = "hurt"; break;
        }

        if (currentClip_ != clip)
        {
            animator_.Play(clip, 0.12f);
            currentClip_ = clip;
        }
        animator_.SetPlaySpeed(playSpeed);
        animator_.Update(deltaSeconds);

        if (jetBone_)
        {
            Assets::NodeUtils::SetVisibleRecursive(jetBone_->shared_from_this(),
                                                   visible_ && state == ELocomotion::Hover);
        }
    }
}
