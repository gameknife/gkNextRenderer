#pragma once

// ============================================================================
// TimeSystem.hpp - Day/night cycle for NextDayz, adapted from AirportSim. Sweeps
// the scene sun rotation/intensity by game hour and supports a single "overcast"
// weather toggle that dims sun + sky. No rain particles (MVP scope).
// ============================================================================

#include "Application/Game/NextDayz/NextDayzConfig.hpp"

namespace Assets
{
    class Scene;
}

namespace NextDayz
{
    class TimeSystem
    {
    public:
        void Reset(const FTimeConfig& config);

        void Tick(double deltaRealSeconds, Assets::Scene& scene);
        void Skip(double gameMinutes) { gameMinutes_ += gameMinutes; }

        double GameMinutes() const { return gameMinutes_; }
        double Hour() const;               // 0..24
        int HourInt() const;
        int MinuteInt() const;
        double& TimeScaleRef() { return timeScale_; }
        double TimeScale() const { return timeScale_; }

        bool Overcast() const { return overcast_; }
        void SetOvercast(bool overcast) { overcast_ = overcast; }
        void ToggleOvercast() { overcast_ = !overcast_; }

    private:
        void ApplyEnvironment(Assets::Scene& scene);

        FTimeConfig config_{};
        double gameMinutes_ = 8.0 * 60.0;
        double timeScale_ = 60.0;
        bool overcast_ = false;
        float daylight_ = 1.0f;
    };
}
