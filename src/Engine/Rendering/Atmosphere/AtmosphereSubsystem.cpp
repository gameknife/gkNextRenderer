#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Atmosphere/AtmosphereSubsystem.hpp"
#include "Engine/Rendering/Atmosphere/SkyIrradianceProjector.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <cstring>

namespace Rendering::Atmosphere
{
    namespace
    {
        constexpr uint32_t transmittanceSlot =
            static_cast<uint32_t>(Assets::Bindless::RES_ATMOSPHERE_TRANSMITTANCE);
        constexpr uint32_t multiScatterSlot =
            static_cast<uint32_t>(Assets::Bindless::RES_ATMOSPHERE_MULTI_SCATTER);
        constexpr uint32_t skyViewSlot =
            static_cast<uint32_t>(Assets::Bindless::RES_ATMOSPHERE_SKY_VIEW);
        constexpr uint32_t aerialPerspectiveSlot =
            static_cast<uint32_t>(Assets::Bindless::RES_ATMOSPHERE_AERIAL_PERSPECTIVE);

        constexpr VkExtent2D transmittanceExtent{256, 64};
        constexpr VkExtent2D multiScatterExtent{32, 32};
        constexpr VkExtent2D baseSkyViewExtent{192, 108};
        constexpr VkExtent3D aerialPerspectiveExtent{32, 32, 32};

        VkExtent2D ScaledSkyViewExtent(float scale)
        {
            scale = std::clamp(scale, 0.25f, 2.0f);
            return {
                std::max(1u, static_cast<uint32_t>(
                    std::lround(static_cast<float>(baseSkyViewExtent.width) * scale))),
                std::max(1u, static_cast<uint32_t>(
                    std::lround(static_cast<float>(baseSkyViewExtent.height) * scale))),
            };
        }

        std::unique_ptr<Vulkan::RenderImage> CreateLut2D(
            Vulkan::VulkanBaseRenderer& renderer, VkExtent2D extent, const char* name)
        {
            return std::make_unique<Vulkan::RenderImage>(
                renderer.Device(), VkExtent3D{extent.width, extent.height, 1}, VK_IMAGE_TYPE_2D,
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, name,
                Vulkan::SamplerConfig::VolumeLut());
        }
    }

    AtmosphereSubsystem::AtmosphereSubsystem(Vulkan::VulkanBaseRenderer& renderer) : renderer_(renderer)
    {
    }

    AtmosphereSubsystem::~AtmosphereSubsystem()
    {
        DestroySwapChainPipelines();
        DestroyDeviceResources();
    }

