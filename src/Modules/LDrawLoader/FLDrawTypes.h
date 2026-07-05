#pragma once

#include <cmath>

namespace Assets
{
    inline constexpr float defaultLDrawLduToWorldScale = 0.001f;

    struct LDrawLoadOptions
    {
        float lduToWorldScale = defaultLDrawLduToWorldScale;
        bool useLibraryPak = true;
    };

    inline float SanitizeLDrawLduToWorldScale(float scale)
    {
        if (!std::isfinite(scale) || scale <= 0.0f)
        {
            return defaultLDrawLduToWorldScale;
        }

        return scale;
    }
}
