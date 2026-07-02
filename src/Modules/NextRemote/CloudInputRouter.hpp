#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Runtime::Remote
{
    struct FCloudInputEvent
    {
        enum class EType : uint8_t
        {
            Key,
            MouseMove,
            MouseButton,
            Wheel,
            Gamepad,
        };

        EType type = EType::Key;
        bool down = false;
        bool repeat = false;
        uint8_t mode = 0;
        uint8_t button = 0;
        uint16_t scancode = 0;
        uint16_t mod = 0;
        float x = 0.0f;
        float y = 0.0f;
        std::array<int16_t, 6> axes{};
        uint32_t buttonMask = 0;
    };

    class FCloudInputRouter final
    {
    public:
        void EnqueueBinaryMessage(const std::string& sessionId, const std::vector<std::byte>& message);
        void EnqueueTextMessage(const std::string& sessionId, const std::string& message);
        std::vector<FCloudInputEvent> Drain(const std::string& sessionId);
        void ClearSession(const std::string& sessionId);
        void Clear();

    private:
        void Enqueue(const std::string& sessionId, FCloudInputEvent event);

        std::mutex mutex_;
        std::unordered_map<std::string, std::vector<FCloudInputEvent>> pendingEvents_;
    };
}
