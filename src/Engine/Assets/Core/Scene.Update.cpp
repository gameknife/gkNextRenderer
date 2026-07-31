// Scene per-frame update: tick, node/material updates, GPU-driven culling
// buffers and HDR SH refresh. Split from Scene.cpp; same class, separate TU.
#include "Engine/Assets/Core/Scene.hpp"
#include <glm/detail/type_half.hpp>
#include <meshoptimizer.h>
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/LightObject.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Utilities/Exception.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <entt/meta/factory.hpp>
#include <limits>

namespace Assets
{
    std::vector<LightObject> Scene::ResolveActiveLights() const
    {
        std::vector<LightObject> resolvedLights;
        resolvedLights.reserve(std::min<size_t>(lights_.size(), kMaxLightCount));
        for (const LightObject& light : lights_)
        {
            resolvedLights.push_back(light);
            if (resolvedLights.size() == kMaxLightCount)
            {
                return resolvedLights;
            }
        }

        for (const auto& node : nodes_)
        {
            const auto* lightComponent = node ? node->GetComponentPtr<Runtime::LightComponent>() : nullptr;
            if (!lightComponent || !lightComponent->GetEnabled())
            {
                continue;
            }
            if (const auto* render = node->GetRenderComponent();
                render && !render->GetVisible())
            {
                continue;
            }
            for (const LightObject& localLight : lightComponent->Lights())
            {
                resolvedLights.push_back(LightObjects::Transform(localLight, node->WorldTransform()));
                if (resolvedLights.size() == kMaxLightCount)
                {
                    return resolvedLights;
                }
            }
        }
        return resolvedLights;
    }

    void Scene::UpdateLights()
    {
        std::vector<LightObject> uploadLights = ResolveActiveLights();
        lightCount_ = static_cast<uint32_t>(uploadLights.size());

        // Light-set signature (count + type/material sequence, FNV-1a): a change means stored
        // light indices change meaning, so index-holding consumers must drop history.
        uint64_t signature = 1469598103934665603ull;
        const auto mix = [&signature](uint64_t v)
        {
            signature ^= v;
            signature *= 1099511628211ull;
        };
        mix(uploadLights.size());
        for (const LightObject& light : uploadLights)
        {
            mix(light.lightType);
            mix(light.lightMatIdx);
        }
        if (signature != lightsSignature_)
        {
            lightsSignature_ = signature;
            ++lightsGeneration_;
        }

        if (!lightBufferMemory_ || lightCount_ == 0)
        {
            return;
        }
        std::vector<float> weights(lightCount_, 0.0f);
        float totalWeight = 0.0f;
        for (uint32_t i = 0; i < lightCount_; ++i)
        {
            const LightObject& light = uploadLights[i];
            if (light.lightMatIdx >= materials_.size())
            {
                continue;
            }
            const Material& material = materials_[light.lightMatIdx].gpuMaterial_;
            if (material.MaterialModel != Material::Enum::DiffuseLight)
            {
                continue;
            }
            const glm::vec3 radiance = glm::max(glm::vec3(material.Diffuse), glm::vec3(0.0f));
            const float luminance = glm::dot(radiance, glm::vec3(0.2126f, 0.7152f, 0.0722f));
            const float sampleMeasure = light.lightType == LightTypePoint
                ? 1.0f
                : (light.lightType == LightTypeArea ? std::max(light.normal_area.w, 0.0f) : 0.0f);
            weights[i] = luminance * sampleMeasure;
            totalWeight += weights[i];
        }

        float cdf = 0.0f;
        for (uint32_t i = 0; i < lightCount_; ++i)
        {
            const float pdf = totalWeight > 0.0f ? weights[i] / totalWeight : 1.0f / float(lightCount_);
            cdf += pdf;
            uploadLights[i].reserved1 = std::bit_cast<uint32_t>(i + 1 == lightCount_ ? 1.0f : cdf);
            uploadLights[i].reserved2 = std::bit_cast<uint32_t>(pdf);
        }

        void* data = lightBufferMemory_->Map(0, sizeof(LightObject) * lightCount_);
        std::memcpy(data, uploadLights.data(), sizeof(LightObject) * lightCount_);
        lightBufferMemory_->Unmap();
    }

