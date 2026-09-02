#pragma once

// ============================================================================
// MechanismCurves.hpp - The motion curves every mechanism is built from, as
// free functions with no state so Test_AstroMechanismCurves.cpp can pin them
// down without an engine.
// ============================================================================

#include <algorithm>
#include <cmath>

namespace NextAstrobot
{
    inline constexpr float kPi = 3.14159265358979323846f;

    /// Smoothstep, clamped to [0, 1].
    inline float Smoothstep01(float t)
    {
        const float x = std::clamp(t, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    /// 0 -> 1 -> 0 over one period, eased at both ends so a rail platform does not
    /// slam into a direction reversal. period <= 0 parks the curve at 0.
    inline float PingPong01(float t, float period)
    {
        if (!(period > 0.0f))
        {
            return 0.0f;
        }
        const float phase = std::fmod(std::fmod(t, period) + period, period) / period;
        const float triangle = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        return Smoothstep01(triangle);
    }

    /// amp * sin(2*pi*(t/period + phase01)): a pendulum's angle in degrees.
    inline float Swing(float t, float period, float phase01, float amp)
    {
        if (!(period > 0.0f))
        {
            return 0.0f;
        }
        return amp * std::sin(2.0f * kPi * (t / period + phase01));
    }

    /// Moves current toward target at rate units/second without ever overshooting.
    inline float Approach(float current, float target, float rate, float dt)
    {
        const float step = std::abs(rate) * std::max(dt, 0.0f);
        const float delta = target - current;
        if (std::abs(delta) <= step)
        {
            return target;
        }
        return current + (delta > 0.0f ? step : -step);
    }

    /// Exponential smoothing that is stable at any frame rate.
    inline float Damp(float current, float target, float lambda, float dt)
    {
        if (!(lambda > 0.0f))
        {
            return target;
        }
        return target + (current - target) * std::exp(-lambda * std::max(dt, 0.0f));
    }
}
