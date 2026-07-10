// Scene per-frame update: tick, node/material updates, GPU-driven culling
// buffers and HDR SH refresh. Split from Scene.cpp; same class, separate TU.
#include "Engine/Assets/Core/Scene.hpp"
#include <glm/detail/type_half.hpp>
#include <meshoptimizer.h>
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Vulkan/BufferUtil.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Utilities/Exception.hpp"

#include <algorithm>
#include <cmath>
#include <entt/meta/factory.hpp>
#include <limits>

namespace Assets
{
    void Scene::Tick(float deltaSeconds)
    {
        if (NextEngine::GetInstance()->GetUserSettings().TickAnimation)
        {
            {
                SCOPED_CPU_TIMER("skinned mesh");

                for (auto& node : nodes_)
                {
                    if (auto* skinnedMesh = node->GetComponentPtr<Runtime::SkinnedMeshComponent>())
                    {
                        skinnedMesh->Update(deltaSeconds);
                        if (NextEngine::GetInstance()->GetShowFlags().ShowDebugSkeleton)
                        {
                            skinnedMesh->DrawDebugSkeleton(node->WorldTransform());
                        }

                        if (skinnedMesh->IsPlaying())
                        {
                            MarkDirty();
                            if (auto* renderComponent = node->GetComponentPtr<Runtime::RenderComponent>())
                            {
                                if (renderComponent->GetModelId() != -1)
                                {
                                    NextEngine::GetInstance()->GetRenderer().RequestSkinUpdate(
                                        renderComponent->GetModelId());
                                }
                            }
                        }
                    }
                }
            }

            {
                SCOPED_CPU_TIMER("track anims");

                float durationMax = 0;

                for (auto& track : tracks_)
                {
                    if (!track.Playing())
                        continue;
                    durationMax = glm::max(durationMax, track.Duration_);
                }

                for (auto& track : tracks_)
                {
                    if (!track.Playing())
                        continue;
                    track.Time_ += deltaSeconds * track.PlaySpeed_;
                    if (track.Time_ > durationMax)
                    {
                        track.PlaySpeed_ = -1.0f;
                    }
                    if (track.Time_ < 0.0f)
                    {
                        track.PlaySpeed_ = 1.0f;
                    }
                    Node* node = GetNode(track.NodeName_);
                    if (node)
                    {
                        glm::vec3 translation = node->Translation();
                        glm::quat rotation = node->Rotation();
                        glm::vec3 scaling = node->Scale();

                        track.Sample(track.Time_, translation, rotation, scaling);

                        node->SetTranslation(translation);
                        node->SetRotation(rotation);
                        node->SetScale(scaling);
                        node->RecalcTransform(true);

                        MarkDirty();

                        // to physicSys
                        std::function<void(Node*)> UpdatePhysicsBodyRecursive = [&](Node* n)
                        {
                            if (!n)
                                return;
                            auto* phys = n->GetComponentPtr<Runtime::PhysicsComponent>();
                            if (phys)
                            {
                                NextBodyID bodyID = phys->GetPhysicsBody();
                                if (!bodyID.IsInvalid())
                                {
                                    NextEngine::GetInstance()->GetPhysicsEngine()->MoveKinematicBody(
                                        bodyID, n->WorldTranslation(), n->WorldRotation(), 0.01f);
                                }
                            }

                            for (auto& child : n->Children())
                            {
                                UpdatePhysicsBodyRecursive(child.get());
                            }
                        };
                        UpdatePhysicsBodyRecursive(node);

                        // temporal if camera node, request override
                        if (node->GetName() == "Shot.BlueCar")
                        {
                            requestOverrideModelView = true;
                            overrideModelView = glm::lookAtRH(translation, translation + rotation * glm::vec3(0, 0, -1),
                                                              glm::vec3(0.0f, 1.0f, 0.0f));
                        }
                    }
                }
            }
        }

        if (NextEngine::GetInstance()->GetShowFlags().DebugDraw_BoundingBox)
        {
            for (auto& node : nodes_)
            {
                auto* render = node->GetComponentPtr<Runtime::RenderComponent>();
                if (!render || !render->GetVisible() || !render->IsDrawable())
                {
                    continue;
                }

                const Model* model = GetModel(render->GetModelId());
                if (!model)
                {
                    continue;
                }

                glm::vec3 localaabbMin = model->GetLocalAABBMin();
                glm::vec3 localaabbMax = model->GetLocalAABBMax();

                const auto& worldMtx = node->WorldTransform();
                glm::vec3 corners[8];
                corners[0] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMin.y, localaabbMin.z, 1.0f));
                corners[1] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMin.y, localaabbMin.z, 1.0f));
                corners[2] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMax.y, localaabbMin.z, 1.0f));
                corners[3] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMax.y, localaabbMin.z, 1.0f));
                corners[4] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMin.y, localaabbMax.z, 1.0f));
                corners[5] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMin.y, localaabbMax.z, 1.0f));
                corners[6] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMax.y, localaabbMax.z, 1.0f));
                corners[7] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMax.y, localaabbMax.z, 1.0f));

                glm::vec3 worldAABBMin = corners[0];
                glm::vec3 worldAABBMax = corners[0];
                for (int i = 1; i < 8; ++i)
                {
                    worldAABBMin = glm::min(worldAABBMin, corners[i]);
                    worldAABBMax = glm::max(worldAABBMax, corners[i]);
                }

                Runtime::EngineHelper::DrawAuxBox(worldAABBMin, worldAABBMax, glm::vec4(0.2f, 0.8f, 1.0f, 1.0f), 1.5f);
            }
        }

        for (const auto& splat : gaussianSplats_)
        {
            if (!IsSelected(splat.nodeInstanceId) &&
                !NextEngine::GetInstance()->GetShowFlags().DebugDraw_BoundingBox)
            {
                continue;
            }

            glm::vec3 boundsMin;
            glm::vec3 boundsMax;
            if (GetGaussianSplatWorldBounds(splat.nodeInstanceId, boundsMin, boundsMax))
            {
                const glm::vec4 color = IsSelected(splat.nodeInstanceId)
                    ? glm::vec4(1.0f, 0.65f, 0.1f, 1.0f)
                    : glm::vec4(0.2f, 0.8f, 1.0f, 1.0f);
                Runtime::EngineHelper::DrawAuxBox(boundsMin, boundsMax, color, 2.0f);
            }
        }

        if (NextEngine::GetInstance()->GetShowFlags().DebugDraw_PhysicsBodies)
        {
            if (NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine())
            {
                physicsEngine->DrawDebugBodies();
            }
        }

        if (enableCpuAcceleration_ && NextEngine::GetInstance()->GetTotalFrames() % 30 == 0)
        {
            auto& renderer = NextEngine::GetInstance()->GetRenderer();
            // Voxelize whenever any renderer needs the voxel SDF, decoupled from ambient-cube baking.
            const bool shouldUpdateVoxel =
                renderer.ActiveRendererRequirements().NeedsVoxelGeometry() && !renderer.ShouldSkipAmbientCubeUpdates();

            if (shouldUpdateVoxel && ambientArenaBufferMemory_)
            {
                const bool voxelUploadCompleted = cpuAccelerationStructure_.Tick(
                    *this, ambientArenaBufferMemory_.get(), ambientArenaBufferMemory_.get(), ambientArenaBufferMemory_.get());

                if (sceneDirtyForCpuAS_ && !cpuAccelerationStructure_.HasPendingWork())
                {
                    if (cpuAccelerationStructure_.AsyncProcessFull(*this, ambientArenaBufferMemory_.get(), true))
                    {
                        sceneDirtyForCpuAS_ = false;
                    }
                }
            }
            else if (sceneDirtyForCpuAS_)
            {
                cpuAccelerationStructure_.RebuildBVHOnly(*this);
                sceneDirtyForCpuAS_ = false;
            }
        }
    }

    void Scene::UpdateAllMaterials()
    {
        if (materials_.empty())
            return;

        gpuMaterials_.clear();
        for (auto& material : materials_)
        {
            gpuMaterials_.push_back(material.gpuMaterial_);
        }

        Material* data = reinterpret_cast<Material*>(sceneDynamicBufferMemory_->Map(
            Assets::GPU_SCENE_DYNAMIC_MATERIALS_OFFSET, sizeof(Material) * gpuMaterials_.size()));
        std::memcpy(data, gpuMaterials_.data(), gpuMaterials_.size() * sizeof(Material));
        sceneDynamicBufferMemory_->Unmap();

        NextEngine::GetInstance()->SetProgressiveRendering(false, false);
    }

    bool Scene::UpdateNodes()
    {
        GPUDrivenStat zero{};
        // read back gpu driven stats
        const auto data = sceneDynamicBufferMemory_->Map(
            Assets::GPU_SCENE_DYNAMIC_GPU_DRIVEN_STATS_OFFSET,
            sizeof(Assets::GPUDrivenStat) * (1 + Assets::Scene::kSunShadowCascadeCount));
        // download
        GPUDrivenStat* gpuData = static_cast<GPUDrivenStat*>(data);
        std::memcpy(&gpuDrivenStat_, gpuData, sizeof(GPUDrivenStat));
        std::memcpy(shadowGpuDrivenStats_.data(), gpuData + 1,
                    sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount);
        gpuData[0] = zero;
        if (!HasSun())
        {
            std::fill(shadowGpuDrivenStats_.begin(), shadowGpuDrivenStats_.end(), zero);
            std::fill_n(gpuData + 1, Assets::Scene::kSunShadowCascadeCount, zero);
        }
        sceneDynamicBufferMemory_->Unmap();


        // if mat dirty, update
        if (materialDirty_)
        {
            materialDirty_ = false;
            UpdateAllMaterials();
        }

        return UpdateNodesGpuDriven();
    }

    void Scene::UpdateHDRSH()
    {
        auto& shData = GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics();
        if (shData.size() > 0)
        {
            SphericalHarmonics* data = reinterpret_cast<SphericalHarmonics*>(
                sceneDynamicBufferMemory_->Map(
                    Assets::GPU_SCENE_DYNAMIC_HDRSHS_OFFSET, sizeof(SphericalHarmonics) * shData.size()));
            std::memcpy(data, shData.data(), shData.size() * sizeof(SphericalHarmonics));
            sceneDynamicBufferMemory_->Unmap();
        }
    }

    bool Scene::UpdateNodesGpuDriven()
    {
        // do always, no flicker now
        if (sceneDirty_)
        {
            sceneDirty_ = false;
            {
                SCOPED_CPU_TIMER("update nodeproxy");

                nodeProxys.clear();
                indirectDrawBatchCount_ = 0;

                uint64_t expandedTriangleCapacity = 0;
                uint32_t currentJointOffset = 0;
                for (auto& node : nodes_)
                {
                    // record all
                    auto* render = node->GetComponentPtr<Runtime::RenderComponent>();
                    if (render && render->IsDrawable())
                    {
                        glm::mat4 combined;
                        if (node->TickVelocity(combined))
                        {
                            sceneDirty_ = true;
                            // MarkDirty();
                        }

                        auto model = GetModel(render->GetModelId());
                        if (model)
                        {
                            const uint32_t modelId = render->GetModelId();
                            uint32_t nodeJointOffset = 0;
                            const uint32_t instanceId = node->GetInstanceId();
                            const uint32_t editableInstanceId = node->IsSceneReferenceInternal()
                                ? node->GetSceneReferenceOwnerProxyId()
                                : instanceId;
                            const uint32_t outlineFlags = render->GetOutlineFlags();
                            const uint32_t selectedBit =
                                (selectionState_.IsSelected(editableInstanceId) ||
                                 (outlineFlags & Runtime::RenderOutlineFlags::selected) != 0u) ? 1u : 0u;
                            const uint32_t hoveredBit =
                                (hoveredId_ == editableInstanceId || (outlineFlags & Runtime::RenderOutlineFlags::hovered) != 0u) ? 1u : 0u;
                            const uint32_t lockedBit =
                                (lockedIds_.find(editableInstanceId) != lockedIds_.end() ||
                                 (outlineFlags & Runtime::RenderOutlineFlags::locked) != 0u) ? 1u : 0u;
                            const uint32_t dangerBit =
                                ((outlineFlags & Runtime::RenderOutlineFlags::danger) != 0u) ? 1u : 0u;
                            const uint32_t stateBits = hoveredBit | (lockedBit << 1u) | (dangerBit << 2u);
                            if (auto* skinnedMesh = node->GetComponentPtr<Runtime::SkinnedMeshComponent>())
                            {
                                nodeJointOffset = currentJointOffset;
                                currentJointOffset += (uint32_t)skinnedMesh->GetJointMatrices().size();
                            }

                            for (uint32_t section = 0; section < model->SectionCount(); ++section)
                            {
                                expandedTriangleCapacity += offsets_[modelId * 10 + section].indexCount / 3u;

                                NodeProxy proxy = node->GetNodeProxy();
                                proxy.combinedPrevTS = combined;
                                proxy.modelId = modelId * 10 + section;
                                proxy.nort = section == 0 ? 0 : 1;
                                proxy.reserved1 = selectedBit;
                                proxy.reserved2 = stateBits;
                                proxy.jointMatrixOffset = nodeJointOffset;
                                nodeProxys.push_back(proxy);
                                indirectDrawBatchCount_++;
                            }
                        }
                    }
                }

                if (expandedTriangleCapacity > std::numeric_limits<uint32_t>::max())
                {
                    throw std::overflow_error("GPU-driven scene triangle capacity exceeds uint32_t");
                }
                requiredGpuDrivenTriangleCapacity_ =
                    std::max<uint32_t>(1u, static_cast<uint32_t>(expandedTriangleCapacity));
            }

            if (!nodeProxys.empty())
            {
                {
                    SCOPED_CPU_TIMER("upload nodeproxy");
                    NodeProxy* data = reinterpret_cast<NodeProxy*>(
                        sceneDynamicBufferMemory_->Map(
                            Assets::GPU_SCENE_DYNAMIC_NODES_OFFSET, sizeof(NodeProxy) * nodeProxys.size()));
                    std::memcpy(data, nodeProxys.data(), nodeProxys.size() * sizeof(NodeProxy));
                    sceneDynamicBufferMemory_->Unmap();
                }
            }
            return true;
        }
        return false;
    }

}
