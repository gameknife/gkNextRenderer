#pragma once

#include "Engine/Utilities/Glm.hpp"

#include <deque>
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
        bool isStable = false;
        double poseTimeSeconds = 0.0;
        double predictionSeconds = 0.0;
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
        bool Update(const FHeadPose& pose, double deltaSeconds, float smoothingHz = 0.0f);
        bool Recenter();
        std::optional<glm::quat> RelativeOrientation() const;
        glm::mat4 BuildModelView(const glm::mat4& baseModelView, float worldUnitsPerMeter = 1.0f) const;

    private:
        std::optional<FHeadPose> originPose_;
        std::optional<FHeadPose> currentPose_;
        std::deque<FHeadPose> inputHistory_;
    };

    std::unique_ptr<IHeadPoseTracker> CreateHeadPoseTracker(bool enableSixDof = true,
                                                             double predictionSeconds = 0.020);

} // namespace Modules::Viture
