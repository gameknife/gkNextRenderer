#include "Modules/NextViture/VitureModule.hpp"

#include "Engine/Common/CoreMinimal.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include "viture_device_carina.h"
#include "viture_glasses_provider.h"
#include "viture_result.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace Modules::Viture
{

namespace
{

constexpr uint16_t vitureVendorId = 0x35CA;

double SteadyClockSeconds()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

bool ReadUsbProperty(const io_service_t service, const char* key, uint16_t& outValue)
{
    CFStringRef keyString = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
    if (keyString == nullptr)
    {
        return false;
    }

    const CFTypeRef property = IORegistryEntryCreateCFProperty(service, keyString, kCFAllocatorDefault, 0);
    CFRelease(keyString);
    if (property == nullptr)
    {
        return false;
    }

    const bool read = CFGetTypeID(property) == CFNumberGetTypeID() &&
        CFNumberGetValue(static_cast<CFNumberRef>(property), kCFNumberShortType, &outValue);
    CFRelease(property);
    return read;
}

std::optional<int> FindVitureProductId()
{
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (match == nullptr)
    {
        return std::nullopt;
    }

    io_iterator_t iterator = 0;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iterator) != KERN_SUCCESS || iterator == 0)
    {
        return std::nullopt;
    }

    std::optional<int> productId;
    while (const io_service_t service = IOIteratorNext(iterator))
    {
        uint16_t vendorId = 0;
        uint16_t candidateProductId = 0;
        if (ReadUsbProperty(service, kUSBVendorID, vendorId) &&
            ReadUsbProperty(service, kUSBProductID, candidateProductId) &&
            vendorId == vitureVendorId &&
            xr_device_provider_is_product_id_valid(static_cast<int>(candidateProductId)) != 0)
        {
            productId = static_cast<int>(candidateProductId);
            IOObjectRelease(service);
            break;
        }
        IOObjectRelease(service);
    }
    IOObjectRelease(iterator);
    return productId;
}

class FVitureHeadPoseTracker final : public IHeadPoseTracker
{
public:
    explicit FVitureHeadPoseTracker(const bool enableSixDof, const double predictionSeconds)
        : enableSixDof_(enableSixDof), predictionSeconds_(std::max(predictionSeconds, 0.0))
    {
    }

    ~FVitureHeadPoseTracker() override { Stop(); }

    bool Start() override
    {
        Stop();
        const std::optional<int> productId = FindVitureProductId();
        if (!productId.has_value())
        {
            status_ = "No connected VITURE glasses were found";
            return false;
        }

        handle_ = xr_device_provider_create(*productId);
        if (handle_ == nullptr)
        {
            status_ = "The VITURE SDK could not create a device provider";
            return false;
        }

        if (xr_device_provider_get_device_type(handle_) != XR_DEVICE_TYPE_VITURE_CARINA)
        {
            status_ = "6DOF AR requires a VITURE Carina device (Luma Ultra)";
            Stop();
            return false;
        }

        if (xr_device_provider_set_dof_type_carina(handle_, enableSixDof_ ? 1 : 0) != VITURE_GLASSES_SUCCESS ||
            xr_device_provider_initialize(handle_, nullptr, nullptr) != VITURE_GLASSES_SUCCESS)
        {
            status_ = "The VITURE Carina tracker failed to initialise";
            Stop();
            return false;
        }
        initialized_ = true;

        if (xr_device_provider_register_state_callback(handle_, &FVitureHeadPoseTracker::StateCallback) !=
                VITURE_GLASSES_SUCCESS ||
            xr_device_provider_register_callbacks_carina(handle_, nullptr, nullptr, nullptr,
                &FVitureHeadPoseTracker::CameraCallback) != VITURE_GLASSES_SUCCESS ||
            xr_device_provider_start(handle_) != VITURE_GLASSES_SUCCESS)
        {
            status_ = "The VITURE Carina tracker failed to start";
            Stop();
            return false;
        }

        started_ = true;
        status_ = enableSixDof_ ? "6DOF tracking active" : "3DOF tracking active";
        pollRunning_ = true;
        pollThread_ = std::thread(&FVitureHeadPoseTracker::PollLoop, this);
        SPDLOG_INFO("VITURE AR: Carina {}DOF tracker started", enableSixDof_ ? 6 : 3);
        return true;
    }

