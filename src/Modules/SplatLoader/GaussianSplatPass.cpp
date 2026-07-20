#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/SplatLoader/GaussianSplatPass.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/SplatLoader/GaussianSplatComponent.h"
#include "Modules/SplatLoader/SplatSettings.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"


namespace Vulkan::GaussianSplat
{
    FExternalPassContract GaussianSplatPass::Contract() const
    {
        return {
            .name = "GaussianSplat",
            .requiredOutputs = static_cast<uint32_t>(ERenderOutput::Color | ERenderOutput::Depth),
            .producedOutputs = static_cast<uint32_t>(ERenderOutput::Color),
        };
    }

    namespace
    {
        struct FSplatModel
        {
            const Assets::Node* node;
            const Runtime::GaussianSplatComponent* component;
            const Assets::FGaussianSplatData* data;
        };

        std::vector<FSplatModel> GatherSplatModels(const Assets::Scene& scene)
        {
            std::vector<FSplatModel> result;
            for (const auto& node : scene.Nodes())
            {
                const auto* component = node ? node->GetComponentPtr<Runtime::GaussianSplatComponent>() : nullptr;
                if (component && component->GetData())
                {
                    result.push_back({node.get(), component, component->GetData().get()});
                }
            }
            return result;
        }

        constexpr uint32_t maxSplatBucketCount = 16u * 1024u;
        constexpr uint32_t splatSortGroupSize = 256;

        struct alignas(16) FSplatPushConstants
        {
            Assets::GPUScene gpuScene;
            VkDeviceAddress splats;
            VkDeviceAddress palette;
            VkDeviceAddress sortedIndices;
            VkDeviceAddress modelStates;
            VkDeviceAddress lightingGrid;
            uint32_t count;
            uint32_t width;
            uint32_t height;
            float sigma;
        };
        static_assert(sizeof(FSplatPushConstants) == 192);

        struct FSplatSortPushConstants
        {
            VkDeviceAddress camera;
            VkDeviceAddress splats;
            VkDeviceAddress modelStates;
            VkDeviceAddress sortedIndices;
            VkDeviceAddress bucketCounts;
            VkDeviceAddress bucketOffsets;
            // Carries the per-splat bucket cache address; keep the legacy slot so the payload fits
            // the shared zero-bind custom push-constant block.
            VkDeviceAddress bucketCursors;
            VkDeviceAddress groupBucketCounts;
            VkDeviceAddress groupBucketOffsets;
            VkDeviceAddress drawIndirect;
            uint32_t count;
            uint32_t width;
            uint32_t height;
            uint32_t bucketCount;
            float nearPlane;
            float farPlane;
            uint32_t groupCount;
            uint32_t groupSize;
        };
        static_assert(sizeof(FSplatSortPushConstants) == 112);

        struct FSplatComposePushConstants
        {
            VkDeviceAddress camera;
        };
        static_assert(sizeof(FSplatComposePushConstants) == 8);

        struct alignas(16) FSplatModelState
        {
            glm::mat4 world{1.0f};
            glm::vec4 parameters{1.0f, 1.0f, 0.0f, 0.0f}; // opacity, visible, antialias, SH basis flip XY
            glm::vec4 lightingParameters{0.0f}; // receive, strength, reserved, reserved
        };
        static_assert(sizeof(FSplatModelState) == 96);

        uint32_t AutoBucketCount(uint32_t splatCount)
        {
            if (splatCount == 0) return 4096;
            const float targetBits = std::log2(std::max(1.0f, static_cast<float>(splatCount) / 96.0f));
            const int bits = std::clamp(static_cast<int>(std::round(targetBits)), 12, 14);
            return 1u << bits;
        }

        void HashBytes(uint64_t& hash, const void* data, size_t size)
        {
            constexpr uint64_t fnvPrime = 1099511628211ull;
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t index = 0; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= fnvPrime;
            }
        }

