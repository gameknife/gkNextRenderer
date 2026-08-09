#include "CharacterPool.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Modules/ScadLoader/FScadRig.h"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace NextGameplay::Sim
{
    namespace
    {
        glm::vec3 SlotTint(const FCharacterPoolConfig& config, int slot)
        {
            return slot >= 0 && static_cast<size_t>(slot) < config.slotTints.size()
                       ? config.slotTints[static_cast<size_t>(slot)]
                       : glm::vec3(1.0f);
        }
    }

    void FCharacterPool::Configure(const FCharacterPoolConfig& config)
    {
        config_ = config;
        config_.poolCapacity = std::max(0, config_.poolCapacity);
        config_.rigVisual.parkedPosition = config_.parkedPosition;
        characters_.clear();
        characters_.resize(static_cast<size_t>(config_.poolCapacity));
        for (int slot = 0; slot < config_.poolCapacity; ++slot)
        {
            characters_[static_cast<size_t>(slot)].id = slot;
        }
    }

    void FCharacterPool::InjectAssets(std::vector<Assets::Model>& models,
                                      std::vector<Assets::FMaterial>& materials)
    {
        if (assetsInjected_)
        {
            return;
        }

        rigLoaded_ = false;
        rigAsset_ = nullptr;
        if (config_.useRig && config_.rigAsset != nullptr)
        {
            rigAsset_ = config_.rigAsset;
            rigLoaded_ = true;
        }
        else if (config_.useRig && !config_.rigPath.empty())
        {
            std::string error;
            rigLoaded_ = Assets::FScadRigLoader::LoadRig(config_.rigPath, {}, ownedRigAsset_, error);
            if (rigLoaded_)
            {
                rigAsset_ = &ownedRigAsset_;
            }
            else
            {
                SPDLOG_WARN("Sim/CharacterPool: rig load failed ({}), using box visuals", error);
            }
        }

        if (rigLoaded_)
        {
            rigBaseMaterials_.clear();
            for (const Assets::FRigPart& part : rigAsset_->parts)
            {
                std::array<uint32_t, 16> sectionMaterials = {0};
                for (size_t section = 0;
                     section < part.sectionColors.size() && section < sectionMaterials.size(); ++section)
                {
                    if (!part.sectionTintable[section])
                    {
                        sectionMaterials[section] = Assets::SceneBuilder::AddLambertianMaterial(
                            materials, glm::vec3(part.sectionColors[section]));
                    }
                }
                rigBaseMaterials_.push_back(sectionMaterials);
            }

            rigSlotPartModelIds_.assign(static_cast<size_t>(config_.poolCapacity), {});
            rigSlotTintMaterials_.clear();
            for (int slot = 0; slot < config_.poolCapacity; ++slot)
            {
                auto& slotModels = rigSlotPartModelIds_[static_cast<size_t>(slot)];
                slotModels.reserve(rigAsset_->parts.size());
                for (const Assets::FRigPart& part : rigAsset_->parts)
                {
                    models.push_back(rigAsset_->partModels[part.modelIndex]);
                    slotModels.push_back(static_cast<uint32_t>(models.size() - 1));
                }
                rigSlotTintMaterials_.push_back(
                    Assets::SceneBuilder::AddLambertianMaterial(materials, SlotTint(config_, slot)));
            }
            assetsInjected_ = true;
            return;
        }

        modelIds_.clear();
        materialIds_.clear();
        for (int slot = 0; slot < config_.poolCapacity; ++slot)
        {
            models.push_back(Assets::FProcModel::CreateBox(config_.boxHalfMin, config_.boxHalfMax));
            modelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
            materialIds_.push_back(
                Assets::SceneBuilder::AddLambertianMaterial(materials, SlotTint(config_, slot)));
        }
        assetsInjected_ = true;
    }

    void FCharacterPool::BuildNavGrid(Assets::Scene& scene)
    {
        const glm::vec3 sceneMin = scene.GetSceneAABBMin();
        const glm::vec3 sceneMax = scene.GetSceneAABBMax();
        NextGameplay::FNavGridSettings settings;
        settings.cellSize = config_.navCellSize;
        settings.agentRadius = config_.agentRadius;
        settings.maxSlopeAngle = 50.0f;
        settings.clearanceHeight = 1.7f;
        settings.maxStepHeight = 0.35f;
        settings.worldMin = glm::vec3(sceneMin.x - 2.0f, 0.0f, sceneMin.z - 2.0f);
        settings.worldMax = glm::vec3(sceneMax.x + 2.0f, 0.0f, sceneMax.z + 2.0f);
        settings.sampleCeiling = sceneMax.y + 5.0f;
        settings.floorHeightTolerance = 1.0f;
        navGrid_.Build(scene.GetCPUAccelerationStructure(), settings);
        navReady_ = navGrid_.IsBuilt();
    }

    void FCharacterPool::OnSceneLoaded(Assets::Scene& scene)
    {
        scene_ = &scene;
        BuildNavGrid(scene);
        if (characters_.size() != static_cast<size_t>(config_.poolCapacity))
        {
            Configure(config_);
        }

        for (int slot = 0; slot < config_.poolCapacity; ++slot)
        {
            FSimCharacter& character = characters_[static_cast<size_t>(slot)];
            if (rigLoaded_)
            {
                NextGameplay::FRigInstanceDesc desc;
                desc.namePrefix = fmt::format("{}_{:02d}", config_.nodeNamePrefix, slot);
                desc.partModelIds = rigSlotPartModelIds_[static_cast<size_t>(slot)];
                desc.partMaterialIds = rigBaseMaterials_;
                for (size_t partIndex = 0; partIndex < rigAsset_->parts.size(); ++partIndex)
                {
                    const Assets::FRigPart& part = rigAsset_->parts[partIndex];
                    for (size_t section = 0; section < part.sectionTintable.size() && section < 16; ++section)
                    {
                        if (part.sectionTintable[section])
                        {
                            desc.partMaterialIds[partIndex][section] =
                                rigSlotTintMaterials_[static_cast<size_t>(slot)];
                        }
                    }
                }
                character.visual = std::make_unique<FScadRigVisual>(
                    scene, *rigAsset_, desc, slot, config_.rigVisual);
                continue;
            }

            const uint32_t instanceId = scene.GenerateInstanceId();
            auto node = Assets::SceneBuilder::CreateRenderNode(
                fmt::format("{}_{:02d}", config_.nodeNamePrefix, slot), config_.parkedPosition, glm::vec3(1.0f),
                instanceId, modelIds_[static_cast<size_t>(slot)], materialIds_[static_cast<size_t>(slot)]);
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            node->AddComponent(physics);
            scene.AddNode(node);
            character.visual = std::make_unique<FGeometryVisual>(std::move(node), config_.parkedPosition);
        }
        scene.MarkDirty();
    }

    void FCharacterPool::Clear()
    {
        for (FSimCharacter& character : characters_)
        {
            character.visual.reset();
            character.active = false;
            character.follower.Clear();
        }
        navGrid_ = NextGameplay::FNavGrid{};
        navReady_ = false;
        scene_ = nullptr;
        assetsInjected_ = false;
    }

    void FCharacterPool::SetSlotTint(int slot, const glm::vec3& tintColor)
    {
        if (scene_ == nullptr)
        {
            return;
        }
        const std::vector<uint32_t>& ids = rigLoaded_ ? rigSlotTintMaterials_ : materialIds_;
        if (slot < 0 || static_cast<size_t>(slot) >= ids.size())
        {
            return;
        }
        const uint32_t materialId = ids[static_cast<size_t>(slot)];
        if (materialId < scene_->Materials().size())
        {
            scene_->Materials()[materialId].gpuMaterial_.Diffuse = glm::vec4(tintColor, 1.0f);
        }
    }

    FSimCharacter* FCharacterPool::Acquire(int slot, const glm::vec3& position, const glm::vec3& tintColor)
    {
        if (slot < 0 || static_cast<size_t>(slot) >= characters_.size())
        {
            return nullptr;
        }
        FSimCharacter& character = characters_[static_cast<size_t>(slot)];
        if (character.active)
        {
            return nullptr;
        }
        character.active = true;
        character.position = position;
        character.yaw = 0.0f;
        character.moving = false;
        character.follower.Clear();
        character.scriptWaypoints.clear();
        character.anim = EAnimHint::Idle;
        SetSlotTint(slot, tintColor);
        if (character.visual)
        {
            character.visual->SetVisible(true);
            character.visual->SetAnimHint(EAnimHint::Idle);
            character.visual->SetWorldTransform(position, 0.0f);
        }
        return &character;
    }

    void FCharacterPool::Release(FSimCharacter& character)
    {
        character.active = false;
        character.moving = false;
        character.follower.Clear();
        character.anim = EAnimHint::Idle;
        if (character.visual)
        {
            character.visual->SetAnimHint(EAnimHint::Idle);
            character.visual->SetVisible(false);
        }
    }

    bool FCharacterPool::MoveTo(FSimCharacter& character, const glm::vec3& target)
    {
        character.scriptWaypoints.clear();
        glm::vec3 goal = target;
        goal.y = config_.groundY;
        std::vector<glm::vec3> path;
        if (navReady_)
        {
            path = navGrid_.FindPath(character.position, goal, config_.groundY);
        }
        const bool found = !path.empty();
        if (!found)
        {
            path.push_back(goal);
        }
        character.follower.SetPath(std::move(path), goal);
        character.moveTarget = goal;
        character.moving = true;
        return found;
    }

    void FCharacterPool::MoveAlong(FSimCharacter& character, std::vector<glm::vec3> waypoints)
    {
        if (waypoints.empty())
        {
            return;
        }
        for (glm::vec3& waypoint : waypoints)
        {
            waypoint.y = config_.groundY;
        }
        character.moveTarget = waypoints.back();
        character.follower.SetPath(std::move(waypoints), character.moveTarget);
        character.moving = true;
    }

    bool FCharacterPool::Arrived(const FSimCharacter& character) const
    {
        return !character.moving || character.follower.IsFinished(character.position, 0.45f);
    }

    void FCharacterPool::Tick(float deltaSeconds)
    {
        std::vector<FSimCharacter*> active;
        active.reserve(characters_.size());
        for (FSimCharacter& character : characters_)
        {
            if (character.active)
            {
                active.push_back(&character);
            }
        }
        TickCharacters(deltaSeconds, active);
    }

    void FCharacterPool::Tick(float deltaSeconds, std::span<FSimCharacter*> characters)
    {
        TickCharacters(deltaSeconds, characters);
    }

    void FCharacterPool::TickCharacters(float deltaSeconds, std::span<FSimCharacter*> characters)
    {
        for (FSimCharacter* character : characters)
        {
            if (character == nullptr || !character->active)
            {
                continue;
            }
            glm::vec3 velocity{0.0f};
            if (character->moving)
            {
                if (character->follower.IsFinished(character->position, 0.35f))
                {
                    character->moving = false;
                }
                else
                {
                    velocity = character->follower.GetMoveDirection(character->position, 0.6f) * character->speed;
                }
            }

            if (character->moving)
            {
                glm::vec3 separation{0.0f};
                for (const FSimCharacter* other : characters)
                {
                    if (other == nullptr || !other->active || other == character)
                    {
                        continue;
                    }
                    glm::vec3 away = character->position - other->position;
                    away.y = 0.0f;
                    const float distance = glm::length(away);
                    if (distance > 0.001f && distance < config_.separationRadius)
                    {
                        separation += away / distance * (1.0f - distance / config_.separationRadius);
                    }
                }
                velocity += separation * config_.separationStrength;
            }

            const float speed = glm::length(velocity);
            if (speed > 0.01f)
            {
                character->position += velocity * deltaSeconds;
                character->position.y = config_.groundY;
                const glm::vec3 direction = velocity / speed;
                character->yaw = std::atan2(direction.x, direction.z);
                character->anim = EAnimHint::Walk;
            }
            else if (character->anim == EAnimHint::Walk)
            {
                character->anim = EAnimHint::Idle;
            }

            if (character->visual)
            {
                character->visual->SetMoveSpeed(character->speed);
                character->visual->SetAnimHint(character->anim);
                character->visual->SetWorldTransform(character->position, character->yaw);
                character->visual->Tick(deltaSeconds);
            }
        }
    }

    void FCharacterPool::Tick(float deltaSeconds, Assets::Scene& scene)
    {
        Tick(deltaSeconds);
        scene.MarkTransformDirty();
    }

    void FCharacterPool::Tick(float deltaSeconds, Assets::Scene& scene,
                              std::span<FSimCharacter*> characters)
    {
        TickCharacters(deltaSeconds, characters);
        scene.MarkTransformDirty();
    }
}
