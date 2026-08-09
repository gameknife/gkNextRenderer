#include "Brotato3DPlayerRigVisual.hpp"

#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Modules/ScadLoader/FScadRig.h"

namespace Brotato3D
{
    namespace
    {
        constexpr float rigScale = 1.05f;
        constexpr float movementEpsilon = 0.05f;
        constexpr float authoredWalkSpeed = 2.0f;
        constexpr float parkedY = -100.0f;

        float DirectionYaw(const glm::vec3& direction)
        {
            return std::atan2(direction.x, direction.z);
        }

        float WrapRadians(float radians)
        {
            return std::remainder(radians, glm::two_pi<float>());
        }
    }

    bool FPlayerRigVisual::LoadRig(const std::string& scadPath)
    {
        std::string error;
        std::vector<std::string> warnings;
        hasRig_ = ::Assets::FScadRigLoader::LoadRig(scadPath, {}, asset_, error, &warnings);
        if (!hasRig_)
        {
            SPDLOG_ERROR("[Brotato3D] failed to load player rig '{}': {}", scadPath, error);
        }
        for (const std::string& warning : warnings)
        {
            SPDLOG_WARN("[Brotato3D] player rig: {}", warning);
        }
        return hasRig_;
    }

    void FPlayerRigVisual::InjectAssets(std::vector<::Assets::Model>& models,
                                       std::vector<::Assets::FMaterial>& materials,
                                       uint32_t defaultTintMaterialId)
    {
        if (!hasRig_)
        {
            return;
        }

        tintMaterialId_ = defaultTintMaterialId;
        partModelIds_.clear();
        partMaterialIds_.clear();
        for (const ::Assets::FRigPart& part : asset_.parts)
        {
            std::array<uint32_t, 16> sectionMaterials{};
            for (size_t section = 0;
                 section < part.sectionColors.size() && section < sectionMaterials.size();
                 ++section)
            {
                sectionMaterials[section] = part.sectionTintable[section]
                                                ? tintMaterialId_
                                                : ::Assets::SceneBuilder::AddLambertianMaterial(
                                                      materials, glm::vec3(part.sectionColors[section]));
            }
            partMaterialIds_.push_back(sectionMaterials);
            models.push_back(asset_.partModels[part.modelIndex]);
            partModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
        }
        injected_ = true;
    }

    ::Assets::Node* FPlayerRigVisual::BoneNode(std::string_view boneName)
    {
        const int32_t index = asset_.FindBone(boneName);
        return index >= 0 && index < static_cast<int32_t>(boneNodes_.size()) ? boneNodes_[index] : nullptr;
    }

    void FPlayerRigVisual::OnSceneLoaded(::Assets::Scene& scene)
    {
        if (!hasRig_ || !injected_)
        {
            return;
        }

        scene_ = &scene;
        worldNode_ = ::Assets::Node::CreateNode(
            "Brotato3D_PlayerRig", glm::vec3(0.0f, parkedY, 0.0f),
            glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(rigScale), scene.GenerateInstanceId());
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        NextGameplay::FRigInstanceDesc desc;
        desc.namePrefix = "Brotato3D_PlayerRig";
        desc.partModelIds = partModelIds_;
        desc.partMaterialIds = partMaterialIds_;
        boneNodes_.clear();
        const auto rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset_, desc, boneNodes_);
        if (!rigRoot)
        {
            worldNode_.reset();
            scene_ = nullptr;
            return;
        }
        rigRoot->SetParent(worldNode_);

        partNodes_.assign(asset_.parts.size(), nullptr);
        for (size_t partIndex = 0; partIndex < asset_.parts.size(); ++partIndex)
        {
            const int32_t boneIndex = asset_.parts[partIndex].bone;
            if (boneIndex < 0 || boneIndex >= static_cast<int32_t>(boneNodes_.size()))
            {
                continue;
            }
            const std::string expectedName = fmt::format(
                "Brotato3D_PlayerRig/{}__part{}", asset_.bones[boneIndex].name, partIndex);
            for (const auto& child : boneNodes_[boneIndex]->Children())
            {
                if (child->GetName() == expectedName)
                {
                    partNodes_[partIndex] = child.get();
                    break;
                }
            }
        }

