#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/InputRouter.hpp"

#include "Engine/Runtime/RemoteProtocol.hpp"

#include <array>
#include <cstring>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_keyboard.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    namespace
    {
        template <typename T>
        bool ReadValue(const std::byte* data, size_t size, size_t& offset, T& value)
        {
            if (offset + sizeof(T) > size)
            {
                return false;
            }
            std::memcpy(&value, data + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        void PushEvent(SDL_Event& event)
        {
            event.common.timestamp = SDL_GetTicksNS();
            if (!SDL_PushEvent(&event))
            {
                SPDLOG_WARN("RemotePlay: SDL_PushEvent failed: {}", SDL_GetError());
            }
        }
    }

    FInputRouter::~FInputRouter()
    {
        Stop();
    }

    void FInputRouter::Stop()
    {
        virtualGamepad_.Stop();
    }

    void FInputRouter::HandleBinaryMessage(const std::vector<std::byte>& message)
    {
        if (message.empty())
        {
            return;
        }

        const auto type = static_cast<ERemoteInputMessage>(message[0]);
        const std::byte* data = message.data() + 1;
        const size_t size = message.size() - 1;
        size_t offset = 0;

        switch (type)
        {
        case ERemoteInputMessage::Key:
            {
                uint8_t down = 0;
                uint8_t repeat = 0;
                uint16_t scancode = 0;
                uint16_t mod = 0;
                if (ReadValue(data, size, offset, down) && ReadValue(data, size, offset, repeat) &&
                    ReadValue(data, size, offset, scancode) && ReadValue(data, size, offset, mod))
                {
                    PushKey(down != 0, scancode, mod, repeat != 0);
                }
                break;
            }
        case ERemoteInputMessage::MouseMove:
            {
                uint8_t mode = 0;
                float x = 0.0f;
                float y = 0.0f;
                if (ReadValue(data, size, offset, mode) && ReadValue(data, size, offset, x) &&
                    ReadValue(data, size, offset, y))
                {
                    PushMouseMove(mode, x, y);
                }
                break;
            }
        case ERemoteInputMessage::MouseButton:
            {
                uint8_t down = 0;
                uint8_t button = 0;
                float x = 0.0f;
                float y = 0.0f;
                if (ReadValue(data, size, offset, down) && ReadValue(data, size, offset, button) &&
                    ReadValue(data, size, offset, x) && ReadValue(data, size, offset, y))
                {
                    PushMouseButton(down != 0, button, x, y);
                }
                break;
            }
        case ERemoteInputMessage::Wheel:
            {
                float x = 0.0f;
                float y = 0.0f;
                if (ReadValue(data, size, offset, x) && ReadValue(data, size, offset, y))
                {
                    PushWheel(x, y);
                }
                break;
            }
        case ERemoteInputMessage::Gamepad:
            ApplyGamepad(data, size);
            break;
        default:
            break;
        }
    }

    void FInputRouter::HandleTextMessage(const std::string& message)
    {
        try
        {
            const nlohmann::json json = nlohmann::json::parse(message);
            if (json.value("type", "") == "key")
            {
                PushKey(json.value("down", false), json.value("scancode", 0), json.value("mod", 0),
                        json.value("repeat", false));
            }
        }
        catch (const std::exception& error)
        {
            SPDLOG_WARN("RemotePlay: invalid input json: {}", error.what());
        }
    }

    void FInputRouter::PushKey(bool down, uint16_t scancodeValue, uint16_t modValue, bool repeat)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.type = static_cast<SDL_EventType>(event.type);
        event.key.scancode = static_cast<SDL_Scancode>(scancodeValue);
        event.key.mod = static_cast<SDL_Keymod>(modValue);
        event.key.key = SDL_GetKeyFromScancode(event.key.scancode, event.key.mod, true);
        event.key.down = down;
        event.key.repeat = repeat;
        PushEvent(event);
    }

    void FInputRouter::PushMouseMove(uint8_t mode, float x, float y)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.type = static_cast<SDL_EventType>(event.type);
        if (mode == static_cast<uint8_t>(ERemoteMouseMoveMode::Relative))
        {
            event.motion.which = remoteMouseId;
            event.motion.xrel = x;
            event.motion.yrel = y;
        }
        else
        {
            event.motion.x = x;
            event.motion.y = y;
        }
        PushEvent(event);
    }

    void FInputRouter::PushMouseButton(bool down, uint8_t button, float x, float y)
    {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.type = static_cast<SDL_EventType>(event.type);
        event.button.button = button;
        event.button.down = down;
        event.button.x = x;
        event.button.y = y;
        PushEvent(event);
    }

    void FInputRouter::PushWheel(float x, float y)
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.type = static_cast<SDL_EventType>(event.type);
        event.wheel.x = x;
        event.wheel.y = y;
        PushEvent(event);
    }

    void FInputRouter::ApplyGamepad(const std::byte* data, size_t size)
    {
        size_t offset = 0;
        std::array<int16_t, 6> axes{};
        uint32_t buttonMask = 0;
        for (int16_t& axis : axes)
        {
            if (!ReadValue(data, size, offset, axis))
            {
                return;
            }
        }
        if (!ReadValue(data, size, offset, buttonMask))
        {
            return;
        }
        virtualGamepad_.ApplyState(axes, buttonMask);
    }
}
