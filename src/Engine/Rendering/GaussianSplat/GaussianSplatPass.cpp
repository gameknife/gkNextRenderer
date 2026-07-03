#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/GaussianSplat/GaussianSplatPass.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Components/GaussianSplatComponent.h"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <filesystem>

namespace Vulkan::GaussianSplat
{
    namespace
    {
        constexpr uint32_t maxSplatBucketCount = 16u * 1024u;
        constexpr uint32_t splatSortGroupSize = 256;

        struct alignas(16) FSplatPushConstants
        {
            VkDeviceAddress camera;
            VkDeviceAddress splats;
            VkDeviceAddress palette;
            VkDeviceAddress sortedIndices;
            VkDeviceAddress modelStates;
            uint32_t count;
            uint32_t width;
            uint32_t height;
            float sigma;
        };
        static_assert(sizeof(FSplatPushConstants) == 64);

        struct FSplatSortPushConstants
        {
            VkDeviceAddress camera;
            VkDeviceAddress splats;
            VkDeviceAddress modelStates;
            VkDeviceAddress sortedIndices;
            VkDeviceAddress bucketCounts;
            VkDeviceAddress bucketOffsets;
            VkDeviceAddress bucketCursors;
            VkDeviceAddress drawIndirect;
            uint32_t count;
            uint32_t width;
            uint32_t height;
            uint32_t bucketCount;
            float nearPlane;
            float farPlane;
            uint32_t reserved0;
            uint32_t reserved1;
        };
        static_assert(sizeof(FSplatSortPushConstants) == 96);

        struct FSplatComposePushConstants
        {
            VkDeviceAddress camera;
        };
        static_assert(sizeof(FSplatComposePushConstants) == 8);

        struct alignas(16) FSplatModelState
        {
            glm::mat4 world{1.0f};
            glm::vec4 parameters{1.0f, 1.0f, 0.0f, 0.0f}; // opacity, visible, antialias, SH basis flip XY
        };
        static_assert(sizeof(FSplatModelState) == 80);

        std::string ShaderFilename(const std::string& shaderFile)
        {
            return std::filesystem::path(shaderFile).filename().string();
        }

        bool MarkChangedShaderFile(
            const std::string& shaderFile,
            const std::set<std::string>& changedShaderFiles,
            std::set<std::string>& handledShaderFiles)
        {
            const std::string filename = ShaderFilename(shaderFile);
            if (changedShaderFiles.find(filename) == changedShaderFiles.end())
            {
                return false;
            }
            handledShaderFiles.insert(filename);
            return true;
        }

        uint32_t AutoBucketCount(uint32_t splatCount)
        {
            if (splatCount == 0) return 4096;
            const float targetBits = std::log2(std::max(1.0f, static_cast<float>(splatCount) / 96.0f));
            const int bits = std::clamp(static_cast<int>(std::round(targetBits)), 12, 14);
            return 1u << bits;
        }
    }

    GaussianSplatPass::GaussianSplatPass(VulkanBaseRenderer& renderer) : renderer_(renderer)
    {
    }

    GaussianSplatPass::~GaussianSplatPass()
    {
        DestroyResources();
    }

    void GaussianSplatPass::CreateResources()
    {
        DestroyResources();
        const auto& models = renderer_.GetScene().GaussianSplats();
        if (models.empty()) return;

        std::vector<Assets::FGaussianSplatGpu> combinedSplats;
        std::vector<glm::vec4> combinedPalette;
        for (const auto& model : models)
        {
            combinedSplats.reserve(combinedSplats.size() + model.splats.size());
            combinedPalette.reserve(combinedPalette.size() + model.shPalette.size());
        }
        for (uint32_t modelIndex = 0; modelIndex < models.size(); ++modelIndex)
        {
            const auto& model = models[modelIndex];
            const uint32_t paletteBase = static_cast<uint32_t>(combinedPalette.size());
            combinedPalette.insert(combinedPalette.end(), model.shPalette.begin(), model.shPalette.end());
            for (const auto& sourceSplat : model.splats)
            {
                auto splat = sourceSplat;
                splat.metadata.z = paletteBase;
                splat.metadata.w = modelIndex;
                combinedSplats.push_back(splat);
            }
        }
        if (combinedSplats.empty()) return;
        if (combinedPalette.empty()) combinedPalette.emplace_back(0.0f);

        const VkBufferUsageFlags bufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        BufferUtil::CreateDeviceBuffer(renderer_.CommandPool(), "GaussianSplats", bufferUsage,
                                       combinedSplats, splatBuffer_, splatMemory_);
        BufferUtil::CreateDeviceBuffer(renderer_.CommandPool(), "GaussianSplatShPalette", bufferUsage,
                                       combinedPalette, paletteBuffer_, paletteMemory_);

        modelCount_ = static_cast<uint32_t>(models.size());
        const size_t modelStateSize = sizeof(FSplatModelState) * modelCount_;
        modelStateBuffers_.resize(renderer_.UniformBuffers().size());
        modelStateMemories_.resize(renderer_.UniformBuffers().size());
        mappedModelStates_.resize(renderer_.UniformBuffers().size());
        for (size_t imageIndex = 0; imageIndex < modelStateBuffers_.size(); ++imageIndex)
        {
            BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatModelStates", bufferUsage,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, modelStateSize,
                modelStateBuffers_[imageIndex], modelStateMemories_[imageIndex]);
            mappedModelStates_[imageIndex] = modelStateMemories_[imageIndex]->Map(0, modelStateSize);
        }

