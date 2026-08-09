#pragma once

namespace Assets
{
    class Scene;
}

namespace CitySolSim
{
    class CityTimeSystem
    {
    public:
        void Reset(bool agentValidation);
        void Tick(double deltaRealSeconds, Assets::Scene& scene);
        void Skip(double minutes);

        double GameMinutes() const { return gameMinutes_; }
        double DayMinutes() const;
        int DayIndex() const;
        float Daylight() const { return daylight_; }
        bool IsNight() const { return daylight_ < 0.22f; }
        bool LightsOn() const { return lightsOn_; }
        float& TimeScaleRef() { return timeScale_; }
        bool& PausedRef() { return paused_; }

    private:
        void ApplyEnvironment(Assets::Scene& scene);

        double gameMinutes_ = 0.0;
        float timeScale_ = 1.0f;
        float daylight_ = 1.0f;
        bool paused_ = false;
        bool lightsOn_ = false;
    };
}
