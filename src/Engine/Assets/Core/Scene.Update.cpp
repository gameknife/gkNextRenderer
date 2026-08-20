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
#include "Engine/Vulkan/Device.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/Exception.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <entt/meta/factory.hpp>
#include <limits>

namespace Assets
{
    void Scene::BindNode(Node& node)
    {
        node.scene_ = this;

        for (const auto& component : node.GetComponents())
        {
            if (component)
            {
                OnNodeComponentChanged(node, component->GetTypeId(), component.get());
            }
        }
    }

    void Scene::UnbindNode(Node& node)
    {
        for (const auto& component : node.GetComponents())
        {
            if (component)
            {
                UnregisterComponent(node, component->GetTypeId());
            }
        }
        node.scene_ = nullptr;
    }

    void Scene::OnNodeComponentChanged(Node& node, entt::id_type componentTypeId, Component* component)
    {
        if (component)
        {
            RegisterComponent(node, *component);
        }
        else
        {
            UnregisterComponent(node, componentTypeId);
        }

        if (componentTypeId == ComponentTypeId<Runtime::SkinnedMeshComponent>() && component)
        {
            // A replacement needs a fresh joint-matrix range just like a newly attached skin.
            RegisterSkinComponent(static_cast<Runtime::SkinnedMeshComponent&>(*component));
        }

        if (componentTypeId == ComponentTypeId<Runtime::EnvironmentComponent>())
        {
            if (component)
            {
                if (environmentComponent_ == nullptr || environmentComponent_->GetOwner() == &node)
                {
                    environmentComponent_ = static_cast<Runtime::EnvironmentComponent*>(component);
                }
            }
            else if (environmentComponent_ && environmentComponent_->GetOwner() == &node)
            {
                RefreshEnvironmentComponentCache();
            }
        }
    }

    void Scene::RegisterComponent(Node& node, Component& component)
    {
        const entt::id_type componentTypeId = component.GetTypeId();
        const std::string typeName(component.GetTypeName());
        auto& bucket = componentBuckets_[componentTypeId];
        if (!bucket.typeName.empty() && bucket.typeName != typeName)
        {
            Throw(std::logic_error(fmt::format(
                "Component type id collision between '{}' and '{}'", bucket.typeName, typeName)));
        }
        bucket.typeName = typeName;

        const auto [nameIt, nameInserted] = componentTypeByName_.try_emplace(typeName, componentTypeId);
        if (!nameInserted && nameIt->second != componentTypeId)
        {
            Throw(std::logic_error(fmt::format(
                "Component type name '{}' is registered with multiple type ids", typeName)));
        }

        const auto [slotIt, inserted] = bucket.slotByOwner.try_emplace(&node, bucket.components.size());
        if (inserted)
        {
            bucket.components.push_back(&component);
        }
        else
        {
            bucket.components[slotIt->second] = &component;
        }
    }

    void Scene::UnregisterComponent(Node& node, entt::id_type componentTypeId)
    {
        const auto bucketIt = componentBuckets_.find(componentTypeId);
        if (bucketIt == componentBuckets_.end())
        {
            return;
        }

        ComponentBucket& bucket = bucketIt->second;
        const auto slotIt = bucket.slotByOwner.find(&node);
        if (slotIt == bucket.slotByOwner.end())
        {
            return;
        }

        const size_t removedSlot = slotIt->second;
        const size_t lastSlot = bucket.components.size() - 1;
        if (removedSlot != lastSlot)
        {
            Component* movedComponent = bucket.components[lastSlot];
            bucket.components[removedSlot] = movedComponent;
            bucket.slotByOwner[movedComponent->GetOwner()] = removedSlot;
        }
        bucket.components.pop_back();
        bucket.slotByOwner.erase(slotIt);
    }

    std::span<Component* const> Scene::GetComponentsByType(entt::id_type componentTypeId) const
    {
        const auto bucketIt = componentBuckets_.find(componentTypeId);
        if (bucketIt == componentBuckets_.end())
        {
            return {};
        }
        return bucketIt->second.components;
    }

    void Scene::RegisterSkinComponent(Runtime::SkinnedMeshComponent& component)
    {
        const uint32_t jointCount = static_cast<uint32_t>(component.GetJointMatrices().size());
        component.SetJointMatrixOffset(allocatedJointCount_);
        allocatedJointCount_ += jointCount;
        jointMatrixUploadDirty_ = true;
        sceneDirty_ = true;

        EnsureJointMatrixCapacity();

        if (const Node* owner = component.GetOwner())
        {
            if (const auto* render = owner->GetComponent<Runtime::RenderComponent>();
                render && render->GetModelId() != -1)
            {
                RequestSkinUpdate(render->GetModelId());
            }
        }
    }

