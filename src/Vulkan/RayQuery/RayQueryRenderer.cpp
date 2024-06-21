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
        rayTracingPipeline_.reset(new RayQueryPipeline(Device().GetDeviceProcedures(), SwapChain(), topAs_[0], *outputImageView_, UniformBuffers(), GetScene()));
    }

    void RayQueryRenderer::DeleteSwapChain()
    {
        rayTracingPipeline_.reset();
        outputImageView_.reset();
        outputImage_.reset();
        outputImageMemory_.reset();
        
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
        ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange, 0,
                                   VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        
        // Execute ray tracing shaders.
        {
            SCOPED_GPU_TIMER("rt pass");
            VkDescriptorSet DescriptorSets[] = {rayTracingPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, rayTracingPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    rayTracingPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);

            // Acquire output image and swap-chain image for copying.
            ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
                                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
                                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }
        
        // compose with first bounce
        {
            SCOPED_GPU_TIMER("blit pass");
            
            // Copy output image into swap-chain image.
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {extent.width, extent.height, 1};

            vkCmdCopyImage(commandBuffer,
                           outputImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
    }

    void RayQueryRenderer::CreateOutputImage()
    {
        const auto extent = SwapChain().Extent();
        const auto format = SwapChain().Format();
        const auto tiling = VK_IMAGE_TILING_OPTIMAL;
        
        outputImage_.reset(new Image(Device(), extent, format, tiling,
                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        outputImageMemory_.reset(new DeviceMemory(outputImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        outputImageView_.reset(new ImageView(Device(), outputImage_->Handle(), format, VK_IMAGE_ASPECT_COLOR_BIT));
        
        const auto& debugUtils = Device().DebugUtils();
        
        debugUtils.SetObjectName(outputImage_->Handle(), "Output Image");
        debugUtils.SetObjectName(outputImageMemory_->Handle(), "Output Image Memory");
        debugUtils.SetObjectName(outputImageView_->Handle(), "Output ImageView");
    }
}
