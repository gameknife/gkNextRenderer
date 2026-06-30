#pragma once

#include <string>

#include <fmt/format.h>

namespace AirportSim
{
    inline std::string FormatLatency(double elapsedMs)
    {
        if (elapsedMs < 1000.0)
        {
            return fmt::format("{:.0f} ms", elapsedMs);
        }
        return fmt::format("{:.2f} s", elapsedMs / 1000.0);
    }
}
