#pragma once

#include <algorithm>
#include <string_view>

#include "StudioSimTypes.h"

namespace StudioSim
{
    struct FMeterSnapshot
    {
        const char* key = "";
        const char* label = "";
        float value = 0.0f;
        float target = 0.0f;
    };

    inline const char* ProjectMeterLabelZh(std::string_view meter)
    {
        if (meter == "tech") return "技术";
        if (meter == "design") return "玩法";
        if (meter == "art") return "美术";
        if (meter == "polish") return "品质";
        return "短板";
    }

    inline float MeterCompletion(const FMeterSnapshot& meter)
    {
        return meter.target > 0.0f ? std::clamp(meter.value / meter.target, 0.0f, 1.0f) : 1.0f;
    }

    inline FMeterSnapshot WeakestMeter(const FProjectState& project)
    {
        FMeterSnapshot meters[] = {
            {"tech", ProjectMeterLabelZh("tech"), project.meters.tech, project.targetMeters.tech},
            {"design", ProjectMeterLabelZh("design"), project.meters.design, project.targetMeters.design},
            {"art", ProjectMeterLabelZh("art"), project.meters.art, project.targetMeters.art},
            {"polish", ProjectMeterLabelZh("polish"), project.meters.polish, project.targetMeters.polish},
        };

        FMeterSnapshot weakest = meters[0];
        for (const auto& meter : meters)
        {
            if (MeterCompletion(meter) < MeterCompletion(weakest))
            {
                weakest = meter;
            }
        }
        return weakest;
    }
}
