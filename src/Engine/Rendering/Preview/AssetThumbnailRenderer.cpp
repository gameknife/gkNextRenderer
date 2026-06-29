#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/AssetThumbnailRenderer.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Rendering/RenderViewResourceFactory.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

#include <limits>

namespace Vulkan
{
    AssetThumbnailRenderer::AssetThumbnailRenderer(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
    }

    AssetThumbnailRenderer::~AssetThumbnailRenderer() = default;

    uint32_t AssetThumbnailRenderer::RequestMaterialThumbnail(
        const uint32_t materialIndex,
        const uint64_t materialHash)
    {
        if (materialIndex >= kMaterialThumbnailMaxSlots)
        {
            return std::numeric_limits<uint32_t>::max();
        }

        if (materialThumbnailHashes_.size() <= materialIndex)
        {
            materialThumbnailHashes_.resize(static_cast<size_t>(materialIndex) + 1, 0);
        }
        if (materialThumbnailImages_.size() <= materialIndex)
        {
            materialThumbnailImages_.resize(static_cast<size_t>(materialIndex) + 1);
        }

        if (materialThumbnailImages_[materialIndex] != nullptr &&
            materialThumbnailHashes_[materialIndex] == materialHash)
        {
            return kMaterialThumbnailSampleSlotBase + materialIndex;
        }

        materialThumbnailHashes_[materialIndex] = materialHash;
        if (std::find(pendingMaterialThumbnails_.begin(), pendingMaterialThumbnails_.end(), materialIndex) ==
            pendingMaterialThumbnails_.end())
        {
            pendingMaterialThumbnails_.push_back(materialIndex);
        }
        return std::numeric_limits<uint32_t>::max();
    }

    uint32_t AssetThumbnailRenderer::RequestMeshThumbnail(const uint32_t modelIndex, const uint64_t modelHash)
    {
        if (modelIndex >= kMeshThumbnailMaxSlots)
        {
            return std::numeric_limits<uint32_t>::max();
        }

        if (meshThumbnailHashes_.size() <= modelIndex)
        {
            meshThumbnailHashes_.resize(static_cast<size_t>(modelIndex) + 1, 0);
        }
        if (meshThumbnailImages_.size() <= modelIndex)
        {
            meshThumbnailImages_.resize(static_cast<size_t>(modelIndex) + 1);
        }

        if (meshThumbnailImages_[modelIndex] != nullptr && meshThumbnailHashes_[modelIndex] == modelHash)
        {
            return kMeshThumbnailSampleSlotBase + modelIndex;
        }

        meshThumbnailHashes_[modelIndex] = modelHash;
        if (std::find(pendingMeshThumbnails_.begin(), pendingMeshThumbnails_.end(), modelIndex) ==
            pendingMeshThumbnails_.end())
        {
            pendingMeshThumbnails_.push_back(modelIndex);
        }
        return std::numeric_limits<uint32_t>::max();
    }

    void AssetThumbnailRenderer::BeforeNextFrame()
    {
        if (HasPendingMaterialThumbnail())
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

        materialThumbnailScene_.reset();
        materialThumbnailImages_.clear();
        materialThumbnailHashes_.clear();
        pendingMaterialThumbnails_.clear();
        materialThumbnailSceneReady_ = false;
        meshThumbnailScene_.reset();
        meshThumbnailImages_.clear();
        meshThumbnailHashes_.clear();
        pendingMeshThumbnails_.clear();
        thumbnailVisibilityFrameBuffer_.reset();
    }

    void AssetThumbnailRenderer::OnSwapChainResourcesInvalidated()
    {
        thumbnailVisibilityFrameBuffer_.reset();
        if (thumbnailRenderView_ != nullptr)
        {
            thumbnailRenderView_->SetSceneOverride(nullptr);
            thumbnailRenderView_->ResetSwapChainResources();
        }
    }

    bool AssetThumbnailRenderer::ScheduleNextThumbnail(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (ScheduleNextMaterialThumbnail(commandBuffer, imageIndex))
        {
            return true;
        }
        return ScheduleNextMeshThumbnail(commandBuffer, imageIndex);
    }

