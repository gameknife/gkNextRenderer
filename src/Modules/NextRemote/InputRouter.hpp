#pragma once

#include "Modules/NextRemote/VirtualGamepad.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Runtime::Remote
{
    class FInputRouter final
    {
    public:
        FInputRouter() = default;
        ~FInputRouter();

        void Stop();
        void HandleBinaryMessage(const std::vector<std::byte>& message);
        void HandleTextMessage(const std::string& message);

    private:
        void PushKey(bool down, uint16_t scancodeValue, uint16_t modValue, bool repeat);
        void PushMouseMove(uint8_t mode, float x, float y);
        void PushMouseButton(bool down, uint8_t button, float x, float y);
        void PushWheel(float x, float y);
        void ApplyGamepad(const std::byte* data, size_t size);

        FVirtualGamepad virtualGamepad_;
    };
}
