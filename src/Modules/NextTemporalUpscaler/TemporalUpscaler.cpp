#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTemporalUpscaler/TemporalUpscaler.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/Upscaler/IUpscaler.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"

#include <array>

namespace Modules::NextTemporalUpscaler
{
    namespace
    {
        constexpr uint32_t descriptorBindingCount = 14;

        struct FHistoryImage
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
        };

        struct alignas(16) FPushConstants
        {
            glm::uvec2 renderSize{};
            glm::uvec2 outputSize{};
            glm::vec2 jitter{};
            glm::vec2 previousJitter{};
            uint32_t reset = 0;
            float sharpness = 0.25f;
            float historyWeight = 0.97f;
            float padding = 0.0f;
            uint32_t filterPassIndex = 0;
            uint32_t filterPassCount = 3;
            uint32_t filterStepWidth = 1;
            uint32_t applyFireflyClamp = 0;
            float filterStrength = 0.65f;
            float filterLumaSigma = 0.10f;
            float fireflySigma = 2.5f;
            float filterPadding = 0.0f;
        };
        static_assert(sizeof(FPushConstants) == 80);

        uint32_t FindMemoryType(
            VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
        {
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            {
                if ((typeFilter & (1u << i)) != 0 &&
                    (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                {
                    return i;
                }
            }
            throw std::runtime_error("NativeTemporal could not find device-local image memory");
        }

        std::vector<uint8_t> ReadBinaryFile(const char* filename)
        {
            std::vector<uint8_t> data;
            if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, data))
            {
                throw std::runtime_error(fmt::format("NativeTemporal shader not found: {}", filename));
            }
            return data;
        }

        VkShaderModule CreateShaderModule(VkDevice device, const char* filename)
        {
            const std::vector<uint8_t> code = ReadBinaryFile(filename);
            VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule shader = VK_NULL_HANDLE;
            Vulkan::Check(vkCreateShaderModule(device, &createInfo, nullptr, &shader),
                          "create NativeTemporal shader module");
            return shader;
        }

        void InsertImageBarrier(
            VkCommandBuffer commandBuffer, VkImage image, VkAccessFlags sourceAccess,
            VkAccessFlags destinationAccess, VkImageLayout oldLayout, VkImageLayout newLayout,
            VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage)
        {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = sourceAccess;
            barrier.dstAccessMask = destinationAccess;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);
        }

        class FTemporalUpscaler final : public Rendering::Upscaler::IUpscaler
        {
        public:
            ~FTemporalUpscaler() override
            {
                Shutdown();
            }

            void OnDeviceCreated(
                const Rendering::Upscaler::FDeviceInfo& deviceInfo,
                Rendering::Upscaler::FFeatureCaps& caps) override
            {
                deviceInfo_ = deviceInfo;
                VkPhysicalDeviceFeatures features{};
                vkGetPhysicalDeviceFeatures(deviceInfo_.physicalDevice, &features);
                deviceReady_ = features.shaderStorageImageReadWithoutFormat &&
                               features.shaderStorageImageWriteWithoutFormat;
                caps.provider = Rendering::Upscaler::EUpscalerProvider::NativeTemporal;
                if (!deviceReady_)
                {
                    SPDLOG_WARN("NativeTemporal disabled: formatless storage image access is unavailable");
                    return;
                }
                try
                {
                    CreateStaticResources();
                }
                catch (const std::exception& error)
                {
                    SPDLOG_ERROR("NativeTemporal initialization failed: {}", error.what());
                    Shutdown();
                    return;
                }
                caps.supportNativeTemporal = true;
                SPDLOG_INFO("NativeTemporal compute provider ready");
            }

            void OnSwapChainDestroyed() override
            {
                DestroyHistory();
                previousJitter_ = {};
                loggedDispatch_ = false;
                loggedFilterDispatch_ = false;
            }

            void Shutdown() override
            {
                if (deviceInfo_.device == VK_NULL_HANDLE)
                {
                    return;
                }
                DestroyHistory();
                if (reprojectPipeline_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(deviceInfo_.device, reprojectPipeline_, nullptr);
                    reprojectPipeline_ = VK_NULL_HANDLE;
                }
                if (sharpenPipeline_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(deviceInfo_.device, sharpenPipeline_, nullptr);
                    sharpenPipeline_ = VK_NULL_HANDLE;
                }
                if (atrousPipeline_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(deviceInfo_.device, atrousPipeline_, nullptr);
                    atrousPipeline_ = VK_NULL_HANDLE;
                }
                if (pipelineLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipelineLayout(deviceInfo_.device, pipelineLayout_, nullptr);
                    pipelineLayout_ = VK_NULL_HANDLE;
                }
                if (descriptorPool_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorPool(deviceInfo_.device, descriptorPool_, nullptr);
                    descriptorPool_ = VK_NULL_HANDLE;
                }
                if (descriptorSetLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(deviceInfo_.device, descriptorSetLayout_, nullptr);
                    descriptorSetLayout_ = VK_NULL_HANDLE;
                }
                deviceInfo_ = {};
                deviceReady_ = false;
            }