    void Stop() override
    {
        pollRunning_ = false;
        if (pollThread_.joinable())
        {
            pollThread_.join();
        }

        if (handle_ != nullptr)
        {
            if (started_)
            {
                xr_device_provider_stop(handle_);
            }
            if (initialized_)
            {
                xr_device_provider_shutdown(handle_);
            }
            xr_device_provider_destroy(handle_);
            handle_ = nullptr;
        }
        started_ = false;
        initialized_ = false;
        std::scoped_lock lock(poseMutex_);
        latestPose_.reset();
    }

    std::optional<FHeadPose> PollPose() override
    {
        std::scoped_lock lock(poseMutex_);
        return latestPose_;
    }

    std::string_view Name() const override { return "VITURE Carina"; }
    std::string_view Status() const override { return status_; }

private:
    // Carina's camera-backed pose callback is documented as 25 Hz. Poll at the
    // same cadence so the render thread interpolates actual SDK samples rather
    // than repeatedly timestamping the same camera pose at 120 Hz.
    static constexpr std::chrono::microseconds pollInterval{40000};

    bool ReadPose(const double queryTimeSeconds)
    {
        if (!started_ || handle_ == nullptr)
        {
            return false;
        }

        float pose[7]{};
        int poseStatus = 1;
        if (xr_device_provider_get_gl_pose_carina(handle_, pose, predictionSeconds_, &poseStatus) !=
            VITURE_GLASSES_SUCCESS)
        {
            return false;
        }

        const glm::quat orientation(pose[3], pose[4], pose[5], pose[6]);
        const float orientationLength = glm::length(orientation);
        if (orientationLength <= 0.0001f)
        {
            return false;
        }

        std::scoped_lock lock(poseMutex_);
        latestPose_ = FHeadPose{
            .positionMeters = glm::vec3(pose[0], pose[1], pose[2]),
            .orientation = glm::normalize(orientation),
            .isTracked = true,
            .isStable = poseStatus == 0,
            .poseTimeSeconds = queryTimeSeconds + predictionSeconds_,
            .predictionSeconds = predictionSeconds_,
        };
        return true;
    }

    void PollLoop()
    {
        using Clock = std::chrono::steady_clock;
        auto nextPoll = Clock::now();
        while (pollRunning_)
        {
            if (!ReadPose(SteadyClockSeconds()))
            {
                std::scoped_lock lock(poseMutex_);
                latestPose_.reset();
            }

            nextPoll += pollInterval;
            const auto now = Clock::now();
            if (now > nextPoll + pollInterval)
            {
                nextPoll = now;
            }
            std::this_thread::sleep_until(nextPoll);
        }
    }

    static void StateCallback(int /*stateId*/, int /*value*/) {}
    static void CameraCallback(char* /*left0*/, char* /*right0*/, char* /*left1*/, char* /*right1*/,
                               double /*timestamp*/, int /*width*/, int /*height*/) {}

    XRDeviceProviderHandle handle_ = nullptr;
    bool started_ = false;
    bool initialized_ = false;
    bool enableSixDof_ = true;
    double predictionSeconds_ = 0.020;
    std::atomic<bool> pollRunning_{false};
    std::thread pollThread_;
    mutable std::mutex poseMutex_;
    std::optional<FHeadPose> latestPose_;
    std::string status_{"Not started"};
};

} // namespace