    void Scene::DrawAreaLights() const
    {
        constexpr glm::vec4 activeColor(1.0f, 0.72f, 0.08f, 1.0f);
        constexpr glm::vec4 inactiveColor(1.0f, 0.18f, 0.12f, 1.0f);
        constexpr glm::vec4 normalColor(0.08f, 0.9f, 1.0f, 1.0f);

        for (const LightObject& light : ResolveActiveLights())
        {
            const bool isPoint = light.lightType == LightTypePoint;
            const glm::vec3 p0(light.p0);
            const glm::vec3 p1(light.p1);
            const glm::vec3 p3(light.p3);
            const glm::vec3 p2 = p1 + p3 - p0;
            const bool active = light.lightMatIdx < materials_.size() &&
                materials_[light.lightMatIdx].gpuMaterial_.MaterialModel == Material::Enum::DiffuseLight &&
                glm::any(glm::greaterThan(glm::vec3(materials_[light.lightMatIdx].gpuMaterial_.Diffuse),
                                          glm::vec3(0.0f))) &&
                (isPoint || light.normal_area.w > 0.0f);
            const glm::vec4 outlineColor = active ? activeColor : inactiveColor;

            if (isPoint)
            {
                Runtime::EngineHelper::DrawAuxPoint(p0, outlineColor, 7.0f, 0, false);
                continue;
            }

            Runtime::EngineHelper::DrawAuxLine(p0, p1, outlineColor, 2.5f, false);
            Runtime::EngineHelper::DrawAuxLine(p1, p2, outlineColor, 2.5f, false);
            Runtime::EngineHelper::DrawAuxLine(p2, p3, outlineColor, 2.5f, false);
            Runtime::EngineHelper::DrawAuxLine(p3, p0, outlineColor, 2.5f, false);

            const glm::vec3 center = (p0 + p1 + p2 + p3) * 0.25f;
            Runtime::EngineHelper::DrawAuxPoint(center, outlineColor, 4.0f, 0, false);

            const glm::vec3 normal(light.normal_area);
            const float normalLengthSquared = glm::dot(normal, normal);
            if (normalLengthSquared > 1.0e-8f)
            {
                const float markerLength = std::clamp(std::sqrt(std::max(light.normal_area.w, 0.0f)) * 0.5f,
                                                      0.2f, 2.0f);
                const glm::vec3 normalEnd = center + normal * glm::inversesqrt(normalLengthSquared) * markerLength;
                Runtime::EngineHelper::DrawAuxLine(center, normalEnd, normalColor, 2.0f, false);
                Runtime::EngineHelper::DrawAuxPoint(normalEnd, normalColor, 3.0f, 0, false);
            }
        }
    }

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
                            if (auto* renderComponent = node->GetRenderComponent())
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
                    if (track.Target_ == AnimationTrack::Target::Environment)
                    {
                        track.Sample(track.Time_, GetEnvSettings());
                        MarkDirty();
                        continue;
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
                                    if (NextPhysics* physics = NextEngine::GetInstance()->GetPhysicsEngine())
                                    {
                                        physics->MoveKinematicBody(
                                            bodyID, n->WorldTranslation(), n->WorldRotation(), 0.01f);
                                    }
                                }
                            }

                            for (auto& child : n->Children())
                            {
                                UpdatePhysicsBodyRecursive(child.get());
                            }
                        };
                        UpdatePhysicsBodyRecursive(node);

                    }
                }

                if (HasCameraAnimation())
                {
                    if (Node* cameraNode = GetNode(renderCamera_.NodeName_))
                    {
                        const glm::vec3 translation = cameraNode->WorldTranslation();
                        const glm::quat rotation = cameraNode->WorldRotation();
                        overrideModelView = glm::lookAtRH(
                            translation,
                            translation + rotation * glm::vec3(0.0f, 0.0f, -1.0f),
                            rotation * glm::vec3(0.0f, 1.0f, 0.0f));
                        renderCamera_.ModelView = overrideModelView;
                        requestOverrideModelView = true;
                    }
                }
            }
        }

        if (NextEngine::GetInstance()->GetShowFlags().DebugDraw_BoundingBox)
        {
            for (auto& node : nodes_)
            {
                auto* render = node->GetRenderComponent();
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

        NextEngine::GetInstance()->SetProgressiveRendering(false);
    }

    void Scene::StartUpdateNodes()
    {
        UpdateLights();
        if (NextEngine::GetInstance()->GetShowFlags().DebugDraw_AreaLights)
        {
            DrawAreaLights();
        }
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
        
        UpdateNodesGpuDriven();
    }

    bool Scene::EndUpdateNodes()
    {
        if (!nodeProxyUpdatePending_)
        {
            return false;
        }

        Tasks::TaskCoordinator::GetInstance()->WaitForNamedTask(Tasks::ENamedTaskThread::SCENE_UPDATE);
        nodeProxyUpdatePending_ = false;
        nodeProxies.swap(nodeProxiesBackup);
        std::swap(indirectDrawBatchCount_, indirectDrawBatchCountBackup_);
        needUpdateTLAS = true;
        return true;
    }

    bool Scene::GPUUpdateNodes()
    {
        if (!needUpdateTLAS)
        {
            return false;
        }

        if (!nodeProxiesBackup.empty())
        {
            SCOPED_CPU_TIMER("upload nodeproxy");
            NodeProxy* data = reinterpret_cast<NodeProxy*>(
                sceneDynamicBufferMemory_->Map(
                    Assets::GPU_SCENE_DYNAMIC_NODES_OFFSET, sizeof(NodeProxy) * nodeProxiesBackup.size()));
            std::memcpy(data, nodeProxiesBackup.data(), nodeProxiesBackup.size() * sizeof(NodeProxy));
            sceneDynamicBufferMemory_->Unmap();
        }

        needUpdateTLAS = false;
        return true;
    }
    
    void Scene::SyncUpdateScene()
    {
        StartUpdateNodes();
        EndUpdateNodes();
        GPUUpdateNodes();
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

                nodeProxies.clear();
                indirectDrawBatchCount_ = 0;

                auto sceneNodesUpdateTask =
            [this](Tasks::ResTask& task)
            {
                uint64_t expandedTriangleCapacity = 0;
                uint32_t currentJointOffset = 0;
                for (auto& node : nodes_)
                {
                    // record all
                    auto* render = node->GetRenderComponent();
                    if (render && render->IsDrawable())
                    {
                        if (node->TickVelocity())
                        {
                            sceneDirty_ = true;
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
                                uint32_t encodedModelSection = 0;
                                if (!TryEncodeModelSection(modelId, section, encodedModelSection) ||
                                    encodedModelSection >= offsets_.size())
                                {
                                    SPDLOG_ERROR("Skipping model {} section {}: invalid encoded offset (offset count {})",
                                                 modelId, section, offsets_.size());
                                    continue;
                                }
                                expandedTriangleCapacity += offsets_[encodedModelSection].indexCount / 3u;

                                NodeProxy proxy;
                                node->GetNodeProxy(proxy);
                                proxy.modelId = encodedModelSection;
                                proxy.excludeFromAS = section == 0 ? 0 : 1;
                                proxy.reserved1 = selectedBit;
                                proxy.reserved2 = stateBits;
                                proxy.jointMatrixOffset = nodeJointOffset;
                                nodeProxies.emplace_back(std::move(proxy));
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
            };
                
                nodeProxyUpdatePending_ = true;
                Tasks::TaskCoordinator::GetInstance()->AddNamedTask(
                    Tasks::ENamedTaskThread::SCENE_UPDATE, sceneNodesUpdateTask);
            }
            return true;
        }
        return false;
    }
}