    void AtmosphereSubsystem::CreateDeviceResources()
    {
        if (paramsBuffer_)
        {
            return;
        }

        paramsBuffer_ = std::make_unique<Vulkan::Buffer>(
            renderer_.Device(), sizeof(Assets::FAtmosphereParams),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        paramsMemory_ = std::make_unique<Vulkan::DeviceMemory>(paramsBuffer_->AllocateMemory(
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            {.AllocateFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT}));
        renderer_.Device().DebugUtils().SetObjectName(paramsBuffer_->Handle(), "Atmosphere Params Buffer");
        paramsMemory_->SetName("Atmosphere Params Memory");

        transmittanceLut_ = CreateLut2D(renderer_, transmittanceExtent, "Atmosphere Transmittance LUT");
        multiScatterLut_ = CreateLut2D(renderer_, multiScatterExtent, "Atmosphere Multi Scatter LUT");
        CreateSkyViewLut(ScaledSkyViewExtent(
            renderer_.FrameSettings().userSettings.AtmosphereSkyViewLutScale));

        auto* texturePool = Assets::GlobalTexturePool::GetInstance();
        texturePool->BindStorageTexture(transmittanceSlot, transmittanceLut_->GetImageView());
        texturePool->BindSampleTexture(transmittanceSlot, transmittanceLut_->GetImageView(), transmittanceLut_->Sampler());
        texturePool->BindStorageTexture(multiScatterSlot, multiScatterLut_->GetImageView());
        texturePool->BindSampleTexture(multiScatterSlot, multiScatterLut_->GetImageView(), multiScatterLut_->Sampler());
        renderer_.CreateStorageImage3D(
            aerialPerspectiveSlot, aerialPerspectiveExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            "Atmosphere Aerial Perspective LUT", Vulkan::SamplerConfig::VolumeLut());

        staticLutsDirty_ = true;
        aerialPerspectiveReady_ = false;
        haveLastParams_ = false;
        WriteParams(BuildParams());
    }

    void AtmosphereSubsystem::CreateSkyViewLut(VkExtent2D extent)
    {
        skyViewExtent_ = extent;
        skyViewLut_ = CreateLut2D(renderer_, skyViewExtent_, "Atmosphere Sky View LUT");
        auto* texturePool = Assets::GlobalTexturePool::GetInstance();
        texturePool->BindStorageTexture(skyViewSlot, skyViewLut_->GetImageView());
        texturePool->BindSampleTexture(
            skyViewSlot, skyViewLut_->GetImageView(), skyViewLut_->Sampler());
    }

    void AtmosphereSubsystem::SyncRuntimeResources(bool deviceIsIdle)
    {
        if (!paramsBuffer_)
        {
            return;
        }
        const VkExtent2D desiredExtent = ScaledSkyViewExtent(
            renderer_.FrameSettings().userSettings.AtmosphereSkyViewLutScale);
        if (desiredExtent.width == skyViewExtent_.width &&
            desiredExtent.height == skyViewExtent_.height)
        {
            return;
        }

        if (!deviceIsIdle)
        {
            renderer_.RequestRecreateSwapChain();
            return;
        }

        skyViewLut_.reset();
        lutStates_.Reset();
        CreateSkyViewLut(desiredExtent);
        staticLutsDirty_ = true;
        aerialPerspectiveReady_ = false;
        SPDLOG_INFO("Atmosphere SkyView LUT resized to {}x{}",
                    skyViewExtent_.width, skyViewExtent_.height);
    }

    void AtmosphereSubsystem::DestroyDeviceResources()
    {
        if (!paramsBuffer_)
        {
            return;
        }
        if (renderer_.Device().Handle() != VK_NULL_HANDLE)
        {
            renderer_.DestroyStorageImage3D(aerialPerspectiveSlot);
        }
        skyViewLut_.reset();
        skyViewExtent_ = {};
        multiScatterLut_.reset();
        transmittanceLut_.reset();
        paramsBuffer_.reset();
        paramsMemory_.reset();
        lutStates_.Reset();
        haveLastParams_ = false;
        aerialPerspectiveReady_ = false;
    }

    void AtmosphereSubsystem::CreateSwapChainPipelines()
    {
        if (!paramsBuffer_ || transmittancePipeline_)
        {
            return;
        }
        auto& swapChain = renderer_.SwapChain();
        auto& scene = renderer_.GetScene();
        transmittancePipeline_ = std::make_unique<Vulkan::PipelineCommon::ZeroBindPipeline>(
            swapChain, "assets/shaders/Sky.Transmittance.comp.slang.spv", scene);
        multiScatterPipeline_ = std::make_unique<Vulkan::PipelineCommon::ZeroBindPipeline>(
            swapChain, "assets/shaders/Sky.MultiScatter.comp.slang.spv", scene);
        skyViewPipeline_ = std::make_unique<Vulkan::PipelineCommon::ZeroBindPipeline>(
            swapChain, "assets/shaders/Sky.SkyView.comp.slang.spv", scene);
        aerialPerspectivePipeline_ = std::make_unique<Vulkan::PipelineCommon::ZeroBindPipeline>(
            swapChain, "assets/shaders/Sky.AerialPerspective.comp.slang.spv", scene);
        compositePipeline_ = std::make_unique<Vulkan::PipelineCommon::ZeroBindPipeline>(
            swapChain, "assets/shaders/Process.AtmosphereComposite.comp.slang.spv", scene);
    }

    void AtmosphereSubsystem::DestroySwapChainPipelines()
    {
        compositePipeline_.reset();
        aerialPerspectivePipeline_.reset();
        skyViewPipeline_.reset();
        multiScatterPipeline_.reset();
        transmittancePipeline_.reset();
    }

    bool AtmosphereSubsystem::Enabled() const
    {
        const auto& settings = renderer_.FrameSettings().userSettings;
        return settings.AtmosphereEnable || settings.AtmosphereHeightFog;
    }

    VkDeviceAddress AtmosphereSubsystem::ParamsAddress() const
    {
        return Enabled() && paramsBuffer_ ? paramsBuffer_->GetDeviceAddress() : 0;
    }

    Assets::FAtmosphereParams AtmosphereSubsystem::BuildParams() const
    {
        const auto& source = renderer_.GetScene().GetEnvSettings().Atmosphere;
        const auto& runtime = renderer_.FrameSettings().userSettings;
        Assets::FAtmosphereParams params{};
        params.RayleighScattering = source.RayleighScattering;
        params.RayleighDensityH = source.RayleighDensityH;
        params.MieScattering = source.MieScattering;
        params.MieDensityH = source.MieDensityH;
        params.MieAbsorption = source.MieAbsorption;
        params.MiePhaseG = source.MiePhaseG;
        params.OzoneAbsorption = source.OzoneAbsorption;
        params.OzoneCenterAltitude = source.OzoneCenterAltitude;
        params.GroundAlbedo = source.GroundAlbedo;
        params.OzoneWidth = source.OzoneWidth;
        params.BottomRadius = source.BottomRadius;
        params.TopRadius = source.TopRadius;
        params.WorldUnitsPerKm = std::max(source.WorldUnitsPerKm, 0.001f);
        params.WorldOriginAltitude = source.WorldOriginAltitude;
        params.TransmittanceLutId = transmittanceSlot;
        params.MultiScatterLutId = multiScatterSlot;
        params.SkyViewLutId = skyViewSlot;
        params.AerialPerspectiveLutId = aerialPerspectiveSlot;
        params.AerialPerspectiveMaxDistance = std::max(source.AerialPerspectiveMaxDistance, 1.0f);
        params.SkyLuminanceScale = std::max(source.SkyLuminanceScale, 0.0f);
        params.Flags = 0;
        if (runtime.AtmosphereEnable)
        {
            params.Flags |= Assets::ATMOSPHERE_FLAG_SKY;
            if (runtime.AtmosphereAerialPerspective)
            {
                params.Flags |= Assets::ATMOSPHERE_FLAG_AERIAL_PERSPECTIVE;
            }
        }
        if (runtime.AtmosphereHeightFog)
        {
            params.Flags |= Assets::ATMOSPHERE_FLAG_HEIGHT_FOG;
        }
        params.FogInscatteringColor = source.FogInscatteringColor;
        params.FogDensity = std::max(source.FogDensity, 0.0f);
        params.FogHeightFalloff = std::max(source.FogHeightFalloff, 0.0f);
        params.FogBaseHeight = source.FogBaseHeight;
        params.FogStartDistance = std::max(source.FogStartDistance, 0.0f);
        params.FogMaxOpacity = std::clamp(source.FogMaxOpacity, 0.0f, 1.0f);
        return params;
    }

    void AtmosphereSubsystem::WriteParams(const Assets::FAtmosphereParams& params)
    {
        if (!paramsMemory_)
        {
            return;
        }
        void* data = paramsMemory_->Map(0, sizeof(params));
        std::memcpy(data, &params, sizeof(params));
        paramsMemory_->Unmap();
    }

    void AtmosphereSubsystem::TransitionImage(
        VkCommandBuffer commandBuffer, Vulkan::RenderImage& image, uint32_t slot,
        Vulkan::PipelineCommon::ERenderStage stages, Vulkan::PipelineCommon::EResourceAccess access,
        VkImageLayout layout, bool discard, std::string_view passName)
    {
        const auto barrier = lutStates_.Use({
            .image = {static_cast<uint64_t>(slot) + 1u},
            .stages = stages,
            .access = access,
            .layout = layout,
            .discardPreviousContents = discard,
        }, passName);
        if (!barrier)
        {
            return;
        }

        VkImageMemoryBarrier vkBarrier{};
        vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        vkBarrier.srcAccessMask = barrier->srcAccess;
        vkBarrier.dstAccessMask = barrier->dstAccess;
        vkBarrier.oldLayout = barrier->oldLayout;
        vkBarrier.newLayout = barrier->newLayout;
        vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkBarrier.image = image.GetImage().Handle();
        vkBarrier.subresourceRange = barrier->range;
        vkCmdPipelineBarrier(commandBuffer, barrier->srcStages, barrier->dstStages, 0,
                             0, nullptr, 0, nullptr, 1, &vkBarrier);
    }

    void AtmosphereSubsystem::Dispatch2D(
        VkCommandBuffer commandBuffer, uint32_t imageIndex,
        Vulkan::PipelineCommon::ZeroBindPipeline& pipeline, Vulkan::RenderImage& target,
        uint32_t slot, VkExtent2D extent, std::string_view passName)
    {
        TransitionImage(commandBuffer, target, slot,
                        Vulkan::PipelineCommon::ERenderStage::Compute,
                        Vulkan::PipelineCommon::EResourceAccess::ShaderWrite,
                        VK_IMAGE_LAYOUT_GENERAL, true, passName);
        pipeline.BindPipeline(commandBuffer, renderer_.GetScene(), imageIndex);
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                      Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
        TransitionImage(commandBuffer, target, slot,
                        Vulkan::PipelineCommon::ERenderStage::Compute,
                        Vulkan::PipelineCommon::EResourceAccess::ShaderRead,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, passName);
    }

    void AtmosphereSubsystem::BeginSceneFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!paramsBuffer_ || !skyViewPipeline_)
        {
            return;
        }

