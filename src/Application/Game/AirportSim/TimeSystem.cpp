#include "TimeSystem.h"

#include "AirportSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"

namespace AirportSim
{
    namespace
    {
        // 平滑过渡：x 在 [a,b] 内 0→1。
        float SmoothStep(float a, float b, float x)
        {
            const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
    }

    void TimeSystem::Reset()
    {
        gameMinutes_ = Config::kDayStartMinutes;
        timeScale_ = Config::kDefaultTimeScale;
        paused_ = false;
        daylight_ = 0.0f;
    }

    double TimeSystem::DayMinutes() const
    {
        return std::fmod(gameMinutes_, 24.0 * 60.0);
    }

    int TimeSystem::DayIndex() const
    {
        return static_cast<int>(gameMinutes_ / (24.0 * 60.0));
    }

    void TimeSystem::Tick(double deltaRealSeconds, Assets::Scene& scene)
    {
        if (!paused_)
        {
            gameMinutes_ += deltaRealSeconds * static_cast<double>(timeScale_);
        }
        ApplyEnvironment(scene);
    }

    void TimeSystem::ApplyEnvironment(Assets::Scene& scene)
    {
        const float hour = static_cast<float>(DayMinutes()) / 60.0f;

        // 黎明 05:00–07:00 升、黄昏 17:00–19:00 落（§4.2）。
        const float rise = SmoothStep(5.0f, 7.0f, hour);
        const float set = 1.0f - SmoothStep(17.0f, 19.0f, hour);
        daylight_ = std::min(rise, set);

        auto& env = scene.GetEnvSettings();
        // 06:00→18:00 方位扫过半圈（东升西落）。
        env.SunRotation = std::clamp((hour - 6.0f) / 12.0f, 0.0f, 1.0f);
        env.HasSun = daylight_ > 0.02f;
        env.SunIntensity = Config::kDaySunIntensity * daylight_;
        env.SkyIntensity =
            Config::kDaySkyIntensity * (Config::kNightSkyFraction + (1.0f - Config::kNightSkyFraction) * daylight_);
    }

    bool TimeSystem::IsShiftActiveAt(EShift shift, double dayMinutes)
    {
        switch (shift)
        {
        case EShift::Morning: return dayMinutes >= Config::kMorningShiftStart && dayMinutes < Config::kMorningShiftEnd;
        case EShift::Evening: return dayMinutes >= Config::kMorningShiftEnd && dayMinutes < Config::kEveningShiftEnd;
        case EShift::AllDay:  return dayMinutes >= Config::kAllDayShiftStart && dayMinutes < Config::kEveningShiftEnd;
        case EShift::Night:   return dayMinutes >= Config::kNightShiftStart || dayMinutes < Config::kNightShiftEnd;
        default:              return false;
        }
    }

    bool TimeSystem::IsCommuteWindow(EShift shift, double dayMinutes)
    {
        double start = 0.0;
        switch (shift)
        {
        case EShift::Morning: start = Config::kMorningShiftStart; break;
        case EShift::Evening: start = Config::kMorningShiftEnd; break;
        case EShift::AllDay:  start = Config::kAllDayShiftStart; break;
        case EShift::Night:   start = Config::kNightShiftStart; break;
        default:              return false;
        }
        const double lead = start - Config::kCommuteLeadMinutes;
        return dayMinutes >= lead && !IsShiftActiveAt(shift, dayMinutes) &&
               dayMinutes < start + 1.0; // 班次开始后由 IsShiftActive 接管
    }

    double TimeSystem::ShiftEndMinutes(EShift shift)
    {
        switch (shift)
        {
        case EShift::Morning: return Config::kMorningShiftEnd;
        case EShift::Evening: return Config::kEveningShiftEnd;
        case EShift::AllDay:  return Config::kEveningShiftEnd;
        case EShift::Night:   return Config::kNightShiftEnd;
        default:              return 0.0;
        }
    }
}
