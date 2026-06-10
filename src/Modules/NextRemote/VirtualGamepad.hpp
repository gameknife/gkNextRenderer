#pragma once

#include <array>
#include <cstdint>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>

namespace Runtime::Remote
{
    class FVirtualGamepad final
    {
    public:
        FVirtualGamepad() = default;
        ~FVirtualGamepad();

        bool Start();
        void Stop();
        void ApplyState(const std::array<int16_t, 6>& axes, uint32_t buttonMask);

    private:
        SDL_JoystickID joystickId_ = 0;
        SDL_Joystick* joystick_ = nullptr;
    };
}