    void Scene::EnsureJointMatrixCapacity()
    {
        if (allocatedJointCount_ == 0 || allocatedJointCount_ <= jointMatrixCapacity_)
        {
            return;
        }

        uint32_t newCapacity = std::max(allocatedJointCount_, std::max(64u, jointMatrixCapacity_ * 2u));
        if (jointMatrixBuffer_)
        {
            commandPool_->Device().WaitIdle();
        }

        const VkBufferUsageFlags flags =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            *commandPool_, "JointMatrices", flags,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            static_cast<size_t>(newCapacity) * sizeof(glm::mat4),
            jointMatrixBuffer_, jointMatrixBufferMemory_);
        jointMatrixCapacity_ = newCapacity;
        jointMatrixUploadDirty_ = true;
    }

    void Scene::RequestSkinUpdate(uint32_t modelId)
    {
        if (GetModel(modelId) == nullptr)
        {
            SPDLOG_WARN("Ignoring skin update for invalid model {}", modelId);
            return;
        }
        if (std::find(skinUpdateRequests_.begin(), skinUpdateRequests_.end(), modelId) ==
            skinUpdateRequests_.end())
        {
            skinUpdateRequests_.push_back(modelId);
        }
        jointMatrixUploadDirty_ = true;
    }

    void Scene::ClearSkinUpdateRequests()
    {
        skinUpdateRequests_.clear();
    }

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

        for (const auto* lightComponent : Components<Runtime::LightComponent>())
        {
            const Node* node = lightComponent->GetOwner();
            if (!node || !lightComponent->GetEnabled())
            {
                continue;
            }
            if (const auto* render = node->GetComponent<Runtime::RenderComponent>();
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
        if (enableCpuAcceleration_)
        {
            cpuAccelerationStructure_->PollBVHBuild();
        }

        if (NextEngine::GetInstance()->GetUserSettings().TickAnimation)
        {
            {
                SCOPED_CPU_TIMER("skinned mesh");

                for (auto* skinnedMesh : Components<Runtime::SkinnedMeshComponent>())
                {
                    Node* node = skinnedMesh->GetOwner();
                    if (node)
                    {
                        skinnedMesh->Update(deltaSeconds);
                        if (NextEngine::GetInstance()->GetShowFlags().ShowDebugSkeleton)
                        {
                            skinnedMesh->DrawDebugSkeleton(node->WorldTransform());
                        }

                        if (skinnedMesh->IsPlaying())
                        {
                            MarkDirty();
                            if (auto* renderComponent = node->GetComponent<Runtime::RenderComponent>())
                            {
                                if (renderComponent->GetModelId() != -1)
                                {
                                    RequestSkinUpdate(renderComponent->GetModelId());
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
                            auto* phys = n->GetComponent<Runtime::PhysicsComponent>();
                            // Only kinematic bodies are valid MoveKinematic targets. Skipping other
                            // mobilities here also avoids a blocking CompleteTick per animated node,
                            // which would serialize the async physics tick.
                            if (phys && phys->GetMobility() == Runtime::ENodeMobility::Kinematic)
                            {
                                NextBodyID bodyID = phys->GetPhysicsBody();
                                if (!bodyID.IsInvalid())
                                {
                                    if (NextPhysics* physics = NextEngine::GetInstance()->GetPhysicsEngine())
                                    {
                                        physics->MoveKinematicBody(
                                            bodyID, n->WorldTranslation(), n->WorldRotation(), 1.0f / 60.0f);
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
            for (auto* render : Components<Runtime::RenderComponent>())
            {
                Node* node = render->GetOwner();
                if (!node || !render->GetVisible() || !render->IsDrawable())
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
                cpuAccelerationStructure_->Tick(
                    *this, ambientArenaBufferMemory_.get(), ambientArenaBufferMemory_.get(), ambientArenaBufferMemory_.get());
            }

            if (cpuBvhDirty_)
            {
                cpuAccelerationStructure_->RebuildBVHOnly(*this);
                cpuBvhDirty_ = false;
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

    void Scene::SyncPhysics()
    {
        for (auto* physics : Components<Runtime::PhysicsComponent>())
        {
            if (Node* node = physics->GetOwner())
            {
                node->SyncPhysics();
            }
        }
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
        if (!GetEnvSettings().HasSun)
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

        while (nodeProxyTasksRemaining_.load(std::memory_order_acquire) != 0)
        {
            // Scene-update tasks run immediately on their dedicated worker group. Do not tick the
            // coordinator here, because that can dispatch or complete unrelated shared-pool work.
            std::this_thread::yield();
        }

        const uint64_t expandedTriangleCount =
            nodeProxyExpandedTriangleCount_.load(std::memory_order_acquire);
        if (expandedTriangleCount > std::numeric_limits<uint32_t>::max())
        {
            throw std::overflow_error("GPU-driven scene triangle capacity exceeds uint32_t");
        }
        requiredGpuDrivenTriangleCapacity_ =
            std::max<uint32_t>(1u, static_cast<uint32_t>(expandedTriangleCount));
        if (nodeProxyMovingNodeDetected_.load(std::memory_order_acquire))
        {
            sceneDirty_ = true;
        }

        nodeProxyWorkItems_.clear();
        nodeProxyUpdatePending_ = false;
        nodeProxies.swap(nodeProxiesBackup);
        std::swap(indirectDrawBatchCount_, indirectDrawBatchCountBackup_);
        needUpdateTLAS = true;
        return true;
    }

    bool Scene::GPUUpdateNodes()
    {
        const bool updateNodeProxies = needUpdateTLAS;
        if (!updateNodeProxies && !jointMatrixUploadDirty_)
        {
            return false;
        }

        if (updateNodeProxies && !nodeProxiesBackup.empty())
        {
            SCOPED_CPU_TIMER("upload nodeproxy");
            if (nodeProxiesBackup.size() > kRenderProxyCapacity)
            {
                throw std::length_error(fmt::format(
                    "Scene contains {} render proxies, exceeding capacity {}",
                    nodeProxiesBackup.size(), kRenderProxyCapacity));
            }
            NodeProxy* data = reinterpret_cast<NodeProxy*>(
                sceneDynamicBufferMemory_->Map(
                    Assets::GPU_SCENE_DYNAMIC_NODES_OFFSET, sizeof(NodeProxy) * nodeProxiesBackup.size()));
            std::memcpy(data, nodeProxiesBackup.data(), nodeProxiesBackup.size() * sizeof(NodeProxy));
            sceneDynamicBufferMemory_->Unmap();
        }

        if (jointMatrixUploadDirty_ && jointMatrixBufferMemory_ && allocatedJointCount_ > 0)
        {
            SCOPED_CPU_TIMER("upload joint matrices");
            glm::mat4* data = static_cast<glm::mat4*>(jointMatrixBufferMemory_->Map(
                0, static_cast<size_t>(allocatedJointCount_) * sizeof(glm::mat4)));
            for (const auto* skinnedMesh : Components<Runtime::SkinnedMeshComponent>())
            {
                const auto& matrices = skinnedMesh->GetJointMatrices();
                std::memcpy(data + skinnedMesh->GetJointMatrixOffset(), matrices.data(),
                            matrices.size() * sizeof(glm::mat4));
            }
            jointMatrixBufferMemory_->Unmap();
            jointMatrixUploadDirty_ = false;
        }

        needUpdateTLAS = false;
        return updateNodeProxies;
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

                nodeProxyWorkItems_.clear();
                indirectDrawBatchCount_ = 0;

                // First pass stays on the main thread: determine the exact output range owned by
                // every node. Workers can then write directly into a resized vector without locks.
                uint64_t proxyCount = 0;
                const auto renderComponents = Components<Runtime::RenderComponent>();
                nodeProxyWorkItems_.reserve(renderComponents.size());
                for (auto* render : renderComponents)
                {
                    Node* node = render->GetOwner();
                    if (!node || !render->IsDrawable())
                    {
                        continue;
                    }

                    const uint32_t modelId = render->GetModelId();
                    const auto* model = GetModel(modelId);
                    if (!model)
                    {
                        continue;
                    }

                    uint32_t validSectionCount = 0;
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
                        ++validSectionCount;
                    }

                    if (validSectionCount > 0)
                    {
                        const uint64_t nextProxyCount = proxyCount + validSectionCount;
                        if (nextProxyCount > kRenderProxyCapacity)
                        {
                            throw std::length_error(fmt::format(
                                "Scene render proxy count exceeds capacity {} while expanding {} render components",
                                kRenderProxyCapacity, renderComponents.size()));
                        }
                        nodeProxyWorkItems_.push_back(
                            {node, modelId, static_cast<uint32_t>(proxyCount), validSectionCount});
                        proxyCount = nextProxyCount;
                    }
                }

                nodeProxies.clear();
                nodeProxies.resize(static_cast<size_t>(proxyCount));
                indirectDrawBatchCount_ = static_cast<uint32_t>(proxyCount);
                nodeProxyExpandedTriangleCount_.store(0, std::memory_order_relaxed);
                nodeProxyMovingNodeDetected_.store(false, std::memory_order_relaxed);

                const uint32_t availableCores = std::max(1u, std::thread::hardware_concurrency());
                const uint32_t requestedWorkerCount = std::max(1u, availableCores / 2u);
                const uint32_t taskCount = std::min<uint32_t>(
                    requestedWorkerCount, static_cast<uint32_t>(nodeProxyWorkItems_.size()));
                nodeProxyTasksRemaining_.store(taskCount, std::memory_order_release);
                nodeProxyUpdatePending_ = true;

                if (taskCount == 0)
                {
                    return true;
                }

                auto* taskCoordinator = Tasks::TaskCoordinator::GetInstance();
                for (uint32_t taskIndex = 0; taskIndex < taskCount; ++taskIndex)
                {
                    const uint32_t begin =
                        static_cast<uint32_t>(nodeProxyWorkItems_.size()) * taskIndex / taskCount;
                    const uint32_t end =
                        static_cast<uint32_t>(nodeProxyWorkItems_.size()) * (taskIndex + 1u) / taskCount;
                    taskCoordinator->AddNamedTask(
                        Tasks::ENamedTaskThread::SCENE_UPDATE,
                        [this, begin, end](Tasks::ResTask&)
                        {
                            uint64_t localExpandedTriangleCount = 0;
                            bool localMovingNodeDetected = false;

                            for (uint32_t itemIndex = begin; itemIndex < end; ++itemIndex)
                            {
                                const NodeProxyUpdateWorkItem& item = nodeProxyWorkItems_[itemIndex];
                                Node* node = item.node;
                                const Model* model = GetModel(item.modelId);
                                if (!node || !model)
                                {
                                    continue;
                                }

                                localMovingNodeDetected |= node->TickVelocity();

                                const auto* render = node->GetComponent<Runtime::RenderComponent>();
                                const uint32_t instanceId = node->GetInstanceId();
                                const uint32_t editableInstanceId = node->IsSceneReferenceInternal()
                                    ? node->GetSceneReferenceOwnerProxyId()
                                    : instanceId;
                                const uint32_t outlineFlags = render->GetOutlineFlags();
                                const uint32_t selectedBit =
                                    (selectionState_.IsSelected(editableInstanceId) ||
                                     (outlineFlags & Runtime::RenderOutlineFlags::selected) != 0u) ? 1u : 0u;
                                const uint32_t hoveredBit =
                                    (hoveredId_ == editableInstanceId ||
                                     (outlineFlags & Runtime::RenderOutlineFlags::hovered) != 0u) ? 1u : 0u;
                                const uint32_t lockedBit =
                                    (lockedIds_.find(editableInstanceId) != lockedIds_.end() ||
                                     (outlineFlags & Runtime::RenderOutlineFlags::locked) != 0u) ? 1u : 0u;
                                const uint32_t dangerBit =
                                    (outlineFlags & Runtime::RenderOutlineFlags::danger) != 0u ? 1u : 0u;
                                const uint32_t stateBits =
                                    hoveredBit | (lockedBit << 1u) | (dangerBit << 2u);

                                uint32_t nodeJointOffset = 0;
                                if (const auto* skinnedMesh =
                                        node->GetComponent<Runtime::SkinnedMeshComponent>())
                                {
                                    nodeJointOffset = skinnedMesh->GetJointMatrixOffset();
                                }

                                NodeProxy baseProxy;
                                node->GetNodeProxy(baseProxy);
                                uint32_t outputIndex = item.outputOffset;
                                for (uint32_t section = 0; section < model->SectionCount(); ++section)
                                {
                                    uint32_t encodedModelSection = 0;
                                    if (!TryEncodeModelSection(item.modelId, section, encodedModelSection) ||
                                        encodedModelSection >= offsets_.size())
                                    {
                                        continue;
                                    }

                                    NodeProxy proxy = baseProxy;
                                    proxy.modelId = encodedModelSection;
                                    proxy.excludeFromAS = section == 0 ? 0 : 1;
                                    proxy.reserved1 = selectedBit;
                                    proxy.reserved2 = stateBits;
                                    proxy.jointMatrixOffset = nodeJointOffset;
                                    nodeProxies[outputIndex++] = std::move(proxy);
                                    localExpandedTriangleCount +=
                                        offsets_[encodedModelSection].indexCount / 3u;
                                }
                            }

                            nodeProxyExpandedTriangleCount_.fetch_add(
                                localExpandedTriangleCount, std::memory_order_relaxed);
                            if (localMovingNodeDetected)
                            {
                                nodeProxyMovingNodeDetected_.store(true, std::memory_order_relaxed);
                            }
                            nodeProxyTasksRemaining_.fetch_sub(1, std::memory_order_acq_rel);
                        },
                        {},
                        "Scene node proxy update");
                }
            }
            return true;
        }
        return false;
    }
}
