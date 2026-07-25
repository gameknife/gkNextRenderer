#include "CityTimeSystem.h"

#include "CitySolSimConfig.hpp"

#include <algorithm>
#include <array>
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

        template <typename T, size_t N>
        T SampleCurve(float hour, const std::array<std::pair<float, T>, N>& keys)
        {
            for (size_t index = 1; index < keys.size(); ++index)
            {
                if (hour <= keys[index].first)
                {
                    const auto& previous = keys[index - 1];
                    const auto& next = keys[index];
                    const float span = std::max(next.first - previous.first, 0.001f);
                    const float t = CitySmoothStep(0.0f, 1.0f, (hour - previous.first) / span);
                    return previous.second + (next.second - previous.second) * t;
                }
            }
            return keys.back().second;
        }

        glm::vec3 SampleSunColor(float hour)
        {
            constexpr std::array keys = {
                std::pair{0.0f, glm::vec3(1.00f, 0.30f, 0.08f)},
                std::pair{5.2f, glm::vec3(1.00f, 0.25f, 0.06f)},
                std::pair{6.2f, glm::vec3(1.00f, 0.48f, 0.18f)},
                std::pair{8.2f, glm::vec3(1.00f, 0.86f, 0.68f)},
                std::pair{12.5f, glm::vec3(1.00f, 0.97f, 0.91f)},
                std::pair{16.5f, glm::vec3(1.00f, 0.88f, 0.68f)},
                std::pair{18.4f, glm::vec3(1.00f, 0.30f, 0.07f)},
                std::pair{24.0f, glm::vec3(1.00f, 0.30f, 0.08f)},
            };
            return SampleCurve(hour, keys);
        }

        glm::vec3 SampleSkyColor(float hour)
        {
            constexpr std::array keys = {
                std::pair{0.0f, glm::vec3(0.34f, 0.22f, 0.62f)},
                std::pair{4.6f, glm::vec3(0.28f, 0.19f, 0.55f)},
                std::pair{5.4f, glm::vec3(0.55f, 0.30f, 0.62f)},
                std::pair{6.3f, glm::vec3(1.00f, 0.53f, 0.40f)},
                std::pair{7.8f, glm::vec3(1.00f, 0.84f, 0.72f)},
                std::pair{10.0f, glm::vec3(1.00f, 0.98f, 0.96f)},
                std::pair{16.2f, glm::vec3(1.00f, 0.96f, 0.90f)},
                std::pair{17.7f, glm::vec3(1.00f, 0.67f, 0.40f)},
                std::pair{18.7f, glm::vec3(0.76f, 0.38f, 0.52f)},
                std::pair{20.2f, glm::vec3(0.39f, 0.25f, 0.64f)},
                std::pair{24.0f, glm::vec3(0.34f, 0.22f, 0.62f)},
            };
            return SampleCurve(hour, keys);
        }

        float SampleSkyIntensity(float hour)
        {
            constexpr std::array keys = {
                std::pair{0.0f, 12.0f},  std::pair{4.7f, 12.0f},
                std::pair{5.5f, 24.0f},  std::pair{6.4f, 55.0f},
                std::pair{8.0f, 92.0f},  std::pair{12.0f, 105.0f},
                std::pair{16.5f, 95.0f}, std::pair{17.8f, 62.0f},
                std::pair{18.8f, 30.0f}, std::pair{20.3f, 14.0f},
                std::pair{24.0f, 12.0f},
            };
            return SampleCurve(hour, keys);
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
        const float sunrise = CitySmoothStep(5.0f, 7.2f, hour);
        const float sunset = 1.0f - CitySmoothStep(17.2f, 19.4f, hour);
        daylight_ = std::min(sunrise, sunset);
        lightsOn_ = daylight_ < 0.25f;

        Assets::EnvironmentSetting& env = scene.GetEnvSettings();
        env.HasSky = true;
        env.HasSun = daylight_ > 0.015f;
        const float solarProgress = std::clamp((hour - 5.2f) / 13.6f, 0.0f, 1.0f);
        const float sunArc = std::sin(solarProgress * glm::pi<float>());
        env.SunRotation = solarProgress;
        env.SunElevation = glm::radians(2.0f + 56.0f * std::max(sunArc, 0.0f));
        env.SkyRotation = std::fmod(hour / 24.0f + 0.18f, 1.0f);
        env.SunColor = SampleSunColor(hour);
        env.SkyColor = SampleSkyColor(hour);
        env.SunIntensity = 260.0f * daylight_ * (0.28f + 0.72f * std::max(sunArc, 0.0f));
        env.SkyIntensity = SampleSkyIntensity(hour);
    }
}
