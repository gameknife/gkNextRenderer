#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/MaterialPreviewRenderer.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Rendering/RenderViewResourceFactory.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

#include <cmath>
#include <cstring>

namespace Vulkan
{
    MaterialPreviewRenderer::MaterialPreviewRenderer(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
        previewMaterial_.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.75f));
        previewMaterial_.name_ = "__material_preview";
    }

    MaterialPreviewRenderer::~MaterialPreviewRenderer() = default;

    void MaterialPreviewRenderer::SetEnabled(const bool enabled)
    {
        enabled_ = enabled;
        if (!enabled_ && previewView_ != nullptr)
        {
            previewView_->SetSceneOverride(nullptr);
        }
    }

    void MaterialPreviewRenderer::SetRenderExtent(VkExtent2D extent)
    {
        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);
        if (requestedExtent_.width == extent.width && requestedExtent_.height == extent.height)
        {
            return;
        }

        requestedExtent_ = extent;
        if (previewView_ != nullptr)
        {
            previewView_->SetRenderExtent(requestedExtent_);
            previewView_->InvalidateTemporalHistory();
        }
    }

    void MaterialPreviewRenderer::SetPreviewMaterial(const Assets::FMaterial& material)
    {
        if (previewMaterial_.name_ == "__material_preview" &&
            std::memcmp(&previewMaterial_.gpuMaterial_, &material.gpuMaterial_, sizeof(Assets::Material)) == 0)
        {
            return;
        }
        previewMaterial_ = material;
        previewMaterial_.name_ = "__material_preview";
        materialDirty_ = true;
        if (previewView_ != nullptr)
        {
            previewView_->InvalidateTemporalHistory();
        }
    }

    void MaterialPreviewRenderer::SetCameraOrbit(float yawRadians, float pitchRadians, float distance)
    {
        pitchRadians = std::clamp(pitchRadians, -1.2f, 1.2f);
        distance = std::clamp(distance, 1.8f, 8.0f);
        if (std::abs(cameraYaw_ - yawRadians) < 0.0001f &&
            std::abs(cameraPitch_ - pitchRadians) < 0.0001f &&
            std::abs(cameraDistance_ - distance) < 0.0001f)
        {
            return;
        }

        cameraYaw_ = yawRadians;
        cameraPitch_ = pitchRadians;
        cameraDistance_ = distance;
        if (previewScene_)
        {
            Assets::Camera camera = previewScene_->GetRenderCamera();
            const glm::vec3 eye(
                std::sin(cameraYaw_) * std::cos(cameraPitch_) * cameraDistance_,
                std::sin(cameraPitch_) * cameraDistance_,
                std::cos(cameraYaw_) * std::cos(cameraPitch_) * cameraDistance_);
            camera.ModelView = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            camera.FocalDistance = cameraDistance_;
            previewScene_->SetRenderCamera(camera);
        }
        if (previewView_ != nullptr)
        {
            previewView_->InvalidateTemporalHistory();
        }
    }

    void MaterialPreviewRenderer::BeforeNextFrame()
    {
        if (enabled_)
        {
            EnsurePreviewScene();
        }
    }

    void MaterialPreviewRenderer::OnMainSceneChanged()
    {
        materialDirty_ = true;
        if (previewView_ != nullptr)
        {
            previewView_->InvalidateTemporalHistory();
            previewView_->SetSceneOverride(nullptr);
        }
    }

    void MaterialPreviewRenderer::OnHdrShUpdated()
    {
        if (previewScene_ != nullptr)
        {
            previewScene_->UpdateHDRSH();
        }
        if (previewView_ != nullptr)
        {
            previewView_->InvalidateTemporalHistory();
        }
    }

    void MaterialPreviewRenderer::OnSwapChainResourcesInvalidated()
    {
        target_.ResetSwapChainResources(/*releaseSampledOutput*/ false);
        if (previewView_ != nullptr)
        {
            previewView_->SetSceneOverride(nullptr);
        }
    }

    void MaterialPreviewRenderer::EnsurePreviewScene()
    {
        if (sceneReady_)
        {
            return;
        }

        previewScene_ = std::make_unique<Assets::Scene>(
            renderer_.CommandPool(), false, /*allocateAmbientResources*/ false, /*enableCpuAcceleration*/ false);

        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
        materials.push_back(previewMaterial_);
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "__MaterialPreviewSphere",
            glm::vec3(0.0f),
            glm::vec3(1.0f),
            1u,
            0u,
            0u,
            true));

        previewScene_->Reload(nodes, models, materials, lights, tracks);
        previewScene_->PostLoad(skeletons);
        previewScene_->RebuildMeshBuffer(renderer_.CommandPool(), false);

        Assets::EnvironmentSetting env;
        env.Reset();
        env.HasSky = true;
        env.HasSun = false;
        env.SunIntensity = 1000.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 300.0f;
        previewScene_->SetEnvSettings(env);

        Assets::Camera camera{};
        camera.name = "Material Preview";
        camera.FieldOfView = 38.0f;
        camera.Aperture = 0.0f;
        camera.FocalDistance = 3.0f;
        camera.NearPlane = 0.01f;
        camera.FarPlane = 20.0f;
        const glm::vec3 eye(
            std::sin(cameraYaw_) * std::cos(cameraPitch_) * cameraDistance_,
            std::sin(cameraPitch_) * cameraDistance_,
            std::cos(cameraYaw_) * std::cos(cameraPitch_) * cameraDistance_);
        camera.ModelView = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        previewScene_->SetRenderCamera(camera);
        previewScene_->UpdateNodes();

        sceneReady_ = true;
        materialDirty_ = true;
    }

    void MaterialPreviewRenderer::EnsureRenderTarget()
    {
        EnsurePreviewScene();
        if (previewView_ == nullptr)
        {
            FViewDesc viewDesc{};
            viewDesc.renderExtent = requestedExtent_;
            viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
            viewDesc.schedule = EViewSchedule::Persistent;
            previewView_ = renderer_.renderViews_->CreateView(viewDesc, "material preview view");
            if (previewView_ == nullptr)
            {
                Throw(std::runtime_error("failed to allocate material preview RenderView bank"));
            }
            previewView_->CreateSwapChain(renderer_.SwapChain());
            previewView_->SetCopyObjectIdHistory(false);
        }

        previewView_->SetDebugName("material preview view");
        previewView_->SetRenderExtent(requestedExtent_);
        previewView_->SetCopyObjectIdHistory(false);

        if (previewView_->AllocatedExtent().width == requestedExtent_.width &&
            previewView_->AllocatedExtent().height == requestedExtent_.height &&
            previewView_->VisibilityFramebuffer() != nullptr &&
            target_.offscreenImage != nullptr)
        {
            return;
        }

        target_.visibilityFramebuffer.reset();
        target_.offscreenImage.reset();
        RenderViewResourceFactory resources(renderer_);
        target_.visibilityFramebuffer = resources.RebuildVisibilityFramebuffer(*previewView_, requestedExtent_);
        target_.offscreenImage = resources.CreateSampledColorImage(requestedExtent_, "Material Preview Offscreen");
        if (!target_.offscreenSampler)
        {
            target_.offscreenSampler = resources.CreateClampSampler();
        }
        target_.outputSampleSlot = SampleSlot();
        resources.BindSampledColorImage(target_.outputSampleSlot, *target_.offscreenImage, *target_.offscreenSampler);
        previewView_->InvalidateTemporalHistory();
    }

    void MaterialPreviewRenderer::CopyPreviewOutput(VkCommandBuffer commandBuffer, RenderView& view)
    {
        const RenderImage* src = renderer_.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_DENOISED);
        if (!src || !target_.offscreenImage)
        {
            return;
        }

        const VkExtent2D extent = view.RenderExtent();
        src->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        target_.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT,
                                              VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[0] = {0, 0, 0};
        region.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[0] = {0, 0, 0};
        region.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        vkCmdBlitImage(commandBuffer,
                       src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       target_.offscreenImage->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region, VK_FILTER_LINEAR);

        target_.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                                              VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        src->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    bool MaterialPreviewRenderer::ScheduleView(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (!enabled_)
        {
            return false;
        }

        EnsureRenderTarget();
        if (!previewScene_ || !previewView_)
        {
            return false;
        }

        if (materialDirty_)
        {
            if (previewScene_->Materials().empty())
            {
                previewScene_->Materials().push_back(previewMaterial_);
            }
            else
            {
                previewScene_->Materials()[0] = previewMaterial_;
            }
            previewScene_->UpdateAllMaterials();
            previewScene_->UpdateNodes();
            materialDirty_ = false;
        }

        LogicRendererBase* logicRenderer = renderer_.EnsureLogicRenderer(ERT_SoftwareModernNoAmbient);
        if (logicRenderer == nullptr)
        {
            logicRenderer = renderer_.EnsureLogicRenderer(renderer_.logicRenderers_.current);
        }
        if (logicRenderer == nullptr)
        {
            return false;
        }

        previewScene_->UpdateHDRSH();
        const Assets::UniformBufferObject previewCamera = BuildViewCameraUbo({
            .scene = *previewScene_,
            .camera = previewScene_->GetRenderCamera(),
            .extent = requestedExtent_,
            .cascadeDistance = 20.0f,
            .totalFrames = static_cast<uint32_t>(std::max(renderer_.frame_.frameCount, 1)),
            .fillSceneLighting = true,
            .thumbnailDefaults = true,
        });

        renderer_.SetRenderViewUbo(*previewView_, imageIndex, previewCamera);
        previewView_->SetVisibilityFramebuffer(target_.visibilityFramebuffer.get());
        previewView_->SetSceneOverride(previewScene_.get());
        previewView_->SetPrevDepthValid(false);
        renderer_.ScheduleRenderView(
            *previewView_,
            *logicRenderer,
            /*clearSwapchain*/ false,
            [this, commandBuffer](RenderView& view)
            {
                CopyPreviewOutput(commandBuffer, view);
                view.SetSceneOverride(nullptr);
            });

        return true;
    }
}