bool FHeadTrackingCamera::Update(const FHeadPose& pose, const double deltaSeconds, const float smoothingHz)
{
    if (!pose.isTracked)
    {
        return false;
    }

    if (inputHistory_.empty() || pose.poseTimeSeconds <= 0.0 ||
        pose.poseTimeSeconds > inputHistory_.back().poseTimeSeconds + 0.000001)
    {
        inputHistory_.push_back(pose);
        constexpr size_t maxInputHistorySamples = 8;
        while (inputHistory_.size() > maxInputHistorySamples)
        {
            inputHistory_.pop_front();
        }
    }

    FHeadPose renderPose = inputHistory_.back();
    if (inputHistory_.size() >= 2)
    {
        const FHeadPose& previousPose = inputHistory_[inputHistory_.size() - 2];
        const FHeadPose& latestPose = inputHistory_.back();
        const double sampleInterval = latestPose.poseTimeSeconds - previousPose.poseTimeSeconds;
        if (sampleInterval > 0.000001)
        {
            // A pure interpolator needs the next sample to be available before
            // it can render the interval. Keep exactly one 25 Hz sample in
            // reserve. The SDK prediction horizon is applied both to the query
            // and to this target time.
            const double interpolationDelaySeconds = sampleInterval;
            const double targetTimeSeconds = SteadyClockSeconds() +
                std::max(pose.predictionSeconds, 0.0) - interpolationDelaySeconds;

            auto upper = std::lower_bound(
                inputHistory_.begin(), inputHistory_.end(), targetTimeSeconds,
                [](const FHeadPose& sample, const double targetTime)
                {
                    return sample.poseTimeSeconds < targetTime;
                });

            if (upper == inputHistory_.begin())
            {
                renderPose = *upper;
            }
            else if (upper == inputHistory_.end())
            {
                renderPose = inputHistory_.back();
            }
            else
            {
                const FHeadPose& nextPose = *upper;
                const FHeadPose& previousInterpolatedPose = *(upper - 1);
                const double interval = nextPose.poseTimeSeconds - previousInterpolatedPose.poseTimeSeconds;
                const float alpha = static_cast<float>(std::clamp(
                    (targetTimeSeconds - previousInterpolatedPose.poseTimeSeconds) / interval, 0.0, 1.0));
                renderPose.positionMeters = glm::mix(
                    previousInterpolatedPose.positionMeters, nextPose.positionMeters, alpha);
                renderPose.orientation = glm::normalize(glm::slerp(
                    previousInterpolatedPose.orientation, nextPose.orientation, alpha));
                renderPose.isTracked = previousInterpolatedPose.isTracked && nextPose.isTracked;
                renderPose.isStable = previousInterpolatedPose.isStable && nextPose.isStable;
                renderPose.poseTimeSeconds = targetTimeSeconds;
            }
        }
    }

    if (!originPose_.has_value())
    {
        originPose_ = renderPose;
        currentPose_ = renderPose;
        return true;
    }

    if (smoothingHz > 0.0f)
    {
        const float clampedDelta = static_cast<float>(glm::clamp(deltaSeconds, 0.0, 0.1));
        const float blend = 1.0f - std::exp(-2.0f * glm::pi<float>() * smoothingHz * clampedDelta);
        const FHeadPose previous = currentPose_.value_or(renderPose);
        renderPose.positionMeters = glm::mix(previous.positionMeters, renderPose.positionMeters, blend);
        renderPose.orientation = glm::normalize(glm::slerp(previous.orientation, renderPose.orientation, blend));
    }
    currentPose_ = renderPose;
    return true;
}

bool FHeadTrackingCamera::Recenter()
{
    if (!currentPose_.has_value())
    {
        return false;
    }

    originPose_ = currentPose_;
    return true;
}

std::optional<glm::quat> FHeadTrackingCamera::RelativeOrientation() const
{
    if (!originPose_.has_value() || !currentPose_.has_value())
    {
        return std::nullopt;
    }

    return glm::normalize(glm::inverse(originPose_->orientation) * currentPose_->orientation);
}

glm::mat4 FHeadTrackingCamera::BuildModelView(const glm::mat4& baseModelView,
                                               const float worldUnitsPerMeter) const
{
    if (!originPose_.has_value() || !currentPose_.has_value())
    {
        return baseModelView;
    }

    const FHeadPose& origin = *originPose_;
    const FHeadPose& current = *currentPose_;
    const glm::quat inverseOrigin = glm::inverse(origin.orientation);
    const glm::quat relativeOrientation = glm::normalize(inverseOrigin * current.orientation);
    const glm::vec3 relativePosition = inverseOrigin * (current.positionMeters - origin.positionMeters) *
        std::max(worldUnitsPerMeter, 0.0f);
    const glm::mat4 trackingDelta = glm::translate(glm::mat4(1.0f), relativePosition) *
        glm::mat4_cast(relativeOrientation);
    return glm::inverse(glm::inverse(baseModelView) * trackingDelta);
}

std::unique_ptr<IHeadPoseTracker> CreateHeadPoseTracker(const bool enableSixDof, const double predictionSeconds)
{
    return std::make_unique<FVitureHeadPoseTracker>(enableSixDof, predictionSeconds);
}

} // namespace Modules::Viture
