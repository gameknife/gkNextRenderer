#include "PlayerRigVisual.hpp"

#include <glm/gtc/quaternion.hpp>

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
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

        worldNode_->RecalcTransform(true);

        animator_.Bind(&asset_, boneNodes_, worldNode_.get());
        animator_.Play(asset_.FindClip("idle") != nullptr ? "idle" : "", 0.0f);
        bound_ = true;
        state_ = EAnimState::Idle;
        scene.MarkDirty();
    }

    void PlayerRigVisual::OnSceneUnloaded()
    {
        // Only clear per-scene runtime state; injected models/materials belong to
        // the incoming scene rebuild and must survive (the AirportSim lesson).
        animator_ = NextGameplay::FRigAnimator{};
        bound_ = false;
        scene_ = nullptr;
        worldNode_.reset();
        helmetNode_.reset();
        backpackNode_.reset();
        boneNodes_.clear();
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

    void PlayerRigVisual::Update(const glm::vec3& feetPosition, float yaw, EAnimState state, float moveSpeed,
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

        // Clip selection: Run reuses the walk clip sped up; Fire is a one-shot.
        if (state != state_)
        {
            state_ = state;
            switch (state)
            {
            case EAnimState::Fire:
                animator_.SetPlaySpeed(1.0f);
                animator_.Play(asset_.FindClip("fire") != nullptr ? "fire" : "idle", 0.05f);
                break;
            case EAnimState::Run:
            case EAnimState::Walk:
                animator_.Play(asset_.FindClip("walk") != nullptr ? "walk" : "idle");
                break;
            case EAnimState::Idle:
            default:
                animator_.SetPlaySpeed(1.0f);
                animator_.Play(asset_.FindClip("idle") != nullptr ? "idle" : "");
                break;
            }
        }

        if (state_ == EAnimState::Walk || state_ == EAnimState::Run)
        {
            const float speed = baseWalkClipSpeed_ > 0.0f ? moveSpeed / baseWalkClipSpeed_ : 1.0f;
            animator_.SetPlaySpeed(glm::clamp(speed, 0.6f, 2.4f));
        }

        animator_.Update(deltaSeconds);
        if (scene_)
        {
            scene_->MarkTransformDirty();
        }
    }
}
