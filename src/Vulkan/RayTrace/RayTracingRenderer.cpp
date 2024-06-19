#include "RayTracingRenderer.hpp"
#include "RayTracingPipeline.hpp"
#include "Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Vulkan/RayTracing/ShaderBindingTable.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/SwapChain.hpp"
#include <chrono>
#include <iostream>
#include <numeric>

#include "Vulkan/RenderImage.hpp"

#if WITH_OIDN
#include "ThirdParty/oidn/include/oidn.hpp"
#endif

namespace Vulkan::RayTracing
{
#if WITH_OIDN
    oidn::DeviceRef device;
#endif
    
    struct DenoiserPushConstantData
    {
        uint32_t pingpong;
        uint32_t stepsize;
    };

    namespace
    {
        template <class TAccelerationStructure>
        VkAccelerationStructureBuildSizesInfoKHR GetTotalRequirements(
            const std::vector<TAccelerationStructure>& accelerationStructures)
        {
            VkAccelerationStructureBuildSizesInfoKHR total{};

            for (const auto& accelerationStructure : accelerationStructures)
            {
                total.accelerationStructureSize += accelerationStructure.BuildSizes().accelerationStructureSize;
                total.buildScratchSize += accelerationStructure.BuildSizes().buildScratchSize;
                total.updateScratchSize += accelerationStructure.BuildSizes().updateScratchSize;
            }

            return total;
        }
    }

    RayTracingRenderer::RayTracingRenderer(const WindowConfig& windowConfig, const VkPresentModeKHR presentMode,
                             const bool enableValidationLayers) :
        RayTraceBaseRenderer(windowConfig, presentMode, enableValidationLayers)
    {
        // try use amd gpu as denoise device
#if WITH_OIDN
        device = oidn::newDevice(oidn::DeviceType::CUDA); // CPU or GPU if available
        device.commit();
#endif
    }

    RayTracingRenderer::~RayTracingRenderer()
    {
        RayTracingRenderer::DeleteSwapChain();
        DeleteAccelerationStructures();
        rayTracingProperties_.reset();
        deviceProcedures_.reset();         
    }

    void RayTracingRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
        supportRayTracing_ = true;
        
