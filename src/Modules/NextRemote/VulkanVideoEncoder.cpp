#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/VulkanVideoEncoder.hpp"

#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"

#include <vk_video/vulkan_video_codec_h264std.h>
#include <vk_video/vulkan_video_codec_h264std_encode.h>

#include <algorithm>
#include <array>
#include <cstring>

#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    enum class ERateControlMode : uint8_t
    {
        Disabled,
        Cbr,
        Vbr,
    };

    namespace
    {
        constexpr VkFormat videoInputFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        constexpr int32_t defaultConstantQp = 28;

        struct FEncodeFeedbackResult
        {
            uint32_t bitstreamOffset = 0;
            uint32_t bitstreamBytes = 0;
            VkQueryResultStatusKHR status = VK_QUERY_RESULT_STATUS_NOT_READY_KHR;
        };

        void InsertImageBarrier(VkCommandBuffer commandBuffer, VkImage image, VkAccessFlags srcAccessMask,
                                VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkImageAspectFlags aspectMask, VkPipelineStageFlags srcStageMask,
                                VkPipelineStageFlags dstStageMask)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = srcAccessMask;
            barrier.dstAccessMask = dstAccessMask;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = aspectMask;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        StdVideoH264LevelIdc SelectLevel(uint32_t width, uint32_t height, uint32_t fps)
        {
            if (width <= 1280 && height <= 720 && fps <= 60)
            {
                return STD_VIDEO_H264_LEVEL_IDC_3_2;
            }
            if (width <= 1920 && height <= 1080 && fps <= 60)
            {
                return STD_VIDEO_H264_LEVEL_IDC_4_2;
            }
            return STD_VIDEO_H264_LEVEL_IDC_5_1;
        }

        const char* RateControlModeName(ERateControlMode mode)
        {
            switch (mode)
            {
            case ERateControlMode::Cbr: return "cbr";
            case ERateControlMode::Vbr: return "vbr";
            default: return "disabled";
            }
        }
    }

    FVulkanVideoEncoder::FVulkanVideoEncoder(const Vulkan::Device& device, Vulkan::FVulkanVideoCaps caps,
                                             FVideoEncoderConfig config)
        : device_(device)
        , caps_(std::move(caps))
        , config_(config)
    {
        codedWidth_ = AlignUp(std::max(16u, config_.width), 16u);
        codedHeight_ = AlignUp(std::max(16u, config_.height), 16u);

        const int32_t selectedProfileIdc =
            config_.h264ProfileIdc >= 0 ? config_.h264ProfileIdc
                                        : (caps_.profileIdc >= 0 ? caps_.profileIdc : STD_VIDEO_H264_PROFILE_IDC_BASELINE);
        h264ProfileInfo_.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR;
        h264ProfileInfo_.stdProfileIdc = static_cast<StdVideoH264ProfileIdc>(selectedProfileIdc);

        usageInfo_.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_USAGE_INFO_KHR;
        usageInfo_.pNext = &h264ProfileInfo_;
        usageInfo_.videoUsageHints = VK_VIDEO_ENCODE_USAGE_STREAMING_BIT_KHR;
        usageInfo_.videoContentHints = VK_VIDEO_ENCODE_CONTENT_RENDERED_BIT_KHR |
                                       VK_VIDEO_ENCODE_CONTENT_DESKTOP_BIT_KHR;
        usageInfo_.tuningMode = VK_VIDEO_ENCODE_TUNING_MODE_LOW_LATENCY_KHR;

        profileInfo_.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
        profileInfo_.pNext = &usageInfo_;
        profileInfo_.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
        profileInfo_.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
        profileInfo_.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        profileInfo_.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;

        supportsInterFrames_ = caps_.maxDpbSlots >= dpbSlotCount && caps_.maxActiveReferencePictures >= 1;
    }

    FVulkanVideoEncoder::~FVulkanVideoEncoder()
    {
        Stop();
    }

    bool FVulkanVideoEncoder::Start()
    {
        if (started_)
        {
            return true;
        }

        const Vulkan::DeviceProcedures& procedures = device_.GetDeviceProcedures();
        if (!procedures.vkCreateVideoSessionKHR || !procedures.vkCmdEncodeVideoKHR)
        {
            SPDLOG_ERROR("RemotePlay: Vulkan Video encoder procedures are unavailable on the logical device");
            return false;
        }

        FillSessionParameters();

        if (!CreateVideoSession() || !CreateSessionParameters() || !LoadHeaders() || !CreateInputResources() ||
            !CreateDpbResources() || !CreateBitstreamResources() || !CreateQueryPool() || !CreateCommandResources())
        {
            Stop();
            return false;
        }

        started_ = true;
        forceKeyframe_ = true;
        rateControlDirty_ = true;
        firstControlCommand_ = true;
        frameNum_ = 0;
        pictureOrderCount_ = 0;
        idrPicId_ = 0;
        lastReferenceSlotIndex_ = -1;
        for (FDpbSlotState& state : dpbStates_)
        {
            state = {};
        }

        SPDLOG_INFO("RemotePlay: Vulkan Video encoder initialized {}x{} (coded {}x{}) {}fps {}kbps profile={} inter={}",
                    config_.width, config_.height, codedWidth_, codedHeight_, config_.fps, config_.bitrateKbps,
                    H264ProfileNameFromStdProfileIdc(static_cast<int32_t>(h264ProfileInfo_.stdProfileIdc)),
                    supportsInterFrames_);
        return true;
    }

    void FVulkanVideoEncoder::Stop()
    {
        if (commandPool_ != VK_NULL_HANDLE && commandBuffer_ != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(device_.Handle(), commandPool_, 1, &commandBuffer_);
            commandBuffer_ = VK_NULL_HANDLE;
        }
        if (encodeFence_ != VK_NULL_HANDLE)
        {
            vkDestroyFence(device_.Handle(), encodeFence_, nullptr);
            encodeFence_ = VK_NULL_HANDLE;
        }
        if (commandPool_ != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device_.Handle(), commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
        }
        if (queryPool_ != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(device_.Handle(), queryPool_, nullptr);
            queryPool_ = VK_NULL_HANDLE;
        }

        DestroyBuffer(bitstreamBuffer_);
        DestroyBuffer(stagingBuffer_);
        DestroyImage(inputImage_);
        for (FAllocatedImage& image : dpbImages_)
        {
            DestroyImage(image);
        }

        if (sessionParameters_ != VK_NULL_HANDLE)
        {
            device_.GetDeviceProcedures().vkDestroyVideoSessionParametersKHR(device_.Handle(), sessionParameters_,
                                                                             nullptr);
            sessionParameters_ = VK_NULL_HANDLE;
        }
        DestroySessionMemory();
        if (session_ != VK_NULL_HANDLE)
        {
            device_.GetDeviceProcedures().vkDestroyVideoSessionKHR(device_.Handle(), session_, nullptr);
            session_ = VK_NULL_HANDLE;
        }

        parameterSetsAnnexb_.clear();
        started_ = false;
    }

    void FVulkanVideoEncoder::RequestKeyframe()
    {
        forceKeyframe_ = true;
    }

    void FVulkanVideoEncoder::SetBitrate(uint32_t bitrateKbps)
    {
        config_.bitrateKbps = std::max(1u, bitrateKbps);
        rateControlDirty_ = true;
    }

    bool FVulkanVideoEncoder::Encode(const FI420View& view, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                                     bool& keyframe)
    {
        outFrame.clear();
        keyframe = false;

        if (!started_ && !Start())
        {
            return false;
        }
        if (!CopyI420ToNv12(view))
        {
            return false;
        }
        return RecordAndSubmitEncode(nullptr, timestampMs, outFrame, keyframe);
    }

    bool FVulkanVideoEncoder::EncodeGpu(FGpuVideoFrame& frame, uint64_t timestampMs,
                                        std::vector<std::byte>& outFrame, bool& keyframe)
    {
        outFrame.clear();
        keyframe = false;

        if (!started_ && !Start())
        {
            return false;
        }
        if (frame.image == VK_NULL_HANDLE || frame.view == VK_NULL_HANDLE)
        {
            return false;
        }
        return RecordAndSubmitEncode(&frame, timestampMs, outFrame, keyframe);
    }

    bool FVulkanVideoEncoder::CreateVideoSession()
    {
        const auto& procedures = device_.GetDeviceProcedures();
        static const VkExtensionProperties stdHeaderVersion = {
            VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME,
            VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION};

        VkVideoEncodeH264SessionCreateInfoKHR h264SessionInfo{};
        h264SessionInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_CREATE_INFO_KHR;
        h264SessionInfo.useMaxLevelIdc = VK_TRUE;
        h264SessionInfo.maxLevelIdc = SelectLevel(config_.width, config_.height, config_.fps);

        VkVideoSessionCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR;
        createInfo.pNext = &h264SessionInfo;
        createInfo.pVideoProfile = &profileInfo_;
        createInfo.queueFamilyIndex = device_.VideoEncodeFamilyIndex();
        createInfo.pictureFormat = videoInputFormat;
        createInfo.maxCodedExtent = {codedWidth_, codedHeight_};
        createInfo.referencePictureFormat = supportsInterFrames_ ? videoInputFormat : VK_FORMAT_UNDEFINED;
        createInfo.maxDpbSlots = supportsInterFrames_ ? dpbSlotCount : 0;
        createInfo.maxActiveReferencePictures = supportsInterFrames_ ? 1 : 0;
        createInfo.pStdHeaderVersion = &stdHeaderVersion;

        const VkResult result = procedures.vkCreateVideoSessionKHR(device_.Handle(), &createInfo, nullptr, &session_);
        if (result != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: vkCreateVideoSessionKHR failed: {}", static_cast<int>(result));
            return false;
        }

        uint32_t requirementCount = 0;
        if (procedures.vkGetVideoSessionMemoryRequirementsKHR(device_.Handle(), session_, &requirementCount, nullptr) !=
            VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to query Vulkan Video session memory requirement count");
            return false;
        }

        std::vector<VkVideoSessionMemoryRequirementsKHR> requirements(requirementCount);
        for (auto& requirement : requirements)
        {
            requirement = {};
            requirement.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
        }
        if (procedures.vkGetVideoSessionMemoryRequirementsKHR(device_.Handle(), session_, &requirementCount,
                                                              requirements.data()) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to query Vulkan Video session memory requirements");
            return false;
        }

        sessionMemory_.clear();
        sessionMemory_.reserve(requirementCount);
        std::vector<VkBindVideoSessionMemoryInfoKHR> bindings(requirementCount);
        for (uint32_t i = 0; i < requirementCount; ++i)
        {
            VkDeviceMemory memory = VK_NULL_HANDLE;
            if (!AllocateMemory(requirements[i].memoryRequirements, 0, memory))
            {
                SPDLOG_ERROR("RemotePlay: failed to allocate Vulkan Video session memory block {}", i);
                return false;
            }

            sessionMemory_.push_back({memory, requirements[i].memoryBindIndex, requirements[i].memoryRequirements.size});
            bindings[i].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
            bindings[i].memoryBindIndex = requirements[i].memoryBindIndex;
            bindings[i].memory = memory;
            bindings[i].memoryOffset = 0;
            bindings[i].memorySize = requirements[i].memoryRequirements.size;
        }

        const VkResult bindResult =
            procedures.vkBindVideoSessionMemoryKHR(device_.Handle(), session_, requirementCount, bindings.data());
        if (bindResult != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: vkBindVideoSessionMemoryKHR failed: {}", static_cast<int>(bindResult));
            return false;
        }
        return true;
    }

    bool FVulkanVideoEncoder::CreateSessionParameters()
    {
        VkVideoEncodeH264SessionParametersAddInfoKHR addInfo{};
        addInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR;
        addInfo.stdSPSCount = 1;
        addInfo.pStdSPSs = &sps_;
        addInfo.stdPPSCount = 1;
        addInfo.pStdPPSs = &pps_;

        VkVideoEncodeH264SessionParametersCreateInfoKHR h264CreateInfo{};
        h264CreateInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
        h264CreateInfo.maxStdSPSCount = 1;
        h264CreateInfo.maxStdPPSCount = 1;
        h264CreateInfo.pParametersAddInfo = &addInfo;

        VkVideoSessionParametersCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR;
        createInfo.pNext = &h264CreateInfo;
        createInfo.videoSession = session_;

        const VkResult result = device_.GetDeviceProcedures().vkCreateVideoSessionParametersKHR(
            device_.Handle(), &createInfo, nullptr, &sessionParameters_);
        if (result != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: vkCreateVideoSessionParametersKHR failed: {}", static_cast<int>(result));
            return false;
        }
        return true;
    }

    bool FVulkanVideoEncoder::CreateInputResources()
    {
        const VkExtent3D extent{codedWidth_, codedHeight_, 1};

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = videoInputFormat;
        imageInfo.extent = extent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device_.Handle(), &imageInfo, nullptr, &inputImage_.image) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video input image");
            return false;
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device_.Handle(), inputImage_.image, &memoryRequirements);
        if (!AllocateMemory(memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, inputImage_.memory))
        {
            SPDLOG_ERROR("RemotePlay: failed to allocate Vulkan Video input image memory");
            return false;
        }
        if (vkBindImageMemory(device_.Handle(), inputImage_.image, inputImage_.memory, 0) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to bind Vulkan Video input image memory");
            return false;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = inputImage_.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = videoInputFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_.Handle(), &viewInfo, nullptr, &inputImage_.view) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video input image view");
            return false;
        }

        const VkDeviceSize stagingSize = static_cast<VkDeviceSize>(codedWidth_) * codedHeight_ * 3u / 2u;
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = stagingSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_.Handle(), &bufferInfo, nullptr, &stagingBuffer_.buffer) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video staging buffer");
            return false;
        }
        stagingBuffer_.size = stagingSize;

        vkGetBufferMemoryRequirements(device_.Handle(), stagingBuffer_.buffer, &memoryRequirements);
        if (!AllocateMemory(memoryRequirements,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                            stagingBuffer_.memory))
        {
            if (!AllocateMemory(memoryRequirements,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                stagingBuffer_.memory))
            {
                SPDLOG_ERROR("RemotePlay: failed to allocate Vulkan Video staging buffer memory");
                return false;
            }
        }
        if (vkBindBufferMemory(device_.Handle(), stagingBuffer_.buffer, stagingBuffer_.memory, 0) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to bind Vulkan Video staging buffer memory");
            return false;
        }
        if (vkMapMemory(device_.Handle(), stagingBuffer_.memory, 0, stagingSize, 0,
                        reinterpret_cast<void**>(&stagingBuffer_.mapped)) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to map Vulkan Video staging buffer");
            return false;
        }

        return true;
    }

    bool FVulkanVideoEncoder::CreateDpbResources()
    {
        if (!supportsInterFrames_)
        {
            return true;
        }

        for (FAllocatedImage& image : dpbImages_)
        {
            const VkExtent3D extent{codedWidth_, codedHeight_, 1};

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = videoInputFormat;
            imageInfo.extent = extent;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            if (vkCreateImage(device_.Handle(), &imageInfo, nullptr, &image.image) != VK_SUCCESS)
            {
                SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video DPB image");
                return false;
            }

            VkMemoryRequirements memoryRequirements{};
            vkGetImageMemoryRequirements(device_.Handle(), image.image, &memoryRequirements);
            if (!AllocateMemory(memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image.memory))
            {
                SPDLOG_ERROR("RemotePlay: failed to allocate Vulkan Video DPB image memory");
                return false;
            }
            if (vkBindImageMemory(device_.Handle(), image.image, image.memory, 0) != VK_SUCCESS)
            {
                SPDLOG_ERROR("RemotePlay: failed to bind Vulkan Video DPB image memory");
                return false;
            }

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = videoInputFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_.Handle(), &viewInfo, nullptr, &image.view) != VK_SUCCESS)
            {
                SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video DPB image view");
                return false;
            }
        }

        return true;
    }

    bool FVulkanVideoEncoder::CreateBitstreamResources()
    {
        const VkDeviceSize minimumSize = static_cast<VkDeviceSize>(config_.width) * config_.height * 4u;
        const VkDeviceSize requestedSize = std::max<VkDeviceSize>(minimumSize, 4u * 1024u * 1024u);
        const VkDeviceSize alignedSize =
            AlignUp(requestedSize, std::max<VkDeviceSize>(1, caps_.minBitstreamBufferSizeAlignment));

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = alignedSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_.Handle(), &bufferInfo, nullptr, &bitstreamBuffer_.buffer) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video bitstream buffer");
            return false;
        }
        bitstreamBuffer_.size = alignedSize;

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(device_.Handle(), bitstreamBuffer_.buffer, &memoryRequirements);
        if (!AllocateMemory(memoryRequirements,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                            bitstreamBuffer_.memory))
        {
            if (!AllocateMemory(memoryRequirements,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                bitstreamBuffer_.memory))
            {
                SPDLOG_ERROR("RemotePlay: failed to allocate Vulkan Video bitstream buffer memory");
                return false;
            }
        }
        if (vkBindBufferMemory(device_.Handle(), bitstreamBuffer_.buffer, bitstreamBuffer_.memory, 0) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to bind Vulkan Video bitstream buffer memory");
            return false;
        }
        if (vkMapMemory(device_.Handle(), bitstreamBuffer_.memory, 0, alignedSize, 0,
                        reinterpret_cast<void**>(&bitstreamBuffer_.mapped)) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to map Vulkan Video bitstream buffer");
            return false;
        }

        return true;
    }

    bool FVulkanVideoEncoder::CreateQueryPool()
    {
        VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo{};
        feedbackInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR;
        feedbackInfo.pNext = &profileInfo_;
        feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR |
                                           VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;

        VkQueryPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        createInfo.pNext = &feedbackInfo;
        createInfo.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
        createInfo.queryCount = 1;

        const VkResult result = vkCreateQueryPool(device_.Handle(), &createInfo, nullptr, &queryPool_);
        if (result != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video feedback query pool: {}", static_cast<int>(result));
            return false;
        }
        return true;
    }

    bool FVulkanVideoEncoder::CreateCommandResources()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = device_.VideoEncodeFamilyIndex();
        if (vkCreateCommandPool(device_.Handle(), &poolInfo, nullptr, &commandPool_) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video command pool");
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_.Handle(), &allocInfo, &commandBuffer_) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to allocate Vulkan Video command buffer");
            return false;
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device_.Handle(), &fenceInfo, nullptr, &encodeFence_) != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video encode fence");
            return false;
        }
        return true;
    }

    bool FVulkanVideoEncoder::LoadHeaders()
    {
        VkVideoEncodeH264SessionParametersGetInfoKHR h264GetInfo{};
        h264GetInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_GET_INFO_KHR;
        h264GetInfo.writeStdSPS = VK_TRUE;
        h264GetInfo.writeStdPPS = VK_TRUE;
        h264GetInfo.stdSPSId = sps_.seq_parameter_set_id;
        h264GetInfo.stdPPSId = pps_.pic_parameter_set_id;

        VkVideoEncodeSessionParametersGetInfoKHR getInfo{};
        getInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_GET_INFO_KHR;
        getInfo.pNext = &h264GetInfo;
        getInfo.videoSessionParameters = sessionParameters_;

        VkVideoEncodeH264SessionParametersFeedbackInfoKHR h264Feedback{};
        h264Feedback.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_FEEDBACK_INFO_KHR;

        VkVideoEncodeSessionParametersFeedbackInfoKHR feedbackInfo{};
        feedbackInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_SESSION_PARAMETERS_FEEDBACK_INFO_KHR;
        feedbackInfo.pNext = &h264Feedback;

        size_t headerSize = 0;
        VkResult result = device_.GetDeviceProcedures().vkGetEncodedVideoSessionParametersKHR(
            device_.Handle(), &getInfo, &feedbackInfo, &headerSize, nullptr);
        if (result != VK_SUCCESS || headerSize == 0)
        {
            SPDLOG_ERROR("RemotePlay: failed to query Vulkan Video SPS/PPS size: {}", static_cast<int>(result));
            return false;
        }

        parameterSetsAnnexb_.resize(headerSize);
        result = device_.GetDeviceProcedures().vkGetEncodedVideoSessionParametersKHR(
            device_.Handle(), &getInfo, &feedbackInfo, &headerSize, parameterSetsAnnexb_.data());
        if (result != VK_SUCCESS)
        {
            SPDLOG_ERROR("RemotePlay: failed to fetch Vulkan Video SPS/PPS payload: {}", static_cast<int>(result));
            parameterSetsAnnexb_.clear();
            return false;
        }
        parameterSetsAnnexb_.resize(headerSize);
        return true;
    }

    ERateControlMode FVulkanVideoEncoder::UpdateRateControlState()
    {
        if ((caps_.rateControlModes & VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR) != 0)
        {
            return ERateControlMode::Vbr;
        }
        if ((caps_.rateControlModes & VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR) != 0)
        {
            return ERateControlMode::Cbr;
        }
        return ERateControlMode::Disabled;
    }

    bool FVulkanVideoEncoder::CopyI420ToNv12(const FI420View& view)
    {
        if (!stagingBuffer_.mapped)
        {
            return false;
        }

        const uint32_t width = std::min(view.width, codedWidth_);
        const uint32_t height = std::min(view.height, codedHeight_);
        const size_t lumaPlaneSize = static_cast<size_t>(codedWidth_) * codedHeight_;
        std::memset(stagingBuffer_.mapped, 16, lumaPlaneSize);
        std::memset(stagingBuffer_.mapped + lumaPlaneSize, 128, lumaPlaneSize / 2u);

        for (uint32_t y = 0; y < height; ++y)
        {
            std::memcpy(stagingBuffer_.mapped + static_cast<size_t>(y) * codedWidth_,
                        view.y + static_cast<size_t>(y) * view.strideY, width);
        }

        uint8_t* uvPlane = stagingBuffer_.mapped + lumaPlaneSize;
        const uint32_t chromaWidth = width / 2u;
        const uint32_t chromaHeight = height / 2u;
        for (uint32_t y = 0; y < chromaHeight; ++y)
        {
            uint8_t* dstRow = uvPlane + static_cast<size_t>(y) * codedWidth_;
            const uint8_t* srcU = view.u + static_cast<size_t>(y) * view.strideC;
            const uint8_t* srcV = view.v + static_cast<size_t>(y) * view.strideC;
            for (uint32_t x = 0; x < chromaWidth; ++x)
            {
                dstRow[x * 2u + 0u] = srcU[x];
                dstRow[x * 2u + 1u] = srcV[x];
            }
        }
        return true;
    }

    bool FVulkanVideoEncoder::RecordAndSubmitEncode(FGpuVideoFrame* gpuFrame, uint64_t timestampMs,
                                                    std::vector<std::byte>& outFrame, bool& keyframe)
    {
        (void)timestampMs;
        if (!ResetAndBeginCommandBuffer(commandBuffer_))
        {
            return false;
        }

        vkCmdResetQueryPool(commandBuffer_, queryPool_, 0, 1);

        VkImage srcImage = inputImage_.image;
        VkImageView srcView = inputImage_.view;
        VkImageLayout* srcLayout = &inputImage_.layout;
        if (gpuFrame)
        {
            srcImage = gpuFrame->image;
            srcView = gpuFrame->view;
            srcLayout = gpuFrame->layout;
        }

        if (!gpuFrame)
        {
            InsertImageBarrier(commandBuffer_, srcImage, 0, VK_ACCESS_TRANSFER_WRITE_BIT, *srcLayout,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
            *srcLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

            VkBufferImageCopy copyRegions[2]{};
            copyRegions[0].bufferOffset = 0;
            copyRegions[0].bufferRowLength = codedWidth_;
            copyRegions[0].bufferImageHeight = codedHeight_;
            copyRegions[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
            copyRegions[0].imageSubresource.layerCount = 1;
            copyRegions[0].imageExtent = {config_.width, config_.height, 1};

            copyRegions[1].bufferOffset = static_cast<VkDeviceSize>(codedWidth_) * codedHeight_;
            copyRegions[1].bufferRowLength = codedWidth_ / 2u;
            copyRegions[1].bufferImageHeight = codedHeight_ / 2u;
            copyRegions[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
            copyRegions[1].imageSubresource.layerCount = 1;
            copyRegions[1].imageExtent = {config_.width / 2u, config_.height / 2u, 1};

            vkCmdCopyBufferToImage(commandBuffer_, stagingBuffer_.buffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   2, copyRegions);

            InsertImageBarrier(commandBuffer_, srcImage, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
                               VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            *srcLayout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
        }
        else if (srcLayout && *srcLayout != VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR)
        {
            InsertImageBarrier(commandBuffer_, srcImage,
                               VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, 0, *srcLayout,
                               VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
                               VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT,
                               VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            *srcLayout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
        }

        const bool emitKeyframe = forceKeyframe_ || !supportsInterFrames_ || lastReferenceSlotIndex_ < 0;
        const bool usePreviousReference =
            supportsInterFrames_ && !emitKeyframe && lastReferenceSlotIndex_ >= 0 && dpbStates_[lastReferenceSlotIndex_].valid;
        const bool storeAsReference = supportsInterFrames_;
        const int32_t setupSlotIndex = storeAsReference ? (lastReferenceSlotIndex_ == 0 ? 1 : 0) : -1;
        const uint32_t frameNumMask = (1u << (sps_.log2_max_frame_num_minus4 + 4u)) - 1u;
        const uint32_t currentFrameNum = frameNum_ & frameNumMask;
        ++frameNum_;
        const int32_t currentPicOrderCnt = static_cast<int32_t>(pictureOrderCount_);
        pictureOrderCount_ += 2;

        StdVideoH264PictureType currentPictureType =
            emitKeyframe ? STD_VIDEO_H264_PICTURE_TYPE_IDR : STD_VIDEO_H264_PICTURE_TYPE_P;
        StdVideoH264SliceType currentSliceType =
            emitKeyframe ? STD_VIDEO_H264_SLICE_TYPE_I : STD_VIDEO_H264_SLICE_TYPE_P;

        VkVideoPictureResourceInfoKHR setupPictureResource{};
        VkVideoReferenceSlotInfoKHR setupReferenceSlot{};
        setupReferenceSlot.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
        VkVideoEncodeH264DpbSlotInfoKHR setupDpbSlotInfo{};
        setupDpbSlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR;
        StdVideoEncodeH264ReferenceInfo setupReferenceInfo{};

        VkVideoPictureResourceInfoKHR previousPictureResource{};
        VkVideoReferenceSlotInfoKHR previousReferenceSlot{};
        previousReferenceSlot.sType = VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR;
        VkVideoEncodeH264DpbSlotInfoKHR previousDpbSlotInfo{};
        previousDpbSlotInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_DPB_SLOT_INFO_KHR;
        StdVideoEncodeH264ReferenceInfo previousReferenceInfo{};

        std::array<VkVideoReferenceSlotInfoKHR, dpbSlotCount> beginReferenceSlots{};
        uint32_t beginReferenceSlotCount = 0;

        if (storeAsReference)
        {
            FAllocatedImage& setupImage = dpbImages_[setupSlotIndex];
            if (setupImage.layout != VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR)
            {
                InsertImageBarrier(commandBuffer_, setupImage.image, 0, 0, setupImage.layout,
                                   VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR,
                                   VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
                setupImage.layout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR;
            }

            setupReferenceInfo.primary_pic_type = currentPictureType;
            setupReferenceInfo.FrameNum = currentFrameNum;
            setupReferenceInfo.PicOrderCnt = currentPicOrderCnt;

            setupDpbSlotInfo.pStdReferenceInfo = &setupReferenceInfo;

            setupPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
            setupPictureResource.codedExtent = {config_.width, config_.height};
            setupPictureResource.baseArrayLayer = 0;
            setupPictureResource.imageViewBinding = setupImage.view;

            setupReferenceSlot.pNext = &setupDpbSlotInfo;
            setupReferenceSlot.slotIndex = setupSlotIndex;
            setupReferenceSlot.pPictureResource = &setupPictureResource;
            beginReferenceSlots[beginReferenceSlotCount++] = setupReferenceSlot;
        }

        if (usePreviousReference)
        {
            const FDpbSlotState& previousState = dpbStates_[lastReferenceSlotIndex_];
            const FAllocatedImage& previousImage = dpbImages_[lastReferenceSlotIndex_];

            previousReferenceInfo.primary_pic_type = previousState.pictureType;
            previousReferenceInfo.FrameNum = previousState.frameNum;
            previousReferenceInfo.PicOrderCnt = previousState.picOrderCnt;
            previousDpbSlotInfo.pStdReferenceInfo = &previousReferenceInfo;

            previousPictureResource.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
            previousPictureResource.codedExtent = {config_.width, config_.height};
            previousPictureResource.baseArrayLayer = 0;
            previousPictureResource.imageViewBinding = previousImage.view;

            previousReferenceSlot.pNext = &previousDpbSlotInfo;
            previousReferenceSlot.slotIndex = lastReferenceSlotIndex_;
            previousReferenceSlot.pPictureResource = &previousPictureResource;
            beginReferenceSlots[beginReferenceSlotCount++] = previousReferenceSlot;
        }

        VkVideoBeginCodingInfoKHR beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR;
        beginInfo.videoSession = session_;
        beginInfo.videoSessionParameters = sessionParameters_;
        beginInfo.referenceSlotCount = beginReferenceSlotCount;
        beginInfo.pReferenceSlots = beginReferenceSlotCount != 0 ? beginReferenceSlots.data() : nullptr;
        device_.GetDeviceProcedures().vkCmdBeginVideoCodingKHR(commandBuffer_, &beginInfo);

        const ERateControlMode rateControlMode = UpdateRateControlState();
        if (rateControlDirty_ || firstControlCommand_)
        {
            const uint64_t targetBitrate = std::max<uint64_t>(1u, config_.bitrateKbps) * 1000u;
            const uint64_t cappedMaxBitrate = caps_.maxBitrate != 0 ? caps_.maxBitrate : targetBitrate * 2u;
            const uint64_t burstBitrate =
                std::min<uint64_t>(cappedMaxBitrate, std::max<uint64_t>(targetBitrate + targetBitrate / 2u,
                                                                        targetBitrate + 1000u * 1000u));

            VkVideoEncodeRateControlLayerInfoKHR rateLayerInfo{};
            rateLayerInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_LAYER_INFO_KHR;
            rateLayerInfo.averageBitrate = targetBitrate;
            rateLayerInfo.maxBitrate =
                rateControlMode == ERateControlMode::Vbr ? burstBitrate : targetBitrate;
            rateLayerInfo.frameRateNumerator = std::max(1u, config_.fps);
            rateLayerInfo.frameRateDenominator = 1;

            VkVideoEncodeH264RateControlLayerInfoKHR h264RateLayerInfo{};
            h264RateLayerInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_LAYER_INFO_KHR;
            h264RateLayerInfo.useMinQp = VK_TRUE;
            h264RateLayerInfo.minQp = {caps_.minQp, caps_.minQp, caps_.minQp};
            h264RateLayerInfo.useMaxQp = VK_TRUE;
            h264RateLayerInfo.maxQp = {caps_.maxQp, caps_.maxQp, caps_.maxQp};
            rateLayerInfo.pNext = &h264RateLayerInfo;

            VkVideoEncodeH264RateControlInfoKHR h264RateControl{};
            h264RateControl.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_RATE_CONTROL_INFO_KHR;
            h264RateControl.flags = caps_.preferredRateControlFlags;
            if (h264RateControl.flags == 0)
            {
                h264RateControl.flags = VK_VIDEO_ENCODE_H264_RATE_CONTROL_REGULAR_GOP_BIT_KHR |
                                        VK_VIDEO_ENCODE_H264_RATE_CONTROL_REFERENCE_PATTERN_FLAT_BIT_KHR;
            }
            h264RateControl.gopFrameCount =
                caps_.preferredGopFrameCount != 0 ? caps_.preferredGopFrameCount
                                                  : std::max(30u, std::max(1u, config_.fps) * 2u);
            h264RateControl.idrPeriod =
                caps_.preferredIdrPeriod != 0 ? caps_.preferredIdrPeriod : h264RateControl.gopFrameCount;
            h264RateControl.consecutiveBFrameCount = 0;
            h264RateControl.temporalLayerCount = 1;

            VkVideoEncodeRateControlInfoKHR rateControl{};
            rateControl.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_RATE_CONTROL_INFO_KHR;
            rateControl.pNext = &h264RateControl;
            rateControl.rateControlMode =
                rateControlMode == ERateControlMode::Vbr ? VK_VIDEO_ENCODE_RATE_CONTROL_MODE_VBR_BIT_KHR
                : rateControlMode == ERateControlMode::Cbr ? VK_VIDEO_ENCODE_RATE_CONTROL_MODE_CBR_BIT_KHR
                                                           : VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
            rateControl.layerCount = 1;
            rateControl.pLayers = &rateLayerInfo;
            rateControl.virtualBufferSizeInMs = rateControlMode == ERateControlMode::Vbr ? 1500 : 1000;
            rateControl.initialVirtualBufferSizeInMs = rateControl.virtualBufferSizeInMs / 2u;

            VkVideoEncodeQualityLevelInfoKHR qualityLevelInfo{};
            qualityLevelInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_QUALITY_LEVEL_INFO_KHR;
            qualityLevelInfo.qualityLevel = caps_.preferredQualityLevel;
            if (caps_.preferredQualityLevel != UINT32_MAX)
            {
                qualityLevelInfo.pNext = &rateControl;
            }

            VkVideoCodingControlInfoKHR controlInfo{};
            controlInfo.sType = VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR;
            controlInfo.pNext = caps_.preferredQualityLevel != UINT32_MAX ? static_cast<const void*>(&qualityLevelInfo)
                                                                          : static_cast<const void*>(&rateControl);
            controlInfo.flags = VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR;
            if (caps_.preferredQualityLevel != UINT32_MAX)
            {
                controlInfo.flags |= VK_VIDEO_CODING_CONTROL_ENCODE_QUALITY_LEVEL_BIT_KHR;
            }
            if (firstControlCommand_)
            {
                controlInfo.flags |= VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;
            }
            device_.GetDeviceProcedures().vkCmdControlVideoCodingKHR(commandBuffer_, &controlInfo);
            SPDLOG_INFO("RemotePlay: Vulkan Video rate control mode={} avg={}kbps max={}kbps gop={} idr={} quality={}",
                        RateControlModeName(rateControlMode), targetBitrate / 1000u, rateLayerInfo.maxBitrate / 1000u,
                        h264RateControl.gopFrameCount, h264RateControl.idrPeriod,
                        caps_.preferredQualityLevel != UINT32_MAX
                            ? std::to_string(caps_.preferredQualityLevel)
                            : std::string("default"));
            rateControlDirty_ = false;
            firstControlCommand_ = false;
        }
        forceKeyframe_ = false;

        StdVideoEncodeH264ReferenceListsInfo stdReferenceListsInfo{};
        std::memset(stdReferenceListsInfo.RefPicList0, STD_VIDEO_H264_NO_REFERENCE_PICTURE,
                    sizeof(stdReferenceListsInfo.RefPicList0));
        std::memset(stdReferenceListsInfo.RefPicList1, STD_VIDEO_H264_NO_REFERENCE_PICTURE,
                    sizeof(stdReferenceListsInfo.RefPicList1));
        if (usePreviousReference)
        {
            stdReferenceListsInfo.num_ref_idx_l0_active_minus1 = 0;
            stdReferenceListsInfo.RefPicList0[0] = static_cast<uint8_t>(lastReferenceSlotIndex_);
        }

        StdVideoEncodeH264PictureInfo stdPictureInfo{};
        stdPictureInfo.flags.IdrPicFlag = emitKeyframe ? VK_TRUE : VK_FALSE;
        stdPictureInfo.flags.is_reference = storeAsReference ? VK_TRUE : VK_FALSE;
        stdPictureInfo.seq_parameter_set_id = sps_.seq_parameter_set_id;
        stdPictureInfo.pic_parameter_set_id = pps_.pic_parameter_set_id;
        stdPictureInfo.idr_pic_id = emitKeyframe ? idrPicId_++ : 0;
        stdPictureInfo.primary_pic_type = currentPictureType;
        stdPictureInfo.frame_num = currentFrameNum;
        stdPictureInfo.PicOrderCnt = currentPicOrderCnt;
        stdPictureInfo.pRefLists = usePreviousReference ? &stdReferenceListsInfo : nullptr;

        StdVideoEncodeH264SliceHeader stdSliceHeader{};
        stdSliceHeader.slice_type = currentSliceType;
        stdSliceHeader.cabac_init_idc = STD_VIDEO_H264_CABAC_INIT_IDC_0;
        stdSliceHeader.disable_deblocking_filter_idc = STD_VIDEO_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLED;

        VkVideoEncodeH264NaluSliceInfoKHR sliceInfo{};
        sliceInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_NALU_SLICE_INFO_KHR;
        sliceInfo.constantQp = rateControlMode == ERateControlMode::Disabled
                                   ? std::clamp(emitKeyframe ? 22 : 24, caps_.minQp, caps_.maxQp)
                                   : 0;
        sliceInfo.pStdSliceHeader = &stdSliceHeader;

        VkVideoEncodeH264PictureInfoKHR h264PictureInfo{};
        h264PictureInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_KHR;
        h264PictureInfo.naluSliceEntryCount = 1;
        h264PictureInfo.pNaluSliceEntries = &sliceInfo;
        h264PictureInfo.pStdPictureInfo = &stdPictureInfo;

        VkVideoPictureResourceInfoKHR srcPicture{};
        srcPicture.sType = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
        srcPicture.codedExtent = {config_.width, config_.height};
        srcPicture.baseArrayLayer = 0;
        srcPicture.imageViewBinding = srcView;

        VkVideoEncodeInfoKHR encodeInfo{};
        encodeInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR;
        encodeInfo.pNext = &h264PictureInfo;
        encodeInfo.dstBuffer = bitstreamBuffer_.buffer;
        encodeInfo.dstBufferOffset = 0;
        encodeInfo.dstBufferRange = bitstreamBuffer_.size;
        encodeInfo.srcPictureResource = srcPicture;
        encodeInfo.pSetupReferenceSlot = storeAsReference ? &setupReferenceSlot : nullptr;
        encodeInfo.referenceSlotCount = usePreviousReference ? 1u : 0u;
        encodeInfo.pReferenceSlots = usePreviousReference ? &previousReferenceSlot : nullptr;

        vkCmdBeginQuery(commandBuffer_, queryPool_, 0, 0);
        device_.GetDeviceProcedures().vkCmdEncodeVideoKHR(commandBuffer_, &encodeInfo);
        vkCmdEndQuery(commandBuffer_, queryPool_, 0);

        VkVideoEndCodingInfoKHR endInfo{};
        endInfo.sType = VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR;
        device_.GetDeviceProcedures().vkCmdEndVideoCodingKHR(commandBuffer_, &endInfo);

        if (!EndSubmitAndWait(commandBuffer_))
        {
            return false;
        }

        FEncodeFeedbackResult feedback{};
        const VkResult queryResult =
            vkGetQueryPoolResults(device_.Handle(), queryPool_, 0, 1, sizeof(feedback), &feedback, sizeof(feedback),
                                  VK_QUERY_RESULT_WITH_STATUS_BIT_KHR | VK_QUERY_RESULT_WAIT_BIT);
        if (queryResult != VK_SUCCESS || feedback.status != VK_QUERY_RESULT_STATUS_COMPLETE_KHR)
        {
            SPDLOG_WARN("RemotePlay: Vulkan Video query failed result={} status={}", static_cast<int>(queryResult),
                        static_cast<int>(feedback.status));
            return false;
        }

        if (!bitstreamBuffer_.mapped || feedback.bitstreamBytes == 0 ||
            feedback.bitstreamOffset + feedback.bitstreamBytes > bitstreamBuffer_.size)
        {
            return false;
        }

        const size_t headerBytes = emitKeyframe ? parameterSetsAnnexb_.size() : 0;
        outFrame.resize(headerBytes + feedback.bitstreamBytes);
        if (headerBytes != 0)
        {
            std::memcpy(outFrame.data(), parameterSetsAnnexb_.data(), headerBytes);
        }
        std::memcpy(outFrame.data() + headerBytes, bitstreamBuffer_.mapped + feedback.bitstreamOffset,
                    feedback.bitstreamBytes);

        if (storeAsReference)
        {
            dpbStates_[setupSlotIndex].valid = true;
            dpbStates_[setupSlotIndex].frameNum = currentFrameNum;
            dpbStates_[setupSlotIndex].picOrderCnt = currentPicOrderCnt;
            dpbStates_[setupSlotIndex].pictureType = currentPictureType;
            lastReferenceSlotIndex_ = setupSlotIndex;
        }

        keyframe = emitKeyframe;
        return true;
    }

    uint32_t FVulkanVideoEncoder::AlignUp(uint32_t value, uint32_t alignment) const
    {
        if (alignment <= 1)
        {
            return value;
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    VkDeviceSize FVulkanVideoEncoder::AlignUp(VkDeviceSize value, VkDeviceSize alignment) const
    {
        if (alignment <= 1)
        {
            return value;
        }
        return ((value + alignment - 1) / alignment) * alignment;
    }

    uint32_t FVulkanVideoEncoder::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memoryProperties);

        uint32_t fallbackIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
        {
            if ((typeBits & (1u << i)) == 0)
            {
                continue;
            }
            const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
            if ((flags & properties) == properties)
            {
                return i;
            }
            if (fallbackIndex == UINT32_MAX)
            {
                fallbackIndex = i;
            }
        }

        if (fallbackIndex != UINT32_MAX)
        {
            return fallbackIndex;
        }
        Throw(std::runtime_error("failed to find suitable Vulkan Video memory type"));
    }

    bool FVulkanVideoEncoder::AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties,
                                             VkDeviceMemory& memory) const
    {
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(requirements.memoryTypeBits, properties);
        return vkAllocateMemory(device_.Handle(), &allocInfo, nullptr, &memory) == VK_SUCCESS;
    }

    void FVulkanVideoEncoder::DestroyBuffer(FAllocatedBuffer& buffer)
    {
        if (buffer.mapped)
        {
            vkUnmapMemory(device_.Handle(), buffer.memory);
            buffer.mapped = nullptr;
        }
        if (buffer.buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device_.Handle(), buffer.buffer, nullptr);
            buffer.buffer = VK_NULL_HANDLE;
        }
        if (buffer.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_.Handle(), buffer.memory, nullptr);
            buffer.memory = VK_NULL_HANDLE;
        }
        buffer.size = 0;
    }

    void FVulkanVideoEncoder::DestroyImage(FAllocatedImage& image)
    {
        if (image.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device_.Handle(), image.view, nullptr);
            image.view = VK_NULL_HANDLE;
        }
        if (image.image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device_.Handle(), image.image, nullptr);
            image.image = VK_NULL_HANDLE;
        }
        if (image.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device_.Handle(), image.memory, nullptr);
            image.memory = VK_NULL_HANDLE;
        }
        image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void FVulkanVideoEncoder::DestroySessionMemory()
    {
        for (FSessionMemoryBinding& binding : sessionMemory_)
        {
            if (binding.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device_.Handle(), binding.memory, nullptr);
                binding.memory = VK_NULL_HANDLE;
            }
        }
        sessionMemory_.clear();
    }

    bool FVulkanVideoEncoder::ResetAndBeginCommandBuffer(VkCommandBuffer& commandBuffer)
    {
        if (vkResetFences(device_.Handle(), 1, &encodeFence_) != VK_SUCCESS)
        {
            return false;
        }
        if (vkResetCommandPool(device_.Handle(), commandPool_, 0) != VK_SUCCESS)
        {
            return false;
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        return vkBeginCommandBuffer(commandBuffer, &beginInfo) == VK_SUCCESS;
    }

    bool FVulkanVideoEncoder::EndSubmitAndWait(VkCommandBuffer commandBuffer)
    {
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        if (vkQueueSubmit(device_.VideoEncodeQueue(), 1, &submitInfo, encodeFence_) != VK_SUCCESS)
        {
            return false;
        }
        return vkWaitForFences(device_.Handle(), 1, &encodeFence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    }

    void FVulkanVideoEncoder::FillSessionParameters()
    {
        sps_ = {};
        pps_ = {};

        sps_.profile_idc = h264ProfileInfo_.stdProfileIdc;
        sps_.level_idc = SelectLevel(config_.width, config_.height, config_.fps);
        sps_.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
        sps_.seq_parameter_set_id = 0;
        sps_.log2_max_frame_num_minus4 = 4;
        sps_.pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_2;
        sps_.max_num_ref_frames = supportsInterFrames_ ? 1 : 0;
        sps_.pic_width_in_mbs_minus1 = codedWidth_ / 16u - 1u;
        sps_.pic_height_in_map_units_minus1 = codedHeight_ / 16u - 1u;
        sps_.flags.constraint_set0_flag = true;
        sps_.flags.constraint_set1_flag = true;
        sps_.flags.direct_8x8_inference_flag = true;
        sps_.flags.frame_mbs_only_flag = true;

        if (codedWidth_ != config_.width || codedHeight_ != config_.height)
        {
            sps_.flags.frame_cropping_flag = true;
            sps_.frame_crop_right_offset = (codedWidth_ - config_.width) / 2u;
            sps_.frame_crop_bottom_offset = (codedHeight_ - config_.height) / 2u;
        }

        pps_.seq_parameter_set_id = sps_.seq_parameter_set_id;
        pps_.pic_parameter_set_id = 0;
        pps_.weighted_bipred_idc = STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_DEFAULT;
        pps_.num_ref_idx_l0_default_active_minus1 = supportsInterFrames_ ? 0 : 0;
        pps_.flags.deblocking_filter_control_present_flag = true;
        pps_.flags.entropy_coding_mode_flag = false;
    }
}
