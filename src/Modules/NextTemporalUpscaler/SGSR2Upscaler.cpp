#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextTemporalUpscaler/SGSR2Upscaler.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/Upscaler/IUpscaler.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"

#include <array>

namespace Modules::NextTemporalUpscaler
{
    namespace
    {
        constexpr uint32_t sgsr2DescriptorBindingCount = 10;

        struct FSGSR2Image
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
        };

        struct alignas(16) FSGSR2PushConstants
        {
            glm::uvec2 renderSize{};
            glm::uvec2 displaySize{};
            glm::vec2 renderSizeRcp{};
            glm::vec2 displaySizeRcp{};
            glm::vec2 jitterOffset{};
            float preExposure = 1.0f;
            float cameraFovAngleHor = 1.0f;
            float cameraNear = 0.1f;
            float minLerpContribution = 0.0f;
            uint32_t sameCamera = 0;
            uint32_t reset = 0;
        };
        static_assert(sizeof(FSGSR2PushConstants) == 64);

        uint32_t FindSGSR2MemoryType(
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
            throw std::runtime_error("SGSR2 could not find device-local image memory");
        }

        std::vector<uint8_t> ReadSGSR2BinaryFile(const char* filename)
        {
            std::vector<uint8_t> data;
            if (!Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, data))
            {
                throw std::runtime_error(fmt::format("SGSR2 shader not found: {}", filename));
            }
            return data;
        }

        VkShaderModule CreateSGSR2ShaderModule(VkDevice device, const char* filename)
        {
            const std::vector<uint8_t> code = ReadSGSR2BinaryFile(filename);
            VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            createInfo.codeSize = code.size();
            createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule shader = VK_NULL_HANDLE;
            Vulkan::Check(vkCreateShaderModule(device, &createInfo, nullptr, &shader),
                          "create SGSR2 shader module");
            return shader;
        }

        void InsertSGSR2ImageBarrier(
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

        bool SGSR2FormatSupported(VkPhysicalDevice physicalDevice, VkFormat format)
        {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
            constexpr VkFormatFeatureFlags required =
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
            return (properties.optimalTilingFeatures & required) == required;
        }

        bool IsSGSR2CameraStill(const Assets::UniformBufferObject& ubo)
        {
            float difference = 0.0f;
            for (glm::length_t column = 0; column < 4; ++column)
            {
                for (glm::length_t row = 0; row < 4; ++row)
                {
                    difference += std::abs(
                        ubo.ViewProjectionUnJit[column][row] -
                        ubo.PrevViewProjectionUnJit[column][row]);
                }
            }
            return difference < 1.0e-5f;
        }

        class FSGSR2Upscaler final : public Rendering::Upscaler::IUpscaler
        {
        public:
            ~FSGSR2Upscaler() override
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
                               features.shaderStorageImageWriteWithoutFormat &&
                               SGSR2FormatSupported(
                                   deviceInfo_.physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT) &&
                               SGSR2FormatSupported(
                                   deviceInfo_.physicalDevice, VK_FORMAT_R32_UINT);
                if (!deviceReady_)
                {
                    SPDLOG_WARN("SGSR2 disabled: required sampled/storage image formats or formatless access are unavailable");
                    return;
                }

                caps.supportedTypes = Rendering::Upscaler::UpscalerTypeBit(
                    Rendering::Upscaler::EUpscalerType::SnapdragonGSR2);
                SPDLOG_INFO("Snapdragon GSR 2 2-pass compute provider available");
            }

            void SetActiveType(Rendering::Upscaler::EUpscalerType type) override
            {
                if (type == Rendering::Upscaler::EUpscalerType::SnapdragonGSR2)
                {
                    EnsureStaticResources();
                }
                else
                {
                    DestroyGpuResources();
                }
            }

            void OnSwapChainDestroyed() override
            {
                DestroyGpuResources();
                stationaryFrameCount_ = 0;
                loggedDispatch_ = false;
            }

            void Shutdown() override
            {
                if (deviceInfo_.device == VK_NULL_HANDLE)
                {
                    return;
                }

                DestroyGpuResources();
                deviceInfo_ = {};
                deviceReady_ = false;
            }

            Rendering::Upscaler::FOptimalRenderSettings GetOptimalRenderSettings(
                uint32_t mode, VkExtent2D outputExtent, bool, bool,
                Rendering::Upscaler::EUpscalerType) override
            {
                // Qualcomm publishes SGSR2 2-pass CS profiles for 1.5x, 1.7x and 2.0x.
                // Keep Ultra Performance within the supported 2x envelope.
                const float scale = std::min(
                    Rendering::Upscaler::GetUpscaleModeInfo(mode).fallbackScale, 2.0f);
                const VkExtent2D renderExtent =
                    Rendering::Upscaler::ScaleExtent(outputExtent, scale);
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
                if (inputs.upscalerType != Rendering::Upscaler::EUpscalerType::SnapdragonGSR2 || !deviceReady_ ||
                    inputs.commandBuffer == VK_NULL_HANDLE || inputs.ubo == nullptr ||
                    !inputs.scalingInputColor.IsValid() || !inputs.scalingOutputColor.IsValid() ||
                    !inputs.motionVectors.IsValid() || !inputs.depth.IsValid())
                {
                    return false;
                }
                if (!EnsureStaticResources() ||
                    !EnsureFrameResources(inputs.renderExtent, inputs.outputExtent))
                {
                    return false;
                }

                const uint32_t writeIndex = inputs.frameIndex & 1u;
                const uint32_t readIndex = 1u - writeIndex;
                PrepareFrameBarriers(inputs.commandBuffer, readIndex, writeIndex);
                UpdateDescriptorSet(inputs, readIndex, writeIndex);

                const bool reset = inputs.reset || !historyValid_;
                const bool sameCamera = !reset && IsSGSR2CameraStill(*inputs.ubo);
                stationaryFrameCount_ = sameCamera ? stationaryFrameCount_ + 1u : 0u;

                FSGSR2PushConstants constants{};
                constants.renderSize = {inputs.renderExtent.width, inputs.renderExtent.height};
                constants.displaySize = {inputs.outputExtent.width, inputs.outputExtent.height};
                constants.renderSizeRcp = 1.0f / glm::vec2(constants.renderSize);
                constants.displaySizeRcp = 1.0f / glm::vec2(constants.displaySize);
                constants.jitterOffset = {inputs.ubo->Jitter.x, inputs.ubo->Jitter.y};
                constants.cameraFovAngleHor =
                    std::tan(inputs.camera.verticalFovRadians * 0.5f) * inputs.camera.aspectRatio;
                constants.cameraNear = inputs.camera.nearPlane;
                constants.minLerpContribution = stationaryFrameCount_ > 5u ? 0.3f : 0.0f;
                constants.sameCamera = sameCamera ? 1u : 0u;
                constants.reset = reset ? 1u : 0u;
                constants.preExposure = 300.0f;

                vkCmdBindDescriptorSets(inputs.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
                vkCmdPushConstants(inputs.commandBuffer, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                                   0, sizeof(constants), &constants);
                vkCmdBindPipeline(inputs.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, convertPipeline_);
                vkCmdDispatch(inputs.commandBuffer,
                              (inputs.renderExtent.width + 7u) / 8u,
                              (inputs.renderExtent.height + 7u) / 8u, 1);

                InsertSGSR2ImageBarrier(
                    inputs.commandBuffer, motionDepthClip_.image,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                InsertSGSR2ImageBarrier(
                    inputs.commandBuffer, yCoCgColor_.image,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

                vkCmdBindPipeline(inputs.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, upscalePipeline_);
                vkCmdDispatch(inputs.commandBuffer,
                              (inputs.outputExtent.width + 7u) / 8u,
                              (inputs.outputExtent.height + 7u) / 8u, 1);

                historyValid_ = true;
                if (!loggedDispatch_)
                {
                    SPDLOG_INFO("Snapdragon GSR 2 active (2-pass CS): {}x{} -> {}x{}",
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
            bool EnsureStaticResources()
            {
                if (convertPipeline_ != VK_NULL_HANDLE && upscalePipeline_ != VK_NULL_HANDLE)
                {
                    return true;
                }
                if (!deviceReady_ || deviceInfo_.device == VK_NULL_HANDLE)
                {
                    return false;
                }
                try
                {
                    CreateStaticResources();
                    SPDLOG_INFO("SGSR2 GPU resources activated");
                    return true;
                }
                catch (const std::exception& error)
                {
                    SPDLOG_ERROR("SGSR2 GPU resource creation failed: {}", error.what());
                    DestroyGpuResources();
                    return false;
                }
            }

            void CreateStaticResources()
            {
                std::array<VkDescriptorSetLayoutBinding, sgsr2DescriptorBindingCount> bindings{};
                for (uint32_t i = 0; i < sgsr2DescriptorBindingCount; ++i)
                {
                    bindings[i].binding = i;
                    bindings[i].descriptorCount = 1;
                    bindings[i].descriptorType =
                        (i <= 2 || (i >= 5 && i <= 7))
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
                    "create SGSR2 descriptor set layout");

                const std::array<VkDescriptorPoolSize, 2> poolSizes{{
                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 6},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
                }};
                VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                poolInfo.maxSets = 1;
                poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
                poolInfo.pPoolSizes = poolSizes.data();
                Vulkan::Check(vkCreateDescriptorPool(
                    deviceInfo_.device, &poolInfo, nullptr, &descriptorPool_),
                    "create SGSR2 descriptor pool");

                VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocateInfo.descriptorPool = descriptorPool_;
                allocateInfo.descriptorSetCount = 1;
                allocateInfo.pSetLayouts = &descriptorSetLayout_;
                Vulkan::Check(vkAllocateDescriptorSets(
                    deviceInfo_.device, &allocateInfo, &descriptorSet_),
                    "allocate SGSR2 descriptor set");

                VkPushConstantRange pushConstantRange{};
                pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pushConstantRange.size = sizeof(FSGSR2PushConstants);
                VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
                pipelineLayoutInfo.setLayoutCount = 1;
                pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
                pipelineLayoutInfo.pushConstantRangeCount = 1;
                pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
                Vulkan::Check(vkCreatePipelineLayout(
                    deviceInfo_.device, &pipelineLayoutInfo, nullptr, &pipelineLayout_),
                    "create SGSR2 pipeline layout");

                convertPipeline_ = CreatePipeline(
                    "assets/shaders/Process.SGSR2Convert.comp.slang.spv");
                upscalePipeline_ = CreatePipeline(
                    "assets/shaders/Process.SGSR2Upscale.comp.slang.spv");
            }

            void DestroyStaticResources()
            {
                if (deviceInfo_.device == VK_NULL_HANDLE)
                {
                    return;
                }
                if (convertPipeline_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(deviceInfo_.device, convertPipeline_, nullptr);
                    convertPipeline_ = VK_NULL_HANDLE;
                }
                if (upscalePipeline_ != VK_NULL_HANDLE)
                {
                    vkDestroyPipeline(deviceInfo_.device, upscalePipeline_, nullptr);
                    upscalePipeline_ = VK_NULL_HANDLE;
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
                descriptorSet_ = VK_NULL_HANDLE;
                if (descriptorSetLayout_ != VK_NULL_HANDLE)
                {
                    vkDestroyDescriptorSetLayout(deviceInfo_.device, descriptorSetLayout_, nullptr);
                    descriptorSetLayout_ = VK_NULL_HANDLE;
                }
            }

            void DestroyGpuResources()
            {
                const bool hadResources = convertPipeline_ != VK_NULL_HANDLE ||
                    motionDepthClip_.image != VK_NULL_HANDLE;
                DestroyFrameResources();
                DestroyStaticResources();
                if (hadResources)
                {
                    SPDLOG_INFO("SGSR2 GPU resources released");
                }
            }

            VkPipeline CreatePipeline(const char* shaderPath) const
            {
                const VkShaderModule shader = CreateSGSR2ShaderModule(deviceInfo_.device, shaderPath);
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
                Vulkan::Check(result, "create SGSR2 compute pipeline");
                return pipeline;
            }

            bool EnsureFrameResources(VkExtent2D renderExtent, VkExtent2D displayExtent)
            {
                if (renderExtent_.width == renderExtent.width &&
                    renderExtent_.height == renderExtent.height &&
                    displayExtent_.width == displayExtent.width &&
                    displayExtent_.height == displayExtent.height &&
                    motionDepthClip_.image != VK_NULL_HANDLE)
                {
                    return true;
                }

                DestroyFrameResources();
                renderExtent_ = renderExtent;
                displayExtent_ = displayExtent;
                try
                {
                    CreateImage(motionDepthClip_, renderExtent_, VK_FORMAT_R16G16B16A16_SFLOAT);
                    CreateImage(yCoCgColor_, renderExtent_, VK_FORMAT_R32_UINT);
                    for (auto& history : history_)
                    {
                        CreateImage(history, displayExtent_, VK_FORMAT_R16G16B16A16_SFLOAT);
                    }
                }
                catch (const std::exception& error)
                {
                    SPDLOG_ERROR("SGSR2 frame-resource allocation failed: {}", error.what());
                    DestroyFrameResources();
                    return false;
                }

                layoutsInitialized_ = false;
                historyValid_ = false;
                stationaryFrameCount_ = 0;
                return true;
            }

            void CreateImage(FSGSR2Image& resource, VkExtent2D extent, VkFormat format)
            {
                VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = format;
                imageInfo.extent = {extent.width, extent.height, 1};
                imageInfo.mipLevels = 1;
                imageInfo.arrayLayers = 1;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
                imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                Vulkan::Check(vkCreateImage(
                    deviceInfo_.device, &imageInfo, nullptr, &resource.image),
                    "create SGSR2 image");

                VkMemoryRequirements requirements{};
                vkGetImageMemoryRequirements(deviceInfo_.device, resource.image, &requirements);
                VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocateInfo.allocationSize = requirements.size;
                allocateInfo.memoryTypeIndex = FindSGSR2MemoryType(
                    deviceInfo_.physicalDevice, requirements.memoryTypeBits,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                Vulkan::Check(vkAllocateMemory(
                    deviceInfo_.device, &allocateInfo, nullptr, &resource.memory),
                    "allocate SGSR2 image memory");
                Vulkan::Check(vkBindImageMemory(
                    deviceInfo_.device, resource.image, resource.memory, 0),
                    "bind SGSR2 image memory");

                VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                viewInfo.image = resource.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = format;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                Vulkan::Check(vkCreateImageView(
                    deviceInfo_.device, &viewInfo, nullptr, &resource.view),
                    "create SGSR2 image view");
            }

            void DestroyFrameResources()
            {
                if (deviceInfo_.device == VK_NULL_HANDLE)
                {
                    return;
                }
                const auto destroy = [this](FSGSR2Image& resource)
                {
                    if (resource.view != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(deviceInfo_.device, resource.view, nullptr);
                    }
                    if (resource.image != VK_NULL_HANDLE)
                    {
                        vkDestroyImage(deviceInfo_.device, resource.image, nullptr);
                    }
                    if (resource.memory != VK_NULL_HANDLE)
                    {
                        vkFreeMemory(deviceInfo_.device, resource.memory, nullptr);
                    }
                    resource = {};
                };
                destroy(motionDepthClip_);
                destroy(yCoCgColor_);
                for (auto& history : history_)
                {
                    destroy(history);
                }
                renderExtent_ = {};
                displayExtent_ = {};
                layoutsInitialized_ = false;
                historyValid_ = false;
            }

            void PrepareFrameBarriers(
                VkCommandBuffer commandBuffer, uint32_t readIndex, uint32_t writeIndex)
            {
                if (!layoutsInitialized_)
                {
                    for (FSGSR2Image* image : {
                             &motionDepthClip_, &yCoCgColor_, &history_[0], &history_[1]})
                    {
                        InsertSGSR2ImageBarrier(
                            commandBuffer, image->image, 0,
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    }
                    layoutsInitialized_ = true;
                    return;
                }

                for (FSGSR2Image* image : {&motionDepthClip_, &yCoCgColor_})
                {
                    InsertSGSR2ImageBarrier(
                        commandBuffer, image->image,
                        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                }
                InsertSGSR2ImageBarrier(
                    commandBuffer, history_[readIndex].image,
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                InsertSGSR2ImageBarrier(
                    commandBuffer, history_[writeIndex].image,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            }

            void UpdateDescriptorSet(
                const Rendering::Upscaler::FFrameInputs& inputs,
                uint32_t readIndex, uint32_t writeIndex)
            {
                const std::array<VkDescriptorImageInfo, sgsr2DescriptorBindingCount> images{{
                    {VK_NULL_HANDLE, inputs.scalingInputColor.view, inputs.scalingInputColor.layout},
                    {VK_NULL_HANDLE, inputs.depth.view, inputs.depth.layout},
                    {VK_NULL_HANDLE, inputs.motionVectors.view, inputs.motionVectors.layout},
                    {VK_NULL_HANDLE, motionDepthClip_.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, yCoCgColor_.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, motionDepthClip_.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, yCoCgColor_.view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, history_[readIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                    {VK_NULL_HANDLE, inputs.scalingOutputColor.view, inputs.scalingOutputColor.layout},
                    {VK_NULL_HANDLE, history_[writeIndex].view, VK_IMAGE_LAYOUT_GENERAL},
                }};

                std::array<VkWriteDescriptorSet, sgsr2DescriptorBindingCount> writes{};
                for (uint32_t i = 0; i < sgsr2DescriptorBindingCount; ++i)
                {
                    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    writes[i].dstSet = descriptorSet_;
                    writes[i].dstBinding = i;
                    writes[i].descriptorCount = 1;
                    writes[i].descriptorType =
                        (i <= 2 || (i >= 5 && i <= 7))
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
            VkPipeline convertPipeline_ = VK_NULL_HANDLE;
            VkPipeline upscalePipeline_ = VK_NULL_HANDLE;
            FSGSR2Image motionDepthClip_{};
            FSGSR2Image yCoCgColor_{};
            std::array<FSGSR2Image, 2> history_{};
            VkExtent2D renderExtent_{};
            VkExtent2D displayExtent_{};
            uint32_t jitterPhaseCount_ = 16;
            uint32_t stationaryFrameCount_ = 0;
            bool deviceReady_ = false;
            bool layoutsInitialized_ = false;
            bool historyValid_ = false;
            bool loggedDispatch_ = false;
        };
    }

    std::unique_ptr<Rendering::Upscaler::IUpscaler> CreateSGSR2Upscaler()
    {
        return std::make_unique<FSGSR2Upscaler>();
    }
}
