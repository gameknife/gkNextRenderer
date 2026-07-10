#pragma once

#include <glm/glm.hpp>

namespace CitySolSim
{
    class CityTimeSystem;
    class CitizenSystem;
    class TrafficSystem;

    class CitySolSimUI
    {
    public:
        struct FState
        {
            int followVehicleId = -1;
            int followCitizenId = -1;
            bool showCitizenLabels = true;
            bool showHelp = true;
        };

        void Draw(const glm::mat4& viewProjection, const glm::vec3& cameraEye,
                  CityTimeSystem& time, const TrafficSystem& traffic,
                  const CitizenSystem& citizens);

        FState& State() { return state_; }
        const FState& State() const { return state_; }

    private:
        void DrawHud(CityTimeSystem& time, const TrafficSystem& traffic,
                     const CitizenSystem& citizens);
        void DrawSelection(const TrafficSystem& traffic, const CitizenSystem& citizens);
        void DrawWorldLabels(const glm::mat4& viewProjection, const glm::vec3& cameraEye,
                             const CitizenSystem& citizens) const;
        void FollowNextVehicle(const TrafficSystem& traffic);
        void FollowNextCitizen(const CitizenSystem& citizens);

        FState state_;
    };
}
