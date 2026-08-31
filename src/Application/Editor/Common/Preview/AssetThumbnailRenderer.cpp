#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Editor/Common/Preview/AssetThumbnailRenderer.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Modules/ScadLoader/FScadLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Vulkan
{
    RenderView* AssetThumbnailRenderer::ThumbnailView() const
    {
        return renderer_.RenderViews().Resolve(thumbnailRenderView_);
    }

    RenderView* AssetThumbnailRenderer::MaterialPreviewView() const
    {
        return renderer_.RenderViews().Resolve(materialPreviewView_);
    }

    AssetThumbnailRenderer::AssetThumbnailRenderer(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
        thumbnailSlots_.resize(kThumbnailMaxSlots);
        materialPreview_.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.75f));
        materialPreview_.name_ = "__material_preview";
    }

    AssetThumbnailRenderer::~AssetThumbnailRenderer()
    {
        RenderViewResourceFactory factory(renderer_);
        factory.DestroyView(thumbnailRenderView_);
        factory.DestroyView(materialPreviewView_);
    }

    uint32_t AssetThumbnailRenderer::RequestMaterialThumbnail(
        const uint32_t materialIndex,
        const uint64_t materialHash)
    {
        return RequestThumbnail(EThumbnailKind::Material, materialIndex, materialHash);
    }

    uint32_t AssetThumbnailRenderer::RequestMaterialThumbnail(
        const uint32_t materialIndex,
        const Assets::FMaterial& material)
    {
        return RequestMaterialThumbnail(materialIndex, HashMaterialThumbnail(material));
    }

    uint32_t AssetThumbnailRenderer::RequestMeshThumbnail(const uint32_t modelIndex, const uint64_t modelHash)
    {
        return RequestThumbnail(EThumbnailKind::Mesh, modelIndex, modelHash);
    }

    uint32_t AssetThumbnailRenderer::RequestMeshThumbnail(
        const uint32_t modelIndex,
        const Assets::Model& model)
    {
        return RequestMeshThumbnail(modelIndex, HashMeshThumbnail(model));
    }

    uint32_t AssetThumbnailRenderer::RequestScadKitThumbnail(
        const uint32_t thumbnailIndex,
        const std::string& sourcePath,
        const uint64_t sourceHash)
    {
        if (sourcePath.empty())
        {
            return std::numeric_limits<uint32_t>::max();
        }
        return RequestThumbnail(EThumbnailKind::ScadKit, thumbnailIndex, sourceHash, sourcePath);
    }

    uint64_t AssetThumbnailRenderer::HashMaterialThumbnail(const Assets::FMaterial& material)
    {
        uint64_t hash = 1469598103934665603ull;
        const auto mixBytes = [&hash](const void* data, const size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };

        const Assets::Material& gpu = material.gpuMaterial_;
        mixBytes(&gpu.Diffuse, sizeof(gpu.Diffuse));
        mixBytes(&gpu.DiffuseTextureId, sizeof(gpu.DiffuseTextureId));
        mixBytes(&gpu.MRATextureId, sizeof(gpu.MRATextureId));
        mixBytes(&gpu.NormalTextureId, sizeof(gpu.NormalTextureId));
        mixBytes(&gpu.EmissiveTextureId, sizeof(gpu.EmissiveTextureId));
        mixBytes(&gpu.Fuzziness, sizeof(gpu.Fuzziness));
        mixBytes(&gpu.RefractionIndex, sizeof(gpu.RefractionIndex));
        mixBytes(&gpu.MaterialModel, sizeof(gpu.MaterialModel));
        mixBytes(&gpu.Metalness, sizeof(gpu.Metalness));
        return hash;
    }

    uint64_t AssetThumbnailRenderer::HashMeshThumbnail(const Assets::Model& model)
    {
        uint64_t hash = 1469598103934665603ull;
        const auto mixBytes = [&hash](const void* data, const size_t size)
        {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };

        const std::string& name = model.Name();
        mixBytes(name.data(), name.size());
        const uint32_t vertexCount = model.NumberOfVertices();
        const uint32_t indexCount = model.NumberOfIndices();
        const uint32_t sectionCount = model.SectionCount();
        const glm::vec3 aabbMin = model.GetLocalAABBMin();
        const glm::vec3 aabbMax = model.GetLocalAABBMax();
        mixBytes(&vertexCount, sizeof(vertexCount));
        mixBytes(&indexCount, sizeof(indexCount));
        mixBytes(&sectionCount, sizeof(sectionCount));
        mixBytes(&aabbMin, sizeof(aabbMin));
        mixBytes(&aabbMax, sizeof(aabbMax));

        const auto& vertices = model.CPUVertices();
        const auto& indices = model.CPUIndices();
        if (!vertices.empty())
        {
            const size_t sampleIndices[] = {0u, vertices.size() / 2u, vertices.size() - 1u};
            for (const size_t sampleIndex : sampleIndices)
            {
                mixBytes(&vertices[sampleIndex], sizeof(vertices[sampleIndex]));
            }
        }
        if (!indices.empty())
        {
            const size_t sampleIndices[] = {0u, indices.size() / 2u, indices.size() - 1u};
            for (const size_t sampleIndex : sampleIndices)
            {
                mixBytes(&indices[sampleIndex], sizeof(indices[sampleIndex]));
            }
        }
        return hash;
    }

    uint32_t AssetThumbnailRenderer::RequestThumbnail(
        const EThumbnailKind kind,
        const uint32_t assetIndex,
        const uint64_t assetHash,
        const std::string& sourcePath)
    {
        if (kThumbnailSampleSlotBase + kThumbnailMaxSlots > renderer_.BindlessProfile().sampledTextureSlots)
        {
            return std::numeric_limits<uint32_t>::max();
        }

        const FThumbnailKey key{kind, assetIndex};
        const uint64_t currentFrame = static_cast<uint64_t>(std::max(renderer_.FrameCount(), 0));
        uint32_t poolIndex = std::numeric_limits<uint32_t>::max();
        if (const auto found = thumbnailLookup_.find(key); found != thumbnailLookup_.end())
        {
            poolIndex = found->second;
        }
        else
        {
            poolIndex = AcquireThumbnailSlot(key, currentFrame);
            if (poolIndex == std::numeric_limits<uint32_t>::max())
            {
                return poolIndex;
            }
        }

        FThumbnailSlot& slot = thumbnailSlots_[poolIndex];
        slot.lastRequestedFrame = currentFrame;
        if (!sourcePath.empty())
        {
            slot.sourcePath = sourcePath;
        }

        if (slot.contentHash != assetHash)
        {
            slot.contentHash = assetHash;
            slot.ready = false;
            ++slot.generation;
        }

        if (slot.ready)
        {
            return kThumbnailSampleSlotBase + poolIndex;
        }

        QueueThumbnail(poolIndex);
        return std::numeric_limits<uint32_t>::max();
    }

    uint32_t AssetThumbnailRenderer::AcquireThumbnailSlot(
        const FThumbnailKey& key,
        const uint64_t currentFrame)
    {
        uint32_t selected = std::numeric_limits<uint32_t>::max();
        for (uint32_t index = 0; index < thumbnailSlots_.size(); ++index)
        {
            if (!thumbnailSlots_[index].occupied)
            {
                selected = index;
                break;
            }
        }

        if (selected == std::numeric_limits<uint32_t>::max())
        {
            uint64_t oldestFrame = std::numeric_limits<uint64_t>::max();
            for (uint32_t index = 0; index < thumbnailSlots_.size(); ++index)
            {
                const FThumbnailSlot& candidate = thumbnailSlots_[index];
                // A slot requested this frame may already be encoded in ImGui draw data. Never
                // repurpose it until a later frame; if all 512 are visible, the newcomer waits.
                if (candidate.lastRequestedFrame < currentFrame && candidate.lastRequestedFrame < oldestFrame)
                {
                    selected = index;
                    oldestFrame = candidate.lastRequestedFrame;
                }
            }
        }

        if (selected == std::numeric_limits<uint32_t>::max())
        {
            return selected;
        }

        FThumbnailSlot& slot = thumbnailSlots_[selected];
        if (slot.occupied)
        {
            thumbnailLookup_.erase(slot.key);
            std::erase(pendingThumbnails_, selected);
        }

        slot.key = key;
        slot.contentHash = 0;
        slot.lastRequestedFrame = currentFrame;
        ++slot.generation;
        slot.sourcePath.clear();
        slot.occupied = true;
        slot.ready = false;
        slot.pending = false;
        thumbnailLookup_.emplace(key, selected);
        return selected;
    }

    void AssetThumbnailRenderer::QueueThumbnail(const uint32_t poolIndex)
    {
        if (poolIndex >= thumbnailSlots_.size() || thumbnailSlots_[poolIndex].pending)
        {
            return;
        }
        thumbnailSlots_[poolIndex].pending = true;
        pendingThumbnails_.push_back(poolIndex);
    }

    void AssetThumbnailRenderer::SetEnabled(const bool enabled)
    {
        materialPreviewEnabled_ = enabled;
        if (!materialPreviewEnabled_)
        {
            if (RenderView* view = MaterialPreviewView()) view->SetSceneOverride(nullptr);
        }
    }

    void AssetThumbnailRenderer::SetRenderExtent(VkExtent2D extent)
    {
        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);
        if (materialPreviewExtent_.width == extent.width && materialPreviewExtent_.height == extent.height)
        {
            return;
        }

        materialPreviewExtent_ = extent;
        if (RenderView* view = MaterialPreviewView())
        {
            view->SetRenderExtent(materialPreviewExtent_);
            view->InvalidateTemporalHistory();
        }
    }

    void AssetThumbnailRenderer::SetPreviewMaterial(const Assets::FMaterial& material)
    {
        if (materialPreview_.name_ == "__material_preview" &&
            std::memcmp(&materialPreview_.gpuMaterial_, &material.gpuMaterial_, sizeof(Assets::Material)) == 0)
        {
            return;
        }
        materialPreview_ = material;
        materialPreview_.name_ = "__material_preview";
        materialPreviewDirty_ = true;
        if (RenderView* view = MaterialPreviewView())
        {
            view->InvalidateTemporalHistory();
        }
    }

    void AssetThumbnailRenderer::SetCameraOrbit(float yawRadians, float pitchRadians, float distance)
    {
        pitchRadians = std::clamp(pitchRadians, -1.2f, 1.2f);
        distance = std::clamp(distance, 1.8f, 8.0f);
        if (std::abs(materialPreviewYaw_ - yawRadians) < 0.0001f &&
            std::abs(materialPreviewPitch_ - pitchRadians) < 0.0001f &&
            std::abs(materialPreviewDistance_ - distance) < 0.0001f)
        {
            return;
        }

        materialPreviewYaw_ = yawRadians;
        materialPreviewPitch_ = pitchRadians;
        materialPreviewDistance_ = distance;
        if (materialPreviewScene_)
        {
            materialPreviewScene_->GetRenderCamera() = BuildMaterialPreviewCamera();
        }
        if (RenderView* view = MaterialPreviewView())
        {
            view->InvalidateTemporalHistory();
        }
    }

    void AssetThumbnailRenderer::BeforeNextFrame()
    {
        if (releaseThumbnailView_)
        {
            RenderViewResourceFactory(renderer_).DestroyView(thumbnailRenderView_);
            releaseThumbnailView_ = false;
        }
        if (materialPreviewEnabled_)
        {
            EnsureMaterialPreviewScene();
        }
    }

    void AssetThumbnailRenderer::OnMainSceneChanged()
    {
        if (RenderView* view = ThumbnailView())
        {
            view->InvalidateTemporalHistory();
            view->SetSceneOverride(nullptr);
        }
        if (RenderView* view = MaterialPreviewView())
        {
            view->InvalidateTemporalHistory();
            view->SetSceneOverride(nullptr);
        }

        thumbnailScene_.reset();
        thumbnailSceneReady_ = false;
        materialPreviewScene_.reset();
        materialPreviewSceneReady_ = false;
        materialPreviewDirty_ = true;
        ClearThumbnailCache();
        thumbnailTarget_.ResetSwapChainResources(/*releaseSampledOutput*/ false);
    }

    void AssetThumbnailRenderer::OnHdrShUpdated()
    {
        if (thumbnailScene_ != nullptr)
        {
            thumbnailScene_->UpdateHDRSH();
        }
        if (materialPreviewScene_ != nullptr)
        {
            materialPreviewScene_->UpdateHDRSH();
        }

        EnqueueExistingThumbnailImages();
        if (RenderView* view = ThumbnailView())
        {
            view->InvalidateTemporalHistory();
        }
        if (RenderView* view = MaterialPreviewView())
        {
            view->InvalidateTemporalHistory();
        }
    }

    void AssetThumbnailRenderer::OnSwapChainResourcesInvalidated(bool /*releaseSampledOutputs*/)
    {
        thumbnailTarget_.ResetSwapChainResources(/*releaseSampledOutput*/ false);
        materialPreviewTarget_.ResetSwapChainResources(/*releaseSampledOutput*/ false);
        if (RenderView* view = ThumbnailView())
        {
            view->SetSceneOverride(nullptr);
        }
        if (RenderView* view = MaterialPreviewView())
        {
            view->SetSceneOverride(nullptr);
        }
    }

    bool AssetThumbnailRenderer::HasPendingThumbnail() const
    {
        return !pendingThumbnails_.empty();
    }

    void AssetThumbnailRenderer::ClearThumbnailCache()
    {
        Assets::GlobalTexturePool* texturePool = Assets::GlobalTexturePool::GetInstance();
        for (uint32_t index = 0; index < thumbnailSlots_.size(); ++index)
        {
            FThumbnailSlot& slot = thumbnailSlots_[index];
            if (slot.image != nullptr && texturePool != nullptr)
            {
                texturePool->BindDefaultSampleTexture(kThumbnailSampleSlotBase + index);
            }
            slot = {};
        }
        thumbnailLookup_.clear();
        pendingThumbnails_.clear();
    }

    void AssetThumbnailRenderer::EnqueueExistingThumbnailImages()
    {
        for (uint32_t index = 0; index < thumbnailSlots_.size(); ++index)
        {
            if (!thumbnailSlots_[index].occupied || thumbnailSlots_[index].image == nullptr)
            {
                continue;
            }
            QueueThumbnail(index);
        }
    }

    std::unique_ptr<Assets::Scene> AssetThumbnailRenderer::CreateMaterialSphereScene(
        Assets::FMaterial material,
        const char* nodeName,
        const char* cameraName,
        const bool sunEnabled,
        const Assets::Camera& camera)
    {
        auto scene = std::make_unique<Assets::Scene>(
            renderer_.CommandPool(), false, /*allocateAmbientResources*/ false, /*enableCpuAcceleration*/ false);

        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
        materials.push_back(std::move(material));
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            nodeName,
            glm::vec3(0.0f),
            glm::vec3(1.0f),
            1u,
            0u,
            0u,
            true));

        scene->Reload(nodes, models, materials, lights, tracks);
        scene->PostLoad(skeletons);
        scene->RebuildMeshBuffer(renderer_.CommandPool(), false);

        Assets::EnvironmentSetting env;
        env.Reset();
        env.HasSky = true;
        env.HasSun = sunEnabled;
        env.SunIntensity = sunEnabled ? 500.0f : 1000.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 300.0f;
        scene->GetEnvSettings() = env;

        Assets::Camera sceneCamera = camera;
        sceneCamera.name = cameraName;
        scene->GetRenderCamera() = sceneCamera;
        scene->SyncUpdateScene();
        return scene;
    }

    Assets::Camera AssetThumbnailRenderer::BuildMaterialPreviewCamera() const
    {
        Assets::Camera camera{};
        camera.name = "Material Preview";
        camera.FieldOfView = 38.0f;
        camera.Aperture = 0.0f;
        camera.FocalDistance = materialPreviewDistance_;
        camera.NearPlane = 0.01f;
        camera.FarPlane = 20.0f;
        const glm::vec3 eye(
            std::sin(materialPreviewYaw_) * std::cos(materialPreviewPitch_) * materialPreviewDistance_,
            std::sin(materialPreviewPitch_) * materialPreviewDistance_,
            std::cos(materialPreviewYaw_) * std::cos(materialPreviewPitch_) * materialPreviewDistance_);
        camera.ModelView = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        return camera;
    }

    void AssetThumbnailRenderer::EnsureMaterialThumbnailScene()
    {
        if (thumbnailSceneReady_ && thumbnailSceneKind_ == EThumbnailKind::Material)
        {
            return;
        }

        Assets::FMaterial previewMaterial;
        previewMaterial.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.75f));
        previewMaterial.name_ = "__material_thumbnail_preview";

        Assets::Camera camera{};
        camera.name = "Material Thumbnail";
        camera.FieldOfView = 38.0f;
        camera.Aperture = 0.0f;
        camera.FocalDistance = 3.0f;
        camera.NearPlane = 0.01f;
        camera.FarPlane = 20.0f;
        camera.ModelView = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        thumbnailScene_ = CreateMaterialSphereScene(
            std::move(previewMaterial),
            "__MaterialThumbnailSphere",
            "Material Thumbnail",
            true,
            camera);
        thumbnailSceneKind_ = EThumbnailKind::Material;
        thumbnailSceneReady_ = true;
    }

    void AssetThumbnailRenderer::EnsureMaterialPreviewScene()
    {
        if (materialPreviewSceneReady_)
        {
            return;
        }

        materialPreviewScene_ = CreateMaterialSphereScene(
            materialPreview_,
            "__MaterialPreviewSphere",
            "Material Preview",
            false,
            BuildMaterialPreviewCamera());
        materialPreviewSceneReady_ = true;
        materialPreviewDirty_ = true;
    }

    void AssetThumbnailRenderer::EnsureThumbnailRenderTarget()
    {
        FViewDesc viewDesc{};
        viewDesc.renderExtent = kThumbnailExtent;
        viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
        viewDesc.schedule = EViewSchedule::Transient;

        RenderViewResourceFactory resources(renderer_);
        RenderView& view = resources.EnsureView(thumbnailRenderView_, viewDesc, "thumbnail view", false);
        if (view.AllocatedExtent().width != kThumbnailExtent.width ||
            view.AllocatedExtent().height != kThumbnailExtent.height ||
            view.VisibilityFramebuffer() == nullptr)
        {
            thumbnailTarget_.visibilityFramebuffer = resources.RebuildVisibilityFramebuffer(view, kThumbnailExtent);
        }
        if (!thumbnailTarget_.offscreenSampler)
        {
            thumbnailTarget_.offscreenSampler = resources.CreateClampSampler();
        }
    }

    void AssetThumbnailRenderer::EnsureMaterialPreviewRenderTarget()
    {
        EnsureMaterialPreviewScene();

        FViewDesc viewDesc{};
        viewDesc.renderExtent = materialPreviewExtent_;
        viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
        viewDesc.schedule = EViewSchedule::Persistent;

        RenderViewResourceFactory resources(renderer_);
        RenderView& view = resources.EnsureView(materialPreviewView_, viewDesc, "material preview view", false);
        resources.EnsureSampledOffscreenTarget(
            view,
            materialPreviewTarget_,
            materialPreviewExtent_,
            SampleSlot(),
            "Material Preview Offscreen");
    }

    void AssetThumbnailRenderer::RebuildMeshThumbnailScene(const Assets::Model& model)
    {
        thumbnailScene_ = std::make_unique<Assets::Scene>(
            renderer_.CommandPool(), false, /*allocateAmbientResources*/ false, /*enableCpuAcceleration*/ false);

        const glm::vec3 aabbMin = model.GetLocalAABBMin();
        const glm::vec3 aabbMax = model.GetLocalAABBMax();
        const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
        const glm::vec3 extent = glm::max(aabbMax - aabbMin, glm::vec3(0.001f));
        const float radius = std::max(glm::length(extent) * 0.5f, 0.05f);

        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        models.push_back(model);
        Assets::FMaterial previewMaterial;
        previewMaterial.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.72f, 0.74f, 0.78f));
        previewMaterial.name_ = "__mesh_thumbnail_default";
        materials.push_back(previewMaterial);
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "__MeshThumbnailModel",
            -center,
            glm::vec3(1.0f),
            1u,
            0u,
            0u,
            true));

        thumbnailScene_->Reload(nodes, models, materials, lights, tracks);
        thumbnailScene_->PostLoad(skeletons);
        thumbnailScene_->RebuildMeshBuffer(renderer_.CommandPool(), false);

        Assets::EnvironmentSetting env;
        env.Reset();
        env.HasSky = true;
        env.HasSun = true;
        env.SunIntensity = 500.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 300.0f;
        thumbnailScene_->GetEnvSettings() = env;

        Assets::Camera camera{};
        camera.name = "Mesh Thumbnail";
        camera.FieldOfView = 38.0f;
        camera.Aperture = 0.0f;
        const float halfFov = glm::radians(camera.FieldOfView) * 0.5f;
        const float distance = std::max(radius / std::max(std::sin(halfFov), 0.01f) * 1.18f, 0.15f);
        const glm::vec3 viewDir = glm::normalize(glm::vec3(0.72f, 0.42f, 0.86f));
        const glm::vec3 eye = viewDir * distance;
        camera.FocalDistance = distance;
        camera.NearPlane = std::max(0.001f, distance - radius * 2.5f);
        camera.FarPlane = std::max(camera.NearPlane + 1.0f, distance + radius * 3.5f);
        camera.ModelView = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        thumbnailScene_->GetRenderCamera() = camera;
        thumbnailScene_->SyncUpdateScene();
        thumbnailSceneKind_ = EThumbnailKind::Mesh;
        thumbnailSceneReady_ = true;
    }

    bool AssetThumbnailRenderer::RebuildScadKitThumbnailScene(const std::string& sourcePath)
    {
        auto scene = std::make_unique<Assets::Scene>(
            renderer_.CommandPool(), false, /*allocateAmbientResources*/ false, /*enableCpuAcceleration*/ false);

        Assets::EnvironmentSetting cameraInit;
        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        Assets::ScadLoadOptions options;
        // A thumbnail contains one module call. Keep evaluation deterministic
        // and avoid spending worker-thread overhead on this small scene.
        options.parallelTopLevel = false;
        if (!Assets::FScadLoader::LoadScadScene(
                sourcePath, cameraInit, nodes, models, materials, lights, tracks, skeletons, options))
        {
            return false;
        }
        const bool hasGeometry = std::any_of(
            models.begin(), models.end(), [](const Assets::Model& model) { return model.NumberOfVertices() > 0; });
        if (!hasGeometry)
        {
            return false;
        }

        scene->Reload(nodes, models, materials, lights, tracks);
        scene->PostLoad(skeletons);
        scene->RebuildMeshBuffer(renderer_.CommandPool(), false);
        scene->GetEnvSettings() = cameraInit;
        if (!cameraInit.cameras.empty())
        {
            scene->GetRenderCamera() = cameraInit.cameras.front();
        }
        scene->SyncUpdateScene();
        thumbnailScene_ = std::move(scene);
        thumbnailSceneKind_ = EThumbnailKind::ScadKit;
        thumbnailSceneReady_ = true;
        return true;
    }

    void AssetThumbnailRenderer::CopyThumbnailViewOutput(
        VkCommandBuffer commandBuffer,
        RenderView& view,
        RenderImage& dst)
    {
        RenderViewResourceFactory(renderer_).CopyRenderOutputToImage(commandBuffer, view, dst);
    }

    void AssetThumbnailRenderer::CopyMaterialPreviewOutput(VkCommandBuffer commandBuffer, RenderView& view)
    {
        if (materialPreviewTarget_.offscreenImage == nullptr)
        {
            return;
        }
        RenderViewResourceFactory(renderer_).CopyRenderOutputToImage(
            commandBuffer,
            view,
            *materialPreviewTarget_.offscreenImage);
    }

    RenderImage& AssetThumbnailRenderer::EnsureThumbnailImage(
        const uint32_t poolIndex,
        const char* debugName)
    {
        FThumbnailSlot& slot = thumbnailSlots_[poolIndex];
        if (!slot.image)
        {
            RenderViewResourceFactory resources(renderer_);
            slot.image = resources.CreateSampledColorImage(kThumbnailExtent, debugName);
            resources.BindSampledColorImage(
                kThumbnailSampleSlotBase + poolIndex,
                *slot.image,
                *thumbnailTarget_.offscreenSampler);
        }
        return *slot.image;
    }

    bool AssetThumbnailRenderer::ScheduleMaterialPreview(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (!materialPreviewEnabled_)
        {
            return false;
        }

        EnsureMaterialPreviewRenderTarget();
        RenderView* materialPreviewView = MaterialPreviewView();
        if (!materialPreviewScene_ || materialPreviewView == nullptr)
        {
            return false;
        }

        if (materialPreviewDirty_)
        {
            if (materialPreviewScene_->Materials().empty())
            {
                materialPreviewScene_->Materials().push_back(materialPreview_);
            }
            else
            {
                materialPreviewScene_->Materials()[0] = materialPreview_;
            }
            materialPreviewScene_->UpdateAllMaterials();
            materialPreviewScene_->SyncUpdateScene();
            materialPreviewDirty_ = false;
        }

        LogicRendererBase* logicRenderer = renderer_.EnsureLogicRenderer(ERT_SoftwareModernNoAmbient);
        if (logicRenderer == nullptr)
        {
            logicRenderer = renderer_.EnsureLogicRenderer(renderer_.CurrentLogicRendererType());
        }
        if (logicRenderer == nullptr)
        {
            return false;
        }

        materialPreviewScene_->UpdateHDRSH();
        const Assets::UniformBufferObject previewCamera = BuildViewCameraUbo({
            .scene = *materialPreviewScene_,
            .camera = materialPreviewScene_->GetRenderCamera(),
            .extent = materialPreviewExtent_,
            .cascadeDistance = 20.0f,
            .totalFrames = static_cast<uint32_t>(std::max(renderer_.FrameCount(), 1)),
            .fillSceneLighting = true,
            .thumbnailDefaults = true,
        });

        renderer_.SetRenderViewUbo(*materialPreviewView, imageIndex, previewCamera);
        materialPreviewView->SetVisibilityFramebuffer(materialPreviewTarget_.visibilityFramebuffer.get());
        materialPreviewView->SetSceneOverride(materialPreviewScene_.get());
        materialPreviewView->SetPrevDepthValid(false);
        renderer_.ScheduleRenderView(
            *materialPreviewView,
            *logicRenderer,
            /*clearSwapchain*/ false,
            [this, commandBuffer](RenderView& view)
            {
                CopyMaterialPreviewOutput(commandBuffer, view);
                view.SetSceneOverride(nullptr);
            });

        return true;
    }

    bool AssetThumbnailRenderer::ScheduleNextThumbnail(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        if (pendingThumbnails_.empty())
        {
            return false;
        }

        const uint32_t poolIndex = pendingThumbnails_.front();
        pendingThumbnails_.erase(pendingThumbnails_.begin());
        if (poolIndex >= thumbnailSlots_.size())
        {
            return false;
        }

        FThumbnailSlot& slot = thumbnailSlots_[poolIndex];
        slot.pending = false;
        if (!slot.occupied)
        {
            return false;
        }

        const EThumbnailKind kind = slot.key.kind;
        const uint32_t assetIndex = slot.key.assetIndex;
        auto mainScene = renderer_.GetSceneShared();
        if (!mainScene && kind != EThumbnailKind::ScadKit)
        {
            return false;
        }

        const char* viewDebugName = "thumbnail view";
        std::string imageDebugName;
        if (kind == EThumbnailKind::Material)
        {
            if (assetIndex >= mainScene->Materials().size())
            {
                return false;
            }

            EnsureMaterialThumbnailScene();
            if (thumbnailScene_ == nullptr || thumbnailScene_->Materials().empty())
            {
                return false;
            }

            thumbnailScene_->Materials()[0] = mainScene->Materials()[assetIndex];
            thumbnailScene_->Materials()[0].name_ = "__material_thumbnail_preview";
            viewDebugName = "material thumbnail view";
            imageDebugName = fmt::format("Material Thumbnail {}", assetIndex);
        }
        else if (kind == EThumbnailKind::Mesh)
        {
            if (assetIndex >= mainScene->Models().size())
            {
                return false;
            }

            const Assets::Model& model = mainScene->Models()[assetIndex];
            if (model.NumberOfVertices() == 0)
            {
                return false;
            }

            RebuildMeshThumbnailScene(model);
            if (thumbnailScene_ == nullptr)
            {
                return false;
            }
            viewDebugName = "mesh thumbnail view";
            imageDebugName = fmt::format("Mesh Thumbnail {}", assetIndex);
        }
        else
        {
            if (slot.sourcePath.empty() || !RebuildScadKitThumbnailScene(slot.sourcePath))
            {
                return false;
            }
            viewDebugName = "SCAD kit thumbnail view";
            imageDebugName = fmt::format("SCAD Kit Thumbnail {}", assetIndex);
        }

        EnsureThumbnailRenderTarget();
        RenderView* thumbnailRenderView = ThumbnailView();
        assert(thumbnailRenderView != nullptr);
        EnsureThumbnailImage(poolIndex, imageDebugName.c_str());

        LogicRendererBase* logicRenderer = renderer_.EnsureLogicRenderer(ERT_SoftwareModernNoAmbient);
        if (logicRenderer == nullptr)
        {
            logicRenderer = renderer_.EnsureLogicRenderer(renderer_.CurrentLogicRendererType());
        }
        if (logicRenderer == nullptr)
        {
            return false;
        }

        thumbnailScene_->UpdateAllMaterials();
        thumbnailScene_->SyncUpdateScene();
        thumbnailScene_->UpdateHDRSH();
        Assets::UniformBufferObject previewCamera = BuildViewCameraUbo({
            .scene = *thumbnailScene_,
            .camera = thumbnailScene_->GetRenderCamera(),
            .extent = kThumbnailExtent,
            .cascadeDistance = 20.0f,
            .totalFrames = static_cast<uint32_t>(std::max(renderer_.FrameCount(), 1)),
            .fillSceneLighting = true,
            .thumbnailDefaults = true,
        });
        previewCamera.ForceBlackBackground = true;

        thumbnailRenderView->SetDebugName(viewDebugName);
        thumbnailRenderView->SetRenderExtent(kThumbnailExtent);
        renderer_.SetRenderViewUbo(*thumbnailRenderView, imageIndex, previewCamera);
        thumbnailRenderView->SetVisibilityFramebuffer(thumbnailTarget_.visibilityFramebuffer.get());
        thumbnailRenderView->SetSceneOverride(thumbnailScene_.get());
        thumbnailRenderView->SetPrevDepthValid(false);
        const uint64_t generation = slot.generation;
        renderer_.ScheduleRenderView(
            *thumbnailRenderView,
            *logicRenderer,
            /*clearSwapchain*/ false,
            [this, commandBuffer, poolIndex, generation](RenderView& view)
            {
                if (poolIndex < thumbnailSlots_.size())
                {
                    FThumbnailSlot& completedSlot = thumbnailSlots_[poolIndex];
                    if (completedSlot.occupied && completedSlot.generation == generation && completedSlot.image)
                    {
                        CopyThumbnailViewOutput(commandBuffer, view, *completedSlot.image);
                        completedSlot.ready = true;
                    }
                }
                view.SetSceneOverride(nullptr);
                releaseThumbnailView_ = true;
            });

        return true;
    }
}

namespace Vulkan
{
    bool AssetThumbnailRenderer::ScheduleViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const bool scheduledTransient = ScheduleNextThumbnail(commandBuffer, imageIndex);
        ScheduleMaterialPreview(commandBuffer, imageIndex);
        return scheduledTransient;
    }
}

namespace EditorPreview
{
    Vulkan::AssetThumbnailRenderer& AssetThumbnails(Vulkan::VulkanBaseRenderer& renderer)
    {
        Vulkan::RenderViewServices& services = renderer.ViewServices();
        if (Vulkan::IRenderViewProvider* provider = services.FindProvider("EditorAssetThumbnails"))
        {
            return static_cast<Vulkan::AssetThumbnailRenderer&>(*provider);
        }
        return static_cast<Vulkan::AssetThumbnailRenderer&>(*services.RegisterProvider(
            "EditorAssetThumbnails", 0, std::make_unique<Vulkan::AssetThumbnailRenderer>(renderer)));
    }
}