            Rendering::Upscaler::FOptimalRenderSettings GetOptimalRenderSettings(
                uint32_t mode, VkExtent2D outputExtent, bool, bool,
                Rendering::Upscaler::EUpscalerProvider) override
            {
                const VkExtent2D renderExtent = Rendering::Upscaler::ScaleExtent(
                    outputExtent, Rendering::Upscaler::GetUpscaleModeInfo(mode).fallbackScale);
                jitterPhaseCount_ = std::clamp(
                    static_cast<uint32_t>(8.0f *
                        static_cast<float>(outputExtent.width * outputExtent.height) /
                        static_cast<float>(std::max(1u, renderExtent.width * renderExtent.height))),
                    8u, 32u);
                return {renderExtent, renderExtent, outputExtent, false};
            }

            uint32_t JitterPhaseCount() const override
            {
                return jitterPhaseCount_;
            }

            Rendering::Upscaler::FFrameToken BeginFrame(uint32_t frameIndex, bool, uint32_t) override
            {
                return {deviceReady_ ? this : nullptr, frameIndex};
            }

            void MarkFrame(
                Rendering::Upscaler::EFrameMarker,
                const Rendering::Upscaler::FFrameToken&) override
            {
            }

            void SetReflexOptions(bool, uint32_t) override
            {
            }

            void ReflexSleep(const Rendering::Upscaler::FFrameToken&) override
            {
            }

