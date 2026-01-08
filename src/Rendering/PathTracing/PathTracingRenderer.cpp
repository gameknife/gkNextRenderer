#include "PathTracingRenderer.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Rendering/PipelineCommon/CommonComputePipeline.hpp"

#include <chrono>
#include <numeric>

#include "Runtime/Engine.hpp"

namespace Vulkan::RayTracing
{
    PathTracingRenderer::PathTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender) : LogicRendererBase(baseRender)
    {
    }

    PathTracingRenderer::~PathTracingRenderer()
    {
        PathTracingRenderer::DeleteSwapChain();
    }

    void PathTracingRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
#if WITH_OIDN
        // Required extensions.
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                    VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME
                                  });
#if WIN32 && !defined(__MINGW32__)
        requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#endif
        
#endif
    }

    void PathTracingRenderer::OnDeviceSet()
    {
#if WITH_OIDN
        // Query the UUID of the Vulkan physical device
        VkPhysicalDeviceIDProperties id_properties{};
        id_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &id_properties;
        vkGetPhysicalDeviceProperties2(Device().PhysicalDevice(), &properties);

        oidn::UUID uuid;
        std::memcpy(uuid.bytes, id_properties.deviceUUID, sizeof(uuid.bytes));

        device = oidn::newDevice(uuid); // CPU or GPU if available
        device.commit();
#endif
    }

    void PathTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        CreateOutputImage(extent);
        
        rayTracingPipeline_.reset(new PipelineCommon::ZeroBindWithTLASPipeline( SwapChain(), "assets/shaders/Core.PathTracing.comp.slang.spv"));
        accumulatePipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(SwapChain(), "assets/shaders/Process.ReProject.comp.slang.spv", 24));
        composePipelineNonDenoiser_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Process.DenoiseJBF.comp.slang.spv"));

        if (GOption->ReferenceMode)
        {
            prevSingleDiffuseId_ = baseRender_.GetTemporalStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "prevDiffuseTmp");
            prevSingleSpecularId_ = baseRender_.GetTemporalStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "prevSpecularTmp");
            prevSingleAlbedoId_ = baseRender_.GetTemporalStorageImage(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "prevAlbedoTmp");
        }
        else
        {
            prevSingleDiffuseId_ = Assets::Bindless::RT_SINGLE_PREV_DIFFUSE;
            prevSingleSpecularId_ = Assets::Bindless::RT_SINGLE_PREV_SPECULAR;
            prevSingleAlbedoId_ = Assets::Bindless::RT_SINGLE_PREV_ALBEDO;
        }

#if WITH_OIDN
        composePipelineDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), rtDenoise1_->GetImageView(), rtAlbedo_->GetImageView(), rtNormal_->GetImageView(), rtVisibility0->GetImageView(), rtVisibility1_->GetImageView(), UniformBuffers()));
#endif
    }

    void PathTracingRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        accumulatePipeline_.reset();
        composePipelineNonDenoiser_.reset();
#if WITH_OIDN
        composePipelineDenoiser_.reset();
        rtDenoise0_.reset();
        rtDenoise1_.reset();
#endif
    }

    void PathTracingRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        // Acquire destination images for rendering.
        baseRender_.InitializeBarriers(commandBuffer);

        // Execute ray tracing shaders.
        {
            SCOPED_GPU_TIMER("rt pass");
            rayTracingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_NORMAL)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_MOTIONVECTOR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        
         // accumulate with reproject
        {
            SCOPED_GPU_TIMER("reproject pass");
            std::array<uint32_t, 6> pushConst { NextEngine::GetInstance()->IsProgressiveRendering(), uint32_t(NextEngine::GetInstance()->GetUserSettings().TemporalFrames),
                prevSingleDiffuseId_, prevSingleSpecularId_, prevSingleAlbedoId_, 0 };
            accumulatePipeline_->BindPipeline(commandBuffer, pushConst.data());
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL );
            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("compose pass");
#if WITH_OIDN
            if (baseRender_.supportDenoiser_)
            {
                SCOPED_GPU_TIMER("oidn compose");
                ImageMemoryBarrier::Insert(commandBuffer, rtDenoise1_->GetImage().Handle(), subresourceRange, 0,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL);
                VkDescriptorSet DescriptorSets[] = {composePipelineDenoiser_->DescriptorSet(imageIndex)};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipelineDenoiser_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        composePipelineDenoiser_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
                vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 8, 1);
            }
            else
