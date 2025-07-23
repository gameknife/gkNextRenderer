#include "PathTracingRenderer.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Assets/Texture.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Rendering/PathTracing/PathTracingPipeline.hpp"

#include <chrono>
#include <numeric>
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
        rayTracingPipeline_.reset(new PathTracingPipeline(SwapChain(), GetBaseRender<RayTraceBaseRenderer>().TLAS()[0], baseRender_, UniformBuffers(), GetScene()));
        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(), baseRender_, baseRender_.rtOutputDiffuse->GetImageView(), rtPingPong0->GetImageView(), baseRender_.rtAccumlatedDiffuse->GetImageView(), UniformBuffers(), GetScene()));
        accumulatePipelineSpec_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),baseRender_, baseRender_.rtOutputSpecular->GetImageView(), rtPingPong1->GetImageView(), baseRender_.rtAccumlatedSpecular->GetImageView(), UniformBuffers(), GetScene()));
        accumulatePipelineAlbedo_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(), baseRender_, baseRender_.rtAlbedo_->GetImageView(), rtPingPong3->GetImageView(), baseRender_.rtAccumlatedAlbedo_->GetImageView(), UniformBuffers(), GetScene()));
        composePipelineNonDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), baseRender_, UniformBuffers()));
#if WITH_OIDN
        composePipelineDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), rtDenoise1_->GetImageView(), rtAlbedo_->GetImageView(), rtNormal_->GetImageView(), rtVisibility0->GetImageView(), rtVisibility1_->GetImageView(), UniformBuffers()));
#endif
    }

    void PathTracingRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        accumulatePipeline_.reset();
        accumulatePipelineSpec_.reset();
        composePipelineNonDenoiser_.reset();
        accumulatePipelineAlbedo_.reset();
        
#if WITH_OIDN
        composePipelineDenoiser_.reset();
        rtDenoise0_.reset();
        rtDenoise1_.reset();
#endif
        rtPingPong0.reset();
        rtPingPong1.reset();
        rtPingPong3.reset();
    }

    void PathTracingRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        // Acquire destination images for rendering.
        rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.InitializeBarriers(commandBuffer);

        // Execute ray tracing shaders.
        {
            SCOPED_GPU_TIMER("rt pass");
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayTracingPipeline_->Handle());
            rayTracingPipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.rtOutputDiffuse->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.rtObject0->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.rtMotionVector_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        
         // accumulate with reproject
        {
            SCOPED_GPU_TIMER("reproject pass");
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
            accumulatePipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            {
                glm::uvec2 pushConst = { 0, 1 }; vkCmdPushConstants(commandBuffer, accumulatePipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,0, sizeof(glm::uvec2), &pushConst);
            }
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.rtAccumlatedDiffuse->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL );
            
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipelineSpec_->Handle());
            accumulatePipelineSpec_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            {
                glm::uvec2 pushConst = { 0, 1 }; vkCmdPushConstants(commandBuffer, accumulatePipelineSpec_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,0, sizeof(glm::uvec2), &pushConst);
            }
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
            baseRender_.rtAccumlatedSpecular->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipelineAlbedo_->Handle());
            accumulatePipelineAlbedo_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            {
                glm::uvec2 pushConst = { 1, 0 }; vkCmdPushConstants(commandBuffer, accumulatePipelineAlbedo_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,0, sizeof(glm::uvec2), &pushConst);
            }
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.rtAccumlatedAlbedo_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("compose pass");
            SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);
#if WITH_OIDN
            if (baseRender_.supportDenoiser_)
            {
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
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipelineNonDenoiser_->Handle());
                composePipelineNonDenoiser_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
                vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
            }
            SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
        }
        
        {
            SCOPED_GPU_TIMER("copy pass");
            baseRender_.rtAccumlatedDiffuse->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            rtPingPong0->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {rtPingPong0->GetImage().Extent().width, rtPingPong0->GetImage().Extent().height, 1};
        
            vkCmdCopyImage(commandBuffer, baseRender_.rtAccumlatedDiffuse->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong0->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            baseRender_.rtAccumlatedSpecular->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            rtPingPong1->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                    
            vkCmdCopyImage(commandBuffer, baseRender_.rtAccumlatedSpecular->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong1->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            baseRender_.rtAccumlatedAlbedo_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            rtPingPong3->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            vkCmdCopyImage(commandBuffer, baseRender_.rtAccumlatedAlbedo_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong3->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
	
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
        
        rtPingPong0.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT, false, "pingpong0"));
        rtPingPong1.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT, false, "pingpong1"));
        rtPingPong3.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,"prevoutputalbedo"));
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