        template <typename T>
        void HashValue(uint64_t& hash, const T& value)
        {
            HashBytes(hash, &value, sizeof(T));
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
        const auto models = GatherSplatModels(renderer_.GetScene());
        if (models.empty()) return;

        std::vector<Assets::FGaussianSplatGpu> combinedSplats;
        std::vector<glm::vec4> combinedPalette;
        for (const auto& model : models)
        {
            combinedSplats.reserve(combinedSplats.size() + model.data->splats.size());
            combinedPalette.reserve(combinedPalette.size() + model.data->shPalette.size());
        }
        for (uint32_t modelIndex = 0; modelIndex < models.size(); ++modelIndex)
        {
            const auto& model = *models[modelIndex].data;
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
        const auto settings = Modules::Splat::GetSettings(*NextEngine::GetInstance());
        sortBucketCapacity_ = std::clamp(
            std::max(settings->bucketCount, AutoBucketCount(splatCount_)), 16u, maxSplatBucketCount);
        sortGroupCountCapacity_ = (splatCount_ + splatSortGroupSize - 1) / splatSortGroupSize;
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatSortedIndices", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, splatCount_ * sizeof(uint32_t),
            sortedIndexBuffer_, sortedIndexMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatBucketCounts", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, maxSplatBucketCount * sizeof(uint32_t),
            bucketCountBuffer_, bucketCountMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatBucketOffsets", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, maxSplatBucketCount * sizeof(uint32_t),
            bucketOffsetBuffer_, bucketOffsetMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatSortBuckets", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, splatCount_ * sizeof(uint32_t),
            splatBucketBuffer_, splatBucketMemory_);
        const VkDeviceSize groupBucketBufferSize =
            VkDeviceSize(sortBucketCapacity_) * VkDeviceSize(std::max(sortGroupCountCapacity_, 1u)) * sizeof(uint32_t);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatGroupBucketCounts", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, groupBucketBufferSize,
            groupBucketCountBuffer_, groupBucketCountMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatGroupBucketOffsets", bufferUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, groupBucketBufferSize,
            groupBucketOffsetBuffer_, groupBucketOffsetMemory_);
        BufferUtil::CreateDeviceBufferLocal(renderer_.CommandPool(), "GaussianSplatDrawIndirect",
            bufferUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(VkDrawIndirectCommand), drawIndirectBuffer_, drawIndirectMemory_);

        histogramPipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            renderer_.SwapChain(), "assets/shaders/Splat.SortHistogram.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FSplatSortPushConstants)));
        prefixPipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            renderer_.SwapChain(), "assets/shaders/Splat.SortPrefix.comp.slang.spv",
            static_cast<uint32_t>(sizeof(FSplatSortPushConstants)));
        groupScanPipeline_ = std::make_unique<PipelineCommon::ZeroBindCustomPushConstantPipeline>(
            renderer_.SwapChain(), "assets/shaders/Splat.SortGroupScan.comp.slang.spv",
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
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.size = sizeof(FSplatPushConstants);
        std::vector<DescriptorSetManager*> managers = {
            &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
        };
        pipelineLayout_ = std::make_unique<PipelineLayout>(device, managers, 1, &pushRange, 1);
        CreateGraphicsPipeline();

        SPDLOG_INFO("uploaded {} Gaussian splats in {} models ({} SH palette entries)",
                    combinedSplats.size(), modelCount_, combinedPalette.size());
    }

    void GaussianSplatPass::DestroyResources()
    {
        composePipeline_.reset();
        scatterPipeline_.reset();
        groupScanPipeline_.reset();
        prefixPipeline_.reset();
        histogramPipeline_.reset();
        drawIndirectBuffer_.reset();
        drawIndirectMemory_.reset();
        groupBucketOffsetBuffer_.reset();
        groupBucketOffsetMemory_.reset();
        groupBucketCountBuffer_.reset();
        groupBucketCountMemory_.reset();
        splatBucketBuffer_.reset();
        splatBucketMemory_.reset();
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
        sortBucketCapacity_ = 0;
        sortGroupCountCapacity_ = 0;
        currentSortModelStateHash_ = 0;
        lastSortCacheKey_ = 0;
        sortCacheValid_ = false;

        const Device& device = renderer_.Device();
        if (pipeline_) vkDestroyPipeline(device.Handle(), pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        pipelineLayout_.reset();
        if (frameBuffer_) vkDestroyFramebuffer(device.Handle(), frameBuffer_, nullptr);
        frameBuffer_ = VK_NULL_HANDLE;
        if (renderPass_) vkDestroyRenderPass(device.Handle(), renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    void GaussianSplatPass::CreateGraphicsPipeline()
    {
        const Device& device = renderer_.Device();
        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        const ShaderModule vertexShader(device, "assets/shaders/Splat.Billboard.vert.slang.spv");
        const ShaderModule fragmentShader(device, "assets/shaders/Splat.Billboard.frag.slang.spv");
        pipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertexShader, fragmentShader)
            .SetFixedViewport({0, 0}, extent)
            .SetDepth(true, false, VK_COMPARE_OP_LESS_OR_EQUAL)
            .SetAlphaBlend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
            .Build(pipelineLayout_->Handle(), renderPass_, "create Gaussian splat graphics pipeline");
    }

    void GaussianSplatPass::UpdateModelStates(uint32_t imageIndex)
    {
        std::vector<FSplatModelState> states(modelCount_);
        uint64_t sortModelStateHash = 14695981039346656037ull;
        const auto models = GatherSplatModels(renderer_.GetScene());
        const auto settings = Modules::Splat::GetSettings(*NextEngine::GetInstance());
        for (uint32_t modelIndex = 0; modelIndex < modelCount_; ++modelIndex)
        {
            if (modelIndex >= models.size() || !models[modelIndex].node)
            {
                states[modelIndex].parameters.y = 0.0f;
                continue;
            }

            const auto& model = models[modelIndex];
            states[modelIndex].world = model.node->WorldTransform();
            states[modelIndex].parameters.z = (model.data->antialias || settings->forceAA)
                ? std::clamp(settings->aaStrength, 0.0f, 1.0f)
                : 0.0f;
            states[modelIndex].parameters.w = model.data->shBasisFlipXY ? 1.0f : 0.0f;
            if (const auto* component = model.component)
            {
                states[modelIndex].parameters.x = component->GetOpacityScale();
                states[modelIndex].parameters.y = component->GetVisible() ? 1.0f : 0.0f;
                const bool receiveLighting = settings->receiveLighting && component->GetReceiveLighting();
                const float globalStrength = std::clamp(settings->lightingStrength / 0.35f, 0.0f, 4.0f);
                const float strength = receiveLighting
                    ? std::clamp(component->GetLightingStrength() * globalStrength, 0.0f, 1.0f)
                    : 0.0f;
                states[modelIndex].lightingParameters = glm::vec4(receiveLighting ? 1.0f : 0.0f, strength, 0.0f, 0.0f);
            }
            const uint32_t sortVisible =
                (states[modelIndex].parameters.y >= 0.5f && states[modelIndex].parameters.x > 0.0f) ? 1u : 0u;
            HashValue(sortModelStateHash, sortVisible);
            if (sortVisible != 0u)
            {
                HashBytes(sortModelStateHash, &states[modelIndex].world, sizeof(states[modelIndex].world));
            }
        }
        currentSortModelStateHash_ = sortModelStateHash;
        std::memcpy(mappedModelStates_[imageIndex], states.data(), states.size() * sizeof(FSplatModelState));
    }

    void GaussianSplatPass::DispatchGpuSort(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("GS Sort");
        const auto settings = Modules::Splat::GetSettings(*NextEngine::GetInstance());
        const uint32_t activeSplatCount =
            settings->maxCount == 0 ? splatCount_ : std::min(splatCount_, settings->maxCount);
        const uint32_t bucketCount = std::clamp(
            std::max(settings->bucketCount, AutoBucketCount(activeSplatCount)), 16u,
            std::max(16u, sortBucketCapacity_));
        const uint32_t groupCount = std::min(
            (activeSplatCount + splatSortGroupSize - 1) / splatSortGroupSize,
            std::max(sortGroupCountCapacity_, 1u));
        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        const auto camera = NextEngine::GetInstance()->GetScene().GetRenderCamera();
        const float nearPlane = std::max(camera.NearPlane, 1e-4f);
        const float farPlane = std::max(camera.FarPlane, camera.NearPlane + 1e-3f);
        const Assets::UniformBufferObject& currentUbo = renderer_.PrimaryViewState().previousUniformBuffer;

        uint64_t sortCacheKey = 14695981039346656037ull;
        HashBytes(sortCacheKey, &currentUbo.ModelView, sizeof(currentUbo.ModelView));
        HashBytes(sortCacheKey, &currentUbo.ProjectionUnJit, sizeof(currentUbo.ProjectionUnJit));
        HashValue(sortCacheKey, currentSortModelStateHash_);
        HashValue(sortCacheKey, activeSplatCount);
        HashValue(sortCacheKey, bucketCount);
        HashValue(sortCacheKey, groupCount);
        HashValue(sortCacheKey, splatSortGroupSize);
        HashValue(sortCacheKey, extent.width);
        HashValue(sortCacheKey, extent.height);
        HashValue(sortCacheKey, nearPlane);
        HashValue(sortCacheKey, farPlane);
        if (settings->sortCache && sortCacheValid_ && sortCacheKey == lastSortCacheKey_)
        {
            return;
        }

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
        const VkDeviceSize splatBucketBufferSize = VkDeviceSize(activeSplatCount) * sizeof(uint32_t);
        const VkDeviceSize groupBucketBufferSize =
            VkDeviceSize(bucketCount) * VkDeviceSize(std::max(groupCount, 1u)) * sizeof(uint32_t);
        vkCmdFillBuffer(commandBuffer, bucketCountBuffer_->Handle(), 0, bucketBufferSize, 0);
        vkCmdFillBuffer(commandBuffer, splatBucketBuffer_->Handle(), 0, splatBucketBufferSize, 0xffffffffu);
        vkCmdFillBuffer(commandBuffer, groupBucketCountBuffer_->Handle(), 0, groupBucketBufferSize, 0);
        vkCmdFillBuffer(commandBuffer, drawIndirectBuffer_->Handle(), 0, VK_WHOLE_SIZE, 0);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(bucketCountBuffer_->Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, 0, bucketBufferSize),
            BufferMemoryBarrier::Make(splatBucketBuffer_->Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, 0, splatBucketBufferSize),
            BufferMemoryBarrier::Make(groupBucketCountBuffer_->Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, 0, groupBucketBufferSize),
            BufferMemoryBarrier::Make(drawIndirectBuffer_->Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
        });

        FSplatSortPushConstants push{
            renderer_.UniformBuffers()[imageIndex].Buffer().GetDeviceAddress(), splatBuffer_->GetDeviceAddress(),
            modelStateBuffers_[imageIndex]->GetDeviceAddress(), sortedIndexBuffer_->GetDeviceAddress(),
            bucketCountBuffer_->GetDeviceAddress(),
            bucketOffsetBuffer_->GetDeviceAddress(), splatBucketBuffer_->GetDeviceAddress(),
            groupBucketCountBuffer_->GetDeviceAddress(), groupBucketOffsetBuffer_->GetDeviceAddress(),
            drawIndirectBuffer_->GetDeviceAddress(), activeSplatCount, extent.width, extent.height, bucketCount,
            nearPlane, farPlane, groupCount, splatSortGroupSize};

        histogramPipeline_->BindPipeline(commandBuffer, &push);
        vkCmdDispatch(commandBuffer, (activeSplatCount + splatSortGroupSize - 1) / splatSortGroupSize, 1, 1);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
            BufferMemoryBarrier::Make(bucketCountBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0, bucketBufferSize),
            BufferMemoryBarrier::Make(splatBucketBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0, splatBucketBufferSize),
            BufferMemoryBarrier::Make(groupBucketCountBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0, groupBucketBufferSize),
        });

        FSplatSortPushConstants prefixPush = push;
        prefixPush.groupSize = 0u;
        prefixPipeline_->BindPipeline(commandBuffer, &prefixPush);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        groupScanPipeline_->BindPipeline(commandBuffer, &push);
        vkCmdDispatch(commandBuffer, (bucketCount + splatSortGroupSize - 1) / splatSortGroupSize, 1, 1);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    groupBucketOffsetBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0, groupBucketBufferSize);

        VkMemoryBarrier prefixBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        prefixBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        prefixBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &prefixBarrier, 0, nullptr, 0, nullptr);

        scatterPipeline_->BindPipeline(commandBuffer, &push);
        vkCmdDispatch(commandBuffer, (activeSplatCount + splatSortGroupSize - 1) / splatSortGroupSize, 1, 1);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, {
            BufferMemoryBarrier::Make(sortedIndexBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
            BufferMemoryBarrier::Make(drawIndirectBuffer_->Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT),
        });
        lastSortCacheKey_ = sortCacheKey;
        sortCacheValid_ = true;
    }

    void GaussianSplatPass::Execute(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto settings = Modules::Splat::GetSettings(*NextEngine::GetInstance());
        if (!pipeline_ || !settings || !settings->visible) return;
        const VkExtent2D extent = renderer_.SwapChain().RenderExtent();
        UpdateModelStates(imageIndex);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                                    modelStateBuffers_[imageIndex]->Handle(), VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

        DispatchGpuSort(commandBuffer, imageIndex);

        {
            SCOPED_GPU_TIMER("GS Draw");
            renderer_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SPLAT_ACCUM, PipelineCommon::ERenderStage::ColorAttachment,
                 PipelineCommon::EResourceAccess::ColorWrite, VK_IMAGE_LAYOUT_GENERAL, true},
            }, "gaussian splat accumulation");
            
            VkRenderPassBeginInfo beginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            beginInfo.renderPass = renderPass_;
            beginInfo.framebuffer = frameBuffer_;
            beginInfo.renderArea.extent = extent;
            VkClearValue clearValue{};
            beginInfo.clearValueCount = 1;
            beginInfo.pClearValues = &clearValue;
            vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
            pipelineLayout_->BindDescriptorSets(commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);
            const Assets::GPUScene& gpuScene = renderer_.GetScene().FetchGPUScene(
                imageIndex, renderer_.ActiveViewBankBase());
            const FSplatPushConstants push{
                gpuScene,
                splatBuffer_->GetDeviceAddress(), paletteBuffer_->GetDeviceAddress(),
                sortedIndexBuffer_->GetDeviceAddress(), modelStateBuffers_[imageIndex]->GetDeviceAddress(),
                0u, splatCount_,
                extent.width, extent.height, std::clamp(settings->sigma, 1.0f, 4.0f)};
            vkCmdPushConstants(commandBuffer, pipelineLayout_->Handle(), VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(push), &push);
            vkCmdDrawIndirect(commandBuffer, drawIndirectBuffer_->Handle(), 0, 1, sizeof(VkDrawIndirectCommand));
            vkCmdEndRenderPass(commandBuffer);
        }
       
        {
            SCOPED_GPU_TIMER("GS Compose");
            renderer_.TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SPLAT_ACCUM, PipelineCommon::ERenderStage::Compute,
                 PipelineCommon::EResourceAccess::ShaderRead},
                {Assets::Bindless::RT_SCENE_COLOR, PipelineCommon::ERenderStage::Compute,
                 PipelineCommon::EResourceAccess::ShaderRead | PipelineCommon::EResourceAccess::ShaderWrite},
            }, "gaussian splat compose");

            const FSplatComposePushConstants composePush{
                renderer_.UniformBuffers()[imageIndex].Buffer().GetDeviceAddress()};
            composePipeline_->BindPipeline(commandBuffer, &composePush);
            vkCmdDispatch(commandBuffer, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);

        }
    }
}
