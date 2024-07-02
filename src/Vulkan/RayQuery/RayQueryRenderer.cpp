#include "RayQueryRenderer.hpp"
#include "Vulkan/RayTracing/DeviceProcedures.hpp"
#include "RayQueryRenderer.hpp"
#include "Vulkan/RayTracing/TopLevelAccelerationStructure.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/Glm.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SingleTimeCommands.hpp"
#include "Vulkan/SwapChain.hpp"
#include <chrono>
#include <iostream>
#include <numeric>

#include "Utilities/Math.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Vulkan/RayQuery/RayQueryPipeline.hpp"

namespace Vulkan::RayTracing
{
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

    RayQueryRenderer::RayQueryRenderer(const WindowConfig& windowConfig, const VkPresentModeKHR presentMode,
                             const bool enableValidationLayers) :
        Vulkan::RayTracing::RayTraceBaseRenderer(windowConfig, presentMode, enableValidationLayers)
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
        Vulkan::RayTracing::RayTraceBaseRenderer::SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nextDeviceFeatures);
    }

    void RayQueryRenderer::OnDeviceSet()
    {
        Vulkan::RayTracing::RayTraceBaseRenderer::OnDeviceSet();
    }

    void RayQueryRenderer::CreateSwapChain()
    {
        Vulkan::RayTracing::RayTraceBaseRenderer::CreateSwapChain();
        CreateOutputImage();
        rayTracingPipeline_.reset(new RayQueryPipeline(Device().GetDeviceProcedures(), SwapChain(), topAs_[0], rtAccumulation_->GetImageView(), rtMotionVector_->GetImageView(),
                                                         rtVisibility0_->GetImageView(), rtVisibility1_->GetImageView(),
                                                         rtAlbedo_->GetImageView(), rtNormal_->GetImageView(),
                                                         rtAdaptiveSample_->GetImageView(), UniformBuffers(), GetScene()));

        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
                                                                        rtAccumulation_->GetImageView(),
                                                                        rtPingPong0->GetImageView(),
                                                                        rtPingPong1->GetImageView(),
                                                                        rtMotionVector_->GetImageView(),
                                                                        rtVisibility0_->GetImageView(),
                                                                        rtVisibility1_->GetImageView(),
                                                                        rtOutput_->GetImageView(),
                                                                        rtAdaptiveSample_->GetImageView(),
                                                                        UniformBuffers(), GetScene()));


        composePipelineNonDenoiser_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), rtOutput_->GetImageView(), UniformBuffers()));
    }

    void RayQueryRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        accumulatePipeline_.reset();
        composePipelineNonDenoiser_.reset();
        
        rtAccumulation_.reset();
        rtOutput_.reset();
        rtMotionVector_.reset();
        rtPingPong0.reset();
        rtPingPong1.reset();
        rtVisibility0_.reset();
        rtVisibility1_.reset();

        rtAlbedo_.reset();
        rtNormal_.reset();

        rtAdaptiveSample_.reset();


        
        Vulkan::RayTracing::RayTraceBaseRenderer::DeleteSwapChain();
    }

    void RayQueryRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const auto extent = SwapChain().Extent();
        
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        // Acquire destination images for rendering.
        rtAccumulation_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtOutput_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtMotionVector_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtVisibility0_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtVisibility1_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtPingPong1->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtAlbedo_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtNormal_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        rtAdaptiveSample_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        // Execute ray tracing shaders.
        {
            SCOPED_GPU_TIMER("rt pass");
            VkDescriptorSet DescriptorSets[] = {rayTracingPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayTracingPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    rayTracingPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);

            uint32_t workGroupSizeXDivider = 8 * (CheckerboxRendering() ? 2 : 1);
            uint32_t workGroupSizeYDivider = 4;
#if ANDROID
            workGroupSizeXDivider = 32 * (CheckerboxRendering() ? 2 : 1);
            workGroupSizeYDivider = 32;
#endif
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().width, workGroupSizeXDivider),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().height, workGroupSizeYDivider), 1);

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

            ImageMemoryBarrier::Insert(commandBuffer, rtOutput_->GetImage().Handle(), subresourceRange, 0,
                                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL);

            VkDescriptorSet DescriptorSets[] = {composePipelineNonDenoiser_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipelineNonDenoiser_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    composePipelineNonDenoiser_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                           VK_ACCESS_TRANSFER_WRITE_BIT,
                           0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
    }

    void RayQueryRenderer::CreateOutputImage()
    {
        const auto extent = SwapChain().Extent();
        const auto format = SwapChain().Format();
        const auto tiling = VK_IMAGE_TILING_OPTIMAL;
        
        rtAccumulation_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "accumulate"));
        rtOutput_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false, "output0"));
        rtPingPong0.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "pingpong0"));
        rtPingPong1.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "pingpong1"));
        rtMotionVector_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "motionvector"));
        rtVisibility0_.reset(new RenderImage(Device(), extent, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "vis0"));
        rtVisibility1_.reset(new RenderImage(Device(), extent, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "vis1"));

        rtAlbedo_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, true, "albedo"));
        rtNormal_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, true, "albedo"));
        
        rtAdaptiveSample_.reset(new RenderImage(Device(), extent, VK_FORMAT_R8_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "adaptive sample"));
    }
}
