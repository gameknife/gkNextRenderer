#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>

class NextGameInstanceBase;

namespace TruckerDemo
{
    struct FTruckHudState
    {
        float speedKmh = 0.0f;
        float rpm = 0.0f;
        float maxRPM = 3000.0f;
        float throttle = 0.0f;
        float brake = 0.0f;
        float fuel = 0.0f;
        float fuelCapacity = 1.0f;
        float slopeDegrees = 0.0f;
        float targetDistance = 0.0f;
        float elapsed = 0.0f;
        int gear = 0;
        bool handbrake = false;
        bool diffLock = false;
        bool allWheelDrive = false;
        bool recoverySuggested = false;
        bool missionPanel = false;
        bool missionAvailable = false;
        bool missionComplete = false;
        const char* mission = "available";
        const char* surface = "grass";
        glm::vec3 targetPosition{0.0f};
    };

    struct FTruckHudActions
    {
        bool acceptMission = false;
        bool restartMission = false;
    };

    class FTruckHud
    {
    public:
        FTruckHudActions Draw(const NextGameInstanceBase& game, const FTruckHudState& state) const;
    };
}
