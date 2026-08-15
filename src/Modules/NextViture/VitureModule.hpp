#pragma once

#include "Engine/Utilities/Glm.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace Modules::Viture
{
    struct FHeadPose
    {
        glm::vec3 positionMeters{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        bool isTracked = false;
    };

    class IHeadPoseTracker
    {
    public:
        virtual ~IHeadPoseTracker() = default;

        virtual bool Start() = 0;
        virtual void Stop() = 0;
        virtual std::optional<FHeadPose> PollPose() = 0;
        virtual std::string_view Name() const = 0;
        virtual std::string_view Status() const = 0;
    };

    class FHeadTrackingCamera final
    {
    public:
        bool Update(const FHeadPose& pose, double deltaSeconds, float smoothingHz = 30.0f);
        bool Recenter();
        glm::mat4 BuildModelView(const glm::mat4& baseModelView, float worldUnitsPerMeter = 1.0f) const;

    private:
        std::optional<FHeadPose> originPose_;
        std::optional<FHeadPose> currentPose_;
    };

    std::unique_ptr<IHeadPoseTracker> CreateHeadPoseTracker(bool enableSixDof = true);

} // namespace Modules::Viture
