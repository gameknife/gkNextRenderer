#pragma once

#include "AirportSimTypes.h"

namespace Assets
{
    class Scene;
}

namespace AirportSim
{
    // 世界时钟 + 日夜光照 + 班次窗口。所有系统以游戏分钟为单位。
    class TimeSystem
    {
    public:
        void Reset();
        // 推进时钟并把日夜曲线写进 scene.GetEnvSettings()。
        void Tick(double deltaRealSeconds, Assets::Scene& scene);

        // 调试面板跳时段。
        void Skip(double minutes) { gameMinutes_ += minutes; }

        double GameMinutes() const { return gameMinutes_; }            // 累计（跨天递增）
        double DayMinutes() const;                                     // 当日 0..1440
        int DayIndex() const;
        float& TimeScaleRef() { return timeScale_; }
        bool& PausedRef() { return paused_; }
        float DaylightFactor() const { return daylight_; }
        bool IsNight() const { return daylight_ < 0.1f; }

        // 班次在岗窗口（夜班跨午夜）。
        bool IsShiftActive(EShift shift) const { return IsShiftActiveAt(shift, DayMinutes()); }
        static bool IsShiftActiveAt(EShift shift, double dayMinutes);
        // 班次的通勤生成时刻是否已到（开始前 20 分钟）。
        static bool IsCommuteWindow(EShift shift, double dayMinutes);
        static double ShiftEndMinutes(EShift shift);

    private:
        void ApplyEnvironment(Assets::Scene& scene);

        double gameMinutes_ = 0.0;
        float  timeScale_ = 2.0f;
        bool   paused_ = false;
        float  daylight_ = 0.0f;
    };
}
