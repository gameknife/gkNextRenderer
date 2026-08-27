#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include <fmt/printf.h>
#include <glm/mat4x4.hpp>

namespace Utilities
{
    namespace Math
    {
        // Vulkan reverse-Z projection with a finite far plane. The returned matrix uses the
        // engine's usual unflipped Y convention; callers that render to Vulkan still apply the
        // existing Projection[1][1] *= -1 adjustment where required.
        inline glm::mat4 ReverseZPerspective(
            float fovRadians, float aspect, float nearPlane, float farPlane)
        {
            const float safeAspect = std::max(aspect, 1.0e-6f);
            const float safeNear = std::max(nearPlane, 1.0e-4f);
            const float safeFar = std::max(farPlane, safeNear + 1.0e-4f);
            const float tanHalfFov = std::max(std::tan(fovRadians * 0.5f), 1.0e-6f);
            const float depthRange = safeFar - safeNear;

            glm::mat4 projection(0.0f);
            projection[0][0] = 1.0f / (safeAspect * tanHalfFov);
            projection[1][1] = 1.0f / tanHalfFov;
            projection[2][2] = safeNear / depthRange;
            projection[2][3] = -1.0f;
            projection[3][2] = safeNear * safeFar / depthRange;
            return projection;
        }

        static uint32_t GetSafeDispatchCount( uint32_t size, uint32_t divider )
        {
            return size % divider == 0 ? size / divider : size / divider + 1;
        }

        static int32_t floorToInt(float value)
        {
            return static_cast<int32_t>(std::floor(value));
        }

        static int32_t ceilToInt(float value)
        {
            return static_cast<int32_t>(std::ceil(value));
        }
    }

    static std::string metricFormatter(double value, std::string unit, int kilo = 1000) //if pass data as (void*)"b" - show info like kb, Mb, Gb
    {
        static double      s_value[] = { static_cast<double>(kilo * kilo * kilo), static_cast<double>(kilo * kilo), static_cast<double>(kilo), 1, 1.f / static_cast<double>(kilo), 1.f / static_cast<double>(kilo * kilo), 1.f / static_cast<double>(kilo * kilo * kilo) };
        static const char* s_prefix[] = { "G", "M", "k", "", "m", "u", "n" };

        constexpr int s_valueSZ = sizeof(s_value) / sizeof(double);

        if (value < s_value[s_valueSZ - 1])
        {
            return fmt::sprintf("%.0f", value);
        }

        if (value < 10001.f)
        {
            return fmt::sprintf("%.0f", value);
        }

        for (int i = 0; i < s_valueSZ; ++i)
        {
            if (fabs(value) >= s_value[i])
            {
                return fmt::sprintf("%.2f%s%s", value / s_value[i], s_prefix[i], unit);
            }
        }
        return fmt::sprintf("%.2f%s%s", value / s_value[6], s_prefix[6], unit);
    }
}
