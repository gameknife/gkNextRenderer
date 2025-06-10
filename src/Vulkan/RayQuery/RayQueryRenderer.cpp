#include "RayQueryRenderer.hpp"
#include "Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/Glm.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/SwapChain.hpp"
#include <chrono>
#include <numeric>

#include "Assets/Texture.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Vulkan/RayQuery/RayQueryPipeline.hpp"

namespace Vulkan::RayTracing
{
    RayQueryRenderer::RayQueryRenderer(Vulkan::VulkanBaseRenderer& baseRender) : LogicRendererBase(baseRender)
    {
    }

    RayQueryRenderer::~RayQueryRenderer()
    {
        RayQueryRenderer::DeleteSwapChain();
    }

    void RayQueryRenderer::SetPhysicalDeviceImpl(
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

    void RayQueryRenderer::OnDeviceSet()
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

    void RayQueryRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        CreateOutputImage(extent);
        rayTracingPipeline_.reset(new RayQueryPipeline(Device().GetDeviceProcedures(), SwapChain(), GetBaseRender<RayTraceBaseRenderer>().TLAS()[0], baseRender_.rtAccumlation->GetImageView(), baseRender_.rtMotionVector_->GetImageView(),
                                                         baseRender_.rtVisibility0->GetImageView(),
                                                         baseRender_.rtAlbedo_->GetImageView(), baseRender_.rtNormal_->GetImageView(),
                                                         rtShaderTimer_->GetImageView(), UniformBuffers(), GetScene()));

        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
                                                                        baseRender_.rtAccumlation->GetImageView(),
                                                                        rtPingPong0->GetImageView(),
                                                                        baseRender_.rtMotionVector_->GetImageView(),
                                                                        baseRender_.rtVisibility0->GetImageView(),
                                                                        baseRender_.rtVisibility1->GetImageView(),
                                                                        baseRender_.rtOutput->GetImageView(),
                                                                        baseRender_.rtNormal_->GetImageView(),
                                                                        UniformBuffers(), GetScene()));


        composePipelineNonDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), baseRender_.rtOutput->GetImageView(), baseRender_.rtAlbedo_->GetImageView(), baseRender_.rtNormal_->GetImageView(), baseRender_.rtVisibility0->GetImageView(), baseRender_.rtVisibility1->GetImageView(), UniformBuffers()));
#if WITH_OIDN
        composePipelineDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), rtDenoise1_->GetImageView(), rtAlbedo_->GetImageView(), rtNormal_->GetImageView(), rtVisibility0->GetImageView(), rtVisibility1_->GetImageView(), UniformBuffers()));
#endif
        visualDebugPipeline_.reset(new PipelineCommon::VisualDebuggerPipeline(SwapChain(),
                                                                      baseRender_.rtAlbedo_->GetImageView(), rtShaderTimer_->GetImageView(), baseRender_.rtVisibility0->GetImageView(), baseRender_.rtVisibility1->GetImageView(),
                                                                      UniformBuffers()));
    }

    void RayQueryRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        accumulatePipeline_.reset();
        composePipelineNonDenoiser_.reset();
#if WITH_OIDN
        composePipelineDenoiser_.reset();
        rtDenoise0_.reset();
        rtDenoise1_.reset();
#endif
        visualDebugPipeline_.reset();
        
        rtPingPong0.reset();
        
        rtShaderTimer_.reset();
    }

    void RayQueryRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        // Acquire destination images for rendering.
        baseRender_.rtAccumlation->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.rtOutput->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.rtMotionVector_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.rtVisibility0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.rtVisibility1->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.rtAlbedo_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        baseRender_.rtNormal_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtShaderTimer_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        // Execute ray tracing shaders.
        {
            SCOPED_GPU_TIMER("rt pass");
            VkDescriptorSet DescriptorSets[] = {rayTracingPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayTracingPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    rayTracingPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            
            // bind the global bindless set
            static const uint32_t k_bindless_set = 1;
            VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
            vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayTracingPipeline_->PipelineLayout().Handle(), k_bindless_set,
                                     1, GlobalDescriptorSets, 0, nullptr );
            
            uint32_t workGroupSizeXDivider = 8;
            uint32_t workGroupSizeYDivider = 8;
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, workGroupSizeXDivider),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, workGroupSizeYDivider), 1);

            baseRender_.rtAccumlation->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.rtVisibility0->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.rtMotionVector_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        
         // accumulate with reproject
        {
            SCOPED_GPU_TIMER("reproject pass");
            VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    accumulatePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), 1);
        }

        {
            SCOPED_GPU_TIMER("compose");
            
            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
                                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_GENERAL);
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


                ImageMemoryBarrier::Insert(commandBuffer, baseRender_.rtOutput->GetImage().Handle(), subresourceRange, 0,
                                               VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_IMAGE_LAYOUT_GENERAL);

                VkDescriptorSet DescriptorSets[] = {composePipelineNonDenoiser_->DescriptorSet(imageIndex)};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipelineNonDenoiser_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        composePipelineNonDenoiser_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);

                glm::uvec2 pushConst = GOption->ReferenceMode ? glm::uvec2(SwapChain().Extent().width / 2, SwapChain().Extent().height / 2) : glm::uvec2(0,0);
                vkCmdPushConstants(commandBuffer, composePipelineNonDenoiser_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(glm::uvec2), &pushConst);

                vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
            }
            
            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        if (VisualDebug())
        {
            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, rtShaderTimer_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, baseRender_.rtAlbedo_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, baseRender_.rtNormal_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, baseRender_.rtMotionVector_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_GENERAL);

            {
                VkDescriptorSet DescriptorSets[] = {visualDebugPipeline_->DescriptorSet(imageIndex)};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, visualDebugPipeline_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        visualDebugPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
                vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 8, 1);
            }

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
        
        {
            baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            rtPingPong0->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {rtPingPong0->GetImage().Extent().width, rtPingPong0->GetImage().Extent().height, 1};
        
            vkCmdCopyImage(commandBuffer, baseRender_.rtOutput->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong0->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
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

    void RayQueryRenderer::BeforeNextFrame()
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
    
    void RayQueryRenderer::CreateOutputImage(const VkExtent2D& extent)
    {
        const auto format = SwapChain().Format();
        const auto tiling = VK_IMAGE_TILING_OPTIMAL;

        bool externalIfOiDN = false;
#if WITH_OIDN
        externalIfOiDN = true;
#endif
        
        baseRender_.rtAccumlation.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "accumulate"));
        baseRender_.rtOutput.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false, "output0"));
        rtPingPong0.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "pingpong0"));
              
        rtShaderTimer_.reset(new RenderImage(Device(), extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, false, "shadertimer"));
        
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
