#include "Gameplay/Rig/RigSubsystem.h"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextRig.hpp"
#include "Gameplay/Sim/ScadRigVisual.h"
#include "Modules/ScadLoader/FScadRig.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace NextGameplay::Rig
{
    namespace
    {
        /// Pools of ScadRig characters, and the animators that drive them.
        ///
        /// Deliberately not FCharacterPool. That one is a *sim* pool: it owns a navigation grid,
        /// path following and steering, which is most of its cost and none of what a game driving
        /// its own characters wants. What both need — inject part models per slot, instantiate,
        /// animate — is FScadRigVisual, and that is what this shares with it.
        class FRigSubsystem final : public NextRig
        {
        public:
            uint32_t DeclarePool(const std::string& rigPath,
                                 int32_t capacity,
                                 std::vector<Assets::Model>& models,
                                 std::vector<Assets::FMaterial>& materials) override
            {
                if (capacity <= 0)
                {
                    SPDLOG_WARN("[rig] pool for '{}' asked for capacity {}", rigPath, capacity);
                    return kInvalidId;
                }

                FPool pool;
                pool.path = rigPath;
                std::string error;
                if (!Assets::FScadRigLoader::LoadRig(rigPath, {}, pool.asset, error))
                {
                    SPDLOG_ERROR("[rig] '{}' failed to load: {}", rigPath, error);
                    return kInvalidId;
                }

                // Sections the asset does not tint share one material across the whole pool;
                // tinted ones need a material per slot, because that is the only thing that makes
                // two instances of one rig look different.
                for (const Assets::FRigPart& part : pool.asset.parts)
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
                    pool.baseMaterials.push_back(sectionMaterials);
                }

                // One copy of every part model per slot. The GPU-driven primitive buffer is sized
                // from this vector once, so a slot that was not injected here can never be filled.
                pool.slots.resize(static_cast<size_t>(capacity));
                for (int32_t slot = 0; slot < capacity; ++slot)
                {
                    FSlot& slotData = pool.slots[static_cast<size_t>(slot)];
                    slotData.partModelIds.reserve(pool.asset.parts.size());
                    for (const Assets::FRigPart& part : pool.asset.parts)
                    {
                        models.push_back(pool.asset.partModels[part.modelIndex]);
                        slotData.partModelIds.push_back(static_cast<uint32_t>(models.size() - 1));
                    }
                    slotData.tintMaterialId =
                        Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.8f));
                }

                pools_.push_back(std::move(pool));
                const uint32_t poolId = static_cast<uint32_t>(pools_.size());
                SPDLOG_INFO("[rig] pool {} = '{}' ({} bones, {} parts, {} clips) x{}", poolId, rigPath,
                            pools_.back().asset.bones.size(), pools_.back().asset.parts.size(),
                            pools_.back().asset.clips.size(), capacity);
                return poolId;
            }

            void OnSceneLoaded(Assets::Scene& scene) override
            {
                scene_ = &scene;
                for (size_t poolIndex = 0; poolIndex < pools_.size(); ++poolIndex)
                {
                    FPool& pool = pools_[poolIndex];
                    for (size_t slot = 0; slot < pool.slots.size(); ++slot)
                    {
                        FSlot& slotData = pool.slots[slot];

                        NextGameplay::FRigInstanceDesc desc;
                        desc.namePrefix = fmt::format("rig{}_{:02d}", poolIndex + 1, slot);
                        desc.partModelIds = slotData.partModelIds;
                        desc.partMaterialIds = pool.baseMaterials;
                        for (size_t partIndex = 0; partIndex < pool.asset.parts.size(); ++partIndex)
                        {
                            const Assets::FRigPart& part = pool.asset.parts[partIndex];
                            for (size_t section = 0;
                                 section < part.sectionTintable.size() && section < 16; ++section)
                            {
                                if (part.sectionTintable[section])
                                {
                                    desc.partMaterialIds[partIndex][section] = slotData.tintMaterialId;
                                }
                            }
                        }

                        slotData.visual = std::make_unique<Sim::FScadRigVisual>(
                            scene, pool.asset, desc, static_cast<int>(slot));
                        slotData.visual->SetVisible(false);
                        slotData.instanceId = kInvalidId;
                    }
                }
                if (!pools_.empty())
                {
                    scene.MarkDirty();
                }
            }

            void Clear() override
            {
                // Nodes belong to the scene that is being torn down; what has to go is every
                // pointer into it, and every id a game could still be holding.
                pools_.clear();
                instances_.clear();
                scene_ = nullptr;
                nextInstanceId_ = 1;
            }

            void Tick(float deltaSeconds) override
            {
                bool animated = false;
                for (FPool& pool : pools_)
                {
                    for (FSlot& slot : pool.slots)
                    {
                        // Free slots are parked out of the world; animating them would be work
                        // nobody can see.
                        if (slot.instanceId != kInvalidId && slot.visual)
                        {
                            slot.visual->Tick(deltaSeconds);
                            animated = true;
                        }
                    }
                }

                // Done here rather than left to the caller: an animator writes node transforms and
                // the renderer only re-uploads instance transforms when told to, so a game that
                // forgot this would see a character frozen in its bind pose and have no way to
                // guess why. Native pools do the same at the end of their tick.
                if (animated && scene_ != nullptr)
                {
                    scene_->MarkTransformDirty();
                }
            }

            uint32_t Acquire(uint32_t poolId, const glm::vec3& position, float yawRadians,
                             const glm::vec3& tint) override
            {
                FPool* pool = FindPool(poolId);
                if (pool == nullptr || scene_ == nullptr)
                {
                    return kInvalidId;
                }

                for (size_t slot = 0; slot < pool->slots.size(); ++slot)
                {
                    FSlot& slotData = pool->slots[slot];
                    if (slotData.instanceId != kInvalidId || !slotData.visual)
                    {
                        continue;
                    }

                    const uint32_t instanceId = nextInstanceId_++;
                    slotData.instanceId = instanceId;
                    instances_[instanceId] = {poolId, static_cast<uint32_t>(slot)};

                    if (slotData.tintMaterialId < scene_->Materials().size())
                    {
                        scene_->Materials()[slotData.tintMaterialId].gpuMaterial_.Diffuse =
                            glm::vec4(tint, 1.0f);
                    }
                    slotData.visual->PlayClip(pool->asset.FindClip("idle") != nullptr ? "idle" : "", 0.0f);
                    slotData.visual->SetWorldTransform(position, yawRadians);
                    return instanceId;
                }

                SPDLOG_WARN("[rig] pool {} is full ({} slots)", poolId, pool->slots.size());
                return kInvalidId;
            }

            void Release(uint32_t instanceId) override
            {
                if (FSlot* slot = FindSlot(instanceId); slot != nullptr)
                {
                    slot->instanceId = kInvalidId;
                    if (slot->visual)
                    {
                        slot->visual->SetVisible(false);
                    }
                }
                instances_.erase(instanceId);
            }

            bool IsAlive(uint32_t instanceId) const override
            {
                return instances_.find(instanceId) != instances_.end();
            }

            void SetTransform(uint32_t instanceId, const glm::vec3& position, float yawRadians) override
            {
                if (FSlot* slot = FindSlot(instanceId); slot != nullptr && slot->visual)
                {
                    slot->visual->SetWorldTransform(position, yawRadians);
                }
            }

            void SetVisible(uint32_t instanceId, bool visible) override
            {
                if (FSlot* slot = FindSlot(instanceId); slot != nullptr && slot->visual)
                {
                    slot->visual->SetVisible(visible);
                }
            }

            void PlayClip(uint32_t instanceId, const std::string& clip, float fadeSeconds) override
            {
                if (FSlot* slot = FindSlot(instanceId); slot != nullptr && slot->visual)
                {
                    slot->visual->PlayClip(clip, fadeSeconds);
                }
            }

            void SetPlaySpeed(uint32_t instanceId, float speed) override
            {
                if (FSlot* slot = FindSlot(instanceId); slot != nullptr && slot->visual)
                {
                    slot->visual->SetPlaySpeed(speed);
                }
            }

            int32_t GetBoneNodeId(uint32_t instanceId, const std::string& boneName) const override
            {
                const FSlot* slot = FindSlot(instanceId);
                return slot != nullptr && slot->visual ? slot->visual->BoneNodeId(boneName) : -1;
            }

            int32_t GetRootNodeId(uint32_t instanceId) const override
            {
                const FSlot* slot = FindSlot(instanceId);
                return slot != nullptr && slot->visual ? slot->visual->RootNodeId() : -1;
            }

            bool HasClip(uint32_t poolId, const std::string& clip) const override
            {
                const FPool* pool = FindPool(poolId);
                return pool != nullptr && pool->asset.FindClip(clip) != nullptr;
            }

        private:
            struct FSlot
            {
                std::vector<uint32_t> partModelIds;
                uint32_t tintMaterialId = 0;
                std::unique_ptr<Sim::FScadRigVisual> visual;
                /// kInvalidId while free. Also the free-list: there is no separate one to drift.
                uint32_t instanceId = kInvalidId;
            };

            struct FPool
            {
                std::string path;
                Assets::FRigAsset asset;
                std::vector<std::array<uint32_t, 16>> baseMaterials;
                std::vector<FSlot> slots;
            };

            struct FInstanceRef
            {
                uint32_t poolId = kInvalidId;
                uint32_t slot = 0;
            };

            FPool* FindPool(uint32_t poolId)
            {
                return poolId >= 1 && poolId <= pools_.size() ? &pools_[poolId - 1] : nullptr;
            }
            const FPool* FindPool(uint32_t poolId) const
            {
                return poolId >= 1 && poolId <= pools_.size() ? &pools_[poolId - 1] : nullptr;
            }

            FSlot* FindSlot(uint32_t instanceId)
            {
                return const_cast<FSlot*>(std::as_const(*this).FindSlot(instanceId));
            }

            const FSlot* FindSlot(uint32_t instanceId) const
            {
                const auto it = instances_.find(instanceId);
                if (it == instances_.end())
                {
                    return nullptr;
                }
                const FPool* pool = FindPool(it->second.poolId);
                if (pool == nullptr || it->second.slot >= pool->slots.size())
                {
                    return nullptr;
                }
                return &pool->slots[it->second.slot];
            }

            std::vector<FPool> pools_;
            std::unordered_map<uint32_t, FInstanceRef> instances_;
            Assets::Scene* scene_ = nullptr;
            /// Never reused: a stale id from a released character has to read as dead, not as
            /// whoever took its slot.
            uint32_t nextInstanceId_ = 1;
        };
    }

    void Install(NextEngine& engine)
    {
        engine.SetRigFactory([]() -> std::unique_ptr<NextRig> { return std::make_unique<FRigSubsystem>(); });
    }
}