        const Assets::FAtmosphereParams params = BuildParams();
        if (!haveLastParams_ || std::memcmp(&params, &lastParams_, sizeof(params)) != 0)
        {
            staticLutsDirty_ = true;
            lastParams_ = params;
            haveLastParams_ = true;
        }
        WriteParams(params);

        if ((params.Flags & Assets::ATMOSPHERE_FLAG_SKY) == 0)
        {
            aerialPerspectiveReady_ = false;
            return;
        }
        const auto& environment = renderer_.GetScene().GetEnvSettings();
        const glm::vec3 sunDirection = environment.SunDirection();
        const bool sunMoved = !haveProjectedSky_ ||
            glm::dot(glm::normalize(lastProjectedSunDirection_), glm::normalize(sunDirection)) <
                std::cos(glm::radians(0.5f));
        if (staticLutsDirty_ || sunMoved)
        {
            constexpr uint32_t atmosphereShSlot = Assets::MAX_HDR_SH - 1;
            auto* texturePool = Assets::GlobalTexturePool::GetInstance();
            auto& sh = texturePool->GetHDRSphericalHarmonics();
            const glm::vec3 cameraWorld = glm::vec3(renderer_.LastUniformBufferObject().ModelViewInverse[3]);
            const float cameraAltitudeKm =
                params.WorldOriginAltitude + cameraWorld.y / std::max(params.WorldUnitsPerKm, 0.001f);
            const glm::vec3 sunTransmittance =
                TransmittanceToSun(cameraAltitudeKm, sunDirection.y);
            sh[atmosphereShSlot] = SkyIrradianceProjector::Project(
                params, sunDirection,
                environment.SunColor * environment.SunIntensity * sunTransmittance,
                cameraAltitudeKm);
            renderer_.GetScene().UpdateHDRSH();
            lastProjectedSunDirection_ = sunDirection;
            haveProjectedSky_ = true;
        }
        if (staticLutsDirty_)
        {
            Dispatch2D(commandBuffer, imageIndex, *transmittancePipeline_, *transmittanceLut_,
                       transmittanceSlot, transmittanceExtent, "Atmosphere Transmittance");
            Dispatch2D(commandBuffer, imageIndex, *multiScatterPipeline_, *multiScatterLut_,
                       multiScatterSlot, multiScatterExtent, "Atmosphere Multi Scatter");
            staticLutsDirty_ = false;
        }
        Dispatch2D(commandBuffer, imageIndex, *skyViewPipeline_, *skyViewLut_,
                   skyViewSlot, skyViewExtent_, "Atmosphere Sky View");
        aerialPerspectiveReady_ = false;
    }

    void AtmosphereSubsystem::PrepareView(
        VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView,
        const Vulkan::FRendererContract& contract)
    {
        const Assets::FAtmosphereParams params = BuildParams();
        if (!isPrimaryView || !aerialPerspectivePipeline_ ||
            (params.Flags & Assets::ATMOSPHERE_FLAG_AERIAL_PERSPECTIVE) == 0 ||
            !Vulkan::HasAll(contract.outputs, Vulkan::ERenderOutput::Color | Vulkan::ERenderOutput::Depth))
        {
            return;
        }

        auto* volume = const_cast<Vulkan::RenderImage*>(renderer_.GetStorageImage3D(aerialPerspectiveSlot));
        if (!volume)
        {
            return;
        }
        TransitionImage(commandBuffer, *volume, aerialPerspectiveSlot,
                        Vulkan::PipelineCommon::ERenderStage::Compute,
                        Vulkan::PipelineCommon::EResourceAccess::ShaderWrite,
                        VK_IMAGE_LAYOUT_GENERAL, true, "Atmosphere Aerial Perspective");
        aerialPerspectivePipeline_->BindPipeline(
            commandBuffer, renderer_.GetScene(), imageIndex, renderer_.ActiveViewBankBase(), 0, 0);
        vkCmdDispatch(commandBuffer, 4, 4, 32);
        TransitionImage(commandBuffer, *volume, aerialPerspectiveSlot,
                        Vulkan::PipelineCommon::ERenderStage::Compute,
                        Vulkan::PipelineCommon::EResourceAccess::ShaderRead,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false, "Atmosphere Aerial Perspective");
        aerialPerspectiveReady_ = true;
    }

    void AtmosphereSubsystem::ApplyToView(
        VkCommandBuffer commandBuffer, uint32_t imageIndex, bool isPrimaryView,
        const Vulkan::FRendererContract& contract)
    {
        if (!Enabled() || !compositePipeline_ ||
            !Vulkan::HasAll(contract.outputs, Vulkan::ERenderOutput::Color | Vulkan::ERenderOutput::Depth))
        {
            return;
        }
        renderer_.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SCENE_COLOR,
             Vulkan::PipelineCommon::ERenderStage::Compute,
             Vulkan::PipelineCommon::EResourceAccess::ShaderRead |
                 Vulkan::PipelineCommon::EResourceAccess::ShaderWrite,
             VK_IMAGE_LAYOUT_GENERAL},
            {Assets::Bindless::RT_PREV_DEPTHBUFFER,
             Vulkan::PipelineCommon::ERenderStage::Compute,
             Vulkan::PipelineCommon::EResourceAccess::ShaderRead,
             VK_IMAGE_LAYOUT_GENERAL},
        }, "Atmosphere Composite");

        const uint32_t hasAerialPerspective = isPrimaryView && aerialPerspectiveReady_ ? 1u : 0u;
        const uint32_t debugMode = static_cast<uint32_t>(
            std::clamp(renderer_.FrameSettings().userSettings.AtmosphereDebugMode, 0, 3));
        compositePipeline_->BindPipeline(
            commandBuffer, renderer_.GetScene(), imageIndex, renderer_.ActiveViewBankBase(),
            hasAerialPerspective, debugMode);
        const VkExtent2D extent = renderer_.ActiveViewRenderExtent();
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(extent.width, 8),
                      Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
    }

    glm::vec3 AtmosphereSubsystem::TransmittanceToSun(
        float cameraAltitudeKm, float sunZenithCosine) const
    {
        const auto& atmosphere = renderer_.GetScene().GetEnvSettings().Atmosphere;
        const float altitude = std::max(cameraAltitudeKm, 0.0f);
        const float mu = std::clamp(sunZenithCosine, -1.0f, 1.0f);
        if (mu <= -0.15f)
        {
            return glm::vec3(0.0f);
        }
        const float airMass = 1.0f / std::max(
            mu + 0.50572f * std::pow(std::max(6.07995f + glm::degrees(std::asin(mu)), 0.01f), -1.6364f),
            0.02f);
        const float rayleighDensity = std::exp(-altitude / std::max(atmosphere.RayleighDensityH, 0.001f));
        const float mieDensity = std::exp(-altitude / std::max(atmosphere.MieDensityH, 0.001f));
        const glm::vec3 verticalOpticalDepth =
            atmosphere.RayleighScattering * atmosphere.RayleighDensityH * rayleighDensity +
            (atmosphere.MieScattering + atmosphere.MieAbsorption) * atmosphere.MieDensityH * mieDensity +
            atmosphere.OzoneAbsorption * atmosphere.OzoneWidth;
        return glm::exp(-verticalOpticalDepth * airMass);
    }
}
