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

#ifdef WIN32
#	include <aclapi.h>
#	include <dxgi1_2.h>
#endif

#if WITH_OIDN
#include <oidn.hpp>
#endif

namespace Vulkan::RayTracing
{
#if WITH_OIDN
    oidn::DeviceRef device;
    oidn::FilterRef filter;
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

    RayTracingRenderer::RayTracingRenderer(const char* rendererType, const WindowConfig& windowConfig, const VkPresentModeKHR presentMode,
                                           const bool enableValidationLayers) :
        RayTraceBaseRenderer(rendererType, windowConfig, presentMode, enableValidationLayers)
    {
        // try use amd gpu as denoise device
    }

    RayTracingRenderer::~RayTracingRenderer()
    {
        RayTracingRenderer::DeleteSwapChain();
        DeleteAccelerationStructures();
        rayTracingProperties_.reset();
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
                                      VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                                      VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME
                                  });
#if WIN32
        requiredExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
#endif

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures = {};
        rayTracingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rayTracingFeatures.pNext = nextDeviceFeatures;
        rayTracingFeatures.rayTracingPipeline = true;

        RayTraceBaseRenderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, &rayTracingFeatures);
    }

    void RayTracingRenderer::OnDeviceSet()
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

        RayTraceBaseRenderer::OnDeviceSet();
    }

    void RayTracingRenderer::CreateSwapChain()
    {
        RayTraceBaseRenderer::CreateSwapChain();

        CreateOutputImage();

        rayTracingPipeline_.reset(new RayTracingPipeline(Device().GetDeviceProcedures(), SwapChain(), topAs_[0],
                                                         rtAccumulation_->GetImageView(), rtMotionVector_->GetImageView(),
                                                         rtVisibility0_->GetImageView(), rtVisibility1_->GetImageView(),
                                                         rtAlbedo_->GetImageView(), rtNormal_->GetImageView(),
                                                         rtAdaptiveSample_->GetImageView(),
                                                         UniformBuffers(), GetScene()));

        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
                                                                         rtAccumulation_->GetImageView(),
                                                                         rtPingPong0->GetImageView(),
                                                                         rtPingPong1->GetImageView(),
                                                                         rtMotionVector_->GetImageView(),
                                                                         rtVisibility0_->GetImageView(),
                                                                         rtVisibility1_->GetImageView(),
                                                                         rtOutputForOIDN_->GetImageView(),
                                                                         rtAdaptiveSample_->GetImageView(),
                                                                         UniformBuffers(), GetScene()));


        composePipelineNonDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), rtOutputForOIDN_->GetImageView(), UniformBuffers()));
        composePipelineDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), rtDenoise1_->GetImageView(), UniformBuffers()));

        const std::vector<ShaderBindingTable::Entry> rayGenPrograms = {{rayTracingPipeline_->RayGenShaderIndex(), {}}};
        const std::vector<ShaderBindingTable::Entry> missPrograms = {{rayTracingPipeline_->MissShaderIndex(), {}}};
        const std::vector<ShaderBindingTable::Entry> hitGroups = {
            {rayTracingPipeline_->TriangleHitGroupIndex(), {}}, {rayTracingPipeline_->ProceduralHitGroupIndex(), {}}
        };

        shaderBindingTable_.reset(new ShaderBindingTable(Device().GetDeviceProcedures(), rayTracingPipeline_->Handle(),
                                                         *rayTracingProperties_, rayGenPrograms, missPrograms,
                                                         hitGroups));

        visualDebugPipeline_.reset(new PipelineCommon::VisualDebuggerPipeline(SwapChain(),
                                                                              rtAlbedo_->GetImageView(), rtNormal_->GetImageView(), rtAdaptiveSample_->GetImageView(), rtAccumulation_->GetImageView(),
                                                                              UniformBuffers()));
    }

    void RayTracingRenderer::DeleteSwapChain()
    {
        shaderBindingTable_.reset();
        accumulatePipeline_.reset();
        rayTracingPipeline_.reset();
        composePipelineNonDenoiser_.reset();
        composePipelineDenoiser_.reset();
        visualDebugPipeline_.reset();

        rtAccumulation_.reset();
        rtOutput_.reset();
        rtMotionVector_.reset();
        rtVisibility0_.reset();
        rtVisibility1_.reset();

        rtPingPong0.reset();
        rtPingPong1.reset();

        rtAlbedo_.reset();
        rtNormal_.reset();

        rtDenoise0_.reset();
        rtDenoise1_.reset();

        rtAdaptiveSample_.reset();

        RayTraceBaseRenderer::DeleteSwapChain();
    }

    void RayTracingRenderer::BeforeNextFrame()
    {
        {
            SCOPED_CPU_TIMER("OIDN");
#if WITH_OIDN
            if (supportDenoiser_)
            {
                filter.executeAsync();
                device.sync();
            }
#endif
        }
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
        rtAccumulation_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtOutput_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtOutputForOIDN_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtMotionVector_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtVisibility0_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtVisibility1_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtPingPong1->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtAlbedo_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtNormal_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtAdaptiveSample_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        // Bind ray tracing pipeline.
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline_->Handle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);

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
            Device().GetDeviceProcedures().vkCmdTraceRaysKHR(commandBuffer,
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


            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_GENERAL);
