#include "Modules/NextViture/VitureModule.hpp"

#include "Engine/Common/CoreMinimal.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

#include "viture_device_carina.h"
#include "viture_glasses_provider.h"
#include "viture_result.h"

#include <algorithm>
#include <cmath>

namespace Modules::Viture
{

namespace
{

constexpr uint16_t vitureVendorId = 0x35CA;

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
    explicit FVitureHeadPoseTracker(const bool enableSixDof)
        : enableSixDof_(enableSixDof)
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
        SPDLOG_INFO("VITURE AR: Carina {}DOF tracker started", enableSixDof_ ? 6 : 3);
        return true;
    }

    void Stop() override
    {
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
    }

    std::optional<FHeadPose> PollPose() override
    {
        if (!started_)
        {
            return std::nullopt;
        }

        float pose[7]{};
        int ignoredTrackingStatus = 0;
        if (xr_device_provider_get_gl_pose_carina(handle_, pose, 0.0, &ignoredTrackingStatus) != VITURE_GLASSES_SUCCESS)
        {
            return std::nullopt;
        }

        const glm::quat orientation(pose[3], pose[4], pose[5], pose[6]);
        const float orientationLength = glm::length(orientation);
        if (orientationLength <= 0.0001f)
        {
            return std::nullopt;
        }

        return FHeadPose{
            .positionMeters = glm::vec3(pose[0], pose[1], pose[2]),
            .orientation = glm::normalize(orientation),
            .isTracked = true,
        };
    }

    std::string_view Name() const override { return "VITURE Carina"; }
    std::string_view Status() const override { return status_; }

private:
    static void StateCallback(int /*stateId*/, int /*value*/) {}
    static void CameraCallback(char* /*left0*/, char* /*right0*/, char* /*left1*/, char* /*right1*/,
                               double /*timestamp*/, int /*width*/, int /*height*/) {}

    XRDeviceProviderHandle handle_ = nullptr;
    bool started_ = false;
    bool initialized_ = false;
    bool enableSixDof_ = true;
    std::string status_{"Not started"};
};

} // namespace

bool FHeadTrackingCamera::Update(const FHeadPose& pose, const double deltaSeconds, const float smoothingHz)
{
    if (!pose.isTracked)
    {
        return false;
    }

    if (!originPose_.has_value())
    {
        originPose_ = pose;
        currentPose_ = pose;
        return true;
    }

    const float clampedDelta = static_cast<float>(glm::clamp(deltaSeconds, 0.0, 0.1));
    const float clampedSmoothingHz = std::max(smoothingHz, 0.0f);
    const float blend = clampedSmoothingHz > 0.0f
        ? 1.0f - std::exp(-2.0f * glm::pi<float>() * clampedSmoothingHz * clampedDelta)
        : 1.0f;
    const FHeadPose previous = currentPose_.value_or(pose);
    currentPose_ = pose;
    currentPose_->positionMeters = glm::mix(previous.positionMeters, pose.positionMeters, blend);
    currentPose_->orientation = glm::normalize(glm::slerp(previous.orientation, pose.orientation, blend));
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

std::unique_ptr<IHeadPoseTracker> CreateHeadPoseTracker(const bool enableSixDof)
{
    return std::make_unique<FVitureHeadPoseTracker>(enableSixDof);
}

} // namespace Modules::Viture