        animator_.Bind(&asset_, boneNodes_, worldNode_.get());
        animator_.Play(asset_.FindClip("idle") ? "idle" : "", 0.0f);
        bound_ = true;
        walking_ = false;
        SetVisible(visible_);
        ApplyTintMaterials();
        scene.MarkDirty();
    }

    void FPlayerRigVisual::OnSceneUnloaded()
    {
        animator_ = NextGameplay::FRigAnimator{};
        bound_ = false;
        scene_ = nullptr;
        worldNode_.reset();
        boneNodes_.clear();
        partNodes_.clear();
    }

    void FPlayerRigVisual::SetVisible(bool visible)
    {
        visible_ = visible;
        for (::Assets::Node* node : partNodes_)
        {
            if (node)
            {
                if (auto render = node->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetVisible(visible);
                }
            }
        }
        if (!visible && worldNode_)
        {
            worldNode_->Translation().y = parkedY;
            worldNode_->RecalcTransform(true);
        }
    }

    void FPlayerRigVisual::SetTintMaterial(uint32_t materialId)
    {
        tintMaterialId_ = materialId;
        ApplyTintMaterials();
    }

    void FPlayerRigVisual::ApplyTintMaterials()
    {
        for (size_t partIndex = 0; partIndex < asset_.parts.size() && partIndex < partNodes_.size(); ++partIndex)
        {
            ::Assets::Node* node = partNodes_[partIndex];
            if (!node)
            {
                continue;
            }
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (!render)
            {
                continue;
            }
            std::array<uint32_t, 16> materials = partMaterialIds_[partIndex];
            const ::Assets::FRigPart& part = asset_.parts[partIndex];
            for (size_t section = 0; section < part.sectionTintable.size() && section < materials.size(); ++section)
            {
                if (part.sectionTintable[section])
                {
                    materials[section] = tintMaterialId_;
                }
            }
            render->SetMaterials(materials);
        }
        if (scene_)
        {
            scene_->MarkDirty();
        }
    }

    void FPlayerRigVisual::ResetFacing(const glm::vec3& direction)
    {
        glm::vec3 flat(direction.x, 0.0f, direction.z);
        if (glm::length(flat) > movementEpsilon)
        {
            lowerFacingDir_ = glm::normalize(flat);
        }
    }

    void FPlayerRigVisual::Update(const glm::vec3& feetPosition,
                                  const glm::vec3& movementVelocity,
                                  const glm::vec3& aimDirection,
                                  float deltaSeconds)
    {
        if (!bound_ || !worldNode_)
        {
            return;
        }

        glm::vec3 flatVelocity(movementVelocity.x, 0.0f, movementVelocity.z);
        const float moveSpeed = glm::length(flatVelocity);
        const bool isWalking = moveSpeed > movementEpsilon;
        if (isWalking)
        {
            lowerFacingDir_ = flatVelocity / moveSpeed;
        }

        if (isWalking != walking_)
        {
            walking_ = isWalking;
            animator_.Play(asset_.FindClip(walking_ ? "walk" : "idle") ? (walking_ ? "walk" : "idle") : "", 0.12f);
        }
        animator_.SetPlaySpeed(walking_ ? glm::clamp(moveSpeed / authoredWalkSpeed, 0.65f, 3.0f) : 1.0f);

        const float lowerYaw = DirectionYaw(lowerFacingDir_);
        worldNode_->Translation() = visible_ ? feetPosition : glm::vec3(feetPosition.x, parkedY, feetPosition.z);
        worldNode_->Rotation() = glm::angleAxis(lowerYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        animator_.Update(deltaSeconds);

        glm::vec3 flatAim(aimDirection.x, 0.0f, aimDirection.z);
        if (glm::length(flatAim) > movementEpsilon)
        {
            flatAim = glm::normalize(flatAim);
            if (::Assets::Node* torso = BoneNode("bone_torso"))
            {
                const int32_t torsoIndex = asset_.FindBone("bone_torso");
                const float relativeYaw = WrapRadians(DirectionYaw(flatAim) - lowerYaw);
                torso->Rotation() = asset_.bones[torsoIndex].bindR *
                                    glm::angleAxis(relativeYaw, glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }
        worldNode_->RecalcTransform(true);
        if (scene_)
        {
            scene_->MarkTransformDirty();
        }
    }
}