#endif
            {
                if (!(baseRender_.SupportDLSSRR() && NextEngine::GetInstance()->GetUserSettings().DLSSRR))
                {
                    composePipelineNonDenoiser_->BindPipeline(commandBuffer, GetScene(), imageIndex);
                    vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
                }
            }
        }
        
        {
            SCOPED_GPU_TIMER("copy pass");
            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            baseRender_.GetStorageImage(prevSingleDiffuseId_)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {baseRender_.GetStorageImage(prevSingleDiffuseId_)->GetImage().Extent().width, baseRender_.GetStorageImage(prevSingleDiffuseId_)->GetImage().Extent().height, 1};
        
            vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(prevSingleDiffuseId_)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            baseRender_.GetStorageImage(prevSingleSpecularId_)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    
            vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(prevSingleSpecularId_)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            baseRender_.GetStorageImage(prevSingleAlbedoId_)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            vkCmdCopyImage(commandBuffer, baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, baseRender_.GetStorageImage(prevSingleAlbedoId_)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }

#if WITH_OIDN
        if (baseRender_.supportDenoiser_)
        {
            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount = 1;

            ImageMemoryBarrier::Insert(commandBuffer, rtOutput_->GetImage().Handle(), subresourceRange,
                                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            ImageMemoryBarrier::Insert(commandBuffer, rtDenoise0_->GetImage().Handle(), subresourceRange, 0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Copy output image into swap-chain image.
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

            vkCmdCopyImage(commandBuffer,
                           rtOutput_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           rtDenoise0_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::Insert(commandBuffer, rtOutput_->GetImage().Handle(), subresourceRange,
                                       VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
        }
#endif
    }

    void PathTracingRenderer::BeforeNextFrame()
    {
        {
            SCOPED_CPU_TIMER("OIDN");
#if WITH_OIDN
            if (baseRender_.supportDenoiser_)
            {
                filter.executeAsync();
                device.sync();
            }
#endif
        }
    }
    
    void PathTracingRenderer::CreateOutputImage(const VkExtent2D& extent)
    {
        const auto format = SwapChain().Format();
        const auto tiling = VK_IMAGE_TILING_OPTIMAL;

        bool externalIfOiDN = false;
#if WITH_OIDN
        externalIfOiDN = true;
#endif

#if WITH_OIDN   
        rtDenoise0_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, externalIfOiDN, "denoise0"));
        rtDenoise1_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, externalIfOiDN, "denoise1"));

        size_t SrcImageSize = extent.width * extent.height * 4 * 2;
        size_t SrcImageW8 = 4 * 2 * extent.width;
        size_t SrcImage8 = 4 * 2;
        oidn::BufferRef colorBuf = device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtDenoise0_->GetExternalHandle(), nullptr, SrcImageSize);
        oidn::BufferRef outBuf = device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtDenoise1_->GetExternalHandle(), nullptr, SrcImageSize);
        oidn::BufferRef albedoBuf = device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtAlbedo_->GetExternalHandle(), nullptr, SrcImageSize);
        oidn::BufferRef normalBuf = device.newBuffer(oidn::ExternalMemoryTypeFlag::OpaqueWin32, rtNormal_->GetExternalHandle(), nullptr, SrcImageSize);

        filter = device.newFilter("RT"); // generic ray tracing filter
        filter.setImage("color", colorBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // beauty
        filter.setImage("albedo", albedoBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // aux
        filter.setImage("normal", normalBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // aux
        filter.setImage("output", outBuf, oidn::Format::Half3, extent.width, extent.height, 0, SrcImage8, SrcImageW8); // denoised beauty
        filter.set("hdr", true); // beauty image is HDR
        filter.set("quality", oidn::Quality::Fast);
        //filter.set("quality", oidn::Quality::Balanced);
        //filter.set("quality", oidn::Quality::High);
        filter.set("cleanAux", true);
        //filter.set("inputScale", 1.0f);
        filter.commit();
#endif
    }
}
