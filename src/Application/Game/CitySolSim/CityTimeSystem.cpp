#include "CityTimeSystem.h"

#include "CitySolSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace CitySolSim
{
    namespace
    {
        float CitySmoothStep(float begin, float end, float value)
        {
            const float t = std::clamp((value - begin) / (end - begin), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    void CityTimeSystem::Reset(bool agentValidation)
    {
        gameMinutes_ = (agentValidation ? 8.25 : 6.25) * 60.0;
        timeScale_ = agentValidation ? Config::kDefaultTimeScale * 4.0f : Config::kDefaultTimeScale;
        daylight_ = 0.5f;
        paused_ = false;
        lightsOn_ = false;
    }

    double CityTimeSystem::DayMinutes() const
    {
        const double minutes = std::fmod(gameMinutes_, 24.0 * 60.0);
        return minutes < 0.0 ? minutes + 24.0 * 60.0 : minutes;
    }

    int CityTimeSystem::DayIndex() const
    {
        return static_cast<int>(std::floor(gameMinutes_ / (24.0 * 60.0)));
    }

    void CityTimeSystem::Skip(double minutes)
    {
        gameMinutes_ = std::max(0.0, gameMinutes_ + minutes);
    }

    void CityTimeSystem::Tick(double deltaRealSeconds, Assets::Scene& scene)
    {
        if (!paused_)
        {
            gameMinutes_ += std::max(0.0, deltaRealSeconds) * static_cast<double>(timeScale_);
        }
        ApplyEnvironment(scene);
    }

    void CityTimeSystem::ApplyEnvironment(Assets::Scene& scene)
    {
        const float hour = static_cast<float>(DayMinutes() / 60.0);
        const float sunrise = CitySmoothStep(5.0f, 7.0f, hour);
        const float sunset = 1.0f - CitySmoothStep(17.5f, 19.5f, hour);
        daylight_ = std::min(sunrise, sunset);
        lightsOn_ = daylight_ < 0.25f;

        Assets::EnvironmentSetting& env = scene.GetEnvSettings();
        env.HasSky = true;
        env.HasSun = daylight_ > 0.015f;
        env.SunRotation = std::clamp((hour - 5.5f) / 13.0f, 0.0f, 1.0f);
        env.SkyRotation = std::fmod(hour / 24.0f + 0.18f, 1.0f);
        env.SunIntensity = 500.0f * daylight_;
        env.SkyIntensity = 15.0f + 85.0f * daylight_;
    }
}
