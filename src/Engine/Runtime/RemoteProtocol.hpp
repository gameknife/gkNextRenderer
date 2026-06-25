#pragma once

#include <cstdint>

namespace Runtime::Remote
{
    inline constexpr uint32_t remoteMouseId = 0x474B5250u; // "GKRP"

    enum class ERemoteInputMessage : uint8_t
    {
        Key = 1,
        MouseMove = 2,
        MouseButton = 3,
        Wheel = 4,
        Gamepad = 5,
        RequestKeyframe = 6,
        SetBitrate = 7,
    };

    enum class ERemoteMouseMoveMode : uint8_t
    {
        Relative = 0,
        Absolute = 1,
    };
}
