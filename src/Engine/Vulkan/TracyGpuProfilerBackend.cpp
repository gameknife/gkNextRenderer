#include "Engine/Vulkan/TracyGpuProfilerBackend.hpp"
#include "Engine/Options.hpp"

namespace Vulkan
{
    TracyGpuProfilerBackend::TracyGpuProfilerBackend(
        const VkInstance instance, const Device& device, CommandPool& commandPool,
        const bool calibratedTimestampsAvailable)
        : device_(device), commandPool_(commandPool)
    {
#if GK_TRACY_ENABLED
        if (GOption == nullptr || !GOption->HardwareQuery)
        {
            return;
        }

        const auto queueFamilies = Vulkan::GetEnumerateVector(device_.PhysicalDevice(), vkGetPhysicalDeviceQueueFamilyProperties);
        if (device_.GraphicsFamilyIndex() >= queueFamilies.size() ||
            queueFamilies[device_.GraphicsFamilyIndex()].timestampValidBits == 0 ||
            device_.DeviceProperties().limits.timestampPeriod <= 0.0f)
        {
            SPDLOG_WARN("Tracy GPU profiling disabled: Vulkan timestamp queries are unavailable");
            return;
        }

        contextCommandBuffers_ = std::make_unique<CommandBuffers>(commandPool_, 1);
        const VkCommandBuffer initializationCommandBuffer = (*contextCommandBuffers_)[0];
        if (calibratedTimestampsAvailable)
        {
            const auto getCalibratableTimeDomains = reinterpret_cast<PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT"));
            const auto getCalibratedTimestamps = reinterpret_cast<PFN_vkGetCalibratedTimestampsEXT>(
                vkGetDeviceProcAddr(device_.Handle(), "vkGetCalibratedTimestampsEXT"));
            if (getCalibratableTimeDomains != nullptr && getCalibratedTimestamps != nullptr)
            {
                context_ = TracyVkContextCalibrated(
                    device_.PhysicalDevice(), device_.Handle(), device_.GraphicsQueue(),
                    initializationCommandBuffer, getCalibratableTimeDomains, getCalibratedTimestamps);
            }
        }
        if (context_ == nullptr)
        {
            context_ = TracyVkContext(
                device_.PhysicalDevice(), device_.Handle(), device_.GraphicsQueue(), initializationCommandBuffer);
        }

        if (context_ == nullptr)
        {
            SPDLOG_WARN("Tracy GPU profiling disabled: failed to create Vulkan context");
            contextCommandBuffers_.reset();
        }
#else
        (void)instance;
        (void)calibratedTimestampsAvailable;
#endif
    }

    TracyGpuProfilerBackend::~TracyGpuProfilerBackend()
    {
#if GK_TRACY_ENABLED
        if (context_ != nullptr)
        {
            TracyVkDestroy(context_);
            context_ = nullptr;
        }
        contextCommandBuffers_.reset();
#endif
    }

    void TracyGpuProfilerBackend::BeginFrame(const VkCommandBuffer commandBuffer)
    {
#if GK_TRACY_ENABLED
        scopes_.clear();
        if (context_ != nullptr)
        {
            // The command buffer is already in the recording state here. Tracy
            // requires collection at this point, not in EndFrame before the
            // engine has begun the next command buffer.
            TracyVkCollect(context_, commandBuffer);
        }
#else
        (void)commandBuffer;
#endif
    }

    void TracyGpuProfilerBackend::EndFrame(const VkCommandBuffer commandBuffer)
    {
        (void)commandBuffer;
    }

    uint32_t TracyGpuProfilerBackend::BeginScope(const VkCommandBuffer commandBuffer, const char* name)
    {
#if GK_TRACY_ENABLED
        if (context_ == nullptr)
        {
            return Runtime::FrameProfiler::invalidTimerId;
        }

        scopes_.emplace_back();
        ScopeSlot& slot = scopes_.back();
        const char* zoneName = name == nullptr ? "" : name;
        slot.zone.emplace(context_,
                          __LINE__,
                          __FILE__,
                          sizeof(__FILE__) - 1,
                          __FUNCTION__,
                          sizeof(__FUNCTION__) - 1,
                          zoneName,
                          std::strlen(zoneName),
                          commandBuffer,
                          true);
        return static_cast<uint32_t>(scopes_.size() - 1);
#else
        (void)commandBuffer;
        (void)name;
        return Runtime::FrameProfiler::invalidTimerId;
#endif
    }

    void TracyGpuProfilerBackend::EndScope(const VkCommandBuffer commandBuffer, const uint32_t scopeId)
    {
        (void)commandBuffer;
#if GK_TRACY_ENABLED
        if (scopeId < scopes_.size())
        {
            scopes_[scopeId].zone.reset();
        }
#else
        (void)scopeId;
#endif
    }

    float TracyGpuProfilerBackend::GetTime(const char* name) const
    {
        (void)name;
        return 0.0f;
    }

    std::vector<Runtime::ProfileTimerStat> TracyGpuProfilerBackend::FetchTimes(const int maxStack) const
    {
        (void)maxStack;
        return {};
    }
}