        // Required extensions.
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME
                                  });
        
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures = {};
        rayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rayTracingFeatures.pNext = nextDeviceFeatures;
        rayTracingFeatures.rayTracingPipeline = true;

        RayTraceBaseRenderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, &rayTracingFeatures);
    }

    void RayTracingRenderer::OnDeviceSet()
    {
        RayTraceBaseRenderer::OnDeviceSet();
    }

    void RayTracingRenderer::CreateSwapChain()
    {
        RayTraceBaseRenderer::CreateSwapChain();

        CreateOutputImage();

        rayTracingPipeline_.reset(new RayTracingPipeline(*deviceProcedures_, SwapChain(), topAs_[0],
                                                         rtAccumulation_->GetImageView(), rtMotionVector_->GetImageView(),
                                                         rtVisibility0_->GetImageView(), rtVisibility1_->GetImageView(),
                                                         UniformBuffers(), GetScene()));

        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
            rtAccumulation_->GetImageView(),
            rtPingPong0->GetImageView(),
            rtPingPong1->GetImageView(),
            rtMotionVector_->GetImageView(),
            rtVisibility0_->GetImageView(),
            rtVisibility1_->GetImageView(),
            rtOutputForOIDN_->GetImageView(),
            UniformBuffers(), GetScene()));

        composePipeline_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), *oidnImageView_));
    
        
        const std::vector<ShaderBindingTable::Entry> rayGenPrograms = {{rayTracingPipeline_->RayGenShaderIndex(), {}}};
        const std::vector<ShaderBindingTable::Entry> missPrograms = {{rayTracingPipeline_->MissShaderIndex(), {}}};
        const std::vector<ShaderBindingTable::Entry> hitGroups = {
            {rayTracingPipeline_->TriangleHitGroupIndex(), {}}, {rayTracingPipeline_->ProceduralHitGroupIndex(), {}}
        };

        shaderBindingTable_.reset(new ShaderBindingTable(*deviceProcedures_, rayTracingPipeline_->Handle(),
                                                         *rayTracingProperties_, rayGenPrograms, missPrograms,
                                                         hitGroups));     
        
    }

    void RayTracingRenderer::DeleteSwapChain()
    {
        shaderBindingTable_.reset();
        accumulatePipeline_.reset();
        rayTracingPipeline_.reset();
        composePipeline_.reset();

        rtAccumulation_.reset();
        rtOutput_.reset();
        rtMotionVector_.reset();
        rtVisibility0_.reset();
        rtVisibility1_.reset();

        rtPingPong0.reset();
        rtPingPong1.reset();

        oidnImage_.reset();
        oidnImageMemory_.reset();
        oidnImageView_.reset();

        RayTraceBaseRenderer::DeleteSwapChain();
    }

    void RayTracingRenderer::AfterPresent()
    {
        
#if WITH_OIDN
        auto Extent = SwapChain().Extent();
        size_t SrcImageSize = Extent.width * Extent.height * 4 * 2;
        size_t ImageSize = Extent.width * Extent.height * 3 * 2;
        static oidn::BufferRef colorBuf  = device.newBuffer(ImageSize);

        // Create a filter for denoising a beauty (color) image using optional auxiliary images too
        // This can be an expensive operation, so try no to create a new filter for every image!
        static oidn::FilterRef filter = device.newFilter("RT"); // generic ray tracing filter
        filter.setImage("color",  colorBuf,  oidn::Format::Half3, Extent.width, Extent.height); // beauty
        //filter.setImage("albedo", albedoBuf, oidn::Format::Float3, width, height); // auxiliary
        //filter.setImage("normal", normalBuf, oidn::Format::Float3, width, height); // auxiliary
        filter.setImage("output", colorBuf,  oidn::Format::Half3, Extent.width, Extent.height); // denoised beauty
        filter.set("hdr", true); // beauty image is HDR
        filter.set("quality", oidn::Quality::Fast); // beauty image is HDR
        filter.commit();

        // fill it from float3 buffer, frame x
        uint8_t* colorPtr = (uint8_t*)colorBuf.getData();
        uint8_t* mappedData = (uint8_t*)oidnImageMemory_->Map(0,  SrcImageSize );
        for( int i = 0; i < Extent.width * Extent.height; i++)
        {
            memcpy(colorPtr + i * 3 * 2, mappedData + i * 4 * 2, 3 * 2);
        }
        filter.execute();
        for( int i = 0; i < Extent.width * Extent.height; i++)
        {
            memcpy( mappedData + i * 4 * 2, colorPtr + i * 3 * 2, 3 * 2);
        }

        oidnImageMemory_->Unmap();
        // oidn error check
        // const char* errorMessage;
        // if (device.getError(errorMessage) != oidn::Error::None)
        // 	std::cout << "Error: " << errorMessage << std::endl;
        #endif
    }

    void RayTracingRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const auto extent = SwapChain().Extent();

        
        VkDescriptorSet descriptorSets[] = {rayTracingPipeline_->DescriptorSet(imageIndex)};

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        // Acquire destination images for rendering.
        rtAccumulation_->InsertBarrier(commandBuffer, 0,VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtOutput_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtOutputForOIDN_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtMotionVector_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtVisibility0_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtVisibility1_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtPingPong1->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        
        // Bind ray tracing pipeline.
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline_->Handle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,rayTracingPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);

        // Describe the shader binding table.
        VkStridedDeviceAddressRegionKHR raygenShaderBindingTable = {};
        raygenShaderBindingTable.deviceAddress = shaderBindingTable_->RayGenDeviceAddress();
        raygenShaderBindingTable.stride = shaderBindingTable_->RayGenEntrySize();
        raygenShaderBindingTable.size = shaderBindingTable_->RayGenSize();

        VkStridedDeviceAddressRegionKHR missShaderBindingTable = {};
        missShaderBindingTable.deviceAddress = shaderBindingTable_->MissDeviceAddress();
        missShaderBindingTable.stride = shaderBindingTable_->MissEntrySize();
        missShaderBindingTable.size = shaderBindingTable_->MissSize();

        VkStridedDeviceAddressRegionKHR hitShaderBindingTable = {};
        hitShaderBindingTable.deviceAddress = shaderBindingTable_->HitGroupDeviceAddress();
        hitShaderBindingTable.stride = shaderBindingTable_->HitGroupEntrySize();
        hitShaderBindingTable.size = shaderBindingTable_->HitGroupSize();

        VkStridedDeviceAddressRegionKHR callableShaderBindingTable = {};

        // Execute ray tracing shaders.
        {
            SCOPED_GPU_TIMER("rt pass");
            deviceProcedures_->vkCmdTraceRaysKHR(commandBuffer,
                                                &raygenShaderBindingTable, &missShaderBindingTable, &hitShaderBindingTable,
                                                &callableShaderBindingTable,
                                                CheckerboxRendering() ? extent.width / 2 : extent.width, extent.height, 1);

            rtAccumulation_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            rtVisibility0_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            rtMotionVector_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        // accumulate with reproject
        {
            SCOPED_GPU_TIMER("reproject pass");
            VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    accumulatePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);
        }
        
        {
            SCOPED_GPU_TIMER("compose");

            ImageMemoryBarrier::Insert(commandBuffer, oidnImage_->Handle(), subresourceRange, 0,
                               VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_GENERAL);
            
            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_GENERAL);
            
            VkDescriptorSet DescriptorSets[] = {composePipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    composePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            
            // rtOutput_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            // ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
            //                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            //                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            //
            // // Copy output image into swap-chain image.
            // VkImageCopy copyRegion;
            // copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            // copyRegion.srcOffset = {0, 0, 0};
            // copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            // copyRegion.dstOffset = {0, 0, 0};
            // copyRegion.extent = {extent.width, extent.height, 1};
            //
            // vkCmdCopyImage(commandBuffer,
            //                rtOutput_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            //                SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            //                1, &copyRegion);
            //
            // ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
            //                            VK_ACCESS_TRANSFER_WRITE_BIT,
            //                            0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);   
        }

        {
#if WITH_OIDN

            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount = 1;
	           
            ImageMemoryBarrier::Insert(commandBuffer, rtOutputForOIDN_->GetImage().Handle(), subresourceRange,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            
            ImageMemoryBarrier::Insert(commandBuffer, oidnImage_->Handle(), subresourceRange, 0,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            
            // Copy output image into swap-chain image.
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};
            
            vkCmdCopyImage(commandBuffer,
                           rtOutputForOIDN_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           oidnImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::Insert(commandBuffer, rtOutputForOIDN_->GetImage().Handle(), subresourceRange,
               VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_GENERAL);
            // after cmdbuffer commit, we can readback the image
            #endif
        }
    }

    void RayTracingRenderer::CreateOutputImage()
    {
        const auto extent = SwapChain().Extent();
        const auto format = SwapChain().Format();
        
        rtAccumulation_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,  VK_IMAGE_USAGE_STORAGE_BIT));
        rtOutput_.reset(new RenderImage(Device(), extent, format, VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        rtOutputForOIDN_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        rtPingPong0.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT));
        rtPingPong1.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT));
        rtMotionVector_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_STORAGE_BIT));
        rtVisibility0_.reset(new RenderImage(Device(), extent, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT));
        rtVisibility1_.reset(new RenderImage(Device(), extent, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_STORAGE_BIT));

        oidnImage_.reset(new Image(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT));
        oidnImageMemory_.reset(new DeviceMemory(oidnImage_->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        oidnImageView_.reset(new ImageView(Device(), oidnImage_->Handle(), VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT));
    }
}
