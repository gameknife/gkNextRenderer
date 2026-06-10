#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/VirtualGamepad.hpp"

#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    FVirtualGamepad::~FVirtualGamepad()
    {
        Stop();
    }

    bool FVirtualGamepad::Start()
    {
        if (joystick_)
        {
            return true;
        }

        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = 6;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) |
                         (1u << SDL_GAMEPAD_AXIS_RIGHTX) | (1u << SDL_GAMEPAD_AXIS_RIGHTY) |
                         (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) | (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        desc.button_mask = 0xffffffffu;
        desc.name = "gkNext Remote Gamepad";

        joystickId_ = SDL_AttachVirtualJoystick(&desc);
        if (joystickId_ == 0)
        {
            SPDLOG_WARN("RemotePlay: failed to attach virtual gamepad: {}", SDL_GetError());
            return false;
        }

        joystick_ = SDL_OpenJoystick(joystickId_);
        if (!joystick_)
        {
            SPDLOG_WARN("RemotePlay: failed to open virtual gamepad {}: {}", joystickId_, SDL_GetError());
            SDL_DetachVirtualJoystick(joystickId_);
            joystickId_ = 0;
            return false;
        }

        SPDLOG_INFO("RemotePlay: virtual gamepad attached id={}", joystickId_);
        return true;
    }

    void FVirtualGamepad::Stop()
    {
        if (joystick_)
        {
            SDL_CloseJoystick(joystick_);
            joystick_ = nullptr;
        }
        if (joystickId_ != 0)
        {
            SDL_DetachVirtualJoystick(joystickId_);
            joystickId_ = 0;
        }
    }

    void FVirtualGamepad::ApplyState(const std::array<int16_t, 6>& axes, uint32_t buttonMask)
    {
        if (!joystick_ && !Start())
        {
            return;
        }

        for (int axis = 0; axis < static_cast<int>(axes.size()); ++axis)
        {
            SDL_SetJoystickVirtualAxis(joystick_, axis, axes[static_cast<size_t>(axis)]);
        }
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
        {
            SDL_SetJoystickVirtualButton(joystick_, button, (buttonMask & (1u << button)) != 0u);
        }
    }
}
