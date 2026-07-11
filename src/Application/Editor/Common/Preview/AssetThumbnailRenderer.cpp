#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Editor/Common/Preview/AssetThumbnailRenderer.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace Vulkan
{
    AssetThumbnailRenderer::AssetThumbnailRenderer(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
        ThumbnailCache(EThumbnailKind::Material).sampleSlotBase = kMaterialThumbnailSampleSlotBase;
        ThumbnailCache(EThumbnailKind::Material).maxSlots = kMaterialThumbnailMaxSlots;
        ThumbnailCache(EThumbnailKind::Mesh).sampleSlotBase = kMeshThumbnailSampleSlotBase;
        ThumbnailCache(EThumbnailKind::Mesh).maxSlots = kMeshThumbnailMaxSlots;
        materialPreview_.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.75f));
        materialPreview_.name_ = "__material_preview";
    }

    AssetThumbnailRenderer::~AssetThumbnailRenderer()
    {
        RenderViewResourceFactory factory(renderer_);
        factory.DestroyView(thumbnailRenderView_);
        factory.DestroyView(materialPreviewView_);
    }

    AssetThumbnailRenderer::FThumbnailCache& AssetThumbnailRenderer::ThumbnailCache(const EThumbnailKind kind)
    {
        return thumbnailCaches_[ThumbnailKindIndex(kind)];
    }

    const AssetThumbnailRenderer::FThumbnailCache& AssetThumbnailRenderer::ThumbnailCache(
        const EThumbnailKind kind) const
    {
        return thumbnailCaches_[ThumbnailKindIndex(kind)];
    }

    uint32_t AssetThumbnailRenderer::RequestMaterialThumbnail(
        const uint32_t materialIndex,
        const uint64_t materialHash)
    {
        return RequestThumbnail(EThumbnailKind::Material, materialIndex, materialHash);
    }

    uint32_t AssetThumbnailRenderer::RequestMeshThumbnail(const uint32_t modelIndex, const uint64_t modelHash)
    {
        return RequestThumbnail(EThumbnailKind::Mesh, modelIndex, modelHash);
    }

    uint32_t AssetThumbnailRenderer::RequestThumbnail(
        const EThumbnailKind kind,
        const uint32_t assetIndex,
        const uint64_t assetHash)
    {
        FThumbnailCache& cache = ThumbnailCache(kind);
        if (assetIndex >= cache.maxSlots)
        {
            return std::numeric_limits<uint32_t>::max();
        }

        if (cache.hashes.size() <= assetIndex)
        {
            cache.hashes.resize(static_cast<size_t>(assetIndex) + 1, 0);
        }
        if (cache.images.size() <= assetIndex)
        {
            cache.images.resize(static_cast<size_t>(assetIndex) + 1);
        }

        if (cache.images[assetIndex] != nullptr && cache.hashes[assetIndex] == assetHash)
        {
            return cache.sampleSlotBase + assetIndex;
        }

        cache.hashes[assetIndex] = assetHash;
        if (std::find(cache.pending.begin(), cache.pending.end(), assetIndex) == cache.pending.end())
        {
            cache.pending.push_back(assetIndex);
        }
        return std::numeric_limits<uint32_t>::max();
    }

    void AssetThumbnailRenderer::SetEnabled(const bool enabled)
    {
        materialPreviewEnabled_ = enabled;
        if (!materialPreviewEnabled_ && materialPreviewView_ != nullptr)
        {
            materialPreviewView_->SetSceneOverride(nullptr);
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
        if (materialPreviewView_ != nullptr)
        {
            materialPreviewView_->SetRenderExtent(materialPreviewExtent_);
            materialPreviewView_->InvalidateTemporalHistory();
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
        if (materialPreviewView_ != nullptr)
        {
            materialPreviewView_->InvalidateTemporalHistory();
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
            materialPreviewScene_->SetRenderCamera(BuildMaterialPreviewCamera());
        }
        if (materialPreviewView_ != nullptr)
        {
            materialPreviewView_->InvalidateTemporalHistory();
        }
    }

    void AssetThumbnailRenderer::BeforeNextFrame()
    {
        if (materialPreviewEnabled_)
        {
            EnsureMaterialPreviewScene();
        }
        if (HasPendingThumbnail(EThumbnailKind::Material))
        {
            EnsureMaterialThumbnailScene();
        }
    }

    void AssetThumbnailRenderer::OnMainSceneChanged()
    {
        if (thumbnailRenderView_ != nullptr)
        {
            thumbnailRenderView_->InvalidateTemporalHistory();
            thumbnailRenderView_->SetSceneOverride(nullptr);
        }
        if (materialPreviewView_ != nullptr)
        {
            materialPreviewView_->InvalidateTemporalHistory();
            materialPreviewView_->SetSceneOverride(nullptr);
        }

        thumbnailScene_.reset();
        thumbnailSceneReady_ = false;
        materialPreviewScene_.reset();
        materialPreviewSceneReady_ = false;
        materialPreviewDirty_ = true;
        ClearThumbnailCaches();
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

        EnqueueExistingThumbnailImages(ThumbnailCache(EThumbnailKind::Material));
        EnqueueExistingThumbnailImages(ThumbnailCache(EThumbnailKind::Mesh));
        if (thumbnailRenderView_ != nullptr)
        {
            thumbnailRenderView_->InvalidateTemporalHistory();
        }
        if (materialPreviewView_ != nullptr)
        {
            materialPreviewView_->InvalidateTemporalHistory();
        }
    }

    void AssetThumbnailRenderer::OnSwapChainResourcesInvalidated(bool /*releaseSampledOutputs*/)
    {
        thumbnailTarget_.ResetSwapChainResources(/*releaseSampledOutput*/ false);
        materialPreviewTarget_.ResetSwapChainResources(/*releaseSampledOutput*/ false);
        if (thumbnailRenderView_ != nullptr)
        {
            thumbnailRenderView_->SetSceneOverride(nullptr);
        }
        if (materialPreviewView_ != nullptr)
        {
            materialPreviewView_->SetSceneOverride(nullptr);
        }
    }

    bool AssetThumbnailRenderer::ScheduleNextThumbnail(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (ScheduleNextThumbnail(EThumbnailKind::Material, commandBuffer, imageIndex))
        {
            return true;
        }
        return ScheduleNextThumbnail(EThumbnailKind::Mesh, commandBuffer, imageIndex);
    }

    bool AssetThumbnailRenderer::HasPendingThumbnail() const
    {
        return HasPendingThumbnail(EThumbnailKind::Material) || HasPendingThumbnail(EThumbnailKind::Mesh);
    }

    void AssetThumbnailRenderer::ClearThumbnailCaches()
    {
        for (FThumbnailCache& cache : thumbnailCaches_)
        {
            cache.images.clear();
            cache.hashes.clear();
            cache.pending.clear();
        }
    }

    void AssetThumbnailRenderer::EnqueueExistingThumbnailImages(FThumbnailCache& cache)
    {
        for (uint32_t index = 0; index < cache.images.size(); ++index)
        {
            if (cache.images[index] == nullptr)
            {
                continue;
            }
            if (std::find(cache.pending.begin(), cache.pending.end(), index) == cache.pending.end())
            {
                cache.pending.push_back(index);
            }
        }
    }

    bool AssetThumbnailRenderer::HasPendingThumbnail(const EThumbnailKind kind) const
    {
        return !ThumbnailCache(kind).pending.empty();
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
        scene->SetEnvSettings(env);

        Assets::Camera sceneCamera = camera;
        sceneCamera.name = cameraName;
        scene->SetRenderCamera(sceneCamera);
        scene->UpdateNodes();
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
        if (thumbnailRenderView_->AllocatedExtent().width != kThumbnailExtent.width ||
            thumbnailRenderView_->AllocatedExtent().height != kThumbnailExtent.height ||
            thumbnailRenderView_->VisibilityFramebuffer() == nullptr)
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
        thumbnailScene_->SetEnvSettings(env);

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
        thumbnailScene_->SetRenderCamera(camera);
        thumbnailScene_->UpdateNodes();
        thumbnailSceneKind_ = EThumbnailKind::Mesh;
        thumbnailSceneReady_ = true;
    }

    void AssetThumbnailRenderer::CopyThumbnailViewOutput(
        VkCommandBuffer commandBuffer,
        RenderView& view,
        RenderImage& dst)
    {
        RenderViewResourceFactory(renderer_).CopyDenoisedOutputToImage(commandBuffer, view, dst);
    }

    void AssetThumbnailRenderer::CopyMaterialPreviewOutput(VkCommandBuffer commandBuffer, RenderView& view)
    {
        if (materialPreviewTarget_.offscreenImage == nullptr)
        {
            return;
        }
        RenderViewResourceFactory(renderer_).CopyDenoisedOutputToImage(
            commandBuffer,
            view,
            *materialPreviewTarget_.offscreenImage);
    }

    RenderImage& AssetThumbnailRenderer::EnsureThumbnailImage(
        FThumbnailCache& cache,
        const uint32_t assetIndex,
        const char* debugName)
    {
        if (cache.images.size() <= assetIndex)
        {
            cache.images.resize(static_cast<size_t>(assetIndex) + 1);
        }
        if (!cache.images[assetIndex])
        {
            RenderViewResourceFactory resources(renderer_);
            cache.images[assetIndex] = resources.CreateSampledColorImage(kThumbnailExtent, debugName);
            resources.BindSampledColorImage(
                cache.sampleSlotBase + assetIndex,
                *cache.images[assetIndex],
                *thumbnailTarget_.offscreenSampler);
        }
        return *cache.images[assetIndex];
    }

    bool AssetThumbnailRenderer::ScheduleMaterialPreview(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (!materialPreviewEnabled_)
        {
            return false;
        }

        EnsureMaterialPreviewRenderTarget();
        if (!materialPreviewScene_ || !materialPreviewView_)
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
            materialPreviewScene_->UpdateNodes();
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

        renderer_.SetRenderViewUbo(*materialPreviewView_, imageIndex, previewCamera);
        materialPreviewView_->SetVisibilityFramebuffer(materialPreviewTarget_.visibilityFramebuffer.get());
        materialPreviewView_->SetSceneOverride(materialPreviewScene_.get());
        materialPreviewView_->SetPrevDepthValid(false);
        renderer_.ScheduleRenderView(
            *materialPreviewView_,
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
        const EThumbnailKind kind,
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        FThumbnailCache& cache = ThumbnailCache(kind);
        if (cache.pending.empty())
        {
            return false;
        }

        auto mainScene = renderer_.GetSceneShared();
        if (!mainScene)
        {
            cache.pending.clear();
            return false;
        }

        const uint32_t assetIndex = cache.pending.front();
        cache.pending.erase(cache.pending.begin());
        if (assetIndex >= cache.maxSlots)
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
        else
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

        EnsureThumbnailRenderTarget();
        assert(thumbnailRenderView_ != nullptr);
        EnsureThumbnailImage(cache, assetIndex, imageDebugName.c_str());

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
        thumbnailScene_->UpdateNodes();
        thumbnailScene_->UpdateHDRSH();
        const Assets::UniformBufferObject previewCamera = BuildViewCameraUbo({
            .scene = *thumbnailScene_,
            .camera = thumbnailScene_->GetRenderCamera(),
            .extent = kThumbnailExtent,
            .cascadeDistance = 20.0f,
            .totalFrames = static_cast<uint32_t>(std::max(renderer_.FrameCount(), 1)),
            .fillSceneLighting = true,
            .thumbnailDefaults = true,
        });

        thumbnailRenderView_->SetDebugName(viewDebugName);
        thumbnailRenderView_->SetRenderExtent(kThumbnailExtent);
        renderer_.SetRenderViewUbo(*thumbnailRenderView_, imageIndex, previewCamera);
        thumbnailRenderView_->SetVisibilityFramebuffer(thumbnailTarget_.visibilityFramebuffer.get());
        thumbnailRenderView_->SetSceneOverride(thumbnailScene_.get());
        thumbnailRenderView_->SetPrevDepthValid(false);
        renderer_.ScheduleRenderView(
            *thumbnailRenderView_,
            *logicRenderer,
            /*clearSwapchain*/ false,
            [this, commandBuffer, kind, assetIndex](RenderView& view)
            {
                FThumbnailCache& cache = ThumbnailCache(kind);
                if (assetIndex < cache.images.size() && cache.images[assetIndex])
                {
                    CopyThumbnailViewOutput(commandBuffer, view, *cache.images[assetIndex]);
                }
                view.SetSceneOverride(nullptr);
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