#if WITH_OIDN
            if (supportDenoiser_)
            {
                ImageMemoryBarrier::Insert(commandBuffer, rtDenoise1_->GetImage().Handle(), subresourceRange, 0,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL);
                VkDescriptorSet DescriptorSets[] = {composePipelineDenoiser_->DescriptorSet(imageIndex)};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipelineDenoiser_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        composePipelineDenoiser_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
                vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);
            }
            else
#endif
            {
                ImageMemoryBarrier::Insert(commandBuffer, rtOutputForOIDN_->GetImage().Handle(), subresourceRange, 0,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL);
                VkDescriptorSet DescriptorSets[] = {composePipelineNonDenoiser_->DescriptorSet(imageIndex)};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipelineNonDenoiser_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        composePipelineNonDenoiser_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
                vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);
            }

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            if (visualDebug_)
            {
                ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT,
                                           VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                           VK_IMAGE_LAYOUT_GENERAL);
                ImageMemoryBarrier::Insert(commandBuffer, rtAlbedo_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_LAYOUT_GENERAL);
                ImageMemoryBarrier::Insert(commandBuffer, rtNormal_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_LAYOUT_GENERAL);
                ImageMemoryBarrier::Insert(commandBuffer, rtAdaptiveSample_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_LAYOUT_GENERAL);
                ImageMemoryBarrier::Insert(commandBuffer, rtMotionVector_->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_LAYOUT_GENERAL);

                {
                    VkDescriptorSet DescriptorSets[] = {visualDebugPipeline_->DescriptorSet(imageIndex)};
                    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, visualDebugPipeline_->Handle());
                    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                            visualDebugPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
                    vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);
                }

                ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                           VK_ACCESS_TRANSFER_WRITE_BIT,
                                           0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            }
        }


        if (supportDenoiser_)
        {
            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount = 1;

            ImageMemoryBarrier::Insert(commandBuffer, rtOutputForOIDN_->GetImage().Handle(), subresourceRange,
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
                           rtOutputForOIDN_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           rtDenoise0_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::Insert(commandBuffer, rtOutputForOIDN_->GetImage().Handle(), subresourceRange,
                                       VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    void RayTracingRenderer::CreateOutputImage()
    {
        const auto extent = SwapChain().Extent();
        const auto format = SwapChain().Format();

        rtAccumulation_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "accumulate"));
        rtOutput_.reset(new RenderImage(Device(), extent, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false, "output0"));
        rtOutputForOIDN_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false, "output1"));
        rtPingPong0.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "pingpong0"));
        rtPingPong1.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "pingpong1"));
        rtMotionVector_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "motionvector"));
        rtVisibility0_.reset(new RenderImage(Device(), extent, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "vis0"));
        rtVisibility1_.reset(new RenderImage(Device(), extent, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "vis1"));

        rtAlbedo_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, true, "albedo"));
        rtNormal_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, true, "albedo"));

        rtDenoise0_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true, "denoise0"));
        rtDenoise1_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, true, "denoise1"));

        rtAdaptiveSample_.reset(new RenderImage(Device(), extent, VK_FORMAT_R8_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "adaptive sample"));
#if WITH_OIDN
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
