#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

namespace Vulkan::SoftwareModernNoAmbient
{
    SoftwareModernNoAmbientRenderer::SoftwareModernNoAmbientRenderer(Vulkan::VulkanBaseRenderer& baseRender) :
        LogicRendererBase(baseRender)
    {
    }

    SoftwareModernNoAmbientRenderer::~SoftwareModernNoAmbientRenderer()
    {
        DeleteSwapChain();
    }

    void SoftwareModernNoAmbientRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        (void)extent;
        shadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.SwModernNoAmbient.comp.slang.spv", GetScene()));
        surfaceShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.SwModernNoAmbientSurface.comp.slang.spv", GetScene()));
        surfaceBuild_.CreateSwapChain(SwapChain(), GetScene());
        scheduler_.CreateSwapChain(SwapChain(), GetScene());
        standardBucketPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.SwModernNoAmbientStandard.comp.slang.spv", GetScene()));
        backgroundBucketPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.SwModernNoAmbientBackground.comp.slang.spv", GetScene()));
        emissiveBucketPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.SwModernNoAmbientEmissive.comp.slang.spv", GetScene()));
        gtaoPipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Core.GTAO.comp.slang.spv", GetScene()));
        composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(
            SwapChain(), "assets/shaders/Process.GTAOCompose.comp.slang.spv", GetScene()));
    }

    void SoftwareModernNoAmbientRenderer::DeleteSwapChain()
    {
        shadingPipeline_.reset();
        surfaceShadingPipeline_.reset();
        surfaceBuild_.DeleteSwapChain();
        scheduler_.DeleteSwapChain();
        standardBucketPipeline_.reset();
        backgroundBucketPipeline_.reset();
        emissiveBucketPipeline_.reset();
        gtaoPipeline_.reset();
        composePipeline_.reset();
    }

    void SoftwareModernNoAmbientRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (baseRender_.IsSurfacePathActive())
        {
            RenderSurface(commandBuffer, imageIndex);
        }
        else
        {
            RenderLegacy(commandBuffer, imageIndex);
        }
    }

    void SoftwareModernNoAmbientRenderer::RenderLegacy(
        VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
        const Assets::GPUScene gpuScene =
            GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());

        {
            SCOPED_GPU_TIMER("shadingpass");
            baseRender_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_AMBIENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_MOTIONVECTOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
                {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            }, "software modern no ambient shading");
            shadingPipeline_->BindPipeline(commandBuffer, gpuScene);
            vkCmdDispatch(commandBuffer,
                          Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                          Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
        }

        // The rollback path still shades before GTAO exists, so compose is what applies occlusion.
        DispatchGTAO(commandBuffer, imageIndex);
        Compose(commandBuffer, gpuScene);
    }

    void SoftwareModernNoAmbientRenderer::RenderSurface(
        VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        Assets::GPUScene gpuScene =
            GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase());
        baseRender_.ConfigureSurfacePath(gpuScene);
        baseRender_.ConfigureCheckerboardShading(gpuScene);

        // Build resolves the visibility buffer once, at full rate, for every consumer below.
        // SoftwareModernNoAmbient never reads RT_SPECULAR_ALBEDO, so it does not pay for it.
        surfaceBuild_.Run(baseRender_, commandBuffer, gpuScene, false);

        // Classification reads only depth + the material word, so it runs right after Build and
        // before anything that could serialize on the shading kernels.
        const bool scheduled = baseRender_.IsSurfaceSchedulerActive() && scheduler_.IsValid();
        if (scheduled)
        {
            scheduler_.Classify(baseRender_, commandBuffer, gpuScene);
        }

        // GTAO can finally run where it belongs - on the dense depth/normal buffer, before shading -
        // instead of after Core Shading because it used to depend on Core's own output.
        DispatchGTAO(commandBuffer, imageIndex);

        ShadeSurface(commandBuffer, gpuScene, scheduled);

        baseRender_.ResolveCheckerboardShading(
            commandBuffer, gpuScene,
            PipelineCommon::ECheckerboardResolveSet::SoftwareModernNoAmbient);

        Compose(commandBuffer, gpuScene);
    }

    void SoftwareModernNoAmbientRenderer::ShadeSurface(
        VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene, const bool scheduled)
    {
        SCOPED_GPU_TIMER("shadingpass");
        baseRender_.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_BSDF_DATA, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            // GTAO ran before shading on this path, so the kernel consumes occlusion directly and
            // Process.GTAOCompose no longer upsamples it.
            {Assets::Bindless::RT_GTAO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_AMBIENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
        }, "software modern no ambient surface shading");

        if (scheduled)
        {
            ShadeScheduled(commandBuffer, gpuScene);
        }
        else
        {
            ShadeAnalytic(commandBuffer, gpuScene);
        }
    }

    // Analytic allocation: one Standard-only full-screen dispatch, with checkerboard expressed as a
    // compacted dispatch width. Zero scheduling memory and zero atomics; kept as the A/B baseline
    // the scheduler has to beat.
    void SoftwareModernNoAmbientRenderer::ShadeAnalytic(
        VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene)
    {
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
        surfaceShadingPipeline_->BindPipeline(commandBuffer, gpuScene);
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(
                          baseRender_.CheckerboardDispatchWidth(activeExtent.width, gpuScene), 8),
                      Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
    }

    // Scheduled allocation: one indirect dispatch per shading bucket over its own tile list. The
    // three buckets write disjoint pixels, so they need no barrier between them.
    void SoftwareModernNoAmbientRenderer::ShadeScheduled(
        VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene)
    {
        using EBucket = PipelineCommon::ShadingSchedulerPass::EBucket;
        scheduler_.DispatchBucket(commandBuffer, *standardBucketPipeline_, gpuScene, EBucket::Standard);
        scheduler_.DispatchBucket(commandBuffer, *backgroundBucketPipeline_, gpuScene, EBucket::Background);
        scheduler_.DispatchBucket(commandBuffer, *emissiveBucketPipeline_, gpuScene, EBucket::Emissive);
    }

    void SoftwareModernNoAmbientRenderer::DispatchGTAO(
        VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const auto& settings = baseRender_.FrameSettings().userSettings;
        if (!settings.GTAOEnable)
        {
            return;
        }

        SCOPED_GPU_TIMER("gtao pass");
        baseRender_.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_GTAO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
        }, "gtao");
        gtaoPipeline_->BindPipeline(commandBuffer,
            GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase()));
        const VkExtent2D extent = baseRender_.ActiveViewRenderExtent();
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount((extent.width + 1u) / 2u, 8),
                      Utilities::Math::GetSafeDispatchCount((extent.height + 1u) / 2u, 8), 1);
    }

    void SoftwareModernNoAmbientRenderer::Compose(
        VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene)
    {
        const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();

        SCOPED_GPU_TIMER("simplecompose pass");
        baseRender_.TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_AMBIENT, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_GTAO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_SCENE_COLOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
        }, "gtao compose");
        // CustomData2 carries the surface/scheduler flags: compose has to know whether Core
        // already applied sky occlusion to RT_AMBIENT.
        composePipeline_->BindPipeline(commandBuffer, gpuScene, baseRender_.ActiveViewBankBase(),
                                       0u, gpuScene.CustomData2);
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
                      Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
    }
}