            bool Evaluate(const Rendering::Upscaler::FFrameInputs& inputs) override
            {
                if (!inputs.enableNativeTemporal || !deviceReady_ ||
                    inputs.commandBuffer == VK_NULL_HANDLE || inputs.ubo == nullptr ||
                    !inputs.scalingInputColor.IsValid() || !inputs.scalingOutputColor.IsValid() ||
                    !inputs.motionVectors.IsValid() || !inputs.depth.IsValid())
                {
                    return false;
                }
                if (!EnsureHistory(inputs.outputExtent))
                {
                    return false;
                }

                const uint32_t writeIndex = inputs.frameIndex & 1u;
                const uint32_t readIndex = 1u - writeIndex;
                const bool postFilterEnabled = inputs.temporalPostFilterEnabled &&
                    inputs.albedo.IsValid() && inputs.normalRoughness.IsValid();
                PrepareHistoryBarriers(inputs.commandBuffer, readIndex, writeIndex);
                UpdateDescriptorSet(inputs, readIndex, writeIndex, postFilterEnabled);

                FPushConstants constants{};
                constants.renderSize = {inputs.renderExtent.width, inputs.renderExtent.height};
                constants.outputSize = {inputs.outputExtent.width, inputs.outputExtent.height};
                constants.jitter = {inputs.ubo->Jitter.x, inputs.ubo->Jitter.y};
                constants.previousJitter = previousJitter_;
                constants.reset = inputs.reset || !historyValid_ ? 1u : 0u;
                constants.sharpness = std::clamp(inputs.nativeTemporalSharpness, 0.0f, 1.0f);
                constants.historyWeight = std::clamp(
                    inputs.nativeTemporalHistoryWeight, 0.5f, 0.98f);
                constants.filterPassCount = std::clamp(inputs.temporalPostFilterPasses, 1u, 4u);
                constants.filterLumaSigma = std::clamp(
                    inputs.temporalPostFilterLumaSigma, 0.01f, 0.5f);
                constants.fireflySigma = std::clamp(inputs.temporalFireflySigma, 1.0f, 8.0f);
                const float totalFilterStrength = std::clamp(
                    inputs.temporalPostFilterStrength, 0.0f, 1.0f);
                constants.filterStrength = 1.0f - std::pow(
                    1.0f - totalFilterStrength,
                    1.0f / static_cast<float>(constants.filterPassCount));

                vkCmdBindDescriptorSets(inputs.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
                vkCmdPushConstants(inputs.commandBuffer, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(constants), &constants);
                vkCmdBindPipeline(inputs.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reprojectPipeline_);
                vkCmdDispatch(inputs.commandBuffer,
                              (inputs.outputExtent.width + 7u) / 8u,
                              (inputs.outputExtent.height + 7u) / 8u, 1);

                InsertImageBarrier(inputs.commandBuffer, historyColor_[writeIndex].image,
                                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                vkCmdBindPipeline(inputs.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, sharpenPipeline_);
                vkCmdDispatch(inputs.commandBuffer,
                              (inputs.outputExtent.width + 7u) / 8u,
                              (inputs.outputExtent.height + 7u) / 8u, 1);

                if (postFilterEnabled)
                {
                    DispatchAtrousFilter(inputs, constants);
                    if (!loggedFilterDispatch_)
                    {
                        SPDLOG_INFO(
                            "NativeTemporal a-trous filter active: {} passes, strength {:.2f}, firefly sigma {:.2f}",
                            constants.filterPassCount, totalFilterStrength, constants.fireflySigma);
                        loggedFilterDispatch_ = true;
                    }
                }

                historyValid_ = true;
                previousJitter_ = constants.jitter;
                if (!loggedDispatch_)
                {
                    SPDLOG_INFO("NativeTemporal TAAU active: {}x{} -> {}x{}",
                                inputs.renderExtent.width, inputs.renderExtent.height,
                                inputs.outputExtent.width, inputs.outputExtent.height);
                    loggedDispatch_ = true;
                }
                return true;
            }

            void TagFrameGeneration(const Rendering::Upscaler::FFrameInputs&) override
            {
            }

            void UpdateFrameGenerationState() override
            {
            }

            Rendering::Upscaler::FFrameGenerationState FrameGenerationState() const override
            {
                return {};
            }

        private:
            void DispatchAtrousFilter(
                const Rendering::Upscaler::FFrameInputs& inputs,
                FPushConstants constants)
            {
                vkCmdBindPipeline(
                    inputs.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, atrousPipeline_);
                for (uint32_t pass = 0; pass < constants.filterPassCount; ++pass)
                {
                    const bool sourceIsPing = (pass & 1u) == 0u;
                    FHistoryImage& source = filterColor_[sourceIsPing ? 0u : 1u];
                    InsertImageBarrier(inputs.commandBuffer, source.image,
                                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                       VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

                    const bool finalPass = pass + 1u == constants.filterPassCount;
                    if (!finalPass)
                    {
                        FHistoryImage& destination = filterColor_[sourceIsPing ? 1u : 0u];
                        InsertImageBarrier(inputs.commandBuffer, destination.image,
                                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    }

                    constants.filterPassIndex = pass;
                    constants.filterStepWidth = 1u << pass;
                    constants.applyFireflyClamp = pass == 0u ? 1u : 0u;
                    vkCmdPushConstants(inputs.commandBuffer, pipelineLayout_,
                                       VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                       sizeof(constants), &constants);
                    vkCmdDispatch(inputs.commandBuffer,
                                  (inputs.outputExtent.width + 7u) / 8u,
                                  (inputs.outputExtent.height + 7u) / 8u, 1);
                }
            }

            void CreateStaticResources()
            {
                std::array<VkDescriptorSetLayoutBinding, descriptorBindingCount> bindings{};
                for (uint32_t i = 0; i < descriptorBindingCount; ++i)
                {
                    bindings[i].binding = i;
                    bindings[i].descriptorCount = 1;
                    bindings[i].descriptorType = i == 2
                        ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                        : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                }
                VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo{
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
                descriptorLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
                descriptorLayoutInfo.pBindings = bindings.data();
                Vulkan::Check(vkCreateDescriptorSetLayout(
                    deviceInfo_.device, &descriptorLayoutInfo, nullptr, &descriptorSetLayout_),
                    "create NativeTemporal descriptor set layout");

                const std::array<VkDescriptorPoolSize, 2> poolSizes{{
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorBindingCount - 1},
                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                }};
                VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                poolInfo.maxSets = 1;
                poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
                poolInfo.pPoolSizes = poolSizes.data();
                Vulkan::Check(vkCreateDescriptorPool(
                    deviceInfo_.device, &poolInfo, nullptr, &descriptorPool_),
                    "create NativeTemporal descriptor pool");

                VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocateInfo.descriptorPool = descriptorPool_;
                allocateInfo.descriptorSetCount = 1;
                allocateInfo.pSetLayouts = &descriptorSetLayout_;
                Vulkan::Check(vkAllocateDescriptorSets(
                    deviceInfo_.device, &allocateInfo, &descriptorSet_),
                    "allocate NativeTemporal descriptor set");

                VkPushConstantRange pushConstantRange{};
                pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pushConstantRange.size = sizeof(FPushConstants);
                VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                pipelineLayoutInfo.setLayoutCount = 1;
                pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
                pipelineLayoutInfo.pushConstantRangeCount = 1;
                pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
                Vulkan::Check(vkCreatePipelineLayout(
                    deviceInfo_.device, &pipelineLayoutInfo, nullptr, &pipelineLayout_),
                    "create NativeTemporal pipeline layout");

                reprojectPipeline_ = CreatePipeline(
                    "assets/shaders/Process.NativeTemporalReproject.comp.slang.spv");
                sharpenPipeline_ = CreatePipeline(
                    "assets/shaders/Process.NativeTemporalSharpen.comp.slang.spv");
                atrousPipeline_ = CreatePipeline(
                    "assets/shaders/Process.NativeTemporalAtrous.comp.slang.spv");
            }

            VkPipeline CreatePipeline(const char* shaderPath) const
            {
                const VkShaderModule shader = CreateShaderModule(deviceInfo_.device, shaderPath);
                VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
                stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                stage.module = shader;
                stage.pName = "main";
                VkComputePipelineCreateInfo createInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
                createInfo.stage = stage;
                createInfo.layout = pipelineLayout_;
                VkPipeline pipeline = VK_NULL_HANDLE;
                const VkResult result = vkCreateComputePipelines(
                    deviceInfo_.device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline);
                vkDestroyShaderModule(deviceInfo_.device, shader, nullptr);
                Vulkan::Check(result, "create NativeTemporal compute pipeline");
                return pipeline;
            }

            bool EnsureHistory(VkExtent2D extent)
            {
                if (historyExtent_.width == extent.width && historyExtent_.height == extent.height &&
                    historyColor_[0].image != VK_NULL_HANDLE)
                {
                    return true;
                }
                DestroyHistory();
                historyExtent_ = extent;
                try
                {
                    for (auto& image : historyColor_)
                    {
                        CreateHistoryImage(image, VK_FORMAT_R16G16B16A16_SFLOAT);
                    }
                    for (auto& image : historyDepth_)
                    {
                        CreateHistoryImage(image, VK_FORMAT_R32_SFLOAT);
                    }
                    for (auto& image : historyMoments_)
                    {
                        CreateHistoryImage(image, VK_FORMAT_R16G16_SFLOAT);
                    }
                    for (auto& image : filterColor_)
                    {
                        CreateHistoryImage(image, VK_FORMAT_R16G16B16A16_SFLOAT);
                    }
                }
                catch (const std::exception& error)
                {
                    SPDLOG_ERROR("NativeTemporal history allocation failed: {}", error.what());
                    DestroyHistory();
                    return false;
                }
                historyLayoutsInitialized_ = false;
                historyValid_ = false;
                return true;
            }

            void CreateHistoryImage(FHistoryImage& history, VkFormat format)
            {
                VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = format;
                imageInfo.extent = {historyExtent_.width, historyExtent_.height, 1};
                imageInfo.mipLevels = 1;
                imageInfo.arrayLayers = 1;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                Vulkan::Check(vkCreateImage(deviceInfo_.device, &imageInfo, nullptr, &history.image),
                              "create NativeTemporal history image");

                VkMemoryRequirements requirements{};
                vkGetImageMemoryRequirements(deviceInfo_.device, history.image, &requirements);
                VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocateInfo.allocationSize = requirements.size;
                allocateInfo.memoryTypeIndex = FindMemoryType(
                    deviceInfo_.physicalDevice, requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                Vulkan::Check(vkAllocateMemory(
                    deviceInfo_.device, &allocateInfo, nullptr, &history.memory),
                    "allocate NativeTemporal history memory");
                Vulkan::Check(vkBindImageMemory(
                    deviceInfo_.device, history.image, history.memory, 0),
                    "bind NativeTemporal history memory");

                VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = history.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = format;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                Vulkan::Check(vkCreateImageView(
                    deviceInfo_.device, &viewInfo, nullptr, &history.view),
                    "create NativeTemporal history view");
            }

            void DestroyHistory()
            {
                if (deviceInfo_.device == VK_NULL_HANDLE)
                {
                    return;
                }
                const auto destroy = [this](FHistoryImage& history)
                {
                    if (history.view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(deviceInfo_.device, history.view, nullptr);
                    }
                    if (history.image != VK_NULL_HANDLE)
                    {
                        vkDestroyImage(deviceInfo_.device, history.image, nullptr);
                    }
                    if (history.memory != VK_NULL_HANDLE)
                    {
                        vkFreeMemory(deviceInfo_.device, history.memory, nullptr);
                    }
                    history = {};
                };
                for (auto& image : historyColor_)
                {
                    destroy(image);
                }
                for (auto& image : historyDepth_)
                {
                    destroy(image);
                }
                for (auto& image : historyMoments_)
                {
                    destroy(image);
                }
                for (auto& image : filterColor_)
                {
                    destroy(image);
                }
                historyExtent_ = {};
                historyLayoutsInitialized_ = false;
                historyValid_ = false;
            }

            void PrepareHistoryBarriers(
                VkCommandBuffer commandBuffer, uint32_t readIndex, uint32_t writeIndex)
            {
                if (!historyLayoutsInitialized_)
                {
                    for (auto* collection : {
                             &historyColor_, &historyDepth_, &historyMoments_, &filterColor_})
                    {
                        for (auto& image : *collection)
                        {
                            InsertImageBarrier(commandBuffer, image.image, 0,
                                               VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                        }
                    }
                    historyLayoutsInitialized_ = true;
                    return;
                }

                for (auto* collection : {&historyColor_, &historyDepth_, &historyMoments_})
                {
                    InsertImageBarrier(commandBuffer, (*collection)[readIndex].image,
                                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                       VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    InsertImageBarrier(commandBuffer, (*collection)[writeIndex].image,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                }
                for (auto& image : filterColor_)
                {
                    InsertImageBarrier(commandBuffer, image.image,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                }
            }

            void UpdateDescriptorSet(
                const Rendering::Upscaler::FFrameInputs& inputs,
                uint32_t readIndex, uint32_t writeIndex, bool postFilterEnabled)
            {
                const VkImageView sharpenOutput = postFilterEnabled
                    ? filterColor_[0].view
                    : inputs.scalingOutputColor.view;
                const std::array<VkDescriptorImageInfo, descriptorBindingCount> images{{
                    {VK_NULL_HANDLE, inputs.scalingInputColor.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, inputs.motionVectors.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, inputs.depth.view, inputs.depth.layout},
                    {VK_NULL_HANDLE, historyColor_[readIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, historyDepth_[readIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, historyMoments_[readIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, historyColor_[writeIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, historyDepth_[writeIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, historyMoments_[writeIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, inputs.scalingOutputColor.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, sharpenOutput, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, filterColor_[1].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, inputs.albedo.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, inputs.normalRoughness.view, VK_IMAGE_LAYOUT_GENERAL},
                }};
                std::array<VkWriteDescriptorSet, descriptorBindingCount> writes{};
                for (uint32_t i = 0; i < descriptorBindingCount; ++i)
                {
                    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    writes[i].dstSet = descriptorSet_;
                    writes[i].dstBinding = i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType = i == 2
                        ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                        : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    writes[i].pImageInfo = &images[i];
                }
                vkUpdateDescriptorSets(deviceInfo_.device,
                                       static_cast<uint32_t>(writes.size()), writes.data(),
                                       0, nullptr);
            }

            Rendering::Upscaler::FDeviceInfo deviceInfo_{};
            VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
            VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
            VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
            VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
            VkPipeline reprojectPipeline_ = VK_NULL_HANDLE;
            VkPipeline sharpenPipeline_ = VK_NULL_HANDLE;
            VkPipeline atrousPipeline_ = VK_NULL_HANDLE;
            std::array<FHistoryImage, 2> historyColor_{};
            std::array<FHistoryImage, 2> historyDepth_{};
            std::array<FHistoryImage, 2> historyMoments_{};
            std::array<FHistoryImage, 2> filterColor_{};
            VkExtent2D historyExtent_{};
            glm::vec2 previousJitter_{};
            uint32_t jitterPhaseCount_ = 16;
            bool deviceReady_ = false;
            bool historyLayoutsInitialized_ = false;
            bool historyValid_ = false;
            bool loggedDispatch_ = false;
            bool loggedFilterDispatch_ = false;
        };
    }

    std::unique_ptr<Rendering::Upscaler::IUpscaler> CreateTemporalUpscaler()
    {
        return std::make_unique<FTemporalUpscaler>();
    }
}