    bool AssetThumbnailRenderer::HasPendingThumbnail() const
    {
        return HasPendingMaterialThumbnail() || HasPendingMeshThumbnail();
    }

    void AssetThumbnailRenderer::EnsureMaterialThumbnailScene()
    {
        if (materialThumbnailSceneReady_)
        {
            return;
        }

        materialThumbnailScene_ = std::make_unique<Assets::Scene>(
            renderer_.CommandPool(), false, /*allocateAmbientResources*/ false, /*enableCpuAcceleration*/ false);

        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
        Assets::FMaterial previewMaterial;
        previewMaterial.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.75f));
        previewMaterial.name_ = "__material_thumbnail_preview";
        materials.push_back(previewMaterial);
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "__MaterialThumbnailSphere",
            glm::vec3(0.0f),
            glm::vec3(1.0f),
            1u,
            0u,
            0u,
            true));

        materialThumbnailScene_->Reload(nodes, models, materials, lights, tracks);
        materialThumbnailScene_->PostLoad(skeletons);
        materialThumbnailScene_->RebuildMeshBuffer(renderer_.CommandPool(), false);

        Assets::EnvironmentSetting env;
        env.Reset();
        env.HasSky = true;
        env.HasSun = false;
        env.SunIntensity = 1000.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 300.0f;
        materialThumbnailScene_->SetEnvSettings(env);

        Assets::Camera camera{};
        camera.name = "Material Thumbnail";
        camera.FieldOfView = 38.0f;
        camera.Aperture = 0.0f;
        camera.FocalDistance = 3.0f;
        camera.NearPlane = 0.01f;
        camera.FarPlane = 20.0f;
        camera.ModelView = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        materialThumbnailScene_->SetRenderCamera(camera);
        materialThumbnailScene_->UpdateNodes();
        materialThumbnailSceneReady_ = true;
    }

    void AssetThumbnailRenderer::EnsureThumbnailRenderTarget()
    {
        if (thumbnailRenderView_ == nullptr)
        {
            FViewDesc viewDesc{};
            viewDesc.renderExtent = kThumbnailExtent;
            viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
            viewDesc.schedule = EViewSchedule::Transient;
            thumbnailRenderView_ = renderer_.renderViews_->CreateView(viewDesc, "thumbnail view");
            if (thumbnailRenderView_ == nullptr)
            {
                Throw(std::runtime_error("failed to allocate thumbnail RenderView bank"));
            }
            thumbnailRenderView_->CreateSwapChain(renderer_.SwapChain());
            thumbnailRenderView_->SetCopyObjectIdHistory(false);
        }
        thumbnailRenderView_->SetRenderExtent(kThumbnailExtent);
        thumbnailRenderView_->SetCopyObjectIdHistory(false);

        if (thumbnailRenderView_->AllocatedExtent().width != kThumbnailExtent.width ||
            thumbnailRenderView_->AllocatedExtent().height != kThumbnailExtent.height ||
            thumbnailRenderView_->VisibilityFramebuffer() == nullptr)
        {
            RenderViewResourceFactory resources(renderer_);
            thumbnailVisibilityFrameBuffer_ = resources.RebuildVisibilityFramebuffer(
                *thumbnailRenderView_,
                kThumbnailExtent);
        }
        if (!thumbnailSampler_)
        {
            RenderViewResourceFactory resources(renderer_);
            thumbnailSampler_ = resources.CreateClampSampler();
        }
    }

    void AssetThumbnailRenderer::RebuildMeshThumbnailScene(const Assets::Model& model)
    {
        meshThumbnailScene_ = std::make_unique<Assets::Scene>(
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

        meshThumbnailScene_->Reload(nodes, models, materials, lights, tracks);
        meshThumbnailScene_->PostLoad(skeletons);
        meshThumbnailScene_->RebuildMeshBuffer(renderer_.CommandPool(), false);

        Assets::EnvironmentSetting env;
        env.Reset();
        env.HasSky = true;
        env.HasSun = false;
        env.SunIntensity = 500.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 300.0f;
        meshThumbnailScene_->SetEnvSettings(env);

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
        meshThumbnailScene_->SetRenderCamera(camera);
        meshThumbnailScene_->UpdateNodes();
    }

    void AssetThumbnailRenderer::CopyThumbnailViewOutput(
        VkCommandBuffer commandBuffer,
        RenderView& view,
        RenderImage& dst)
    {
        const RenderImage* src = renderer_.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_DENOISED);
        if (!src)
        {
            return;
        }

        const VkExtent2D extent = view.RenderExtent();
        src->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        dst.InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[0] = {0, 0, 0};
        region.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[0] = {0, 0, 0};
        region.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        vkCmdBlitImage(commandBuffer,
                       src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst.GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region, VK_FILTER_LINEAR);

        dst.InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        src->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    bool AssetThumbnailRenderer::ScheduleNextMaterialThumbnail(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        if (pendingMaterialThumbnails_.empty())
        {
            return false;
        }

        auto mainScene = renderer_.scene_.lock();
        if (!mainScene)
        {
            pendingMaterialThumbnails_.clear();
            return false;
        }

        const uint32_t materialIndex = pendingMaterialThumbnails_.front();
        pendingMaterialThumbnails_.erase(pendingMaterialThumbnails_.begin());
        if (materialIndex >= mainScene->Materials().size() || materialIndex >= kMaterialThumbnailMaxSlots)
        {
            return false;
        }

        if (!materialThumbnailSceneReady_)
        {
            return false;
        }
        if (!materialThumbnailScene_ || materialThumbnailScene_->Materials().empty())
        {
            return false;
        }

        EnsureThumbnailRenderTarget();
        assert(thumbnailRenderView_ != nullptr);

        const uint32_t sampleSlot = kMaterialThumbnailSampleSlotBase + materialIndex;

        if (materialThumbnailImages_.size() <= materialIndex)
        {
            materialThumbnailImages_.resize(static_cast<size_t>(materialIndex) + 1);
        }
        if (!materialThumbnailImages_[materialIndex])
        {
            const std::string debugName = fmt::format("Material Thumbnail {}", materialIndex);
            RenderViewResourceFactory resources(renderer_);
            materialThumbnailImages_[materialIndex] = resources.CreateSampledColorImage(
                kThumbnailExtent,
                debugName.c_str());
            resources.BindSampledColorImage(sampleSlot, *materialThumbnailImages_[materialIndex], *thumbnailSampler_);
        }

        materialThumbnailScene_->Materials()[0] = mainScene->Materials()[materialIndex];
        materialThumbnailScene_->Materials()[0].name_ = "__material_thumbnail_preview";

        const auto rendererIt = renderer_.logicRenderers_.renderers.find(ERT_SoftwareModernNoAmbient);
        const auto fallbackIt = renderer_.logicRenderers_.renderers.find(renderer_.logicRenderers_.current);
        if (rendererIt == renderer_.logicRenderers_.renderers.end() &&
            fallbackIt == renderer_.logicRenderers_.renderers.end())
        {
            return false;
        }

        materialThumbnailScene_->UpdateAllMaterials();
        materialThumbnailScene_->UpdateNodes();
        const Assets::UniformBufferObject previewCamera = BuildViewCameraUbo({
            .scene = *materialThumbnailScene_,
            .camera = materialThumbnailScene_->GetRenderCamera(),
            .extent = kThumbnailExtent,
            .cascadeDistance = 20.0f,
            .totalFrames = static_cast<uint32_t>(std::max(renderer_.frame_.frameCount, 1)),
            .fillSceneLighting = true,
            .thumbnailDefaults = true,
        });

        thumbnailRenderView_->SetDebugName("material thumbnail view");
        thumbnailRenderView_->SetRenderExtent(kThumbnailExtent);
        renderer_.SetRenderViewUbo(*thumbnailRenderView_, imageIndex, previewCamera);
        thumbnailRenderView_->SetVisibilityFramebuffer(thumbnailVisibilityFrameBuffer_.get());
        thumbnailRenderView_->SetSceneOverride(materialThumbnailScene_.get());
        thumbnailRenderView_->SetPrevDepthValid(false);
        renderer_.ScheduleRenderView(
            *thumbnailRenderView_,
            *(rendererIt != renderer_.logicRenderers_.renderers.end() ? rendererIt : fallbackIt)->second,
            /*clearSwapchain*/ false,
            [this, commandBuffer, materialIndex](RenderView& view)
            {
                if (materialIndex < materialThumbnailImages_.size() && materialThumbnailImages_[materialIndex])
                {
                    CopyThumbnailViewOutput(commandBuffer, view, *materialThumbnailImages_[materialIndex]);
                }
                view.SetSceneOverride(nullptr);
            });

        return true;
    }

    bool AssetThumbnailRenderer::ScheduleNextMeshThumbnail(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        if (pendingMeshThumbnails_.empty())
        {
            return false;
        }

        auto mainScene = renderer_.scene_.lock();
        if (!mainScene)
        {
            pendingMeshThumbnails_.clear();
            return false;
        }

        const uint32_t modelIndex = pendingMeshThumbnails_.front();
        pendingMeshThumbnails_.erase(pendingMeshThumbnails_.begin());
        if (modelIndex >= mainScene->Models().size() || modelIndex >= kMeshThumbnailMaxSlots)
        {
            return false;
        }

        const Assets::Model& model = mainScene->Models()[modelIndex];
        if (model.NumberOfVertices() == 0)
        {
            return false;
        }

        EnsureThumbnailRenderTarget();
        assert(thumbnailRenderView_ != nullptr);
        RebuildMeshThumbnailScene(model);
        if (!meshThumbnailScene_)
        {
            return false;
        }

        const uint32_t sampleSlot = kMeshThumbnailSampleSlotBase + modelIndex;

        if (meshThumbnailImages_.size() <= modelIndex)
        {
            meshThumbnailImages_.resize(static_cast<size_t>(modelIndex) + 1);
        }
        if (!meshThumbnailImages_[modelIndex])
        {
            const std::string debugName = fmt::format("Mesh Thumbnail {}", modelIndex);
            RenderViewResourceFactory resources(renderer_);
            meshThumbnailImages_[modelIndex] = resources.CreateSampledColorImage(
                kThumbnailExtent,
                debugName.c_str());
            resources.BindSampledColorImage(sampleSlot, *meshThumbnailImages_[modelIndex], *thumbnailSampler_);
        }

        const auto rendererIt = renderer_.logicRenderers_.renderers.find(ERT_SoftwareModernNoAmbient);
        const auto fallbackIt = renderer_.logicRenderers_.renderers.find(renderer_.logicRenderers_.current);
        if (rendererIt == renderer_.logicRenderers_.renderers.end() &&
            fallbackIt == renderer_.logicRenderers_.renderers.end())
        {
            return false;
        }

        meshThumbnailScene_->UpdateAllMaterials();
        meshThumbnailScene_->UpdateNodes();
        const Assets::UniformBufferObject previewCamera = BuildViewCameraUbo({
            .scene = *meshThumbnailScene_,
            .camera = meshThumbnailScene_->GetRenderCamera(),
            .extent = kThumbnailExtent,
            .cascadeDistance = 20.0f,
            .totalFrames = static_cast<uint32_t>(std::max(renderer_.frame_.frameCount, 1)),
            .fillSceneLighting = true,
            .thumbnailDefaults = true,
        });

        thumbnailRenderView_->SetDebugName("mesh thumbnail view");
        thumbnailRenderView_->SetRenderExtent(kThumbnailExtent);
        renderer_.SetRenderViewUbo(*thumbnailRenderView_, imageIndex, previewCamera);
        thumbnailRenderView_->SetVisibilityFramebuffer(thumbnailVisibilityFrameBuffer_.get());
        thumbnailRenderView_->SetSceneOverride(meshThumbnailScene_.get());
        thumbnailRenderView_->SetPrevDepthValid(false);
        renderer_.ScheduleRenderView(
            *thumbnailRenderView_,
            *(rendererIt != renderer_.logicRenderers_.renderers.end() ? rendererIt : fallbackIt)->second,
            /*clearSwapchain*/ false,
            [this, commandBuffer, modelIndex](RenderView& view)
            {
                if (modelIndex < meshThumbnailImages_.size() && meshThumbnailImages_[modelIndex])
                {
                    CopyThumbnailViewOutput(commandBuffer, view, *meshThumbnailImages_[modelIndex]);
                }
                view.SetSceneOverride(nullptr);
            });

        return true;
    }
}