        splatCount_ = static_cast<uint32_t>(combinedSplats.size());
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatSortedIndices", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, splatCount_ * sizeof(uint32_t),
            sortedIndexBuffer_, sortedIndexMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatBucketCounts", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, maxSplatBucketCount * sizeof(uint32_t),
            bucketCountBuffer_, bucketCountMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatBucketOffsets", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, maxSplatBucketCount * sizeof(uint32_t),
            bucketOffsetBuffer_, bucketOffsetMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatBucketCursors", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, maxSplatBucketCount * sizeof(uint32_t),
            bucketCursorBuffer_, bucketCursorMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatDrawIndirect",
            bufferUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(VkDrawIndirectCommand), drawIndirectBuffer_, drawIndirectMemory_);

        histogramPipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            renderer_.SwapChain(), "assets/shaders/Splat.SortHistogram.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FSplatSortPushConstants)));
        prefixPipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            renderer_.SwapChain(), "assets/shaders/Splat.SortPrefix.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FSplatSortPushConstants)));
        scatterPipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            renderer_.SwapChain(), "assets/shaders/Splat.SortScatter.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FSplatSortPushConstants)));
        composePipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            renderer_.SwapChain(), "assets/shaders/Splat.Compose.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FSplatComposePushConstants)));

        const Device& device = renderer_.Device();
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
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

        VkAttachmentReference colorReference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthReference{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        subpass.pDepthStencilAttachment = &depthReference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        renderPassInfo.attachmentCount = 2;
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        Check(vkCreateRenderPass(device.Handle(), &renderPassInfo, nullptr, &renderPass_),
              "create Gaussian splat render pass");

        const VkImageView framebufferAttachments[]{
            renderer_.GetViewStorageImage(Assets::Bindless::RT_SPLAT_ACCUM)->GetImageView().Handle(),
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
        Check(vkCreateFramebuffer(device.Handle(), &framebufferInfo, nullptr, &frameBuffer_),
              "create Gaussian splat framebuffer");

        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.size = sizeof(FSplatPushConstants);
        pipelineLayout_ = std::make_unique<PipelineLayout>(device, &pushRange, 1);
        RecreateGraphicsPipeline();

        SPDLOG_INFO("uploaded {} Gaussian splats in {} models ({} SH palette entries)",
                    combinedSplats.size(), modelCount_, combinedPalette.size());
    }

    void GaussianSplatPass::DestroyResources()
    {
        composePipeline_.reset();
        scatterPipeline_.reset();
        prefixPipeline_.reset();
        histogramPipeline_.reset();
        drawIndirectBuffer_.reset();
        drawIndirectMemory_.reset();
        bucketCursorBuffer_.reset();
        bucketCursorMemory_.reset();
        bucketOffsetBuffer_.reset();
        bucketOffsetMemory_.reset();
        bucketCountBuffer_.reset();
        bucketCountMemory_.reset();
        sortedIndexBuffer_.reset();
        sortedIndexMemory_.reset();
        paletteBuffer_.reset();
        paletteMemory_.reset();
        for (size_t index = 0; index < modelStateMemories_.size(); ++index)
        {
            if (mappedModelStates_[index]) modelStateMemories_[index]->Unmap();
        }
        mappedModelStates_.clear();
        modelStateBuffers_.clear();
        modelStateMemories_.clear();
        splatBuffer_.reset();
        splatMemory_.reset();
        splatCount_ = 0;
        modelCount_ = 0;

        const Device& device = renderer_.Device();
        if (pipeline_) vkDestroyPipeline(device.Handle(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        pipelineLayout_.reset();
        if (frameBuffer_) vkDestroyFramebuffer(device.Handle(), frameBuffer_, nullptr);
        frameBuffer_ = VK_NULL_HANDLE;
        if (renderPass_) vkDestroyRenderPass(device.Handle(), renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    void GaussianSplatPass::RecreateGraphicsPipeline()
    {
        if (!renderPass_ || !pipelineLayout_)
        {
            return;
        }

        const Device& device = renderer_.Device();
        if (pipeline_)
        {
            vkDestroyPipeline(device.Handle(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }

        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        const ShaderModule vertexShader(device, "assets/shaders/Splat.Billboard.vert.slang.spv");
        const ShaderModule fragmentShader(device, "assets/shaders/Splat.Billboard.frag.slang.spv");
        pipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertexShader, fragmentShader)
            .SetFixedViewport({0, 0}, extent)
            .SetDepth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
            .SetAlphaBlend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
            .Build(pipelineLayout_->Handle(), renderPass_, "recreate Gaussian splat graphics pipeline");
    }

    void GaussianSplatPass::ReloadShaders(
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        if (histogramPipeline_)
        {
            histogramPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
        if (prefixPipeline_)
        {
            prefixPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
        if (scatterPipeline_)
        {
            scatterPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }
        if (composePipeline_)
        {
            composePipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
        }

        constexpr const char* vertexShader = "assets/shaders/Splat.Billboard.vert.slang.spv";
        constexpr const char* fragmentShader = "assets/shaders/Splat.Billboard.frag.slang.spv";
        const bool reloadVertex = changedShaderFiles.find(ShaderFilename(vertexShader)) != changedShaderFiles.end();
        const bool reloadFragment = changedShaderFiles.find(ShaderFilename(fragmentShader)) != changedShaderFiles.end();
        if (reloadVertex || reloadFragment)
        {
            RecreateGraphicsPipeline();
            MarkChangedShaderFile(vertexShader, changedShaderFiles, handledShaderFiles);
            MarkChangedShaderFile(fragmentShader, changedShaderFiles, handledShaderFiles);
        }
    }

    void GaussianSplatPass::UpdateModelStates(uint32_t imageIndex)
    {
        std::vector<FSplatModelState> states(modelCount_);
        const auto& models = renderer_.GetScene().GaussianSplats();
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        for (uint32_t modelIndex = 0; modelIndex < modelCount_; ++modelIndex)
        {
            const auto node = renderer_.GetScene().GetNodeSharedByInstanceId(models[modelIndex].nodeInstanceId);
            if (!node)
            {
                states[modelIndex].parameters.y = 0.0f;
                continue;
            }

            states[modelIndex].world = node->WorldTransform();
            states[modelIndex].parameters.z = (models[modelIndex].antialias || settings.SplatForceAA)
                ? std::clamp(settings.SplatAAStrength, 0.0f, 1.0f)
                : 0.0f;
            states[modelIndex].parameters.w = models[modelIndex].shBasisFlipXY ? 1.0f : 0.0f;
            if (const auto* component = node->GetComponentPtr<Runtime::GaussianSplatComponent>())
            {
                states[modelIndex].parameters.x = component->GetOpacityScale();
                states[modelIndex].parameters.y = component->GetVisible() ? 1.0f : 0.0f;
            }
        }
        std::memcpy(mappedModelStates_[imageIndex], states.data(), states.size() * sizeof(FSplatModelState));
    }

    void GaussianSplatPass::DispatchGpuSort(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        const uint32_t activeSplatCount =
            settings.SplatMaxCount == 0 ? splatCount_ : std::min(splatCount_, settings.SplatMaxCount);
        const uint32_t bucketCount = std::clamp(
            std::max(settings.SplatBucketCount, AutoBucketCount(activeSplatCount)), 16u, maxSplatBucketCount);

        VkMemoryBarrier previousFrameBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        previousFrameBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                                             VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        previousFrameBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &previousFrameBarrier, 0, nullptr, 0, nullptr);

        const VkDeviceSize bucketBufferSize = bucketCount * sizeof(uint32_t);
        vkCmdFillBuffer(commandBuffer, bucketCountBuffer_->Handle(), 0, bucketBufferSize, 0);
        vkCmdFillBuffer(commandBuffer, drawIndirectBuffer_->Handle(), 0, VK_WHOLE_SIZE, 0);

        std::array<VkBufferMemoryBarrier, 2> clearBarriers{};
        for (auto& barrier : clearBarriers)
        {
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
        }
        clearBarriers[0].buffer = bucketCountBuffer_->Handle();
        clearBarriers[0].size = bucketBufferSize;
        clearBarriers[1].buffer = drawIndirectBuffer_->Handle();
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(clearBarriers.size()), clearBarriers.data(),
                             0, nullptr);

        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        const auto camera = NextEngine::GetInstance()->GetScene().GetRenderCamera();
        const FSplatSortPushConstants push{
            renderer_.UniformBuffers()[imageIndex].Buffer().GetDeviceAddress(), splatBuffer_->GetDeviceAddress(),
            modelStateBuffers_[imageIndex]->GetDeviceAddress(), sortedIndexBuffer_->GetDeviceAddress(),
            bucketCountBuffer_->GetDeviceAddress(),
            bucketOffsetBuffer_->GetDeviceAddress(), bucketCursorBuffer_->GetDeviceAddress(),
            drawIndirectBuffer_->GetDeviceAddress(), activeSplatCount, extent.width, extent.height, bucketCount,
            std::max(camera.NearPlane, 1e-4f), std::max(camera.FarPlane, camera.NearPlane + 1e-3f), 0u, 0u};

        histogramPipeline_->BindPipeline(commandBuffer, &push);
        vkCmdDispatch(commandBuffer, (activeSplatCount + splatSortGroupSize - 1) / splatSortGroupSize, 1, 1);

        VkBufferMemoryBarrier histogramBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        histogramBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        histogramBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        histogramBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histogramBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        histogramBarrier.buffer = bucketCountBuffer_->Handle();
        histogramBarrier.offset = 0;
        histogramBarrier.size = bucketBufferSize;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &histogramBarrier, 0, nullptr);

        prefixPipeline_->BindPipeline(commandBuffer, &push);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        std::array<VkBufferMemoryBarrier, 3> prefixBarriers{};
        const std::array<VkBuffer, 3> prefixBuffers{
            bucketOffsetBuffer_->Handle(), bucketCursorBuffer_->Handle(), drawIndirectBuffer_->Handle()};
        for (size_t index = 0; index < prefixBarriers.size(); ++index)
        {
            auto& barrier = prefixBarriers[index];
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = prefixBuffers[index];
            barrier.offset = 0;
            barrier.size = index < 2 ? bucketBufferSize : VK_WHOLE_SIZE;
        }
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                             static_cast<uint32_t>(prefixBarriers.size()), prefixBarriers.data(), 0, nullptr);

        scatterPipeline_->BindPipeline(commandBuffer, &push);
        vkCmdDispatch(commandBuffer, (activeSplatCount + splatSortGroupSize - 1) / splatSortGroupSize, 1, 1);

        std::array<VkBufferMemoryBarrier, 2> drawBarriers{};
        drawBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        drawBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        drawBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        drawBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[0].buffer = sortedIndexBuffer_->Handle();
        drawBarriers[0].offset = 0;
        drawBarriers[0].size = VK_WHOLE_SIZE;
        drawBarriers[1] = drawBarriers[0];
        drawBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        drawBarriers[1].buffer = drawIndirectBuffer_->Handle();
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(drawBarriers.size()), drawBarriers.data(),
                             0, nullptr);
    }

    void GaussianSplatPass::Execute(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!pipeline_ || !NextEngine::GetInstance()->GetShowFlags().ShowGaussianSplats) return;
        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        UpdateModelStates(imageIndex);

        VkBufferMemoryBarrier modelStateBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        modelStateBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        modelStateBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        modelStateBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        modelStateBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        modelStateBarrier.buffer = modelStateBuffers_[imageIndex]->Handle();
        modelStateBarrier.offset = 0;
        modelStateBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                             0, 0, nullptr, 1, &modelStateBarrier, 0, nullptr);

        DispatchGpuSort(commandBuffer, imageIndex);

        VkRenderPassBeginInfo beginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        beginInfo.renderPass = renderPass_;
        beginInfo.framebuffer = frameBuffer_;
        beginInfo.renderArea.extent = extent;
        VkClearValue clearValue{};
        beginInfo.clearValueCount = 1;
        beginInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        const FSplatPushConstants push{
            renderer_.UniformBuffers()[imageIndex].Buffer().GetDeviceAddress(),
            splatBuffer_->GetDeviceAddress(), paletteBuffer_->GetDeviceAddress(),
            sortedIndexBuffer_->GetDeviceAddress(), modelStateBuffers_[imageIndex]->GetDeviceAddress(), splatCount_,
            extent.width, extent.height, std::clamp(settings.SplatSigma, 1.0f, 4.0f)};
        vkCmdPushConstants(commandBuffer, pipelineLayout_->Handle(), VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(push), &push);
        vkCmdDrawIndirect(commandBuffer, drawIndirectBuffer_->Handle(), 0, 1, sizeof(VkDrawIndirectCommand));
        vkCmdEndRenderPass(commandBuffer);

        renderer_.GetViewStorageImage(Assets::Bindless::RT_SPLAT_ACCUM)->InsertBarrier(
            commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        renderer_.GetViewStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(
            commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

        const FSplatComposePushConstants composePush{
            renderer_.UniformBuffers()[imageIndex].Buffer().GetDeviceAddress()};
        composePipeline_->BindPipeline(commandBuffer, &composePush);
        vkCmdDispatch(commandBuffer, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);

        renderer_.GetViewStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(
            commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    }
}
