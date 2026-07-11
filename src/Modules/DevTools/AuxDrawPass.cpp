#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/DevTools/AuxDrawPass.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Vulkan::AuxDraw
{
    FExternalPassContract AuxDrawPass::Contract() const
    {
        return {
            .name = "AuxDraw",
            .requiredOutputs = static_cast<uint32_t>(ERenderOutput::Color | ERenderOutput::Depth),
            .producedOutputs = static_cast<uint32_t>(ERenderOutput::Color),
        };
    }

    namespace
    {
        constexpr const char* vertexShaderPath = "assets/shaders/Debug.AuxDraw.vert.slang.spv";
        constexpr const char* fragmentShaderPath = "assets/shaders/Debug.AuxDraw.frag.slang.spv";
        constexpr uint32_t maxPrimitiveCount = 81920;

        struct FAuxDrawPushConstants
        {
            VkDeviceAddress cameraAddress = 0;
            VkDeviceAddress primitiveAddress = 0;
            uint32_t primitiveCount = 0;
            uint32_t outputWidth = 0;
            uint32_t outputHeight = 0;
            uint32_t padding = 0;
        };
        static_assert(sizeof(FAuxDrawPushConstants) == 32);

        std::string ShaderFilename(const std::string& shaderFile)
        {
            return std::filesystem::path(shaderFile).filename().string();
        }

        bool IsShaderChanged(const char* shaderFile, const std::set<std::string>& changedShaderFiles)
        {
            return changedShaderFiles.find(ShaderFilename(shaderFile)) != changedShaderFiles.end();
        }

        void MarkShaderHandled(const char* shaderFile, std::set<std::string>& handledShaderFiles)
        {
            handledShaderFiles.insert(ShaderFilename(shaderFile));
        }
    }

    AuxDrawPass::AuxDrawPass(VulkanBaseRenderer& renderer) : renderer_(renderer) {}

    AuxDrawPass::~AuxDrawPass()
    {
        ReleaseResources();
    }

    void AuxDrawPass::CreateResources()
    {
        ReleaseResources();
        frameBuffers_.resize(renderer_.SwapChain().Images().size());
        CreateRenderPassAndFramebuffer();
        RecreateGraphicsPipeline();
    }

    void AuxDrawPass::ReleaseResources()
    {
        frameBuffers_.clear();
        stagingPrimitives_.clear();
        DestroyGraphicsResources();
    }

    void AuxDrawPass::DestroyGraphicsResources()
    {
        const Device& device = renderer_.Device();
        if (pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device.Handle(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        pipelineLayout_.reset();
        if (framebuffer_ != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device.Handle(), framebuffer_, nullptr);
            framebuffer_ = VK_NULL_HANDLE;
        }
        if (renderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device.Handle(), renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
    }

    void AuxDrawPass::CreateRenderPassAndFramebuffer()
    {
        const RenderImage* outputImage = renderer_.GetViewStorageImage(Assets::Bindless::RT_DENOISED);
        if (outputImage == nullptr)
        {
            return;
        }

        const Device& device = renderer_.Device();
        const VkFormat colorFormat = outputImage->GetImage().Format();
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;

        attachments[1].format = renderer_.DepthBuffer().Format();
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference depthReference{};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        Check(vkCreateRenderPass(device.Handle(), &renderPassInfo, nullptr, &renderPass_),
              "create AuxDraw render pass");
        device.DebugUtils().SetObjectName(renderPass_, "AuxDraw Render Pass");

        const VkImageView framebufferAttachments[]{
            outputImage->GetImageView().Handle(),
            renderer_.DepthBuffer().ImageView().Handle(),
        };
        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = framebufferAttachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;
        Check(vkCreateFramebuffer(device.Handle(), &framebufferInfo, nullptr, &framebuffer_),
              "create AuxDraw framebuffer");
        device.DebugUtils().SetObjectName(framebuffer_, "AuxDraw Framebuffer");
    }

    void AuxDrawPass::RecreateGraphicsPipeline()
    {
        if (renderPass_ == VK_NULL_HANDLE)
        {
            return;
        }

        const Device& device = renderer_.Device();
        if (pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device.Handle(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = sizeof(FAuxDrawPushConstants);
        pipelineLayout_ = std::make_unique<PipelineLayout>(device, &pushRange, 1);

        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        const ShaderModule vertexShader(device, vertexShaderPath);
        const ShaderModule fragmentShader(device, fragmentShaderPath);
        pipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertexShader, fragmentShader)
            .SetFixedViewport({0, 0}, extent)
            .SetDepth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
            .SetAlphaBlend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
            .Build(pipelineLayout_->Handle(), renderPass_, "create AuxDraw graphics pipeline");
    }

    void AuxDrawPass::ReloadShaders(
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        if (!IsShaderChanged(vertexShaderPath, changedShaderFiles) &&
            !IsShaderChanged(fragmentShaderPath, changedShaderFiles))
        {
            return;
        }

        RecreateGraphicsPipeline();
        if (IsShaderChanged(vertexShaderPath, changedShaderFiles))
        {
            MarkShaderHandled(vertexShaderPath, handledShaderFiles);
        }
        if (IsShaderChanged(fragmentShaderPath, changedShaderFiles))
        {
            MarkShaderHandled(fragmentShaderPath, handledShaderFiles);
        }
    }

    void AuxDrawPass::EnsureFrameBufferCapacity(uint32_t imageIndex, VkDeviceSize requiredSize)
    {
        if (imageIndex >= frameBuffers_.size())
        {
            frameBuffers_.resize(imageIndex + 1);
        }

        FrameBuffer& frameBuffer = frameBuffers_[imageIndex];
        if (frameBuffer.buffer && frameBuffer.size >= requiredSize)
        {
            return;
        }

        const VkDeviceSize alignedSize =
            std::max<VkDeviceSize>(requiredSize, sizeof(DevTools::FAuxPrimitiveGpu));
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            renderer_.CommandPool(),
            "AuxDraw Primitive",
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            alignedSize,
            frameBuffer.buffer,
            frameBuffer.memory);
        frameBuffer.size = alignedSize;
    }

    void AuxDrawPass::Execute(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (pipeline_ == VK_NULL_HANDLE || pipelineLayout_ == nullptr || framebuffer_ == VK_NULL_HANDLE)
        {
            return;
        }

        auto* auxDraw = dynamic_cast<DevTools::FAuxDrawSystem*>(NextEngine::GetInstance()->GetDebugDraw());
        if (!auxDraw)
        {
            return;
        }
        auxDraw->ConsumeFramePrimitives(stagingPrimitives_, maxPrimitiveCount);
        if (stagingPrimitives_.empty())
        {
            return;
        }

        const VkExtent2D extent = renderer_.ActiveViewRenderExtent();
        if (extent.width == 0 || extent.height == 0)
        {
            return;
        }

        const VkDeviceSize uploadSize =
            static_cast<VkDeviceSize>(stagingPrimitives_.size()) * sizeof(DevTools::FAuxPrimitiveGpu);
        EnsureFrameBufferCapacity(imageIndex, uploadSize);
        if (imageIndex >= frameBuffers_.size() || !frameBuffers_[imageIndex].buffer || !frameBuffers_[imageIndex].memory)
        {
            return;
        }

        FrameBuffer& frameBuffer = frameBuffers_[imageIndex];
        void* mapped = frameBuffer.memory->Map(0, uploadSize);
        std::memcpy(mapped, stagingPrimitives_.data(), static_cast<size_t>(uploadSize));
        frameBuffer.memory->Unmap();

        BufferMemoryBarrier::Insert(
            commandBuffer,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            frameBuffer.buffer->Handle(),
            VK_ACCESS_HOST_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            0,
            uploadSize);

        const RenderImage* outputImage = renderer_.GetViewStorageImage(Assets::Bindless::RT_DENOISED);
        if (outputImage == nullptr)
        {
            return;
        }
        outputImage->InsertBarrier(
            commandBuffer,
            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL);

        VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        renderPassInfo.renderPass = renderPass_;
        renderPassInfo.framebuffer = framebuffer_;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = extent;
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

        FAuxDrawPushConstants push{};
        push.cameraAddress = renderer_.ActiveViewCameraAddress(imageIndex);
        push.primitiveAddress = frameBuffer.buffer->GetDeviceAddress();
        push.primitiveCount = static_cast<uint32_t>(stagingPrimitives_.size());
        push.outputWidth = extent.width;
        push.outputHeight = extent.height;

        {
            SCOPED_GPU_TIMER("aux draw");
            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
            vkCmdPushConstants(
                commandBuffer,
                pipelineLayout_->Handle(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(FAuxDrawPushConstants),
                &push);
            vkCmdDraw(commandBuffer, push.primitiveCount * 6, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);
        }

        outputImage->InsertBarrier(
            commandBuffer,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL);
    }
}
